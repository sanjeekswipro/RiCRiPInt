/* ============================================================================
 * $HopeName: HQNuri!src:hqnuri.c(EBDSDK_P.1) $
 * $Id: src:hqnuri.c,v 1.9.4.1.1.1 2013/12/19 11:24:42 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *Global Graphics Software Ltd. Confidential Information.
 *
 * -------------------------------------
 * URI grammar, extracted from RFC 2396 appendix A. Character BNF excluded.
 *
 * A. Collected BNF for URI
 *
 *    URI-reference = [ absoluteURI | relativeURI ] [ "#" fragment ]
 *    absoluteURI   = scheme ":" ( hier_part | opaque_part )
 *    relativeURI   = ( net_path | abs_path | rel_path ) [ "?" query ]
 *    hier_part     = ( net_path | abs_path ) [ "?" query ]
 *    opaque_part   = uric_no_slash *uric
 *    uric_no_slash = unreserved | escaped | ";" | "?" | ":" | "@" |
 *                    "&" | "=" | "+" | "$" | ","
 *    net_path      = "//" authority [ abs_path ]
 *    abs_path      = "/"  path_segments
 *    rel_path      = rel_segment [ abs_path ]
 *    rel_segment   = 1*( unreserved | escaped |
 *                        ";" | "@" | "&" | "=" | "+" | "$" | "," )
 *    scheme        = alpha *( alpha | digit | "+" | "-" | "." )
 *    authority     = host | reg_name
 *    reg_name      = 1*( unreserved | escaped | "$" | "," |
 *                        ";" | ":" | "@" | "&" | "=" | "+" )
 *    host        = [ [ userinfo "@" ] hostport ]
 *    userinfo      = *( unreserved | escaped |
 *                       ";" | ":" | "&" | "=" | "+" | "$" | "," )
 *    hostport      = host [ ":" port ]
 *    host          = hostname | IPv4address
 *    hostname      = *( domainlabel "." ) toplabel [ "." ]
 *    domainlabel   = alphanum | alphanum *( alphanum | "-" ) alphanum
 *    toplabel      = alpha | alpha *( alphanum | "-" ) alphanum
 *    IPv4address   = 1*digit "." 1*digit "." 1*digit "." 1*digit
 *    port          = *digit
 *    path          = [ abs_path | opaque_part ]
 *    path_segments = segment *( "/" segment )
 *    segment       = *pchar *( ";" param )
 *    param         = *pchar
 *    pchar         = unreserved | escaped |
 *                    ":" | "@" | "&" | "=" | "+" | "$" | ","
 *    query         = *uric
 *    fragment      = *uric
 *
 * Modification history is at the end of this file.
 * ============================================================================
 */
/**
 * \file
 * \brief Implementation of HQN URI interface.
 */

#include "hqnuri.h"
#include "std.h"       /* For HQASSERT, HQFAIL */
#include "hqmemcmp.h"  /* HqMemCmp */
#include "hqmemcpy.h"  /* HqMemCpy */

/* Macros to differentiate various character types. Extracted from RFC 2396.
 */
#define IS_ALPHA(x) (IS_LOWALPHA(x) || IS_UPALPHA(x))

#define IS_LOWALPHA(x) (((x) >= 'a') && ((x) <= 'z'))

#define IS_UPALPHA(x) (((x) >= 'A') && ((x) <= 'Z'))

#define IS_DIGIT(x) (((x) >= '0') && ((x) <= '9'))

#define IS_ALPHANUM(x) (IS_ALPHA(x) || IS_DIGIT(x))

#define IS_HEX(x) ((IS_DIGIT(x)) || (((x) >= 'a') && ((x) <= 'f')) || \
  (((x) >= 'A') && ((x) <= 'F')))

#define IS_MARK(x) (((x) == '-') || ((x) == '_') || ((x) == '.') || \
  ((x) == '!') || ((x) == '~') || ((x) == '*') || ((x) == '\'') || \
  ((x) == '(') || ((x) == ')') || ((x) == '[') || ((x) == ']'))

#define IS_RESERVED(x) (((x) == ';') || ((x) == '/') || ((x) == '?') || \
  ((x) == ':') || ((x) == '@') || ((x) == '&') || ((x) == '=') || \
  ((x) == '+') || ((x) == '$') || ((x) == ','))

#define IS_UNRESERVED(x) (IS_ALPHANUM(x) || IS_MARK(x))

#define IS_SCHEME(x) ((IS_ALPHA(x)) || (IS_DIGIT(x)) || \
  ((x) == '+') || ((x) == '-') || ((x) == '.'))

#define IS_ESCAPED(p, len) ((len > 2) && (*(p) == '%') && (IS_HEX((p)[1])) && \
  (IS_HEX((p)[2])))

#define IS_URIC_NO_SLASH(p, len) ((len > 0) && ((IS_UNRESERVED(*(p))) || \
  (IS_ESCAPED(p, len)) || \
  ((*(p) == ';')) || ((*(p) == '?')) || ((*(p) == ':')) || \
  ((*(p) == '@')) || ((*(p) == '&')) || ((*(p) == '=')) || \
  ((*(p) == '+')) || ((*(p) == '$')) || ((*(p) == ','))))

#define IS_PCHAR(p, len) ((len > 0) && ((IS_UNRESERVED(*(p))) || \
  (IS_ESCAPED(p, len)) || \
  ((*(p) == ':')) || ((*(p) == '@')) || ((*(p) == '&')) || \
  ((*(p) == '=')) || ((*(p) == '+')) || ((*(p) == '$')) || \
  ((*(p) == ','))))

#define IS_SEGMENT(p, len) ((len > 0) && ((IS_UNRESERVED(*(p))) || \
  (IS_ESCAPED(p, len)) || \
  ((*(p) == ';')) || ((*(p) == '@')) || ((*(p) == '&')) || \
  ((*(p) == '=')) || ((*(p) == '+')) || ((*(p) == '$')) || \
  ((*(p) == ','))))

#define IS_REG_NAME(p, len) ((len > 0) && ((IS_UNRESERVED(*(p))) || \
  (IS_ESCAPED(p, len)) || \
  ((*(p) == '$')) || ((*(p) == ',')) || ((*(p) == ';')) || \
  ((*(p) == ':')) || ((*(p) == '@')) || ((*(p) == '&')) || \
  ((*(p) == '=')) || ((*(p) == '+'))))

#define IS_USERINFO(p, len) ((len > 0) && ((IS_UNRESERVED(*(p))) || \
  (IS_ESCAPED(p, len)) || \
  ((*(p) == ';')) || ((*(p) == ':')) || ((*(p) == '&')) || \
  ((*(p) == '=')) || ((*(p) == '+')) || ((*(p) == '$')) || \
  ((*(p) == ','))))

#define IS_URIC(p, len) ((len > 0) && ((IS_UNRESERVED(*(p))) || \
  (IS_ESCAPED(p, len)) || \
  (IS_RESERVED(*(p)))))

