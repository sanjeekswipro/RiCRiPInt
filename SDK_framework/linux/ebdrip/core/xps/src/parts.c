/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!src:parts.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of part names and related functions which deal with
 * part names.
 */

#include "core.h"
#include "mmcompat.h"          /* mm_alloc_with_header etc.. */
#include "swerrors.h"          /* error_handler */
#include "swctype.h"
#include "tables.h"
#include "hqmemcmp.h"          /* HqMemCmp */
#include "hqmemset.h"
#include "namedef_.h"          /* NAME_* */
#include "stacks.h"            /* operandstack */
#include "dictscan.h"          /* NAMETYPEMATCH */
#include "dicthash.h"

#include "xmlg.h"              /* xmlG interfaces */

#include "xpspartspriv.h"
#include "xps.h"
#include "xpspriv.h"
#include "xpsrelsblock.h"

/* Character lookup table and maro's implement the following as per spec.
 *
 * RFC2234_IS_ALPHA(c) ((c >= 0x41 && c <= 0x5A) || (c >= 0x61 && c <= 0x7A))
 * RFC2234_IS_DIGIT(c)  (c >= 0x30 && c <= 0x39)
 *
 * RFC3986_IS_UNRESERVED(c) (RFC2234_IS_ALPHA(c) || RFC2234_IS_DIGIT(c) || \
 *                           c == '-' || c == '.' || c == '_' || c == '~')
 *
 * RFC3986_IS_GEN_DELIMS(c) (c == ':' || c == '/' || c == '?' || c == '#' || \
 *                           c == '[' || c == ']' || c == '@')
 *
 * RFC3986_IS_SUB_DELIMS(c) (c == '!' || c == '$' || c == '&' || c == '\'' || \
 *                           c == '(' || c == ')' || c == '*' || c == '+' || \
 *                           c == ',' || c == ';' || c == '=')
 *
 * RFC3986_IS_RESERVED(c) (RFC3986_IS_GEN_DELIMS(c) || RFC3986_IS_SUB_DELIMS(c))
 */

#define NONE 0
#define DIGIT 1
#define ALPHA 2
#define SUB_DELIMS 4
#define GEN_DELIMS 8
#define UNRESERVED 16
#define RESERVED 32

/* These macro's are only used to set values in the table and make it
   less likely to introduce mistakes. */
#define SET_SUB_DELIMS (SUB_DELIMS | RESERVED)
#define SET_GEN_DELIMS (GEN_DELIMS | RESERVED)
#define SET_ALPHA (ALPHA | UNRESERVED)
#define SET_DIGIT (DIGIT | UNRESERVED)

static const uint8 char_table[128] = {
  NONE,NONE,NONE,NONE,NONE,NONE,NONE,NONE, /* 0-7 */
  NONE,NONE,NONE,NONE,NONE,NONE,NONE,NONE, /* 8-15 */
  NONE,NONE,NONE,NONE,NONE,NONE,NONE,NONE, /* 16-23 */
  NONE,NONE,NONE,NONE,NONE,NONE,NONE,NONE, /* 24-31 */
  NONE, /* SP */
  SET_SUB_DELIMS, /* ! */
  NONE, /* " */
  SET_GEN_DELIMS, /* # */
  SET_SUB_DELIMS, /* $ */
  NONE, /* % */
  SET_SUB_DELIMS, /* & */
  SET_SUB_DELIMS, /* ' */
  SET_SUB_DELIMS, /* ( */
  SET_SUB_DELIMS, /* ) */
  SET_SUB_DELIMS, /* * */
  SET_SUB_DELIMS, /* + */
  SET_SUB_DELIMS, /* , */
  UNRESERVED, /* - */
  UNRESERVED, /* . */
  SET_GEN_DELIMS, /* / */
  SET_DIGIT, /* 0 */ /* 48 */
  SET_DIGIT, /* 1 */
  SET_DIGIT, /* 2 */
  SET_DIGIT, /* 3 */
  SET_DIGIT, /* 4 */
  SET_DIGIT, /* 5 */
  SET_DIGIT, /* 6 */
  SET_DIGIT, /* 7 */
  SET_DIGIT, /* 8 */
  SET_DIGIT, /* 9 */ /* 57 */
  SET_GEN_DELIMS, /* : */
  SET_SUB_DELIMS, /* ; */
  NONE, /* < */
  SET_SUB_DELIMS, /* = */
  NONE, /* > */
  SET_GEN_DELIMS, /* ? */
  SET_GEN_DELIMS, /* @ */
  SET_ALPHA, /* A */ /* 65 */
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA, /* Z */ /* 90 */
  SET_GEN_DELIMS, /* [ */
  NONE, /* \ */
  SET_GEN_DELIMS, /* ] */
  NONE, /* ^ */
  UNRESERVED, /* _ */ /* 95 */
  NONE, /* ` */
  SET_ALPHA, /* a */ /* 97 */
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA,
  SET_ALPHA, /* z */ /* 122 */
  NONE, /* { */
  NONE, /* | */
  NONE, /* } */
  UNRESERVED, /* ~ */
  NONE /* del */ /* 127 */
} ;

#define IS_ASCII(c)((c) < 128)

/** As defined in RFC 2234. */
#define RFC2234_IS_ALPHA(c) (IS_ASCII(c) && \
    (char_table[(c)] & ALPHA) == ALPHA)

#define RFC2234_IS_DIGIT(c) (IS_ASCII(c) && \
    (char_table[(c)] & DIGIT) == DIGIT)

/* As defined in RFC 3986 */
#define RFC3986_IS_UNRESERVED(c) (IS_ASCII(c) && \
    (char_table[(c)] & UNRESERVED) == UNRESERVED)

#define RFC3986_IS_GEN_DELIMS(c) (IS_ASCII(c) && \
    (char_table[(c)] & GEN_DELIMS) == GEN_DELIMS)

#define RFC3986_IS_SUB_DELIMS(c) (IS_ASCII(c) && \
    (char_table[(c)] & SUB_DELIMS) == SUB_DELIMS)

