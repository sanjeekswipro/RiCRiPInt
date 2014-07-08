/** \file
 * \ingroup ps
 *
 * $HopeName: COREps!marking:src:shows.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1990-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Show operator variants for PostScript.
 */

#include "core.h"
#include "showops.h"

#include "swerrors.h"
#include "mm.h"
#include "mmcompat.h"
#include "objects.h"
#include "dicthash.h"
#include "fonth.h"
#include "namedef_.h"
#include "hqmemset.h"

#include "constant.h"     /* EPSILON */
#include "control.h"
#include "matrix.h"
#include "graphics.h"
#include "gs_color.h"     /* GSC_FILL */
#include "gstate.h"
#include "gu_path.h"
#include "pathops.h"
#include "gu_ctm.h"
#include "ndisplay.h"     /* for routedev.h */
#include "routedev.h"
#include "stacks.h"
#include "system.h"
#include "miscops.h"
#include "fcache.h"
#include "gu_cons.h"
#include "rectops.h"
#include "utils.h"
#include "display.h" /* dl_safe_recursion */
#include "vndetect.h"
#include "cidfont.h"
#include "cmap.h"
#include "charsel.h"
#include "swpdfout.h"
#include "params.h"
#include "idlom.h"
#include "plotops.h"
#include "lowmem.h" /* mm_memory_is_low */

#include "pscontext.h"

/*
   Exported variables
   ==================
*/
typedef struct SHOWARGS {
  int32 itemsrequired;
  int32 widthindex;
  int32 aindex;
  int32 stringindex;
  int32 boolindex;
  int32 xindex;
  int32 yindex;
  int32 procindex;
  int32 opname;
  int32 showtype;
} SHOWARGS;

static Bool doalltheshows(ps_context_t *pscontext, const SHOWARGS *args);
static Bool doalltheshows_internal(ps_context_t *pscontext, const SHOWARGS* args);
static Bool push_composite_font(int32 fontcode);
static Bool pop_composite_font(void);
static Bool create_fontinfo(OBJECT *newfonto);
static void delete_fontinfo(void);
static Bool descend_font_hierarchy(int32 fontcode);
static Bool select_subordinate_font(int32 fontcode,
                                    OBJECT *encoding,
                                    OBJECT *fdepvector,
                                    Bool do_push_composite) ;
static Bool concatenate_fontmatrix(void) ;
static Bool ps_notdef_show(char_selector_t *selector, int32 type, int32 charCount,
                           FVECTOR *advance, void *data) ;

#if defined( ASSERT_BUILD )
static int32 debug_composite ;
#endif

/** Since it specifically says you can't nest more than five of these, we
   could have an array, but why bother: just chain them together. */
typedef struct fontinfolist {
  FONTinfo info;
  struct fontinfolist * next;
  int32 fontcode;
} FONTINFOLIST;

#define theIFontStackInfo( _v ) ((_v)->info)
#define theIFontStackCode( _v ) ((_v)->fontcode)

/** As we descend the hierarchy of composite fonts, we keep the font information
   from the graphics state on this stack. We could do a gpush, but is seems
   a little extravagant when it is only the font information that's changing. */
static FONTINFOLIST * compositefontstack = NULL;

/** This is the rootfont to which this composite font hierarchy relates. If a
   recursive show is done with a new root, a new hierarchy will be created,
   and the old one restored at the end of the show. */
static OBJECT rootfont = OBJECT_NOTVM_NOTHING ;

/** Once we have seen a font, we keep hold of its infomation, since in a
   composite font the fonts switch continuously and the reason for not doing
   explicit setfont operations in the PostScript is to make this switching
   more efficient. */
static FONTINFOLIST * activefonts = NULL;

/* ---------------------------------------------------------------------- */
Bool show_(ps_context_t *pscontext)
{
  static const SHOWARGS args = {
    1, -1, -1, 0, -1, -1, -1, -1, NAME_show, DOSHOW
  };
  UNUSED_PARAM(ps_context_t *, pscontext) ;

/* See Red Book 2 page 520 */
  return doalltheshows(pscontext, &args) ;
}

Bool ashow_(ps_context_t *pscontext)
{
  static const SHOWARGS args = {
    3, -1,  2, 0, -1, -1, -1, -1, NAME_ashow, DOSHOW
  };
  UNUSED_PARAM(ps_context_t *, pscontext) ;

/* See Red Book 2 page 368 */
  return doalltheshows(pscontext, &args) ;
}

Bool widthshow_(ps_context_t *pscontext)
{
  static const SHOWARGS args = {
    4,  3, -1, 0, -1, -1, -1, -1, NAME_widthshow, DOSHOW
  };
  UNUSED_PARAM(ps_context_t *, pscontext) ;

/* See Red Book 2 page 549 */
  return doalltheshows(pscontext, &args) ;
}

Bool awidthshow_(ps_context_t *pscontext)
{
  static const SHOWARGS args = {
    6,  5,  2, 0, -1, -1, -1, -1, NAME_awidthshow, DOSHOW
  };
  UNUSED_PARAM(ps_context_t *, pscontext) ;

/* See Red Book 2 page 369 */
  return doalltheshows(pscontext, &args) ;
}

Bool xyshow_(ps_context_t *pscontext)
{
  static const SHOWARGS args = {
    2, -1, -1, 1, -1,  0,  0, -1, NAME_xyshow, DOSHOW
  };
  UNUSED_PARAM(ps_context_t *, pscontext) ;

/* See Red Book 2 page 553 */
  return doalltheshows(pscontext, &args) ;
}

Bool xshow_(ps_context_t *pscontext)
{
  static const SHOWARGS args = {
    2, -1, -1, 1, -1,  0, -1, -1, NAME_xshow, DOSHOW
  };
  UNUSED_PARAM(ps_context_t *, pscontext) ;

/* See Red Book 2 page 552 */
  return doalltheshows(pscontext, &args) ;
}

Bool yshow_(ps_context_t *pscontext)
{
  static const SHOWARGS args = {
    2, -1, -1, 1, -1, -1,  0, -1, NAME_yshow, DOSHOW
  };
  UNUSED_PARAM(ps_context_t *, pscontext) ;

/* See Red Book 2 page 553 */
  return doalltheshows(pscontext, &args) ;
}

Bool stringwidth_(ps_context_t *pscontext)
{
  static const SHOWARGS args = {
    1, -1, -1, 0, -1, -1, -1, -1, NAME_stringwidth, DOSTRINGWIDTH
  };
  UNUSED_PARAM(ps_context_t *, pscontext) ;

/* See Red Book 2 page 528 */
  return doalltheshows(pscontext, &args) ;
}

Bool cshow_(ps_context_t *pscontext)
{
  static const SHOWARGS args = {
    2, -1, -1, 0, -1, -1, -1,  1, NAME_cshow, DOSTRINGWIDTH
  };
  UNUSED_PARAM(ps_context_t *, pscontext) ;

/* See Red Book 2 page 381 */
  return doalltheshows(pscontext, &args) ;
}

Bool kshow_(ps_context_t *pscontext)
{
  static const SHOWARGS args = {
    2, -1, -1, 0, -1, -1, -1,  1, NAME_kshow, DOSHOW
  };
  UNUSED_PARAM(ps_context_t *, pscontext) ;

/* See Red Book 2 page 447 */
  return doalltheshows(pscontext, &args) ;
}

Bool charpath_(ps_context_t *pscontext)
{
  static const SHOWARGS args = {
    2, -1, -1, 1,  0, -1, -1, -1, NAME_charpath, DOCHARPATH
  };
  UNUSED_PARAM(ps_context_t *, pscontext) ;

/* See Red Book 2 page 447 */
  return doalltheshows(pscontext, &args) ;
}

