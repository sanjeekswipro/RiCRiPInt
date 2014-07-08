/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdffont.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF font handling
 */

#include "core.h"
#include "swerrors.h"
#include "swctype.h"
#include "uvms.h"
#include "objects.h"
#include "dictscan.h"
#include "fileio.h"
#include "mm.h"
#include "mps.h"
#include "gcscan.h" /* ps_scan_field */
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "hqmemset.h"
#include "tables.h"
#include "namedef_.h"

#include "control.h"
#include "stacks.h"
#include "dicthash.h"
#include "streamd.h"
#include "tstream.h"

#include "fileops.h"
#include "stackops.h"
#include "gstate.h"
#include "fontops.h"
#include "showops.h"
#include "fonth.h"

#include "swpdf.h"
#include "pdfxref.h"
#include "pdfstrm.h"
#include "pdfmem.h"
#include "pdfmatch.h"

#include "pdfexec.h"
#include "pdffont.h"
#include "pdfcmap.h"
#include "pdfin.h"
#include "pdfdefs.h"    /* PDF_ENFORCEPDF_X_1_1999 */
#include "pdfx.h"

#include "swdevice.h"   /* INT32_TO_DEVICE_FILEDESCRIPTOR etc */

struct pdf_cached_fontdetails {
  PDF_FONTDETAILS font_details ;
  OBJECT          pdffont ;
  OBJECT          fontdesc ;
  NAMECACHE      *psfontname ;
  int32           slevel ;
  PDF_CACHED_FONTDETAILS *next ;
} ;

/* -------------------------------------------------------------------------- */
static void pdf_freecachedfont(PDFXCONTEXT *pdfxc, PDF_CACHED_FONTDETAILS *tmp) ;


#if defined( ASSERT_BUILD )
static Bool debug_findfonts = FALSE ;
#endif


#if defined( DEBUG_BUILD )

#include "swcopyf.h"

static int32 tfont_number = 0 ;

/* For debugging, when tfont_number is greater than 0, tee the stream created
   from each filter to a file in the SW directory. */
static Bool tee_font_download(OBJECT *streamo)
{
  OBJECT tstringo = OBJECT_NOTVM_NOTHING, tfileo = OBJECT_NOTVM_NOTHING ;

  static uint8 tee_name[10] ;

  swcopyf(tee_name, (uint8 *)"TFont%02d", tfont_number++) ;

  theTags(tstringo) = OSTRING | LITERAL | READ_ONLY ;
  oString(tstringo) = tee_name ;
  theLen(tstringo) = strlen_uint16((char *)tee_name) ;

  /* Open the tee stream font file for writing */
  if (! file_open(&tstringo, SW_WRONLY|SW_CREAT|SW_TRUNC,
                  WRITE_FLAG, /* append_mode = */ FALSE,
                  /* base_flag = */ 0, &tfileo) )
    return FALSE;

  /* and set it up as a tee stream */
  return start_tstream( oFile(*streamo), oFile(tfileo), FALSE ) ;
}

#endif


/* -------------------------------------------------------------------------- */
/* pdf_scancachedfonts -- scan font cache */


static mps_res_t pdf_scanfontdetails(mps_ss_t ss, PDF_FONTDETAILS *fd)
{
  mps_res_t res;
  int32 i;

  res = ps_scan_field( ss, &fd->atmfont );
  if ( res != MPS_RES_OK ) return res;
  if ( fd->cmap_details != NULL ) {
    res = ps_scan_field( ss, &fd->cmap_details->cmap_dict );
    if ( res != MPS_RES_OK ) return res;
  }
  for ( i = fd->num_descendants - 1; i >= 0; i-- ) {
    if ( fd->descendants[i] != NULL ) {
      res = pdf_scanfontdetails( ss, fd->descendants[i] );
      if ( res != MPS_RES_OK ) return res;
    }
  }
  return MPS_RES_OK;
}


mps_res_t pdf_scancachedfonts(mps_ss_t ss, PDF_CACHED_FONTDETAILS *cache)
{
  mps_res_t res;
  PDF_CACHED_FONTDETAILS *cached_fd = cache;

  MPS_SCAN_BEGIN( ss )
    while ( cached_fd != NULL ) {
      MPS_SCAN_CALL( res = pdf_scanfontdetails( ss, &cached_fd->font_details ));
      if ( res != MPS_RES_OK ) return res;
      /* pdffont is the original font dictionary and fontdesc is one of
       * its elements, so they should be PDF objects - no need to scan. */
      MPS_RETAIN( &cached_fd->psfontname, TRUE );
      cached_fd = cached_fd->next;
    }
  MPS_SCAN_END( ss );
  return MPS_RES_OK;
}


/* -------------------------------------------------------------------------- */
static Bool pdf_base14Font( NAMECACHE *fontname )
{
  HQASSERT( fontname , "fontname NULL in pdf_base14Font" ) ;

  /* Check if font is one of base 14. */
  switch (( int32 )theINameNumber( fontname )) {
  case NAME_Courier:
  case NAME_CourierBold:
  case NAME_CourierOblique:
  case NAME_CourierBoldOblique:
  case NAME_Helvetica:
  case NAME_HelveticaBold:
  case NAME_HelveticaOblique:
  case NAME_HelveticaBoldOblique:
  case NAME_Symbol:
  case NAME_TimesRoman:
  case NAME_TimesBold:
  case NAME_TimesItalic:
  case NAME_TimesBoldItalic:
  case NAME_ZapfDingbats:
    return TRUE ;
  default:
    return FALSE ;
  }
  /* Not reached */
}

/* --------------------------------------------------------------------------
** pdf_resource_exists()
** Given a font name, uses 'resourcestatus' to determine whether or not
** the font is known (i.e. whether or not 'findfont' would succeed).
*/
static Bool pdf_resource_exists(OBJECT *resname, int32 restype, Bool *exists)
{
  int32 returned = 1 ;
  OBJECT resourcestatus = OBJECT_NOTVM_NAME(NAME_resourcestatus, EXECUTABLE),
    category = OBJECT_NOTVM_NOTHING ;

  *exists = FALSE;

  HQASSERT(resname, "No resource name/string object") ;
  HQASSERT(oType(*resname) == ONAME || oType(*resname) == OSTRING,
           "Resource name/string object wrong type") ;

  object_store_name(&category, restype, LITERAL) ;

  /* Do "<resname> /<Category> resourcestatus" */
  if ( !interpreter_clean(&resourcestatus, resname, &category, NULL) )
    return FALSE ;

  /* Top-most returned object is boolean for yay or nay */
  HQASSERT(oType(*theTop(operandstack)) == OBOOLEAN,
           "resourcestatus returned non-boolean");

  *exists = oBool(*theTop(operandstack)) ;
  if ( *exists )
    returned = 3 ; /* Don't care about returned arguments */

  npop(returned, &operandstack) ;

  return TRUE;
}

/* -------------------------------------------------------------------------- */
Bool pdf_findresource(int32 category, OBJECT *name)
{
  OBJECT findresource = OBJECT_NOTVM_NAME(NAME_findresource, EXECUTABLE),
    catname = OBJECT_NOTVM_NOTHING ;

  HQASSERT(name, "No resource name object") ;

  object_store_name(&catname, category, LITERAL) ;

  return interpreter_clean(&findresource, name, &catname, NULL) ;
}

/* -------------------------------------------------------------------------- */
static Bool pdf_setfont_mmbase(PDFCONTEXT *pdfc, NAMECACHE *mmfontname)
{
  /* Find the first underscore of the instance and use the root as the name
   * of the original MM font to load into the coreRIP (if exists).
   */
  ps_context_t *pscontext ;
  int32 i ;
  int32 clen ;
  uint8 *clist ;

  HQASSERT(pdfc->pdfxc != NULL, "No PDF execution context") ;
  HQASSERT(pdfc->pdfxc->corecontext != NULL, "No core context") ;
  pscontext = pdfc->pdfxc->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  clen = mmfontname->len ;
  clist = mmfontname->clist ;

  for ( i = 0 ; i < clen ; ++i ) {
    if ( clist[ i ] == '_' ) {
      NAMECACHE *mmbasename ;
      OBJECT nameobj = OBJECT_NOTVM_NOTHING ;

      mmbasename = cachename( clist , i ) ;
      if ( mmbasename == NULL )
        return error_handler( VMERROR ) ;

      theTags( nameobj ) = ONAME | LITERAL ;
      oName( nameobj ) = mmbasename ;
      return (pdf_findresource(NAME_Font, &nameobj) && pop_(pscontext)) ;
    }
  }

  /* Badly formed MM font, since couldn't find an underscore which must
   * exist to separate the dimensions, so return an error.
   */
  return error_handler( INVALIDFONT ) ;
}

/* --------------------------------------------------------------------------
**  Given a font name, strips any subset prefix that it may have.
**  Subset prefixes are always six capital letters followed by a '+' sign
**  (PDF spec. 1.3, sect. 7.7.4).  The function returns TRUE to indicate
**  that a subset was indeed present and *stipped_name is returned to
**  point to the start of the name-stem (after the prefix) within the
**  given name - caller beware!.  If the function returns FALSE, then
**  no subset prefix is present.
*/
static Bool pdf_strip_subset(uint8 *fullname,
                             int32 namelen,
                             OBJECT *strippedname)
{
  HQASSERT(fullname && namelen > 0, "fullname is not valid" ) ;
  HQASSERT( strippedname, "strippedname NULL in Pdf_strip_subset" ) ;

  HQTRACE( debug_findfonts, ("font name (strip subset?): %.*s",
                             namelen, fullname) );

  /* Check if a subset font */
  if ( namelen > 7 ) {
    /* May be a subset font; check and extract PS name if so. */
    if ( isupper( fullname[ 0 ] ) && isupper( fullname[ 1 ] ) &&
         isupper( fullname[ 2 ] ) && isupper( fullname[ 3 ] ) &&
         isupper( fullname[ 4 ] ) && isupper( fullname[ 5 ] ) &&
         fullname[ 6 ] == '+' ) {
      int32 offset = 7 ;

      /* Apparently some fonts use "++", so support this. */
      if ( namelen > 8 && fullname[ 7 ] == '+' )
        offset = 8 ;

      theTags(*strippedname) = OSTRING | READ_ONLY | LITERAL ;
      oString(*strippedname) = fullname + offset;
      theLen(*strippedname) = CAST_TO_UINT16(namelen - offset);

      HQTRACE( debug_findfonts, ("PDF fontname (no subset): %.*s",
                                 theLen(*strippedname),
                                 oString(*strippedname)) );
      return TRUE;  /* subset was present */
    }
  }

  return FALSE;   /* subset was not present */
}