#define RFC3986_IS_RESERVED(c) (IS_ASCII(c) && \
    (char_table[(c)] & RESERVED) == RESERVED)

/* ============================================================================
   Part name UID handling.
   ============================================================================
*/

/* Regrettably the part name UID is used from PS so it needs to be an
   int32. To achieve this we keep a hash from normalised part names
   which exist in the names cache to int32 values which are unique but
   the same for every new part name reference. */

static int32 partname_next_uid = 0 ;

static int32 xps_partname_nextuid(void)
{
  int32 uid = partname_next_uid++ ;
  HQASSERT(partname_next_uid > 0, "wrapped partname uid") ;
  if (partname_next_uid < 0)
    partname_next_uid = 0 ;
  return uid ;
}

struct UID_cache_entry {
  xmlGIStr *key ; /* Normalised part name in the names table. */
  int32 uid ;
  struct UID_cache_entry *next ;
} ;

#define UID_CACHE_TABLE_SIZE 1024

static struct UID_cache_table {
  struct UID_cache_entry* table[UID_CACHE_TABLE_SIZE] ;
} UID_cache ;

static void init_UID_cache()
{
  HqMemSetPtr(UID_cache.table, NULL, UID_CACHE_TABLE_SIZE) ;
}

static void finish_UID_cache()
{
  uint32 i ;
  for (i=0; i < UID_CACHE_TABLE_SIZE; i++) {
    struct UID_cache_entry *curr = UID_cache.table[i] ;
    while (curr != NULL) {
      struct UID_cache_entry *next = curr->next ;
      mm_free(mm_xml_pool, curr, sizeof(struct UID_cache_entry)) ;
      curr = next ;
    }
  }
}

static struct UID_cache_entry *find_uid(xmlGIStr *key, uint32 *hval)
{
  uintptr_t hash ;

  struct UID_cache_entry *curr ;
  HQASSERT(key != NULL, "key is NULL") ;
  HQASSERT(hval != NULL, "hval is NULL") ;

  /* Lets just use the hash value of the interned key. */
  hash = intern_hash(key) % UID_CACHE_TABLE_SIZE ;
  *hval = CAST_UINTPTRT_TO_UINT32(hash) ;

  for (curr = UID_cache.table[*hval]; curr != NULL; curr = curr->next)
    if (curr->key == key)
      return curr;

  return NULL;
}

static Bool add_uid(xmlGIStr *key, int32 *uid)
{
  struct UID_cache_entry *curr ;
  uint32 hval ;

  HQASSERT(key != NULL, "key is NULL") ;
  HQASSERT(uid != NULL, "uid is NULL") ;

  if ((curr = find_uid(key, &hval)) == NULL) {
    if ((curr = mm_alloc(mm_xml_pool, sizeof(struct UID_cache_entry),
                         MM_ALLOC_CLASS_XPS_PARTNAME)) == NULL) {
      *uid = -1 ;
      return error_handler(VMERROR) ;
    }

    curr->key = key ;
    curr->uid = xps_partname_nextuid() ;
    curr->next = UID_cache.table[hval] ;

    UID_cache.table[hval] = curr ;
  }
  *uid = curr->uid ;

  return TRUE ;
}

/* ============================================================================
   Part name normalisation and creation.
   ============================================================================
*/

/** \todo This is a hack and in the wrong place. */
extern Bool xps_versioning_filter_init(
      xmlGFilterChain *filter_chain,
      uint32 position,
      xmlGFilter **filter,
      struct xpsSupportedUri **table) ;

/* From OPC 0.85 section 1.1.1.1 Part Name Syntax

  The part name grammar is defined as follows:

  part_name = 1*( "/" segment )
  segment   = 1*( pchar )
  pchar     = unreserved | sub-delims | pct-encoded | ":" | "@"
  unreserved, sub-delims, pchar and pct-encoded are defined in RFC 3986.

  Part name grammar implies the following constraints:
  a. A part name MUST NOT be empty [M1.1].
  b. A part name MUST NOT have empty segments [M1.3].
  c. A part name MUST start with a forward slash ("/") character [M1.4].
  d. A part name MUST NOT have a forward slash as the last character [M1.5].
  e. A segment MUST NOT hold any characters other than pchar characters [M1.6].

  Part segments have the following additional constraints:
  f. A segment MUST NOT contain percent-encoded forward slash ("/"), or backward slash ("\") characters [M1.7].
  g. A segment MUST NOT contain percent-encoded unreserved characters [M1.8].
  h. A segment MUST NOT end with a dot (".") character [M1.9].
  i. A segment MUST include at least one non-dot character [M1.10]. */
