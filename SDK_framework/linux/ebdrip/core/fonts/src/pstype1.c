/** \file
 * \ingroup fonttype1
 *
 * $HopeName: COREfonts!src:pstype1.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Font methods for PostScript Type 1 fonts. These routines find the
 * information required for Type 1 charstring interpretation from a VM-based
 * PostScript Type 1 font.
 */

#include "core.h"
#include "objects.h"  /* NAMECACHE, OBJECT */
#include "swerrors.h" /* error_handler, INVALIDFONT */
#include "context.h"  /* charcontext_t, CoreContext */
#include "namedef_.h"

#include "graphics.h" /* FONTinfo */
#include "encoding.h" /* StandardEncoding */

#include "fonts.h"
#include "fontcache.h"
#include "t1hint.h"     /* t1hinting_now. Yuck. */
#include "fcache.h"     /* get_sdef */
#include "charstring12.h" /* This file ONLY implements Type 1 charstrings */


/* Exported definition of the font methods for VM-based Type 1 fonts */
static Bool ps1_lookup_char(FONTinfo *fontInfo,
                            charcontext_t *context) ;
static Bool ps1_begin_char(FONTinfo *fontInfo,
                           charcontext_t *context) ;
static void ps1_end_char(FONTinfo *fontInfo,
                         charcontext_t *context) ;

font_methods_t font_type1_ps_fns = {
  fontcache_base_key,
  ps1_lookup_char,
  NULL, /* No subfont lookup */
  ps1_begin_char,
  ps1_end_char
} ;

/* Internal definition of Type 1 charstring methods */
static Bool ps1_begin_subr(void *data, int32 subno, int32 global,
                           uint8 **subrstr, uint32 *subrlen) ;
static Bool ps1_begin_seac(void *data, int32 stdindex,
                           uint8 **subrstr, uint32 *subrlen) ;
static void ps1_end_substring(void *data, uint8 **subrstr, uint32 *subrlen) ;

charstring_methods_t pstype1_charstring_fns = {
  NULL,          /* private data (FONTinfo) */
  ps1_get_info,
  ps1_begin_subr,
  ps1_end_substring,
  ps1_begin_seac,
  ps1_end_substring
} ;

