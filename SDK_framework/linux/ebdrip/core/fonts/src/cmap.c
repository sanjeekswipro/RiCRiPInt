/** \file
 * \ingroup cmap
 *
 * $HopeName: COREfonts!src:cmap.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of Font Character Mapping (cmap) functionality
 */


#define OBJECT_SLOTS_ONLY

#include "core.h"
#include "swerrors.h"
#include "objects.h"
#include "dicthash.h"
#include "dictscan.h"
#include "namedef_.h"
#include "stacks.h"
#include "stackops.h"
#include "graphics.h"
#include "often.h"
#include "utils.h"
#include "cmap.h"

/* Prototypes */
static Bool cmap_codeget(OBJECT *codespace, OBJECT *instr, OBJECT *codestr);
static Bool cmap_cidlookup_local(OBJECT *mapping, OBJECT *stringo,
                                 int32 *fontid, OBJECT *charsel);
static Bool cmap_notdeflookup_local(OBJECT *notdef, OBJECT *stringo,
                                    int32 *fontid, OBJECT *charsel);

#define MAP_CID  0
#define MAP_BF   1

/* ----------------------------------------------------------------------------
   function:            cmap_lookup()      author:              Eric Penfold
   creation date:       10-Jul-1997        last modification:   ##-###-####
   arguments:           lots
   description:

   Top level Lookup function to return char id/cid for input data

   On input, the charsel object must be of type OSTRING, with the clist ptr
   pointing to an area of memory which may be used to store the result of an
   basefont character code mapping (OSTRING mapping type - see below). The size
   of this memory (given by the 'len' field) must be at least
   CMAP_MAX_CODESPACE_LEN bytes.

   The fontid parameter will be updated with id of the font corresponding
   to the mapping

   The charsel object passed will be updated to reflect the mapping type:
      OINTEGER => mapped to a CID font CID
      OSTRING  => mapped to a basefont character code. This may refer to a
                  composite font (check font type using fontid) - in which
                  case this could be a multi-byte string.
      ONAME    => mapped to a glyph name
      ONULL    => no mapping. This can happen if the input str does not start
                  with a valid character code (i.e. within any of the defined
                  codespace ranges).

   stringo should a pointer to the string to parse -> this pointer will be
   incremented by the number of bytes which were used for the cmap lookup,
   and the len parameter will be decremented by this number. Characters may
   still be consumed if no matching or valid mapping exists.

   Returns FALSE on error.
   ---------------------------------------------------------------------------- */
Bool cmap_lookup(OBJECT *cmapdict, OBJECT *stringo,
                 int32 *fontid, OBJECT *charsel)
{
  OBJECT mapstring = OBJECT_NOTVM_NOTHING, *cmap;
  uint8 mapcode[CMAP_MAX_CODESPACE_LEN];

  static NAMETYPEMATCH codemapmatch[] = {
    /* Use the enum below to index this match */
    { NAME_codespacerangeblock,          1, { OARRAY }},
    { NAME_mappingblock,                 1, { OARRAY }},
    { NAME_notdefrangeblock | OOPTIONAL, 1, { OARRAY }},
    DUMMY_END_MATCH
  } ;
  enum { codemapmatch_codespacerangeblock,
         codemapmatch_mappingblock,
         codemapmatch_notdefrangeblock } ;

  if ( oType(*cmapdict) != ODICTIONARY )
    return error_handler(TYPECHECK) ;

  cmap = fast_extract_hash_name(cmapdict, NAME_CodeMap);
  if ( !cmap )
    return error_handler(TYPECHECK);

  if (!dictmatch(cmap, codemapmatch))
    return FALSE;

  theTags(mapstring) = OSTRING | LITERAL | UNLIMITED ;
  theLen(mapstring) = CMAP_MAX_CODESPACE_LEN ;
  oString(mapstring) = mapcode ;

  if (!cmap_codeget(codemapmatch[codemapmatch_codespacerangeblock].result,
                    stringo, &mapstring))
    return FALSE;

  if ( theLen(mapstring) == 0 ) { /* Invalid char string */
    object_store_null(charsel) ;
    return TRUE ;
  }

  if ( !cmap_cidlookup_local(codemapmatch[codemapmatch_mappingblock].result,
                             &mapstring, fontid, charsel) )
    return FALSE ;

  if ( oType(*charsel) != ONULL ) /* Found a character mapping */
    return TRUE ;

  theTags(*charsel) = OINTEGER | LITERAL; /* Notdef mapping must be CID */

  return cmap_notdeflookup_local(codemapmatch[codemapmatch_notdefrangeblock].result,
                                 &mapstring, fontid, charsel);
}



