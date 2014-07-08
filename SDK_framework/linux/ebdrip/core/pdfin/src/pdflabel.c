/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdflabel.c(EBDSDK_P.1) $
 * $Id: src:pdflabel.c,v 1.10.10.1.1.1 2013/12/19 11:25:13 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Page Labels.
 * Contains routines for generating Page Labels.
 * See PDF 1.4 Manual, section 8.3.1 - Page Labels.
 */

#define OBJECT_SLOTS_ONLY

#include "core.h"
#include "swerrors.h"
#include "monitor.h"
#include "objects.h"
#include "dictscan.h"
#include "namedef_.h"

#include "swpdf.h"
#include "swpdfin.h"
#include "pdfin.h"
#include "pdfmem.h"
#include "pdfmatch.h"
#include "pdfxref.h"
#include "hqmemcpy.h"
#include "swcopyf.h"

#define MAX_LETTER_STRING_LEN 512


/* ----------------------------------------------------------------------------
 * pdf_append_letter()
 * Append a letter to the buffer, checking for buffer overflow first.
 */
static Bool pdf_append_letter( uint8 *pBuff, int32 *pBuff_inx, uint8 ltr )
{
  /* The check against MAX_LETTER_STRING_LEN-minus-one is to allow
     room for a terminating '\0' character. */
  if (*pBuff_inx >= (MAX_LETTER_STRING_LEN - 1)) {
    HQFAIL( "Buff overflow in pdf_append_letter" );
    return error_handler( LIMITCHECK);
  }

  /* Append */
  pBuff[ *pBuff_inx ] = ltr;
  (*pBuff_inx)++;

  return TRUE;
}


/* ----------------------------------------------------------------------------
 * pdf_roman_digits()
 * Given a decimal digit and the current index into the (pair of) roman digits,
 * appends the appropriate roman numerals required to represent that digit.
 */
static Bool pdf_roman_digits( int32 dec_digit,  /* decimal digit */
                              int32 roman_inx,  /* index into roman digit pairs */
                              uint8 *pRoman_ltrs, /* array of roman digits letters*/
                              uint8 *pBuff,
                              int32 *pBuff_inx )
{
  uint8 one = pRoman_ltrs[roman_inx * 2];  /* ie. I, X, C or M */
  int32 i;

  HQASSERT( dec_digit > 0 && dec_digit <= 9, "dec_digit wrong in pdf_roman_digits" );

  switch (dec_digit) {
  case 6:     /* produce VI   or LX (60)   */
  case 7:     /* produce VII  or LXX (70)  */
  case 8:     /* produce VIII or LXXX (80) */
    /* Output the 'V' then fall through to produce the 'I's */
    if (!pdf_append_letter( pBuff,
                            pBuff_inx,
                            pRoman_ltrs[ roman_inx * 2 + 1 ] )) /* ie. V, L or D */
      return FALSE;
    dec_digit -= 5;
    /* fall thru */

  case 1:     /* produce I   */
  case 2:     /* produce II  */
  case 3:     /* produce III */
    for (i = 0; i < dec_digit; i++) {
      if (!pdf_append_letter( pBuff, pBuff_inx, one ))
        return FALSE;
    }
    break;

  case 4:     /* produce IV */
    if (!pdf_append_letter( pBuff, pBuff_inx, one ))
      return FALSE;
    /* fall thru */

  case 5:     /* produce V */
    if (!pdf_append_letter( pBuff,
                            pBuff_inx,
                            pRoman_ltrs[ roman_inx * 2 + 1 ] )) /* ie. V, L or D */
      return FALSE;
    break;

  case 9:     /* produce IX  */
    if (!pdf_append_letter( pBuff, pBuff_inx, one ))
      return FALSE;
    if (!pdf_append_letter( pBuff,
                            pBuff_inx,
                            pRoman_ltrs[ roman_inx * 2 + 2 ] )) /* i.e. X, C or M */
      return FALSE;
    break;

  default:
    break;
  }

  return TRUE;
}

/* ----------------------------------------------------------------------------
 * pdf_make_roman()
 * Given a positive integer, returns a string representing the equivalent in
 * roman numerals.  Lowercase or uppercase numerals can be requested.
 */