/*---------------------------------------------------------------------------*/
/* PS Type 1 charstring subrs */
Bool ps1_get_info(void *data, int32 nameid, int32 index, OBJECT *info)
{
  FONTinfo *fontInfo = data ;
  OBJECT *theo ;
  OBJECT *fntdict ;

  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(info, "No data object") ;

  object_store_null(info) ;

  fntdict = & theMyFont(*fontInfo);
  HQASSERT( oType(*fntdict) == ODICTIONARY , "fntdict not a ODICTIONARY" ) ;

  switch ( nameid ) {
    /* Arrays */
  case NAME_OtherSubrs:
  case NAME_BlueValues: case NAME_OtherBlues:
  case NAME_FamilyBlues: case NAME_FamilyOtherBlues:
  case NAME_StdHW: case NAME_StdVW:
  case NAME_StemSnapH: case NAME_StemSnapV:
    fntdict = fast_extract_hash_name(&fontInfo->subfont, NAME_Private );
    if ( ! fntdict )
      return TRUE ;
    if (oType(*fntdict) != ODICTIONARY )
      return error_handler( TYPECHECK ) ;
    /* FALL THROUGH */
  case NAME_WeightVector:
  case NAME_NormalizedDesignVector:
  case NAME_UserDesignVector:
  case NAME_XUID:
    theo = fast_extract_hash_name( fntdict, nameid );
    if ( ! theo )
      return TRUE ;
    if (oType(*theo) != OARRAY  &&  oType(*theo) != OPACKEDARRAY)
      return error_handler( TYPECHECK ) ;
    if ( index >= theLen(*theo) )
      return error_handler(RANGECHECK) ;
    if ( index < 0 ) /* Return array length */
      object_store_integer(info, theLen(*theo)) ;
    else
      OCopy(*info, oArray(*theo)[index]) ;
    return TRUE ;

    /* Integers */
  case NAME_lenBuildCharArray:
  case NAME_LanguageGroup:
  case NAME_lenIV:
  case NAME_initialRandomSeed: /* Type 2 only, but might as well look */
    fntdict = fast_extract_hash_name(&fontInfo->subfont, NAME_Private) ;
    if ( ! fntdict )
      return TRUE ;
    if (oType(*fntdict) != ODICTIONARY )
      return error_handler( TYPECHECK ) ;
    /* FALLTHROUGH */
  case NAME_UniqueID:
    theo = fast_extract_hash_name(fntdict, nameid ) ;
    if ( ! theo )
      return TRUE ;
    if (oType(*theo) != OINTEGER)
      return error_handler( TYPECHECK ) ;
    Copy(info, theo) ;
    return TRUE ;

    /* Numbers */
  case NAME_defaultWidthX:
  case NAME_nominalWidthX:
  case NAME_BlueFuzz:
  case NAME_BlueScale:
  case NAME_BlueShift:
  case NAME_ForceBoldThreshold:
  case NAME_ExpansionFactor:
    fntdict = fast_extract_hash_name(&fontInfo->subfont, NAME_Private) ;
    if ( ! fntdict )
      return TRUE ;
    if (oType(*fntdict) != ODICTIONARY)
      return error_handler( TYPECHECK ) ;
    theo = fast_extract_hash_name(fntdict , nameid ) ;
    if ( ! theo )
      return TRUE ;
    if (oType(*theo) != OINTEGER  &&  oType(*theo) != OREAL)
      return error_handler( TYPECHECK ) ;
    Copy(info, theo) ;
    return TRUE ;

    /* Booleans */
  case NAME_ForceBold:
  case NAME_RndStemUp:
    fntdict = fast_extract_hash_name(&fontInfo->subfont, NAME_Private) ;
    if ( ! fntdict )
      return TRUE ;
    if (oType(*fntdict) != ODICTIONARY )
      return error_handler( TYPECHECK ) ;
    theo = fast_extract_hash_name(fntdict, nameid ) ;
    if ( ! theo )
      return TRUE ;
    if (oType(*theo) != OBOOLEAN)
      return error_handler( TYPECHECK ) ;
    Copy(info, theo) ;
    return TRUE ;

    /* Special */
  case NAME_FID:
    object_store_integer(info, theCurrFid(*fontInfo)) ;
    return TRUE ;
  case NAME_SubFont:
    object_store_integer(info, theFDIndex(*fontInfo)) ;
    return TRUE ;

  default:
    HQFAIL( "Unknown key to look up Type 1 font" ) ;
    return FAILURE(FALSE) ;
  }
  /* Not reached. */
}

/* PS Type 1 charstring subrs */
static Bool ps1_begin_subr(void *data, int32 subno, int32 global,
                           uint8 **subrstr, uint32 *subrlen)
{
  FONTinfo *fontInfo = data ;
  OBJECT *private, *theo ;

  UNUSED_PARAM(int32, global) ;

  HQASSERT(fontInfo != NULL, "No font info") ;
  HQASSERT(subrstr != NULL, "Nowhere for subr string") ;
  HQASSERT(subrlen != NULL, "Nowhere for subr length") ;
  HQASSERT(!global, "Type 1 subr should not be global") ;

  /* Lookup the dictionary "Private" from the current font. */
  private = fast_extract_hash_name(&fontInfo->subfont, NAME_Private);
  if (private == NULL)
    return FAILURE(FALSE) ;

  /* Lookup the array "Subrs" from the Private dictionary. */
  theo = fast_extract_hash_name(private, NAME_Subrs) ;
  if ( theo == NULL )
    return FAILURE(FALSE) ;

  /* Type check the "Subrs". */
  if ( oType(*theo) != OARRAY && oType(*theo) != OPACKEDARRAY )
    return FAILURE(FALSE) ;

  /* Range check the subroutine number. */
  if ( subno < 0 || subno >= theLen(*theo) )
    return FAILURE(FALSE) ;

  theo = &oArray(*theo)[subno] ;

  /* Type check the subroutine. */
  if (oType(*theo) != OSTRING )
    return FAILURE(FALSE) ;

  *subrstr = oString(*theo) ;
  *subrlen = theLen(*theo) ;

  return TRUE ;
}