/* ----------------------------------------------------------------------------
   function:            cmap_lookup_notdef()  author:              Eric Penfold
   creation date:       07-Aug-1997            last modification:   ##-###-####
   arguments:           lots
   description:

   Top level Lookup function to return notdef cid for input data

   In Cmaps, notdefs can only map to CID fonts, and this routine is intended
   for use when the main cmap_lookup routine has been called, but the resulting
   CID in the specified font doesn't physically exist in the font definition,
   and so the appropriate notdef character needs to be displayed.

   The fontid parameter will be updated with id of the font corresponding to
   the mapping.

   The charsel parameter will be set to the notdef character appropriate to
   the input code.

   codestr should be a pointer to the input code string which was originally
   passed to cmap_lookup. The length of the input code will be (length of the
   input steam before cmap_lookup) - (length of input stream after
   cmap_lookup)

   Returns FALSE on error (an invalid CMap). Sets the type of charsel to
   ONULL if no mapping was performed.
   ---------------------------------------------------------------------------- */
Bool cmap_lookup_notdef(OBJECT *cmapdict, OBJECT *codestr,
                        int32 *fontid, OBJECT *charsel)
{
  OBJECT *cmap;

  static NAMETYPEMATCH codemapmatch[] = {
    /* Use the enum below to index this match */
    { NAME_codespacerangeblock,          1, { OARRAY }},
    { NAME_mappingblock,                 1, { OARRAY }},
    { NAME_notdefrangeblock | OOPTIONAL, 1, { OARRAY }},
    DUMMY_END_MATCH
  } ;
  enum { codemapmatch_codespacerangeblock,
         codemapmatch_mappingblock,
         codemapmatch_notdefrangeblock } ;

  if ( oType(*cmapdict) != ODICTIONARY )
    return error_handler(TYPECHECK) ;

  cmap = fast_extract_hash_name(cmapdict, NAME_CodeMap);
  if ( !cmap )
    return error_handler(TYPECHECK);

  if (!dictmatch(cmap, codemapmatch))
    return FALSE;

  /* This routine should not be called for non-CID fonts, because it forces
     the output to be a CID font. Asserting this would involve a huge number
     of extra includes, so I've not done it. */
  if ( !cmap_notdeflookup_local(codemapmatch[codemapmatch_notdefrangeblock].result,
                                codestr, fontid, charsel) )
    return FALSE ;

  /* PLRM3 p.390: If no notdef mapping, force font 0 and CID 0. We can do
     this here because we shouldn't be called with a non-CID font. */
  if ( oType(*charsel) == ONULL ) {
    object_store_integer(charsel, 0) ;
    *fontid = 0 ;
  }

  return TRUE ;
}