/* ---------------------------------------------------------------------- */
Bool glyphshow_(ps_context_t *pscontext)
{
  OBJECT *nameobj ;
  OBJECT stringname = OBJECT_NOTVM_NOTHING ;
  int32 type;
  int32 fonttype;
  Bool  iscid = FALSE ;
  Bool success = FALSE ;
  SYSTEMVALUE startx ,starty ;
  char_selector_t selector = {0} ; /* Embedded OBJECTs are ISNOTVM */
  HDLTinfo savedHDLT = {0}; /* pacify compiler about initialization */

  if ( ! flush_vignette( VD_Default ))
    return FALSE ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  nameobj = theTop( operandstack );
  type = oType(*nameobj);
  if ( type == OSTRING ) {
    NAMECACHE *nptr ;
    if (( nptr = cachename( oString( *nameobj ), theLen(* nameobj )))
        == NULL )
      return FALSE ;
    oName( nnewobj ) = nptr ;
    Copy( & stringname, & nnewobj ) ;
    nameobj = & stringname ;
  }
  else if ( type != ONAME && type != OINTEGER )
    return error_handler( TYPECHECK ) ;

  if ( ! CurrentPoint )
    return error_handler( NOCURRENTPOINT ) ;
  startx = theX(theIPoint( CurrentPoint)) ;
  starty = theY(theIPoint( CurrentPoint)) ;

  if ( isHDLTEnabled(*gstateptr) ) {
    /* Save HDLT info, because we're not doing a gsave to preserve the current
       target. */
    savedHDLT = gstateptr->theHDLTinfo ;
    gstateptr->theHDLTinfo.next = &savedHDLT ;
    if ( !IDLOM_BEGINTEXT(NAME_glyphshow) )
      return FALSE ;
  }

#define return DO_NOT_RETURN_break_INSTEAD!

  textContextEnter();

  /* Single run 'do' block - allows us to break rather than return and always
   * exit the text context. */
  do {
    FVECTOR advance ;
    corecontext_t *context = ps_core_context(pscontext);

    if ( !DEVICE_SETG(context->page, GSC_FILL, DEVICE_SETG_NORMAL) )
      /* to avoid recursion when painting in a pattern */
      break ;

    fonttype = theFontType(theFontInfo(*gstateptr));
    if ( fonttype == FONTTYPE_0 ) {
      (void) error_handler( INVALIDFONT ) ;
      break ;
    } else if ( FONT_IS_CID(fonttype) ) {
      iscid = TRUE ;
      if ( type != OINTEGER ) {
        (void) error_handler( TYPECHECK ) ;
        break ;
      }
    } else { /* Font is neither CID nor Type0 */
      if ( type == OINTEGER
           && fonttype != FONTTYPE_TT
           && fonttype != FONTTYPE_CFF ) {  /* thanks to [60727] */
        (void) error_handler( TYPECHECK ) ;
        break ;
      }
    }

    if ( ! push( nameobj , &temporarystack ))
      break ;
    nameobj = theTop( temporarystack ) ;

    pop( & operandstack ) ;

    Copy(&selector.font, &theMyFont(theFontInfo(*gstateptr))) ;
    selector.index = -1 ;
    selector.type3cid = -1 ;

    if ( type == OINTEGER ) {
      charcontext_t * charcontext = pscontext->corecontext->charcontext ;

      selector.name = NULL ;
      selector.cid = oInteger(*nameobj) ;

      /* Spot recursive HDLT glyphshow for a symbolic PDF TT */
      if ( charcontext &&
           oType(charcontext->glyphname) == OINTEGER &&
           !iscid ) {
        selector.pdf = TRUE ;
      }

    } else {
      selector.name = oName(*nameobj) ;
      selector.cid = -1 ;
    }

    if ( ! plotchar(&selector, DOSHOW, 1, ps_notdef_show, NULL, &advance,
                    CHAR_NORMAL) ) {
      pop( &temporarystack ) ;
      break ;
    }

    /* DEVICE_SETG can change this value, so don't store it earlier */
    HQASSERT(CurrentPoint, "No current point in glyphshow_()") ;
    success = path_moveto(startx + advance.x,
                          starty + advance.y,
                          MOVETO,
                          &thePathInfo(*gstateptr)) ;
  } while ( FALSE ) ;

  textContextExit();

  if ( isHDLTEnabled(*gstateptr) ) {
    if ( !IDLOM_ENDTEXT(NAME_glyphshow, success) )
      success = FALSE ;
    gstateptr->theHDLTinfo = savedHDLT ;
  }

  if ( success )
    pop( &temporarystack ) ;

#undef return
  return success ;
}

/*---------------------------------------------------------------------------*/
enum { PLOT_normal, PLOT_cidfont, PLOT_glyphname, PLOT_notdef };

/**
   This routine is the equivalent of char_base_selector for composite
   fonts. It's more complex because we have to track through all
   the composite fonts to find the character we actually want to show,
   and this involves a variety of possible specified algorithms.

   The procedure is as follows
   1. Get a pair fontcode/charcode according to the current font's
      mapping algorithm. fontcode is used to index
      a subordinate font and charcode, the character within that font.
   2. Set the font to the subordinate font (remembering where we've been)
   3. Either show the character (charcode) if we have reached the bottom
      of the hierarchy of composite fonts, or go round again to descend
      further.

   Escape mapping is a little different: we may end up with either no
   fontcode (set to -1) because we don't have to move up or down the
   hierarchy for an escape mapped font, in which case we just show the
   character; or no charcode, indicating that a further descent is
   required (which is normal with other types too, but gives us an
   extra opportunity to check things if we then encounter a base font
   when we're expecting another composite font). */