Bool xps_validate_partname_grammar(uint8 *in, uint32 inlen,
                                   uint32 type)
{
  uint8 *inlimit ;
  Bool dotsonly = FALSE ;
  Bool empty = FALSE ;

  HQASSERT(in, "No input partname to validate") ;
  inlimit = in + inlen ;

  /* check a) and c) */
  if ( type == XPS_NORMALISE_PARTNAME ) {
    if ( in == inlimit || *in != '/' )
      return error_handler(RANGECHECK) ;
  }

  /* check d) */
  if ( in[inlen - 1] == '/' )
    return error_handler(RANGECHECK) ;

  /*  part_name = 1*( "/" segment )
      segment   = 1*( pchar )
      pchar     = unreserved | sub-delims | pct-encoded | ":" | "@"
      unreserved, sub-delims, pchar and pct-encoded are defined in RFC 3986. */

  /* check e) */
  while ( in < inlimit ) {
    UTF8 ch = *in++ ;

    /* If its not ASCII, its invalid. */
    if ( iscntrl(ch) || (ch & 0x80) != 0 )
      return error_handler(RANGECHECK) ;

    switch ( ch ) {
      int8 hi, lo ;
    case '/':
      /* check b) and i) */
      if ( empty || dotsonly )
        return error_handler(RANGECHECK) ;
      empty = TRUE ;
      /* Only part name references are allowed to contains sequences
         of dots. These will ultimately be resolved against the part
         base URI and then checked for syntax again. */
      dotsonly = (type == XPS_NORMALISE_PARTREFERENCE) ;
      break ;
    case '.':
      empty = FALSE ;

      /* check h) */
      if (! dotsonly && in < inlimit && *in == '/')
        return error_handler(RANGECHECK) ;

      continue ;
    /* pchar = unreserved / pct-encoded / sub-delims / ":" / "@" as
       per RFC 3986 */
    case '%': /* We may have escape encoded characters. */
      if ( in + 2 > inlimit ||
           (hi = char_to_hex_nibble[*in++]) < 0 ||
           (lo = char_to_hex_nibble[*in++]) < 0 )
        return error_handler(RANGECHECK) ;

      ch = (UTF8)((hi << 4) | lo) ;

      /* check g) */
      if ( RFC3986_IS_UNRESERVED(ch) )
        return error_handler(RANGECHECK) ;

      /* check f) */
      switch ( ch ) {
      case '.': case '/': case '\\': /* explicitly mentioned */
        return error_handler(RANGECHECK) ;
      }
      empty = dotsonly = FALSE ;
      break ;
    case ':' : case '@': /* explicitly mentioned in pchar */
      empty = dotsonly = FALSE ;
      break ;
    case '[' : /* Handle content type stream for ZIP device *only* */
      if (type != XPS_NORMALISE_ZIPNAME)
        return error_handler(RANGECHECK) ;

#define CONTENT_TYPES "content_types].xml"
      if (inlimit - in != sizeof(CONTENT_TYPES) - 1)
        return error_handler(RANGECHECK) ;

      if (HqMemCmp(in, CAST_PTRDIFFT_TO_INT32(inlimit - in),
                   (const uint8*)CONTENT_TYPES, sizeof(CONTENT_TYPES) - 1) != 0 )
        return error_handler(RANGECHECK) ;

      return TRUE ;
    default:
      /* check e) */
      if ( ! RFC3986_IS_UNRESERVED(ch) &&
           ! RFC3986_IS_SUB_DELIMS(ch))
        return error_handler(RANGECHECK) ;

      empty = dotsonly = FALSE ;
      break ;
    }
  }

  if ( empty || dotsonly ) /* Last segment was empty or contained only dots */
    return error_handler(RANGECHECK) ;

  return TRUE ;
}

/* \brief Auxiliary function to normalize part names and references.

   There are effectively 3 different general entry points into this
   function:

   1. ZIP device when is sees ZIP unit names needs to check that the
      ZIP unit name adheres to the part naming grammar. If it does
      not, the unit can be ignored.

   2. Content type stream uses part names, and like the ZIP device,
      simply need to check that the part names adheres to the part
      naming grammar.

   3. Part references from XPS markup. These are Unicode strings which
      need to be converted to Uri's, resolved against the base URI and
      then validated that they are still valid part names.

   In all cases above we also convert to lowercase (more efficient
   while scanning the string).

   Finally, we also use this function for normalizing the extention in
   the content type stream. */