static Bool pdf_make_roman( PDFCONTEXT *pdfc,
                            int32  number,
                            Bool   use_lowercase,  /* TRUE/FALSE */
                            OBJECT *pRetStr )
{
  /* The roman letters are listed below in pairs - I & V, X & L, C & D - where
     the first of each pair is the "unit" and the second is the "five".
     The last latter - M - is the exception - it's not paired.  */
  static uint8 k_roman_lowers[] = {'i', 'v', 'x', 'l', 'c', 'd', 'm' };
  static uint8 k_roman_uppers[] = {'I', 'V', 'X', 'L', 'C', 'D', 'M' };
  static int32 k_roman_values[] = { 1,   5,  10,  50, 100,  500, 1000 };
  static int32 largest_roman_inx = sizeof(k_roman_lowers) / 2;
  uint8 *pRoman_ltrs;

  uint8 buff[ MAX_LETTER_STRING_LEN ];
  int32 buff_inx = 0;
  int roman_inx = largest_roman_inx;  /* Start with biggest roman digit value (i.e. M) */

  HQASSERT( number > 0, "Number <= 0 in pdf_make_letters" );
  if (number <= 0)
    return error_handler(RANGECHECK);

  if (use_lowercase)
    pRoman_ltrs = k_roman_lowers;
  else
    pRoman_ltrs = k_roman_uppers;


  while (roman_inx >= 0) {

    int32 digit_value = k_roman_values[ roman_inx * 2 ];   /* i.e. 1000, 100, 10, 1 */
    int32 remainder = number % digit_value;
    int32 dec_digit = (number - remainder) / digit_value;

    if (roman_inx == largest_roman_inx) {
      /* As M is the biggest roman digit, this needs repeating several
         times for large decimal numbers.  E.g. 5163 => MMMMMCLXIII */
      int32 i;
      uint8 em = pRoman_ltrs[roman_inx * 2];  /* ie. M */
      for (i = 0; i < dec_digit; i++) {
        if (!pdf_append_letter( buff, &buff_inx, em ))
          return FALSE;
      }
    }
    else if (dec_digit > 0)
      if (!pdf_roman_digits( dec_digit, roman_inx, pRoman_ltrs, buff, &buff_inx ))
        return FALSE;

    number = remainder;
    roman_inx--;
  }

  /* Terminate the string */
  buff[buff_inx++] = '\0';

  /* Allocate a string to return the produced roman numeral. */
  if (!pdf_create_string( pdfc, buff_inx+1, pRetStr ))
    return FALSE;

  HqMemCpy( oString(*pRetStr), buff, buff_inx );

  return TRUE;
}

#if 0 /* -- test code only */
Bool test_roman_generator( PDFCONTEXT *pdfc )
{
  long numbers[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                  1998, 2001, 1499, 996, 1, 10, 100, 3000, 4000, 14000,
  11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
  95, 96, 97, 98, 99, 100, 101, 102 , 103, 104, 105, 106 };
  OBJECT StrObj = OBJECT_NOTVM_NOTHING;
  int32 i;

  for (i = 0; i < NUM_ARRAY_ITEMS(numbers); i++) {
    pdf_make_roman( pdfc, numbers[i], TRUE, &StrObj );
    monitorf( (uint8*)"Number %d as << %s >>\n", numbers[i], oString(StrObj) );
    pdf_destroy_string( pdfc, theLen(StrObj), &StrObj );
  }

  return TRUE;
}
#endif


/* ----------------------------------------------------------------------------
 * pdf_make_letters()
 * Given a positive integer, returns a string representing the equivalent in
 * a format using only letters.  A to Z for the first 26 pages, AA to ZZ for
 * the next 26, etc.
 */
static Bool pdf_make_letters( PDFCONTEXT *pdfc,
                              int32  number,
                              Bool   use_lowercase,  /* TRUE/FALSE */
                              OBJECT *pRetStr )
{
  int32 buff_pos = 0;
  int32 letter_num;
  int32 times;
  uint8 ltr;

  HQASSERT( number > 0, "Number <= 0 in pdf_make_letters" );
  if (number <= 0)
    return error_handler(RANGECHECK);

  /* Which letter to use. */
  letter_num = (number - 1) % 26;
  if (use_lowercase)
    ltr = (uint8) ((uint8)'a' + (uint8) letter_num);
  else
    ltr = (uint8) ((uint8)'A' + (uint8) letter_num);

  /* Number of times to repeat that letter. Limit it to something reasonable. */
  times = ((number - 1) / 26) + 1;

  HQASSERT( times <= MAX_LETTER_STRING_LEN,
            "Rather too many letters to make in pdf_make_lettes" );
  if (times > MAX_LETTER_STRING_LEN)
    times = MAX_LETTER_STRING_LEN;

  /* Create a string big enough. */
  if (!pdf_create_string( pdfc, times+1, pRetStr ))
    return FALSE;

  /* Produce the string. */
  while (times-- > 0) {
    oString(*pRetStr)[buff_pos++] = ltr;
  }

  /* Terminate it for easy printing. */
  oString(*pRetStr)[buff_pos] = '\0';

  return TRUE;
}