/* ----------------------------------------------------------------------------
   function:            cmap_codeget()     author:              Eric Penfold
   creation date:       10-Jul-1997        last modification:   ##-###-####

   Extracts the cmap code from the 'show' string and writes into code string.
   If the input string 'str' can be matched to a defined codespace range,
   The number of matched bytes (length of code) is deducted from the len
   parameter, the str pointer is incremented by this number, and TRUE returned.

   TRUE is also returned if str is not within any defined codespace range
   (and the minimum length codespace will be consumed from the input str). In
   this case, the output string will have length zero.

   Bytes consumed from str as per PLRM3, p. 390
---------------------------------------------------------------------------- */
static Bool cmap_codeget(OBJECT *codespace, OBJECT *instr, OBJECT *codestr)
{
  int32 num_block;
  int32 num_entry;
  int32 idx_block;
  int32 idx_entry;
  OBJECT *rd_block;
  OBJECT *rd_entry;
  uint8 *str, *code ;

  int32 max_match = 0; /* max # bytes matched for defined codespace ranges */
  int32 min_csdim = CMAP_MAX_CODESPACE_LEN; /* shortest codespace (in bytes) */
  int32 i;

  HQASSERT(oType(*instr) == OSTRING, "Input string wrong type") ;
  HQASSERT(oType(*codestr) == OSTRING, "Output string wrong type") ;

  str = oString(*instr) ;
  code = oString(*codestr) ;
  HQASSERT(theLen(*codestr) >= CMAP_MAX_CODESPACE_LEN,
           "Not enough space in code string");

  HQASSERT(oType(*codespace) == OARRAY, "CMap codespace block is not an array") ;
  num_block = theLen(*codespace);
  if (num_block < 1)
    return error_handler( RANGECHECK );

  /* For each 'codespacerange definition block' */
  for (idx_block = 0; idx_block < num_block; idx_block++) {
    rd_block = oArray(*codespace) + idx_block;

    HQASSERT(oType(*rd_block) == OARRAY,
             "CMap codespace sub-block is not an array") ;
    num_entry = theLen(*rd_block);

    /* For each entry in that block */
    for (idx_entry = 0; idx_entry < num_entry; idx_entry++) {
      OBJECT *rd_mbyte;
      int32   len_entry;
      int32   idx_byte;
      uint8  *rstart;
      uint8  *rend;
      uint8  *inbyte;

      rd_entry  = oArray(*rd_block) + idx_entry;
      HQASSERT(oType(*rd_entry) == OARRAY,
               "CMap codespace entry pair is not an array") ;
      rd_mbyte  = oArray(*rd_entry);

      HQASSERT(oType(rd_mbyte[0]) == OSTRING && oType(rd_mbyte[1]) == OSTRING,
               "CMap codespace pair entry is not a string") ;
      HQASSERT(theLen(*rd_mbyte) == theLen(rd_mbyte[1]),
              "CMap codespace ranges of different lengths");
      HQASSERT(theLen(*rd_mbyte) <= CMAP_MAX_CODESPACE_LEN,
              "CMap range has more bytes than expected");

      len_entry = theLen(*rd_mbyte); /* == theLen(rd_mbyte[1]) [we hope!] */
      if (len_entry < min_csdim)
        min_csdim = len_entry;

      /* Extract range_start and range_end, and stream_start  */
      rstart = oString(rd_mbyte[0]);
      rend   = oString(rd_mbyte[1]);
      inbyte = str;

      /* Test each byte of input with corresp. bytes of range start/end */
      idx_byte = 0;
      do {
        if (idx_byte >= theLen(*instr))
          break; /* Too short */
        if (*inbyte < *rstart)
          break; /* Below range start */
        if (*inbyte > *rend)
          break; /* Above range end */

        /* All bytes of this entry were matched */
        if (++idx_byte == len_entry) {
          /* Copy matched bytes into output string 'code' */
          for (i = 0; i < len_entry; i++)
            code[i] = str[i];
          theLen(*codestr) = (uint16)len_entry;
          /* Consume bytes from str */
          theLen(*instr) = (uint16)(theLen(*instr) - len_entry);
          oString(*instr) += len_entry;
          return TRUE;
        }

        /* Next range start/end bytes */
        rstart++;
        rend++;
        inbyte++;
      } while (idx_byte <= len_entry);

      /* If any bytes matched, should consume # bytes of tested code range */
      if (idx_byte > 0)
        max_match = len_entry;
    }
  }

  /* If no bytes matched, then must consume # bytes of shortest cs range */
  if (max_match == 0)
    max_match = min_csdim;
  /* Don't consume more bytes than we have */
  if (max_match > theLen(*instr))
    max_match = theLen(*instr);

  theLen(*codestr) = 0 ;
  oString(*codestr) = NULL ;

  /* Consume bytes from str */
  theLen(*instr) = (uint16)(theLen(*instr) - max_match);
  oString(*instr) += max_match;

  return TRUE; /* But theLen(*codestr) == 0, so not valid */
}



/* ----------------------------------------------------------------------------
   function:            cmap_cidlookup_local()    author:          Eric Penfold
   creation date:       10-Jul-1997            last modification:   07-Aug-1997

   Internal lookup function to obtain character selector for input character
   code.

   On input, the charsel's string field must be of type OSTRING, with the
   clist ptr pointing to an area of memory which may be used to store the
   result of an basefont character code mapping (OSTRING mapping type - see
   below). The size of this memory (given by the 'len' field) must be at
   least CMAP_MAX_CODESPACE_LEN bytes.

   The fontid parameter will be updated with id of the font corresponding
   to the mapping

   The charsel object passed will be updated to reflect the mapping type:
      OINTEGER => mapped to a CID font CID
      OSTRING  => mapped to a basefont character code. This may refer to a
                  composite font (check font type using fontid) - in which
                  case this could be a multi-byte string.
      ONAME    => mapped to a glyph name
      ONULL    => no mapping

   Returns FALSE on error.
   ---------------------------------------------------------------------------- */