static Bool char_pstype0_selector(void *data, char_selector_t *selector,
                                  Bool *eod)
{
  OBJECT *stringo = data ;
  int32 fontcode, charcode;
  NAMECACHE *charname ;
  uint8 shiftin, shiftout, escchar;
  uint8 *clist;
  int32 length, i, fonttype;
  int32 plot_state = PLOT_normal;
  OBJECT cmap_return = OBJECT_NOTVM_NOTHING,
    cmap_input = OBJECT_NOTVM_NOTHING ;
  uint8 scratch[CMAP_MAX_CODESPACE_LEN] ;

  HQASSERT(stringo != NULL && oType(*stringo) == OSTRING,
           "No string to extract selector from") ;
  HQASSERT(selector, "No destination for character selector") ;
  HQASSERT(eod, "No end of data pointer") ;

  if ( theLen(*stringo) == 0 ) {
    *eod = TRUE ;
    return TRUE ;
  }

  HQTRACE(debug_composite, ("in plotcomposite"));

  clist = oString(*stringo) ;
  length = theLen(*stringo) ;

  selector->index = -1 ;
  selector->cid = -1 ;
  selector->name = NULL ;
  selector->type3cid = -1 ;

  theTags(selector->string) = OSTRING | LITERAL | READ_ONLY ;
#if defined( DEBUG_BUILD )
  theLen(selector->string) = 0 ;
  oString(selector->string) = NULL ;
#endif
  Copy(&selector->complete, stringo) ;
  theTags(selector->cmap) = ONULL ;
  theTags(selector->parent) = ONULL ;

  charcode = -1;
  charname = NULL ;
  fontcode = compositefontstack ? theIFontStackCode(compositefontstack) : 0;

  /* STEP 1 -------------------------------------------------- */

  /* move up the tree until the parent is a modal font or we go off the top */
  while (compositefontstack &&
         theFMapType(theIFontStackInfo(compositefontstack)) != MAP_ESC &&
         theFMapType(theIFontStackInfo(compositefontstack)) != MAP_DESC &&
         theFMapType(theIFontStackInfo(compositefontstack)) != MAP_SHIFT) {
    HQTRACE(debug_composite, ("up to modal"));
    fontcode = theIFontStackCode (compositefontstack);
    (void) pop_composite_font ();
  }


  /* STEP 2 -------------------------------------------------- */

  /* at this point gstate is a non-modal-composite or base font -
     with parent, if any, a modal font - move up further if an escape
     or shift-code is encountered */
  if (compositefontstack) {
    if (theFMapType(theIFontStackInfo(compositefontstack)) == MAP_ESC ||
        theFMapType(theIFontStackInfo(compositefontstack)) == MAP_DESC) {
      escchar = theEscChar (theIFontStackInfo (compositefontstack));
      if ( clist[0] == escchar ) {
        HQTRACE(debug_composite, ("up to next modal (escape)"));
        (void) pop_composite_font (); /* must succeed */
        while (compositefontstack &&
               length > 1 &&
               theFMapType(theFontInfo(*gstateptr)) == MAP_ESC &&
               clist[1] == escchar) {
          clist++; length--;
          HQTRACE(debug_composite, ("further pop escape"));
          (void) pop_composite_font (); /* must succeed */
        }
      }
    } else if ( theFMapType(theIFontStackInfo(compositefontstack)) == MAP_SHIFT ) {
      shiftin = theShiftIn (theIFontStackInfo (compositefontstack));
      shiftout = theShiftOut (theIFontStackInfo (compositefontstack));
      if ( clist[0] == shiftin || clist[0] == shiftout ) {
        HQTRACE(debug_composite, ("up to next modal (shift)"));
        (void) pop_composite_font (); /* must succeed */
      }
    }
  }

  /* STEP 3 -------------------------------------------------- */

  /* we are now at the correct level of font to start a descent: first
     descend through any top level only fonts: shift and double escape */
  if ( theFontType(theFontInfo(*gstateptr)) == FONTTYPE_0) {
    if ( theFMapType(theFontInfo(*gstateptr)) == MAP_DESC ) {
      /* double escape mapping; that is, bytes represent characters
         unless they are two escapes followed by a fontcode */

      escchar = theEscChar( theFontInfo( *gstateptr ));

      HQTRACE(debug_composite, ("desc mapping: length=%d esc=%x",
                                length, escchar));
      fontcode = 0;
      if ( length > 0 && clist[0] == escchar ) {
        /* get any new font */
        clist++; length--; /* consume first escape */
        if (length > 0) {
          if ( clist[0] == escchar ) {
            clist++; length--; /* consume second escape */
            if (length > 0) {
              fontcode = 256 + clist[0];
              clist++; length--; /* consume fontcode */
            }
          } else {
            fontcode = clist[0];
            clist++; length--; /* consume fontcode */
          }
        }
      }
      if ( ! descend_font_hierarchy ( fontcode ))
        return FALSE ;
    } else if ( theFMapType(theFontInfo(*gstateptr)) == MAP_SHIFT ) {
      /* shift mapping; that is, bytes represent characters unless they are
         the special characters shift-in and shift-out which indicate
         select font 0 or font 1 respectively
         NOTE: the string was not adbvanced when we saw the shift above */

      shiftin = theShiftIn( theFontInfo( *gstateptr ));
      shiftout = theShiftOut( theFontInfo( *gstateptr ));

      HQTRACE(debug_composite, ("shift mapping: length=%d in=%x out=%x",
                                length, shiftin, shiftout));

      /* if its not a shift, fontcode is implicitly the same as before
         and charcode is the byte; otherwise get an extra
         byte for charcode */

      fontcode = 0;
      while ( length > 0 && (clist [0] == shiftin || clist [0] == shiftout) ) {
        HQTRACE(debug_composite, ("shift seen"));
        fontcode = clist[0] == shiftin ? 0 : 1;
        clist++; length --;
      }

      if ( ! descend_font_hierarchy ( fontcode ))
        return FALSE ;
    }
  }

  /* STEP 4 -------------------------------------------------- */

  /* now descend through any ordinary escape mapped fonts */
  while ( theFontType (theFontInfo (*gstateptr)) == FONTTYPE_0 &&
          theFMapType (theFontInfo (*gstateptr)) == MAP_ESC ) {
    escchar = theEscChar( theFontInfo( *gstateptr ));
    HQTRACE(debug_composite, ("esc mapping: length=%d current escchar=%x",
                              length, escchar));

    fontcode = 0;
    if ( length > 0 && clist[0] == escchar ) {
      /* get any new font */
      clist++; length--; /* consume escape */
      if (length > 0) {
        if ( clist[0] == escchar && ! compositefontstack) {
          fontcode = 0; /* must be a simpler way of doing this */
        } else {
          fontcode = clist [0];
          clist++; length--; /* consume fontcode */
        }
      }
    }
    if ( ! descend_font_hierarchy ( fontcode ))
      return FALSE;
  }

  /* STEP 5 -------------------------------------------------- */

  /* PLRM p.719: For widthshow and awidthshow, we have to compare the
     font/char combination with a value supplied by the PostScript. The
     character goes in the low 8 bits (except for 1/7 and 9/7 mappings where
     it is in the low 7 bits, and an exception for CMaps). This is calulated
     in-line in the font descent code below, with a catch-all for the case
     of a base font character after the loop. */
  HQASSERT(charcode == -1, "Charcode should not be set before descending") ;

  /* now descend the tree of non-modal fonts until we get to a base font */
  while ( theFontType (theFontInfo (*gstateptr)) == FONTTYPE_0 ) {

    /* Shouldn't be here, should have dropped out */
    if ( plot_state != PLOT_normal )
      return error_handler(INVALIDFONT);

    /* Update the string object with the remaining bytes. If we have already
       descended a CMap mapping, stringo will not be the original data field
       passed into this function, instead it will be the results of the
       previous CMap lookup (which must be a string for this case to be
       true). This prevents extra characters from being consumed from the
       input string when a CMap descendent is seen. See p.389 of the PLRM3. */
    theLen(*stringo) = (uint16)length ;
    oString(*stringo) = clist ;

    switch ( theFMapType (theFontInfo (*gstateptr)) ) {
    case MAP_88:
      /* 8/8 mapping; that is the bytes are taken from the string two at
         a time, the first being the font code and the second being the
         character code - except if there is a hierarchy of fonts, when we
         already know the font code - see note below */
      oString(selector->string) = clist ;
      if (charcode < 0) {
        if (length < 2)
          return error_handler ( RANGECHECK );
        fontcode = clist [0];
        charcode = clist [1];
        clist += 2; length -= 2;
        theLen(selector->string) = 2;
      } else {
        /* recursive definition - we already have fontcode, the previous
           charcode; though there is known to be a byte in the string
           when we enter the routine, the loop may have consumed some
           during descent of the font hierarchy, so check. */
        if (length < 1)
          return error_handler ( RANGECHECK );
        fontcode = charcode;
        charcode = clist [0];
        clist++; length--;
        theLen(selector->string) = 1;
      }
      selector->index = (fontcode << 8) | charcode ;
      break;

    case MAP_17:
      /* 1/7 mapping; like case 2, but only one byte is extracted and fontcode
         is 0 or 1. In theory we could be in a hierarchy - it is not
         really clear whether we should consume a byte or not then; in practice
         it's not intended to work like that though - see note below - and I
         will choose to assume that no further bytes are extracted */
      if (charcode < 0) {
        theLen(selector->string) = 1;
        oString(selector->string) = clist ;
        charcode = clist [0];
        clist++; length--;
      }
      selector->index = charcode ;
      fontcode = ( charcode & 0x80 ) >> 7;
      charcode &= 0x7f;
      break;

    case MAP_97:
      /* 9/7 mapping, a combination of case 2 and case 4, nine bits of font
         code and seven bits of character code */
      oString(selector->string) = clist ;
      if (charcode < 0) {
        if (length < 2)
          return error_handler ( RANGECHECK );
        fontcode = clist [0];
        charcode = clist [1];
        clist += 2; length -= 2;
        theLen(selector->string) = 2;
      } else {
        /* we have a hierarchical definition: we need one more byte, of which
           the first bit is incorporated into fontcode - that is like the
           normal case above but the first byte comes from the previous
           charcode. */
        if (length < 1)
          return error_handler ( RANGECHECK );
        fontcode = charcode;
        charcode = clist [0];
        clist++; length--;
        theLen(selector->string) = 1;
      }
      fontcode = ( fontcode << 1 ) | (( charcode & 0x80 ) >> 7 );
      charcode &= 0x7f;
      selector->index = (fontcode << 7) | charcode ;
      break;

    case MAP_SUBS:
      /* SubsVector mapping; we have an array of cut off points - the idea
         is to take a certain number of bytes out of the string (4 seems to be
         the maximum reasonable number) and convert them into a number; then
         compare this number with the stored array to find the last element
         which is smaller - the index of that number is the fontcode, and the
         distance into the interval is the character code. In the hierarchy
         case, the first byte comes from the previous charcode, not the
         string */
      {
        int32 bytes = theSubsBytes( theFontInfo( *gstateptr ));
        uint32 *subsvector = theSubsVector( theFontInfo( *gstateptr ));

        if (charcode < 0) {
          charcode = 0;
        } else {
          bytes -= 1;
        }
        if (length < bytes)
          return error_handler ( RANGECHECK );

        for ( i = 0; i < bytes ; i++ ) {
          charcode = charcode << 8;
          charcode |= clist [i];
        }

        /* now search the intervals array, subsvector */
        for ( fontcode = 0; fontcode < theSubsCount( theFontInfo ( *gstateptr )); ++fontcode ) {
          if ( (uint32)charcode < subsvector [fontcode] ) {
            break;
          }
          charcode -= subsvector [fontcode];
        }
        /* if we drop out naturally, that means it's off the end, but that is
           o.k. PLRM 3 p. 361 says the last range is omitted. */

        theLen(selector->string) = (uint16)bytes;
        oString(selector->string) = clist ;

        selector->index = (fontcode << 8) | charcode ;
        clist += bytes; length -= bytes;
      }
      break;

    case MAP_CMAP:
      /* In this case, we have to match the input against mappings defined in a
         CMap dictionary.  This should give us back a font number and a
         character selector, which may be a CID, a glyph name or a character
         code.  Use the font number against the Encoding and then FDepVector as
         usual.  Select this font and present the character selector to it.
         */

      /* Save CMap for notdef lookups and PDF Out */
      Copy(&selector->cmap, theCMap(theFontInfo(*gstateptr))) ;
      OCopy(selector->parent, theMyFont(theFontInfo(*gstateptr))) ;

      theTags(cmap_return) = OSTRING | UNLIMITED | LITERAL;
      theLen(cmap_return) = (uint16)CMAP_MAX_CODESPACE_LEN;
      oString(cmap_return) = scratch;

      if ( !cmap_lookup(theCMap(theFontInfo(*gstateptr)), stringo,
                        &fontcode, &cmap_return) )
        return FALSE ;

      /* Setup final string that was consumed by cmap_lookup */
      theLen(selector->string) = (uint16)(length - theLen(*stringo));
      oString(selector->string) = oString(*stringo) - theLen(selector->string) ;

      /* widthshow/awidthshow index consists of all bytes in string (PLRM3
         p.719. This is not explicit in the spec for undefined codespaces,
         but since the notdef glyph is a "glyph or CID" (PLRM p. 390) in that
         case, it is the most reasonable behaviour. We do this regardless of
         the mapping type. In the case that the mapping yielded a string, the
         index generation will be ignored. */
      for ( i = selector->index = 0 ; i < theLen(selector->string) ; ++i )
        selector->index = (selector->index << 8) | oString(selector->string)[i] ;

      switch ( oType(cmap_return) ) {
      case ONULL:
        /* PLRM3, p.390: "If the CMap does not contain either a character
           mapping or a notdef mapping for the code, font 0 is selected and a
           glyph is substituted from the associated font or CIDFont. If it is
           a base font, the character name .notdef is used; if it is a
           CIDFont, CID 0 is used. If it is a composite font, the behaviour
           is implementation-dependent. */
        fontcode = 0;
        plot_state = PLOT_notdef;

        break ;

      case OINTEGER: /* CID font */
        charcode = oInteger(cmap_return);
        plot_state = PLOT_cidfont;

        break;

      case ONAME: /* Glyph name */
        plot_state = PLOT_glyphname;
        charname = oName(cmap_return) ;

        break;

      case OSTRING: /* Mapped to string */
        /* This is the most awkward case, as it's the only one of the three
           (valid) results that can lead to going round the loop again.

           Descendent from this point can't consume more bytes from the
           show string, only from the cmap-translated result string that
           we've got back, so copy this string to the input scratch space
           and reset stringo to point at it.
           */
        theTags(cmap_input) = OSTRING | LITERAL | UNLIMITED ;
        theLen(cmap_input) = theLen(cmap_return) ;
        oString(cmap_input) = selector->codes;
        for ( i = 0 ; i < theLen(cmap_return) ; ++i )
          selector->codes[i] = scratch[i] ;

        stringo = &cmap_input ;

        break ;

      default:
        HQFAIL("Got an unexpected result from cmap_lookup");
      }

      HQASSERT(fontcode >= 0, "Invalid fontcode from cmap_lookup") ;

      clist = oString(*stringo);
      length = theLen(*stringo);

      break;
    default:
      /* error: invalid font */
      return error_handler ( INVALIDFONT );
    }

    if ( ! descend_font_hierarchy ( fontcode ))
      return FALSE;
  }

  /* STEP 6 -------------------------------------------------- */

  fonttype = theFontType(theFontInfo(*gstateptr)) ;
  OCopy(selector->font, theMyFont(theFontInfo(*gstateptr))) ;

  switch ( plot_state ) {
  case PLOT_notdef:
    /* Either CID 0 or /.notdef, according to base font type */
    if ( FONT_IS_CID(fonttype) ) {
      charcode = 0 ;
      charname = NULL ;
    } else {
      charcode = -1 ;
      charname = system_names + NAME_notdef ;
    }
    break ;
  case PLOT_normal:
    if ( FONT_IS_CID(fonttype) )
      return error_handler(INVALIDFONT) ;

    /* Now we're down at a base font; we may not have collected a
       character on the way - if not do so now */
    if ( charcode < 0 ) {
      if (length < 1)
        return error_handler (RANGECHECK);
      theLen(selector->string) = 1;
      oString(selector->string) = clist ;
      charcode = clist [0];
      selector->index = (fontcode << 8) | charcode ;
      clist++; length--;
    }
    HQASSERT(charname == NULL, "Name set in charcode font selector") ;
    break ;
  case PLOT_glyphname:
    if ( FONT_IS_CID(fonttype) )
      return error_handler(INVALIDFONT) ;
    HQASSERT(charcode < 0, "CID set in named font selector") ;
    HQASSERT(charname != NULL, "Name invalid in named font selector") ;
    break ;
  case PLOT_cidfont:
    /* PLRM3 p.389: See comments about Type 3/CID special handling below. */
    if ( fonttype == FONTTYPE_3 ) {
      selector->type3cid = charcode ;  /* Save CID we calculated */
      charcode = clist[-1] ;           /* Substitute for last character */

      /* Turn off caching for Type 3 intermediary fonts used in CID
         hierarchies. We do not need to update compositefontstack, because
         that contains Type 0 fonts only. We also do not need to update
         activefonts, because activefonts are only copied in during font
         hierarchy descent (all of which has been done above), or during
         CMap notdef mapping, in which case the selected font must be a CID
         font. */
      theFontInfo(*gstateptr).cancache = FALSE ;
    } else if ( !FONT_IS_CID(fonttype) )
      return error_handler(INVALIDFONT) ;

    HQASSERT(charcode >= 0, "CID invalid in CID font selector") ;
    HQASSERT(charname == NULL, "Name set in CID font selector") ;
    break ;
  default:
    HQFAIL("Strange plot_state") ;
  }

  HQASSERT(stringo == data || length == 0,
           "Not enough bytes consumed from final composite CMap") ;
  theLen(*stringo) = (uint16)length ;
  oString(*stringo) = length == 0 ? NULL : clist ;

  selector->cid = charcode ;
  selector->name = charname ;

  /* STEP 7 -------------------------------------------------- */

  /* Look at updated string to see how much we consumed. If length of
     remaining string is zero, we've used the whole of the original string. */
  stringo = data ;
  if ( theLen(*stringo) > 0 )
    theLen(selector->complete) = (uint16)(oString(*stringo) -
                                          oString(selector->complete)) ;

  HQASSERT(theLen(selector->string) > 0 &&
           oString(selector->string) != NULL,
           "Selector string not set") ;

  *eod = FALSE ;

  /* Displaying the character is deferred to doalltheshows */

  return TRUE ;
}

