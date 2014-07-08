/* ============================================================================
 * $HopeName: HQNuri!src:hqnuri-3986.c(EBDSDK_P.1) $
 * $Id: src:hqnuri-3986.c,v 1.11.2.1.1.1 2013/12/19 11:24:42 anon Exp $
 *
 * Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * -------------------------------------
 * URI grammar, extracted from RFC 3986 appendix A. Character BNF excluded.
 *
 * Appendix A.  Collected ABNF for URI
 *
 *    URI           = scheme ":" hier-part [ "?" query ] [ "#" fragment ]
 *
 *    hier-part     = "//" authority path-abempty
 *                  / path-absolute
 *                  / path-rootless
 *                  / path-empty
 *
 *    URI-reference = URI / relative-ref
 *
 *    absolute-URI  = scheme ":" hier-part [ "?" query ]
 *
 *    relative-ref  = relative-part [ "?" query ] [ "#" fragment ]
 *
 *    relative-part = "//" authority path-abempty
 *                  / path-absolute
 *                  / path-noscheme
 *                  / path-empty
 *
 *    scheme        = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." )
 *
 *    authority     = [ userinfo "@" ] host [ ":" port ]
 *    userinfo      = *( unreserved / pct-encoded / sub-delims / ":" )
 *    host          = IP-literal / IPv4address / reg-name
 *    port          = *DIGIT
 *
 *    IP-literal    = "[" ( IPv6address / IPvFuture  ) "]"
 *
 *    IPvFuture     = "v" 1*HEXDIG "." 1*( unreserved / sub-delims / ":" )
 *
 *    IPv6address   =                            6( h16 ":" ) ls32
 *                  /                       "::" 5( h16 ":" ) ls32
 *                  / [               h16 ] "::" 4( h16 ":" ) ls32
 *                  / [ *1( h16 ":" ) h16 ] "::" 3( h16 ":" ) ls32
 *                  / [ *2( h16 ":" ) h16 ] "::" 2( h16 ":" ) ls32
 *                  / [ *3( h16 ":" ) h16 ] "::"    h16 ":"   ls32
 *                  / [ *4( h16 ":" ) h16 ] "::"              ls32
 *                  / [ *5( h16 ":" ) h16 ] "::"              h16
 *                  / [ *6( h16 ":" ) h16 ] "::"
 *
 *    h16           = 1*4HEXDIG
 *    ls32          = ( h16 ":" h16 ) / IPv4address
 *    IPv4address   = dec-octet "." dec-octet "." dec-octet "." dec-octet
 *
 *    dec-octet     = DIGIT                 ; 0-9
 *                  / %x31-39 DIGIT         ; 10-99
 *                  / "1" 2DIGIT            ; 100-199
 *                  / "2" %x30-34 DIGIT     ; 200-249
 *                  / "25" %x30-35          ; 250-255
 *
 *    reg-name      = *( unreserved / pct-encoded / sub-delims )
 *
 *    path          = path-abempty    ; begins with "/" or is empty
 *                  / path-absolute   ; begins with "/" but not "//"
 *                  / path-noscheme   ; begins with a non-colon segment
 *                  / path-rootless   ; begins with a segment
 *                  / path-empty      ; zero characters
 *
 *    path-abempty  = *( "/" segment )
 *    path-absolute = "/" [ segment-nz *( "/" segment ) ]
 *    path-noscheme = segment-nz-nc *( "/" segment )
 *    path-rootless = segment-nz *( "/" segment )
 *    path-empty    = 0<pchar>
 *
 *    segment       = *pchar
 *    segment-nz    = 1*pchar
 *    segment-nz-nc = 1*( unreserved / pct-encoded / sub-delims / "@" )
 *                  ; non-zero-length segment without any colon ":"
 *
 *    pchar         = unreserved / pct-encoded / sub-delims / ":" / "@"
 *
 *    query         = *( pchar / "/" / "?" )
 *
 *    fragment      = *( pchar / "/" / "?" )
 *
 *    pct-encoded   = "%" HEXDIG HEXDIG
 *
 *    unreserved    = ALPHA / DIGIT / "-" / "." / "_" / "~"
 *    reserved      = gen-delims / sub-delims
 *    gen-delims    = ":" / "/" / "?" / "#" / "[" / "]" / "@"
 *    sub-delims    = "!" / "$" / "&" / "'" / "(" / ")"
 *                  / "*" / "+" / "," / ";" / "="
 *
 * Function prefixs:
 *  uri_       are local to this file.
 *  uri_scan_  are local scan functions which ought to match the above ABNF
 *             grammar.
 *  hqn_uri_   are public functions.
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Implementation of HQN 3986 URI interface.
 */

#include "hqnuri.h"
#include "std.h"       /* For HQASSERT, HQFAIL */
#include "hqmemcmp.h"  /* HqMemCmp */
#include "hqmemcpy.h"  /* HqMemCpy */

#ifdef METRO
static uint8* xps_content_typestream_partname = (uint8 *)"/[Content_Types].xml" ;
static uint32 xps_content_typestream_partname_len  = 20u ;
#endif

#define DEBUG_RFC3986

/* Macros to differentiate various character types. Extracted from RFC
   3986 and 4234. */
#define IS_ALPHA(x) (IS_LOWALPHA(x) || IS_UPALPHA(x))

#define IS_LOWALPHA(x) ((x) >= 'a' && (x) <= 'z')

#define IS_UPALPHA(x) ((x) >= 'A' && (x) <= 'Z')

#define IS_DIGIT(x) ((x) >= '0' && (x) <= '9')

#define IS_ALPHANUM(x) (IS_ALPHA(x) || IS_DIGIT(x))

#define IS_HEXDIG(x) (IS_DIGIT(x) || ((x) >= 'A' && (x) <= 'F'))

#define IS_HEX(x) (IS_HEXDIG(x) || ((x) >= 'a' && (x) <= 'f'))

#define IS_GEN_DELIMS(x) ((x) == ':' || (x) == '/' || (x) == '?' || \
                          (x) == '#' || (x) == '[' || (x) == ']' || \
                          (x) == '@')

#define IS_SUB_DELIMS(x) ((x) == '!'  || (x) == '$' || (x) == '&' || \
                          (x) == '\'' || (x) == '(' || (x) == ')' || \
                          (x) == '*'  || (x) == '+' || (x) == ',' || \
                          (x) == ';'  || (x) == '=')

#define IS_RESERVED(x) (IS_GEN_DELIMS(x) || IS_SUB_DELIMS(x))

#define IS_UNRESERVED(x) (IS_ALPHANUM(x) || (x) == '-' || (x) == '.' || \
                          (x) == '_' || (x) == '~')

#define IS_SCHEME(x) (IS_ALPHANUM(x) || (x) == '+' || (x) == '-' || (x) == '.')

#define IS_PCHAR(p, len) ((len > 0) && \
                          (IS_UNRESERVED(*(p)) || IS_ESCAPED(p, len) || \
                           IS_SUB_DELIMS(*(p)) || *(p) == ':' || *(p) == '@'))

#define IS_ESCAPED(p, len) ((len > 2) && (*(p) == '%') && IS_HEX((p)[1]) && \
                                          IS_HEX((p)[2]))

/* Skip to next pointer char, handle escaped sequences. */
#define NEXT(p, len) \
  (((len) > 2 && *(p) == '%') ? ((p) += 3, (len) -= 3 )  : ((p)++, (len)-- ))

#define IS_REG_NAME(p, len) ((len > 0) && (IS_UNRESERVED(*(p)) || \
  IS_ESCAPED(p, len) ||  IS_SUB_DELIMS(*(p))))

#define IS_USERINFO(p, len) ((len > 0) && (IS_UNRESERVED(*(p)) || \
  IS_ESCAPED(p, len) || IS_SUB_DELIMS(*(p)) || (*(p) == ':')))

struct hqn_uri_context_t {
  hqn_uri_memhandler_t mem ;
} ;

struct hqn_uri_t {
  hqn_uri_context_t *uri_context ;
  HqBool copied ;        /**< have we copied the original string? */
  uint8 *original ;      /**< the original string - unchanged */
  uint32 original_len ;
  /* Where possible, these point into the original string */
  uint8 *scheme ;        /**< the URI scheme */
  uint32 scheme_len ;
  uint8 *authority ;     /**< the authority part */
  uint32 authority_len ;
  uint8 *host ;        /**< the host part */
  uint32 host_len ;
  uint8 *user ;          /**< the user part */
  uint32 user_len ;
  uint8* port ;          /**< the port number */
  uint32 port_len ;
  uint8 *path ;          /**< the path string */
  uint32 path_len ;
  uint8 *query ;         /**< the query string */
  uint32 query_len ;
  uint8 *fragment ;      /**< the fragment identifier */
  uint32 fragment_len ;
} ;

/* ============================================================================
 * Private functions to aid in the parse process.
 */
#if defined(DEBUG_BUILD)
static HqBool hqn_uri_do_tests(
      hqn_uri_context_t *uri_context) ;
#endif

static void uri_clean(
      hqn_uri_t *uri_object)
{
  hqn_uri_context_t *uri_context ;
  HqBool copied ;
  uint32 original_len ;
  uint8 *original ;

  HQASSERT(uri_object != NULL, "uri_onject is NULL") ;
  uri_context = uri_object->uri_context ;
  HQASSERT(uri_context != NULL, "uri_context is NULL") ;

  copied = uri_object->copied ;
  original = uri_object->original ;
  original_len = uri_object->original_len ;

  (void)memset(uri_object, 0, sizeof(struct hqn_uri_t));

  /* reset what we want to keep */
  uri_object->original = original ;
  uri_object->original_len = original_len ;
  uri_object->copied = copied ;
  uri_object->uri_context = uri_context ;
}

/*    h16           = 1*4HEXDIG
 */