static Bool cmap_cidlookup_local(OBJECT *mapping, OBJECT *codestr,
                                 int32 *fontid, OBJECT *charsel)
{
  int32   num_block;
  int32   num_entry;
  int32   idx_block;
  int32   idx_entry;
  OBJECT *rd_block;
  OBJECT *rd_entry;
  uint8  *code ;

  HQASSERT(charsel && oType(*charsel) == OSTRING,
           "charsel parameter must be a string object");
  HQASSERT(codestr && oType(*codestr) == OSTRING,
           "codestr parameter must be a string object");

  code = oString(*codestr) ;

  HQASSERT(oType(*mapping) == OARRAY, "CMap mapping block is not an array") ;
  num_block = theLen(*mapping);
  if (num_block < 1)
    return error_handler(RANGECHECK);

  /* For each mapping definition block */
  for (idx_block = 0; idx_block < num_block; idx_block+=3) {
    int32 map_type;
    rd_block = oArray(*mapping) + idx_block + 2;

    /* Extract fontid and mapping type */
    HQASSERT(oType(rd_block[-2]) == OINTEGER,
             "CMap mapping fontid is not an integer") ;
    *fontid  = oInteger(rd_block[-2]);
    HQASSERT(oType(rd_block[-1]) == OINTEGER,
             "CMap mapping type is not an integer") ;
    map_type = oInteger(rd_block[-1]);
    HQASSERT(map_type == MAP_CID || map_type == MAP_BF,
             "Invalid CMap mapping type") ;

    /* For each entry in that block */
    HQASSERT(oType(*rd_block) == OARRAY,
             "CMap mapping sub-block is not an array") ;
    num_entry = theLen(*rd_block);
    for (idx_entry = 0; idx_entry < num_entry; idx_entry++) {
      OBJECT *rd_mbyte;
      int32   idx_byte;
      int32   len_entry;
      int32   offset_vector[CMAP_MAX_CODESPACE_LEN];
      uint8  *rstart;
      uint8  *rend;
      uint8  *inbyte;
      rd_entry  = oArray(*rd_block) + idx_entry;
      HQASSERT(oType(*rd_entry) == OARRAY,
               "CMap mapping block entry is not an array") ;
      rd_mbyte  = oArray(*rd_entry);
      HQASSERT(oType(rd_mbyte[0]) == OSTRING && oType(rd_mbyte[1]) == OSTRING,
               "CMap mapping range entry is not a string") ;
      HQASSERT( theLen(*rd_mbyte) == theLen(rd_mbyte[1]),
                "mapping range start/end are of different length" );
      len_entry = theLen(*rd_mbyte);

      /* If the range is of a different dimension to the code, then skip */
      if (len_entry != theLen(*codestr))
        continue;

      /* Extract range_start and range_end */
      rstart = oString(rd_mbyte[0]);
      rend   = oString(rd_mbyte[1]);
      inbyte = code;

      /* Process each byte of the range */
      for (idx_byte = 0; idx_byte < len_entry; idx_byte++) {
        if (*inbyte < *rstart)
          break; /* Below range start */
        if (*inbyte > *rend)
          break; /* Above range end */

        /* Build position vector for this mapping */
        *(offset_vector + idx_byte) = (*inbyte - *rstart);

        /* Next range start/end bytes */
        rstart++;
        rend++;
        inbyte++;
      }

      if (idx_byte == len_entry) { /* Stop after first match */
        int32 i, j;
        OBJECT *base = rd_mbyte + 2;

        /* Process third parameter, depending on mapping type */
        if ((map_type == MAP_BF) && oType(*base) == OSTRING) {
          /* character code */
          uint8 *baselist = oString(*base);
          int32 baselen = theLen(*base);

          /* Check that mem ptr passed in to store result */
          HQASSERT(oString(*charsel),
                   "char selector string clist field must be ptr to valid memory to store result");
          /* Check that string memory provided is large enough */
          if (baselen > theLen(*charsel)) {
            HQFAIL("Insufficient memory passed to store multi-byte string result");
            return error_handler(RANGECHECK);
          }
          theLen(*charsel) = (uint16)baselen;

          i = j = 0 ;

          if ( len_entry < baselen ) {
            do {
              oString(*charsel)[i] = baselist[i] ;
              ++i ;
            } while ( len_entry + i < baselen ) ;
          } else if ( len_entry > baselen ) {
            do { /* Check that we're not stuffing too large a value */
              if ( offset_vector[j] != 0 )
                return error_handler(RANGECHECK) ;
              ++j ;
            } while ( baselen + j < len_entry ) ;
          }

          /* Add our vector offset components to the base character code */
          while ( i < baselen ) {
            oString(*charsel)[i] = (uint8)(baselist[i] + offset_vector[j]);
            ++i, ++j ;
          }
        }
        else if ( map_type == MAP_BF && oType(*base) == ONAME ) {
          theTags(*charsel) = ONAME | LITERAL ;
          oName(*charsel) = oName(*base) ;
        }
        else {
          int32 res;
          /* This is the complex bit since mapping can be multibyte i.e
           * multidimensional.
           * Assuming: n bytes, In = input byte n
           *           Sn = Range start byte n, En = Range end byte n
           * Calculation is:
           *   1.  rvec = [I1-S1, I2-S2, ..., In-Sn]
           *   2.  rvec[i] *= Prod(j=i+1...n) (Ej-Sj+1)
           *   3.  result = offset + Sum(i=1...n) (rvec[i])
           *
           * Note that we use the offset_vector array from above which
           * contains a position vector from mapping start to our point
           * (i.e. =rvec of Calculation_Stage1)
           */
          /* 2 */
          rstart = oString(rd_mbyte[0]);
          rend   = oString(rd_mbyte[1]);
          for (i = 0; i < len_entry; i++) {
            for (j = i+1; j < len_entry; j++)
              *(offset_vector + i) *= *(rend + j) - *(rstart + j) + 1;
          }
          /* 3 */
          for (i = 0, res = 0; i < len_entry; i++)
            res += *(offset_vector + i);

          if (map_type == MAP_CID) { /* Offset by CID base */
            if ( oType(*base) == OINTEGER ) {
              object_store_integer(charsel, oInteger(*base) + res);
            } else
              return error_handler(TYPECHECK);
          } else if (map_type == MAP_BF) {
            if ( oType(*base) == OARRAY ) { /* => array of glyph names */
              theTags(*charsel) = ONAME | LITERAL ;
              oName(*charsel) = oName(oArray(*base)[res]);
            } else
              return error_handler(TYPECHECK);
          } else
            return error_handler(RANGECHECK);
        }
        return TRUE ;
      }
    }
  }

  /* No mapping made, but no error */
  object_store_null(charsel) ;
  return TRUE;
}