/* ------------------------------------------------------------------------- */
/** Callback functions for undefined CID characters in plotchar. */
static Bool ps_notdef_show(char_selector_t *selector, int32 type, int32 charCount,
                           FVECTOR *advance, void *data)
{
  char_selector_t selector_copy ;

  UNUSED_PARAM(void *, data) ;

  HQASSERT(selector, "No char selector for PS notdef character") ;
  /* Note: cid > 0 in this assertion, because we shouldn't be notdef mapping
     the notdef cid (value 0) */
  HQASSERT(selector->cid > 0, "PS notdef char selector is not a defined CID") ;

  selector_copy = *selector ;

  /* No CMap lookup for notdef. Use CID 0 (notdef) in current font instead */
  selector_copy.cid = 0 ;

  return plotchar(&selector_copy, type, charCount, NULL, NULL, advance, CHAR_NORMAL) ;
}

/** This function is tried first, allowing the CMap notdef mapping to be
   consulted to try and find a character definition. The recursive call
   to plotchar uses the function above to use the notdef CID 0 if the
   notdef mapping turns out to be bad. */
static Bool ps_notdef_cmap(char_selector_t *selector, int32 type, int32 charCount,
                           FVECTOR *advance, void *data)
{
  char_selector_t selector_copy ;

  HQASSERT(selector, "No char selector for PS notdef character") ;
  /* Note: cid > 0 in this assertion, because we shouldn't be notdef mapping
     the notdef cid (value 0) */
  HQASSERT(selector->cid > 0, "PS notdef char selector is not a defined CID") ;

  selector_copy = *selector ;

  /* PLRM3 p390: Go back to CMap for a notdef lookup. Note that the font
     number in the notdef mapping can be different from the font number in
     the original character mapping. This should re-select the descendent
     font from the Type 0 font which used the CMap. The CMap may be ONULL if
     called through glyphshow, in which case the notdef CID from the current
     CID font is used. */
  if ( oType(selector->cmap) == ODICTIONARY ) {
    OBJECT cidobj = OBJECT_NOTVM_NOTHING, *encoding, *fdepvector, *parent ;
    int32 fontcode ;

    if ( ! cmap_lookup_notdef(&selector_copy.cmap, &selector_copy.string,
                              &fontcode, &cidobj) )
      return FALSE ;

    HQASSERT(oType(cidobj) == OINTEGER,
             "Notdef mapping did not give a CID") ;
    selector_copy.cid = oInteger(cidobj) ;

    /* Select new font from parent, verify it is a CID font. */
    parent = &selector->parent ;
    HQASSERT(oType(*parent) == ODICTIONARY,
             "Parent of notdef CID font is not a font") ;


    error_clear_newerror();
    if ( (encoding = fast_extract_hash_name(parent, NAME_Encoding)) == NULL ||
         (fdepvector = fast_extract_hash_name(parent, NAME_FDepVector)) == NULL ) {
      if ( !newerror )
        (void)error_handler(INVALIDFONT) ;
      return FALSE ;
    }

    /* Select the font referred to by the CMap */
    if ( !select_subordinate_font(fontcode, encoding, fdepvector,
                                  FALSE /* do_push_composite */) )
      return FALSE ;

    /* The sub-font selected must be a CID font. This will break if a Type 3
       intermediary is used (PLRM p.389); we have no evidence about how Adobe
       treat these, since Distiller 4.0 locks up on trying such a font. */
    if ( !FONT_IS_CID(theFontType(theFontInfo(*gstateptr))) )
      return error_handler(INVALIDFONT) ;
  } else {
    /* No CMap, so cannot look up notdef. Use CID 0 (notdef) in
       current font instead */
    selector_copy.cid = 0 ;
  }

  return plotchar(&selector_copy, type, charCount, ps_notdef_show, data,
                  advance, CHAR_NORMAL) ;
}