/* ----------------------------------------------------------------------------
 * pdf_make_arabic()
 * Given a positive integer, returns a string representing the equivalent in
 * decimal arabic numbers.
 */
static Bool pdf_make_arabic( PDFCONTEXT *pdfc,
                             int32  number,
                             OBJECT *pRetStr )
{
  int32 num;
  int32 count;

  HQASSERT( number > 0, "Number <= 0 in pdf_make_arabic" );
  if (number <= 0)
    return error_handler(RANGECHECK);

  /* Count how many digits we're likely to produce. */
  num = number;
  count = 1;
  while (num / 10 >= 1) {
    num /= 10;
    count++;
  }
  count += 2;   /* plus a bit extra (for safety & a null terminator). */

  /* Create a string big enough. */
  if (!pdf_create_string( pdfc, count, pRetStr ))
    return FALSE;

  /* Produce the string. NB: 'swcopyf' will put a terminating '\0' character
     on the end. */
  swcopyf( oString(*pRetStr), (uint8*) "%d", number );

  return TRUE;
}



/* ----------------------------------------------------------------------------
 * pdf_page_label_text()
 * Given a page label dictionary (and current page number), constructs a
 * complete page label string.  The dictionary names a "style" (/S) which can
 * be decimal, roman or letters; a prefix string (/P) and also a start number
 * (/St).
 */
static Bool pdf_page_label_text( PDFCONTEXT *pdfc,
                                 OBJECT *pPLKey,
                                 OBJECT *pPLDict,
                                 OBJECT *pRetStr )
{
  int32 keynum;
  int32 startnum;
  int32 totalLen;
  OBJECT NumStr = OBJECT_NOTVM_NOTHING;
  int32  NumStrLen;
  OBJECT Prfx = OBJECT_NOTVM_NOTHING;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  static NAMETYPEMATCH page_label_dict[] = {
/* 0 */  { NAME_S  | OOPTIONAL,  2, { ONAME, OINDIRECT }},
/* 1 */  { NAME_P  | OOPTIONAL,  2, { OSTRING, OINDIRECT }},
/* 2 */  { NAME_St | OOPTIONAL,  2, { OINTEGER, OINDIRECT }},
    DUMMY_END_MATCH
  };

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;


  /* First, read in the dictionary if not already done so. */
  if (oType(*pPLDict) == OINDIRECT) {
    OBJECT *pTmp;
    if (!pdf_lookupxref( pdfc, &pTmp, oXRefID(*pPLDict),
                         theIGen(pPLDict), FALSE ))
      return FALSE;
    if (pTmp == NULL)
      return error_handler( UNDEFINED );
    Copy( pPLDict, pTmp );
  }

  if (oType(*pPLDict) != ODICTIONARY ) {
    HQFAIL( "Page label key not a dictionary" );
    return error_handler( TYPECHECK );
  }

  /* Extract page label dictionary values */
  if (!pdf_dictmatch( pdfc, pPLDict, page_label_dict ))
    return FALSE;

  /* For the numeric portion of the page label, use the current page number
     minus the start-number of the range, and then add the 'St'art value. */
  if ( !object_get_integer( pPLKey, &keynum) ) {
    HQFAIL( "Page entry key not an integer." );
    return FALSE;
  }

  if (page_label_dict[2].result != NULL) {  /* /St key */
    if ( !object_get_integer( page_label_dict[2].result, &startnum) ) {
      HQFAIL( "Start page key not an integer." );
      return FALSE ;
    }
    HQASSERT( startnum > 0, "Page label start number <= 0" );
  } else {
    startnum = 1;
  }

  startnum = (ixc->pageno - 1) - keynum + startnum;

  /* Now generate the text of the page label. Note that the lack of a
     style key is not an error. Each of the "make" functions listed in
     the switch below will allocate the string buffer to return the
     text in.  The string itself will be null terminated but the 'theLen'
     of the string OBJECT will be a bit bigger. */
  theTags(NumStr) = OSTRING;
  theLen(NumStr) = 0;
  oString(NumStr) = NULL;

  if (page_label_dict[0].result != NULL) {
    int32 stylenum = theINameNumber( oName(*page_label_dict[0].result) );

    switch (stylenum)
    {
    case NAME_D:  /* Decimal arabic numerals */
      if (!pdf_make_arabic( pdfc, startnum, &NumStr ))
        return FALSE;
      break;
    case NAME_R:  /* Uppercase roman numerals */
      if (!pdf_make_roman( pdfc, startnum, FALSE, &NumStr ))
        return FALSE;
      break;
    case NAME_r:  /* Lowercase roman numerals */
      if (!pdf_make_roman( pdfc, startnum, TRUE, &NumStr ))
        return FALSE;
      break;
    case NAME_A:  /* Uppercase letters */
      if (!pdf_make_letters( pdfc, startnum, FALSE, &NumStr ))
        return FALSE;
      break;
    case NAME_a:  /* Lowercase letters */
      if (!pdf_make_letters( pdfc, startnum, TRUE, &NumStr ))
        return FALSE;
      break;
    default:
      HQFAIL( "Unrecognised style for page label." );
      break;
    }
  }

  /* Use the prefix */
  if (page_label_dict[1].result != NULL)
    Copy( &Prfx, page_label_dict[1].result );
  else {
    theTags(Prfx) = OSTRING;
    theLen(Prfx)  = 0;
    oString(Prfx) = NULL;
  }

  /* Generate a return string big enough to retain both the
     prefix and the numeric portion. */
  totalLen = theLen(Prfx);
  if (oString(NumStr) != NULL) {
    NumStrLen = strlen_int32((char*)oString(NumStr));
    totalLen += NumStrLen;
  } else
    NumStrLen = 0;

  if (totalLen > 0) {
    if (!pdf_create_string( pdfc, totalLen, pRetStr ))
      return FALSE;

    if (theLen(Prfx) > 0)
      HqMemCpy( oString(*pRetStr), oString(Prfx), theLen(Prfx) );

    if (NumStrLen > 0) {
      HqMemCpy( &oString(*pRetStr)[theLen(Prfx)], oString(NumStr), NumStrLen );
      pdf_destroy_string( pdfc, theLen(NumStr), &NumStr );
    }
  } else {
    theTags(*pRetStr) = OSTRING;
    theLen(*pRetStr)  = 0;
    oString(*pRetStr) = NULL;
  }

  return TRUE;
}