/* PS Type 1 SEAC recursive charstrings */
static Bool ps1_begin_seac(void *data, int32 stdindex,
                           uint8 **subrstr, uint32 *subrlen)
{
  FONTinfo *fontInfo = data ;
  OBJECT *theo ;

  HQASSERT(fontInfo != NULL, "No font info") ;
  HQASSERT(subrstr != NULL, "Nowhere for subr string") ;
  HQASSERT(subrlen != NULL, "Nowhere for subr length") ;

  if ( stdindex < 0 || stdindex >= NUM_ARRAY_ITEMS(StandardEncoding) )
    return error_handler(RANGECHECK) ;

  oName(nnewobj) = StandardEncoding[stdindex] ;
  if ( NULL == (theo = extract_hash(theCharStrings(*fontInfo), &nnewobj)) )
    return FAILURE(FALSE) ;

  /* Range check the "recursive character." */
  if (oType(*theo) != OSTRING )
    return FAILURE(FALSE) ;

  *subrstr = oString(*theo) ;
  *subrlen = theLen(*theo) ;

  return TRUE ;
}

static void ps1_end_substring(void *data, uint8 **subrstr, uint32 *subrlen)
{
  UNUSED_PARAM(void *, data) ;

  *subrstr = NULL ;
  *subrlen = 0 ;
}

/*---------------------------------------------------------------------------*/
/* Font lookup and top-level charstring routines for VM-based Type 1 fonts */
static Bool ps1_lookup_char(FONTinfo *fontInfo,
                            charcontext_t *context)
{
  UNUSED_PARAM(FONTinfo *, fontInfo) ;

  HQASSERT(fontInfo, "No font info") ;
  HQASSERT(context, "No context") ;

  HQASSERT(theIFontType(fontInfo) == FONTTYPE_1, "Not in a Type 1 font") ;

  if ( !get_sdef(fontInfo, &context->glyphname, &context->definition) )
    return FALSE ;

  /* PLRM3 p.351: Array (procedure) for charstring is a glyph replacement
     procedure. */
  switch ( oType(context->definition) ) {
  case OARRAY: case OPACKEDARRAY:
    /* Replacement glyph detected. Use Type 3 charstring methods. */
    context->chartype = CHAR_BuildChar ;
    break ;
  case OSTRING:
    context->chartype = CHAR_Type1 ;
    break ;
  default:
    return error_handler(INVALIDFONT) ;
  }

  return TRUE ;
}

/* Determine if the named char exists in the font, and whether it has been
   replaced by a procedure. */
static Bool ps1_begin_char(FONTinfo *fontInfo,
                           charcontext_t *context)
{
  HQASSERT(context, "No char context") ;

  HQASSERT(theIFontType(fontInfo) == FONTTYPE_1, "Not in a Type 1 font") ;

  /* PLRM3 p.351: Array (procedure) for charstring is a glyph replacement
     procedure. */
  if ( context->chartype == CHAR_BuildChar )
    return (*font_type3_fns.begin_char)(fontInfo, context) ;

  HQASSERT(context->chartype == CHAR_Type1,
           "Char type not set for Type 1 font") ;

  pstype1_charstring_fns.data = fontInfo ;
  context->methods = &pstype1_charstring_fns ;

  return TRUE ;
}

static void ps1_end_char(FONTinfo *fontInfo,
                         charcontext_t *context)
{
  HQASSERT(context, "No context object") ;

  if ( context->chartype == CHAR_BuildChar ) {
    (*font_type3_fns.end_char)(fontInfo, context) ;
    return ;
  }

  HQASSERT(context->methods == &pstype1_charstring_fns,
           "PS Type 1 charstring methods changed while building character") ;
  HQASSERT(pstype1_charstring_fns.data == fontInfo,
           "PS Type 1 charstring data changed while building character") ;
  pstype1_charstring_fns.data = NULL ;

  /* Clear out string data, it's no longer allocated. */
  object_store_null(&context->definition) ;
}

/*
Log stripped */