static HqBool uri_scan_h16(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s ;
  uint32 l, i ;

  UNUSED_PARAM(hqn_uri_t*, uri_object);

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  if (l == 0)
    return FALSE ;

  if (! IS_HEXDIG(*s))
    return FALSE ;

  l-- ;
  s++ ;

  for (i=0; i<3; i++) {
    if (l == 0 || ! IS_HEXDIG(*s))
      break ;
    l-- ;
    s++ ;
  }

  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/*    dec-octet     = DIGIT                 ; 0-9
 *                  / %x31-39 DIGIT         ; 10-99
 *                  / "1" 2DIGIT            ; 100-199
 *                  / "2" %x30-34 DIGIT     ; 200-249
 *                  / "25" %x30-35          ; 250-255
 */
static HqBool uri_scan_dec_octet(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s ;
  uint32 l ;
  int32 v ;

  UNUSED_PARAM(hqn_uri_t*, uri_object);

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  /* Cheat slightly because I know the intention of the scan. */
  if (l == 0)
    return FALSE ;

  if (! IS_DIGIT(*s))
    return FALSE ;

  v = *s - '0' ;
  l-- ;
  s++ ;

  if (l > 0 && IS_DIGIT(*s)) {
    v = v * 10 ;
    v = v + (*s - '0') ;
    l-- ;
    s++ ;

    if (l > 0 && IS_DIGIT(*s)) {
      v = v * 10 ;
      v = v + (*s - '0') ;
      l-- ;
      s++ ;
    }
  }

  if (v > 255)
    return FALSE ;

  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/*    IPv4address   = dec-octet "." dec-octet "." dec-octet "." dec-octet
 */
static HqBool uri_scan_ipv4_address(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s ;
  uint32 l ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  if (! uri_scan_dec_octet(uri_object, &s, &l))
    return FALSE ;

  if (*s != '.')
    return FALSE ;
  s++ ;
  l-- ;

  if (! uri_scan_dec_octet(uri_object, &s, &l))
    return FALSE ;

  if (*s != '.')
    return FALSE ;
  s++ ;
  l-- ;

  if (! uri_scan_dec_octet(uri_object, &s, &l))
    return FALSE ;

  if (*s != '.')
    return FALSE ;
  s++ ;
  l-- ;

  if (! uri_scan_dec_octet(uri_object, &s, &l))
    return FALSE ;

  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/*    ls32          = ( h16 ":" h16 ) / IPv4address
 */
static HqBool uri_scan_ls32(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s ;
  uint32 l ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  if (uri_scan_h16(uri_object, &s, &l)) {
    if (l > 0 && *s == ':') {
      l-- ;
      s++ ;

      if (uri_scan_h16(uri_object, &s, &l)) {
        *str = s ;
        *str_len = l ;
        return TRUE ;
      } else {
        return FALSE ;
      }
    }
    /* If we don't see a :, it could be the first numeric in an IP v4
       address, so we can't cause an error yet! */
  }

  /* reset search for ipv4 address */
  s = *str ;
  l = *str_len ;

  if (! uri_scan_ipv4_address(uri_object, &s, &l))
    return FALSE ;

  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/*    path-abempty  = *( "/" segment )
 */
static HqBool uri_scan_path_abempty(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s ;
  uint32 l ;

  UNUSED_PARAM(hqn_uri_t*, uri_object);

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  if (l == 0)
    return TRUE ;

  if (*s != '/')
    return FALSE ;

  while (l > 0 && *s == '/') {
    l-- ; /* skip / */
    s++ ;

    /* segment = *pchar */
    while (l > 0 && IS_PCHAR(s, l))
      NEXT(s, l) ;
  }

  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/*    segment     = *pchar
 */
static HqBool uri_scan_segment(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s ;
  uint32 l ;

  UNUSED_PARAM(hqn_uri_t*, uri_object);

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  while (l > 0 && IS_PCHAR(s, l))
    NEXT(s, l) ;

  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/*    segment-nz    = 1*pchar
 */
static HqBool uri_scan_segment_nz(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  HqBool found = FALSE ;
  uint8 *s ;
  uint32 l ;

  UNUSED_PARAM(hqn_uri_t*, uri_object);

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  while (l > 0 && IS_PCHAR(s, l)) {
    NEXT(s, l) ;
    found = TRUE ;
  }

  *str = s ;
  *str_len = l ;
  return found ;
}

/*    segment-nz-nc = 1*( unreserved / pct-encoded / sub-delims / "@" )
 *                  ; non-zero-length segment without any colon ":"
 */
static HqBool uri_scan_segment_nz_nc(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  HqBool found = FALSE ;
  uint8 *s ;
  uint32 l ;

  UNUSED_PARAM(hqn_uri_t*, uri_object);

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  while (l > 0 && IS_PCHAR(s, l)) {
    if (*s == ':') /* no colon allowed */
      return FALSE ;
    NEXT(s, l) ;
    found = TRUE ;
  }

  *str = s ;
  *str_len = l ;
  return found ;
}

/* scheme = alpha *( alpha | digit | "+" | "-" | "." )
 */
static HqBool uri_scan_scheme(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s, *scheme ;
  uint32 l, scheme_len = 0 ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  /* skip white space */
  while (l > 0 && *s == ' ') {
    l-- ;
    s++ ;
  }

  if (l == 0)
    return FALSE ;

  scheme = s ;

  /* must have at least one char */
  if (l > 0 && IS_ALPHA(*s)) {
    l-- ;
    s++ ;
    scheme_len++ ;
  } else {
    return FALSE ;
  }

  /* scan potential scheme */
  while (l > 0 && IS_SCHEME(*s)) {
    l-- ;
    s++ ;
    scheme_len++ ;
  }

  /* if data remains and next is a colon - it really is scheme */
  if (l > 0 && *s == ':') {
    l-- ;
    s++ ;
    uri_object->scheme = scheme ;
    uri_object->scheme_len = scheme_len ;
  } else {
    return FALSE ;
  }

  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/*    query         = *( pchar / "/" / "?" )
 */
static HqBool uri_scan_query(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s, *query ;
  uint32 l, query_len = 0 ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  if (l == 0)
    return TRUE ;

  query = s ;

  while (l > 0 && (IS_PCHAR(s, l) || *s == '/' || *s == '?'))
    NEXT(s, l) ;

  query_len = CAST_PTRDIFFT_TO_UINT32(s - query) ;

  uri_object->query = query ;
  uri_object->query_len = query_len ;
  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/*    userinfo      = *( unreserved / pct-encoded / sub-delims / ":" )
 */
static HqBool uri_scan_user_info(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s, *userinfo ;
  uint32 l, userinfo_len = 0 ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  userinfo = s ;

  /* is there a userinfo? */
  while (IS_USERINFO(s, l))
    NEXT(s, l) ;

  if (l > 0 && *s == '@') {
    userinfo_len = CAST_PTRDIFFT_TO_UINT32(s - userinfo) ;
    s++ ;
    l-- ;
  } else {
    return FALSE ;
  }

  uri_object->user = userinfo ;
  uri_object->user_len = userinfo_len ;
  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/* parse:
   [ *<number>( h16 ":" ) h16 ] "::"
   from IP v6 BNF */
static HqBool uri_scan_ipv6_pre_seg(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len,
      uint32 number)
{
  uint8 *s ;
  uint32 l ;
  uint32 i = number - 1 ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;
  HQASSERT(number < 8, "number is not less than 8") ;

  /* Read h16's */
  if (uri_scan_h16(uri_object, &s, &l)) {
    for (i=0; i < number - 1; i++) { /* one less than allowed */
      if (l == 0 || *s != ':') {
        break ;
      } else {
        l-- ; /* consume :, could be first from a :: */
        s++ ;
      }
      if (! uri_scan_h16(uri_object, &s, &l)) {
        break ;
      }
    }
  }

  /* We scanned up to the final h16 allowed which means we must have a
     :: next. Includes case where number == 0 */
  if (i == number - 1) {
    if (l < 1 || *s != ':')
      return FALSE ;

    l-- ; /* consume : */
    s++ ;
  }

  if (l < 1 || *s != ':')
    return FALSE ;

  l-- ; /* consume : */
  s++ ;

  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/* parse:
   <number>( h16 ":" ) ls32
   from IP v6 BNF */
static HqBool uri_scan_ipv6_post_seg(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len,
      uint32 number)
{
  uint8 *s ;
  uint32 l ;
  uint32 i ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;
  HQASSERT(number < 7, "number is not less than 7") ;

  for (i=0; i<number; i++) {
    if (uri_scan_h16(uri_object, &s, &l)) {
      if (l > 0 && *s == ':') {
        l-- ;
        s++ ;
      } else {
        return FALSE ;
      }
    } else {
      return FALSE ;
    }
  }

  if (! uri_scan_ls32(uri_object, &s, &l))
    return FALSE ;

  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/*    IPv6address   =                            6( h16 ":" ) ls32
 *                  /                       "::" 5( h16 ":" ) ls32
 *                  / [               h16 ] "::" 4( h16 ":" ) ls32
 *                  / [ *1( h16 ":" ) h16 ] "::" 3( h16 ":" ) ls32
 *                  / [ *2( h16 ":" ) h16 ] "::" 2( h16 ":" ) ls32
 *                  / [ *3( h16 ":" ) h16 ] "::"    h16 ":"   ls32
 *                  / [ *4( h16 ":" ) h16 ] "::"              ls32
 *                  / [ *5( h16 ":" ) h16 ] "::"              h16
 *                  / [ *6( h16 ":" ) h16 ] "::"
 */
static HqBool uri_scan_ipv6_address(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s ;
  uint32 l ;
  HqBool status ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  /* Its done this way rather than one large if statement to make
     debugging tolerable. */
  status = uri_scan_ipv6_post_seg(uri_object, &s, &l, 6) ;       /* 6( h16 ":" ) ls32 */
  if (! status) {
    s = *str ;
    l = *str_len ;
    status = ( uri_scan_ipv6_pre_seg(uri_object, &s, &l, 0) &&   /* "::" 5( h16 ":" ) ls32 */
               uri_scan_ipv6_post_seg(uri_object, &s, &l, 5) ) ;
  }
  if (! status) {
    s = *str ;
    l = *str_len ;
    status = ( uri_scan_ipv6_pre_seg(uri_object, &s, &l, 1) &&   /* [     h16 ] "::" 4( h16 ":" ) ls32 */
               uri_scan_ipv6_post_seg(uri_object, &s, &l, 4) ) ;
  }
  if (! status) {
    s = *str ;
    l = *str_len ;
    status = ( uri_scan_ipv6_pre_seg(uri_object, &s, &l, 2) &&   /* [ *1( h16 ":" ) h16 ] "::" 3( h16 ":" ) ls32 */
               uri_scan_ipv6_post_seg(uri_object, &s, &l, 3) ) ;
  }
  if (! status) {
    s = *str ;
    l = *str_len ;
    status = ( uri_scan_ipv6_pre_seg(uri_object, &s, &l, 3) &&   /* [ *2( h16 ":" ) h16 ] "::" 2( h16 ":" ) ls32 */
               uri_scan_ipv6_post_seg(uri_object, &s, &l, 2) ) ;
  }
  if (! status) {
    s = *str ;
    l = *str_len ;
    status = ( uri_scan_ipv6_pre_seg(uri_object, &s, &l, 4) &&   /* [ *3( h16 ":" ) h16 ] "::"    h16 ":"   ls32 */
               uri_scan_ipv6_post_seg(uri_object, &s, &l, 1) ) ;
  }
  if (! status) {
    s = *str ;
    l = *str_len ;
    status = ( uri_scan_ipv6_pre_seg(uri_object, &s, &l, 5) &&   /* [ *4( h16 ":" ) h16 ] "::"              ls32 */
               uri_scan_ipv6_post_seg(uri_object, &s, &l, 0) ) ;
  }
  if (! status) {
    s = *str ;
    l = *str_len ;
    status = ( uri_scan_ipv6_pre_seg(uri_object, &s, &l, 6) &&   /* [ *5( h16 ":" ) h16 ] "::"              h16 */
               uri_scan_h16(uri_object, &s, &l) ) ;
  }
  if (! status) {
    s = *str ;
    l = *str_len ;
    status = uri_scan_ipv6_pre_seg(uri_object, &s, &l, 7) ;      /* [ *6( h16 ":" ) h16 ] "::" */
  }

  if (! status)
    return FALSE ;

  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/*    IPvFuture     = "v" 1*HEXDIG "." 1*( unreserved / sub-delims / ":" )
 */
static HqBool uri_scan_ipv_future(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s ;
  uint32 l ;

  UNUSED_PARAM(hqn_uri_t*, uri_object);

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  if (l == 0)
    return FALSE ;

  /* RFC 3986 states v is case insensitive. */
  if (*s != 'v' && *s != 'V')
    return FALSE ;

  s++ ; /* consume v */
  l-- ;

  if (l == 0)
    return FALSE ;

  if (! IS_HEXDIG(*s))
    return FALSE ;

  s++ ; /* consume single hex digit */
  l-- ;

  while (l > 0 && IS_HEXDIG(*s)) {
    s++ ; /* consume single hex digit */
    l-- ;
  }

  if (l == 0)
    return FALSE ;

  if (*s != '.')
    return FALSE ;

  s++ ; /* consume . */
  l-- ;

  if (l == 0)
    return FALSE ;

  if (! (IS_UNRESERVED(*s) || IS_SUB_DELIMS(*s) || *s == ':'))
    return FALSE ;

  s++ ; /* consume */
  l-- ;

  while (l > 0 && (IS_UNRESERVED(*s) || IS_SUB_DELIMS(*s) || *s == ':')) {
    s++ ; /* consume */
    l-- ;
  }

  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/*    IP-literal    = "[" ( IPv6address / IPvFuture  ) "]"
 */
static HqBool uri_scan_ip_literal(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s ;
  uint32 l ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  if (l > 0 && *s == '[') {
    l-- ; /* consume [ */
    s++ ;

    if (! uri_scan_ipv6_address(uri_object, &s, &l) &&
        ! uri_scan_ipv_future(uri_object, &s, &l))
      return FALSE ;

    if (l == 0 || *s != ']')
      return FALSE ;

    l-- ; /* consume ] */
    s++ ;
  } else {
    return FALSE ;
  }

  *str = s ;
  *str_len = l ;
  return TRUE ;
}


/*    reg-name      = *( unreserved / pct-encoded / sub-delims )
 */
static void uri_scan_reg_name(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s ;
  uint32 l ;

  UNUSED_PARAM(hqn_uri_t*, uri_object);

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  /* is there a reg name? */
  while (IS_REG_NAME(s, l))
    NEXT(s, l) ;

  *str = s ;
  *str_len = l ;
}

/*    host          = IP-literal / IPv4address / reg-name
 */
static void uri_scan_host(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s, *host ;
  uint32 l, host_len ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  host = s ;

  /* From RFC3986:
   *
   * The syntax rule for host is ambiguous because it does not completely
   * distinguish between an IPv4address and a reg-name.  In order to
   * disambiguate the syntax, we apply the "first-match-wins" algorithm:
   */
  if ( !uri_scan_ip_literal(uri_object, &s, &l) )
    if ( !uri_scan_ipv4_address(uri_object, &s, &l) )
      uri_scan_reg_name(uri_object, &s, &l);

  host_len = CAST_PTRDIFFT_TO_UINT32(s - host) ;

  uri_object->host = host ;
  uri_object->host_len = host_len ;
  *str = s ;
  *str_len = l ;
}

/*    port          = *DIGIT
 */
static void uri_scan_port(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s, *port ;
  uint32 l, port_len ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  port =  s ;

  while(l > 0 && IS_DIGIT(*s)) {
    s++ ;
    l-- ;
  }

  port_len = CAST_PTRDIFFT_TO_UINT32(s - port) ;

  uri_object->port = port ;
  uri_object->port_len = port_len ;
  *str = s ;
  *str_len = l ;
}

/* authority     = [ userinfo "@" ] host [ ":" port ]
 */
static void uri_scan_authority(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s, *authority ;
  uint32 l, authority_len = 0 ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  authority = s ;

  /* userinfo is optional */
  (void)uri_scan_user_info(uri_object, &s, &l) ;

  uri_scan_host(uri_object, &s, &l);

  if (l > 0 && *s == ':') {
    --l ; /* consume : */
    ++s ;
    uri_scan_port(uri_object, &s, &l);
  }

  authority_len = CAST_PTRDIFFT_TO_UINT32(s - authority) ;

  uri_object->authority = authority ;
  uri_object->authority_len = authority_len ;
  *str = s ;
  *str_len = l ;
}

/*    hier-part     = "//" authority path-abempty
 *                  / path-absolute
 *                  / path-rootless
 *                  / path-empty
 *
 *    path-empty    = 0<pchar>
 *    path-rootless = segment-nz *( "/" segment )
 *    path-absolute = "/" [ segment-nz *( "/" segment ) ]
 */
static HqBool uri_scan_hier_part(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s, *path = NULL ;
  uint32 l, path_len = 0 ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  if (l > 0 && *s == '/') {
    s++ ; /* consume / */
    l-- ;

    if (l > 0 && *s == '/') { /* "//" authority path-abempty */
      s++ ; /* consume / */
      l-- ;

      uri_scan_authority(uri_object, &s, &l);

      path = s ;

#ifdef METRO
      /* Special case for Metro content type stream. */
      if (l == xps_content_typestream_partname_len &&
          HqMemCmp(xps_content_typestream_partname,
                   xps_content_typestream_partname_len, s, l) == 0) {
        s+= xps_content_typestream_partname_len ;
        l-= xps_content_typestream_partname_len ;

        path_len = CAST_PTRDIFFT_TO_UINT32(s - path) ;
        *str = s ;
        *str_len = l ;
        uri_object->path = path ;
        uri_object->path_len = path_len ;
        return TRUE ;
      }
#endif

      if (! uri_scan_path_abempty(uri_object, &s, &l))
        return FALSE ;

      path_len = CAST_PTRDIFFT_TO_UINT32(s - path) ;

    } else { /* path-absolute = "/" [ segment-nz *( "/" segment ) ] */
      if (l == 0) { /* We are done. */
        *str = s ;
        *str_len = l ;
        return TRUE ;
      }

      path = s ;

      if (! uri_scan_segment_nz(uri_object, &s, &l))
        return FALSE ;

      while (l > 0 && *s == '/') {
        l-- ; /* consume / */
        s++ ;
        if (! uri_scan_segment(uri_object, &s, &l))
          return FALSE ;
      }

      path_len = CAST_PTRDIFFT_TO_UINT32(s - path) ;

    }

  } else { /* path-rootless = segment-nz *( "/" segment ) */

    path = s ;

    if (! uri_scan_segment_nz(uri_object, &s, &l)) {

      /* Unable to parse a segment_nz so the last alternative is an
         empty path. */
      if (IS_PCHAR(s, l)) { /* path-empty    = 0<pchar> */
        return FALSE ;
      } else {
        return TRUE ; /* we are done - no <pchar> */
      }
    }

    while (l > 0 && *s == '/') {
      l-- ; /* consume / */
      s++ ;
      if (! uri_scan_segment(uri_object, &s, &l))
        return FALSE ;
    }

    path_len = CAST_PTRDIFFT_TO_UINT32(s - path) ;
  }

  uri_object->path = path ;
  uri_object->path_len = path_len ;
  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/*    fragment      = *( pchar / "/" / "?" )
 */
static HqBool uri_scan_fragment(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s, *fragment ;
  uint32 l, fragment_len = 0 ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  if (l == 0)
    return TRUE ;

  fragment = s ;

  while (l > 0 && (IS_PCHAR(s, l) || *s == '/' || *s == '?'))
    NEXT(s, l) ;

  fragment_len = CAST_PTRDIFFT_TO_UINT32(s - fragment) ;

  uri_object->fragment = fragment ;
  uri_object->fragment_len = fragment_len ;

  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/* absolute-URI  = scheme ":" hier-part [ "?" query ]
 */
static HqBool uri_scan_absolute(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s ;
  uint32 l ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  if (l == 0)
    return FALSE ;

  /* scheme */
  if (! uri_scan_scheme(uri_object, &s, &l))
    return FALSE ;

  /* hier */
  if (! uri_scan_hier_part(uri_object, &s, &l))
    return FALSE ;

  if (l > 0 && *s == '?') {
    ++s ;
    --l ;

    if (! uri_scan_query(uri_object, &s, &l))
      return FALSE ;
  }

  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/*    URI           = scheme ":" hier-part [ "?" query ] [ "#" fragment ]
 */
static HqBool uri_scan_uri(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s ;
  uint32 l ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  if (! uri_scan_absolute(uri_object, &s, &l))
    return FALSE ;

  if (l == 0) {
    *str = s ;
    *str_len = l ;
    return TRUE ;
  }

  if (*s != '#')
    return FALSE ;

  l-- ; /* consume # */
  s++ ;

  if (! uri_scan_fragment(uri_object, &s, &l))
    return FALSE ;

  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/*    relative-part = "//" authority path-abempty
 *                  / path-absolute
 *                  / path-noscheme
 *                  / path-empty
 *
 *    path-empty    = 0<pchar>
 *    path-absolute = "/" [ segment-nz *( "/" segment ) ]
 *    path-noscheme = segment-nz-nc *( "/" segment )
 */
static HqBool uri_scan_relative_part(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s, *path = NULL ;
  uint32 l, path_len = 0 ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  if (l > 1 && *s == '/' && s[1] == '/') { /* "//" authority path-abempty */
    s += 2 ; /* consume // */
    l -= 2 ;

    uri_scan_authority(uri_object, &s, &l);

    path = s ;

#ifdef METRO
    /* Special case for Metro content type stream. */
    if (l == xps_content_typestream_partname_len &&
        HqMemCmp(xps_content_typestream_partname,
                 xps_content_typestream_partname_len, s, l) == 0) {
      s+= xps_content_typestream_partname_len ;
      l-= xps_content_typestream_partname_len ;

      path_len = CAST_PTRDIFFT_TO_UINT32(s - path) ;
      *str = s ;
      *str_len = l ;
      uri_object->path = path ;
      uri_object->path_len = path_len ;
      return TRUE ;
    }
#endif

    if (! uri_scan_path_abempty(uri_object, &s, &l))
      return FALSE ;

    path_len = CAST_PTRDIFFT_TO_UINT32(s - path) ;

  } else if (l > 0 && *s == '/') { /* path-absolute = "/" [ segment-nz *( "/" segment ) ] */
    path = s ;

    s++ ; /* consume / */
    l-- ;

    if (l == 0) { /* We are done. */
      path_len = CAST_PTRDIFFT_TO_UINT32(s - path) ;
      *str = s ;
      *str_len = l ;
      uri_object->path = path ;
      uri_object->path_len = path_len ;
      return TRUE ;
    }

    if (! uri_scan_segment_nz(uri_object, &s, &l))
      return FALSE ;

    while (l > 0 && *s == '/') {
      l-- ; /* consume / */
      s++ ;
      if (! uri_scan_segment(uri_object, &s, &l))
        return FALSE ;
    }

    path_len = CAST_PTRDIFFT_TO_UINT32(s - path) ;

  } else { /* path-noscheme = segment-nz-nc *( "/" segment ) */
    path = s ;

    if (! uri_scan_segment_nz_nc(uri_object, &s, &l)) {
      /* Unable to parse a segment_nz_nc so the last alternative is an
         empty path. */
      if (IS_PCHAR(s, l)) { /* path-empty    = 0<pchar> */
        return FALSE ;
      } else {
        return TRUE ; /* we are done - no <pchar> */
      }
    }

    while (l > 0 && *s == '/') {
      l-- ; /* consume / */
      s++ ;
      if (! uri_scan_segment(uri_object, &s, &l))
        return FALSE ;
    }
    path_len = CAST_PTRDIFFT_TO_UINT32(s - path) ;
  }

  uri_object->path = path ;
  uri_object->path_len = path_len ;
  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/*    relative-ref  = relative-part [ "?" query ] [ "#" fragment ]
 */
static HqBool uri_scan_relative_ref(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *s ;
  uint32 l ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(str_len != NULL, "str_len is NULL") ;
  l = *str_len ;
  HQASSERT(str != NULL, "str is NULL") ;
  s = *str ;
  HQASSERT(s != NULL, "s is NULL") ;

  if (! uri_scan_relative_part(uri_object, &s, &l))
    return FALSE ;

  if (l > 0 && *s == '?') {
    ++s ;
    --l ;

    if (! uri_scan_query(uri_object, &s, &l))
      return FALSE ;
  }

  if (l > 0 && *s == '#') {
    s++ ;
    l-- ; /* consume # */

    if (! uri_scan_fragment(uri_object, &s, &l))
      return FALSE ;
  }

  *str = s ;
  *str_len = l ;
  return TRUE ;
}

/*    URI-reference = URI / relative-ref
 */
static HqBool uri_scan_uri_reference(
      hqn_uri_t *uri_object)
{
  uint8 *s ;
  uint32 l ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;

  s = uri_object->original ;
  l = uri_object->original_len ;

  if (! uri_scan_uri(uri_object, &s, &l)) {
    /* not absolute - try relative - start again */
    s = uri_object->original ;
    l = uri_object->original_len ;

    /* Clear all fields - some may have been filled in while looking
       for an absolute URI */
    uri_clean(uri_object) ;

    if (! uri_scan_relative_ref(uri_object, &s, &l))
      return FALSE ;
  }

  /* Did we consume the entire URI? */
  if (l != 0)
    return FALSE ;

  return TRUE ;
}

static HqBool uri_copy_field(
      hqn_uri_t *to_uri,
      hqn_uri_t *from_uri,
      uint32 field)
{
  hqn_uri_context_t *uri_context ;
  uint8 **to_str, *from_str ;
  uint32 from_len, *to_len ;

  HQASSERT(to_uri != NULL, "to_uri is NULL") ;
  HQASSERT(from_uri != NULL, "from_uri is NULL") ;

  uri_context = to_uri->uri_context ;

  HQASSERT(uri_context != NULL, "uri_context is NULL") ;

  if (field == HQN_URI_SCHEME) {
    from_str = from_uri->scheme ;
    from_len = from_uri->scheme_len ;
    to_str = &(to_uri->scheme) ;
    to_len = &(to_uri->scheme_len) ;

  } else if (field == HQN_URI_AUTHORITY) {
    from_str = from_uri->authority ;
    from_len = from_uri->authority_len ;
    to_str = &(to_uri->authority) ;
    to_len = &(to_uri->authority_len) ;

  } else if (field == HQN_URI_PATH) {
    from_str = from_uri->path ;
    from_len = from_uri->path_len ;
    to_str = &(to_uri->path) ;
    to_len = &(to_uri->path_len) ;

  } else if (field == HQN_URI_QUERY) {
    from_str = from_uri->query ;
    from_len = from_uri->query_len ;
    to_str = &(to_uri->query) ;
    to_len = &(to_uri->query_len) ;

  } else if (field == HQN_URI_FRAGMENT) {
    from_str = from_uri->fragment ;
    from_len = from_uri->fragment_len ;
    to_str = &(to_uri->fragment) ;
    to_len = &(to_uri->fragment_len) ;

  } else {
    from_str = NULL ; /* stop warnings */
    from_len = 0 ;
    to_str = NULL ;
    to_len = NULL ;
    HQFAIL("Unknown URI field") ;
    return FALSE ;
  }

  HQASSERT(to_str != NULL, "to_str is NULL") ;
  HQASSERT(to_len != NULL, "to_len is NULL") ;

  *to_str = from_str ;
  *to_len = from_len ;

  return TRUE ;
}

/* Ought to be long enough to hold most of the URI paths we deal
   with. */
#define URI_TEMP_BUF_SIZE 256

/* Note that the to_uri may equal the from_uri. */
static HqBool uri_copy_path_norm(
      hqn_uri_t *to_uri,
      hqn_uri_t *from_uri,
      uint8 **new_buf)
{
  hqn_uri_context_t *uri_context ;
  static uint8 in_temp_buf[URI_TEMP_BUF_SIZE] ;
  static uint8 out_temp_buf[URI_TEMP_BUF_SIZE] ;
  uint8 *input = in_temp_buf, *output = out_temp_buf, *limit,
        *in_ptr, *out_ptr, *in_next ;
  uint32 remaining ;

  HQASSERT(to_uri != NULL, "to_uri is NULL") ;
  HQASSERT(from_uri != NULL, "from_uri is NULL") ;
  HQASSERT(new_buf != NULL, "new_buf is NULL") ;
  uri_context = to_uri->uri_context ;
  HQASSERT(uri_context != NULL, "uri_context is NULL") ;

  *new_buf = NULL ;

  /* The output buffer will never be larger than the input. Its likely
     to be smaller, but we don't know. */
  if (from_uri->path_len > URI_TEMP_BUF_SIZE) {
    if ((input = uri_context->mem.f_malloc((size_t)from_uri->path_len)) == NULL)
      return FALSE ;
    if ((output = uri_context->mem.f_malloc((size_t)from_uri->path_len)) == NULL) {
      uri_context->mem.f_free(input) ;
      return FALSE ;
    }
#if defined(DEBUG_BUILD) && defined(DEBUG_RFC3986)
    /* Some sanity when trying to debug - clear buffers so you can see chars being copied. */
    memset((void *)input, 0, (size_t)from_uri->path_len) ;
    memset((void *)output, 0, (size_t)from_uri->path_len) ;
#endif
  }
#if defined(DEBUG_BUILD) && defined(DEBUG_RFC3986)
  else {
    /* Some sanity when trying to debug - clear buffers so you can see chars being copied. */
    memset((void *)input, 0, (size_t)URI_TEMP_BUF_SIZE) ;
    memset((void *)output, 0, (size_t)URI_TEMP_BUF_SIZE) ;
  }
#endif

  /* We need to copy the input as we are going to be destructive on
     the input. */
  HqMemCpy(input, from_uri->path, from_uri->path_len) ;

  limit = input + from_uri->path_len ;
  in_ptr = input ;
  remaining = from_uri->path_len ;

  out_ptr = output ;

  /* RFC 3986 section 5.2.4 */

  /* 2.  While the input buffer is not empty, loop as follows: */
  while (in_ptr != limit) {

    /* A. If the input buffer begins with a prefix of "../" or "./",
       then remove that prefix from the input buffer; otherwise, */
    if (remaining > 1 && (in_ptr[0] == '.' && in_ptr[1] == '/')) {
      in_ptr += 2 ;
      remaining -= 2 ;
      in_next = in_ptr ;
      continue ;
    } else if (remaining > 2 && (in_ptr[0] == '.' && in_ptr[1] == '.' && in_ptr[2] == '/')) {
      in_ptr += 3 ;
      remaining -= 3 ;
      continue ;
    }

    /* B.  if the input buffer begins with a prefix of "/./" or "/.",
           where "." is a complete path segment, then replace that
           prefix with "/" in the input buffer; otherwise, */
    if (remaining > 2 && (in_ptr[0] == '/' && in_ptr[1] == '.' && in_ptr[2] == '/')) {
      in_ptr += 2 ;
      remaining -= 2 ;
      continue ;
    } else if (remaining == 2 && (in_ptr[0] == '/' && in_ptr[1] == '.')) {
      in_ptr += 1 ;
      remaining -= 1 ;
      *in_ptr = '/' ;
      continue ;
    }

    /* C.  if the input buffer begins with a prefix of "/../" or "/..",
           where ".." is a complete path segment, then replace that
           prefix with "/" in the input buffer and remove the last
           segment and its preceding "/" (if any) from the output
           buffer; otherwise, */
    if (remaining > 3 && (in_ptr[0] == '/' && in_ptr[1] == '.' && in_ptr[2] == '.' && in_ptr[3] == '/')) {
      in_ptr += 3 ;
      remaining -= 3 ;

      /* remove last segment in output */
      while (out_ptr != output) {
        out_ptr-- ;
        if (*out_ptr == '/')
          break ;
      }
      continue ;
    } else if (remaining == 3 && (in_ptr[0] == '/' && in_ptr[1] == '.' && in_ptr[2] == '.')) {
      in_ptr += 2 ;
      remaining -= 2 ;
      *in_ptr = '/' ;

      /* remove last segment in output */
      while (out_ptr != output) {
        out_ptr-- ;
        if (*out_ptr == '/')
          break ;
      }
      continue ;
    }

    /* D.  if the input buffer consists only of "." or "..", then remove
       that from the input buffer; otherwise, */
    if ((remaining == 1 && in_ptr[0] == '.') ||
        (remaining == 2 && in_ptr[0] == '.' && in_ptr[2] == '.')) {
      in_ptr += 2 ;
      remaining -= 2 ;
      continue ;
    }

    /* E.  move the first path segment in the input buffer to the end of
           the output buffer, including the initial "/" character (if
           any) and any subsequent characters up to, but not including,
           the next "/" character or the end of the input buffer. */
    if (in_ptr != limit) {
      *out_ptr = *in_ptr ;
      remaining-- ;
      in_ptr++ ;
      out_ptr++ ;
    }
    while (in_ptr != limit && *in_ptr != '/') {
      *out_ptr = *in_ptr ;
      remaining-- ;
      in_ptr++ ;
      out_ptr++ ;
    }
  }

  if (from_uri->path_len > URI_TEMP_BUF_SIZE) {
    uri_context->mem.f_free(input) ;
    *new_buf = output ; /* Caller will have to free this. */
  }

  to_uri->path = output ;
  to_uri->path_len = CAST_PTRDIFFT_TO_UINT32(out_ptr - output) ;

  return TRUE ;
}

static HqBool uri_copy_path_merge(
      hqn_uri_t *to_uri,
      hqn_uri_t *base_uri,
      hqn_uri_t *ref_uri,
      uint8 **new_buf)
{
  hqn_uri_context_t *uri_context ;
  uint8 *new_str ;

  HQASSERT(to_uri != NULL, "to_uri is NULL") ;
  HQASSERT(base_uri != NULL, "base_uri is NULL") ;
  HQASSERT(ref_uri != NULL, "ref_uri is NULL") ;

  uri_context = to_uri->uri_context ;

  HQASSERT(uri_context != NULL, "uri_context is NULL") ;

  /* This function WILL allocate memory to hold the new path. The
     memory can be freed at a latter stage. */
  *new_buf = NULL ;

  /* From 3986 section 5.2.3 */
  if (base_uri->authority_len > 0 &&
      base_uri->path_len == 0) {

    /* If the base URI has a defined authority component and an empty
      path, then return a string consisting of "/" concatenated with
      the reference's path; otherwise, */

    if ((new_str = uri_context->mem.f_malloc((size_t)ref_uri->path_len + 1)) == NULL)
      return FALSE ;
#if defined(DEBUG_BUILD) && defined(DEBUG_RFC3986)
    /* Some sanity when trying to debug - clear buffers so you can see chars being copied. */
    memset((void *)new_str, 0, (size_t)ref_uri->path_len + 1) ;
#endif

    new_str[0] = '/' ;
    HqMemCpy(&new_str[1], ref_uri->path, ref_uri->path_len) ;
    to_uri->path_len = ref_uri->path_len + 1 ;
    to_uri->path = new_str ;

  } else {
    uint8 *ptr ;
    uint32 new_base_len ;
    /* return a string consisting of the reference's path component
      appended to all but the last segment of the base URI's path
      (i.e., excluding any characters after the right-most "/" in the
      base URI path, or excluding the entire base URI path if it does
      not contain any "/" characters). */

    /* we want to keep the trailing / */
    ptr = base_uri->path + (base_uri->path_len - 1) ;

    while (ptr != base_uri->path && *ptr != '/')
      ptr-- ;

    new_base_len = CAST_PTRDIFFT_TO_UINT32(ptr - base_uri->path) ;

    if (*ptr == '/') /* we keep the / */
      new_base_len++ ;

    if ((new_str = uri_context->mem.f_malloc((size_t)new_base_len + ref_uri->path_len)) == NULL)
      return FALSE ;
#if defined(DEBUG_BUILD) && defined(DEBUG_RFC3986)
    /* Some sanity when trying to debug - clear buffers so you can see chars being copied. */
    memset((void *)new_str, 0, (size_t)new_base_len + ref_uri->path_len) ;
#endif

    HqMemCpy(&new_str[0], base_uri->path, new_base_len) ;
    HqMemCpy(&new_str[new_base_len], ref_uri->path, ref_uri->path_len) ;
    to_uri->path_len = new_base_len + ref_uri->path_len ;
    to_uri->path = new_str ;
  }

  *new_buf = new_str ;
  return TRUE ;
}

/* ============================================================================
 * Public interface.
 */

HqBool hqn_uri_copy(
      hqn_uri_context_t *uri_context,
      hqn_uri_t *from_uri_object,
      hqn_uri_t **to_uri_object)
{
  hqn_uri_t *new_uri ;

  HQASSERT(uri_context != NULL, "uri_context is NULL") ;
  HQASSERT(from_uri_object != NULL, "from_uri_object is NULL") ;
  HQASSERT(to_uri_object != NULL, "new_uri_object is NULL") ;

  *to_uri_object = NULL ;

  if (! hqn_uri_new_empty(uri_context, &new_uri))
    return FALSE ;

  new_uri->original = uri_context->mem.f_malloc(from_uri_object->original_len) ;
  if (new_uri->original == NULL) {
    hqn_uri_free(&new_uri) ;
    return FALSE ;
  }
  new_uri->copied = TRUE ; /* only set after allocation success */
  HqMemCpy(new_uri->original, from_uri_object->original,
           from_uri_object->original_len) ;
  new_uri->original_len = from_uri_object->original_len ;

  if (! uri_scan_uri_reference(new_uri)) {
    hqn_uri_free(&new_uri) ;
    return FALSE ;
  }

  *to_uri_object = new_uri ;

  return TRUE ;
}

/* Parsing is not destructive on original string. When creating the
 * new URI object, copying can be specified.
 */
HqBool hqn_uri_parse(
      hqn_uri_context_t *uri_context,
      hqn_uri_t **new_uri_object,
      const uint8 *uri,
      uint32 uri_len,
      HqBool copy_string)
{
  hqn_uri_t *new_uri ;

  HQASSERT(uri_context != NULL, "uri_context is NULL") ;
  HQASSERT(new_uri_object != NULL, "new_uri_object is NULL") ;
  HQASSERT(uri != NULL, "uri is NULL") ;
  HQASSERT(copy_string == TRUE ||
           copy_string == FALSE,
           "copy_string is incorrect") ;

  *new_uri_object = NULL ;

  if (! hqn_uri_new_empty(uri_context, &new_uri))
    return FALSE ;

  /* copy the original string */
  if (copy_string && uri_len != 0) {
    /* copy the string */
    new_uri->original = uri_context->mem.f_malloc((size_t)uri_len) ;
    if (new_uri->original == NULL) {
      hqn_uri_free(&new_uri) ;
      return FALSE ;
    }
    new_uri->copied = TRUE ; /* only set after allocation success */
    HqMemCpy(new_uri->original, uri, uri_len) ;

    /* static link to original string */
  } else {
    new_uri->original = (uint8 *)uri ;
  }
  new_uri->original_len = uri_len ;

  if (! uri_scan_uri_reference(new_uri)) {
    hqn_uri_free(&new_uri) ;
    return FALSE ;
  }

  *new_uri_object = new_uri ;
  return TRUE ;
}

HqBool hqn_uri_init(
      hqn_uri_context_t **uri_context,
      hqn_uri_memhandler_t *memory_handler)
{
  hqn_uri_context_t *new_uri_context ;

  HQASSERT(uri_context != NULL, "uri_context is NULL") ;
  HQASSERT(memory_handler != NULL, "memory_handler is NULL") ;

  *uri_context = NULL ;

  new_uri_context = memory_handler->f_malloc(sizeof(struct hqn_uri_context_t)) ;

  if (new_uri_context == NULL)
    return FALSE ;

  new_uri_context->mem = *memory_handler ;

  *uri_context = new_uri_context ;

#if defined(DEBUG_BUILD)
  if (! hqn_uri_do_tests(new_uri_context)) {
    hqn_uri_finish(uri_context) ;
    return FALSE ;
  }
#endif

  return TRUE ;
}

void hqn_uri_finish(
      hqn_uri_context_t **uri_context)
{
  HQASSERT(uri_context != NULL, "uri_context is NULL") ;

  (*uri_context)->mem.f_free(*uri_context) ;
  *uri_context = NULL ;
}

HqBool hqn_uri_new_empty(
      hqn_uri_context_t *uri_context,
      hqn_uri_t **new_uri_object)
{
  HQASSERT(uri_context != NULL, "uri_context is NULL") ;
  HQASSERT(new_uri_object != NULL, "new_uri_onject is NULL") ;

  *new_uri_object = uri_context->mem.f_malloc(sizeof(struct hqn_uri_t)) ;

  if (*new_uri_object == NULL)
    return FALSE ;

  (void)memset(*new_uri_object, 0, sizeof(struct hqn_uri_t));

  (*new_uri_object)->uri_context = uri_context ;
  return TRUE ;
}

void hqn_uri_free(
      hqn_uri_t **uri_object)
{
  hqn_uri_context_t *uri_context ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(*uri_object != NULL, "uri_object pointer is NULL") ;
  HQASSERT((*uri_object)->uri_context != NULL, "uri_object context pointer is NULL") ;

  uri_context = (*uri_object)->uri_context ;

  uri_clean((*uri_object)) ;

  if ((*uri_object)->copied)
    uri_context->mem.f_free((*uri_object)->original) ;

  uri_context->mem.f_free(*uri_object) ;
  *uri_object = NULL ;
}

static HqBool rebuild_uri(
      hqn_uri_context_t *uri_context,
      hqn_uri_t *uri)
{
  uint32 len = 0 ;
  uint8 *new_uri_string, *old_uri_string ;

  if (uri->scheme_len > 0)
    len += uri->scheme_len + 1 ; /* : */

  /* The authority is made up from use, host and port */
  if (uri->authority_len > 0)
    len += uri->authority_len + 2 ; /* //<auth> */

  if (uri->path_len > 0)
    len += uri->path_len ;

  if (uri->query_len > 0)
    len += uri->query_len + 1 ;  /* ? */

  if (uri->fragment_len > 0)
    len += uri->fragment_len + 1 ; /* # */

  if (uri->copied) {
    old_uri_string = uri->original ;
  } else {
    old_uri_string = NULL ;
  }

  if ((new_uri_string = uri_context->mem.f_malloc((size_t)len)) == NULL)
    return FALSE ;

  uri->original = new_uri_string ;
  uri->original_len = len ;
  uri->copied = TRUE ;

  if (uri->scheme_len > 0) {
    HqMemCpy(new_uri_string, uri->scheme, uri->scheme_len) ;
    uri->scheme = new_uri_string ;
    new_uri_string += uri->scheme_len ;
    HqMemCpy(new_uri_string, ":", 1) ;
    new_uri_string += 1 ;
  }

  if (uri->authority_len > 0) {
    HqMemCpy(new_uri_string, "//", 2) ;
    new_uri_string += 2 ;

    HqMemCpy(new_uri_string, uri->authority, uri->authority_len) ;
    uri->authority = new_uri_string ;
    new_uri_string += uri->authority_len ;
  }

  if (uri->path_len > 0) {
    HqMemCpy(new_uri_string, uri->path, uri->path_len) ;
    uri->path = new_uri_string ;
    new_uri_string += uri->path_len ;
  }

  if (uri->query_len > 0) {
    HqMemCpy(new_uri_string, "?", 1) ;
    new_uri_string += 1 ;

    HqMemCpy(new_uri_string, uri->query, uri->query_len) ;
    uri->query = new_uri_string ;
    new_uri_string += uri->query_len ;
  }

  if (uri->fragment_len > 0) {
    HqMemCpy(new_uri_string, "#", 1) ;
    new_uri_string += 1 ;

    HqMemCpy(new_uri_string, uri->fragment, uri->fragment_len) ;
    uri->fragment = new_uri_string ;
    new_uri_string += uri->fragment_len ;
  }

  /* Free any previous memory */
  if (old_uri_string != NULL)
    uri_context->mem.f_free(old_uri_string) ;

  return TRUE ;
}

/* Computes the final URI of the reference done by checking that the
 * given URI is valid, and building the final URI using the base
 * URI. This is processed according to section 5.2 of the RFC 3986
 *
 * 5.2. Relative Resolution
 */
HqBool hqn_uri_rel_parse(
      hqn_uri_context_t *uri_context,
      hqn_uri_t **new_uri_object,
      hqn_uri_t *base_uri_object,
      const uint8 *rel_uri,
      uint32 rel_len)
{
  hqn_uri_t *rel_uri_object ;
  hqn_uri_t *build_uri ;
  HqBool status = TRUE ;
  uint8 *merge_buf = NULL ;
  uint8 *norm_buf = NULL ;

  HQASSERT(uri_context != NULL, "uri_context is NULL") ;
  HQASSERT(new_uri_object != NULL, "new_uri_object is NULL") ;
  HQASSERT(rel_uri != NULL, "rel_uri is NULL" ) ;

  *new_uri_object = NULL ;

  /* 5.2.2 The URI reference is parsed into the five URI components
     (R.scheme, R.authority, R.path, R.query, R.fragment) =
     parse(R); */
  if (! hqn_uri_parse(uri_context,
                      &rel_uri_object,
                      rel_uri, rel_len,
                      FALSE /* don't copy string */)) {
    return FALSE ;
  }

  /* A non-strict parser may ignore a scheme in the reference if it is
     identical to the base URI's scheme.

     if ((not strict) and (R.scheme == Base.scheme)) then
       undefine(R.scheme);
     endif;

     We ARE a strict parser, so we have nothing to do for this step.
  */

  /* We have a base and we have a relative URI, we need to inherit
     appropriately from the base - so we need a new URI to build. The
     rel_uri_object effectively becomes a transient object. */
  if (! hqn_uri_new_empty(uri_context, &build_uri)) {
    hqn_uri_free(&rel_uri_object) ;
    return FALSE ;
  }

  if (rel_uri_object->scheme_len > 0) { /* if defined(R.scheme) then */
    if (! uri_copy_field(build_uri, rel_uri_object, HQN_URI_SCHEME) ||    /* T.scheme    = R.scheme; */
        ! uri_copy_field(build_uri, rel_uri_object, HQN_URI_AUTHORITY) || /* T.authority = R.authority; */
        ! uri_copy_field(build_uri, rel_uri_object, HQN_URI_QUERY) ||     /* T.query     = R.query; */
        ! uri_copy_path_norm(build_uri, rel_uri_object, &norm_buf)) {     /* T.path      = remove_dot_segments(R.path); */
      status = FALSE ;
    }
  } else {
    if (rel_uri_object->authority_len > 0) { /* if defined(R.authority) then */
      if (! uri_copy_field(build_uri, rel_uri_object, HQN_URI_AUTHORITY) || /*T.authority = R.authority; */
          ! uri_copy_field(build_uri, rel_uri_object, HQN_URI_QUERY) ||     /*T.query     = R.query; */
          ! uri_copy_path_norm(build_uri, rel_uri_object, &norm_buf)) {     /* T.path      = remove_dot_segments(R.path); */

        status = FALSE ;
      }
    } else {
      if (rel_uri_object->path_len == 0) { /* if (R.path == "") then */
        if (! uri_copy_field(build_uri, base_uri_object, HQN_URI_PATH)) /* T.path = Base.path; */
          status = FALSE ;

        if (status && rel_uri_object->query_len > 0) { /* if defined(R.query) then */
          if (! uri_copy_field(build_uri, rel_uri_object, HQN_URI_QUERY)) /* T.query = R.query; */
            status = FALSE ;
        } else {
          if (! uri_copy_field(build_uri, base_uri_object, HQN_URI_QUERY)) /* T.query = Base.query; */
            status = FALSE ;
        }
      } else {
        if (rel_uri_object->path[0] == '/') { /* if (R.path starts-with "/") then */
          if (status && ! uri_copy_path_norm(build_uri, rel_uri_object, &norm_buf)) /* T.path = remove_dot_segments(R.path); */
            status = FALSE ;
        } else {
          if (status && ! uri_copy_path_merge(build_uri, base_uri_object, rel_uri_object, &merge_buf)) /* T.path = remove_dot_segments(R.path); */
            status = FALSE ;
          if (status && ! uri_copy_path_norm(build_uri, build_uri, &norm_buf)) /* T.path = remove_dot_segments(T.path); */
            status = FALSE ;
        }
        if (status && ! uri_copy_field(build_uri, rel_uri_object, HQN_URI_QUERY)) /* T.query = R.query; */
          status = FALSE ;
      }

      if (status && ! uri_copy_field(build_uri, base_uri_object, HQN_URI_AUTHORITY)) /* T.authority = Base.authority; */
        status = FALSE ;
    }
    if (status && ! uri_copy_field(build_uri, base_uri_object, HQN_URI_SCHEME)) /* T.scheme = Base.scheme; */
      status = FALSE ;
  }

  if (status) {
    status = uri_copy_field(build_uri, rel_uri_object, HQN_URI_FRAGMENT) ; /* T.fragment = R.fragment; */
  } else {
    hqn_uri_free(&build_uri) ;
  }

  /* This reallocates the appropriate memory. */
  status = rebuild_uri(uri_context, build_uri) ;

  hqn_uri_free(&rel_uri_object) ;
  if (merge_buf != NULL) {
    uri_context->mem.f_free(merge_buf) ;
    merge_buf = NULL ;
  }
  if (norm_buf != NULL) {
    uri_context->mem.f_free(norm_buf) ;
    norm_buf = NULL ;
  }

  if (status)
    *new_uri_object = build_uri ;

  return status ;
}

HqBool hqn_uri_set_field(
      hqn_uri_t *uri_object,
      uint32 which_fields,
      uint8 *buf,
      uint32 buf_len)
{
  hqn_uri_context_t *uri_context ;
  int32 diff_len = 0 ;
  size_t alloc_size = 0 ;
  size_t copy_len = 0 ;
  uint8 *newstr = NULL ;
  uint8 *remaining_str = NULL ;
  uint32 remaining_len = 0 ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(buf != NULL, "bufis NULL") ;
  HQASSERT(buf_len > 0, "buf_len is not greater than zero") ;
  uri_context = uri_object->uri_context ;
  HQASSERT(uri_context != NULL, "uri_context is NULL") ;

  /* We do not allow trashing of potentially static strings. */
  if (! uri_object->copied) {
    HQFAIL("You may only set fields on copied URI's") ;
    return FALSE ;
  }

  if (which_fields == HQN_URI_FRAGMENT) {
    diff_len = buf_len - uri_object->fragment_len ;
    if (diff_len != 0) { /* its longer or shorter */
      alloc_size = uri_object->original_len + diff_len ;

      if (uri_object->fragment_len == 0) /* no fragment at all */
        alloc_size++ ; /* for the # */

      if ((newstr = uri_context->mem.f_malloc(alloc_size)) == NULL)
        return FALSE ;

      if (uri_object->fragment_len == 0) { /* no fragment at all */
        copy_len = uri_object->original_len ;
        HqMemCpy(newstr, uri_object->original, copy_len) ;
        HqMemCpy(newstr + copy_len, "#", 1) ;
        HqMemCpy(newstr + copy_len + 1, buf, buf_len) ;
      } else {
        HQASSERT(uri_object->fragment != NULL,
                 "field has length but field pointer is NULL") ;
        copy_len = uri_object->fragment - uri_object->original ;
        HqMemCpy(newstr, uri_object->original, copy_len) ;
        HqMemCpy(newstr + copy_len, buf, buf_len) ;
      }
    } else {
      /* clobber same length fragment */
      HqMemCpy(uri_object->fragment, buf, buf_len) ;
    }

  } else if (which_fields == HQN_URI_QUERY) {
    diff_len = buf_len - uri_object->query_len ;
    if (diff_len != 0) { /* its longer or shorter */
      alloc_size = uri_object->original_len + diff_len ;

      if (uri_object->query_len == 0) /* no query at all */
        alloc_size++ ; /* for the ? */

      if ((newstr = uri_context->mem.f_malloc(alloc_size)) == NULL)
        return FALSE ;

      remaining_str = NULL ;
      remaining_len = 0 ;
      if (uri_object->fragment_len > 0) { /* we have a fragment */
        remaining_str = --(uri_object->fragment) ; /* points to the # */
        remaining_len = uri_object->original_len - CAST_PTRDIFFT_TO_UINT32(remaining_str - uri_object->original) ;
      }

      copy_len = uri_object->original_len ;
      if (uri_object->query_len == 0) { /* no existing query */
        /* copy everything upto just before the fragment */
        if (remaining_str != NULL)
          copy_len = remaining_str - uri_object->original ;

      } else { /* we have a query */
        HQASSERT(uri_object->query != NULL,
                 "field has length but field pointer is NULL") ;

        /* copy everything upto just before the query */
        if (remaining_str != NULL)
          copy_len = uri_object->query - uri_object->original - 1 ;
      }

      HqMemCpy(newstr, uri_object->original, copy_len) ;
      /* copy in query */
      HqMemCpy(newstr + copy_len, "?", 1) ;
      copy_len++ ;
      HqMemCpy(newstr + copy_len, buf, buf_len) ;

      /* append remaining guff */
      if (remaining_str != NULL) {
        copy_len += buf_len ;
        HqMemCpy(newstr + copy_len, remaining_str, remaining_len) ;
      }

    } else {
      /* clobber same length query */
      HqMemCpy(uri_object->query, buf, buf_len) ;
    }

  } else if (which_fields == HQN_URI_PATH) {
    /* NOTE: The path contains the leading "/" unlike fragment and
       query. */

    diff_len = buf_len - uri_object->path_len ;
    if (diff_len != 0) { /* its longer or shorter */
      alloc_size = uri_object->original_len + diff_len ;

      if ((newstr = uri_context->mem.f_malloc(alloc_size)) == NULL)
        return FALSE ;

      remaining_str = NULL ;
      remaining_len = 0 ;
      if (uri_object->query_len > 0) { /* we have a query */
        remaining_str = --(uri_object->query) ; /* points to the ? */
        remaining_len = uri_object->original_len - CAST_PTRDIFFT_TO_UINT32(remaining_str - uri_object->original) ;
      }

      copy_len = uri_object->original_len ;
      if (uri_object->path_len == 0) { /* no existing path */
        /* copy everything upto just before the query */
        if (remaining_str != NULL)
          copy_len = remaining_str - uri_object->original ;

      } else { /* we have a path */
        HQASSERT(uri_object->path != NULL,
                 "field has length but field pointer is NULL") ;

        /* copy everything upto just before the path */
        if (remaining_str != NULL)
          copy_len = uri_object->path - uri_object->original ;
      }

      HqMemCpy(newstr, uri_object->original, copy_len) ;
      /* copy in path */
      HqMemCpy(newstr + copy_len, buf, buf_len) ;

      /* append remaining guff */
      if (remaining_str != NULL) {
        copy_len += buf_len ;
        HqMemCpy(newstr + copy_len, remaining_str, remaining_len) ;
      }

    } else {
      /* clobber same length path */
      HqMemCpy(uri_object->path, buf, buf_len) ;
    }
  } else if (which_fields == HQN_URI_NAME) {
    /* NOTE: The path contains the leading "/" unlike fragment and
       query. */
    diff_len = buf_len - uri_object->path_len - uri_object->query_len -
                         uri_object->fragment_len ;

    /* if query or fragment, don't forget to count ? and # */
    if (uri_object->query_len > 0)
      diff_len-- ;
    if (uri_object->fragment_len > 0)
      diff_len-- ;

    if (diff_len != 0) { /* its longer or shorter */
      alloc_size = uri_object->original_len + diff_len ;

      if ((newstr = uri_context->mem.f_malloc(alloc_size)) == NULL)
        return FALSE ;

      if (uri_object->path_len == 0) { /* no path at all */
        copy_len = uri_object->original_len ;
      } else {
        HQASSERT(uri_object->path != NULL,
                 "field has length but field pointer is NULL") ;
        copy_len = uri_object->path - uri_object->original ;
      }

      HqMemCpy(newstr, uri_object->original, copy_len) ;
      HqMemCpy(newstr + copy_len, buf, buf_len) ;
    } else {
      /* clobber same length */
      HqMemCpy(uri_object->path, buf, buf_len) ;
    }

  } else {
    HQFAIL("You are unable to set this URI field.") ;
    return FALSE ;
  }

  if (diff_len != 0) { /* it was re-allocated */
    uri_context->mem.f_free(uri_object->original) ;
    uri_object->original = newstr ;
    uri_object->original_len = CAST_SIZET_TO_UINT32(alloc_size) ;
  }
  uri_clean(uri_object) ;
  if (! uri_scan_uri_reference(uri_object))
    return FALSE ;

  return TRUE ;
}

HqBool hqn_uri_get_field(
      hqn_uri_t *uri_object,
      uint8 **field,
      uint32 *field_len,
      uint32 which_fields)
{
  uint32 prev_segment = 0 ;
  uint8 *start = NULL ;
  uint32 len = 0 ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;
  HQASSERT(field != NULL, "field is NULL") ;
  HQASSERT(field_len != NULL, "field_len is NULL") ;

  *field = NULL ;
  *field_len = 0 ;

  if (which_fields & HQN_URI_ENTIRE) {
    if (uri_object->original_len > 0) {
      start = uri_object->original ;
      len = uri_object->original_len ;
      which_fields ^= HQN_URI_ENTIRE ;
    } else {
      return FALSE ;
    }
  } else {
    if (which_fields & HQN_URI_SCHEME) { /* asking for scheme */
      if (uri_object->scheme_len > 0) {
        start = uri_object->scheme ;
        len = uri_object->scheme_len ;
      } else {
        return FALSE ;
      }
      prev_segment = HQN_URI_SCHEME ;
      which_fields ^= HQN_URI_SCHEME ;
    }

    if (which_fields & HQN_URI_AUTHORITY) {
      HQASSERT(! (which_fields & HQN_URI_USER ||
                  which_fields & HQN_URI_HOST ||
                  which_fields & HQN_URI_PORT),
               "You can't request all of the authority and a specific segement together.") ;

      if (start != NULL) {
        if (prev_segment != HQN_URI_SCHEME) {
          HQFAIL("Not requesting contiguous fields.") ;
          return FALSE ;
        }
      } else {
        if (uri_object->authority_len > 0) {
          start = uri_object->authority ;
          /* cheat - pretent we matched the port */
          prev_segment = HQN_URI_PORT ;
          which_fields ^= HQN_URI_AUTHORITY ;
        }
      }
      len += uri_object->authority_len ;
       /* we did not find the authority above - so look for
         user@host:port */
      if (which_fields & HQN_URI_AUTHORITY) {
        which_fields |= HQN_URI_USER ;
        which_fields |= HQN_URI_HOST ;
        which_fields |= HQN_URI_PORT ;
      }
    }

    if (which_fields & HQN_URI_USER) {
      if (start != NULL) {
        if (prev_segment != HQN_URI_SCHEME) {
          HQFAIL("Not requesting contiguous fields.") ;
          return FALSE ;
        }
      } else {
        if (uri_object->user_len > 0) {
          which_fields ^= HQN_URI_AUTHORITY ;
          start = uri_object->user ;

          /* look ahead for host request */
          if (which_fields & HQN_URI_HOST && uri_object->host_len > 0)
            len += 1 ; /* add @ */
        }
      }
      len += uri_object->user_len ;
      prev_segment = HQN_URI_USER ;
      which_fields ^= HQN_URI_USER ;
    }

    if (which_fields & HQN_URI_HOST) {
      if (start != NULL) {
        if (prev_segment != HQN_URI_USER) {
          HQFAIL("Not requesting contiguous fields.") ;
          return FALSE ;
        }
      } else {
        if (uri_object->host_len > 0) {
          which_fields ^= HQN_URI_AUTHORITY ;
          start = uri_object->host ;
          /* look ahead for port request */
          if (which_fields & HQN_URI_PORT && uri_object->port_len > 0)
            len += 1 ; /* add : */
       }
      }

      len += uri_object->host_len ;
      prev_segment = HQN_URI_HOST ;
      which_fields ^= HQN_URI_HOST ;
    }

    if (which_fields & HQN_URI_PORT) {
      if (start != NULL) {
        if (prev_segment != HQN_URI_HOST) {
          HQFAIL("Not requesting contiguous fields.") ;
          return FALSE ;
        }
      } else {
        if (uri_object->port_len > 0) {
          which_fields ^= HQN_URI_AUTHORITY ;
          start = uri_object->port ;
        }
      }

      len += uri_object->port_len ;
      prev_segment = HQN_URI_PORT ;
      which_fields ^= HQN_URI_PORT ;
    }

    /* We have still not found the authority when it was
       requested. */
    if (which_fields & HQN_URI_AUTHORITY)
      return FALSE ;

    if (which_fields & HQN_URI_PATH) {
      if (start != NULL) {
        if (prev_segment != HQN_URI_PORT) {
          HQFAIL("Not requesting contiguous fields.") ;
          return FALSE ;
        }
      } else {
        if (uri_object->path > 0)
          start = uri_object->path ;
      }
      len += uri_object->path_len ;
      prev_segment = HQN_URI_PATH ;
      which_fields ^= HQN_URI_PATH ;

      /* look ahead for query request */
      if (which_fields & HQN_URI_QUERY && uri_object->query_len > 0)
        len += 1 ; /* add ? */
    }

    if (which_fields & HQN_URI_QUERY) {
      if (start != NULL) {
        if (prev_segment != HQN_URI_PATH) {
          HQFAIL("Not requesting contiguous fields.") ;
          return FALSE ;
        }
      } else {
        if (uri_object->query_len > 0)
          start = uri_object->query ;
      }
      len += uri_object->query_len ;
      prev_segment = HQN_URI_QUERY ;
      which_fields ^= HQN_URI_QUERY ;

      /* look ahead for fragment request */
      if (which_fields & HQN_URI_FRAGMENT && uri_object->fragment_len > 0)
        len += 1 ; /* add # */
    }

    if (which_fields & HQN_URI_FRAGMENT) {
      if (start != NULL) {
        if (prev_segment != HQN_URI_QUERY) {
          HQFAIL("Not requesting contiguous fields.") ;
          return FALSE ;
        }
      } else {
        if (uri_object->fragment > 0)
          start = uri_object->fragment ;
      }
      len += uri_object->fragment_len ;
      prev_segment = HQN_URI_FRAGMENT ;
      which_fields ^= HQN_URI_FRAGMENT ;
    }

  } /* end not entire */

  /* We should have satisfied all segment requests */
  HQASSERT(which_fields == 0,
           "Requesting invalid segement combination.") ;

  *field = start ;
  *field_len = len ;

  return (len > 0) ;
}

/* ============================================================================
 * Many tests are copied from RFC 3986 section 5.4. Works out to be a
 * rather good set of tests for the URI library.
 */
#if defined(DEBUG_BUILD)

/* As per RFC 3986 */
#define RFC_3986_BASE "http://a/b/c/d;p?q"
/* The string and its length, a useful shorthand */
#define SL(s) ((uint8 *)s), (sizeof(s) - 1)

#define PASS TRUE
#define FAIL FALSE

static HqBool test_parse_uri(
      hqn_uri_context_t *uri_context,
      HqBool expect_pass,
      uint8 *base_uri,
      uint32 base_uri_len)
{
  hqn_uri_t *base_uri_object ;
  HqBool status ;

  status =  hqn_uri_parse(uri_context,
                          &base_uri_object,
                          base_uri,
                          base_uri_len,
                          TRUE /* copy string */) ;

  if (status)
    hqn_uri_free(&base_uri_object) ;

  /* just do these asserts here so we check that the free's are doing
     what they are supposed to be doing. */
  HQASSERT(base_uri_object == NULL,
           "base URI was not set to NULL") ;
  return (status == expect_pass) ;
}

static HqBool test_parse_rel_uri(
      hqn_uri_context_t *uri_context,
      uint8 *base_uri,
      uint32 base_uri_len,
      uint8 *rel_uri,
      uint32 rel_uri_len,
      uint8 *expected_uri,
      uint32 expected_uri_len)
{
  hqn_uri_t *base_uri_object ;
  hqn_uri_t *new_uri_object ;

  if (! hqn_uri_parse(uri_context,
                      &base_uri_object,
                      base_uri,
                      base_uri_len,
                      TRUE /* copy string */)) {
    return FALSE ;
  }

  if (! hqn_uri_rel_parse(uri_context,
                          &new_uri_object,
                          base_uri_object,
                          rel_uri, rel_uri_len)) {

    hqn_uri_free(&base_uri_object) ;
    return FALSE ;
  }

  /* get the string and compare to the expected result */
  if (HqMemCmp(expected_uri, expected_uri_len,
               new_uri_object->original,
               new_uri_object->original_len) != 0) {
    hqn_uri_free(&base_uri_object) ;
    hqn_uri_free(&new_uri_object) ;
    return FALSE ;
  }

  hqn_uri_free(&base_uri_object) ;
  hqn_uri_free(&new_uri_object) ;

  /* just do these asserts here so we check that the free's are doing what
     they are supposed to be doing. */
  HQASSERT(base_uri_object == NULL,
           "base URI was not set to NULL") ;
  HQASSERT(new_uri_object == NULL,
           "base URI was not set to NULL") ;
  return TRUE ;
}

static HqBool test_get_field(
      hqn_uri_t *uri_object,
      uint32 which_fields,
      uint8 *expected_str,
      uint32 expected_str_len)
{
  HqBool status ;
  uint8 *field ;
  uint32 field_len ;

  status = hqn_uri_get_field(uri_object, &field, &field_len, which_fields) ;

  if (! status ||
      HqMemCmp(field, field_len, expected_str, expected_str_len) != 0)
    status = FALSE ;

  if (! status) {
    hqn_uri_free(&uri_object) ;
    HQFAIL("get_field appears to have failed") ;
  }

  return status ;
}

static HqBool hqn_uri_do_tests(
      hqn_uri_context_t *uri_context)
{
  /* Absolute URI parsing. Although we do not check to see the results
     are correct, this can be done in the debugger. */

  /* Typical authority */
  if (! test_parse_uri(uri_context, PASS, SL("http://authority/path/a/b/c;;;?hello=there#fragment")))
    HQFAIL("URI absolute parse failed") ;

  if (! test_parse_uri(uri_context, PASS, SL("http://www.global.com/path/a/b/c;;;?hello=there#fragment")))
    HQFAIL("URI absolute parse failed") ;

  /* User and port test. */
  if (! test_parse_uri(uri_context, PASS, SL("http://johnk@www.global.com:1234/path/a/b/c;;;?hello=there#fragment")))
    HQFAIL("URI absolute parse failed") ;

  /* Empty test. */
  if (! test_parse_uri(uri_context, PASS, SL("http:")))
    HQFAIL("URI absolute parse failed") ;

  /* v4 IP address */
  if (! test_parse_uri(uri_context, PASS, SL("http://123.123.123.123/path/a/b/c;;;?hello=there#fragment")))
    HQFAIL("URI absolute parse failed") ;

  /* Future IP address */
  if (! test_parse_uri(uri_context, PASS, SL("http://[v23.hello]/path/a/b/c;;;?hello=there#fragment")))
    HQFAIL("URI absolute parse failed") ;

  /* IP v6 addresses */
  if (! test_parse_uri(uri_context, PASS, SL("http://[ABC:ABC:ABC:ABC:123:123:AB:AB]/path/a/b/c;;;?hello=there#fragment")))
    HQFAIL("URI absolute parse failed") ;

  if (! test_parse_uri(uri_context, PASS, SL("http://[ABCD:ABC:ABC:ABC:123:123:123.123.123.123]/path/a/b/c;;;?hello=there#fragment")))
    HQFAIL("URI absolute parse failed") ;

  if (! test_parse_uri(uri_context, PASS, SL("http://[::BC:ABCD:123.123.123.123]/path/a/b/c;;;?hello=there#fragment")))
    HQFAIL("URI absolute parse failed") ;

  if (! test_parse_uri(uri_context, PASS, SL("http://[AB:AB::BC:ABCD:123.123.123.123]/path/a/b/c;;;?hello=there#fragment")))
    HQFAIL("URI absolute parse failed") ;

  if (! test_parse_uri(uri_context, FAIL, SL("http://[AB:AB:AB:AB::BC:ABCD:123.123.123.123]/path/a/b/c;;;?hello=there#fragment")))
    HQFAIL("URI absolute parse failed") ;

  if (! test_parse_uri(uri_context, PASS, SL("/g")))
    HQFAIL("URI absolute parse failed") ;

  /* From RFC 3986 */
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g:h"), /* rel URI */
                      SL("g:h"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g"), /* rel URI */
                      SL("http://a/b/c/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("./g"), /* rel URI */
                      SL("http://a/b/c/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g/"), /* rel URI */
                      SL("http://a/b/c/g/"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("/g"), /* rel URI */
                      SL("http://a/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("//g"), /* rel URI */
                      SL("http://g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("?y"), /* rel URI */
                      SL("http://a/b/c/d;p?y"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g?y"), /* rel URI */
                      SL("http://a/b/c/g?y"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("#s"), /* rel URI */
                      SL("http://a/b/c/d;p?q#s"))) /* expected URI - <CurrentDocument> */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g#s"), /* rel URI */
                      SL("http://a/b/c/g#s"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g?y#s"), /* rel URI */
                      SL("http://a/b/c/g?y#s"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL(";x"), /* rel URI */
                      SL("http://a/b/c/;x"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g;x"), /* rel URI */
                      SL("http://a/b/c/g;x"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g;x?y#s"), /* rel URI */
                      SL("http://a/b/c/g;x?y#s"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL(""), /* rel URI */
                      SL("http://a/b/c/d;p?q"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("."), /* rel URI */
                      SL("http://a/b/c/"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("./"), /* rel URI */
                      SL("http://a/b/c/"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL(".."), /* rel URI */
                      SL("http://a/b/"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("../"), /* rel URI */
                      SL("http://a/b/"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("../g"), /* rel URI */
                      SL("http://a/b/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("../.."), /* rel URI */
                      SL("http://a/"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("../../"), /* rel URI */
                      SL("http://a/"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("../../g"), /* rel URI */
                      SL("http://a/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;

  /* C.2 Abnormal examples */

  /* NOTE: We strip them off. */
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("../../../g"), /* rel URI */
                      SL("http://a/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("../../../../g"), /* rel URI */
                      SL("http://a/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;

  /* being careful with . and .. */
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("/./g"), /* rel URI */
                      SL("http://a/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("/../g"), /* rel URI */
                      SL("http://a/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g."), /* rel URI */
                      SL("http://a/b/c/g."))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL(".g"), /* rel URI */
                      SL("http://a/b/c/.g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g.."), /* rel URI */
                      SL("http://a/b/c/g.."))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("..g"), /* rel URI */
                      SL("http://a/b/c/..g"))) /* expected URI */
    HQFAIL("URI parse failed") ;

  /* less likely cases */
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("./../g"), /* rel URI */
                      SL("http://a/b/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("./g/."), /* rel URI */
                      SL("http://a/b/c/g/"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g/./h"), /* rel URI */
                      SL("http://a/b/c/g/h"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g/../h"), /* rel URI */
                      SL("http://a/b/c/h"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g;x=1/./y"), /* rel URI */
                      SL("http://a/b/c/g;x=1/y"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g;x=1/../y"), /* rel URI */
                      SL("http://a/b/c/y"))) /* expected URI */
    HQFAIL("URI parse failed") ;

  /* testing query and fragment components */
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g?y/./x"), /* rel URI */
                      SL("http://a/b/c/g?y/./x"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g?y/../x"), /* rel URI */
                      SL("http://a/b/c/g?y/../x"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g#s/./x"), /* rel URI */
                      SL("http://a/b/c/g#s/./x"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("g#s/../x"), /* rel URI */
                      SL("http://a/b/c/g#s/../x"))) /* expected URI */
    HQFAIL("URI parse failed") ;

  /* We will implement a strict parser */
  if (! test_parse_rel_uri(uri_context,SL(RFC_3986_BASE),
                      SL("http:g"), /* rel URI */
                      SL("http:g"))) /* expected URI */
    HQFAIL("URI parse failed") ;

  /* Test get field */
  {
#define GET_FIELD_TEST "http://a/b/c/d;p?q#f"
    hqn_uri_t *uri_object ;

    if (! hqn_uri_parse(uri_context, &uri_object, SL(GET_FIELD_TEST), FALSE))
      return FALSE ;

    if (! test_get_field(uri_object, HQN_URI_ENTIRE, SL(GET_FIELD_TEST)))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_SCHEME, SL("http")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_AUTHORITY, SL("a")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_NAME, SL("/b/c/d;p?q#f")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_FRAGMENT, SL("f")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_QUERY | HQN_URI_FRAGMENT, SL("q#f")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_PATH, SL("/b/c/d;p")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_QUERY, SL("q")))
      return FALSE ;

    hqn_uri_free(&uri_object) ;
  }

  /* Test set field */
  {
    hqn_uri_t *uri_object ;

    /* FRAGMENT TESTS */

#define SET_FIELD_TEST "http://a/b/c/d;p?q#frag"
    /* no fragment test */
    if (! hqn_uri_parse(uri_context, &uri_object, SL(RFC_3986_BASE), TRUE))
      return FALSE ;
    if (! hqn_uri_set_field(uri_object, HQN_URI_FRAGMENT, SL("myfragment")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_FRAGMENT, SL("myfragment")))
      return FALSE ;
    hqn_uri_free(&uri_object) ;

    /* shorter fragment test */
    if (! hqn_uri_parse(uri_context, &uri_object, SL(SET_FIELD_TEST), TRUE))
      return FALSE ;
    if (! hqn_uri_set_field(uri_object, HQN_URI_FRAGMENT, SL("myfragment")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_FRAGMENT, SL("myfragment")))
      return FALSE ;
    hqn_uri_free(&uri_object) ;

    /* same length fragment test */
    if (! hqn_uri_parse(uri_context, &uri_object, SL(SET_FIELD_TEST), TRUE))
      return FALSE ;
    if (! hqn_uri_set_field(uri_object, HQN_URI_FRAGMENT, SL("Zzzz")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_FRAGMENT, SL("Zzzz")))
      return FALSE ;
    hqn_uri_free(&uri_object) ;

    /* longer fragment test */
    if (! hqn_uri_parse(uri_context, &uri_object, SL(SET_FIELD_TEST), TRUE))
      return FALSE ;
    if (! hqn_uri_set_field(uri_object, HQN_URI_FRAGMENT, SL("AB")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_FRAGMENT, SL("AB")))
      return FALSE ;
    hqn_uri_free(&uri_object) ;

    /* QUERY TESTS */

#define SET_QUERY_TEST_1 "http://a/b/c/d;p"
#define SET_QUERY_TEST_2 "http://a/b/c/d;p#fragment"
    /* no query test */
    if (! hqn_uri_parse(uri_context, &uri_object, SL(SET_QUERY_TEST_1), TRUE))
      return FALSE ;
    if (! hqn_uri_set_field(uri_object, HQN_URI_QUERY, SL("query")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_QUERY, SL("query")))
      return FALSE ;
    hqn_uri_free(&uri_object) ;

    /* no query, but there is a fragment test */
    if (! hqn_uri_parse(uri_context, &uri_object, SL(SET_QUERY_TEST_2), TRUE))
      return FALSE ;
    if (! hqn_uri_set_field(uri_object, HQN_URI_QUERY, SL("query")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_QUERY, SL("query")))
      return FALSE ;
    hqn_uri_free(&uri_object) ;

    /* same length query test */
    if (! hqn_uri_parse(uri_context, &uri_object, SL(SET_FIELD_TEST), TRUE))
      return FALSE ;
    if (! hqn_uri_set_field(uri_object, HQN_URI_QUERY, SL("Z")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_QUERY, SL("Z")))
      return FALSE ;
    hqn_uri_free(&uri_object) ;

    /* longer query test */
    if (! hqn_uri_parse(uri_context, &uri_object, SL(SET_FIELD_TEST), TRUE))
      return FALSE ;
    if (! hqn_uri_set_field(uri_object, HQN_URI_QUERY, SL("Zzzz")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_QUERY, SL("Zzzz")))
      return FALSE ;
    hqn_uri_free(&uri_object) ;

    /* PATH TESTS */

#define SET_PATH_TEST_1 "http://a"
#define SET_PATH_TEST_2 "http://a/b/c/d;p"
    /* no path test */
    if (! hqn_uri_parse(uri_context, &uri_object, SL(SET_PATH_TEST_1), TRUE))
      return FALSE ;
    if (! hqn_uri_set_field(uri_object, HQN_URI_PATH, SL("/a/b/c/d/e/f/g")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_PATH, SL("/a/b/c/d/e/f/g")))
      return FALSE ;
    hqn_uri_free(&uri_object) ;

    /* path test */
    if (! hqn_uri_parse(uri_context, &uri_object, SL(SET_PATH_TEST_2), TRUE))
      return FALSE ;
    if (! hqn_uri_set_field(uri_object, HQN_URI_PATH, SL("/Z/z/z/z")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_PATH, SL("/Z/z/z/z")))
      return FALSE ;
    hqn_uri_free(&uri_object) ;

    /* path test with query and fragment */
    if (! hqn_uri_parse(uri_context, &uri_object, SL(SET_FIELD_TEST), TRUE))
      return FALSE ;
    if (! hqn_uri_set_field(uri_object, HQN_URI_PATH, SL("/Z/z")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_PATH, SL("/Z/z")))
      return FALSE ;
    hqn_uri_free(&uri_object) ;

    /* NAME TESTS */

#define SET_NAME_TEST_1 "http://a"
#define SET_NAME_TEST_2 "http://a/b/c/d;p?q#frag"
    /* no name test - set just path */
    if (! hqn_uri_parse(uri_context, &uri_object, SL(SET_NAME_TEST_1), TRUE))
      return FALSE ;
    if (! hqn_uri_set_field(uri_object, HQN_URI_NAME, SL("/a/b/c/d/e/f/g")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_NAME, SL("/a/b/c/d/e/f/g")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_PATH, SL("/a/b/c/d/e/f/g")))
      return FALSE ;
    hqn_uri_free(&uri_object) ;

    /* no name test - set path and query */
    if (! hqn_uri_parse(uri_context, &uri_object, SL(SET_NAME_TEST_1), TRUE))
      return FALSE ;
    if (! hqn_uri_set_field(uri_object, HQN_URI_NAME, SL("/a/b/c/d/e/f/g?query")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_NAME, SL("/a/b/c/d/e/f/g?query")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_PATH, SL("/a/b/c/d/e/f/g")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_QUERY, SL("query")))
      return FALSE ;
    hqn_uri_free(&uri_object) ;

    /* no name test - set path, query and fragment  */
    if (! hqn_uri_parse(uri_context, &uri_object, SL(SET_NAME_TEST_1), TRUE))
      return FALSE ;
    if (! hqn_uri_set_field(uri_object, HQN_URI_NAME, SL("/a/b/c/d/e/f/g?query#frag")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_NAME, SL("/a/b/c/d/e/f/g?query#frag")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_PATH, SL("/a/b/c/d/e/f/g")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_QUERY, SL("query")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_FRAGMENT, SL("frag")))
      return FALSE ;
    hqn_uri_free(&uri_object) ;

    /* name test - set just path */
    if (! hqn_uri_parse(uri_context, &uri_object, SL(SET_NAME_TEST_2), TRUE))
      return FALSE ;
    if (! hqn_uri_set_field(uri_object, HQN_URI_NAME, SL("/a/b/c/d/e/f/g")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_NAME, SL("/a/b/c/d/e/f/g")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_PATH, SL("/a/b/c/d/e/f/g")))
      return FALSE ;
    hqn_uri_free(&uri_object) ;

    /* name test - set path and query */
    if (! hqn_uri_parse(uri_context, &uri_object, SL(SET_NAME_TEST_2), TRUE))
      return FALSE ;
    if (! hqn_uri_set_field(uri_object, HQN_URI_NAME, SL("/a/b/c/d/e/f/g?query")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_NAME, SL("/a/b/c/d/e/f/g?query")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_PATH, SL("/a/b/c/d/e/f/g")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_QUERY, SL("query")))
      return FALSE ;
    hqn_uri_free(&uri_object) ;

    /* name test - set path, query and fragment  */
    if (! hqn_uri_parse(uri_context, &uri_object, SL(SET_NAME_TEST_2), TRUE))
      return FALSE ;
    if (! hqn_uri_set_field(uri_object, HQN_URI_NAME, SL("/a/b/c/d/e/f/g?query#frag")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_NAME, SL("/a/b/c/d/e/f/g?query#frag")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_PATH, SL("/a/b/c/d/e/f/g")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_QUERY, SL("query")))
      return FALSE ;
    if (! test_get_field(uri_object, HQN_URI_FRAGMENT, SL("frag")))
      return FALSE ;
    hqn_uri_free(&uri_object) ;

  }

  return TRUE ;
}

#endif /* defined(DEBUG_BUILD) */

/* ============================================================================
* Log stripped */