/* ----------------------------------------------------------------------------
   function:            cmap_notdeflookup_local()    author:       Eric Penfold
   creation date:       10-Jul-1997            last modification:   07-Aug-1997
   arguments:
     notdef  (Input)
     code    (Input)
     code length (Input)
     fontid  (Output)
     cid     (Output)
   description:

   Internal lookup function to obtain notdef character selector for input
   character code. If no notdef is defined for this code then the default of
   CID 0 is used (with default font 0).

   Returns FALSE on error. Sets fontid to -1 if no mapping could be performed.
---------------------------------------------------------------------------- */
static Bool cmap_notdeflookup_local(OBJECT *notdef, OBJECT *codestr,
                                    int32 *fontid, OBJECT *charsel)
{
  int32   num_block;
  int32   num_entry;
  int32   idx_block;
  int32   idx_entry;
  OBJECT *rd_block;
  OBJECT *rd_entry;
  uint8  *code ;

  HQASSERT(charsel, "No character selector parameter") ;
  HQASSERT(fontid, "No font number parameter") ;
  HQASSERT(codestr, "No character code string parameter") ;

  if ( notdef == NULL || theLen(*notdef) == 0 ) {
    /* Optional notdefrangeblock dict entry is empty */
    object_store_null(charsel) ;
    return TRUE;
  }

  code = oString(*codestr) ;

  /* For each mapping definition block */
  num_block = theLen(*notdef);
  for (idx_block = 0; idx_block < num_block; idx_block+=2) {
    rd_block = oArray(*notdef) + idx_block + 1;

    /* Extract fontid */
    *fontid = oInteger(rd_block[-1]);

    /* For each entry in that block */
    num_entry = theLen(*rd_block);
    for (idx_entry = 0; idx_entry < num_entry; idx_entry++) {
      OBJECT *rd_mbyte;
      int32   idx_byte;
      int32   len_entry;
      uint8  *rstart;
      uint8  *rend;
      uint8  *inbyte;

      rd_entry  = oArray(*rd_block) + idx_entry;
      rd_mbyte  = oArray(*rd_entry);
      HQASSERT( theLen(*rd_mbyte) == theLen(rd_mbyte[1]),
                "mapping range start/end are of different length" );
      len_entry = theLen(*rd_mbyte);

      /* If the range is of a different dimension to the code, then skip */
      if (len_entry != theLen(*codestr))
        continue;

      /* Extract range_start and range_end */
      rstart = oString(rd_mbyte[0]);
      rend   = oString(rd_mbyte[1]);
      inbyte = code;

      /* Process each byte of the range */
      for (idx_byte = 0; idx_byte < len_entry; idx_byte++) {
        if (*inbyte < *rstart)
          break; /* Below range start */
        if (*inbyte > *rend)
          break; /* Above range end */

        /* Next range start/end bytes */
        rstart++;
        rend++;
        inbyte++;
      }

      if (idx_byte == len_entry) { /* Stop after first match */
        OBJECT *base = rd_mbyte + 2;

        /* Process third parameter (notdef base character) */
        if ( oType(*base) == OINTEGER ) {
          object_store_integer(charsel, oInteger(*base)); /* Don't offset */
        } else
          return error_handler(TYPECHECK);
        return TRUE;
      }
    }
  }

  /* Didn't obtain a notdef mapping result */
  object_store_null(charsel) ;
  return TRUE;
}