#define IS_UNWISE(p, len) \
  ((len > 0) && (((*(p) == '{')) || ((*(p) == '}')) || ((*(p) == '|')) || \
   ((*(p) == '\\')) || ((*(p) == '^')) || \
   ((*(p) == '`'))))

/* Skip to next pointer char, handle escaped sequences. */
#define NEXT(p, len) \
  (((len) > 2 && *(p) == '%') ? ((p) += 3, (len) -= 3 )  : ((p)++, (len)-- ))

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
  uint8 *opaque ;        /**< opaque part */
  uint32 opaque_len ;
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

/* scheme = alpha *( alpha | digit | "+" | "-" | "." )
 */
static HqBool uri_parse_scheme(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *scheme ;
  uint32 scheme_len = 0 ;

  /* skip white space */
  while (*str_len > 0 && *(*str) == ' ') {
    (*str_len)-- ;
    (*str)++ ;
  }

  if (*str_len == 0)
    return FALSE ;

  scheme = *str ;

  /* must have at least one char */
  if (*str_len > 0 && IS_ALPHA(*(*str))) {
    (*str_len)-- ;
    (*str)++ ;
    scheme_len++ ;
  } else {
    return FALSE ;
  }

  /* scan potential scheme */
  while (*str_len > 0 && (IS_SCHEME(*(*str)))) {
    (*str_len)-- ;
    (*str)++ ;
    scheme_len++ ;
  }

  /* if data remains and next is a colon - it really is scheme */
  if (*str_len > 0 && *(*str) == ':') {
    uri_object->scheme = scheme ;
    uri_object->scheme_len = scheme_len ;
  } else {
    return FALSE ;
  }

  return TRUE ;
}

/*query = *uric */
static HqBool uri_parse_query(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *keep ;
  uint32 keep_len ;

  if (*str_len == 0)
    return FALSE ;

  keep = *str ;
  keep_len = *str_len ;

  while (*str_len > 0 && (IS_URIC(*str, *str_len) || IS_UNWISE(*str, *str_len)))
    NEXT(*str, *str_len) ;

  uri_object->query = keep ;
  uri_object->query_len = keep_len - *str_len ;

  return TRUE ;
}

/* host        = [ [ userinfo "@" ] hostport ]
 * userinfo      = *( unreserved | escaped |
 *                       ";" | ":" | "&" | "=" | "+" | "$" | "," )
 * hostport      = host [ ":" port ]
 * host          = hostname | IPv4address
 * hostname      = *( domainlabel "." ) toplabel [ "." ]
 * domainlabel   = alphanum | alphanum *( alphanum | "-" ) alphanum
 * toplabel      = alpha | alpha *( alphanum | "-" ) alphanum
 * IPv4address   = 1*digit "." 1*digit "." 1*digit "." 1*digit
 * port          = *digit
 */
static HqBool uri_parse_host(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *keep ;
  uint32 keep_len ;
  uint32 numclass = 0 ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;

#define NUMIP 4

  keep = *str ;
  keep_len = *str_len ;

  /* is there a userinfo? */
  while (IS_USERINFO(*str, *str_len))
    NEXT(*str, *str_len) ;

  if (*str_len > 0 && *(*str) == '@') {
    uri_object->user = keep ;
    uri_object->user_len = CAST_PTRDIFFT_TO_UINT32(keep - (*str)) ;
    ++(*str) ; /* consume @ */
    --(*str_len) ;
  } else {
    /* reset scan  - no userinfo */
    *str = keep ;
    *str_len = keep_len ;
  }

  /* This can be empty in the case where there is no host user@/ or /// */
  if (*str_len > 0 && *(*str) == '/')
    return TRUE ;

  keep = *str ;
  keep_len = *str_len ;

  /* Host part of hostport can derive either an IPV4 address
   * or an unresolved name. Check the IP first.
   * 123.123.123.123
   */
  if (*str_len > 0) {
    for (numclass = 0; numclass < NUMIP; ++numclass) {
      if (*str_len > 0 && *(*str) == '.')
        return FALSE ; /* e.g. http://.12/ or http://18.29..30/ */

      /* Although this is not valid IP, the spec allows any number of
         digits. */
      while(IS_DIGIT(*(*str))) {
        ++(*str) ;
        --(*str_len) ;
      }
      if (numclass == (NUMIP - 1)) /* have found all IP classes */
        continue ;

      if (*str_len == 0 || *(*str) != '.') /* eg: 123Z */
        break ;

      /* we have another dot, consume eg: 123. */
      ++(*str) ;
      --(*str_len) ;
    }
  }

  /* Could have consumed something which looks like an IP, but has more
     numbers. Eg: could have 123Z or 123.123.x or 123.123.123.123.a */

  if (numclass < NUMIP ||
      ((*str_len > 0 && *(*str) == '.') && ++(*str) && --(*str_len)) ||
      (*str_len > 0 && IS_ALPHA(*(*str))) ) {

    /* maybe host_name */
    if (str_len > 0 && ! IS_ALPHANUM(*(*str)))
      return FALSE ; /* e.g. http://something.$oft or 123.123.123.123./ */

    for (;;) {
      --(*str_len) ;
      ++(*str) ;
      while (*str_len > 0 && IS_ALPHANUM(*(*str))) {
        --(*str_len) ;
        ++(*str) ;
      }
      if (*str_len > 0 && *(*str) == '-') {
        ++(*str_len) ; /* Look back one */
        --(*str) ;
        if (*str_len > 0 && *(*str) == '.')
          return FALSE ; /* e.g. http://something.-soft */

        --(*str_len) ;
        ++(*str) ;
        continue ;
      }
      if (str_len > 0 && *(*str) == '.') {
        ++(*str_len) ; /* Look back one */
        --(*str) ;
        if (*str_len > 0 && *(*str) == '-')
          return FALSE ; /* e.g. http://something-.soft */
        if (*str_len > 0 && *(*str) == '.')
          return FALSE ; /* e.g. http://something..soft */
        if (*str_len > 0 && ! IS_ALPHANUM(*(*str)))
          return FALSE ;

        --(*str_len) ; /* Move forward again */
        ++(*str) ;
        continue ;
      }
      break ;
    }
  }

  uri_object->host = keep ;
  uri_object->host_len = keep_len - *str_len ;

  /* Either at the end of the string, a /, question mark or we have a
     port. */
  if (*str_len > 0 && *(*str) == ':') {
    --(*str_len) ; /* consume : */
    ++(*str) ;

    keep = *str ;
    keep_len = *str_len ;

    if (*str_len > 0 && IS_DIGIT(*(*str))) {
      while (*str_len > 0 && IS_DIGIT(*(*str))) {
        --(*str_len) ; /* consume : */
        ++(*str) ;
      }
    }

    uri_object->port = keep ;
    uri_object->port_len = keep_len - *str_len ;
  }

  return TRUE ;
}

/*
 * authority = host | reg_name
 * host    = [ [ userinfo "@" ] hostport ]
 * reg_name  = 1*( unreserved | escaped | "$" | "," | ";" | ":" |
 *                        "@" | "&" | "=" | "+" )
 */