static Bool xps_normalise_name(uint8 *in, uint32 inlen,
      uint8 *output, uint32 *outlen, uint32 type)
{
  UTF8 *inlimit, *out, *outlimit, *first_dot ;
  Bool dots_only, empty, previous_was_slash ;

  HQASSERT(in, "No input partname to normalise") ;
  HQASSERT(output, "No output buffer for normalised partname") ;
  HQASSERT(outlen, "No output buffer length") ;

  out = output ;
  outlimit = out + *outlen ;
  inlimit = in + inlen ;

  /* Zero length part names are not allowed. */
  if ( in == inlimit)
    return error_handler(RANGECHECK) ;

  switch (type) {
  case XPS_NORMALISE_PARTNAME :
    if ( *in != '/' )
      return error_handler(RANGECHECK) ;
  case XPS_NORMALISE_ZIPNAME :
  case XPS_NORMALISE_EXTENSION :
    /* From the Open Package 0.75+ specification, I see no evidence
       that ZIP unit names ought to undergo any normalisation what so
       ever except that comparison is case insensitive. The same
       applies to part names in the content type stream and
       extensions. */
    while ( in < inlimit ) {
      UTF8 ch = *in++ ;

      if ( out == outlimit )
        return error_handler(LIMITCHECK) ;

      *out++ = (UTF8)tolower(ch) ;
    }
    break ;

  case XPS_NORMALISE_PARTREFERENCE :
    /* OPC 0.85 Appendix B. B.1 Creating an IRI from a Unicode String.

       With reference to Arc [1-2] in Figure B-1, a Unicode string is
       converted to an IRI by percent-encoding each ASCII character
       that does not belong to the set of reserved or unreserved
       characters as defined in RFC 3986. */

    /* We can use tolower on the characters at this stage because
       there are no UTF-8 high bit characters. Our canonical internal
       format is UTF8. This loop takes care of B.1, B.2 and B.3 step 1
       & 2 */
    while ( in < inlimit ) {
      UTF8 ch = *in++ ;

      /* We percent encode [ and ] because of B.3 step 1 */
      if ((RFC3986_IS_UNRESERVED(ch) || RFC3986_IS_RESERVED(ch)) &&
          (ch != '[' && ch != ']')) {
        if ( out == outlimit )
          return error_handler(LIMITCHECK) ;

        *out++ = (UTF8)tolower(ch) ;

      } else  { /* Looks like we MUST escape the character. */
        int8 hi, lo ;

        if ( out + 3 == outlimit )
          return error_handler(LIMITCHECK) ;

        /* We could have an existing percent encoded tripplet which
           should be left untouched. B.3 step 2. */
        if ( ch == '%' && in + 2 < inlimit &&
             char_to_hex_nibble[in[0]] >= 0 &&
             char_to_hex_nibble[in[1]] >= 0 ) {
          *out++ = (UTF8)ch ;
          ch = *in++ ;
          *out++ = (UTF8)tolower(ch) ;
          ch = *in++ ;
          *out++ = (UTF8)tolower(ch) ;
          continue ;
        }

        *out++ = (UTF8)'%' ;
        hi = (int8)(ch >> 4) ;
        lo = (int8)(ch & 0x0f) ;
        HQASSERT(hi < 16, "hi is not less than 16") ;
        HQASSERT(lo < 16, "lo is not less than 16") ;
        *out++ = (UTF8)tolower(nibble_to_hex_char[hi]) ;
        *out++ = (UTF8)tolower(nibble_to_hex_char[lo]) ;
      }
    }

    /* OPC final fraft from ECMA, Appendix A.3

       1. Percent-encode each open bracket ([) and close bracket (]).
       2. Percent-encode each percent (%) character that is not followed by a
          hexadecimal notation of an octet value.
       3. Un-percent-encode each percent-encoded unreserved character.
       4. Un-percent-encode each forward slash (/) and back slash (\).
       5. Convert all back slashes to forward slashes.
       6. If present, remove trailing dot (".") characters from each segment.
       7. Replace each occurrence of multiple consecutive forward slashes (/) with a single forward slash. 13
       8. If present, remove trailing forward slash.
       9. Remove complete segments that consist of three or more dots.
      10. Resolve the relative reference against the base URI of the part
          holding the Unicode string, as it is defined in RFC 3986 (Section
          5.2). */

    /* step 3 to 8 */
    inlimit = out ;
    in = output ;
    out = output ;
    dots_only = empty = TRUE ;
    first_dot = NULL ;
    previous_was_slash = FALSE ; /* used for step 7 */
    while ( in < inlimit ) {
      UTF8 ch = *in++ ;
      int8 hi, lo ;
      Bool is_escaped = FALSE ;

      /* If a % exists, it must now be a valid percent escaped
         tripplet because of scan above. */
      if ( ch == '%') {
        if ( in + 2 > inlimit ||
             (hi = char_to_hex_nibble[*in++]) < 0 ||
             (lo = char_to_hex_nibble[*in++]) < 0 ) {
          /* This should not be possible given the previous scan which
             will escape all non-valid percent escaped tripplets. */
          HQFAIL("Somehow got an invalid percent encode.") ;
          return error_handler(RANGECHECK) ;
        }

        is_escaped = TRUE ;
        ch = (UTF8)((hi << 4) | lo) ;
      }

      /* step 3 & 4 */
      if (RFC3986_IS_UNRESERVED(ch) || ch == '/' || ch == '\\') {
        if (ch == '\\') /* step 5 */
          ch = '/' ;

        if (ch == '/') {
          if (previous_was_slash) /* step 7 */
            continue ;
          previous_was_slash = TRUE ;

          if (first_dot != NULL) { /* We have seen a [dot]+/ */
            if (dots_only) { /* We have seen /[dot]+/ */
              if ((out - first_dot) > 2) {
                if (first_dot != output) /* Remove previous / */
                  first_dot-- ;
                out = first_dot ; /* step 9 */
              }
            } else { /* We have seen a [!dot][dot]+/ */
              out = first_dot ; /* step 6 */
            }
          }
          dots_only = empty = TRUE ;
          first_dot = NULL ;
          *out++ = '/' ;
          continue ;

        } else if (ch == '.') {
          if (first_dot == NULL) {
            first_dot = out ;
          }
          *out++ = '.' ;
          empty = previous_was_slash = FALSE ;
          continue ;
        }

        /* not a dot or /, must be unreserved */
        *out++ = (UTF8)tolower(ch) ;
        first_dot = NULL ;
        dots_only = empty = previous_was_slash = FALSE ;
        continue ;

      } else {
        if (is_escaped) {
          /* Leave the percent escaped value, which has already been
             lower cased. */
          *out++ = '%' ;
          in -= 2 ;
          *out++ = *in++ ;
          *out++ = *in++ ;
        } else {
          HQASSERT(RFC3986_IS_RESERVED(ch), "non reserved char is not escaped") ;
          *out++ = (UTF8)tolower(ch) ;
        }
      }

      dots_only = empty = previous_was_slash = FALSE ;
      first_dot = NULL ;
    }

    /* There is an implicit / at the end of the URI. */
    if (first_dot != NULL) { /* We have seen a [dot]+/ */
      /* If the last segment has trailing dots, remove them no matter
         what the length. */
      out = first_dot ;
    }
    break ;

  default:
    return error_handler(UNDEFINED) ;
    break ;
  } /* end switch */

  /* Update output length to reflect actual length of normalised
     string. */
  *outlen = CAST_PTRDIFFT_TO_UINT32(out - output) ;

  /* Remove trailing slash - step 9. */
  if (output[(*outlen) - 1] == '/') {
    /* Just to see it go when debugging. */
    output[(*outlen) - 1] = '\0' ;
    (*outlen)-- ;
  }

  return TRUE ;
}

Bool xps_xml_part_context_new(
      xmlDocumentContext *xps_ctxt,
      xps_partname_t *part_name,
      xpsRelationshipsBlock *rels_block,
      uint32 relationships_to_process,
      Bool need_new_relationships_parser,
      struct xpsXmlPartContext **part_ctxt)
{
  struct xpsXmlPartContext *new_part_ctxt ;

  HQASSERT(part_name != NULL, "NULL part_name") ;
  HQASSERT(part_ctxt != NULL, "NULL part_ctxt") ;

  *part_ctxt = NULL ;

  if ((new_part_ctxt = mm_alloc(mm_xml_pool,
                                sizeof(xpsXmlPartContext),
                                MM_ALLOC_CLASS_XPS_PARTNAME)) == NULL)
    return error_handler(VMERROR) ;

  new_part_ctxt->base.xps_ctxt = xps_ctxt ;
  new_part_ctxt->base.part_name = part_name ;
  new_part_ctxt->base.relationships_to_process = relationships_to_process ;
  new_part_ctxt->defining_resources = FALSE ;
  new_part_ctxt->defining_brush_resource = FALSE ;
  new_part_ctxt->flptr = NULL ;
  new_part_ctxt->within_element = FALSE ;
  new_part_ctxt->printticket_relationship_seen = FALSE ;