/* ------------------------------------------------------------------------- */
static Bool doalltheshows(ps_context_t *pscontext, const SHOWARGS* args)
{
  int32 i;
  Bool result;

  HQASSERT((args != NULL),
           "doalltheshows: NULL args pointer");

  if ( !ps_core_context(pscontext)->systemparams->PostScript ) {
    return(error_handler(INVALIDACCESS));
  }
  if ( theStackSize(operandstack) < (args->itemsrequired - 1) ) {
    return(error_handler(STACKUNDERFLOW));
  }
  if ( args->showtype != DOCHARPATH ) {
    if ( !flush_vignette(VD_Default) ) {
      return(FALSE);
    }
  }

  /* Move required items to temporary stack */
  i = args->itemsrequired;
  while ( i-- > 0 ) {
    if ( !push(stackindex(i, &operandstack), &temporarystack) ) {
      npop(args->itemsrequired - i - 1, &temporarystack);
      return(FALSE);
    }
  }
  npop(args->itemsrequired, &operandstack);

  /* Do the type of show operator work */
  result = doalltheshows_internal(pscontext, args);

  if ( !result && newerror != NOT_AN_ERROR ) {
    /* On error, copy args back to operand stack, except if the error was
       already handled in a nested interpreter, as that would just put them in
       the wrong place, on top of the args of the op that failed. */
    HQASSERT((theStackSize(temporarystack) >= (args->itemsrequired - 1)),
             "doalltheshows: objects missing from temporary stack.");
    i = args->itemsrequired;
    while ( i-- > 0 ) {
      if ( !push(stackindex(i, &temporarystack), &operandstack) ) {
        HQFAIL("ran out of space copying operands back to operand stack - please report to core");
        break;
      }
    }
  }

  /* Remove operand copies from temporarystack */
  npop(args->itemsrequired, &temporarystack);

  return(result);

} /* doalltheshows */