/* ----------------------------------------------------------------------------
   function:            cmap_getfontmatrix           author:       Eric Penfold
   creation date:       13-Aug-1997            last modification:   ##-###-####
   arguments:
     cmapdict (Input)
     fontid   (Input)
     matrix   (Output)

   description:
     Obtains the font matrix from the CMap to use with the specified
     descendant font.
     If no matrices are defined at all, or no matrix is defined for this
     fontid, then 'matrix' will be set to a null object.


     Returns FALSE if the object in the matrix array is not a valid matrix,
     or if the fontid is invalid (i.e. the matrix array has fewer entries);
     TRUE is otherwise returned.
---------------------------------------------------------------------------- */
Bool cmap_getfontmatrix(OBJECT *cmapdict, int32 fontid, OBJECT *matrix)
{
  OBJECT *cmap, *matblk;
  OMATRIX omat;

  /* Extract CMap */
  if ( oType(*cmapdict) != ODICTIONARY )
    return FAILURE(FALSE); /* Error - Invalid dictionary */

  cmap = fast_extract_hash_name(cmapdict, NAME_CodeMap);

  /* Extract matrix block */
  if ( oType(*cmap) != ODICTIONARY )
    return FAILURE(FALSE); /* Error - Invalid dictionary */

  matblk = fast_extract_hash_name(cmap, NAME_usematrixblock);

  if ( !matblk ) {
    object_store_null(matrix); /* No usematrix defined, use defaults */
  } else if ( oType(*matblk) != OARRAY ||
              theLen(*matblk) <= fontid ||
              !is_matrix(oArray(*matblk) + fontid, &omat) ) {
    return FAILURE(FALSE); /* usematrix definition is not valid (for this fontid) */
  } else
    Copy(matrix, oArray(*matblk) + fontid);

  return TRUE;
}


/* ----------------------------------------------------------------------------
   function:            cmap_getfontmatrix           author:       Eric Penfold
   creation date:       13-Aug-1997            last modification:   ##-###-####
   arguments:
     cmapdict (Input)
     wmode    (Output)

   description:
     Obtains the writing mode 'wmode' (valid only for CID descendants) to be
     applied for the Type0 font with this CMap.

     If this is not defined for the CMap, the default wmode of 0 is used.

     Returns TRUE unless the wmode entry is invalid, in which case it returns
     FALSE
---------------------------------------------------------------------------- */
Bool cmap_getwmode(OBJECT *cmapdict, int32 *wmode)
{
  OBJECT *val;

  if ( oType(*cmapdict) != ODICTIONARY )
    return FAILURE(FALSE); /* Error - Invalid dictionary */

  val = fast_extract_hash_name(cmapdict, NAME_WMode);

  if ( !val )
    *wmode = 0; /* No WMode entry - use default */
  else if ( oType(*val) != OINTEGER )
    return FAILURE(FALSE); /* Error - Invalid WMode entry */
  else
    *wmode = oInteger(*val);

  return TRUE;
}