/* --------------------------------------------------------------------------
** Given a font name, strips out any embedded style name(s), returning a mask
** flagging which styles (e.g. bold, italic) were found.  Note that if we're
** doing strict PDF, then only one style name is allowed.  The function will
** return FALSE to indicate that no styles were present.
**
** This no longer insists on the style being a suffix, and strips PS flavour
** styles too, so that it is possible to match TimeNewRomanPSMT,Bold to
** TimesNewRomanPS-BoldMT [29601].
*/
static Bool pdf_strip_styles( int32    be_strict,
                              uint8   *name,
                              int32    namelen,
                              OBJECT  *strippedname, /* returned as OSTIRNG */
                              int32   *style_flags )
{
  char str[MAXPSNAME + 1], *found, *prev = str;

  HQASSERT(name && namelen >= 0, "name is not valid" ) ;
  HQASSERT( strippedname, "strippedname NULL in Pdf_strip_styles" ) ;
  HQASSERT( style_flags, "style_flags NULL in Pdf_strip_styles" ) ;
  HQASSERT(namelen < 128, "name is too long") ;

  *style_flags = FSTYLE_None;

  /* copy name into working buffer and terminate */
  HqMemCpy(str, name, namelen);
  str[namelen] = 0;

  /* Check for style-strings - "Italic", "Bold" or "BoldItalic".
   * Continuously extract font styles (unless strict PDF is required)
   * building the font style mask as we do so.
   *
   * Note that we check from the previously found style, so that we don't
   * accidentally construct styles, eg "Contrived,Bo-Italicld" should be
   * Italic, not BoldItalic.
   */
  do {
    int32 len = 0;

    if ( (found = strstr(prev, "-BoldItalic")) != 0 ||
         (found = strstr(prev, ",BoldItalic")) != 0 ||
         (found = strstr(prev, " BoldItalic")) != 0 ) {

      HQTRACE(debug_findfonts,("BoldItalic style"));
      *style_flags |= FSTYLE_Italic | FSTYLE_Bold;
      len = 11;

    } else if ( (found = strstr(prev, "-Bold")) != 0 ||
                (found = strstr(prev, ",Bold")) != 0 ||
                (found = strstr(prev, " Bold")) != 0 ||
                /* The following is a hacky work around for
                 * a single faulty font. [368499] */
                (found = strstr(prev, "-Bol1")) == str + namelen - 5 ) {

      HQTRACE(debug_findfonts,("Bold style"));
      *style_flags |= FSTYLE_Bold;
      len = 5;

    } else if ( (found = strstr(prev, "-Italic")) != 0 ||
                (found = strstr(prev, ",Italic")) != 0 ||
                (found = strstr(prev, " Italic")) != 0 ) {

      HQTRACE(debug_findfonts,("Italic style"));
      *style_flags |= FSTYLE_Italic;
      len = 7;
    }
    if ( found ) {
      char *i = found, *j = found+len;
      while ( (*(i++) = *(j++)) != 0 );
      namelen -= len;
      prev = found;
    }
  } while (found && !be_strict) ;

  if ( *style_flags != FSTYLE_None ) {
    NAMECACHE* temp = cachename( (const uint8*) str, namelen );

    if ( temp == NULL )
      return FALSE;
    theTags(*strippedname) = OSTRING | READ_ONLY | LITERAL ;
    oString(*strippedname) = temp->clist;
    theLen(*strippedname) = CAST_TO_UINT16(namelen);

    HQTRACE(debug_findfonts,("TT fontname (no styles): %.*s", namelen, name ) );

    return TRUE;  /* style(s) were present */
  }

  return FALSE; /* styles not present */
}