static char_selector_t *under_cshow = NULL ;
static char_selector_t *parent_selector = NULL ;
static Bool doalltheshows_internal(ps_context_t *pscontext, const SHOWARGS* args)
{
  corecontext_t *context = ps_core_context(pscontext);
  DL_STATE *page = context->page;
  Bool stroke = FALSE, anexit = FALSE, result = FALSE ;
  int32 type0 ;
  int32 charindex = 0 ;
  int32 adjustsize = 0 ;
  int32 adjuststep = 0 ;
  int32 i , stringCount = 1 ;
  Bool textContextEntered = FALSE ;
  SYSTEMVALUE *adjustbase = NULL , *adjustptr = NULL ;
  SYSTEMVALUE wx , wy ;
  SYSTEMVALUE tx , ty , dx , dy ;
  SYSTEMVALUE cx = 0.0 , cy = 0.0 , ax = 0.0 , ay = 0.0 ;
  int32 kshow_lastchar = -1 ;
  FONTINFOLIST *oldcompositefontstack = NULL ;
  OBJECT oldrootfont = OBJECT_NOTVM_NOTHING ;
  HDLTinfo savedHDLT = {0}; /* pacify compiler about initialization */

  char_selector_t selector = { 0 } ; /* Embedded OBJECTs are ISNOTVM */
  char_selector_fn get_selector ;
  void *get_selector_data ;

  OBJECT *xyindexptr = NULL ;

  OBJECT *axobject = NULL , *ayobject = NULL ;
  OBJECT *cxobject = NULL , *cyobject = NULL , *charobject = NULL ;
  OBJECT *procobject = NULL ;
  OBJECT *boolobject = NULL ;
  OBJECT *stringobject ;
  OBJECT *xyobject = NULL ;

  OBJECT *thefontmatrix;

  int saved_dl_safe_recursion = dl_safe_recursion;

  char_selector_t *ancestor ;

  /* Does_Composite_Fonts */
  type0 = theFontType( theFontInfo( *gstateptr )) == FONTTYPE_0 ;
  if ( args->opname == NAME_kshow && type0 )
    return error_handler( INVALIDFONT );

  if ( args->widthindex >= 0 ) {
    cxobject = stackindex( args->widthindex , & temporarystack ) ;
    if ( ! object_get_numeric(cxobject, &cx) )
      return FALSE ;

    cyobject = stackindex( args->widthindex - 1 , & temporarystack ) ;
    if ( ! object_get_numeric(cyobject, & cy) )
      return FALSE ;

    MATRIX_TRANSFORM_DXY(cx, cy, cx, cy, & thegsPageCTM( *gstateptr )) ;

    charobject = stackindex( args->widthindex - 2 , & temporarystack ) ;
    if ( oType( *charobject ) != OINTEGER )
      return error_handler( TYPECHECK ) ;
    charindex = oInteger( *charobject ) ;
    if ( charindex < 0 )
      return error_handler( RANGECHECK ) ;
    /* PLRM3 p.719: If the font is a basefont, char is an integer between 0
       and 255. If not a base font, then it's fontcode and charcode
       combination, or CMap special index. */
    if ( ! type0 && charindex > 255 )
      return error_handler( RANGECHECK ) ;
  }

  if ( args->aindex >= 0 ) {
    axobject = stackindex( args->aindex , & temporarystack ) ;
    if ( ! object_get_numeric(axobject, & ax) )
      return FALSE ;

    ayobject = stackindex( args->aindex - 1 , & temporarystack ) ;
    if ( ! object_get_numeric(ayobject, & ay) )
      return FALSE ;

    MATRIX_TRANSFORM_DXY(ax, ay, ax, ay, & thegsPageCTM( *gstateptr )) ;
  }

  if ( args->procindex >= 0 ) {
    procobject = stackindex( args->procindex , & temporarystack ) ;
    switch ( oType( *procobject )) {
      static Bool recshow = FALSE ; /* Always reset to FALSE so this
                                       static is OK. --johnk */
    case OARRAY :
    case OPACKEDARRAY :
      break ;
    default:
      if ( args->opname == NAME_cshow )
        if ( ! recshow ) {
          /* Swap proc and string indexes as Adobe RIPs are not picky on stack order. */
          static const SHOWARGS t_args = {
            2, -1, -1, 1, -1, -1, -1,  0, NAME_cshow, DOSTRINGWIDTH
          };
          recshow = TRUE ;
          result = doalltheshows_internal(pscontext, &t_args) ;
          recshow = FALSE ;
          return ( result ) ;
        }
      return error_handler( TYPECHECK ) ;
    }
    if ( ! oCanExec( *procobject ) && ! object_access_override(procobject) )
      return error_handler( INVALIDACCESS ) ;
  }

  stringobject = stackindex( args->stringindex , & temporarystack ) ;
  if ( oType( *stringobject ) != OSTRING )
    return error_handler( TYPECHECK ) ;
  if ( ! oCanRead( *stringobject ) && ! object_access_override(stringobject) )
    return error_handler( INVALIDACCESS ) ;

  /* From now on, we'll use the character selector to refer to the string
     object. If in a cshow with a CID font, we replace the character selector
     with the previous one rather than backup to the root font (which has a
     whole host of problems with it). */
  get_selector = type0 ? char_pstype0_selector : char_base_selector ;
  get_selector_data = NULL ; /* This will be set to stringobject later */

  /* Strictly, none of the show operators are valid with a CIDFont directly.
     BUT, show called from cshow could legitimatly have a CIDFont in the
     gstate. */

  if ( FONT_IS_CID(theFontType(theFontInfo(*gstateptr))) ) {
    if ( under_cshow &&
         OBJECTS_IDENTICAL(theMyFont(theFontInfo(*gstateptr)), under_cshow->font) ) {
      /* PLRM3 p552: If we're in a show from a cshow, and the font hasn't
         changed, we must be attempting to show the original character. */

      OBJECT *ustring = &under_cshow->string ;

      /* Check character is the same as last char of parent CMap string */
      HQASSERT(theLen(*ustring) > 0, "cshow selector string too short") ;

      if ( theLen(*stringobject) != 1 ||
           oString(*stringobject)[0] != oString(*ustring)[theLen(*ustring) - 1] )
        return error_handler(RANGECHECK);

      get_selector = char_ancestor_selector ;
      ancestor = under_cshow ;
      get_selector_data = &ancestor ;
    } else if ( parent_selector && parent_selector->type3cid >= 0 ) {
      /* PLRM3 p.389: "Under special conditions, a CID can be used when the
         descendant is a Type 3 base font. The font's BuildGlyph or BuildChar
         procedure is invoked to render a character whose code is the last
         byte originally extracted from the show string. If this procedure
         executes setfont to establish a CIDFont as the current font and then
         executes a show operation on a string consisting of just that
         character code, the code is ignored; instead, the CID determined by
         the earlier CMap mapping is used to look up the glyph in the
         CIDFont." */

      /* Note that the wording on p.389 does not explicitly say the font must
         be the same as the one in which the CID was originally looked up.
         It's also not clear what happens if you do a cshow in the Type 3
         BuildChar, which then calls another show operation. */
      if ( theLen(*stringobject) != 1 ||
           oString(*stringobject)[0] != parent_selector->cid )
        return error_handler(INVALIDFONT);

      get_selector = char_ancestor_selector ;
      ancestor = parent_selector ;
      get_selector_data = &ancestor ;
    } else /* Not under cshow or different CID font, this case is invalid */
      return error_handler(INVALIDFONT);
  } else if ( theFontType(theFontInfo(*gstateptr)) == FONTTYPE_3 &&
              parent_selector && parent_selector->type3cid >= 0 ) {
    /* See the comments about Type 3 fonts above. Quark has a nasty glitch on
       this; it has a Type 3 font substituted in a CID hierarchy, which uses
       both the underlying CID font and another Type 3 font which in turn uses
       the underlying CID font. This case is to take care of the latter case,
       when a second Type 3 font is selected. If the rules apply, use the
       parent selector. */
    if ( theLen(*stringobject) == 1 &&
         oString(*stringobject)[0] == parent_selector->cid ) {
      get_selector = char_transitive_selector ;
      ancestor = parent_selector ;
      get_selector_data = &ancestor ;

      /* Turn off caching for Type 3 intermediary fonts used in CID
         hierarchies */
      theFontInfo(*gstateptr).cancache = FALSE ;
    } /* else use char_base_selector as normal */
  }

  if ( args->boolindex >= 0 ) {
    boolobject = stackindex( args->boolindex , & temporarystack ) ;
    if ( oType( *boolobject ) != OBOOLEAN )
      return error_handler( TYPECHECK ) ;
    stroke = oBool( *boolobject ) ;
  }

  if ( args->xindex >= 0 || args->yindex >= 0 ) {
    xyobject = stackindex( (args->xindex >= 0 ? args->xindex : args->yindex) , & temporarystack ) ;
    if ( ! oCanRead( *xyobject ) && ! object_access_override(xyobject) )
      return error_handler( INVALIDACCESS ) ;

    /* obtain whatever type of number array was provided */
    adjuststep = ( args->xindex >= 0 ? 1 : 0 ) + ( args->yindex >= 0 ? 1 : 0 ) ;
    switch ( oType( *xyobject )) {
    case OSTRING:
      adjustsize = 0 ;
      if ( ! decode_number_string( xyobject , & adjustbase , & adjustsize , 1 ))
        return FALSE ;
      adjustptr = adjustbase ;
      break ;
    case OARRAY:
    case OPACKEDARRAY:
      adjustsize = theLen(* xyobject );
      xyindexptr = oArray( *xyobject );
      break ;
    default:
      return error_handler( TYPECHECK ) ;
    }
  }

  if ( ! CurrentPoint && args->opname != NAME_stringwidth && args->opname != NAME_cshow ) {
    result = error_handler( NOCURRENTPOINT ) ;
    goto cleanup_adjustbase ;
  }

  /* Empty string is not a problem */
  if ( ! theLen(* stringobject )) {
    if ( adjustbase )
      mm_free_with_header(mm_pool_temp, adjustbase ) ;
    if ( args->opname == NAME_stringwidth ) {
      /* Operands have been moved to temporary stack so need to push new objects */
      oReal(rnewobj) = 0.0f;
      if ( !push2(&rnewobj, &rnewobj, &operandstack) ) {
        goto cleanup_adjustbase;
      }
    }
    return TRUE ;
  }

#define return DO_NOT_return_GO_TO_cleanup_adjustbase_INSTEAD!

  if (( args->opname != NAME_cshow ) &&
      ( args->opname != NAME_stringwidth ) &&
      ( args->opname != NAME_charpath )) {
    textContextEnter();
    textContextEntered = TRUE ;
  }

  /* add any pending characters to the display list and start over, but only
     if doing a show which could put characters on the DL. Length is the
     maximum number of chars we could put on the DL, not a guarantee that it
     will be that number. */
  if ( args->showtype == DOSHOW )
    if (! finishaddchardisplay(page,
                          args->procindex >= 0 ? 1 : theLen(*stringobject)) ) {
      /* This will call finishaddchardisplay again in the cleanup, but that's
         OK because its first action is to test if showDLobj is NULL and set
         it to NULL if it is not. The second call will return quickly, without
         affecting the return value. */
      goto cleanup_adjustbase ;
    }

  if ( !DEVICE_SETG(page, GSC_FILL, DEVICE_SETG_NORMAL) ) {
    /* to avoid recursion when painting in a pattern */
    goto cleanup_adjustbase ;
  }

  if ( type0 ) {
    /* If the rootfont has changed, the user has done a setfont within a
       composite font leaf BuildChar. Start a nested composite font
       hierarchy. Save the old rootfont and compositefontstack first. */
    Copy(&oldrootfont, &rootfont) ;
    oldcompositefontstack = compositefontstack ;
    if ( !OBJECTS_IDENTICAL(rootfont, theFontInfo(*gstateptr).rootfont) ) {
      Copy(&rootfont, &theFontInfo(*gstateptr).rootfont) ;
      compositefontstack = NULL ;
    }

    if ( ! create_fontinfo( & theMyFont( theFontInfo( *gstateptr )))) {
      goto cleanup_adjustbase ;
    }
  }
  else if ( args->xindex >= 0 || args->yindex >= 0 ) {
    if ( adjustsize < theLen(* stringobject ) * adjuststep ) {
      /* we don't know how many characters to expect in a composite font
         until we try it, but we're o.k. for normal fonts */
      result = error_handler( RANGECHECK ) ;
      goto cleanup_adjustbase ;
    }
  }

#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
  /* Pacify MSVC about savedHDLT being unused */
  HqMemZero(&savedHDLT, sizeof(savedHDLT)) ;
#endif

  if ( args->showtype == DOCHARPATH ) {
    if ( ! init_charpath(stroke) ) {
      goto cleanup_adjustbase ;
    }

  } else if ( isHDLTEnabled(*gstateptr) ) {
    /* Save HDLT info, because we're not doing a gsave to preserve the current
       target. */
    savedHDLT = gstateptr->theHDLTinfo ;
    gstateptr->theHDLTinfo.next = &savedHDLT ;
    if ( !IDLOM_BEGINTEXT(args->opname) ) {
      goto cleanup_adjustbase ;
    }
  }

  if ( get_selector_data == NULL )
    get_selector_data = stringobject ;

  tx = ty = 0.0 ;
  wx = wy = 0.0 ;

  for (;;) {
    Bool done ;
    char_selector_t *old_parent = parent_selector ;
    FVECTOR advance ;

    /* Try to make sure the reserve is full */
    if ( mm_memory_is_low || dosomeaction ) {
      dl_erase_nr eraseno_before = page->eraseno;
      int current_dl_safe_recursion = dl_safe_recursion ;

      /* Set dl_safe_recursion to previous level to allow partial paint
       * (if top-level dl_safe_recursion allows it). */
      dl_safe_recursion = saved_dl_safe_recursion ;
      result = handleNormalAction();
      /* Reset dl_safe_recursion to current level in case DEVICE_SETG
       * not called again. */
      dl_safe_recursion = current_dl_safe_recursion ;

      if ( ! result )
        break ;
      /* to avoid recursion when painting in a pattern */
      if ( page->eraseno != eraseno_before
           && !DEVICE_SETG(page, GSC_FILL, DEVICE_SETG_NORMAL) ) {
        result = FALSE ;
        break ;
      }
    }

    result = (*get_selector)(get_selector_data, &selector, &done) ;
    if ( !result || done )
      break ;

    HQASSERT(oType(selector.string) == OSTRING &&
             theLen(selector.string) > 0,
             "Selector has invalid string") ;
    HQASSERT(oType(selector.complete) == OSTRING &&
             theLen(selector.complete) > 0,
             "Selector has invalid complete string") ;
    HQASSERT((selector.cid >= 0 && selector.name == NULL) ||
             (selector.cid < 0 && selector.name != NULL),
             "Selector should have either CID or name") ;
    /* The index for CMaps is a high-byte first concatenation of the bytes
       which made the mapping. Since we support 4-byte CMaps, there is no
       value not representable (a CMap could contain a begincidchar/range
       which finishes at 0xffffffff). In these unlikely circumstances, this
       assert will be bogus. The CMap index will never match the widthshow
       index in this case. */
    HQASSERT(selector.index != -1,
             "Selector does not have valid (a)widthshow index") ;

    if ( args->opname == NAME_kshow ) {
      if ( kshow_lastchar >= 0 ) {
        /* PLRM p.620: kshow applies only to base fonts, so char selector CID
           must be a single byte. */
        HQASSERT((selector.cid & ~0xff) == 0, "Not a base font character") ;
        if ( FALSE == (result = stack_push_integer(kshow_lastchar, &operandstack)) )
          break ;
        if ( FALSE == (result = stack_push_integer(selector.cid, &operandstack)) )
          break ;
        if ( FALSE == ( result = push( procobject , & executionstack )))
          break ;

        /* Leaving the text context while we reenter the interpreter */
        if ( textContextEntered )
          textContextExit();
        result = interpreter( 1 , & anexit ) ;
        /* Back from interpreter, back into text context. */
        if ( textContextEntered )
          textContextEnter();

        if ( !result ) {
          if ( anexit ) {
            error_clear_context(context->error);
            result = TRUE;
          }
          break;
        }

        /* Make sure we still  have a valid point. */
        if ( ! CurrentPoint) {
          result = error_handler( NOCURRENTPOINT ) ;
          break;
        }
      }
      kshow_lastchar = selector.cid ;
    }

    parent_selector = &selector ;
    result = plotchar(&selector, args->showtype, stringCount, ps_notdef_cmap, NULL,
                      &advance, CHAR_NORMAL) ;
    parent_selector = old_parent ;

    stringCount++ ;
    if ( args->xindex >= 0 || args->yindex >= 0 )
      if ( adjustsize <= 0 )
        result = error_handler( RANGECHECK ) ;

    if ( ! result )
      break ;

    if ( args->xindex < 0 && args->yindex < 0 ) {
      dx = advance.x ;
      dy = advance.y ;
      if ( args->widthindex >= 0 ) {
        if ( charindex == selector.index ) {
          dx += cx ;
          dy += cy ;
        }
      }
      if ( args->aindex >= 0 ) {
        dx += ax ;
        dy += ay ;
      }
    }
    else {                      /* if ( xindex >= 0 || yindex >= 0 ) */
      adjustsize -= adjuststep ;

      if ( adjustbase ) {
        if ( args->xindex >= 0 )
          tx = *adjustptr++ ;
        if ( args->yindex >= 0 )
          ty = *adjustptr++ ;
      }
      else {
        if ( args->xindex >= 0 ) {
          if ( FALSE == ( result = object_get_numeric(xyindexptr, & tx)))
            break ;
          ++xyindexptr ;
        }
        if ( args->yindex >= 0 ) {
          if ( FALSE == ( result = object_get_numeric(xyindexptr, & ty)))
            break ;
          ++xyindexptr ;
        }
      }
      MATRIX_TRANSFORM_DXY( tx, ty, dx, dy, & thegsPageCTM( *gstateptr )) ;
    }

    if ( args->showtype == DOSTRINGWIDTH ) { /* includes cshow_ */
      wx += dx ;
      wy += dy ;
    }
    else {
      PATHINFO *lpath = &(thePathInfo(*gstateptr)) ;
      LINELIST *theline = lpath->lastline ;

      if ( theline ) {
        SYSTEMVALUE x = theX( theIPoint( theline )) ;
        SYSTEMVALUE y = theY( theIPoint( theline )) ;

        if ( ! path_moveto( x + dx ,
                            y + dy ,
                            MOVETO, lpath )) {
          result = FALSE ;
          break ;
        }
      }
    }

    if ( args->opname == NAME_cshow ) {
      OBJECT savematrix[6];
      char_selector_t *old_cshow = under_cshow ;

      /* PLRM3 p.552: If the descent of the font heirarchy wound up with a
         CID font, then only push the low order byte. Otherwise, push
         character code. */
      if ( FONT_IS_CID(theFontType(theFontInfo(*gstateptr))) ) {
        result = stack_push_integer(oString(selector.string)[theLen(selector.string) - 1],
                                    &operandstack);
      } else {
        HQASSERT((selector.cid & ~0xff) == 0, "Not a base font character") ;
        result = stack_push_integer(selector.cid, &operandstack);
      }

      if ( !result )
        break ;

      /* convert width back to user space: both composite fonts and normal */
      SET_SINV_SMATRIX( & thegsPageCTM( *gstateptr ) , NEWCTM_RCOMPONENTS ) ;
      if ( SINV_NOTSET( NEWCTM_RCOMPONENTS ) ) {
        result = error_handler( UNDEFINEDRESULT ) ;
        break ;
      }

      MATRIX_TRANSFORM_DXY( dx, dy, tx, ty, & sinv ) ;
      if ( tx < EPSILON && tx > -EPSILON ) tx = 0.0 ;
      if ( FALSE == ( result = stack_push_real( tx, &operandstack )))
        break ;
      if ( ty < EPSILON && ty > -EPSILON ) ty = 0.0 ;
      if ( FALSE == ( result = stack_push_real( ty, &operandstack )))
        break ;
      if ( FALSE == ( result = push( procobject , & executionstack )))
        break ;

      /* modify the matrix to be that of the whole font tree while
         executing the BuildChar/Glyph (when this
         is not a composite font, the result is the same as before,
         though the values may be reals rather than integers).
         So remember the old one first. This is the same code as
         in fcache before calling the buildchar; that's because currentfont
         explicitly says this is also the case while doing cshow.
         */
      if (compositefontstack == NULL && ! set_matrix ()) {
        /* from fcache.c: needed for non-composite fonts so
           the matrix is something sensible. */
        result = FALSE ;
        break ;
      }

      thefontmatrix = fast_extract_hash_name(&theMyFont(theFontInfo(*gstateptr)),
                                             NAME_FontMatrix);
      HQASSERT (thefontmatrix != NULL, "font matrix is null before cshow");
      HQASSERT (oType(*thefontmatrix) == OARRAY ||
                oType(*thefontmatrix) == OPACKEDARRAY,
                "font matrix is not an array before cshow");
      thefontmatrix = oArray (*thefontmatrix);
      for (i = 0; i < 6; i++) {
        HQASSERT (oType(thefontmatrix [i]) == OINTEGER ||
                  oType(thefontmatrix [i]) == OREAL,
                  "font matrix element isnt a number before cshow");
        Copy(object_slot_notvm(&savematrix[i]), &thefontmatrix[i]);
        object_store_numeric(&thefontmatrix[i],
                             theFontCompositeMatrix( theFontInfo( *gstateptr )).matrix[ i/2 ][ i%2 ]) ;
      }

      /* Push old cshow info and call proc */
      under_cshow = &selector;
      result = interpreter( 1 , & anexit );
      under_cshow = old_cshow;

      /* put the matrix back as it was; should this happen only if font is
         not changed in the proc? */
      for (i = 0; i < 6; i++) {
        OCopy(thefontmatrix[i], savematrix[i]);
      }

      if (! result) {
        if ( anexit ) {
          error_clear_context(context->error);
          result = TRUE;
        }
        break;
      }
    }
  }

  if ( args->opname == NAME_stringwidth && result ) {
    /* convert width back to user space: both composite fonts and normal */
    SET_SINV_SMATRIX( & thegsPageCTM( *gstateptr ) , NEWCTM_RCOMPONENTS ) ;
    if ( SINV_NOTSET( NEWCTM_RCOMPONENTS ) )
      result = error_handler( UNDEFINEDRESULT ) ;
    else {
      MATRIX_TRANSFORM_DXY( wx, wy, tx, ty, & sinv ) ;
      if ( tx < EPSILON && tx > -EPSILON ) tx = 0.0 ;
      result = result && stack_push_real( tx, &operandstack ) ;
      if ( ty < EPSILON && ty > -EPSILON ) ty = 0.0 ;
      result = result && stack_push_real( ty, &operandstack ) ;
    }
  }

  if ( args->showtype == DOCHARPATH ) {
    result = end_charpath( result ) ;
  } else if ( isHDLTEnabled(*gstateptr) ) {
    if ( !IDLOM_ENDTEXT(args->opname, result) )
      result = FALSE ;
    gstateptr->theHDLTinfo = savedHDLT ;
  }

 cleanup_adjustbase:
  if ( adjustbase )
    mm_free_with_header(mm_pool_temp, adjustbase ) ;

  /* Leave the text context if we entered one */
  if ( textContextEntered ) {
    textContextExit();
  }

  if ( type0 ) {
    if ( compositefontstack != oldcompositefontstack ) {
      /* dealing with a composite font - clean up by popping
         the composite font stack. */
      while ( pop_composite_font() )
        EMPTY_STATEMENT() ;

      /* Restore the old composite font stack, for the benefit of the next
         character in ancestral recursive shows. */
      Copy(&rootfont, &oldrootfont) ;
      compositefontstack = oldcompositefontstack ;
    }

    /* Delete all of the cached font information only when finished with
       all nested composite font instances. */
    if ( compositefontstack == NULL )
      delete_fontinfo() ;
  }

  /* Do this even if result is false; if any characters were successfully
     added to the DL before an error occurred, we don't want to lose them. */
  if ( args->showtype == DOSHOW )
    if (! finishaddchardisplay(page, 1))
      result = FALSE;

#undef return
  return ( result ) ;
}