/* ----------------------------------------------------------------------------
 * pdf_numtree_check_node()
 *
 * Traverses (depth-first) a number tree.  Unlike a _name_ tree (where the
 * /Limits entry is actually useful), the /Limits entry cannot be used because
 * each number key actually applies to a range of (page) numbers - the upper
 * limit of which is only determine by whatever the next number key is. So, if
 * /Limits gave a greatest key value of say 5, the '5' entry could still apply
 * to page 6, say, if the next node's first key was '7' or above.
 * Consequently, this function does a depth-first traversal of the number tree
 * looking at each '/Nums' entry to find a match.  Since it cannot know what
 * the range of any particular entry is until it sees the next one, and since
 * the next one may only be available from another node, we have to keep a
 * pointer to the 'last' entry seen as we go from node to node.
 *
 * Parameters:-
 *   pdfc - usual PDF context - current PDF page number is required out of it;
 *   pPLDict - dictionary of current node in the number tree;
 *   pLastKey - an OBJECT is maintained with the last key that was seen so far...
 *   pLastValue - ... and with that key's value;
 *   pPageKey - returned key that applies to the current PDF page number...
 *   pPageValue - ... and the value of that key (a page label dictionary).
 */
static Bool pdf_numtree_check_node( PDFCONTEXT *pdfc,
                                    OBJECT     *pPLDict,
                                    OBJECT     *pLastKey,
                                    OBJECT     *pLastValue,
                                    OBJECT     *pPageKey,
                                    OBJECT     *pPageValue )
{
  OBJECT *pArr;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  static NAMETYPEMATCH num_tree_dict[] = {
    /* 0 */ { NAME_Kids | OOPTIONAL,   3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    /* 1 */ { NAME_Nums | OOPTIONAL,   3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    /* -    { NAME_Limits | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }}, */
             DUMMY_END_MATCH
  };

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  /* Read in the page labels dictionary if not already done so. */
  if (oType(*pPLDict) == OINDIRECT) {
    OBJECT *pTmp;
    if (!pdf_lookupxref( pdfc, &pTmp, oXRefID(*pPLDict),
                         theIGen(pPLDict), FALSE ))
      return FALSE;
    if (pTmp == NULL)
      return error_handler( UNDEFINED );
    if (oType(*pTmp) != ODICTIONARY )
      return error_handler( TYPECHECK );
    pPLDict = pTmp;
  }

  /* Extract the keys */
  if (!pdf_dictmatch( pdfc, pPLDict, num_tree_dict ))
    return FALSE;


  /* Check each key in the Nums array until we find a key which
     either equals or is greater than the page number we're interested
     in.  The values of the Nums array are in pairs - the first is the
     number key and the second is the indirect object reference yielding
     the page label dictionary. */
  pArr = num_tree_dict[1].result;   /* /Nums entry */
  if (pArr != NULL) {
    OBJECT *pEnt = &oArray(*pArr)[0];
    int inx;

    HQASSERT( theLen(*pArr) % 2 == 0, "Nums array not all in pairs." );

    for ( inx = 0;  inx < theLen(*pArr); inx += 2 ) {
      int32 num;

      if ( !object_get_integer( pEnt, &num ) ) {
        HQFAIL( "Num entry not numeric." );
        return FALSE;
      }

      if (num == (ixc->pageno - 1)) {     /* "pageno - 1" as keys are page indexes */
        /* This is definitely the key to use. */
        Copy( pPageKey, pEnt );
        pEnt++;           /* point to page label dict */
        Copy( pPageValue, pEnt );
        return TRUE;

      } else if (num >= ixc->pageno) {
        /* Should use the last key. */
        Copy( pPageKey, pLastKey );
        Copy( pPageValue, pLastValue );
        return TRUE;
      }

      /* Retain the last key pair since it might still apply. */
      Copy( pLastKey, pEnt );
      pEnt++;
      Copy( pLastValue, pEnt );
      pEnt++;  /* Next Nums entry */
    }
  }

  /* Descend to each of the Kids nodes.*/
  pArr = num_tree_dict[0].result;   /* /Kids entry */
  if (pArr != NULL) {
    OBJECT *pEnt = &oArray(*pArr)[0];
    int inx;

    for (inx = 0;  inx < theLen(*pArr); inx++) {

      if (oType(*pEnt) != OINDIRECT && oType(*pEnt) != ODICTIONARY) {
        HQFAIL( "Kids entry in number tree not a dict." );
        return error_handler(TYPECHECK);
      }

      if (!pdf_numtree_check_node( pdfc, pEnt, pLastKey, pLastValue, pPageKey, pPageValue ))
        return FALSE;

      /* Check if a key was found. */
      if (oType(*pPageKey) != ONULL)
        return TRUE;

      pEnt++; /* Next Kids entry */
    }
  }

  return TRUE;
}

/* ----------------------------------------------------------------------------
 * Given a page number, this function uses the Page label dictionary (which is
 * a number tree) to construct the page _label_ appropriate for it.  The label
 * is returned as a string via the pRetStr parameter - after using it the
 * can 'pdf_destroy_string()' it.  If no such page label is found, *pRetStr is
 * returned as an ONULL object.  Beware that an empty string being returned is
 * a possibility.
 */
Bool pdf_make_page_label( PDFCONTEXT *pdfc, OBJECT *pRetStr )
{
  OBJECT *pPLDict;
  OBJECT LastKey = OBJECT_NOTVM_NULL;
  OBJECT LastValue = OBJECT_NOTVM_NULL;
  OBJECT PageKey = OBJECT_NOTVM_NULL;
  OBJECT PageValue = OBJECT_NOTVM_NULL;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  /* If there was no PageLabels dictionary in the Catalog, return
     no string (not an error). */
  theTags(*pRetStr) = ONULL;
  oString(*pRetStr) = NULL;
  pPLDict = ixc->pPageLabelsDict;
  if (pPLDict == NULL)
    return TRUE;

  /* Perform a traversal of the number tree hierarchy to find the entry
     appropriate to the given page number. */
  if (!pdf_numtree_check_node( pdfc, pPLDict, &LastKey, &LastValue, &PageKey, &PageValue ))
    return FALSE;

  /* Check a key was found. If not, then resort to the last key. */
  if (oType(PageKey) == ONULL) {
    OCopy( PageKey, LastKey );
    OCopy( PageValue, LastValue );
  }

  if (oType(PageKey) == ONULL) {
    HQFAIL( "Warning: page label entry just not found." );
    return TRUE;    /* Not necessarily an error. */
  }

  /* Using the found key, construct the appropriate text of the page label. */
  if (!pdf_page_label_text( pdfc, &PageKey, &PageValue, pRetStr ))
    return FALSE;

  return TRUE;
}


/* Log stripped */