  /* no resources have been defined */
  SLL_RESET_LIST(&(new_part_ctxt->resourceblock_stack));

  new_part_ctxt->active_resource = NULL ;
  new_part_ctxt->executing_stack = NULL ;
  new_part_ctxt->active_res_depth = 0 ;
  new_part_ctxt->outermost_userlabel_depth = 0 ;
  new_part_ctxt->commit = NULL ;

  NAME_OBJECT(new_part_ctxt, XMLPART_CTXT_NAME) ;

  /* create relationships block or point to an provided indirect
     block */
  if (rels_block != NULL) {
    new_part_ctxt->relationships = rels_block ;
    new_part_ctxt->indirect_relationships = TRUE ;
  } else {
    /* If we need a new relationships parser but we are not interested
       in any relationships, why bother parsing the relationships
       part. */
    if (need_new_relationships_parser && relationships_to_process == 0) {
      need_new_relationships_parser = FALSE ;
    }
    if (! xps_create_relationship_block(&(new_part_ctxt->relationships), xps_ctxt,
                                        relationships_to_process,
                                        part_name,
                                        need_new_relationships_parser)) {
      mm_free(mm_xml_pool, new_part_ctxt, sizeof(struct xpsXmlPartContext)) ;
      return FALSE ;
    }
    new_part_ctxt->indirect_relationships = FALSE ;

    /* If we needed a new parser for the relationships file (which we
       will have just created, but only if a relationships file
       existed).. */
    if (need_new_relationships_parser) {
      /* If we need to process a PT, we need to do it now. */
      if (relationships_to_process & XPS_PROCESS_PRINTTICKET_REL) {
        xpsRelationship *relationship ;

        if (! xps_lookup_relationship_type(new_part_ctxt->relationships,
                                           XML_INTERN(rel_xps_2005_06_printticket),
                                           &relationship, TRUE)) {
          (void)xps_destroy_relationship_block(&(new_part_ctxt->relationships)) ;
          mm_free(mm_xml_pool, new_part_ctxt, sizeof(struct xpsXmlPartContext)) ;
          return FALSE ;
        }
        /* When we get here we have parsed enough of the .rels file to
           have processed the PT or we might have reached the end of
           the .rels stream without having found it. Eitherway, we
           don't care as the Relationship callback will have processed
           the print ticket. */
      }
    }
  }

  *part_ctxt = new_part_ctxt ;

  return TRUE ;
}

Bool xps_xml_part_context_free(
      struct xpsXmlPartContext **part_ctxt)
{
  Bool status = TRUE ;
  struct xpsXmlPartContext *old_part_ctxt ;
  xpsResourceBlock *resblock ;

  HQASSERT(part_ctxt != NULL, "NULL part_ctxt") ;
  old_part_ctxt = *part_ctxt ;
  HQASSERT(old_part_ctxt != NULL, "NULL old_part_ctxt") ;
  VERIFY_OBJECT(old_part_ctxt, XMLPART_CTXT_NAME) ;

  UNNAME_OBJECT(old_part_ctxt) ;

  HQASSERT(old_part_ctxt->flptr == NULL,
           "flptr is not NULL") ;

  HQASSERT(old_part_ctxt->commit == NULL,
           "commit stack is not NULL") ;

  HQASSERT(old_part_ctxt->active_res_depth == 0,
           "active_res_depth != 0");

  /*
  Under error condition, this does not get reset, so we can't assert
  this.

  HQASSERT(old_part_ctxt->outermost_userlabel_depth == 0,
           "outermost_userlabel_depth != 0");
  */

  HQASSERT(old_part_ctxt->active_resource == NULL,
           "active_resource is not NULL");

  HQASSERT(! old_part_ctxt->defining_resources,
           "defining_resources is TRUE");

  HQASSERT(! old_part_ctxt->within_element,
           "within element has been left TRUE") ;

  /* destroy all resource blocks at this level */
  while ((resblock = SLL_GET_HEAD(&(old_part_ctxt->resourceblock_stack), xpsResourceBlock, sll)) != NULL) {
    SLL_REMOVE_HEAD(&(old_part_ctxt->resourceblock_stack)) ;
    xps_resblock_destroy(&resblock) ;
  }
  HQASSERT(SLL_LIST_IS_EMPTY(&(old_part_ctxt->resourceblock_stack)),
           "resourceblock stack is not empty");

  HQASSERT(old_part_ctxt->relationships != NULL, "relationships is NULL") ;

  /* When the part is a relationships part, the relationships pointer
     will be indirect. */
  if (! old_part_ctxt->indirect_relationships)
    status = xps_destroy_relationship_block(&(old_part_ctxt->relationships)) ;

  mm_free(mm_xml_pool, old_part_ctxt, sizeof(struct xpsXmlPartContext)) ;
  *part_ctxt = NULL ;
  return status ;
}


void xps_partname_context_init(void)
{
  init_UID_cache() ;
  partname_next_uid = 0 ;
}

void xps_partname_context_finish(void)
{
  partname_next_uid = 0 ;
  finish_UID_cache() ;
}

void xps_partname_free(
      xps_partname_t **partname )
{
  HQASSERT(partname != NULL, "partname is NULL") ;
  HQASSERT(*partname != NULL, "partname pointer is NULL") ;
  HQASSERT((*partname)->uri != NULL, "partname pointer uri is NULL") ;

  hqn_uri_free(&((*partname)->uri)) ;
  mm_free(mm_xml_pool, *partname, sizeof(struct xps_partname_t)) ;
  *partname = NULL ;
}