static HqBool uri_parse_authority(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *keep = *str ;
  uint32 keep_len = *str_len ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;

  if (! uri_parse_host(uri_object, str, str_len)) {
    /* not a host, look for a reg_name */
    *str = keep ;
    *str_len = keep_len ;
  } else {
   if ((*str_len == 0) || /* consumed the entire URI */
       (*str_len > 0 &&   /* or we have a / or ? */
       (*(*str) == '/' || *(*str) == '?')))
     return TRUE ;
   else
     return FALSE ;
  }

  if (str_len > 0 && ! IS_REG_NAME(*str, *str_len))
    return FALSE ;

  NEXT(*str, *str_len) ;

  while (IS_REG_NAME(*str, *str_len))
    NEXT(*str, *str_len) ;

  uri_object->authority = keep ;
  uri_object->authority_len = keep_len - *str_len ;

  return TRUE ;
}

/* path_segments = segment *( "/" segment )
 * segment       = *pchar *( ";" param )
 * param         = *pchar
 */
static HqBool uri_parse_path_segments(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *keep ;
  uint32 keep_len ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;

  if (*str_len == 0)
    return FALSE ;

  keep = *str ;
  keep_len = *str_len ;

  for (;;) {
    while (*str_len > 0 && (IS_PCHAR(*str, *str_len) || IS_UNWISE(*str, *str_len)))
      NEXT(*str, *str_len) ;
    while (*str_len > 0 && *(*str) == ';') {
      --(*str_len) ;
      ++(*str) ;
    }
    while (*str_len > 0 && (IS_PCHAR(*str, *str_len) || IS_UNWISE(*str, *str_len)))
      NEXT(*str, *str_len) ;

    if (*str_len == 0 || *(*str) != '/')
      break;
    --(*str_len) ;
    ++(*str) ;
  }

  /* If there is path stuff in the field, it will be from a relative parse. */
  if (uri_object->path == NULL) {
    uri_object->path = keep ;
    uri_object->path_len = keep_len - *str_len ;
  } else {
    HQASSERT(uri_object->path_len != 0, "path_len is zero") ;
    uri_object->path_len += (keep_len - *str_len) ;
  }

  return TRUE ;
}

/* rel_segment = 1*( unreserved | escaped | ";" | "@" | "&" | "=" |
 *                          "+" | "$" | "," )
 */
static HqBool uri_parse_rel_path_segments(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *keep ;
  uint32 keep_len ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;

  if (*str_len == 0)
    return FALSE ;

  keep = *str ;
  keep_len = *str_len ;

  if (! (IS_SEGMENT(*str, *str_len) || IS_UNWISE(*str, *str_len)))
    return FALSE ;

  NEXT(*str, *str_len) ;

  while (*str_len > 0 && (IS_SEGMENT(*str, *str_len) || IS_UNWISE(*str, *str_len)))
    NEXT(*str, *str_len) ;

  uri_object->path = keep ;
  uri_object->path_len = keep_len - *str_len ;

  return TRUE ;
}

/* hier_part = ( net_path | abs_path ) [ "?" query ]
 * abs_path = "/"  path_segments
 * net_path = "//" authority [ abs_path ]
 */
static HqBool uri_parse_hier(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  HQASSERT(uri_object != NULL, "uri_object is NULL") ;

  /* if we have a net_path */
  if (*str_len > 1 && *(*str) == '/' && *((*str) + 1) == '/') {
    (*str) += 2 ;
    (*str_len) -= 2 ;

    if (! uri_parse_authority(uri_object, str, str_len))
      return FALSE ;

    if (*str_len > 0 && *(*str) == '/') {
      uri_object->path  = *str ;
      uri_object->path_len  = 1 ;
      ++(*str) ;
      --(*str_len) ;

      /* empty path, example: "http://abc/" */
      if (*str_len == 0)
        return TRUE ;

      if (! uri_parse_path_segments(uri_object, str, str_len)) {
        uri_object->path  = NULL ;
        uri_object->path_len  = 0 ;
        return FALSE ;
      }
    }

  /* We have an abs_path */
  } else if (*str_len > 0 && *(*str) == '/') {
    uri_object->path  = *str ;
    uri_object->path_len  = 1 ;
    ++(*str) ;
    --(*str_len) ;

    if (! uri_parse_path_segments(uri_object, str, str_len)) {
      uri_object->path  = NULL ;
      uri_object->path_len  = 0 ;
      return FALSE ;
    }

  } else {
    return FALSE ;
  }

  if (*str_len > 0 && *(*str) == '?') {
    ++(*str) ;
    --(*str_len) ;

    if (! uri_parse_query(uri_object, str, str_len))
      return FALSE ;
  }

  return TRUE ;
}

/* opaque_part = uric_no_slash *uric
 */
static HqBool uri_parse_opaque(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *keep ;
  uint32 keep_len ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;

  if (*str_len == 0)
    return FALSE ;

  keep = *str ;
  keep_len = *str_len ;

  if (! (IS_URIC_NO_SLASH(*str, *str_len) || IS_UNWISE(*str, *str_len)))
    return FALSE ;

  NEXT(*str, *str_len) ;

  while (IS_URIC(*str, *str_len) || IS_UNWISE(*str, *str_len))
    NEXT(*str, *str_len) ;

  uri_object->opaque = keep ;
  uri_object->opaque_len = keep_len - *str_len ;

  return TRUE ;
}


/* fragment = *uric
 */
static HqBool uri_parse_fragment(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  uint8 *keep ;
  uint32 keep_len ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;

  if (*str_len == 0)
    return FALSE ;

  keep = *str ;
  keep_len = *str_len ;

  while (IS_URIC(*str, *str_len) || IS_UNWISE(*str, *str_len))
    NEXT(*str, *str_len) ;

  uri_object->fragment = keep ;
  uri_object->fragment_len = keep_len - *str_len ;

  return TRUE ;
}

/* absoluteURI = scheme ":" ( hier_part | opaque_part )
 */
static HqBool hqn_uri_absolute(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  HQASSERT(uri_object != NULL, "uri_object is NULL") ;

  if (*str_len == 0)
    return FALSE ;

  /* scheme */
  if (! uri_parse_scheme(uri_object, str, str_len))
    return FALSE ;

  HQASSERT(str_len > 0 && *(*str) == ':', "parse scheme odd behaviour") ;

  /* consume : */
  ++(*str) ;
  --(*str_len) ;

  /* hier */
  if (*str_len > 0 && *(*str) == '/') {
    if (! uri_parse_hier(uri_object, str, str_len))
      return FALSE ;

  /* opaque */
  } else {
    if (! uri_parse_opaque(uri_object, str, str_len))
      return FALSE ;
  }

  return TRUE ;
}

/* relativeURI = ( net_path | abs_path | rel_path ) [ "?" query ]
 */