/* ---------------------------------------------------------------------- */
static Bool push_composite_font(int32 fontcode)
{
  FONTINFOLIST
    * fi = activefonts,
    * prevfi = NULL;

  while ( fi ) {
    if ( oDict( theMyFont( theIFontStackInfo( fi ))) ==
             oDict( theMyFont( theFontInfo( *gstateptr ))))
      break ; /* found it */

    prevfi = fi;
    fi = fi->next;
  }

  if ( ! fi ) {
    /* something has gone wrong because we should have created one of
       these when the font was first noticed */
    return error_handler ( UNREGISTERED );
  }

  /* unlink from active fonts chain */
  if ( prevfi ) {
    prevfi->next = fi->next;
  } else {
    activefonts = fi->next;
  }

  /* push on top of composite font stack */
  fi->next = compositefontstack;
  theIFontStackInfo( fi ) = theFontInfo( *gstateptr ) ; /* copies a structure */
  theIFontStackCode( fi ) = fontcode;
  compositefontstack = fi;

  return TRUE;
}

/* ---------------------------------------------------------------------- */
/** Restore the graphics state to its position as before the last push
   (only so far as the font information is concerned - current point etc.
   are unaffected); since we are likely to encounter the font we were
   working on again, don't throw the information away - put it back
   on the list of active composite fonts. */