Bool xps_partname_copy(
      xps_partname_t **to_partname,
      xps_partname_t *from_partname )
{
  xps_partname_t *new_partname = NULL ;

  HQASSERT(from_partname != NULL, "partname pointer is NULL") ;
  HQASSERT((from_partname)->uri != NULL, "partname pointer uri is NULL") ;
  HQASSERT(to_partname != NULL, "partname is NULL");

  *to_partname = NULL;

  new_partname = mm_alloc(mm_xml_pool, sizeof(struct xps_partname_t),
                          MM_ALLOC_CLASS_XPS_PARTNAME);

  if (!new_partname)
    return error_handler(VMERROR);

  if (!hqn_uri_copy(core_uri_context,
                    (from_partname)->uri, &(new_partname)->uri)) {
    mm_free(mm_xml_pool, new_partname, sizeof(struct xps_partname_t));
    return FALSE;
  }

  new_partname->norm_name = from_partname->norm_name;
  new_partname->mimetype = from_partname->mimetype;
  new_partname->uid = from_partname->uid;

  *to_partname = new_partname;

  return TRUE;
}


Bool xps_partname_new(
      xps_partname_t **partname,
      hqn_uri_t *base_uri,
      uint8 *name,
      uint32 name_len,
      uint32 type)
{
  xps_partname_t *new_partname = NULL ;
  hqn_uri_t *new_uri = NULL ;
  uint8 norm_name[LONGESTFILENAME] ;
  uint32 norm_length = LONGESTFILENAME ;
  uint8 *abs_name = NULL ;
  uint32 abs_name_len = 0 ;
  Bool result = TRUE ;

  HQASSERT(partname != NULL, "partname is NULL") ;
  HQASSERT(name != NULL, "name is NULL") ;
  HQASSERT(name_len > 0, "name_len is not greater than zero") ;

  *partname = NULL ;

  HQASSERT(type == XPS_NORMALISE_PARTNAME ||
           type == XPS_NORMALISE_PARTREFERENCE,
           "not building a canonical part name") ;

  /* In both cases, we need to canonicalise the part name reference
     and part name before resolving against the base. Note that the
     base of the content type stream is in effect the package, so we
     can turn part names in the content type stream in to absolute
     URI's. */
  if (result)
    result = xps_normalise_name(name, name_len,
                                norm_name, &norm_length,
                                type) ;

  /* Parse the normalised URI merging with the part's base to get the
     absolute URI which is our internal RIP canonical formart. For the
     content type stream, the base is the package. */
  if (result) {
    result = hqn_uri_rel_parse(core_uri_context, &new_uri, base_uri,
                               norm_name, norm_length) ;
    if (!result)
      (void) error_handler(TYPECHECK) ;
  }

  /* Get the path from the URI. */
  if (result) {
    result = hqn_uri_get_field(new_uri, &abs_name, &abs_name_len,
                               HQN_URI_PATH) ;
    if (!result)
      (void) error_handler(TYPECHECK) ;
  }

  if (result && abs_name_len > LONGESTFILENAME)
    result = error_handler(LIMITCHECK) ;

  if (result)
    result = xps_validate_partname_grammar(abs_name, abs_name_len, type) ;

  if (result && (new_partname = mm_alloc(mm_xml_pool,
                                         sizeof(struct xps_partname_t),
                                         MM_ALLOC_CLASS_XPS_PARTNAME)) == NULL) {
    (void) error_handler(VMERROR) ;
    result = FALSE ;
  }

  /* We intern the normalized name as this is what we use to do all
     our comaprisons on. */
  if (result)
    result = intern_create(&new_partname->norm_name, abs_name, abs_name_len) ;

  result = result && add_uid(new_partname->norm_name, &(new_partname->uid)) ;

  if (result) {
    new_partname->uri = new_uri ;
    *partname = new_partname ;
  } else { /* error cleanup */
    if (new_uri != NULL)
      hqn_uri_free(&new_uri) ;
    if (new_partname != NULL)
      mm_free(mm_xml_pool, new_partname, sizeof(struct xps_partname_t)) ;
  }
  /* UID cache will be cleaned up when XPS context is destroyed. */

  return result ;
}

void xps_extension_free(
      xps_extension_t **extension )
{
  HQASSERT(extension != NULL, "extension is NULL") ;
  HQASSERT(*extension != NULL, "extension pointer is NULL") ;

  mm_free(mm_xml_pool, *extension, sizeof(struct xps_extension_t)) ;
  *extension = NULL ;
}

Bool xps_extension_new(
      xmlGFilter *filter,
      xps_extension_t **extension,
      uint8 *name,
      uint32 name_len)
{
  xps_extension_t *new_extension ;
  xmlGIStr *extension_istr ;
  UTF8 normalised_units[LONGESTFILENAME] ;
  uint32 length = LONGESTFILENAME ;

  UNUSED_PARAM(xmlGFilter*, filter);

  HQASSERT(filter != NULL, "filter is NULL") ;
  HQASSERT(extension != NULL, "extension is NULL") ;
  HQASSERT(name != NULL, "name is NULL") ;
  HQASSERT(name_len > 0, "name_len is not greater than zero") ;

  *extension = NULL ;

  new_extension = mm_alloc(mm_xml_pool, sizeof(struct xps_extension_t),
                           MM_ALLOC_CLASS_XPS_EXTENSION) ;
  if (new_extension == NULL)
    return error_handler(VMERROR) ;

  if (! xps_normalise_name(name, name_len,
                           normalised_units, &length,
                           XPS_NORMALISE_EXTENSION)) {
    mm_free(mm_xml_pool, new_extension, sizeof(struct xps_extension_t)) ;
    return FALSE ;
  }

  /* Validate the syntax of the resulting extension.  The validate
     will raise the appropriate PS error. */
  if (! xps_validate_partname_grammar(normalised_units, length, XPS_NORMALISE_EXTENSION)) {
    mm_free(mm_xml_pool, new_extension, sizeof(struct xps_extension_t)) ;
    return FALSE ;
  }

  if (! intern_create(&extension_istr, normalised_units, length)) {
    mm_free(mm_xml_pool, new_extension, sizeof(struct xps_extension_t)) ;
    return FALSE ;
  }

  new_extension->extension = extension_istr ;

  *extension = new_extension ;

  return TRUE ;
}