static HqBool hqn_uri_relative(
      hqn_uri_t *uri_object,
      uint8 **str,
      uint32 *str_len)
{
  HQASSERT(uri_object != NULL, "uri_object is NULL") ;

  if (*str_len == 0)
    return FALSE ;

  /* if we have a net_path */
  if (*str_len > 1 && *(*str) == '/' && *((*str) + 1) == '/') {
    (*str) += 2 ;
    (*str_len) -= 2 ;

    if (! uri_parse_authority(uri_object, str, str_len))
      return FALSE ;

    if (*str_len > 0 && *(*str) == '/') {
      ++(*str) ;
      --(*str_len) ;

      if (! uri_parse_path_segments(uri_object, str, str_len))
        return FALSE ;
    }

  /* We have an abs_path */
  } else if (*str_len > 0 && *(*str) == '/') {
    /*
    ++(*str) ;
    --(*str_len) ;
    */
    if (! uri_parse_path_segments(uri_object, str, str_len))
      return FALSE ;

  } else if (*str_len == 0 || (*(*str) != '#' && *(*str) != '?')) {

    if (! uri_parse_rel_path_segments(uri_object, str, str_len))
      return FALSE ;

    if (*str_len > 0 && *(*str) == '/') {
      ++(*str) ;
      --(*str_len) ;

      /* count this slash */
      if (uri_object->path != NULL) {
        HQASSERT(uri_object->path_len != 0, "path_len is zero") ;
        uri_object->path_len += 1 ;
      }

      if (*str_len > 0) {
        if (! uri_parse_path_segments(uri_object, str, str_len))
          return FALSE ;
      }
    }
  }

  if (*str_len > 0 && *(*str) == '?') {
    ++(*str) ;
    --(*str_len) ;

    if (! uri_parse_query(uri_object, str, str_len))
      return FALSE ;
  }

  return TRUE ;
}

/* Applies the 5 normalization steps to a path string--that is, RFC 2396
 * Section 5.2, steps 6.c through 6.g.
 *
 * Normalisation occurs directly on the string, no new allocation is done.
 */
static HqBool uri_normalise_path(
      uint8 **str,
      uint32 *str_len)
{
  uint8 *out, *new_path, *segment_start ;
  uint32 out_len, new_path_len, segment_len ;

  HQASSERT(str != NULL, "str is NULL") ;
  HQASSERT(*str != NULL, "str pointer is NULL") ;
  HQASSERT(*str_len > 0, "str_len is not greater than zero") ;

  if (str == NULL || *str == NULL) /* safe asserts */
    return FALSE ;

  out = *str ;
  out_len = 0 ;
  new_path = out ;

  if (*str_len == 0)
    return FALSE ;

  /* analyze each segment in sequence for cases (c) and (d). */
  while (*str_len > 0) {
    /* c) All occurrences of "./", where "." is a complete path segment,
          are removed from the buffer string. */
    if (*str_len > 1 && *(*str)== '.' && *((*str) + 1) == '/') {
      (*str) += 2 ;
      (*str_len) -= 2 ;

      /* '//' normalization should be done at this point too */
      while (*str_len > 0 && *(*str) == '/') {
        ++(*str) ;
        --(*str_len) ;
      }
      continue ;
    }

    /* otherwise keep the segment.  */
    while (*str_len > 0 && *(*str) != '/') {
      *(out++) = *(*str) ; /* copy char */
      out_len++ ;

      ++(*str) ; /* move along */
      --(*str_len) ;
    }

    /* nomalize // */
    while (*str_len > 1 && *(*str) == '/' && *((*str) + 1) == '/') {
      ++(*str) ; /* move along */
      --(*str_len) ;
    }

    if (*str_len > 0) {
      *(out++) = *(*str) ; /* copy char */
      out_len++ ;
      ++(*str) ; /* move along */
      --(*str_len) ;
    }
  }
  new_path_len = out_len ;

  /* d) If the buffer string ends with "." as a complete path segment,
        that "." is removed. */
  if (new_path_len > 1 &&
      new_path[new_path_len - 2] == '/' &&
      new_path[new_path_len - 1] == '.') {
    new_path_len-- ;
  }

  /* analyze each segment in sequence for cases (e) and (f). */

  /* start at the beginning of the new path */
  *str = new_path ;
  *str_len = new_path_len ;

  /* e) All occurrences of "<segment>/../", where <segment> is a
        complete path segment not equal to "..", are removed from the
        buffer string.  Removal of these path segments is performed
        iteratively, removing the leftmost matching pattern on each
        iteration, until no matching pattern remains. */

  segment_start = *str ;
  segment_len = 0 ;

  while (*str_len > 0) {
    /* find end of segment */
    while (*str_len > 0 && *(*str) != '/') {
      ++(*str) ; /* move along */
      --(*str_len) ;
      segment_len++ ;
    }

    if (*str_len > 0) {

      /* skip the / */
      ++(*str) ; /* move along */
      --(*str_len) ;

      /* if "../" or ".." and at end of buffer */
      if ((*str_len > 2 && (*str)[0] == '.' && (*str)[1] == '.' && (*str)[2] == '/') ||
          (*str_len == 2 && (*str)[0] == '.' && (*str)[1] == '.')) {

        if (! (segment_len == 2 &&
               segment_start[0] == '.' &&
               segment_start[1] == '.')) {

          if (*str_len == 2) {
            (*str) += 2; /* move along 2 */
            (*str_len) -= 2 ;
            new_path_len -= 3 ; /* "/.." */
          } else {
            /* We have too many "../" */
            if (segment_len == 0) {
              (*str) += 2; /* move along 2 */
              (*str_len) -= 2 ;
              new_path_len -= 3 ; /* "/.." */
            } else {
              (*str) += 3; /* move along 3 */
              (*str_len) -= 3 ;
              new_path_len -= 4 ; /* "/../" */
            }
          }

          new_path_len -= segment_len ;

          /* remove "<segment>/../" */
          while (*str_len > 0) {
            *segment_start++ = *(*str)++ ;
            --(*str_len) ;
          }
          /* start again */
          *str = new_path ;
          *str_len = new_path_len ;
        }
      }
    }

    /* next segment */
    segment_start = *str ;
    segment_len = 0 ;
  }

  *str = new_path ;
  *str_len = new_path_len ;

  /* f) If the buffer string ends with "<segment>/..", where <segment>
        is a complete path segment not equal to "..", that
        "<segment>/.." is removed. */

  /* if "/.." */
  if (*str_len > 2 && (*str)[*str_len - 3] == '/' &&
      (*str)[*str_len - 2] == '.' && (*str)[*str_len - 1] == '.') {

    /* if not "<something>/../.." or "../.." */
    if (! ((*str_len > 5 &&
            (*str)[*str_len - 6] == '/' &&
            (*str)[*str_len - 5] == '.' &&
            (*str)[*str_len - 4] == '.') ||
           (*str_len == 5 &&
            (*str)[*str_len - 5] == '.' &&
            (*str)[*str_len - 4] == '.')) ) {

      /* just before the "/.." */
      *str = *str + (*str_len - 3) ;
      *str_len -= 3 ;

      /* search backwards for first / or beginning of string */
      while (*str_len > 0 && *(*str) != '/') {
        --(*str_len) ;
        --(*str) ;

        /* <segment>/.. removed */
        new_path_len-- ;
      }
    }
  }

  *str = new_path ;
  *str_len = new_path_len ;

  /* g) If the resulting buffer string still begins with one or more
        complete path segments of "..", then the reference is
        considered to be in error. Implementations may handle this
        error by retaining these components in the resolved path (i.e.,
        treating them as part of the final URI), by removing them from
        the resolved path (i.e., discarding relative levels above the
        root), or by avoiding traversal of the reference. */

  /* we discard them from the final path - until our testsuite is updated. */

  /* ../ */
  if (*str_len > 2 &&
      *(*str) == '.' &&
      *(*str + 1) == '.' &&
      *(*str + 2) == '/') {
    (*str) += 2 ; /* move along 2 */
    (*str_len) -= 2 ;
  }
  /* /../ */
  while (*str_len > 3 &&
         *(*str) == '/' &&
         *(*str + 1) == '.' &&
         *(*str + 2) == '.' &&
         *(*str + 3) == '/') {
    (*str) += 3 ; /* move along 3 */
    (*str_len) -= 3 ;
  }
  new_path = *str ;
  new_path_len = *str_len ;

  return TRUE ;
}