/*---------------------------------------------------------------------------*/
/* Walk the CMap's mapping block, calling back the iterator function for each
   block in turn. This is used when constructing CIDMaps for CID Font Type 2
   (TrueType). */
Bool cmap_walk(OBJECT *cmap, cmap_iterator_fn iterator, void *data)
{
  OBJECT *mapping;
  int32 num_block;

  HQASSERT(cmap, "No CMap dictionary") ;
  HQASSERT(iterator, "No CMap iterator function") ;

  if ( oType(*cmap) != ODICTIONARY )
    return error_handler(TYPECHECK) ;

  cmap = fast_extract_hash_name(cmap, NAME_CodeMap);
  if ( !cmap )
    return error_handler(TYPECHECK);

  mapping = fast_extract_hash_name(cmap, NAME_mappingblock);
  if ( !mapping || oType(*mapping) != OARRAY )
    return error_handler(TYPECHECK);

  num_block = theLen(*mapping);
  if (num_block < 1)
    return error_handler(RANGECHECK);

  /* For each mapping definition block */
  for ( mapping = oArray(*mapping) ; (num_block -= 3) >= 0 ; mapping += 3 ) {
    int32 num_entry;
    OBJECT *rd_entry;
#if defined(ASSERT_BUILD)
    int32 fontid, map_type;

    /* Fontid and mapping type are in mapping[0] and mapping[1]. Neither of
       these are used for real; map_type is used for assertions. */
    HQASSERT(oType(mapping[0]) == OINTEGER,
             "CMap mapping fontid is not an integer") ;
    fontid = oInteger(mapping[0]);
    HQASSERT(oType(mapping[1]) == OINTEGER,
             "CMap mapping type is not an integer") ;
    map_type = oInteger(mapping[1]);
    HQASSERT(map_type == MAP_CID || map_type == MAP_BF,
             "Invalid CMap mapping type") ;
#endif

    /* For each entry in that block */
    HQASSERT(oType(mapping[2]) == OARRAY,
             "CMap mapping block list is not an array") ;
    num_entry = theLen(mapping[2]);
    for ( rd_entry = oArray(mapping[2]); --num_entry >= 0 ; ++rd_entry ) {
      OBJECT *rd_mbyte, mapsfrom = OBJECT_NOTVM_NOTHING,
        mapsto = OBJECT_NOTVM_NOTHING ;
      uint32 offset, rlen, bindex, eindex, dimensions;
      Bool more ;
      uint8 rcurr[CMAP_MAX_CODESPACE_LEN], rmap[CMAP_MAX_CODESPACE_LEN] ;
      uint8 *rstart, *rend ;

      HQASSERT(oType(*rd_entry) == OARRAY,
               "CMap mapping block entry is not an array") ;
      HQASSERT(theLen(*rd_entry) == 3,
               "CMap mapping block entry length is wrong") ;
      rd_mbyte = oArray(*rd_entry);
      HQASSERT(oType(rd_mbyte[0]) == OSTRING && oType(rd_mbyte[1]) == OSTRING,
               "CMap mapping range entry is not a string") ;
      HQASSERT(theLen(rd_mbyte[0]) == theLen(rd_mbyte[1]),
               "mapping range start/end are of different length" );
      dimensions = theLen(rd_mbyte[0]);
      HQASSERT(dimensions > 0 && dimensions <= CMAP_MAX_CODESPACE_LEN,
               "mapping range dimensions invalid" );

      rstart = oString(rd_mbyte[0]) ;
      rend = oString(rd_mbyte[1]) ;

      /* Make a copy of the range string; this will be modified to iterate
         over contiguous sub-ranges of the range. */
      OCopy(mapsfrom, rd_mbyte[0]) ;
      for (bindex = 0; bindex < dimensions; ++bindex) {
        rcurr[bindex] = rstart[bindex];
      }
      oString(mapsfrom) = rcurr ;

      /* The range may differ in more than one byte (a multidimensional
         range). We are going to pass contiguous ranges to the iterator
         function, so we will unpack multi-dimensional ranges to single
         dimensional ranges. The following code iterates over the range,
         selecting contiguous sub-ranges for each iteration. We can optimise
         the size of the sub-ranges selected by noting how many trailing
         bytes of the dimension strings cover the full byte range; these need
         never be changed in our iteration. */
      for ( eindex = dimensions, rlen = 1 ; --eindex > 0 ; ) {
        if ( rstart[eindex] != 0 || rend[eindex] != 0xff )
          break ;
        rlen <<= 8 ;
      }
      /* rlen is the length of the maximum contiguous span within the
         dimensions. eindex is the last dimension in the range strings that
         does not cover the full byte range. */
      rlen *= rend[eindex] - rstart[eindex] + 1 ;

      /* bindex is the first dimension in which there is a difference. If
         bindex and eindex are the same, the whole range is covered in one
         go. */
      for ( bindex = 0 ; bindex < eindex ; ++bindex ) {
        if ( rstart[bindex] != rend[bindex] )
          break ;
      }

      offset = 0 ;
      do {
        /* Set up the mapped value. */
        OCopy(mapsto, rd_mbyte[2]);
        switch ( oType(mapsto) ) {
        case OSTRING:
          HQASSERT(map_type == MAP_BF, "String mapping but not bf*") ;
          /* ToUnicode CMaps can have multiple Unicode character mappings for
             CIDs, longer than CMAP_MAX_CODESPACE_LEN. They will only map a
             single character to such a sequence, so we allow single
             mappings to use the string directly. */
          if ( eindex > bindex ) {
            uint32 dindex = 0, mindex = 0 ;

            HQASSERT(theLen(mapsto) <= CMAP_MAX_CODESPACE_LEN,
                     "String mapping too long") ;

            if ( dimensions < theLen(mapsto) ) {
              do {
                rmap[mindex] = oString(mapsto)[mindex] ;
                ++mindex ;
              } while ( dimensions + mindex < theLen(mapsto) ) ;
            } else if ( dimensions > theLen(mapsto) ) {
              do { /* Check that we're not stuffing too large a value */
                if ( rcurr[dindex] != rstart[dindex] )
                  return error_handler(RANGECHECK) ;
                ++dindex ;
              } while ( theLen(mapsto) + dindex < dimensions ) ;
            }

            /* Add offsets of current start to the base character code */
            while ( mindex < theLen(mapsto) ) {
              rmap[mindex] = (uint8)(oString(mapsto)[mindex] +
                                     rcurr[dindex] - rstart[dindex]);
              ++dindex, ++mindex ;
            }
            oString(mapsto) = rmap ;
          }
          break ;
        case OARRAY:
          HQASSERT(map_type == MAP_BF, "Array mapping but not bf*") ;
          HQASSERT(theLen(mapsto) >= offset + rlen,
                   "Array mapping does not cover range") ;
          theLen(mapsto) = CAST_TO_UINT16(rlen) ;
          oArray(mapsto) += offset ;
          break ;
        case ONAME:
          HQASSERT(map_type == MAP_BF, "Name mapping but not bf*") ;
          HQASSERT(rlen == 1, "Name mapping but not a single element") ;
          break ;
        case OINTEGER:
          HQASSERT(map_type == MAP_CID, "Integer mapping but not cid*") ;
          oInteger(mapsto) += offset ;
          break ;
        default:
          HQFAIL("Invalid mapping") ;
          break ;
        }

        if ( !(*iterator)(&mapsfrom, &mapsto, rlen, data) )
          return FALSE ;

        SwOftenUnsafe() ;

        /* We just dealt with the eindex dimension. Update the next dimension
           that is not done. */
        offset += rlen ;
        more = FALSE ;
        for ( bindex = eindex ; bindex-- > 0 ; ) {
          if ( rcurr[bindex] != rend[bindex] ) {
            rcurr[bindex]++ ;
            more = TRUE ;
            break ;
          } else
            rcurr[bindex] = rstart[bindex] ;
        }
      } while ( more ) ;
    }
  }

  return TRUE ;
}


/* Log stripped */