/* -------------------------------------------------------------------------- */
static Bool pdf_setdescriptor( PDFCONTEXT *pdfc ,
                               OBJECT *fontdesc ,
                               NAMECACHE *basename ,
                               NAMECACHE **opsname ,
                               int32 *fontdownload ,
                               int32 fontenum ,
                               OBJECT *fontstream,
                               int32 *flags,
                               PDF_WIDTHDETAILS *width_details )
{
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  static NAMETYPEMATCH fdesc_match[] = {
    /* Use the enum below to index this match */
    { NAME_Type,                  2, { ONAME, OINDIRECT }},
    /* /Ascent, CapHeight & Descent are optional if not strict pdf. */
    { NAME_Ascent,                3, { OINTEGER, OREAL, OINDIRECT }},
    { NAME_CapHeight,             3, { OINTEGER, OREAL, OINDIRECT }},
    { NAME_Descent,               3, { OINTEGER, OREAL, OINDIRECT }},
    { NAME_Flags,                 2, { OINTEGER, OINDIRECT }},
    { NAME_FontBBox,              3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_FontName,              2, { ONAME, OINDIRECT }},
    { NAME_ItalicAngle,           3, { OREAL, OINTEGER, OINDIRECT }},
    { NAME_StemV,                 3, { OREAL, OINTEGER, OINDIRECT }},

    { NAME_AvgWidth | OOPTIONAL,  3, { OINTEGER, OREAL, OINDIRECT }},

    { NAME_FontFile | OOPTIONAL,  2, { OFILE, OINDIRECT }},
    { NAME_FontFile2 | OOPTIONAL, 2, { OFILE, OINDIRECT }},
    { NAME_FontFile3 | OOPTIONAL, 2, { OFILE, OINDIRECT }},

    { NAME_Leading | OOPTIONAL,   3, { OINTEGER, OREAL, OINDIRECT }},
    { NAME_MaxWidth | OOPTIONAL,  3, { OINTEGER, OREAL, OINDIRECT }},
    { NAME_MissingWidth | OOPTIONAL,
                                        3, { OINTEGER, OREAL, OINDIRECT }},
    { NAME_StemH | OOPTIONAL,     3, { OREAL, OINTEGER, OINDIRECT }},
    { NAME_XHeight | OOPTIONAL,   3, { OINTEGER, OREAL, OINDIRECT }},
    { NAME_CharSet | OOPTIONAL,   2, { OSTRING, OINDIRECT }},
    DUMMY_END_MATCH
  } ;

  static NAMETYPEMATCH strict_FontName =
     { NAME_FontName, 2, { ONAME, OINDIRECT }} ;

  /* Acrobat copes with FontName provided as a string */
  static NAMETYPEMATCH notstrict_FontName =
     { NAME_FontName | OOPTIONAL, 3, { ONAME, OSTRING, OINDIRECT }} ;

  enum {
    fdesc_match_Type, fdesc_match_Ascent, fdesc_match_CapHeight,
    fdesc_match_Descent, fdesc_match_Flags, fdesc_match_FontBBox,
    fdesc_match_FontName, fdesc_match_ItalicAngle, fdesc_match_StemV,
    fdesc_match_AvgWidth, fdesc_match_FontFile, fdesc_match_FontFile2,
    fdesc_match_FontFile3, fdesc_match_Leading, fdesc_match_MaxWidth,
    fdesc_match_MissingWidth, fdesc_match_StemH, fdesc_match_XHeight,
    fdesc_match_CharSet
  } ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( fontdesc, "fontdesc NULL in pdf_setdescriptor" ) ;
  HQASSERT( opsname,  "opsname NULL in pdf_setdescriptor" ) ;
  HQASSERT( fontstream, "fontstream NULL in pdf_setdescriptor" ) ;
  HQASSERT( width_details , "width_details NULL in pdf_setdescriptor" ) ;

  /* See if we can find the Font Descriptor Details. */
  if ( oType( *fontdesc ) == OINDIRECT ) {

    if ( ! pdf_lookupxref( pdfc , & fontdesc ,
                           oXRefID( *fontdesc ) ,
                           theGen(*fontdesc) ,
                           FALSE ))
      return FALSE ;

    if ( fontdesc == NULL )
      return error_handler( UNDEFINEDRESOURCE ) ;
  }

  *fontdownload = 0;

  if ( ! ixc->strictpdf && oType( *fontdesc ) == ONULL ) {
    /* Strictly speaking, fontdesc dictionary shouldn't be null. */
    *opsname = NULL ;
    width_details->wid_validw = FALSE ;
    return TRUE ;
  }
  else if ( oType( *fontdesc ) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;

  if ( ! ixc->strictpdf ) {
    fdesc_match[fdesc_match_Type].name |= OOPTIONAL ;
    fdesc_match[fdesc_match_Ascent].name |= OOPTIONAL ;
    fdesc_match[fdesc_match_CapHeight].name |= OOPTIONAL ;
    fdesc_match[fdesc_match_Descent].name |= OOPTIONAL ;
    fdesc_match[fdesc_match_StemV].name |= OOPTIONAL ;
    fdesc_match[fdesc_match_FontName] = notstrict_FontName ;
    if ( fontenum == FONT_TypeCID ) {
      fdesc_match[fdesc_match_Flags].name |= OOPTIONAL ;
      fdesc_match[fdesc_match_ItalicAngle].name |= OOPTIONAL ;
    }
  } else {
    fdesc_match[fdesc_match_Type].name &= ~OOPTIONAL ;
    fdesc_match[fdesc_match_Ascent].name &= ~OOPTIONAL ;
    fdesc_match[fdesc_match_CapHeight].name &= ~OOPTIONAL ;
    fdesc_match[fdesc_match_Descent].name &= ~OOPTIONAL ;
    fdesc_match[fdesc_match_StemV].name &= ~OOPTIONAL ;
    fdesc_match[fdesc_match_FontName] = strict_FontName ;
    fdesc_match[fdesc_match_Flags].name &= ~OOPTIONAL ;
    fdesc_match[fdesc_match_ItalicAngle].name &= ~OOPTIONAL ;
  }

  if ( ! pdf_dictmatch( pdfc , fontdesc , fdesc_match ))
    return FALSE ;

  if ( fdesc_match[fdesc_match_MissingWidth].result ) {
    width_details->wid_validw = TRUE ;
    width_details->wid_mwidth = (int32)object_numeric_value(fdesc_match[fdesc_match_MissingWidth].result) ;
  } else {
    width_details->wid_validw = FALSE ;
    width_details->wid_mwidth = 0 ;
  }

  if ( fdesc_match[fdesc_match_Flags].result ) {
    /* Flags should have either bit 2 or bit 5 (value 4 or 32) set */
    *flags = oInteger(*fdesc_match[fdesc_match_Flags].result) ;
    if ( ixc->strictpdf ) {
      int32 symbolic = (*flags & 4) != 0 ;
      int32 nonsymbolic = (*flags & 32) != 0 ;
      if ( symbolic == nonsymbolic )
        return detail_error_handler(RANGECHECK,
                                    "Font descriptor Flags are neither symbolic nor non-symbolic.") ;
    }
  } else {
    /* We will only come here, if we are dealing with CID fonts, for which we allow Flags to be optional */
    *flags = 32 ; /* nonsymbolic */
  }

  /* Return the "post script" font name object */
  if ( fdesc_match[fdesc_match_FontName].result ) {
    if ( oType(*fdesc_match[fdesc_match_FontName].result) == ONAME )
      *opsname = oName(*fdesc_match[fdesc_match_FontName].result);
    else {
      *opsname = cachename(oString(*fdesc_match[fdesc_match_FontName].result),
                           theLen(*fdesc_match[fdesc_match_FontName].result));
      if ( *opsname == NULL )
        return FALSE;
    }
  } else {
    if (basename)
      *opsname = basename ;
    else
      return detail_error_handler(INVALIDFONT, "Font descriptor FontName missing.") ;
  }

  /* Determine which /FontFile key we've matched, if any */
  object_store_null(fontstream);

  if ( fdesc_match[fdesc_match_FontFile].result ) {
    *fontdownload = 1;
    Copy( fontstream, fdesc_match[fdesc_match_FontFile].result );
  }

  if ( fdesc_match[fdesc_match_FontFile2].result ) {
    *fontdownload = 2;
    Copy( fontstream, fdesc_match[fdesc_match_FontFile2].result );
  }

  if ( fdesc_match[fdesc_match_FontFile3].result ) {
    *fontdownload = 3;
    Copy( fontstream, fdesc_match[fdesc_match_FontFile3].result );
  }

  if (*fontdownload == 0) {
    if (! pdfxExternalFontDetected(pdfc, TRUE))
      return FALSE;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* [12803] The PFB font filter - strip out the PFB header and chunk headers
   leaving the font data that should have been inserted into the PDF.

   We use the FILELIST's u.descriptor (which is at least an int32) to store
   the number of bytes left in the current chunk. This is a filter not a file,
   so that field wouldn't otherwise be used and is all we need.
 */

#define PFBFONTBUFFSIZE 1024
static FILELIST pfbFontFilter = {tag_LIMIT} ;

/* Does the file look like a PFB? */
static Bool filelist_is_pfb(FILELIST *file)
{
  int32 c = Getc(file) ;
  UnGetc(c, file) ;

  return (c == 0x80) ; /* A Type1 has no business starting with 0x80 */
}


/* Initialise the new pfbFontFilter, zeroing the chunk data count */
static Bool pfbFontFilterInit(FILELIST *filter, OBJECT *args, STACK *stack)
{
  UNUSED_PARAM(STACK *, stack) ;

  HQASSERT(args != NULL && stack == NULL, "Args but no stack please") ;

  if ( !filter_target_or_source(filter, args) )
    return FALSE ;

  /* Allocate the buffer */
  theIBuffer(filter) = mm_alloc(mm_pool_temp, PFBFONTBUFFSIZE+1,
                                MM_ALLOC_CLASS_FILTER_BUFFER) ;
  if (theIBuffer(filter) == NULL)
    return error_handler(VMERROR) ;

  theIBuffer(filter)++ ;
  theIPtr(filter) = theIBuffer(filter) ;
  theICount(filter) = 0 ;
  theIBufferSize(filter) = PFBFONTBUFFSIZE ;
  theIFilterState(filter) = FILTER_INIT_STATE ;

  /* No bytes left in current chunk - so read new chunk header */
  theIDescriptor(filter) = 0 ;

  return TRUE ;
}


/* Discard the pfbFontFilter */
static void pfbFontFilterDispose(FILELIST *filter)
{
  HQASSERT(filter, "filter NULL in pfbFontFilterDispose.");

  if (theIBuffer(filter)) {
    mm_free(mm_pool_temp, theIBuffer(filter) - 1, PFBFONTBUFFSIZE + 1) ;
    theIBuffer(filter) = NULL ;
  }
  theIDescriptor(filter) = 0 ;
}


/* Refill the pfnFontFilter buffer, stripping chunk headers */
static Bool pfbFontDecodeBuffer(FILELIST *filter, int32 *ret_bytes)
{
  FILELIST *source ;
  uint8    *ptr ;
  int32    count = 0, left, c ;
  int      i ;

  HQASSERT(filter, "filter NULL in pfbFontDecodeBuffer") ;

  source = theIUnderFile(filter) ;
  ptr    = theIBuffer(filter) ;
  left   = DEVICE_FILEDESCRIPTOR_TO_INT32(theIDescriptor(filter)) ;

  HQASSERT(source, "No underlying file in pfbFontDecodeBuffer") ;
  HQASSERT(ptr, "NULL buffer in pfbFontDecodeBuffer") ;
  HQASSERT(left >= 0, "Silly chunk data count in pfbFontDecodeBuffer") ;

  for (i=0; i<PFBFONTBUFFSIZE; i++) {
    if (--left < 0) {
      /* Finished the last chunk, so read in the next chunk header */
      if (Getc(source) != 0x80)
        return error_handler(INVALIDFONT) ;  /* PFB chunks start with 0x80 */
      c = Getc(source) ;
      if (c == 3) {
        /* eof */
        count = -count ;
        break ; /* out of the for loop */

      } else if (c == 1 || c == 2) {
        /* text or binary (we don't care) - read amount of data following */
        int j ;
        left = 0 ;
        for (j=0; j<4; ++j) {
          c = Getc(source) ;
          if (c == EOF)
            return error_handler(INVALIDFONT) ; /* truncated chunk header */
          left = ((left >> 8) & 0xffffff) | (c << 24) ;
        }
        if (--left < 0) /* decremented as we tried to in the topmost if */
          return error_handler(INVALIDFONT) ; /* silly chunk length */

      } else { /* undefined chunk type */
        return error_handler(INVALIDFONT) ; /* unrecognised PFB chunk */
      }
    } /* endif */

    c = Getc(source) ;
    if (c == EOF)
      return error_handler(INVALIDFONT) ; /* truncated chunk data */
    *ptr++ = (uint8) c ;
    count++ ;                             /* left already decremented */
  } /* buffer full or EOF reached */

  theIDescriptor(filter) = INT32_TO_DEVICE_FILEDESCRIPTOR(left) ;
  *ret_bytes = count ;
  return TRUE ;
}


FILELIST * pfb_font_filter()
{
  if (pfbFontFilter.typetag == tag_LIMIT) {
    init_filelist_struct(&pfbFontFilter, NAME_AND_LENGTH("pfbFontFilter"),
                         FILTER_FLAG | READ_FLAG, 0, NULL, 0,
                         FilterFillBuff, FilterFlushBufError, pfbFontFilterInit,
                         FilterCloseFile, pfbFontFilterDispose, FilterBytes,
                         FilterReset, FilterPos, FilterSetPos, FilterFlushFile,
                         FilterEncodeError, pfbFontDecodeBuffer,
                         FilterLastError, -1, NULL, NULL, NULL) ;
  }
  return &pfbFontFilter ;
}

/* -------------------------------------------------------------------------- */

static Bool pdf_download_t1(PDFCONTEXT *pdfc, OBJECT *fontstream,
                            Bool is_cid, int32 encoding,
                            OBJECT *cidsysinfo,
                            OBJECT *font_names, OBJECT *newfont,
                            NAMECACHE *fontname)
{
  ps_context_t *pscontext ;
  OBJECT *streamdict ;
  OBJECT *fdict ;
  STACK_POSITIONS stacks ;
  OBJECT stopped_op = OBJECT_NOTVM_OPERATOR(&system_ops[NAME_stopped]) ;
  OBJECT dataobj = OBJECT_NOTVM_NOTHING ;

  static NAMETYPEMATCH t1font_match[] = {
    /* Index using enum below */
    { NAME_Subtype | OOPTIONAL,      2, { ONAME, OINDIRECT }},
    { NAME_Length1,                  2, { OINTEGER, OINDIRECT }},
    { NAME_Length2,                  2, { OINTEGER, OINDIRECT }},
    { NAME_Length3,                  2, { OINTEGER, OINDIRECT }},
    DUMMY_END_MATCH
  } ;
  enum { t1font_match_Subtype, t1font_match_Length1,
         t1font_match_Length2, t1font_match_Length3 } ;

  UNUSED_PARAM( Bool, is_cid ) ;
  UNUSED_PARAM( OBJECT *, cidsysinfo ) ;
  UNUSED_PARAM( int32 , encoding ) ;

  PDF_CHECK_MC( pdfc ) ;

  HQASSERT(pdfc->pdfxc != NULL, "No PDF execution context") ;
  HQASSERT(pdfc->pdfxc->corecontext != NULL, "No core context") ;
  pscontext = pdfc->pdfxc->corecontext->pscontext ;
  HQASSERT(pscontext != NULL, "No PostScript context") ;

  HQASSERT(font_names && oType(*font_names) == OARRAY,
           "Font names is not an array") ;
  HQTRACE(debug_findfonts,("Downloading t1..."));

  streamdict = streamLookupDict( fontstream ) ;
  if ( streamdict == NULL )
    return error_handler( UNDEFINED ) ;

  if ( ! pdf_dictmatch( pdfc , streamdict , t1font_match ))
    return FALSE ;

#if defined( DEBUG_BUILD )
  if ( tfont_number > 0 && !tee_font_download(fontstream) )
    return FALSE ;
#endif

  if ( (fdict = fast_extract_hash_name(&internaldict, NAME_FontDescriptor)) != NULL ) {
    if ( oType(*fdict) == ODICTIONARY ) {
      /* Ignore the return status of remove_hash(), let the font resource
         machinery fail if it's going to. */
      oName(nnewobj) = &system_names[NAME_FontDict] ;
      (void)remove_hash(fdict, &nnewobj, FALSE) ;

      oName(nnewobj) = &system_names[NAME_FontName] ;
      (void)remove_hash(fdict, &nnewobj, FALSE) ;
    } else {
      HQFAIL("internaldict /FontDescriptor reference is not a dictionary") ;
      fdict = NULL ;
    }
  }

  /* [12803] Check whether the font is still in PFB format. This is illegal,
     but we can recover the correct data with a filter. */
  if (filelist_is_pfb(oFile(*fontstream))) {
    FILELIST * pfbFilter = pfb_font_filter() ;
    if ( !filter_create_object(pfbFilter, &dataobj, fontstream, NULL) )
      return FALSE;
    theTags(dataobj) |= EXECUTABLE ; /* we're going to run the contents */

    fontstream = &dataobj ;  /* old fontstream not needed after this point */
  }

  saveStackPositions(&stacks) ;  /* [62518] & [64325] */

  /* Run the type 1 font definition in a stopped context to allow PDF processing
   * to continue, even if the type 1 resource has errors. Some embeded type 1
   * resources have errors after they successfully define a font resource, and
   * we should continue the job in those cases. */
  if ( !interpreter_clean(&stopped_op, fontstream, NULL) ||
       !pop_(pscontext)) { /* discard result of "stopped" */
    /* If the interpreter call failed, the PDF stream we passed in will have
    been copied into the $error dictionary; this reference will become invalid
    once the PDF memory has been deallocated, so throw another error now to
    force the containing interpreter to refill the $error dictionary (without
    the offending PDF stream). */
    error_clear_context(ps_core_context(pscontext)->error);
    return error_handler(INVALIDFONT);
  }

  if ( oInteger( *t1font_match[t1font_match_Length3].result ) == 0 )
    if ( ! cleartomark_(pscontext))
      return FALSE ;

  /* [62518] & [64325] & [65936] Fonts do weird things, tidy up after them */
  if ( !restoreStackPositions(&stacks, TRUE) )
    return error_handler(INVALIDFONT) ;

  /* definefont stores the font dictionary in an internaldict dictionary that
     we can use to find it easily. */
  if ( fdict ) {
    OBJECT *theo ;
    if ( (theo = fast_extract_hash_name(fdict, NAME_FontDict)) != NULL ) {
      Copy(newfont, theo) ;
    } else {
      if ( fontname ) {
        /* look for the font anyway [12434] and [66322] */
        OBJECT findresource = OBJECT_NOTVM_NAME(NAME_findresource, EXECUTABLE) ;
        OBJECT catname = OBJECT_NOTVM_NAME(NAME_Font, LITERAL) ;
        OBJECT fontnameobj = OBJECT_NOTVM_NAME(0, LITERAL) ;
        oName(fontnameobj) = fontname ;

        if ( interpreter_clean(&findresource, &fontnameobj, &catname, NULL) ) {
          theo = theTop( operandstack ) ;
          Copy(newfont, theo) ;
          Copy(font_names, &fontnameobj) ;
        }
        pop( &operandstack ) ;
      }
    }
    if ( (theo = fast_extract_hash_name(fdict, NAME_FontName)) != NULL )
      Copy(font_names, theo) ;
  }

  return TRUE ;
}


/* -------------------------------------------------------------------------- */
static Bool pdf_download_tt(PDFCONTEXT *pdfc, OBJECT *fontstream,
                            Bool is_cid, int32 encoding,
                            OBJECT *cidsysinfo,
                            OBJECT *font_names, OBJECT *newfont,
                            Bool is_cff_otf, Bool * found_3_1)
{
  OBJECT *streamdict ;
  OBJECT cidflag = OBJECT_NOTVM_NOTHING ;
  OBJECT *theo ;

  HQASSERT(font_names && oType(*font_names) == OARRAY,
           "Font names is not an array") ;
  HQTRACE(debug_findfonts,("Downloading tt..."));

  if (found_3_1)
    *found_3_1 = FALSE ;

  streamdict = streamLookupDict( fontstream ) ;
  if ( streamdict == NULL )
    return error_handler( UNDEFINED ) ;

  /* Search for the fonts in the order specified. */
  if ( ! pdf_fast_insert_hash_name(pdfc, streamdict, NAME_SubFont, font_names) )
    return FALSE ;

  /* Assume it is the top-priority FontName for the resultant TT font. */
  if ( ! pdf_fast_insert_hash_name(pdfc, streamdict, NAME_FontName, oArray(*font_names)) )
    return FALSE ;

  /* Add a flag to say if this is a CID font or not. */
  object_store_bool(&cidflag, is_cid) ;
  if ( ! pdf_fast_insert_hash_name(pdfc, streamdict, NAME_CIDFont, &cidflag) )
    return FALSE ;

  if ( encoding >= 0 ) { /* Note implicit base encoding. */
    OBJECT enc = OBJECT_NOTVM_NOTHING ;
    object_store_name(&enc, encoding, LITERAL) ;
    if ( ! pdf_fast_insert_hash_name(pdfc, streamdict, NAME_Encoding, &enc) )
      return FALSE ;
  }

  if ( cidsysinfo ) {
    if ( ! pdf_fast_insert_hash_name(pdfc, streamdict, NAME_CIDSystemInfo, cidsysinfo) )
      return FALSE ;
  }

#if defined( DEBUG_BUILD )
  {
    OBJECT fontstream_copy = OBJECT_NOTVM_NOTHING ;
    OCopy(fontstream_copy, *fontstream); /* local copy for teeing */
    if ( tfont_number > 0 && !tee_font_download(&fontstream_copy) )
      return FALSE ;
  }
#endif

  /* We need a seekable stream that isn't freed until the end of the
     job because we don't know how long the PS world will hang on to
     the font we define here. */
  if ( !pdf_xrefexplicitaccess_stream(pdfc->pdfxc,fontstream,TRUE) ||
       !pdf_xrefmakeseekable_stream(pdfc->pdfxc,fontstream)) {
    return FALSE ;
  }

  /* Define a PostScript VM stub using the RSD as the underlying file. */
  if (is_cff_otf) {
    /* We only do this if it is a cff otf, not a tt otf! */
    if ( !otf_definefont(newfont, streamdict, fontstream) )
     return FALSE ;
  } else {
    if ( !tt_definefont(newfont, streamdict, fontstream, found_3_1) )
      return FALSE ;
  }

  /* Extract the font name matched from the font dictionary. This tells us
     what the selected font was, rather than the name overridden by the
     FontName parameter. */
  if ( (theo = fast_extract_hash_name(newfont, NAME_CIDFontName)) != NULL ||
       (theo = fast_extract_hash_name(newfont, NAME_FontName)) != NULL )
    Copy(font_names, theo) ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
#if defined( ANY_EMBEDDED_TYPE_ALLOWED )
static Bool pdf_download_t3( PDFCONTEXT *pdfc , OBJECT *fontstream ,
                             Bool is_cid, int32 encoding,
                             OBJECT *cidsysinfo,
                             OBJECT *font_names, OBJECT *newfont )
{
  UNUSED_PARAM( PDFCONTEXT * , pdfc ) ;
  UNUSED_PARAM( OBJECT * , fontstream ) ;
  UNUSED_PARAM( Bool, is_cid ) ;
  UNUSED_PARAM( OBJECT *, cidsysinfo ) ;
  UNUSED_PARAM( int32 , encoding ) ;
  UNUSED_PARAM( OBJECT * , font_names ) ;
  UNUSED_PARAM( OBJECT * , newfont ) ;

  /* This is essentially a nop since we never want to download T3 fonts. */
  HQTRACE(debug_findfonts,("Doc doesn't say anything about downloading T3 fonts..."));
  return TRUE ;
}
#endif

/* -------------------------------------------------------------------------- */
#if defined( ANY_EMBEDDED_TYPE_ALLOWED )
static Bool pdf_download_mm( PDFCONTEXT *pdfc , OBJECT *fontstream ,
                             Bool is_cid, int32 encoding,
                             OBJECT *cidsysinfo,
                             OBJECT *font_names, OBJECT *newfont)
{
  UNUSED_PARAM( PDFCONTEXT * , pdfc ) ;
  UNUSED_PARAM( OBJECT * , fontstream ) ;
  UNUSED_PARAM( Bool, is_cid ) ;
  UNUSED_PARAM( OBJECT *, cidsysinfo ) ;
  UNUSED_PARAM( int32 , encoding ) ;
  UNUSED_PARAM( OBJECT * , font_names ) ;
  UNUSED_PARAM( OBJECT * , newfont ) ;

  HQTRACE(debug_findfonts,("Doc doesn't say anything about downloading MM fonts..."));
  return TRUE ;
}
#endif

/* -------------------------------------------------------------------------- */
#if defined( ANY_EMBEDDED_TYPE_ALLOWED )
static Bool pdf_download_t0( PDFCONTEXT *pdfc , OBJECT *fontstream ,
                             Bool is_cid, int32 encoding,
                             OBJECT *cidsysinfo,
                             OBJECT *font_names, OBJECT *newfont )
{
  UNUSED_PARAM( PDFCONTEXT * , pdfc ) ;
  UNUSED_PARAM( OBJECT * , fontstream ) ;
  UNUSED_PARAM( Bool, is_cid ) ;
  UNUSED_PARAM( OBJECT *, cidsysinfo ) ;
  UNUSED_PARAM( int32 , encoding ) ;
  UNUSED_PARAM( OBJECT * , font_names ) ;
  UNUSED_PARAM( OBJECT * , newfont ) ;

  HQTRACE(debug_findfonts,("Doc doesn't say anything about downloading T0 fonts..."));
  return TRUE ;
}
#endif

/* -------------------------------------------------------------------------- */
#if defined( ANY_EMBEDDED_TYPE_ALLOWED )
static Bool pdf_download_tcid( PDFCONTEXT *pdfc , OBJECT *fontstream ,
                               Bool is_cid, int32 encoding,
                               OBJECT *cidsysinfo,
                               OBJECT *font_names, OBJECT *newfont )
{
  UNUSED_PARAM( PDFCONTEXT * , pdfc ) ;
  UNUSED_PARAM( OBJECT * , fontstream ) ;
  UNUSED_PARAM( Bool, is_cid ) ;
  UNUSED_PARAM( OBJECT *, cidsysinfo ) ;
  UNUSED_PARAM( int32 , encoding ) ;
  UNUSED_PARAM( OBJECT * , font_names ) ;
  UNUSED_PARAM( OBJECT * , newfont ) ;

  HQTRACE(debug_findfonts,("Doc doesn't say anything about downloading CID fonts..."));
  return TRUE ;
}
#endif

/* -------------------------------------------------------------------------- */
static Bool pdf_download_cff(PDFCONTEXT *pdfc, OBJECT *fontstream,
                             Bool is_cid, int32 encoding,
                             OBJECT *cidsysinfo,
                             OBJECT *font_names, OBJECT *newfont)
{
  OBJECT *streamdict ;
  OBJECT *theo ;

  UNUSED_PARAM( Bool, is_cid ) ;
  UNUSED_PARAM( OBJECT *, cidsysinfo ) ;
  UNUSED_PARAM( int32 , encoding ) ; /* Implicit already */

  HQTRACE(debug_findfonts,("Downloading CFF..."));

  HQASSERT(font_names && oType(*font_names) == OARRAY,
           "Font names is not an array") ;

  streamdict = streamLookupDict( fontstream ) ;
  if ( streamdict == NULL )
    return error_handler( UNDEFINED ) ;

  /* Set the list of font names that will be searched for. */
  if ( ! pdf_fast_insert_hash_name(pdfc, streamdict, NAME_SubFont, font_names) )
    return FALSE ;

  /* Assume it is the top-priority FontName for the resultant CFF font. */
  if ( ! pdf_fast_insert_hash_name(pdfc, streamdict, NAME_FontName,
                                   oArray(*font_names)) )
    return FALSE ;

  /* Add a flag to tell the PS code that its being called from a PDF file. */
  oInteger(inewobj) = (is_cid) ? 2 : 1 ; /* ...and that CID is needed [12764] */
  if ( ! pdf_fast_insert_hash_name(pdfc, streamdict, NAME_PDF, &inewobj) )
    return FALSE ;

  if ( encoding >= 0 ) { /* Note implicit base encoding. */
    OBJECT enc = OBJECT_NOTVM_NOTHING ;
    object_store_name(&enc, encoding, LITERAL) ;
    if ( ! pdf_fast_insert_hash_name(pdfc, streamdict, NAME_Encoding, &enc) )
      return FALSE ;
  }

#if defined( DEBUG_BUILD )
  {
    OBJECT fontstream_copy = OBJECT_NOTVM_NOTHING ;
    OCopy(fontstream_copy, *fontstream); /* local copy for teeing */
    if ( tfont_number > 0 && !tee_font_download(&fontstream_copy) )
      return FALSE ;
  }
#endif

  /* We need a seekable stream that isn't freed until the end of the
     job because we don't know how long the PS world will hang on to
     the font we define here. */
  if ( !pdf_xrefexplicitaccess_stream(pdfc->pdfxc,fontstream,TRUE) ||
       !pdf_xrefmakeseekable_stream(pdfc->pdfxc,fontstream)) {
    return FALSE ;
  }

  /* Define a PostScript VM stub using the RSD as the underlying file. */
  if ( !cff_definefont(newfont, streamdict, fontstream) )
    return FALSE ;

  /* Extract the font name matched from the font dictionary. This tells us
     what the selected font was, rather than the name overridden by the
     FontName parameter. */
  if ( (theo = fast_extract_hash_name(newfont, NAME_CIDFontName)) != NULL ||
       (theo = fast_extract_hash_name(newfont, NAME_FontName)) != NULL )
    Copy(font_names, theo) ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool pdf_download_tX(PDFCONTEXT *pdfc, OBJECT *fontstream,
                            Bool is_cid, int32 encoding,
                            OBJECT *cidsysinfo,
                            OBJECT *font_names, OBJECT *newfont,
                            PDF_FONTDETAILS *font_details, Bool * found_3_1)
{
  NAMECACHE *name ;
  OBJECT *streamdict ;

  static NAMETYPEMATCH tXfont_match[] = {
  /* 0 */ { NAME_Subtype,                  2, { ONAME, OINDIRECT }},
           DUMMY_END_MATCH
  } ;

  streamdict = streamLookupDict( fontstream ) ;
  if ( streamdict == NULL )
    return error_handler( UNDEFINED ) ;

  if ( ! pdf_dictmatch( pdfc , streamdict , tXfont_match ))
    return FALSE ;

  name = oName( *tXfont_match[ 0 ].result ) ;
  switch (( int32 )theINameNumber( name )) {
#if defined( ANY_EMBEDDED_TYPE_ALLOWED )
  case NAME_Type1:
    return pdf_download_t1(pdfc, fontstream, is_cid, encoding, cidsysinfo, font_names, newfont, NULL) ;
  case NAME_TrueType:
    return pdf_download_tt(pdfc, fontstream, is_cid, encoding, cidsysinfo, font_names, newfont, FALSE, found_3_1) ;
  case NAME_Type3:
    return pdf_download_t3(pdfc, fontstream, is_cid, encoding, cidsysinfo, font_names, newfont) ;
  case NAME_MMType1:
    return pdf_download_mm(pdfc, fontstream, is_cid, encoding, cidsysinfo, font_names, newfont) ;
  case NAME_Type0:
    return pdf_download_t0(pdfc, fontstream, is_cid, encoding, cidsysinfo, font_names, newfont) ;
  case NAME_CIDFontType2:
    HQFAIL("May need to use parent CMap to select character set") ;
    return pdf_download_tt(pdfc, fontstream, is_cid, encoding, cidsysinfo, font_names, newfont, FALSE, found_3_1) ;
#endif

  case NAME_Type1C:
  case NAME_CIDFontType0C:
    return pdf_download_cff(pdfc, fontstream, is_cid, encoding, cidsysinfo, font_names, newfont) ;

  case NAME_OpenType:
    return pdf_download_tt(pdfc, fontstream, is_cid, encoding, cidsysinfo, font_names, newfont,
                           font_details->font_type == FONT_Type1 ||
                           (font_details->font_type == FONT_TypeCID &&
                            font_details->font_sub == FSUB_CID0), found_3_1 ) ;

  default:
    HQTRACE(debug_findfonts,("Unknown embedded font type: %.*s",name->len,name->clist));
    return error_handler( UNDEFINED ) ;
  }

  /* NOT REACHED */
}

/* -------------------------------------------------------------------------- */
static PDF_FONTDETAILS *pdf_findfontdetails( PDF_IXC_PARAMS *ixc, OBJECT *pdffont )
{
  PDF_CACHED_FONTDETAILS *fd ;

  HQASSERT( pdffont , "pdffont NULL in pdf_findfontdetails" ) ;

  if (oType(*pdffont) == OINDIRECT) {
    for ( fd = ixc->cached_fontdetails ; fd ; fd = fd->next ) {
      if ( oXRefID( *pdffont ) == oXRefID( fd->pdffont ) &&
           theGen(*pdffont) == theGen( fd->pdffont ))
        return & fd->font_details ;
    }
  } else {
    HQASSERT( oType(*pdffont) == ODICTIONARY,
              "pdffont not a dictionary in pdf_findfontdetails()" );

    for ( fd = ixc->cached_fontdetails ; fd ; fd = fd->next ) {
      if ( oDict( *pdffont ) == oDict( fd->pdffont ) )
        return & fd->font_details ;
    }
  }
  return NULL ;
}

/* -------------------------------------------------------------------------- */
static PDF_FONTDETAILS *pdf_findfontdesc( PDFCONTEXT *pdfc ,
                                          OBJECT *fontdesc ,
                                          int32  font_type )
{
  PDF_CACHED_FONTDETAILS *fd ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( fontdesc , "fontdesc NULL in pdf_findfontdesc" ) ;

  for ( fd = ixc->cached_fontdetails ; fd ; fd = fd->next ) {
    if ( fd->psfontname && fd->font_details.font_type == font_type &&
         oXRefID( *fontdesc ) == oXRefID( fd->fontdesc ) &&
         theGen(*fontdesc) == theGen( fd->fontdesc )) {
      return ( & fd->font_details ) ;
    }
  }
  return NULL ;
}

/* -------------------------------------------------------------------------- */
/* General (macro) routines for storing indirect font details in cache */
static void *fd_cache(PDFXCONTEXT *pdfxc, void *src, int32 size, Bool *err)
{
  void *dest ;

  if ( src == NULL || *err )
    return NULL ;
  dest = (void *)mm_alloc( pdfxc->mm_structure_pool, size,
                           MM_ALLOC_CLASS_GENERAL ) ;
  if (dest == NULL) {
    (void)error_handler( VMERROR );
    *err = TRUE ;
  } else
    HqMemCpy( dest , src , size ) ;
  return dest;
}

#define fd_uncache(pdfxc, field, size) MACRO_START \
  if ( (field) != NULL ) { \
    mm_free( (pdfxc)->mm_structure_pool, ( mm_addr_t )(field), (size) ) ; \
    (field) = NULL ; \
  } \
MACRO_END


/* -------------------------------------------------------------------------- */
static PDF_FONTDETAILS *pdf_storefontdetails( PDFCONTEXT *pdfc ,
                                              PDF_FONTDETAILS *font_details ,
                                              OBJECT *pdffont ,
                                              NAMECACHE *psfontname ,
                                              OBJECT *fontdesc )
{
  PDF_CACHED_FONTDETAILS *fd ;
  PDF_FONTDETAILS *lfd ;
  OBJECT nullxrefobj = OBJECT_NOTVM_NOTHING ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  Bool err = FALSE ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  HQASSERT( font_details , "font_details NULL in pdf_storefontdetails" ) ;
  HQASSERT( pdffont , "pdffont NULL in pdf_storefontdetails" ) ;

  fd = mm_alloc( pdfxc->mm_structure_pool ,
                 sizeof( PDF_CACHED_FONTDETAILS ) ,
                 MM_ALLOC_CLASS_GENERAL ) ;
  if ( fd == NULL ) {
    (void)error_handler( VMERROR );
    return NULL ;
  }

  fd->slevel = pdfc->corecontext->savelevel ;

  /* &&&& Destined for generalised sidewards cache */
  fd->font_details = (*font_details) ; /* Copy structure */
  lfd = &(fd->font_details) ; /* Then redo pointers */
  lfd->width_details  = fd_cache( pdfxc, font_details->width_details,
                                  sizeof(PDF_WIDTHDETAILS), &err ) ;
  lfd->type3_details  = fd_cache( pdfxc, font_details->type3_details,
                                  sizeof(PDF_TYPE3DETAILS), &err ) ;
  lfd->encode_details = fd_cache( pdfxc, font_details->encode_details,
                                  sizeof(PDF_ENCODEDETAILS), &err ) ;
  lfd->cmap_details   = fd_cache( pdfxc, font_details->cmap_details,
                                  sizeof(PDF_CMAPDETAILS), &err ) ;
  lfd->cid_details    = fd_cache( pdfxc, font_details->cid_details,
                                  sizeof(PDF_CIDDETAILS), &err ) ;
  lfd->descendants    = fd_cache( pdfxc, font_details->descendants,
                                  sizeof(PDF_FONTDETAILS *) * font_details->num_descendants,
                                  &err ) ;

  /* 'err' could be set at any point during the above. Note that the Type 3
     encodings and the CIDToGIDMap are allocated separately; any error
     returns between their respective allocation points and
     pdf_storefontdetails will result in a memory leak. */
  if (err != FALSE) {
    pdf_freecachedfont( pdfxc, fd ) ;
    return NULL ;
  }

  if ( ! fontdesc ) {
    fontdesc = &nullxrefobj ;
    theTags(*fontdesc) = OINDIRECT | LITERAL ;
    theGen(*fontdesc) = 0 ;
    oXRefID( *fontdesc ) = 0 ;
  }

  Copy(object_slot_notvm(&fd->pdffont), pdffont);
  fd->psfontname = psfontname ;
  Copy(object_slot_notvm(&fd->fontdesc), fontdesc);

  fd->next = ixc->cached_fontdetails ;
  ixc->cached_fontdetails = fd ;

  return lfd ;
}

/* -------------------------------------------------------------------------- */
static void pdf_freecachedfont(PDFXCONTEXT *pdfxc, PDF_CACHED_FONTDETAILS *tmp)
{
  fd_uncache( pdfxc, tmp->font_details.width_details, sizeof( PDF_WIDTHDETAILS )) ;
  if (tmp->font_details.type3_details) {
    fd_uncache( pdfxc, tmp->font_details.type3_details->encodings,
                sizeof(OBJECT) * TYPE3_ENCODELEN ) ;
  }
  fd_uncache( pdfxc, tmp->font_details.type3_details, sizeof( PDF_TYPE3DETAILS )) ;
  fd_uncache( pdfxc, tmp->font_details.encode_details, sizeof( PDF_ENCODEDETAILS )) ;
  fd_uncache( pdfxc, tmp->font_details.cmap_details, sizeof( PDF_CMAPDETAILS )) ;
  if (tmp->font_details.cid_details) {
    fd_uncache( pdfxc, tmp->font_details.cid_details->cidtogid,
                tmp->font_details.cid_details->ncidtogids * sizeof(uint16) ) ;
  }
  fd_uncache( pdfxc, tmp->font_details.cid_details, sizeof( PDF_CIDDETAILS )) ;
  fd_uncache( pdfxc, tmp->font_details.descendants,
              sizeof(PDF_FONTDETAILS *) * tmp->font_details.num_descendants ) ;
  mm_free( pdfxc->mm_structure_pool , ( mm_addr_t )tmp ,
           sizeof( PDF_CACHED_FONTDETAILS )) ;
}

void pdf_flushfontdetails( PDFXCONTEXT *pdfxc, int32 slevel )
{
  PDF_IXC_PARAMS *ixc ;
  PDF_CACHED_FONTDETAILS **fd_ref ;

  PDF_GET_IXC( ixc ) ;

  for ( fd_ref = &ixc->cached_fontdetails ; *fd_ref != NULL ; ) {
    PDF_CACHED_FONTDETAILS *fd = *fd_ref ;

    if ( fd->slevel > slevel ) {
      *fd_ref = fd->next ;
      pdf_freecachedfont(pdfxc, fd) ;
    } else {
      fd_ref = &fd->next ;
    }
  }
}

/* -------------------------------------------------------------------------- */
static Bool pdf_setwidthdetails( PDF_WIDTHDETAILS *width_details ,
                                 OBJECT *firstchar ,
                                 OBJECT *lastchar ,
                                 OBJECT *widths )
{
  HQASSERT( width_details , "width_details NULL in pdf_setwidthdetails" ) ;

  width_details->wid_firstchar = 0 ;
  if ( firstchar )
    width_details->wid_firstchar = oInteger( *firstchar ) ;

  width_details->wid_lastchar = 255 ;
  if ( lastchar )
    width_details->wid_lastchar = oInteger( *lastchar ) ;

  width_details->wid_diffs = NULL ;
  width_details->wid_length = 0 ;
  if ( widths ) {
    int32 length = theLen(* widths ) ;
    if ( length < ( width_details->wid_lastchar - width_details->wid_firstchar + 1 ))
      return error_handler( RANGECHECK ) ;
    width_details->wid_length = length ;
    width_details->wid_diffs = oArray( *widths ) ;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool pdf_maketype3font( PDFCONTEXT *pdfc , OBJECT *pdffontdict , OBJECT *type3font )
{
  /* Fake a font with no name that is a Type3 font.
   * Add entries:
   *  /FontMatrix   [ 1 2 3 4 5 6 ]
   *  /FontType     3
   *  /BuildGlyph   -null-
   *  /Encoding     []
   *  /FID          -fid-
   */

  static NAMETYPEMATCH type3_match[] = {
  /* 0 */ { NAME_CharProcs,     2, { ODICTIONARY, OINDIRECT }},
  /* 1 */ { NAME_FontMatrix,    3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
  /* 2 */ { NAME_FontBBox,      3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
           DUMMY_END_MATCH
  } ;

  OBJECT dict = OBJECT_NOTVM_NOTHING ;
  OBJECT value = OBJECT_NOTVM_NOTHING ;

  if ( ! pdf_dictmatch( pdfc , pdffontdict , type3_match ))
    return FALSE ;

  if ( ! pdf_create_dictionary( pdfc , TYPE3_DICTLEN , & dict ))
    return FALSE ;

#if 0
  if ( ! pdf_fast_insert_hash_name(pdfc, &dict, NAME_CharProcs, type3_match[0].result) )
    return FALSE ;
#endif

  if ( ! pdf_fast_insert_hash_name(pdfc, &dict, NAME_FontMatrix, type3_match[1].result) )
    return FALSE ;

  if ( ! pdf_fast_insert_hash_name(pdfc,  &dict, NAME_FontBBox, type3_match[2].result) )
    return FALSE ;

  object_store_integer(&value, 3) ;
  if ( ! pdf_fast_insert_hash_name(pdfc, &dict, NAME_FontType, &value) )
    return FALSE ;

  if ( ! pdf_fast_insert_hash_name(pdfc, &dict, NAME_BuildGlyph, &onull) )
    return FALSE ;

  theTags(value) = OARRAY | LITERAL | UNLIMITED ;
  theLen(value) = 0 ;
  oArray(value) = NULL ;
  if ( ! pdf_fast_insert_hash_name(pdfc, &dict, NAME_Encoding, &value) )
    return FALSE ;

  theTags(value) = OFONTID ;
  oFid(value) = ++fid_count ;
  if ( ! pdf_fast_insert_hash_name(pdfc, &dict, NAME_FID, &value) )
    return FALSE ;

  Copy( type3font , & dict ) ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool pdf_buildtype3font( PDFCONTEXT *pdfc ,
                                OBJECT *pdffontdict ,
                                PDF_FONTDETAILS *font_details ,
                                OBJECT *type3font )
{
  static NAMETYPEMATCH type3_match[] = {
  /* 0 */ { NAME_CharProcs,              2, { ODICTIONARY, OINDIRECT }},
  /* 1 */ { NAME_FontMatrix,             3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
  /* 2 */ { NAME_Resources | OOPTIONAL , 2, { ODICTIONARY, OINDIRECT }},
           DUMMY_END_MATCH
  } ;

  int32 i ;
  OBJECT value = OBJECT_NOTVM_NOTHING ;
  PDF_TYPE3DETAILS *type3_details ;
  PDFXCONTEXT *pdfxc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;

  /* Get a copy of the Resources & CharProcs for later. */
  if ( ! pdf_dictmatch( pdfc , pdffontdict , type3_match ))
    return FALSE ;

  if ( ! pdf_maketype3font( pdfc , pdffontdict , type3font ))
    return FALSE ;

  type3_details = font_details->type3_details ;

  type3_details->charprocs = type3_match[ 0 ].result ;
  type3_details->fntmatrix = type3_match[ 1 ].result ;
  type3_details->resources = type3_match[ 2 ].result ;
  type3_details->encodings = (OBJECT *)mm_alloc( pdfxc->mm_structure_pool,
                                                 sizeof(OBJECT) * TYPE3_ENCODELEN,
                                                 MM_ALLOC_CLASS_GENERAL ) ;
  if ( type3_details->encodings == NULL )
    return error_handler( VMERROR );

  /* Map in encoding. */
  for ( i = 0 ; i < TYPE3_ENCODELEN ; ++i ) {
    NAMECACHE *charname ;
    int32 cid = i ;

    if ( ! pdf_getencoding(font_details->encode_details, &cid, &charname) )
      return FALSE ;
    if ( charname == NULL )
      charname = system_names + NAME_notdef ;

    (void)object_slot_notvm(&type3_details->encodings[i]) ;
    theTags(type3_details->encodings[i]) = ONAME | LITERAL ;
    oName(type3_details->encodings[i]) = charname ;
  }

  theTags(value) = OARRAY | LITERAL | UNLIMITED ;
  theLen(value) = TYPE3_ENCODELEN ;
  oArray(value) = type3_details->encodings ;

  if ( ! pdf_fast_insert_hash_name(pdfc, type3font, NAME_Encoding, &value) )
    return FALSE ;

  /* Null out charstrms. */
  for ( i = 0 ; i < 256 ; ++i )
    type3_details->charstrms[ i ] = NULL ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool pdf_setfonttype( NAMECACHE *fonttype, int32 strictpdf,
                             int32 *font_type, int32 *sub_type )
{
  *sub_type = FSUB_None ; /* Yet - base14 check done later */

  switch ( theINameNumber(fonttype) ) {
  case NAME_Type1:        (*font_type) = FONT_Type1 ;    break ;
  case NAME_MMType1:      (*font_type) = FONT_MMType1 ;  break ;
  case NAME_Type3:        (*font_type) = FONT_Type3 ;    break ;
  case NAME_TrueType:     (*font_type) = FONT_TrueType ; break ;
  case NAME_Type0:        (*font_type) = FONT_Type0 ;    break ;
  case NAME_CIDFontType0: (*font_type) = FONT_TypeCID ;
                          (*sub_type)  = FSUB_CID0 ;     break ;
  case NAME_CIDFontType2: (*font_type) = FONT_TypeCID ;
                          (*sub_type)  = FSUB_CID2 ;     break ;
  default:
    if ( strictpdf )
      return error_handler( UNDEFINED ) ;
    else
      (*font_type) = FONT_UnknownType ;
  }
  return TRUE ;
}


/* The combination of Acrobat distiller 4.0 and Adobe PS 4.41 driver can
   produce some totally broken results, such as font names containing
   recursive escaping of '#', i.e. #232382. The name conversion in PDF
   scanning will remove the top level of recursion, leaving #2382. We need to
   remove the subsequent levels of recursion here. */
static NAMECACHE *pdf_escape_name(NAMECACHE *fontname)
{
  uint8 str[MAXPSNAME] ;
  int32 slen = 0, len ;
  uint8 *scan ;

  HQASSERT(fontname, "No name cache for recursive hex removal") ;
  HQASSERT(fontname->len <= MAXPSNAME, "Name too long") ;

  scan = fontname->clist ;
  len = fontname->len ;
  while ( --len >= 0 ) {
    uint8 ch = *scan++ ;

    while ( ch == '#' && len >= 2 &&
            char_to_hex_nibble[scan[0]] >= 0 &&
            char_to_hex_nibble[scan[1]] >= 0 ) {
      ch = CAST_TO_UINT8((char_to_hex_nibble[scan[0]] << 4) |
                         char_to_hex_nibble[scan[1]]) ;
      scan += 2 ;
      len -= 2 ;
    }
    str[slen++] = ch ;
  }

  if ( slen != fontname->len )
    return cachename(str, slen) ;

  return fontname ;
}

/*---------------------------------------------------------------------------
   Now get (and possibly strip) the font's subset prefix & style from the
   font name. The preferred_names array will contain a list of font names,
   in the order of preference to use, represented as PostScript strings.
   The string names point into the names cache, and so should be copied
   using cachename before use. Given two font names (which may or may not
   be different), the first taken from the /FontName key of the font
   descriptor and the second taken from the /BaseFont key of the font
   dictionary, we construct a prioritised list of possible font
   names that could be matched.

   A full font name has the following "syntax":-

     [ SUBSET+ ] name-stem [,styles]

   Either or both of the subset prefix and style suffixes could be absent.
   So, from a single (full) font name we could have the following
   possibilities:-

     SUBSET+namestem,styles  -- ie. full name
     namestem,styles         -- ie. subset prefix stripped off or absent
     SUBSET+namestem         -- ie. style(s) stripped off or absent
     namestem                -- ie. no subset prefix & no style (ie. just a font name!)

   Given two full names, there's a maximum of 8 possibilities in all, but
   fewer may be generated. Note that if the BaseFont name has no subset
   prefix but the /FontName one does, a variant of BaseFont with that
   prefix is produced. The full prioritised list is

     [0]      SUBSET+FontName,styles;
     [1]      SUBSET+BaseName,styles;
     [2]      FontName,styles;
     [3]      BaseName,styles;
     [4]      SUBSET+FontName;
     [5]      SUBSET+BaseName;
     [6]      FontName;
     [7]      BaseName;
     [8]      The first font in a fontset, selected by index

   [27164] If any of these can't be produced - eg. there is no SUBSET -
   then later constructions from the same name are promoted (also note that
   indices 2-5 were reordered at this time). eg. if FontName has subset and
   styles but BaseName doesn't, the list produced is:

     [0]      SUBSET+FontName,styles;
     [1]      BaseName;
     [2]      FontName,styles;
     [3]      SUBSET+FontName;
     [4]      FontName;
*/
#define MAX_POSS_FNAMES 9


/* Helper functions for pdf_list_fontnames; pdf_list_styles puts the name
   with styles in names[0] if it exists, and name without styles in names[4].
   pdf_list_subsets calls pdf_list_styles to fill in subsetted names in
   names[0] and names[4], and non-subsetted names in names[2] and names[6].
   These are used to create a sparse array of potential fontnames, which is
   then compacted by pdf_list_fontnames. */
static void pdf_list_styles(Bool be_strict,
                            uint8 *fullname,
                            uint16 namelen,
                            OBJECT *names)
{
  int32 style ;

  HQASSERT(fullname && namelen > 0, "fullname not valid") ;
  HQASSERT(names, "No names array") ;
  HQASSERT(oType(names[0]) == ONULL && oType(names[4]) == ONULL,
           "Names array has already been filled in") ;

  /* If we successfully stripped, put the full version in the first location.
     Otherwise the fullname is the stripped name. The style flags for the
     fullname are the styles stripped off the stripped version. */
  ( void )pdf_strip_styles(be_strict, fullname, namelen, &names[4], &style) ;

  theTags(names[0]) = OSTRING | READ_ONLY | LITERAL ;
  theLen(names[0]) = namelen ;
  oString(names[0]) = fullname ;
}

static void pdf_list_subsets(Bool be_strict,
                             uint8 *fullname,
                             uint16 namelen,
                             OBJECT *names)
{
  OBJECT stripped = OBJECT_NOTVM_NOTHING ;

  HQASSERT(fullname && namelen > 0, "fullname is not valid") ;
  HQASSERT(names, "No names array") ;

  if ( pdf_strip_subset(fullname, namelen, &stripped) )
    pdf_list_styles(be_strict, oString(stripped), theLen(stripped),
                    names + 2) ;

  pdf_list_styles(be_strict, fullname, namelen, names) ;
}

static Bool pdf_list_fontnames(PDFCONTEXT *pdfc,
                               Bool be_strict,
                               NAMECACHE *fontname,
                               NAMECACHE *basename,
                               OBJECT *parray)
{
  OBJECT names[MAX_POSS_FNAMES] ;
  int32 i, j ;

  PDF_CHECK_MC( pdfc ) ;

  HQASSERT(parray, "Nowhere for font names array") ;
  HQASSERT(oType(*parray) == ONULL, "Font names array already built") ;

  /* We can't re-assing the lengths of PDF arrays after creation, so build
     the names array here first, and copy to PDF memory. */
  for ( i = 0 ; i < MAX_POSS_FNAMES ; ++i ) {
    names[i] = onull ; /* Struct copy to set slot properties */
  }

  /* Resolve given objects to namecache pointers.  If the two given
  ** names are equivalent, just take one.
  */
  if (fontname != NULL)
    fontname = pdf_escape_name(fontname);

  if (basename != NULL)
    basename = pdf_escape_name(basename);

  if (basename == fontname)
    basename = NULL;

  /* Do all font name styles */
  if (fontname != NULL) {
    pdf_list_subsets(be_strict, fontname->clist, fontname->len, &names[0]) ;
  }

  /* Basename styles. If there is no subset in the basename, but there is in
     the fontname, add the fontname's subset to the basename. */
  if (basename != NULL) {
    OBJECT stripped = OBJECT_NOTVM_NOTHING ;

    if ( fontname != NULL &&
         !pdf_strip_subset(basename->clist, basename->len, &stripped) &&
         pdf_strip_subset(fontname->clist, fontname->len, &stripped) ) {
      /* Note in TN5176 (CFF spec), sect.7, about name lengths being
         less than 127 for compatibility. This can never overflow. */
      uint8 strName[MAXPSNAME + MAXPSNAME] ;
      int32 subset_len ;

      HQASSERT(oType(stripped) == OSTRING, "Stripped name not string") ;
      subset_len = fontname->len - theLen(stripped) ;
      HQASSERT(sizeof(strName) >= subset_len + basename->len,
               "font name too long in Pdf_list_fontnames");

      HqMemCpy(strName, fontname->clist, subset_len);
      HqMemCpy(&strName[subset_len], basename->clist, basename->len);

      /* Replace existing name with subset version */
      basename = cachename(strName, subset_len + basename->len);
      if (basename == NULL)
        return error_handler( VMERROR ) ;
    }

    pdf_list_subsets(be_strict, basename->clist, basename->len, &names[1]) ;
  }

  /* Compact the sparse array. As explained above, elements 0..7 may or may not
     be filled in by the various styled elements. We now compact the non-null
     elements to the bottom of the array. */
  for ( i = j = 0 ; i < MAX_POSS_FNAMES ; ++i ) {
    if ( oType(names[i]) == ONAME ) {
      names[j++] = names[i] ;
    } else if ( oType(names[i]) == OSTRING ) {
      /* Convert string to name in case exposed to the PostScript world */
      if ( (oName(nnewobj) = cachename(oString(names[i]),
                                       theLen(names[i]))) == NULL )
        return error_handler(VMERROR) ;
      names[j++] = nnewobj ;
    }
  }

  /* If no name matches, select the first font in a font set. See comments
     above. */
  HQASSERT(j < MAX_POSS_FNAMES, "Font names array overfilled") ;
  object_store_integer(&names[j++], 0) ;

  if ( !pdf_create_array(pdfc, j, parray) )
    return FALSE ;

  for ( i = 0 ; i < j ; ++i )
    OCopy(oArray(*parray)[i], names[i]) ;

  return TRUE;
}

/* Font names are equivalent for the purposes of determining style
 * if the names are identical when insignificant characters are
 * ignored. Insignificant characters are those below '0', assuming
 * ASCII encoding. Insignificant characters include hyphen, comma
 * etc.*/
static Bool equivalent_font_names(const uint8 *name1, uint32 length1,
                                  const uint8 *name2, uint32 length2)
{
  uint8 a = '0', b = '1' ;

  HQASSERT( name1 != NULL && name2 != NULL, "NULL ptrs!");
  HQASSERT( length1 > 0 && length2 > 0, "Zero length strings");

  do {
    do {  a=*name1++; --length1 ; } while (length1 && a <'0') ;
    do {  b=*name2++; --length2 ; } while (length2 && b <'0') ;
  } while ( length1 && length2 && a == b ) ;

  return ((a == b) || (a < '0' && b < '0')) && length1 == 0 && length2 == 0 ;
}

/* ----------------------------------------------------------------------------
 * pdf_font_unpack()
 * Invoked lazily just before a character is about to be shown, this function
 * ensures the correct font (as dictated by the last Tf operator) is in the
 * right place, if not already.
 * From the 'ixc' is retained a cache of known font details - a linked list.
 * The aim of this function is to return one of the entries in this cache, all
 * filled in - i.e. PDF_FONTDETAILS.
 */
Bool pdf_font_unpack(PDFCONTEXT *pdfc, OBJECT *pdffont,
                     PDF_FONTDETAILS **pdffontdetails)
{
  int32   fontdownload ;
  int32  *fontenum ;
  int32   fontstyle, fdescstyle, foundstyle ;
  Bool    found_3_1 = FALSE ;
  OBJECT newfont = OBJECT_NOTVM_NULL ;
  OBJECT *pdffontdict ;
  OBJECT *fontdesc ;
  NAMECACHE *fontname, *fdescname ;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_FONTDETAILS *font_details = NULL;
  PDF_FONTDETAILS *dfont_details ;

  PDF_FONTDETAILS   lfontdetails ;
  PDF_TYPE3DETAILS  ltype3details ;
  PDF_WIDTHDETAILS  lwidth_details ;
  PDF_ENCODEDETAILS lencode_details ;
  PDF_CMAPDETAILS   lcmap_details ;
  PDF_CIDDETAILS    lcid_details ;
  OBJECT font_names = OBJECT_NOTVM_NULL ;


  /* The ONOTHINGS below cause incorrectly typed optionals to be ignored */
  static NAMETYPEMATCH font_match[] = {
    /* Use the enum below to index this dictmatch */
    { NAME_Type,                  2, { ONAME, OINDIRECT }},
    { NAME_Name | OOPTIONAL,      2, { ONAME, OINDIRECT }},
    { NAME_FirstChar | OOPTIONAL, 3, { ONOTHING, OINTEGER, OINDIRECT }},
    { NAME_LastChar | OOPTIONAL,  3, { ONOTHING, OINTEGER, OINDIRECT}},
    { NAME_Widths | OOPTIONAL,    4, { ONOTHING, OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_Encoding | OOPTIONAL,  4, { ONAME, ODICTIONARY, OFILE, OINDIRECT }},
    { NAME_Subtype,               2, { ONAME, OINDIRECT }},
    { NAME_BaseFont | OOPTIONAL,  2, { ONAME, OINDIRECT }},
    { NAME_DescendantFonts | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    /*{ NAME_ToUnicode       | OOPTIONAL, 2, { OFILE, OINDIRECT }},
    NB: If we ever implement ToUnicode we should add ONAME to the list and either
        skip the value in that case (as if optionally unavailable) or use it to
        gleen a CMAP file name. Such cases have appeared (task #12499) */
    DUMMY_END_MATCH
  } ;
  enum { font_match_Type, font_match_Name, font_match_FirstChar,
         font_match_LastChar, font_match_Widths, font_match_Encoding,
         font_match_Subtype, font_match_BaseFont, font_match_DescendantFonts,
         /* font_match_ToUnicode */
         } ;

  /* this dictmatch is used to check type when not optional */
  static NAMETYPEMATCH mandatory_match[] = {
    { NAME_FirstChar, 2, { OINTEGER, OINDIRECT }},
    { NAME_LastChar,  2, { OINTEGER, OINDIRECT }},
    { NAME_Widths,    3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    DUMMY_END_MATCH
  } ;


  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  /* See if we can find the Font Details from our cache. */
  font_details = pdf_findfontdetails( ixc, pdffont ) ;
  if ( font_details ) {
    *pdffontdetails = font_details ;
    return TRUE ;
  }

  /* Didn't find in cache, so create new one. */
  font_details = &lfontdetails;
  font_details->atmfont = onull ; /* Struct copy to set slot properties */
  font_details->font_type       = FONT_None ;
  font_details->font_sub        = FSUB_None ;
  font_details->width_details   = NULL ;
  font_details->type3_details   = NULL ;
  font_details->encode_details  = NULL ;
  font_details->cmap_details    = NULL ;
  font_details->cid_details     = NULL ;
  font_details->num_descendants = 0 ;
  font_details->descendants     = NULL ;
  font_details->font_flags      = 0 ;
  font_details->df_array = onull ; /* Struct copy to set slot properties */

  if (oType(*pdffont) == OINDIRECT) {
    if ( ! pdf_lookupxref( pdfc, &pdffontdict, oXRefID(*pdffont),
                           theGen(*pdffont), FALSE ) )
      return FALSE ;
  } else {
    pdffontdict = pdffont;
  }

  if (pdffontdict == NULL  ||  oType(*pdffontdict) != ODICTIONARY)
    return error_handler( TYPECHECK ) ;

  if ( ! ixc->strictpdf )
    font_match[font_match_Type].name |= OOPTIONAL ;
  else
    font_match[font_match_Type].name &= ~OOPTIONAL ;

  if ( ! pdf_dictmatch( pdfc , pdffontdict , font_match ))
    return FALSE ;

  /* Set font type field for this font */
  if ( ! pdf_setfonttype( oName(*font_match[font_match_Subtype].result) ,
                          ixc->strictpdf ,
                          & font_details->font_type ,
                          & font_details->font_sub ) )
    return FALSE ;

  fontenum = &(font_details->font_type) ;
  dfont_details = NULL ;
  fontdesc      = NULL ;
  fontdownload  = 0;    /* = 'N' of key /FontFile'N', where 0 => no /FontFile at all */
  fontname      = NULL ;
  fdescname     = NULL ;
  fdescstyle    = FSTYLE_Unknown ;
  fontstyle     = FSTYLE_Unknown ;
  foundstyle    = FSTYLE_None ;

  if ( font_match[font_match_BaseFont].result != NULL ) { /* Get the style flags of the Font object's name */
    OBJECT stripped = OBJECT_NOTVM_NOTHING ;

    fontname = oName(*font_match[font_match_BaseFont].result) ;

    (void)pdf_strip_styles(ixc->strictpdf,
                           fontname->clist, fontname->len,
                           &stripped, &fontstyle) ;
  } else if ( *fontenum != FONT_Type3 ) {
    /* BaseFont must exist for non-Type3 fonts. */
    return error_handler( UNDEFINED ) ;
  }

  /* Extract (and download) font descriptor for non-Type0/Type3 fonts
   * If we need widths later, then we create it now
   * (for downloading font descriptor)
   */
  if ( *fontenum != FONT_Type0 ) {

    font_details->width_details = &lwidth_details ;
    lwidth_details.wid_validw = FALSE ;

    if ( *fontenum != FONT_Type3 ) {
      fontdesc = fast_extract_hash_name( pdffontdict, NAME_FontDescriptor ) ;

      if ( fontdesc ) {

        dfont_details = pdf_findfontdesc( pdfc, fontdesc, *fontenum ) ;

        if ( dfont_details ) {
          lwidth_details.wid_validw = dfont_details->width_details->wid_validw ;
          lwidth_details.wid_mwidth = dfont_details->width_details->wid_mwidth ;
          fdescstyle = dfont_details->font_style ;
          font_details->font_flags = dfont_details->font_flags ;
        } else {
          /* We could extract CIDSystemInfo here for cross-checking against
             the font. */
          OBJECT fontstream = OBJECT_NOTVM_NOTHING;

          if ( ! pdf_setdescriptor( pdfc, fontdesc, fontname,
                                    &fdescname,
                                    &fontdownload, *fontenum, &fontstream,
                                    &font_details->font_flags,
                                    &lwidth_details ) )
            return FALSE ;


          /* If the font descriptor was present, we must have got a /FontName
             entry. */
          if ( fdescname != NULL ) {
            Bool is_cid = (*fontenum == FONT_TypeCID);

            int32 encoding = ((font_details->font_flags) & 32) ? NAME_StandardEncoding : -1 ;

            OBJECT *cidsysinfo = NULL;
            OBJECT stripped = OBJECT_NOTVM_NOTHING ;

            (void)pdf_strip_styles(ixc->strictpdf,
                                   fdescname->clist, fdescname->len,
                                   &stripped, &fdescstyle) ;

            if ( !pdf_list_fontnames(pdfc, ixc->strictpdf,
                                     fdescname, fontname,
                                     &font_names) )
              return FALSE ;

            /* Read the appropriate font file */
            switch (fontdownload) {
              OBJECT ros ;
            case 1:               /* '/FontFile' specified */
              if (!pdf_download_t1( pdfc, &fontstream, is_cid, encoding,
                                    cidsysinfo, &font_names, &newfont, fontname))
                return FALSE;
              break;
            case 2:               /* '/FontFile2' specified */
              if ( is_cid ) {
                /* CIDToGIDMap is mandatory, so force the underlying font to
                   use an identity mapping. */
                OBJECT registry = OBJECT_NOTVM_NOTHING, ordering = OBJECT_NOTVM_NOTHING ;

                if ( !pdf_create_dictionary(pdfc, 3, object_slot_notvm(&ros)) ||
                     !pdf_create_string(pdfc, sizeof("Adobe") - 1, &registry) ||
                     !pdf_create_string(pdfc, sizeof("Identity") - 1, &ordering) )
                  return FALSE ;

                HqMemCpy(oString(registry), "Adobe", sizeof("Adobe") - 1) ;
                HqMemCpy(oString(ordering), "Identity", sizeof("Identity") - 1) ;
                oInteger(inewobj) = 0 ;

                if ( !pdf_fast_insert_hash_name(pdfc, &ros, NAME_Registry, &registry) ||
                     !pdf_fast_insert_hash_name(pdfc, &ros, NAME_Ordering, &ordering) ||
                     !pdf_fast_insert_hash_name(pdfc, &ros, NAME_Supplement, &inewobj) )
                  return FALSE ;

                cidsysinfo = &ros ;
              }
              if (!pdf_download_tt( pdfc, &fontstream, is_cid, encoding,
                                    cidsysinfo, &font_names, &newfont, FALSE, &found_3_1 ))
                return FALSE;
              break;
            case 3:               /* '/FontFile3' specified */
              if (!pdf_download_tX( pdfc, &fontstream, is_cid, encoding,
                                    cidsysinfo, &font_names, &newfont, font_details, &found_3_1 ))
                return FALSE;

              break;
            }
          }
        }
      }
      else {    /*  fontdesc == NULL */
        if (! pdfxExternalFontDetected( pdfc, FALSE ))
          return FALSE;
      }
    }
  }

  /* If we didn't download a font, we have to search for it. We'll try the
     full order of potential font names, because plenty of apps get the
     fontname and/or basefont wrong. Don't try this for Type 0 fonts, they
     will select a descendant font anyway. Don't do it for Type 3 fonts, they
     don't require FontName or BaseFont. */
  if ( *fontenum != FONT_Type0 && *fontenum != FONT_Type3 ) {
    OBJECT stripped = OBJECT_NOTVM_NOTHING ;
    int32 fonttype = (*fontenum == FONT_TypeCID) ? NAME_CIDFont : NAME_Font ;

    if ( oType(newfont) == ONULL ) {
      int32 i ;

      /* We may have had a FontDescriptor, but no embedded font, in which case
         we'll have already created the name mapping. */
      if ( oType(font_names) != OARRAY ) {
        if ( !pdf_list_fontnames(pdfc, ixc->strictpdf,
                                 fdescname, fontname,
                                 &font_names) )
          return FALSE ;
      }

      for ( i = 0 ; i < theLen(font_names) ; ++i ) {
        OBJECT *nameobj = &oArray(font_names)[i] ;

        if ( oType(*nameobj) == ONAME || oType(*nameobj) == OSTRING ) {
          Bool exists;

          if ( !pdf_resource_exists(nameobj, fonttype, &exists) )
            return FALSE;

          if ( exists ) {
            Copy(&font_names, nameobj) ;
            break ;
          }
        }
      }
    }

    if ( oType(font_names) == ONAME ) {
      fontname = oName(font_names) ;
    } else if ( oType(font_names) == OSTRING ) {
      if ( (fontname = cachename(oString(font_names), theLen(font_names))) == NULL )
        return error_handler(VMERROR) ;
    } else if ( fdescname ) { /* Prefer name from descriptor for findfont */
      fontname = fdescname ;
    } else if ( fontname == NULL ) /* No font name */
      return error_handler(UNDEFINED) ;

    (void)pdf_strip_styles(ixc->strictpdf,
                           fontname->clist, fontname->len,
                           &stripped, &foundstyle) ;
  }

  /* We always need to take any font style from the font dictionary first -
   * since styled fonts may share a common descriptor. Faux the differences
   * between the font found and the font style requested. If fonts are
   * embedded the driver which embedded the font should have done the
   * appropriate magic.
   */
  if ( fontstyle != FSTYLE_Unknown ) {

    if ((font_details->font_style = (fontstyle & ~foundstyle)) != FSTYLE_None) {
      /* If we deduce a faux style is required, then check the font names in
       * more detail to see if the style is really required.
       * fontstyle != unknown => we have a Basefont name. If Basefont and
       * current font name are equivalent in terms of the styles implied
       * by the names, then we don't need to apply faux styles.
       */
      uint8 *basename = oName(*font_match[font_match_BaseFont].result)->clist;
      uint16 basename_length = oName(*font_match[font_match_BaseFont].result)->len;
      uint8 *fontname_ptr = fontname->clist;
      uint16 fontname_length = fontname->len;
      OBJECT non_subset = OBJECT_NOTVM_NULL;

      if (pdf_strip_subset(basename, basename_length, &non_subset)) {
        basename = oString(non_subset);
        basename_length = theLen(non_subset);
      }

      if (pdf_strip_subset(fontname_ptr, fontname_length, &non_subset)) {
        fontname_ptr = oString(non_subset);
        fontname_length = theLen(non_subset);
      }

      if (equivalent_font_names( fontname_ptr, fontname_length,
                                 basename, basename_length ))
         font_details->font_style = FSTYLE_None;
    }

    HQTRACE( debug_findfonts,
             ( "Use font dictionary style: %d", font_details->font_style )) ;
  }
  else if ( fdescstyle != FSTYLE_Unknown ) {
    if ( fontdownload != 0 ) /* Only faux font if not embedded */
      font_details->font_style = FSTYLE_None ;
    else
      font_details->font_style = (fdescstyle & ~foundstyle) ;
    HQTRACE( debug_findfonts,
             ( "Use font descriptor style: %d", font_details->font_style )) ;
  }
  else {
    font_details->font_style = FSTYLE_None ;
    HQTRACE( debug_findfonts,
             ( "Unable to obtain font style; using default: %d",
               font_details->font_style )) ;
  }

  if ( *fontenum == FONT_MMType1 && fontdownload == 0 ) {
    /* For a MM font we need to do a findfont of the base font to load it
     * into the RIP.
     */
    if ( !pdf_setfont_mmbase(pdfc, fontname) )
      return FALSE ;
  }

  /* Is it one of the base14 fonts (only applies to Type1/Truetype fonts) */
  if ( *fontenum == FONT_Type1 || *fontenum == FONT_TrueType ) {
    if ( pdf_base14Font( fontname ))
      font_details->font_sub = FSUB_Base14 ;
  }

  /* First/Last Char and Widths must be present for a non-base14,
   * non-composite font. Use second dictmatch to do the work.
   */
  if ( font_details->font_sub != FSUB_Base14 &&
       *fontenum != FONT_Type0 && *fontenum != FONT_TypeCID ) {
    if ( ixc->strictpdf &&
         ! pdf_dictmatch( pdfc , pdffontdict , mandatory_match ) )
      return error_handler( UNDEFINED ) ;
  }

  /* if one of the base14, then enforce the PRM's definition that either
   * ALL or NONE of First/LastChar and Widths must be present, and no
   * other combination
   */
  if ( font_details->font_sub == FSUB_Base14 && (
       font_match[font_match_FirstChar].result == NULL ||
       font_match[font_match_LastChar].result == NULL ||
       font_match[font_match_Widths].result == NULL ) ) {
    font_match[font_match_FirstChar].result = NULL ;
    font_match[font_match_LastChar].result = NULL ;
    font_match[font_match_Widths].result = NULL ;
  }

  /* Cache the values we require for getting character widths. */
  if ( font_details->width_details &&
       ! pdf_setwidthdetails( font_details->width_details ,
                              font_match[font_match_FirstChar].result ,
                              font_match[font_match_LastChar].result ,
                              font_match[font_match_Widths].result ))
    return FALSE ;

  /* Set Encoding or CMap details (depending on font type) */
  if ( *fontenum == FONT_Type0 ) {
    PDF_FONTDETAILS **dfptr ;
    int32 dfnum ;

    /* Type0 Font must list its descendant fonts -> save that list */
    if (font_match[font_match_DescendantFonts].result == NULL)
      return error_handler( UNDEFINED ) ;

    Copy( &(font_details->df_array),
          font_match[font_match_DescendantFonts].result ) ;

    /* Prepare array for storing font details for descendants as and
     * when they get used/cached.
     */
    dfnum = theLen(font_details->df_array) ;
    font_details->num_descendants = dfnum ;
    dfptr = mm_alloc( pdfxc->mm_structure_pool,
                      sizeof(PDF_FONTDETAILS *) * dfnum, MM_ALLOC_CLASS_GENERAL ) ;
    if (dfptr == NULL)
      return error_handler( VMERROR );

    HqMemZero(dfptr, sizeof(PDF_FONTDETAILS *) * dfnum ) ;
    font_details->descendants = dfptr ;

    lcmap_details.cmap_dict = onull ; /* set slot properties */

    font_details->cmap_details = &lcmap_details ;
    if ( ! pdf_setcmapdetails( pdfc, &lcmap_details,
                               font_match[font_match_Encoding].result ))
      return FALSE ;
  }
  else if ( *fontenum == FONT_TypeCID ) {
    /* Obtain CID font info here */
    lcid_details.cidsysinfo = onull ; /* struct copy to set slot properties */
    lcid_details.w = onull ; /* struct copy to set slot properties */
    lcid_details.w2 = onull ; /* struct copy to set slot properties */

    font_details->cid_details = &lcid_details ;
    if ( ! pdf_setciddetails( pdfc, &lcid_details, pdffontdict,
                              font_details->font_sub ))
      return FALSE ;
  }
  else {
    /* If the font is a TT and flags indicate Symbolic yet there is an Encoding,
       we have to decide which way to jump. This depends on the presence of a
       (3,1) cmap. */
    if (*fontenum == FONT_TrueType && (font_details->font_flags & 4) == 4 &&
        font_match[font_match_Encoding].result) {
      if (found_3_1) {
        /* There was a (3,1) cmap, so change it to nonsymbolic [64587] */
        font_details->font_flags ^= 4 + 32 ;
      } else {
        /* This *is* a symbolic TrueType, so remove the encoding [64149] */
        font_match[font_match_Encoding].result = NULL ;
      }
    }

    font_details->encode_details = &lencode_details ;
    if ( ! pdf_setencodedetails( pdfc, &lencode_details,
                                 font_match[font_match_Encoding].result ))
      return FALSE ;
  }

  if (*fontenum == FONT_Type3) {
    /* Font must be a Type3 font; need to create a font instance but
     * not register it in FontDirectory.
     */
    OBJECT type3font = OBJECT_NOTVM_NOTHING ;
    font_details->type3_details = &ltype3details ;
    if ( ! pdf_buildtype3font( pdfc, pdffontdict, font_details, & type3font ))
      return FALSE ;
    Copy( &font_details->atmfont, &type3font ) ;
  }
  else if (*fontenum != FONT_Type0) {
    /* NOTE:
     * The findfont/setfont stuff may need to be patched up at some point to
     * handle Type0 fonts. The reason for this is that the Cmap for a Type0
     * font allows a FontMatrix to be specified for its descendants -> thus
     * the same base font may be used by multiple CMaps with different
     * Matrices and should therefore not be treated as the same font by the
     * font machinery.
     * For now, this complication is ignored!
     */
    if ( dfont_details ) {
      HQASSERT( font_details->font_type != FONT_Type3 , "Cant match Type 3 FDs" ) ;
      Copy( &font_details->atmfont, &dfont_details->atmfont ) ;
    } else if ( oType(newfont) == ODICTIONARY ) { /* Use the downloaded font */
      Copy( &font_details->atmfont, &newfont ) ;
    } else {
      Bool result ;
      OBJECT psfontD = OBJECT_NOTVM_NOTHING, *fontdescD,
        nameO = OBJECT_NOTVM_NAME(NAME_FontDirectory, LITERAL) ;

      /* make font dict etc visible to PS so that FontEmulation can get at the
         various font metrics within the PDF font dictionary */
      if ( (fontdescD = fast_extract_hash_name(&internaldict, NAME_FontDescriptor)) == NULL ||
           !pdf_resolvexrefs( pdfc, pdffontdict ) ||
           !pdf_copyobject( NULL, pdffontdict, &psfontD) ||
           !insert_hash(fontdescD, &nameO, &psfontD) )
        return FALSE ;

      /* now find the font... (as before) */
      oName( nnewobj ) = fontname ;
      result = pdf_findresource( *fontenum == FONT_TypeCID ? NAME_CIDFont : NAME_Font,
                                 &nnewobj ) ;
      /* destroy the visible PDF font dict...*/
      if ( !remove_hash( fontdescD, &nameO, TRUE )) return FALSE ;
      /* and give up if we failed */
      if ( !result ) return FALSE ;

      Copy( & font_details->atmfont, theTop( operandstack )) ;
      pop( &operandstack ) ;

      /* However, if the font was not embedded (ignoring the whole FontSubstitution issue)
       * the font we now have may be of a different type to font_details->font_type */
      if (*fontenum != FONT_TypeCID) {
        OBJECT *actual ;
        oName(nnewobj) = system_names + NAME_FontType ;
        actual = fast_extract_hash(&font_details->atmfont, &nnewobj) ;
        if (actual && oType(*actual) == OINTEGER)
          font_details->font_type = oInteger(*actual) ;
      }
    }
  }

  /* Font details are cached (locally).  */
  font_details = pdf_storefontdetails( pdfc , font_details , pdffont ,
                                       fontname , fontdesc ) ;
  if ( font_details == NULL )
    return FALSE ;


  /* Free any memory allocated locally */
  if (lfontdetails.descendants) {
    mm_free( pdfxc->mm_structure_pool,
             ( mm_addr_t )lfontdetails.descendants,
             sizeof(PDF_FONTDETAILS *) * lfontdetails.num_descendants ) ;
  }

  *pdffontdetails = font_details ;
  return TRUE ;
}

void init_C_globals_pdffont(void)
{
  FILELIST reset = {tag_LIMIT} ;
  pfbFontFilter  = reset ;

#if defined( ASSERT_BUILD )
  debug_findfonts = FALSE ;
#endif
#if defined( DEBUG_BUILD )
  tfont_number = 0 ;
#endif
}

/* Log stripped */