static HqBool uri_copy_field(
      hqn_uri_t *to_uri,
      hqn_uri_t *from_uri,
      uint32 field)
{
  hqn_uri_context_t *uri_context ;
  uint8 *new_str, **to_str, *from_str ;
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

  } else if (field == HQN_URI_OPAQUE) {
    from_str = from_uri->opaque ;
    from_len = from_uri->opaque_len ;
    to_str = &(to_uri->opaque) ;
    to_len = &(to_uri->opaque_len) ;

  } else if (field == HQN_URI_AUTHORITY) {
    from_str = from_uri->authority ;
    from_len = from_uri->authority_len ;
    to_str = &(to_uri->authority) ;
    to_len = &(to_uri->authority_len) ;

  } else if (field == HQN_URI_HOST) {
    from_str = from_uri->host ;
    from_len = from_uri->host_len ;
    to_str = &(to_uri->host) ;
    to_len = &(to_uri->host_len) ;

  } else if (field == HQN_URI_USER) {
    from_str = from_uri->user ;
    from_len = from_uri->user_len ;
    to_str = &(to_uri->user) ;
    to_len = &(to_uri->user_len) ;

  } else if (field == HQN_URI_PORT) {
    from_str = from_uri->port ;
    from_len = from_uri->port_len ;
    to_str = &(to_uri->port) ;
    to_len = &(to_uri->port_len) ;

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
  HQASSERT(from_str != NULL, "from_str is NULL") ;
  HQASSERT(from_len > 0, "from_len is not greater than zero") ;

  new_str = from_str ;

  *to_str = new_str ;
  *to_len = from_len ;
  return TRUE ;
}

/* ============================================================================
 * Public interface.
 */

static HqBool hqn_uri_reparse(
      hqn_uri_t *uri_object)
{
  uint8 *upto ;
  uint32 upto_len ;

  HQASSERT(uri_object != NULL, "uri_object is NULL") ;

  upto = uri_object->original ;
  upto_len = uri_object->original_len ;

  if (! hqn_uri_absolute(uri_object, &upto, &upto_len)) {
    /* not absolute - try relative - start again */
    upto = uri_object->original ;
    upto_len = uri_object->original_len ;

    /* clear all fields - some may have been filled in while looking
       for an absolute URI */
    uri_clean(uri_object) ;

    if (! hqn_uri_relative(uri_object, &upto, &upto_len))
      return FALSE ;
  }

  /* fragment */
  if (upto_len > 0 && *upto == '#') {
    /* consume # */
    ++upto ;
    --upto_len ;

    if (! uri_parse_fragment(uri_object, &upto, &upto_len))
      return FALSE ;
  }

  /* Did we consume the entire URI? */
  if (upto_len != 0)
    return FALSE ;

  return TRUE ;
}

HqBool hqn_uri_copy_2396(
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

  if (! hqn_uri_reparse(new_uri)) {
    hqn_uri_free(&new_uri) ;
    return FALSE ;
  }

  *to_uri_object = new_uri ;

  return TRUE ;
}

/* URI-reference = [ absoluteURI | relativeURI ] [ "#" fragment ]
 *
 * Parsing is not destructive on original string. When creating the new URI
 * object, copying can be specified.
 */