static Bool xps_parse_xml_internal(
      xmlDocumentContext *xps_ctxt,
      xmlGFilterChain *filter_chain,
      xps_partname_t *partname,
      uint32 additional_filters,
      XMLG_VALID_CHILDREN *valid_children,
      Bool optional_part)
{
  xmlGFilter *new_filter ;
  xpsXmlPartContext *xmlpart_ctxt ;

  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  HQASSERT(partname != NULL, "partname is NULL") ;

  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt != NULL, "NULL xmlpart_ctxt") ;

  /* Override the parse error callback to be XPS specific. */
  xmlg_fc_set_parse_error_cb(filter_chain, xps_parse_error_cb) ;

  if (additional_filters & XPS_PART_VERSIONED) {
    if (! xps_versioning_filter_init(filter_chain, 3, &new_filter, xps_ctxt->supported_uris))
      return FALSE ;
  }

  if (additional_filters & XPS_LOWMEMORY) {
    if (! lowmem_pre_filter_init(filter_chain, 9, &new_filter))
      return FALSE ;
    if (! lowmem_post_filter_init(filter_chain, 11, &new_filter))
      return FALSE ;
  }

  /* Core properties is a special case. We ought to rename
     additional_filters to be filter list or something more
     meaningful. */
  if (additional_filters & XPS_CORE_PROPERTIES) {
    if (! xps_core_properties_filter_init(filter_chain, 10, &new_filter,
                                          valid_children, xps_ctxt))
      return FALSE ;
  } else {
    if (! xps_commit_filter_init(filter_chain, 5, &new_filter, xps_ctxt))
      return FALSE ;

    if (! xps_resource_filter_init(filter_chain, 7, &new_filter, xps_ctxt))
      return FALSE ;

    if (! xps_fixed_payload_filter_init(filter_chain, 10, &new_filter,
                                        valid_children, xps_ctxt))
      return FALSE ;
  }

  if (optional_part)
    return xml_parse_from_optional_uri(partname->uri, filter_chain, &xmlpart_ctxt->flptr) ;

  return xml_parse_from_uri(partname->uri, filter_chain, &xmlpart_ctxt->flptr) ;
}

static Bool check_rel_and_mimetype(
      xmlGFilter *filter,
      xps_partname_t *partname,
      xmlGIStr *required_relationship,
      XPS_CONTENT_TYPES *allowable_content_types,
      xmlGIStr **content_type)
{
  xmlGIStr *mimetype ;
  XPS_CONTENT_TYPES *curr_ct  = allowable_content_types ;
  Bool found ;
  uint8* part ;
  uint32 part_len ;
  xpsXmlPartContext *xmlpart_ctxt ;
  xmlGFilterChain *filter_chain ;
  xpsRelationship *relationship ;
  xps_partname_t *from_partname ;

  *content_type = NULL ;

  HQASSERT(filter != NULL, "NULL filter") ;
  filter_chain = xmlg_get_fc(filter) ;
  HQASSERT(filter_chain != NULL, "filter_chain is NULL") ;
  xmlpart_ctxt = xmlg_fc_get_user_data(filter_chain) ;
  HQASSERT(xmlpart_ctxt, "no xps xmlpart context") ;
  from_partname = xmlpart_ctxt->base.part_name ;
  HQASSERT(from_partname != NULL, "from_partname is NULL") ;

  if (! hqn_uri_get_field(partname->uri, &part, &part_len,
                          HQN_URI_PATH)) {
    HQFAIL("Unable to get URI from partname.") ;
    return error_handler(TYPECHECK) ;
  }

  /* Check that the target has a mimetype. */
  if (! xps_types_get_part_mimetype(filter, partname, &mimetype))
    return FALSE ;

  found = TRUE ;
  if (curr_ct != NULL) {
    found = FALSE ;
    while (curr_ct->content_type != NULL) {
      if (curr_ct->content_type == mimetype) {
        found = TRUE ;
        break ;
      }
      ++curr_ct ;
    }
  }
  if (! found)
    return detailf_error_handler(UNDEFINED,
                                 "Incorrect content type \"%.*s\" found for part %.*s.",
                                 intern_length(mimetype), intern_value(mimetype),
                                 part_len, part) ;

  *content_type = mimetype ;

  if (required_relationship != NULL) {
    if (! xps_lookup_relationship_target_type(xmlpart_ctxt->relationships,
                                              partname,
                                              required_relationship,
                                              &relationship, TRUE))
      return FALSE ;
    if (relationship == NULL)
      return detailf_error_handler(UNDEFINED,
                                   "Required relationship \"%.*s\" not found from part \"%.*s\" to part \"%.*s\".",
                                   intern_length(required_relationship),
                                   intern_value(required_relationship),
                                   intern_length(from_partname->norm_name),
                                   intern_value(from_partname->norm_name),
                                   part_len, part) ;
  }

  return TRUE ;
}

Bool xps_parse_xml_from_partname(
      xmlGFilter *filter,
      xps_partname_t *partname,
      uint32 relationships_to_process,
      uint32 additional_filters,
      XMLG_VALID_CHILDREN *valid_children,
      xmlGIStr *required_relationship,
      XPS_CONTENT_TYPES *allowable_content_types,
      xmlGIStr **content_type)
{
  xmlGFilterChain *new_filter_chain = NULL ;
  xmlDocumentContext *xps_ctxt ;
  xpsXmlPartContext *new_xmlpart_ctxt = NULL ;
  Bool status = FALSE ;

  HQASSERT(filter != NULL, "filter is NULL") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;

  HQASSERT(partname != NULL, "partname is NULL") ;
  HQASSERT(partname->uri != NULL, "partname uri is NULL") ;

  if (! check_rel_and_mimetype(filter, partname, required_relationship,
                               allowable_content_types, content_type))
    return FALSE ;

  if (! xps_xml_part_context_new(xps_ctxt, partname, NULL, relationships_to_process, TRUE,
                                 &new_xmlpart_ctxt))
    return FALSE ;

  /* filter chains carry the part context as their user data */
  if (! xmlg_fc_new(core_xml_subsystem,
                    &new_filter_chain,
                    &xmlexec_memory_handlers,
                    partname->uri,    /* stream uri */
                    partname->uri,    /* base uri */
                    new_xmlpart_ctxt)) {
    (void) error_handler(UNDEFINED);
    goto cleanup ;
  }

  status = xps_parse_xml_internal(xps_ctxt, new_filter_chain, partname,
                                  additional_filters, valid_children, FALSE) ;

cleanup:
  if (new_filter_chain != NULL)
    xmlg_fc_destroy(&new_filter_chain) ;

  /* Be careful that the function is before the && so it always gets
     executed. */
  if (new_xmlpart_ctxt != NULL)
    status = xps_xml_part_context_free(&new_xmlpart_ctxt) && status ;

  return status ;
}