static Bool pop_composite_font(void)
{
  FONTINFOLIST * fi;

  fi = compositefontstack;
  if ( fi == NULL ) {
/* composite font stack underflow */
    return FALSE;
  }
  compositefontstack = compositefontstack->next;

  theFontInfo( *gstateptr ) = theIFontStackInfo ( fi );

  fi->next = activefonts;
  activefonts = fi;

  return TRUE;
}

/* ---------------------------------------------------------------------- */
static Bool create_fontinfo(OBJECT *newfonto)
{
  /* have we already seen the font in this string? */
  FONTINFOLIST  *fi , *new ;
  uint8 escchar;

  fi = activefonts ;
  while ( fi ) {
    if ( oDict( theMyFont( theIFontStackInfo( fi ))) == oDict( *newfonto ))
      break; /* found it */

    fi = fi->next;
  }

  if ( ! fi ) {
/* first time we have seen this font in parsing this string */
    Copy(&theMyFont(theFontInfo(*gstateptr)), newfonto);
    Copy(&theFontInfo(*gstateptr).subfont, newfonto);
    gotFontMatrix( theFontInfo( *gstateptr )) = FALSE ;
    theLookupFont( theFontInfo( *gstateptr )) = NULL ;
    theLookupMatrix( theFontInfo( *gstateptr )) = NULL ;
    theFontInfo(*gstateptr).cancache = TRUE ;

/* escchar and wmode are inherited from the root font. Setting wmode
   to other than 0 or 1 causes get_font to read it; for escchar
   remember it and reinstate it afterwards */
    if ( compositefontstack == NULL ) {
/* it's a top level font; really read things */
      HQTRACE(debug_composite, ("top level"));
      theWModeNeeded( theFontInfo( *gstateptr )) = TRUE;
      if ( ! set_font())
        return FALSE;

/* set up the current fontmatrix from the font's matrix and the CTM; do
   this unconditionally - we might be able to optimise this later,
   but it's probably not worth it */
      if ( ! set_matrix ()) /* from fcache.c */
        return FALSE;
    }
    else {
      HQTRACE(debug_composite, ("subordinate"));
      /* it's a subordinate font - inherit things */
      escchar = theEscChar( theFontInfo( *gstateptr )); /* may be garbage */
      if ( ! set_font())
        return FALSE;

      if ( (int32)theFMapType ( theFontInfo( *gstateptr )) == MAP_ESC ) {
        /* it can't be MAP_DESC because they are only allowed as root fonts.
           only assign if it is an esc font because EscChar is unioned with
           the subs vector */
        theEscChar( theFontInfo( *gstateptr )) = escchar;
      }
      /* during descent, concatenate the font matrices */
      if ( ! concatenate_fontmatrix ())
        return FALSE;
    }

    gotFontMatrix( theFontInfo( *gstateptr )) = TRUE ;

    HQTRACE(debug_composite, ("wmode=%d (%d)", theWMode( theFontInfo( *gstateptr )),
                          theFontType( theFontInfo( *gstateptr))));

    /* remember it for the future (within this string only because
       bits of font can change from string to string) */
    new = (FONTINFOLIST *) mm_alloc(mm_pool_temp, sizeof( FONTINFOLIST ),
                                    MM_ALLOC_CLASS_FONT_INFO);
    if ( ! new ) {
      return error_handler(VMERROR);
    }
    new->next = activefonts;
    theIFontStackInfo( new ) = theFontInfo( *gstateptr );
      /* copies a structure */
    activefonts = new;
  } else {
    /* found the font from a previous use - reinstate it */
    theFontInfo( *gstateptr ) = theIFontStackInfo( fi );
    HQTRACE(debug_composite, ("wmode=%d (inherited)", theWMode( theFontInfo( *gstateptr ))));
  }
  return TRUE;
}

/* ---------------------------------------------------------------------- */
static void delete_fontinfo(void)
{
  while ( activefonts ) {
    FONTINFOLIST * currentone = activefonts;
    activefonts = activefonts->next;
    if ((int32)theFMapType (theIFontStackInfo( currentone )) == MAP_SUBS &&
      theSubsVector( theIFontStackInfo( currentone )))
    {
      mm_free_with_header(mm_pool_temp,
                          (mm_addr_t) theSubsVector( theIFontStackInfo( currentone )));
    }
    mm_free(mm_pool_temp, (mm_addr_t) currentone, sizeof( FONTINFOLIST ) );
  }
}

/* ---------------------------------------------------------------------- */
/** Get the subordinate font dictionary out of the composite font- this is
    via the encoding vector (which is here an array of integers) to index
    into the FDepVector entry .
*/
static Bool select_subordinate_font(int32 fontcode,
                                    OBJECT *encoding,
                                    OBJECT *fdepvector,
                                    Bool do_push_composite)
{
  int32 encoded_fontcode;
  OBJECT * newfonto;

  HQASSERT(encoding, "No Encoding in composite font") ;
  HQASSERT(oType(*encoding) == OARRAY ||
           oType(*encoding) == OPACKEDARRAY,
           "Encoding isn't an array in composite font") ;
  HQASSERT(fdepvector, "No FDepVector in composite font") ;
  HQASSERT(oType(*fdepvector) == OARRAY ||
           oType(*fdepvector) == OPACKEDARRAY,
           "FDepVector isn't an array in composite font") ;

  /* encoding has already been checked as being an array which is readable
     ---> don't forget to do this! */
  if ( fontcode >= theLen(*encoding) ) {
    /* error: too few entries in encoding vector - rangecheck */
    return error_handler ( RANGECHECK );
  }

  newfonto = oArray(*encoding) + fontcode;
  if ( oType( *newfonto ) != OINTEGER ) {
    /* error: encoding vector must always be integers - could check this
       in advance, but there are sufficient callbacks in which someone
       could change it, that I'll check it here. One way this error could
       arise is if the string says descend another composite font, but we're
       actually in a base font already.
     */
    return error_handler ( INVALIDFONT );
  }

  encoded_fontcode = oInteger( *newfonto );
  /* now index into the array of font dictionaries - already
     checked this for type  */
  if ( encoded_fontcode >= theLen(*fdepvector) ) {
    /* error: too few entries in FDepVector - invalidfont */
    return error_handler ( INVALIDFONT );
  }

  newfonto = oArray(*fdepvector) + encoded_fontcode;
  if ( oType( *newfonto ) != ODICTIONARY )
    return error_handler ( INVALIDFONT );

  if ( ! oCanRead( *oDict( *newfonto ) ))
    if ( ! object_access_override(newfonto) )
      return error_handler( INVALIDACCESS ) ;

  if ( ! ( theTags( *oDict( *newfonto )) & ISAFONT ))
    return error_handler( INVALIDFONT );

  if ( do_push_composite && ! push_composite_font(fontcode))
    return FALSE;

  return create_fontinfo(newfonto);
}

/* ---------------------------------------------------------------------- */
static Bool descend_font_hierarchy(int32 fontcode)
{
  return select_subordinate_font(fontcode,
                                 &theEncoding(theFontInfo(*gstateptr)),
                                 &theFDepVector(theFontInfo(*gstateptr)),
                                 TRUE /* do_push_composite */ ) ;
}

/* ---------------------------------------------------------------------- */
/** Takes the current gstate fontmatrix (which is set to the matrix of the
   top font x the current matrix initially) and applies the matrix of the
   current font - which if done all the way down gives the complete font
   matrix by the time we call this routine with the base font. The whole
   thing is very like set_matrix in fcache.c, just using different source
   matrices. */
static Bool concatenate_fontmatrix(void)
{
  OMATRIX matrix ;
  OBJECT *theo ;
  FONTinfo *fontInfo = &theFontInfo(*gstateptr) ;
  OBJECT key = OBJECT_NOTVM_NAME(NAME_FontMatrix, LITERAL) ;

  /* Extract the matrix list from the FontMatrix in the currentfont */
  error_clear_newerror();
  if ( NULL == (theo = extract_hash(&theMyFont(*fontInfo), &key)) ) {
    if ( newerror )
      return FALSE ;
    else
      return error_handler( INVALIDFONT ) ;
  }
  if ( ! is_matrix( theo , & matrix ))
    return FALSE ;

/*  Multiply this by the current fontmatrix to give a new one */
  matrix_mult(&matrix, &theFontMatrix(*fontInfo),
              &theFontMatrix(*fontInfo)) ;

  matrix_mult(&matrix, &theFontCompositeMatrix(*fontInfo),
              &theFontCompositeMatrix(*fontInfo)) ;

  if ( NULL != (theo = fast_extract_hash_name(&theMyFont(*fontInfo),
                                              NAME_ScaleMatrix)) ) {
    if ( ! is_matrix( theo , & matrix ))
      return FALSE ;

    /*  Multiply this by the current scalematrix to give a new one */
    matrix_mult(&matrix, &fontInfo->scalematrix, &fontInfo->scalematrix) ;
  }

  return TRUE ;
}

void init_C_globals_shows(void)
{
  rootfont = onothing ; /* Struct copy to set slot properties */
  activefonts = NULL ;
  compositefontstack = NULL ;
  under_cshow = NULL ;
  parent_selector = NULL ;
}

/*
Log stripped */