HqBool hqn_uri_parse_2396(
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

  if (uri_len == 0)
    return FALSE ;

  if (! hqn_uri_new_empty(uri_context, &new_uri))
    return FALSE ;

  /* copy the original string */
  if (copy_string) {
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

  if (! hqn_uri_reparse(new_uri)) {
    hqn_uri_free(&new_uri) ;
    return FALSE ;
  }

  *new_uri_object = new_uri ;
  return TRUE ;
}

HqBool hqn_uri_init_2396(
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

void hqn_uri_finish_2396(
      hqn_uri_context_t **uri_context)
{
  HQASSERT(uri_context != NULL, "uri_context is NULL") ;

  (*uri_context)->mem.f_free(*uri_context) ;
  *uri_context = NULL ;
}

HqBool hqn_uri_new_empty_2396(
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

void hqn_uri_free_2396(
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
  uint8 *new_uri_string ;

  if (uri->scheme_len > 0)
    len += uri->scheme_len + 1 ; /* : */

  if (uri->opaque_len > 0) {
      len += uri->opaque_len ;
  } else {
    if (uri->authority_len > 0) {
      len += uri->authority_len + 2 ; /* //<auth> */
    } else if (uri->host_len > 0) {
      len += uri->host_len + 2 ; /* //<host> */
      if (uri->user_len > 0) {
        len += uri->user_len + 1 ; /* @ */
      }
      if (uri->port_len > 0) {
        len += uri->port_len + 1 ; /* : */
      }
    }
    if (uri->path_len > 0)
      len += uri->path_len ;
    if (uri->query_len > 0)
      len += uri->query_len + 1 ;  /* ? */
  }

  if (uri->fragment_len > 0)
    len += uri->fragment_len + 1 ; /* # */

  new_uri_string = uri_context->mem.f_malloc((size_t)len) ;
  if (new_uri_string == NULL)
    return FALSE ;

  /* Free any previous memory */
  if (uri->copied)
    uri_context->mem.f_free(uri->original) ;

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

  if (uri->opaque_len > 0) {
      HqMemCpy(new_uri_string, uri->opaque, uri->opaque_len) ;
      uri->opaque = new_uri_string ;
      new_uri_string += uri->opaque_len ;
  } else {
    if (uri->authority_len > 0) {
      HqMemCpy(new_uri_string, "//", 2) ;
      new_uri_string += 2 ;
      uri->authority = new_uri_string ;
      HqMemCpy(new_uri_string, uri->authority, uri->authority_len) ;
      new_uri_string += uri->authority_len ;

    } else if (uri->host_len > 0) {
      HqMemCpy(new_uri_string, "//", 2) ;
      new_uri_string += 2 ;

      if (uri->user_len > 0) {
        HqMemCpy(new_uri_string, uri->user, uri->user_len) ;
        uri->user = new_uri_string ;
        new_uri_string += uri->user_len ;
        HqMemCpy(new_uri_string, "@", 1) ;
        new_uri_string += 1 ;
      }

      HqMemCpy(new_uri_string, uri->host, uri->host_len) ;
      uri->host = new_uri_string ;
      new_uri_string += uri->host_len ;

      if (uri->port_len > 0) {
        HqMemCpy(new_uri_string, ":", 1) ;
        new_uri_string += 1 ;
        HqMemCpy(new_uri_string, uri->port, uri->port_len) ;
        uri->port = new_uri_string ;
        new_uri_string += uri->port_len ;
      }
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
  }

  if (uri->fragment_len > 0) {
    HqMemCpy(new_uri_string, "#", 1) ;
    new_uri_string += 1 ;

    HqMemCpy(new_uri_string, uri->fragment, uri->fragment_len) ;
    uri->fragment = new_uri_string ;
    new_uri_string += uri->fragment_len ;
  }
  return TRUE ;
}

/* Computes the final URI of the reference done by checking that the
 * given URI is valid, and building the final URI using the base
 * URI. This is processed according to section 5.2 of the RFC 2396
 *
 * 5.2. Resolving Relative References to Absolute Form
 */
HqBool hqn_uri_rel_parse_2396(
      hqn_uri_context_t *uri_context,
      hqn_uri_t **new_uri_object,
      hqn_uri_t *base_uri_object,
      const uint8 *rel_uri,
      uint32 rel_len)
{
  hqn_uri_t *rel_uri_object ;
  hqn_uri_t *build_uri ;
  uint32 base_path_len, new_path_len, keep_path_len = 0 ;
  /* used when merging */
  uint8 *tmp_path, *base_path, *keep_path = NULL ;
#define TMPBUF_SIZE 1024

/* Hopefully large enough to hold most URI strings. Used as temporary
   storage when calculating an absolute URI given a relative URI and a
   base URI. */
   uint8 temp_buf[TMPBUF_SIZE] ;

  HQASSERT(uri_context != NULL, "uri_context is NULL") ;
  HQASSERT(new_uri_object != NULL, "new_uri_object is NULL") ;
  HQASSERT(rel_uri != NULL, "rel_uri is NULL" ) ;
  HQASSERT(rel_len > 0, "rel_len is not greater than zero") ;

  *new_uri_object = NULL ;

  /* 1) The URI reference is parsed into the potential four components
        and fragment identifier, as described in Section 4.3. */
  if (! hqn_uri_parse(uri_context,
                      &rel_uri_object,
                      rel_uri, rel_len,
                      FALSE /* don't copy string */)) {
    return FALSE ;
  }

  /* 3) If the scheme component is defined, indicating that the
        reference starts with a scheme name, then the reference is
        interpreted as an absolute URI and we are done. Otherwise, the
        reference URI's scheme is inherited from the base URI's scheme
        component. */
  if (rel_uri_object->scheme != NULL || base_uri_object == NULL) {
    *new_uri_object = rel_uri_object ;
    return TRUE ;
  }

  /* We have a base and we have a relative URI, we need to inherit
     appropriately from the base - so we need a new URI to build. The
     rel_uri_object effectively becomes a transient object. */
  if (! hqn_uri_new_empty(uri_context, &build_uri)) {
    hqn_uri_free(&rel_uri_object) ;
    return FALSE ;
  }

  /* 2) If the path component is empty and the scheme, authority, and
       query components are undefined, then it is a reference to the
       current document and we are done.  Otherwise, the reference
       URI's query and fragment components are defined as found (or
       not found) within the URI reference and not inherited from the
       base URI.  */
  if (rel_uri_object->path == NULL &&
      rel_uri_object->scheme == NULL &&
      rel_uri_object->authority == NULL &&
      rel_uri_object->host == NULL &&
      rel_uri_object->query == NULL) {

    if (base_uri_object->scheme != NULL) {
      if (! uri_copy_field(build_uri, base_uri_object, HQN_URI_SCHEME)) {
        hqn_uri_free(&rel_uri_object) ;
        hqn_uri_free(&build_uri) ;
        return FALSE ;
      }

    } else if (base_uri_object->host != NULL) {
      if (! uri_copy_field(build_uri, base_uri_object, HQN_URI_HOST)) {
        hqn_uri_free(&rel_uri_object) ;
        hqn_uri_free(&build_uri) ;
        return FALSE ;
      }
      if (base_uri_object->user != NULL) {
        if (! uri_copy_field(build_uri, base_uri_object, HQN_URI_USER)) {
          hqn_uri_free(&rel_uri_object) ;
          hqn_uri_free(&build_uri) ;
          return FALSE ;
        }
      }
      if (base_uri_object->port != NULL) {
        if (! uri_copy_field(build_uri, base_uri_object, HQN_URI_PORT)) {
          hqn_uri_free(&rel_uri_object) ;
          hqn_uri_free(&build_uri) ;
          return FALSE ;
        }
      }
    }

    if (base_uri_object->path != NULL) {
      if (! uri_copy_field(build_uri, base_uri_object, HQN_URI_PATH)) {
        hqn_uri_free(&rel_uri_object) ;
        hqn_uri_free(&build_uri) ;
        return FALSE ;
      }
    }

    if (rel_uri_object->query != NULL) {
      if (! uri_copy_field(build_uri, rel_uri_object, HQN_URI_QUERY)) {
        hqn_uri_free(&rel_uri_object) ;
        hqn_uri_free(&build_uri) ;
        return FALSE ;
      }
    } else if (base_uri_object->query != NULL) {
      if (! uri_copy_field(build_uri, base_uri_object, HQN_URI_QUERY)) {
        hqn_uri_free(&rel_uri_object) ;
        hqn_uri_free(&build_uri) ;
        return FALSE ;
      }
    }

    if (rel_uri_object->fragment != NULL) {
      if (! uri_copy_field(build_uri, rel_uri_object, HQN_URI_FRAGMENT)) {
        hqn_uri_free(&rel_uri_object) ;
        hqn_uri_free(&build_uri) ;
        return FALSE ;
      }
    }

    goto step_7 ;
  }

  /* 3) If the scheme component is defined, indicating that the
        reference starts with a scheme name, then the reference is
        interpreted as an absolute URI and we are done.  Otherwise,
        the reference URI's scheme is inherited from the base URI's
        scheme component. */

  /* "If scheme component is defined" is handled in step #1 */

  if (base_uri_object->scheme != NULL) {
    if (! uri_copy_field(build_uri, base_uri_object, HQN_URI_SCHEME)) {
      hqn_uri_free(&rel_uri_object) ;
      hqn_uri_free(&build_uri) ;
      return FALSE ;
    }
  }

  if (rel_uri_object->query != NULL) {
    if (! uri_copy_field(build_uri, rel_uri_object, HQN_URI_QUERY)) {
      hqn_uri_free(&rel_uri_object) ;
      hqn_uri_free(&build_uri) ;
      return FALSE ;
    }
  }

  if (rel_uri_object->fragment != NULL) {
    if (! uri_copy_field(build_uri, rel_uri_object, HQN_URI_FRAGMENT)) {
      hqn_uri_free(&rel_uri_object) ;
      hqn_uri_free(&build_uri) ;
      return FALSE ;
    }
  }

  /* 4) If the authority component is defined, then the reference is a
       network-path and we skip to step 7.  Otherwise, the reference
       URI's authority is inherited from the base URI's authority
       component, which will also be undefined if the URI scheme does
       not use an authority component. */

  if ((rel_uri_object->authority != NULL) || (rel_uri_object->host != NULL)) {
    if (rel_uri_object->authority != NULL) {
      if (! uri_copy_field(build_uri, rel_uri_object, HQN_URI_AUTHORITY)) {
        hqn_uri_free(&rel_uri_object) ;
        hqn_uri_free(&build_uri) ;
        return FALSE ;
      }
    } else {
      if (rel_uri_object->host != NULL) {
        if (! uri_copy_field(build_uri, rel_uri_object, HQN_URI_HOST)) {
          hqn_uri_free(&rel_uri_object) ;
          hqn_uri_free(&build_uri) ;
          return FALSE ;
        }
      }
      if (rel_uri_object->user != NULL) {
        if (! uri_copy_field(build_uri, rel_uri_object, HQN_URI_USER)) {
          hqn_uri_free(&rel_uri_object) ;
          hqn_uri_free(&build_uri) ;
          return FALSE ;
        }
      }
      if (base_uri_object->port != NULL) {
        if (! uri_copy_field(build_uri, rel_uri_object, HQN_URI_PORT)) {
          hqn_uri_free(&rel_uri_object) ;
          hqn_uri_free(&build_uri) ;
          return FALSE ;
        }
      }
    }
    if (rel_uri_object->path != NULL) {
      if (! uri_copy_field(build_uri, rel_uri_object, HQN_URI_PATH)) {
        hqn_uri_free(&rel_uri_object) ;
        hqn_uri_free(&build_uri) ;
        return FALSE ;
      }
    }
    goto step_7 ;
  }

  if (base_uri_object->authority != NULL) {
    if (! uri_copy_field(build_uri, base_uri_object, HQN_URI_AUTHORITY)) {
      hqn_uri_free(&rel_uri_object) ;
      hqn_uri_free(&build_uri) ;
      return FALSE ;
    }

  } else if (base_uri_object->host != NULL) {
    if (! uri_copy_field(build_uri, base_uri_object, HQN_URI_HOST)) {
      hqn_uri_free(&rel_uri_object) ;
      hqn_uri_free(&build_uri) ;
      return FALSE ;
    }

    if (base_uri_object->user != NULL) {
      if (! uri_copy_field(build_uri, base_uri_object, HQN_URI_USER)) {
        hqn_uri_free(&rel_uri_object) ;
        hqn_uri_free(&build_uri) ;
        return FALSE ;
      }
    }

    if (base_uri_object->port != NULL) {
      if (! uri_copy_field(build_uri, base_uri_object, HQN_URI_PORT)) {
        hqn_uri_free(&rel_uri_object) ;
        hqn_uri_free(&build_uri) ;
        return FALSE ;
      }
    }
  }

  /* 5) If the path component begins with a slash character ("/"),
     then the reference is an absolute-path and we skip to step 7. */

  if ((rel_uri_object->path != NULL) && (rel_uri_object->path[0] == '/')) {
    if (! uri_copy_field(build_uri, rel_uri_object, HQN_URI_PATH)) {
      hqn_uri_free(&rel_uri_object) ;
      hqn_uri_free(&build_uri) ;
      return FALSE ;
    }

    /* I use a goto here because "its a good thing to use in this
       circumstance". This is how the spec is written and actually
       makes the code easier to read according to the spec! */
    goto step_7 ;
  }

  /* 6) If this step is reached, then we are resolving a relative-path
        reference.  The relative path needs to be merged with the base
        URI's path.  Although there are many ways to do this, we will
        describe a simple method using a separate string buffer. */

  /* a) All but the last segment of the base URI's path component is
        copied to the buffer.  In other words, any characters after
        the last (right-most) slash character, if any, are
        excluded. */

  /* NOTE: We keep the / on the end */
  base_path_len = base_uri_object->path_len ;
  base_path = base_uri_object->path ;

  /* search backwards for last / */
  while ((base_path_len > 0) && (base_path[base_path_len - 1] != '/'))
    base_path_len-- ;

  /* b) The reference's path component is appended to the buffer
     string. */

  /* We now know a maximum length of the path. Allocate it and append
     the two together. */

  if (rel_uri_object->path != NULL) {
    new_path_len = base_path_len + rel_uri_object->path_len ;
  } else {
    new_path_len = base_path_len ;
  }

  if (new_path_len > TMPBUF_SIZE) {
    tmp_path = uri_context->mem.f_malloc((size_t)new_path_len) ;
    if (tmp_path == NULL) {
      hqn_uri_free(&rel_uri_object) ;
      hqn_uri_free(&build_uri) ;
      return FALSE ;
    }
  } else {
    tmp_path = temp_buf ;
  }

  HqMemCpy(tmp_path, base_path, base_path_len) ;
  if (rel_uri_object->path != NULL) {
    HqMemCpy(tmp_path + base_path_len, rel_uri_object->path, rel_uri_object->path_len) ;
  }

  /* keep pointer to beginning for de-allocation as normalise is
     likely to destroy tmp_path */
  keep_path = tmp_path ;
  keep_path_len = new_path_len ;

  /* 6.c through 6.g */
  if (! uri_normalise_path(&tmp_path, &new_path_len)) {
    if (keep_path_len > TMPBUF_SIZE)
      uri_context->mem.f_free(keep_path) ;
    hqn_uri_free(&rel_uri_object) ;
    hqn_uri_free(&build_uri) ;
    return FALSE ;
  }

  build_uri->path = tmp_path ;
  build_uri->path_len = new_path_len ;

step_7:
  /* 7) The resulting URI components, including any inherited from the
        base URI, are recombined to give the absolute form of the URI
        reference. */

  if (! rebuild_uri(uri_context, build_uri)) {
    if (keep_path_len > TMPBUF_SIZE)
      uri_context->mem.f_free(keep_path) ;
    return FALSE ;
  }

  if (keep_path_len > TMPBUF_SIZE)
    uri_context->mem.f_free(keep_path) ;

  hqn_uri_free(&rel_uri_object) ;

  *new_uri_object = build_uri ;

  return TRUE ;
}


HqBool hqn_uri_set_field_2396(
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
  if (! hqn_uri_reparse(uri_object))
    return FALSE ;

  return TRUE ;
}

HqBool hqn_uri_get_field_2396(
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

    if (which_fields & HQN_URI_OPAQUE) {
      if (start != NULL) {
        if (prev_segment != HQN_URI_SCHEME) {
          HQFAIL("Not requesting contiguous fields.") ;
          return FALSE ;
        }
      } else {
        if (uri_object->opaque_len > 0) {
          start = uri_object->opaque ;
        } else {
          return FALSE ;
        }
      }
      len += uri_object->opaque_len ;
      which_fields ^= HQN_URI_OPAQUE ;

    } else { /* NOT opaque */

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
    } /* end NOT opaque */

  } /* end not entire */

  /* We should have satisfied all segment requests */
  HQASSERT(which_fields == 0,
           "Requesting invalid segement combination.") ;

  *field = start ;
  *field_len = len ;

  return (len > 0) ;
}

/**
 * Return the numeric value of the passed hexedecimal character, or -1 if it is
 * outside of the character ranges 0-1, A-F, a-f.
 */
int32 hex_char_value(uint8 c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A';
  if (c >= 'a' && c <= 'f')
    return c - 'a';

  return -1;
}

/* See header for doc. */
HqBool hqn_uri_validate_percent_encoding(uint8* string, int32 length,
                                         int32* decodedLength)
{
  int32 i;

  *decodedLength = length;
  for (i = 0; i < length; i ++) {
    if (string[i] == '%') {
      /* The next two chars should be hex digits. */
      if (length - i < 3 ||
          hex_char_value(string[i + 1]) == -1 ||
          hex_char_value(string[i + 2]) == -1) {
        return FALSE;
      }
      else {
        i += 2;
        *decodedLength -= 2;
      }
    }
  }
  return TRUE;
}

/* See header for doc. */
void hqn_uri_percent_decode(uint8* string, int32 length,
                            uint8* decoded, int32 decodedLength)
{
  int32 i, scan;

  UNUSED_PARAM(int32, length);

  scan = 0;
  for (i = 0; i < decodedLength; i ++) {
    HQASSERT(scan < length, "URI shorter than expected.");

    if (string[scan] == '%') {
      decoded[i] = (uint8)((hex_char_value(string[scan + 1]) << 4) |
                           hex_char_value(string[scan + 2]));
      scan += 3;
    }
    else {
      decoded[i] = string[scan];
      scan ++;
    }
  }
}

/* See header for doc. */
void hqn_uri_find_leaf_name(uint8* uri, int32 length,
                            int32 *start, int32 *leafLength) {
  int32 end, i;

  *start = 0;
  end = length;
  i = end - 1;

  /* Scan the URI from the end. Discard any fragment or query strings, and stop
   * when we hit the first '/'. */
  while (i > *start) {
    if (uri[i] == '#')
      end = i;

    if (uri[i] == '?')
      end = i;

    if (uri[i] == '/')
      *start = i + 1;

    i --;
  }
  *leafLength = end - *start;
}

/* ============================================================================
 * Tests are copied from RFC 2396 appendix C. Work out to be a rather good
 * set of tests for the URI library.
 */
#if defined(DEBUG_BUILD)

/* As per RFC 2396 */
#define RFC_2396_BASE "http://a/b/c/d;p?q"
/* The string and its length, a useful shorthand */
#define SL(s) ((uint8 *)s), (sizeof(s) - 1)

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
  /* Appendix C, C.1 */
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g:h"), /* rel URI */
                      SL("g:h"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g"), /* rel URI */
                      SL("http://a/b/c/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("./g"), /* rel URI */
                      SL("http://a/b/c/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g/"), /* rel URI */
                      SL("http://a/b/c/g/"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("/g"), /* rel URI */
                      SL("http://a/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("//g"), /* rel URI */
                      SL("http://g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("?y"), /* rel URI */
                      SL("http://a/b/c/?y"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g?y"), /* rel URI */
                      SL("http://a/b/c/g?y"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("#s"), /* rel URI */
                      SL("http:/b/c/d;p?q#s"))) /* expected URI - <CurrentDocument> */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g#s"), /* rel URI */
                      SL("http://a/b/c/g#s"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g?y#s"), /* rel URI */
                      SL("http://a/b/c/g?y#s"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL(";x"), /* rel URI */
                      SL("http://a/b/c/;x"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g;x"), /* rel URI */
                      SL("http://a/b/c/g;x"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g;x?y#s"), /* rel URI */
                      SL("http://a/b/c/g;x?y#s"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("."), /* rel URI */
                      SL("http://a/b/c/"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("./"), /* rel URI */
                      SL("http://a/b/c/"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL(".."), /* rel URI */
                      SL("http://a/b/"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("../"), /* rel URI */
                      SL("http://a/b/"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("../g"), /* rel URI */
                      SL("http://a/b/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("../.."), /* rel URI */
                      SL("http://a/"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("../../"), /* rel URI */
                      SL("http://a/"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("../../g"), /* rel URI */
                      SL("http://a/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;

  /* C.2 Abnormal examples */

  /* NOTE: We strip them off. */
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("../../../g"), /* rel URI */
                      SL("http://a/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("../../../../g"), /* rel URI */
                      SL("http://a/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;

  /* being careful with . and .. */
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("/./g"), /* rel URI */
                      SL("http://a/./g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("/../g"), /* rel URI */
                      SL("http://a/../g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g."), /* rel URI */
                      SL("http://a/b/c/g."))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL(".g"), /* rel URI */
                      SL("http://a/b/c/.g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g.."), /* rel URI */
                      SL("http://a/b/c/g.."))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("..g"), /* rel URI */
                      SL("http://a/b/c/..g"))) /* expected URI */
    HQFAIL("URI parse failed") ;

  /* less likely cases */
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("./../g"), /* rel URI */
                      SL("http://a/b/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("./../g"), /* rel URI */
                      SL("http://a/b/g"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("./g/."), /* rel URI */
                      SL("http://a/b/c/g/"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g/./h"), /* rel URI */
                      SL("http://a/b/c/g/h"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g/../h"), /* rel URI */
                      SL("http://a/b/c/h"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g;x=1/./y"), /* rel URI */
                      SL("http://a/b/c/g;x=1/y"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g;x=1/../y"), /* rel URI */
                      SL("http://a/b/c/y"))) /* expected URI */
    HQFAIL("URI parse failed") ;

  /* testing query and fragment components */
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g?y/./x"), /* rel URI */
                      SL("http://a/b/c/g?y/./x"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g?y/../x"), /* rel URI */
                      SL("http://a/b/c/g?y/../x"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g#s/./x"), /* rel URI */
                      SL("http://a/b/c/g#s/./x"))) /* expected URI */
    HQFAIL("URI parse failed") ;
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
                      SL("g#s/../x"), /* rel URI */
                      SL("http://a/b/c/g#s/../x"))) /* expected URI */
    HQFAIL("URI parse failed") ;

  /* We will implement a validating parser */
  if (! test_parse_rel_uri(uri_context,SL(RFC_2396_BASE),
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
    if (! hqn_uri_parse(uri_context, &uri_object, SL(RFC_2396_BASE), TRUE))
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