Bool xps_open_file_from_partname(
      xmlGFilter *filter,
      xps_partname_t *partname,
      OBJECT *ofile,
      /*@in@*/ /*@null@*/
      xmlGIStr *required_relationship,
      /*@in@*/ /*@null@*/
      XPS_CONTENT_TYPES *allowable_content_types,
      /*@out@*/ /*@notnull@*/
      xmlGIStr **content_type,
      Bool implicit_close_file)
{
  xmlDocumentContext *xps_ctxt ;

  HQASSERT(filter != NULL, "filter is NULL") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;

  HQASSERT(partname != NULL, "partname is NULL") ;
  HQASSERT(partname->uri != NULL, "partname uri is NULL") ;

  if (! check_rel_and_mimetype(filter, partname, required_relationship,
                               allowable_content_types, content_type))
    return FALSE ;

  return open_file_from_psdev_uri(partname->uri, ofile, implicit_close_file) ;
}

Bool xps_ps_filename_from_partname(
      xmlGFilter *filter,
      xps_partname_t *partname,
      uint8 **ps_filename,
      uint32 *ps_filename_len,
      /*@in@*/ /*@null@*/
      xmlGIStr *required_relationship,
      /*@in@*/ /*@null@*/
      XPS_CONTENT_TYPES *allowable_content_types,
      /*@out@*/ /*@notnull@*/
      xmlGIStr **content_type)
{
  xmlDocumentContext *xps_ctxt ;

  HQASSERT(filter != NULL, "filter is NULL") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;

  HQASSERT(partname != NULL, "partname is NULL") ;
  HQASSERT(partname->uri != NULL, "partname uri is NULL") ;

  if (! check_rel_and_mimetype(filter, partname, required_relationship,
                               allowable_content_types, content_type))
    return FALSE ;

  if (! psdev_uri_to_ps_filename(partname->uri, ps_filename, ps_filename_len))
    return FALSE ;

  if (*ps_filename_len > LONGESTFILENAME) {
    mm_free_with_header(mm_xml_pool, *ps_filename) ;
    return error_handler(LIMITCHECK) ;
  }

  return TRUE ;
}

Bool xps_have_processed_part(
      xmlGFilter *filter,
      xps_partname_t *partname)
{
  xmlDocumentContext *xps_ctxt ;

  HQASSERT(filter != NULL, "filter is NULL") ;
  HQASSERT(partname != NULL, "partname is NULL") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;

  HQASSERT(partname->norm_name != NULL, "normalised name is NULL") ;

  return xps_is_processed_partname(xps_ctxt, partname->norm_name) ;
}

Bool xps_mark_part_as_processed(
      xmlGFilter *filter,
      xps_partname_t *partname)
{
  xmlDocumentContext *xps_ctxt ;

  HQASSERT(filter != NULL, "filter is NULL") ;
  HQASSERT(partname != NULL, "partname is NULL") ;
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL") ;

  HQASSERT(partname->norm_name != NULL, "normalised name is NULL") ;

  return xps_add_processed_partname(xps_ctxt, partname->norm_name) ;
}

/* PS operator to take a part reference URI and return an absolute
   filename. */
Bool xpsurifilename_(ps_context_t *pscontext)
{
  xmlDocumentContext *xps_ctxt;
  OBJECT *theo, outobj ;
  utf8_buffer uriname ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* Stack contains filter chain id. The filter chain is found and
     then XML from the cache executed on it. */
  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW );

  theo = theTop(operandstack);
  switch ( oType(*theo) ) {
  case OSTRING:
    uriname.codeunits = oString(*theo) ;
    uriname.unitlength = theLen(*theo) ;
    break ;
  case ONAME:
    uriname.codeunits = theICList(oName(*theo)) ;
    uriname.unitlength = theINLen(oName(*theo)) ;
    break ;
  default:
    return error_handler(TYPECHECK) ;
  }
  npop(1, &operandstack) ;

  xps_ctxt = xml_get_current_doc_context() ;

  if (xps_ctxt == NULL || xps_ctxt->package_uri == NULL)
    return error_handler(INVALIDACCESS) ;

  {
    xps_partname_t *partname ;
    uint8 *ps_filename ;
    uint32 ps_filename_len ;
    int32 result ;

    if (! xps_partname_new(&partname,
                           xps_ctxt->package_uri,
                           uriname.codeunits,
                           uriname.unitlength,
                           XPS_NORMALISE_PARTREFERENCE))
      return error_handler(UNDEFINED) ;


    if (! psdev_uri_to_ps_filename(partname->uri, &ps_filename, &ps_filename_len)) {
      xps_partname_free(&partname) ;
      return error_handler(RANGECHECK) ;
    }

    if (ps_filename_len > LONGESTFILENAME) {
      xps_partname_free(&partname) ;
      mm_free_with_header(mm_xml_pool, ps_filename) ;
      return error_handler(LIMITCHECK) ;
    }

    result = ps_string(&outobj, ps_filename, ps_filename_len) ;
    if (! result)
      return result ;

    mm_free_with_header(mm_xml_pool, ps_filename) ;
    xps_partname_free(&partname) ;
  }

  if ( ! push( &outobj , & operandstack ))
    return error_handler( VMERROR );

  return TRUE ;
}

void init_C_globals_parts(void)
{
  partname_next_uid = 0 ;
  HqMemSetPtr(UID_cache.table, NULL, UID_CACHE_TABLE_SIZE) ;
}

/* ============================================================================
* Log stripped */
