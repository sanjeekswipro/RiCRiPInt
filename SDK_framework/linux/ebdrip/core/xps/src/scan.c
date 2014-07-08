/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!src:scan.c(EBDSDK_P.1) $
 * $Id: src:scan.c,v 1.130.8.1.1.1 2013/12/19 11:24:46 anon Exp $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Scans XPS attribute strings, parsing them into suitable types.
 */

#include "float.h"       /* platforms specific double sizes */
#include "core.h"
#include "mmcompat.h"    /* mm_alloc_with_header etc.. */
#include "swerrors.h"
#include "swctype.h"     /* tolower(), toupper() */
#include "tables.h"
#include "matrix.h"
#include "xml.h"
#include "xmltypeconv.h"
#include "graphics.h"
#include "gu_cons.h"
#include "gu_path.h"
#include "pathcons.h"
#include "constant.h"
#include "hqmemcmp.h"
#include "gstack.h"
#include "matrix.h"
#include "basemap.h"
#include "gstate.h"

#include "xpspriv.h"
#include "xpsscan.h"

#include "namedef_.h"

#define BIGGEST_REAL_DIV_10 (DBL_MAX / 10)

/* ============================================================================
 * Match functions. Match functions should NOT raise a PS error, but
 * rather TRUE or FALSE. Match functions should never use convert
 * functions.
 * ============================================================================
 */

/* From XPS 0.90 s0schema.xsd
  <!--DEFINE [scs]        "( ?, ?)" -->

  NOTE: As of 0.8, [scs] patterns were only used where there was also
  a collapse, hence the name of this function.
*/
static
Bool xps_match_scs_collapse(utf8_buffer* input)
{
  utf8_buffer scan ;
  scan = *input ;

  HQASSERT((input != NULL),
           "NULL utf8 buffer pointer");

  (void)xml_match_space(&scan) ; /* Zero or more leading spaces */

  if ( !xml_match_unicode(&scan, ',') )
    return FALSE ;

  (void)xml_match_space(&scan) ; /* Zero or more trailing spaces */

  *input = scan ;

  return TRUE ;
}

Bool xps_match_static_resource(utf8_buffer* input)
{
  const utf8_buffer match = {UTF8_AND_LENGTH("{StaticResource")};
  utf8_buffer scan ;
  scan = *input ;

  HQASSERT((input != NULL),
           "NULL utf8 buffer pointer");

  /* Must be a {StaticResource */
  if ( ! xml_match_string(&scan, &match) )
    return FALSE ;

  if (xml_match_whitespace(&scan) == 0)
    return FALSE ;

  *input = scan ;

  return TRUE ;
}

/* ============================================================================
 * Convert functions. Convert functions should NOT raise a PS error,
 * but rather TRUE or FALSE.
 * ============================================================================
 */


/* This function is ONLY used by xps_xml_to_int() and
   xps_xml_to_double() so that we can track RANGECHECK compared to
   SYNTAXERROR without actually calling the error handler. */
static Bool xps_scan_numeric_error(
  int32 error,
/*@in@*/ /*@notnull@*/
  int32 *error_result)
{
  *error_result = error ;
  return FALSE ;
}

Bool xps_xml_to_int(
/*@in@*/ /*@notnull@*/
  utf8_buffer* input,
/*@in@*/ /*@notnull@*/
  int32* p_int,
  int32 type,
/*@in@*/ /*@notnull@*/
  int32 *error_result)
{
  utf8_buffer scan ;
  uint32 digits ;
  int32 value = 0, negate_xor = 0, negate_add = 0 ;
  Bool first_digit = TRUE ;

  HQASSERT((input != NULL),
           "NULL utf8 buffer pointer");
  HQASSERT((p_int != NULL),
           "NULL returned integer pointer");
  HQASSERT((type == xps_pint || type == xps_uint || type == xps_int),
            "invalid type") ;
  HQASSERT(error_result != NULL, "error result is NULL") ;

#define error_handler DO NOT USE error_handler, use xps_scan_numeric_error instead!

  *error_result = 0 ;

  scan = *input;
  if ( scan.unitlength == 0 )
    return xps_scan_numeric_error(SYNTAXERROR, error_result) ;

  /* Since the string is UTF-8, we can compare single codepoints to ASCII. We
     cannot use strtol() because (a) the string is not null terminated, and
     (b) we want to cope with an arbitrary number of digits, clipping the
     range of values to MAXINT32/MININT32. */
  switch ( scan.codeunits[0] ) {
  case '-':
    if (type == xps_uint || type == xps_pint) {
      return xps_scan_numeric_error(SYNTAXERROR, error_result) ;
    }
    /* Conditionally negate digits using -x = ~x + 1 to avoid multiplication. */
    negate_xor = -1 ;
    negate_add = 1 ;
    /*@fallthrough@*/
  case '+':
    if (type == xps_uint || type == xps_pint) {
      return xps_scan_numeric_error(SYNTAXERROR, error_result) ;
    }
    ++scan.codeunits ;
    --scan.unitlength ;
    break ;
  }

  for ( digits = 0 ; scan.unitlength > 0 ;
        ++scan.codeunits, --scan.unitlength, ++digits ) {
    int32 digit = scan.codeunits[0] - '0' ;

    if ( digit < 0 || digit > 9 )
      break ;

    if (type == xps_pint && first_digit && digit < 1)
      return xps_scan_numeric_error(SYNTAXERROR, error_result) ;

    first_digit = FALSE ;

    digit ^= negate_xor ;
    digit += negate_add ;

    if ( value >= MAXINT32 / 10 ) {
      value = MAXINT32 ;
      HQFAIL("xml_to_int: integer out of range, limiting to max int") ;
    } else if ( value <= MININT32 / 10 ) {
      value = MININT32 ;
      HQFAIL("xml_to_int: integer out of range, limiting to min int") ;
    } else
      value = value * 10 + digit ;
  }

  if ( digits == 0 ) {
    return xps_scan_numeric_error(SYNTAXERROR, error_result) ;
  }

  *p_int = value ;
  *input = scan ;

#undef error_handler
  return TRUE ;
}

/* The basic value space of double consists of the values m × 2^e,
   where m is an integer whose absolute value is less than 2^53, and e
   is an integer between -1075 and 970, inclusive.

   Looking on Windows (as an example), it turns out that this range is
   inconsistent with what Windows allows (and I suspect other
   platforms as well), so I'm going to simply limit doubles to what
   the platform allows. Using the platform limits is more a limitation
   of what is intended rather than being pure to the schema spec.

   I'm a little concerned that we raise a limit check for boundary
   cases where the number can be written down to be within the boundary
   but our intermediate storage while trying to calculate the final
   number overflows. An example of this might be:

   1[400zeros]E-100 (which is in the legal range, but we would
   overflow trying to store the mantissa itself before applying the
   exponent. */
Bool xps_xml_to_double(
/*@in@*/ /*@notnull@*/
  utf8_buffer* input,
/*@in@*/ /*@notnull@*/
  double* p_double,
  int32 type,
/*@in@*/ /*@notnull@*/
  int32 *error_result)
{
  utf8_buffer scan ;
  int32 sign = 1, exp_sign = 1 ;
  int32 ntotal = 0 ;
  int32 nleading = 0 , n_eleading = 0, ntrailing = 0 ;
  double fleading = 0, eleading = 0 ;
  double power;
  uint8 ch ;
  Bool all_consumed = FALSE ;
  Bool have_exp = FALSE ;
  static double fdivs[ 11 ] = { 0.0 , 0.1 , 0.01 , 0.001 , 0.0001 , 0.00001 , 0.000001 ,
                                0.0000001 , 0.00000001 , 0.000000001 , 0.0000000001 } ;
  HQASSERT((input != NULL),
           "NULL utf8 buffer pointer") ;
  HQASSERT((p_double != NULL),
           "NULL returned double pointer") ;
  HQASSERT(error_result != NULL, "error result is NULL") ;

  HQASSERT((type == xps_dec || type == xps_rn || type == xps_prn ||
            type == xps_ST_Double), "invalid type") ;

#define error_handler DO NOT USE error_handler, use xps_scan_numeric_error instead!

  *error_result = 0 ;

  scan = *input ;
  if ( scan.unitlength == 0 )
    return xps_scan_numeric_error(RANGECHECK, error_result) ;

  ch = *scan.codeunits++ ;
  --scan.unitlength ;

  /* Since the string is UTF-8, we can compare single codepoints to ASCII. We
     cannot use strtod() because (a) the string is not null terminated,
     (b) we want to cope with an arbitrary number of digits, and (c) strtod() is
     locale sensitive, i.e. the decimal separator may be other than a period,
     while XML double is always a period. */

  if (ch == '-' || ch == '+') {
    if (ch == '-') {
      if (type == xps_prn)
        return xps_scan_numeric_error(RANGECHECK, error_result) ;
      sign = -1 ;
    } else if (ch == '+') {
      if (type == xps_dec)
        return xps_scan_numeric_error(SYNTAXERROR, error_result) ;
    }
    if (scan.unitlength == 0)
      return xps_scan_numeric_error(SYNTAXERROR, error_result) ;
    ch = *scan.codeunits++ ;
    --scan.unitlength ;
  }

  nleading = 0 ;
  fleading = 0.0 ;

  /* Scan the m part of a (m.n) number. */
  for (;;) {
    if (! isdigit(ch))
      break ;
    ch = (uint8)( ch - '0' ) ;
    ++nleading ;

    /* Avoid double overflow before doing the multiplication. */
    if (BIGGEST_REAL_DIV_10 < fleading)
      return xps_scan_numeric_error(RANGECHECK, error_result) ;

    fleading = 10.0 * fleading + ch ;

    if (scan.unitlength == 0) {
      all_consumed = TRUE ;
      break ;
    } else {
      ch = *scan.codeunits++ ;
      --scan.unitlength ;
    }
  }

  ntotal = nleading ;

  /* At this stage we can have either a .|E|e|. */
  if (! all_consumed) {
    switch (ch) {

    case 'E':
    case 'e':
      if (type == xps_dec)
        return xps_scan_numeric_error(SYNTAXERROR, error_result) ;
      if (ntotal == 0)
        return xps_scan_numeric_error(SYNTAXERROR, error_result) ;
      if (scan.unitlength == 0)
        return xps_scan_numeric_error(SYNTAXERROR, error_result) ;
      have_exp = TRUE ;
      ch = *scan.codeunits++ ;
      --scan.unitlength ;

      if (ch == '-' || ch == '+') {
        if (ch == '-')
          exp_sign = -1 ;
        if (scan.unitlength == 0)
          return xps_scan_numeric_error(SYNTAXERROR, error_result) ;
        ch = *scan.codeunits++ ;
        --scan.unitlength ;
      }

      /* Scan the p part of a (mEp) number. */
      for (;;) {
        if ( ! isdigit( ch ))
          break ;
        ch = (uint8)( ch - '0' ) ;
        ++n_eleading ;

        /* Avoid double overflow before doing the multiplication. */
        if (BIGGEST_REAL_DIV_10 < eleading)
          return xps_scan_numeric_error(RANGECHECK, error_result) ;

        eleading = 10.0 * ( double )eleading + ch ;

        if (scan.unitlength == 0) {
          all_consumed = TRUE ;
          break ;
        } else {
          ch = *scan.codeunits++ ;
          --scan.unitlength ;
        }
      }

      /* E must be followed by some digits */
      if (n_eleading == 0)
        return xps_scan_numeric_error(SYNTAXERROR, error_result) ;
      break ;

    case '.':
        for (;;) { /* Scan the n part of a (m.n) number. */
          if (scan.unitlength == 0) {
            all_consumed = TRUE ;
            break ;
          } else {
            ch = *scan.codeunits++ ;
            --scan.unitlength ;
          }
          if (! isdigit(ch))
            break ;
          ch = (uint8)( ch - '0' ) ;
          ++ntotal ;

          /* Avoid double overflow before doing the multiplication. */
          if (BIGGEST_REAL_DIV_10 < fleading)
            return xps_scan_numeric_error(RANGECHECK, error_result) ;

          fleading = 10.0 * fleading + ch ;
        }
        ntrailing = ntotal - nleading ;

        /* According to the pattern, . must be followed by some
           characters. */
        if (ntrailing == 0)
          return xps_scan_numeric_error(SYNTAXERROR, error_result) ;
      break ;

    default:
      all_consumed = TRUE ; /* not a digit or one of the above */
      scan.codeunits-- ; /* unget this char */
      ++scan.unitlength ;
    }
  }

  /* We could have (m.nEp) */
  if (! all_consumed && (ch == 'E' || ch == 'e')) {
    if (ntotal == 0)
      return xps_scan_numeric_error(SYNTAXERROR, error_result) ;
    if (scan.unitlength == 0)
      return xps_scan_numeric_error(SYNTAXERROR, error_result) ;
    have_exp = TRUE ;
    ch = *scan.codeunits++ ;
    --scan.unitlength ;

    if (ch == '-' || ch == '+') {
      if (ch == '-')
        exp_sign = -1 ;
      if (scan.unitlength == 0)
        return xps_scan_numeric_error(SYNTAXERROR, error_result) ;
      ch = *scan.codeunits++ ;
      --scan.unitlength ;
    }

    /* Scan the p part of a (m.nEp) number. */
    for (;;) {
      if ( ! isdigit( ch )) {
        scan.codeunits-- ; /* unget this char */
        ++scan.unitlength ;
        break ;
      }
      ch = (uint8)( ch - '0' ) ;
      ++n_eleading ;

      /* Avoid double overflow before doing the multiplication. */
      if (BIGGEST_REAL_DIV_10 < eleading)
        return xps_scan_numeric_error(RANGECHECK, error_result) ;

      eleading = 10.0 * ( double )eleading + ch ;

      if (scan.unitlength == 0) {
        all_consumed = TRUE ;
        break ;
      } else {
        ch = *scan.codeunits++ ;
        --scan.unitlength ;
      }
    }

    /* E must be followed by some digits */
    if (n_eleading == 0)
      return xps_scan_numeric_error(SYNTAXERROR, error_result) ;
  } else {
    if (! all_consumed) {
      scan.codeunits-- ; /* unget this char */
      ++scan.unitlength ;
    }
  }

  /* Bad number; probably just a {+,-,.,+.,-.}. */
  if ( ntotal == 0 )
    return xps_scan_numeric_error(SYNTAXERROR, error_result) ;

  /* Convert number to a real. */
  if ( ntotal == nleading && nleading > 0 ) {
    /* We scanned (m) or (m.). */
  } else {
    /* We scanned (.n) or (m.n). */
    if ( ntrailing < 10 ) {
      /* Avoid underflow - be careful with 0.0 case */
      if (fleading > 0.0 && (DBL_MIN * ntrailing * 10) > fleading)
        return xps_scan_numeric_error(RANGECHECK, error_result) ;
      fleading = fleading * fdivs[ ntrailing ] ;
    } else {
      /* Avoid pow() raising an overfow */
      if (ntrailing < DBL_MIN_10_EXP || ntrailing > DBL_MAX_10_EXP)
        return xps_scan_numeric_error(RANGECHECK, error_result) ;

      power = pow( 10.0 , ( double )( ntrailing )) ;

      /* Avoid division raising an overflow. This is not efficient but
         I can't think of another way to do this. */
      if ((DBL_MIN * power) > fleading)
        return xps_scan_numeric_error(RANGECHECK, error_result) ;

      fleading = fleading / power ;
    }
  }

  /* Take care of exponent. */
  if (have_exp) {
    double power ;
    if (exp_sign < 0 && eleading > 0 && eleading < 10) {
      power = fdivs[ (uint32)eleading ] ;
      eleading = -eleading ; /* Don't need to do this, but for
                                consistency I do. */
    } else {
      /* Avoid pow() raising an overfow */
      if (eleading < DBL_MIN_10_EXP || eleading > DBL_MAX_10_EXP)
        return xps_scan_numeric_error(RANGECHECK, error_result) ;

      if (exp_sign < 0)
        eleading = -eleading ;

      power = pow(10.0, eleading) ;
    }

    /* Avoid multiplication raising an overfow. This is not efficient
       but I can't think of another way to do this. */
    if (power > 1.0 && ((DBL_MAX / power) < fleading))
      return xps_scan_numeric_error(RANGECHECK, error_result) ;

    fleading = fleading * power ;
  }

  if ( sign < 0 )
    fleading = -fleading ;

  *p_double = fleading;
  *input = scan ;

#undef error_handler
  return TRUE;
}

/*
    <!--DEFINE [pint]       "([1-9][0-9]*)" -->
*/
Bool xps_convert_pint(xmlGFilter *filter,
                      xmlGIStr *attrlocalname,
                      utf8_buffer* value,
                      void *data /* int32* */)
{
  int32 error_result ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  if (! xps_xml_to_int(value, data, xps_pint, &error_result))
    return error_handler(error_result) ;

  return TRUE ;
}

/*
    <!--DEFINE [uint]       "([0-9]+)" -->
*/
Bool xps_convert_uint(xmlGFilter *filter,
                      xmlGIStr *attrlocalname,
                      utf8_buffer* value,
                      void *data /* int32* */)
{
  int32 error_result ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  if (! xps_xml_to_int(value, data, xps_uint, &error_result))
    return error_handler(error_result) ;

  return TRUE ;
}

/*
    <!--DEFINE [dec]        "(\-?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+)))" -->
*/
Bool xps_convert_dec(xmlGFilter *filter,
                     xmlGIStr *attrlocalname,
                     utf8_buffer* value,
                     void *data /* double* */)
{
  int32 error_result ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(data != NULL, "Nowhere to put converted double") ;

  if (! xps_xml_to_double(value, data, xps_dec, &error_result))
    return error_handler(error_result) ;

  return TRUE ;
}

/*
    <!--DEFINE [rn]         "((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)" -->
*/
Bool xps_convert_dbl_rn(xmlGFilter *filter,
                        xmlGIStr *attrlocalname,
                        utf8_buffer* value,
                        void *data /* double* */)
{
  int32 error_result ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(data != NULL, "Nowhere to put converted double") ;

  if (! xps_xml_to_double(value, data, xps_rn, &error_result))
    return error_handler(error_result) ;

  return TRUE ;
}

/*
    <!--DEFINE [rn]         "((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)" -->
*/
Bool xps_convert_fl_rn(xmlGFilter *filter,
                       xmlGIStr *attrlocalname,
                       utf8_buffer* value,
                       void *data /* float* */)
{
  double number ;

  HQASSERT(data != NULL, "Nowhere to put converted float") ;

  if (! xps_convert_dbl_rn(filter, attrlocalname, value, &number))
    return(FALSE);;

  if ( (fabs(number) > FLT_MAX) ||
       ((fabs(number) < FLT_MIN) && (number != 0.0)) ) {
    return(error_handler(RANGECHECK));
  }
  *(float*)data = (float)number ;

  return TRUE ;
}

/*
    <!--DEFINE [prn]        "(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)" -->
*/
Bool xps_convert_dbl_prn(xmlGFilter *filter,
                         xmlGIStr *attrlocalname,
                         utf8_buffer* value,
                         void *data /* double* */)
{
  int32 error_result ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(data != NULL, "Nowhere to put converted double") ;

  if (! xps_xml_to_double(value, data, xps_prn, &error_result))
    return error_handler(error_result) ;

  return TRUE ;
}

/*
    <!--DEFINE [prn]        "(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)" -->
*/
Bool xps_convert_fl_prn(xmlGFilter *filter,
                        xmlGIStr *attrlocalname,
                        utf8_buffer* value,
                        void *data /* float* */)
{
  double number ;

  HQASSERT(data != NULL, "Nowhere to put converted float") ;

  if (! xps_convert_dbl_prn(filter, attrlocalname, value, &number) )
    return(FALSE);

  if ( (number > FLT_MAX) ||
       ((number < FLT_MIN) && (number != 0.0)) ) {
    return(error_handler(RANGECHECK));
  }
  *(float*)data = (float)number ;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- positive real number, equal or greater than one -->
    <xs:simpleType name="ST_GEOne">
        <xs:restriction base="ST_Double">
            <xs:minInclusive value="1.0" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_dbl_ST_GEOne(xmlGFilter *filter,
                              xmlGIStr *attrlocalname,
                              utf8_buffer* value,
                              void *data /* double* */)
{
  double  number;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(data != NULL, "Nowhere to put converted double") ;

  if (! xps_convert_dbl_ST_Double(filter, attrlocalname, value, &number))
    return FALSE ;

  if ( number < 1.0 ) {
    return(error_handler(RANGECHECK));
  }
  *(double*)data = number;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- positive real number, equal or greater than one -->
    <xs:simpleType name="ST_GEOne">
        <xs:restriction base="ST_Double">
            <xs:minInclusive value="1.0" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_fl_ST_GEOne(xmlGFilter *filter,
                             xmlGIStr *attrlocalname,
                             utf8_buffer* value,
                             void *data /* float* */)
{
  double number = 0.0 ;

  HQASSERT(data != NULL, "Nowhere to put converted float") ;

  if (! xps_convert_dbl_ST_GEOne(filter, attrlocalname, value, &number) )
    return(FALSE);

  if ( number > FLT_MAX ) {
    return(error_handler(RANGECHECK));
  }
  *(float*)data = (float)number ;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- positive real number -->
    <xs:simpleType name="ST_GEZero">
        <xs:restriction base="ST_Double">
            <xs:minInclusive value="0.0" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_dbl_ST_GEZero(xmlGFilter *filter,
                               xmlGIStr *attrlocalname,
                               utf8_buffer* value,
                               void *data /* double* */)
{
  double  number;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(data != NULL, "Nowhere to put converted double") ;

  if (! xps_convert_dbl_ST_Double(filter, attrlocalname, value, &number))
    return FALSE ;

  if ( number < 0.0 ) {
    return(error_handler(RANGECHECK)) ;
  }
  *(double*)data = number;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- positive real number -->
    <xs:simpleType name="ST_GEZero">
        <xs:restriction base="ST_Double">
            <xs:minInclusive value="0.0" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_fl_ST_GEZero(xmlGFilter *filter,
                              xmlGIStr *attrlocalname,
                              utf8_buffer* value,
                              void *data /* double* */)
{
  double number = 0.0 ;

  HQASSERT(data != NULL, "Nowhere to put converted double") ;

  if (! xps_convert_dbl_ST_GEZero(filter, attrlocalname, value, &number) )
    return(FALSE);

  if ( (number > FLT_MAX) ||
       ((number < FLT_MIN) && (number != 0.0)) ) {
    return(error_handler(RANGECHECK));
  }
  *(float*)data = (float)number ;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- real number from 0.0 to 1.0 inclusive -->
    <xs:simpleType name="ST_ZeroOne">
        <xs:restriction base="ST_Double">
            <xs:minInclusive value="0.0" />
            <xs:maxInclusive value="1.0" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_dbl_ST_ZeroOne(xmlGFilter *filter,
                                xmlGIStr *attrlocalname,
                                utf8_buffer* value,
                                void *data /* double* */)
{
  double  number;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(data != NULL, "Nowhere to put converted double") ;

  if (! xps_convert_dbl_ST_Double(filter, attrlocalname, value, &number))
    return FALSE ;

  if ( (number < 0.0) || (number > 1.0) ) {
    return(error_handler(RANGECHECK));
  }
  *(double*)data = number;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- real number from 0.0 to 1.0 inclusive -->
    <xs:simpleType name="ST_ZeroOne">
        <xs:restriction base="ST_Double">
            <xs:minInclusive value="0.0" />
            <xs:maxInclusive value="1.0" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_fl_ST_ZeroOne(xmlGFilter *filter,
                               xmlGIStr *attrlocalname,
                               utf8_buffer* value,
                               void *data /* float* */)
{
  double number = 0.0 ;

  HQASSERT(data != NULL, "Nowhere to put converted float") ;

  if (! xps_convert_dbl_ST_ZeroOne(filter, attrlocalname, value, &number) )
    return(FALSE);

  if ( (number < FLT_MIN) && (number != 0.0) ) {
    return(RANGECHECK);
  }
  *(float*)data = (float)number ;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- Double -->
    <xs:simpleType name="ST_Double">
        <xs:restriction base="xs:double">
            <xs:whiteSpace value="collapse" />
<!--
            <xs:pattern value="[rn]"/>
-->
            <xs:pattern value="((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_dbl_ST_Double(xmlGFilter *filter,
                               xmlGIStr *attrlocalname,
                               utf8_buffer* value,
                               void *data /* double* */)
{
  int32 error_result ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(data != NULL, "Nowhere to put converted double") ;

  (void)xml_match_space(value) ;

  if (! xps_xml_to_double(value, data, xps_ST_Double, &error_result))
    return error_handler(error_result) ;

  (void)xml_match_space(value) ;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- Double -->
    <xs:simpleType name="ST_Double">
        <xs:restriction base="xs:double">
            <xs:whiteSpace value="collapse" />
<!--
            <xs:pattern value="[rn]"/>
-->
            <xs:pattern value="((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_fl_ST_Double(xmlGFilter *filter,
                              xmlGIStr *attrlocalname,
                              utf8_buffer* value,
                              void *data /* float* */)
{
  double number;

  HQASSERT(data != NULL, "Nowhere to put converted float") ;

  if (! xps_convert_dbl_ST_Double(filter, attrlocalname, value, &number) )
    return(FALSE);

  if ( (fabs(number) > FLT_MAX) ||
       ((fabs(number) < FLT_MIN) && (number != 0.0)) ) {
    return(error_handler(RANGECHECK));
  }
  *(float*)data = (float)number ;

  return TRUE ;
}

/* From XPS 0.85 s0schema.xsd
   <xs:attribute name="BidiLevel" default="0">
     <xs:simpleType>
       <xs:restriction base="xs:integer">
         <xs:minInclusive value="0" />
         <xs:maxInclusive value="61" />
       </xs:restriction>
     </xs:simpleType>
   </xs:attribute>
*/
Bool xps_convert_bidilevel(xmlGFilter *filter,
                           xmlGIStr *attrlocalname,
                           utf8_buffer* value,
                           void *data /* int32* */)
{
  int32 error_result, *bidilevel = data ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  if (! xps_xml_to_int(value, data, xps_int, &error_result))
    return error_handler(error_result) ;

  if (*bidilevel < 0 || *bidilevel > 61)
    return error_handler(RANGECHECK) ;

  return TRUE ;
}

/* convert_point() - converts comma separated double values without any leading
 * or trailing whitespace.  Used by externally visible scanners which handle
 * surrounding whitespace according to XSD patterns.
 */
static
Bool convert_point(
  xmlGFilter*   filter,
  xmlGIStr*     attrlocalname,
  utf8_buffer*  scan,
  void*         data /* SYSTEMVALUE[2] */)
{
  SYSTEMVALUE *point = data;

  HQASSERT((data != NULL),
           "convert_point: pointer for returned point NULL");

  /* xps_convert_dbl_rn raises PS error. */
  if ( !xps_convert_dbl_rn(filter, attrlocalname, scan, &point[0]) )
    return FALSE ;

  if (! xps_match_scs_collapse(scan))
    return error_handler(SYNTAXERROR) ;

  /* xps_convert_dbl_rn raises PS error. */
  if ( !xps_convert_dbl_rn(filter, attrlocalname, scan, &point[1]) )
    return FALSE ;

  return(TRUE);

}

/* From XPS 0.90 s0schema.xsd
    <!-- Point: 2 numbers, separated by , and arbitrary whitespace -->
    <xs:simpleType name="ST_Point">
        <xs:restriction base="xs:string">
            <xs:whiteSpace value="collapse" />
<!--
            <xs:pattern value="[rn][scs][rn]"/>
-->
            <xs:pattern value="((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_Point(xmlGFilter *filter,
                          xmlGIStr *attrlocalname,
                          utf8_buffer* value,
                          void *data /* SYSTEMVALUE[2] */)
{
  utf8_buffer scan ;

  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  (void)xml_match_space(&scan) ; /* Possible leading spaces */

  if ( !convert_point(filter, attrlocalname, &scan, data) ) {
    return(FALSE);
  }

  (void)xml_match_space(&scan) ; /* Possible trailing spaces */

  *value = scan ;

  return TRUE ;
}

/* convert_point_prn() - converts comma separated positive double values without
 * any leading or trailing whitespace.  Used by externally visible scanners
 * which handle surrounding whitespace according to XSD patterns.
 */
static
Bool convert_point_prn(
  xmlGFilter*   filter,
  xmlGIStr*     attrlocalname,
  utf8_buffer*  scan,
  void*         data /* SYSTEMVALUE[2] */)
{
  SYSTEMVALUE *point = data;

  HQASSERT((data != NULL),
           "convert_point_prn: pointer for returned point NULL");

  /* xps_convert_dbl_rn raises PS error. */
  if ( !xps_convert_dbl_prn(filter, attrlocalname, scan, &point[0]) )
    return FALSE ;

  if (! xps_match_scs_collapse(scan))
    return error_handler(SYNTAXERROR) ;

  /* xps_convert_dbl_rn raises PS error. */
  if ( !xps_convert_dbl_prn(filter, attrlocalname, scan, &point[1]) )
    return FALSE ;

  return(TRUE);

}

/* From XPS 0.90 s0schema.xsd
<!--
    <!-- PointGE0: 2 non-negative numbers, separated by , and arbitrary whitespace -->
    <xs:simpleType name="ST_PointGE0">
        <xs:restriction base="xs:string">
            <xs:whiteSpace value="collapse" />
<!--
            <xs:pattern value="[prn][scs][prn]"/>
-->
            <xs:pattern value="(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)" />
        </xs:restriction>
    </xs:simpleType>
-->
*/
Bool xps_convert_ST_PointGE0(xmlGFilter *filter,
                             xmlGIStr *attrlocalname,
                             utf8_buffer* value,
                             void *data /* SYSTEMVALUE[2] */)
{
  utf8_buffer scan ;

  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  (void)xml_match_space(&scan) ; /* Possible leading spaces */

  /* xps_convert_dbl_rn raises PS error. */
  if ( !convert_point_prn(filter, attrlocalname, &scan, data))
    return FALSE ;

  (void)xml_match_space(&scan) ; /* Possible trailing spaces */

  *value = scan ;

  return TRUE ;
}

/*
    <!-- ViewBox: 4 numbers, separated by , and arbitrary whitespace. Second number pair must be non-negative -->
    <xs:simpleType name="ST_ViewBox">
        <xs:restriction base="xs:string">
            <xs:whiteSpace value="collapse" />
<!--
            <xs:pattern value="[rn][scs][rn][scs][prn][scs][prn]"/>
-->
            <xs:pattern value="((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)
                               |(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_ViewBox(xmlGFilter *filter,
                            xmlGIStr *attrlocalname,
                            utf8_buffer* value,
                            void *data /* RECTANGLE* */)
{
  RECTANGLE *rect = data ;
  utf8_buffer scan ;
  double values[4] ;

  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(rect, "Nowhere to put converted rectangle") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  (void)xml_match_space(&scan) ; /* Possible leading spaces */

  if ( !xps_convert_dbl_rn(filter, attrlocalname, &scan, &values[0]) )
    return FALSE ;

  if (! xps_match_scs_collapse(&scan))
    return error_handler(SYNTAXERROR) ;

  if ( !xps_convert_dbl_rn(filter, attrlocalname, &scan, &values[1]) )
    return FALSE ;

  if (! xps_match_scs_collapse(&scan))
    return error_handler(SYNTAXERROR) ;

  if ( !xps_convert_dbl_prn(filter, attrlocalname, &scan, &values[2]) )
    return FALSE ;

  if (! xps_match_scs_collapse(&scan))
    return error_handler(SYNTAXERROR) ;

  if ( !xps_convert_dbl_prn(filter, attrlocalname, &scan, &values[3]) )
    return FALSE ;

  rect->x = values[0] ;
  rect->y = values[1] ;
  rect->w = values[2] ;
  rect->h = values[3] ;

  (void)xml_match_space(&scan) ; /* Possible trailing spaces */
  *value = scan ;

  return TRUE;
}

/*
   <!-- ContentBox: 4 non-negative numbers, separated by commas and arbitrary whitespace -->
    <xs:simpleType name="ST_ContentBox">
        <xs:restriction base="xs:string">
            <xs:whiteSpace value="collapse" />
<!--
            <xs:pattern value="[prn][scs][prn][scs][prn][scs][prn]"/>
-->
            <xs:pattern value="(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)
                               (\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_ContentBox(xmlGFilter *filter,
                               xmlGIStr *attrlocalname,
                               utf8_buffer* value,
                               void *data /* RECTANGLE* */)
{
  RECTANGLE *rect = data ;
  utf8_buffer scan ;
  double values[4] ;

  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(rect, "Nowhere to put converted rectangle") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  (void)xml_match_space(&scan) ; /* Possible leading spaces */

  if ( !xps_convert_dbl_prn(filter, attrlocalname, &scan, &values[0]) )
    return FALSE ;

  if (! xps_match_scs_collapse(&scan))
    return error_handler(SYNTAXERROR) ;

  if ( !xps_convert_dbl_prn(filter, attrlocalname, &scan, &values[1]) )
    return FALSE ;

  if (! xps_match_scs_collapse(&scan))
    return error_handler(SYNTAXERROR) ;

  if ( !xps_convert_dbl_prn(filter, attrlocalname, &scan, &values[2]) )
    return FALSE ;

  if (! xps_match_scs_collapse(&scan))
    return error_handler(SYNTAXERROR) ;

  if ( !xps_convert_dbl_prn(filter, attrlocalname, &scan, &values[3]) )
    return FALSE ;

  rect->x = values[0] ;
  rect->y = values[1] ;
  rect->w = values[2] ;
  rect->h = values[3] ;

  (void)xml_match_space(&scan) ; /* Possible trailing spaces */
  *value = scan ;

  return TRUE;
}

/*
    <!-- BleedBox: 4 numbers, separated by , and arbitrary whitespace. Second number pair must be non-negative -->
        <xs:simpleType name="ST_BleedBox">
        <xs:restriction base="xs:string">
            <xs:whiteSpace value="collapse" />
<!--
            <xs:pattern value="[rn][scs][rn][scs][prn][scs][prn]"/>
-->
            <xs:pattern value="((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))
                               ((e|E)(\-|\+)?[0-9]+)?)( ?, ?)(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_BleedBox(xmlGFilter *filter,
                             xmlGIStr *attrlocalname,
                             utf8_buffer* value,
                             void *data /* RECTANGLE* */)
{

  /* Happens to be identical to a ST_ViewBox */
  return xps_convert_ST_ViewBox(filter, attrlocalname, value, data) ;
}

/* As per 0.90 schema0.xsd:

   <!-- Color: 6 or 8 hex digits -->
    <xs:simpleType name="ST_Color">
        <xs:restriction base="xs:string">
            <!-- The pattern restriction does not check for scRGB gamut -->
            <!-- The pattern restriction does not check for color profile URI validity -->
            <xs:whiteSpace value="collapse" />
<!--
            <xs:pattern value="(#([0-9a-fA-F]{2})?[0-9a-fA-F]{6})|\
                                (sc# ?[dec][scs][dec][scs][dec]([scs][dec])?)|\
                                (ContextColor +[\S]+ ?[dec]([scs][dec]){3,8})"/>
-->
            <xs:pattern value="(#([0-9a-fA-F]{2})?[0-9a-fA-F]{6})|(sc# ?(\-?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+)))( ?, ?)
                               (\-?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+)))( ?, ?)(\-?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+)))(( ?, ?)(\-?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))))?)
                               |(ContextColor +[\S]+ ?(\-?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+)))(( ?, ?)(\-?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+)))){3,8})" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_Color(xmlGFilter *filter,
                          xmlGIStr *attrlocalname,
                          utf8_buffer* value,
                          void *data /* xps_color_designator* */)
{
  xps_color_designator* color_designator = data ;
  uint32 i, argb_value = 0 ;
  utf8_buffer scan ;
  int32 error_result ;
  xps_partname_t *color_profile_partname  = NULL ;
  double dbl_num ;

  static utf8_buffer match_sc = {UTF8_AND_LENGTH("sc#")} ;
  static utf8_buffer match_context_color = {UTF8_AND_LENGTH("ContextColor")} ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(color_designator != NULL, "color_designator is null") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  /* Initialise the alpha, colorant values, count and colorspace */
  color_designator->alpha = -1;
  color_designator->n_colorants = 0;
  for ( i = 0; i < 8; i++ ) color_designator->color[i] = -1;

  /* If this callback gets called, we have a Color attribute set. */
  color_designator->color_set = TRUE ;

  (void)xml_match_space(&scan) ; /* Possible leading spaces */

  /* (#([0-9a-fA-F]{2})?[0-9a-fA-F]{6}) */
  if (xml_match_unicode(&scan, '#')) {

    for ( i = 0 ; i < 8 && i < scan.unitlength ; ++i ) {
      int8 nibble = char_to_hex_nibble[scan.codeunits[i]] ;

      if ( nibble < 0 )
        break ;

      argb_value = (argb_value << 4) | nibble ;
    }

    switch ( i ) {
    case 6:
      /* RGB */
      color_designator->alpha = 1.0; /* Default opacity */
      color_designator->color[0] = (USERVALUE)(((argb_value >> 16) & 0xff) / 255.0) ;
      color_designator->color[1] = (USERVALUE)(((argb_value >> 8) & 0xff) / 255.0) ;
      color_designator->color[2] = (USERVALUE)(((argb_value >> 0) & 0xff) / 255.0) ;
      break ;
    case 8:
      /* ARGB */
      color_designator->alpha = (USERVALUE)(((argb_value >> 24) & 0xff) / 255.0) ;
      color_designator->color[0] = (USERVALUE)(((argb_value >> 16) & 0xff) / 255.0) ;
      color_designator->color[1] = (USERVALUE)(((argb_value >> 8) & 0xff) / 255.0) ;
      color_designator->color[2] = (USERVALUE)(((argb_value >> 0) & 0xff) / 255.0) ;
      break ;
    default:
      return error_handler(SYNTAXERROR) ;
    }

    color_designator->n_colorants = 3 ;
    color_designator->colorspace = sRGB ;

    scan.codeunits += i ;
    scan.unitlength -= i ;

  /* (sc# ?[dec][scs][dec][scs][dec]([scs][dec])?) */
  } else if (xml_match_string(&scan, &match_sc)) {
    (void)xml_match_space(&scan) ;

    if (! xps_xml_to_double(&scan, &dbl_num, xps_dec, &error_result))
      return error_handler(error_result) ;

    color_designator->color[0] = (USERVALUE) dbl_num ;

    if (! xps_match_scs_collapse(&scan))
      return error_handler(SYNTAXERROR) ;

    if (! xps_xml_to_double(&scan, &dbl_num, xps_dec, &error_result))
      return error_handler(error_result) ;

    color_designator->color[1] = (USERVALUE) dbl_num ;

    if (! xps_match_scs_collapse(&scan))
      return error_handler(SYNTAXERROR) ;

    if (! xps_xml_to_double(&scan, &dbl_num, xps_dec, &error_result))
      return error_handler(error_result) ;

    color_designator->color[2] = (USERVALUE) dbl_num ;

    /* If last value exists, it means an alpha was specified. */
    if (xps_match_scs_collapse(&scan)) {
      if (! xps_xml_to_double(&scan, &dbl_num, xps_dec, &error_result))
        return error_handler(error_result) ;

      /* Need to cycle stuff around as alpha comes first */
      color_designator->alpha = color_designator->color[0] ;

      for ( i = 0; i < 2; i++ )
      {
        color_designator->color[i] = color_designator->color[i+1] ;
      }

      color_designator->color[2] = (USERVALUE) dbl_num ;

      /* We are required to clip alpha to range 0 - 1 */
      if ( color_designator->alpha < 0.0f ) {
        color_designator->alpha = 0.0f;
      }
      else if ( color_designator->alpha > 1.0f ) {
        color_designator->alpha = 1.0f;
      }
    }
    else {
      color_designator->alpha = 1.0 ;  /* Default alpha */
    }

    color_designator->n_colorants = 3 ;
    color_designator->colorspace = scRGB ;

  /* (ContextColor +[\S]+ ?[dec]([scs][dec]){3,8}) */
  } else if (xml_match_string(&scan, &match_context_color)) {
    uint32 i ;

    if (xml_match_space(&scan) == 0)
      return error_handler(SYNTAXERROR) ;

    /* Extract URI. */
    for ( i = 0 ; i < scan.unitlength ; ++i ) {
      UTF8 ch = scan.codeunits[i] ;
      if ( IS_XML_WHITESPACE(ch) )
        break ;
    }

    if (! xps_partname_new(&color_profile_partname,
                           xmlg_get_base_uri(filter),
                           scan.codeunits,
                           i,
                           XPS_NORMALISE_PARTREFERENCE))
      return error_handler(UNDEFINED) ;

    scan.codeunits += i ;
    scan.unitlength -= i ;

    /* Unsure if the regular expression ought to be one or more spaces
       rather than no spaces being allowed. Keep to spec for now. */
    (void)xml_match_space(&scan) ;

    /* Get the alpha value */
    if (! xps_xml_to_double(&scan, &dbl_num, xps_dec, &error_result)) {
      if (color_profile_partname != NULL)
        xps_partname_free(&color_profile_partname) ;
      return error_handler(error_result) ;
    }

    color_designator->alpha = (USERVALUE) dbl_num ;

    /* We are required to clip alpha to range 0 - 1 */
    if ( color_designator->alpha < 0.0f ) {
      color_designator->alpha = 0.0f;
    }
    else if ( color_designator->alpha > 1.0f ) {
      color_designator->alpha = 1.0f;
    }


    /* Get the color values and count the colorants */
    {
      int32 count = 0 ;

      while (xps_match_scs_collapse(&scan))
      {
        if (count == 8) {
          if (color_profile_partname != NULL)
            xps_partname_free(&color_profile_partname) ;
          return error_handler(SYNTAXERROR) ;
        }

        if (! xps_xml_to_double(&scan, &dbl_num, xps_dec, &error_result)) {
          if (color_profile_partname != NULL)
            xps_partname_free(&color_profile_partname) ;
          return error_handler(error_result) ;
        }

        color_designator->color[count] = (USERVALUE) dbl_num ;

        /* We are required to clip to the range 0 - 1 */
        if ( color_designator->color[count] < 0.0f ) {
          color_designator->color[count] = 0.0f;
        }
        else if ( color_designator->color[count] > 1.0f ) {
         color_designator->color[count] = 1.0f;
        }

        count++ ;
      }

      if (count < 3) {
        if (color_profile_partname != NULL)
          xps_partname_free(&color_profile_partname) ;
        return error_handler(SYNTAXERROR) ;
      }

      color_designator->n_colorants = count ;
    }

    color_designator->colorspace = iccbased ;


    /* Its now up to the caller of the match to free the part name. */
    color_designator->color_profile_partname = color_profile_partname ;

  } else {
    return error_handler(TYPECHECK) ;
  }

  (void)xml_match_space(&scan) ; /* Possible trailing spaces */
  *value = scan ;

  return TRUE ;
}

Bool xps_convert_ST_RscRefColor(xmlGFilter *filter,
                                xmlGIStr *attrlocalname,
                                utf8_buffer* value,
                                void *data /* xps_color_designator* */)
{
  xps_color_designator* color_designator = data ;
  utf8_buffer scan ;

  HQASSERT(color_designator != NULL, "color_designator is null") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  if ( xml_match_unicode(&scan, '{') ) {

    /* xps_convert_reference raises PS error. */
    return xps_convert_ST_RscRef(filter, attrlocalname, value, color_designator->elementname);

  } else {
    /* Must be a direct color attribute. */
    color_designator->color_set = TRUE;

    /* xps_convert_color raises PS error. */
    return xps_convert_ST_Color(filter, attrlocalname, value, color_designator);
  }
}


Bool xps_convert_FontUri(xmlGFilter *filter,
                         xmlGIStr *attrlocalname,
                         utf8_buffer* value,
                         void *data /* xps_fonturi_designator* */)
{
  xps_fonturi_designator *fonturi_designator = data ;

  HQASSERT(fonturi_designator != NULL, "fonturi_designator is NULL") ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  fonturi_designator->fontface_index = -1 ;

  if (! xps_partname_new(&(fonturi_designator->font),
                         xmlg_get_base_uri(filter),
                         value->codeunits,
                         value->unitlength,
                         XPS_NORMALISE_PARTREFERENCE))
    return error_handler(UNDEFINED) ;

  HQASSERT(fonturi_designator->font != NULL, "font is NULL") ;

  /* Deal with any fragment specified on the font uri. */
  {
    hqn_uri_t *fonturi = fonturi_designator->font->uri ;
    uint8* fontface ;
    uint32 fontface_len ;

    HQASSERT(fonturi != NULL, "fonturi is NULL") ;

    if (hqn_uri_get_field(fonturi, &fontface, &fontface_len,
                          HQN_URI_FRAGMENT)) {
      utf8_buffer value ;
      int32 error_result ;
      int32 fontface_index ;

      value.codeunits = fontface ;
      value.unitlength = fontface_len ;

      /* If we have a fragment, it must be a positive integer. */
      if (! xps_xml_to_int(&value, &fontface_index, xps_uint, &error_result))
        return error_handler(error_result) ;

      fonturi_designator->fontface_index = fontface_index ;
    }
  }

  value->codeunits  = UTF_BUFFER_LIMIT(*value);
  value->unitlength = 0;

  return TRUE ;
}

/* As per 0.90 schema0.xsd:

    <!-- Image reference via Uri -->
    <xs:simpleType name="ST_UriImage">
        <xs:restriction base="xs:anyURI">
            <xs:pattern value="([^\{].*)?" />
        </xs:restriction>
    </xs:simpleType>

    <!-- Image reference via ColorConvertedBitmap -->
    <xs:simpleType name="ST_CtxBmpImage">
        <xs:restriction base="xs:string">
            <xs:pattern value="\{ColorConvertedBitmap[\s]+[\S]+[\s]+[\S]+\}[\s]*" />
        </xs:restriction>
    </xs:simpleType>

    <!-- Image reference via Uri or ColorConvertedBitmap -->
    <xs:simpleType name="ST_UriCtxBmp">
        <xs:union memberTypes="ST_UriImage ST_CtxBmpImage" />
    </xs:simpleType>
 */
Bool xps_convert_ST_UriCtxBmp(xmlGFilter *filter,
                              xmlGIStr *attrlocalname,
                              utf8_buffer* value,
                              void *data /* xps_imagesource_designator* */)
{
  xps_imagesource_designator* imagesource_designator = data ;
  xps_partname_t *imagepart = NULL, *profilepart = NULL ;
  utf8_buffer scan ;
  static utf8_buffer match_color_converted_bitmap = {UTF8_AND_LENGTH("{ColorConvertedBitmap")} ;

  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(imagesource_designator != NULL, "imagesource_designator is null") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  /* \{ColorConvertedBitmap */
  if (xml_match_string(&scan, &match_color_converted_bitmap)) {
    uint32 i ;
    Bool found_closebrace = FALSE ;

    if (xml_match_space(&scan) == 0)
      return error_handler(SYNTAXERROR) ;

    /* The extent of the image URI is up to and not including the
       first space we find. */
    for ( i = 0 ; i < scan.unitlength ; ++i ) {
      UTF8 ch = scan.codeunits[i] ;
      if ( IS_XML_WHITESPACE(ch) )
        break ;
    }

    /* Image URI */
    if (! xps_partname_new(&imagepart,
                           xmlg_get_base_uri(filter),
                           scan.codeunits,
                           i,
                           XPS_NORMALISE_PARTREFERENCE))
      return error_handler(UNDEFINED) ;

    scan.codeunits += i ;
    scan.unitlength -= i ;

    if (xml_match_space(&scan) == 0)
      return error_handler(SYNTAXERROR) ;

    /* Must be at least a char followed by }. Note this protects below
       while loop from wrapping if unitlength is zero at this
       point. */
    if (scan.unitlength < 2)
      return error_handler(SYNTAXERROR) ;

    /* Scan backwards looking for closing '}' */

    /* Look for trailing }, from the end of the attribute value. In
       most cases this will be quicker. By this stage it MUST exist so
       its an error if the char is not whitespace or the } char. */
    while ( --scan.unitlength > 0 ) {
      if ( IS_XML_WHITESPACE(scan.codeunits[scan.unitlength]) ) {
        continue ;
      } else if ( scan.codeunits[scan.unitlength] == '}' ) {
        found_closebrace = TRUE ;
        break ;
      } else {
        return error_handler(SYNTAXERROR) ;
      }
    }

    if (scan.unitlength == 0 || ! found_closebrace)
      return error_handler(SYNTAXERROR) ;

    /* Catch ' }' situation */
    if ( IS_XML_WHITESPACE(scan.codeunits[scan.unitlength - 1]) )
      return error_handler(SYNTAXERROR) ;

    /* Remaining string is profile URI */
    if (! xps_partname_new(&profilepart,
                           xmlg_get_base_uri(filter),
                           scan.codeunits,
                           scan.unitlength,
                           XPS_NORMALISE_PARTREFERENCE)) {
      xps_partname_free(&imagepart) ;
      HQASSERT(imagepart == NULL, "imagepart is not NULL") ;
      return error_handler(UNDEFINED) ;
    }

  } else { /* ST_UriImage */
    if (! xps_partname_new(&imagepart,
                           xmlg_get_base_uri(filter),
                           scan.codeunits,
                           scan.unitlength,
                           XPS_NORMALISE_PARTREFERENCE))
      return error_handler(UNDEFINED) ;
  }

  scan.codeunits  = UTF_BUFFER_LIMIT(scan);
  scan.unitlength = 0;

  /* setup return values */
  *value = scan ;
  imagesource_designator->image = imagepart ;
  imagesource_designator->profile = profilepart ;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd

    <!-- Bare Matrix form: 6 numbers separated by , and arbitrary whitespace -->
    <xs:simpleType name="ST_Matrix">
        <xs:restriction base="xs:string">
            <xs:whiteSpace value="collapse" />
<!--
            <xs:pattern value="[rn][scs][rn][scs][rn][scs][rn][scs][rn][scs][rn]"/>
-->
            <xs:pattern value="((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)
                                ( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)
                                ( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)" />
        </xs:restriction>
    </xs:simpleType> */
Bool xps_convert_ST_Matrix(xmlGFilter *filter,
                           xmlGIStr *attrlocalname,
                           utf8_buffer* value,
                           void *data /* OMATRIX* */)
{
  OMATRIX *matrix = data ;
  utf8_buffer scan ;
  uint32 i ;

  HQASSERT(matrix, "Nowhere to put converted matrix") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  (void)xml_match_space(&scan) ; /* Possible leading spaces */

  for (i = 0; i < 6; ++i) {
    if (i > 0 && ! xps_match_scs_collapse(&scan))
      return error_handler(SYNTAXERROR) ;

    /* xps_convert_dbl_rn raises PS error. */
    if ( !xps_convert_dbl_rn(filter, attrlocalname, &scan, &matrix->matrix[i >> 1][i & 1]) )
      return FALSE ;
  }

  MATRIX_SET_OPT_BOTH(matrix);

  (void)xml_match_space(&scan) ; /* Possible trailing spaces */
  *value = scan ;

  return TRUE;
}

/* From XPS 0.90 s0schema.xsd
    <!-- Resource reference OR Compact Matrix-->
    <xs:simpleType name="ST_RscRefMatrix">
        <xs:union memberTypes="ST_Matrix ST_RscRef" />
    </xs:simpleType>
*/
Bool xps_convert_ST_RscRefMatrix(xmlGFilter *filter,
                                 xmlGIStr *attrlocalname,
                                 utf8_buffer* value,
                                 void *data /* xps_matrix_designator* */)
{
  xps_matrix_designator* matrix_designator = data;
  utf8_buffer scan ;

  HQASSERT(matrix_designator, "matrix_designator missing") ;

  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  if ( xml_match_unicode(&scan, '{') ) {
    /* Must be a reference to a verbose path. */
    /* xps_convert_reference raises PS error. */
    return xps_convert_ST_RscRef(filter, attrlocalname, value, matrix_designator->elementname);

  } else {
    OMATRIX matrix;

    /* xps_convert_matrix raises PS error. */
    if (! xps_convert_ST_Matrix(filter, attrlocalname, &scan, &matrix))
      return FALSE;

    /* xps_convert_matrix consumes leading and trailing spaces */

    /* Nothing more allowed. */
    if ( scan.unitlength != 0 )
      return error_handler(SYNTAXERROR) ;

    /* matrix_designator->matrix may not be the identity matrix. */
    matrix_mult(matrix_designator->matrix, &matrix, matrix_designator->matrix);

    *value = scan ;

    return TRUE;
  }
}

/* From Relationships.xsd 0.85
  <xsd:attribute name="Type" type="xsd:anyURI" use="required" />
*/
Bool xps_convert_Type(xmlGFilter *filter,
                      xmlGIStr *attrlocalname,
                      utf8_buffer* value,
                      void *data /* xmlGIStr** */)
{
  xmlGIStr **intern = data ;
  utf8_buffer scan ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(intern, "Nowhere to put interned string") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  if ( !intern_create(intern, scan.codeunits, scan.unitlength) )
    return FALSE ;

  /* Guaranteed to be at end of value. */
  value->codeunits  = UTF_BUFFER_LIMIT(*value) ;
  value->unitlength = 0 ;

  return TRUE ;
}

Bool xps_convert_utf8(xmlGFilter *filter,
                      xmlGIStr *attrlocalname,
                      utf8_buffer* value,
                      void *data /* utf8_buffer* */)
{
  utf8_buffer *utf8 = data ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT((value != NULL),
           "xps_convert_utf8: NULL utf8 value pointer");
  HQASSERT(utf8, "Nowhere to put converted UTF8 string") ;

  *utf8 = *value;

  value->codeunits  = UTF_BUFFER_LIMIT(*value);
  value->unitlength = 0;
  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd

    <xs:simpleType name="ST_RscRef">
        <xs:restriction base="xs:string">
            <xs:pattern value="\{StaticResource[\s]+[\S]+\}[\s]*" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_RscRef(xmlGFilter *filter,
                           xmlGIStr *attrlocalname,
                           utf8_buffer* value,
                           void *data /* xmlGIStr* */)
{
  xmlGIStr *elementname = data ;
  utf8_buffer scan, refname ;

  HQASSERT(elementname, "No element name for resource reference") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  /* match static resource enforces at least one whitespace after the
     {StaticResource */
  if (! xps_match_static_resource(&scan))
    return error_handler(SYNTAXERROR) ;

  refname = scan ;

  /* Check syntax for resource references - Length must be at least 2 for a one
     character name and the trailing } */
  if ( refname.unitlength < 2 )
    return error_handler(SYNTAXERROR) ;

  /* Look for trailing }, from the end of the attribute value. In most
     cases this will be quicker. */
  while ( --refname.unitlength > 0 ) {
    if ( refname.codeunits[refname.unitlength] == '}' )
      break ;
  }

  if (refname.unitlength == 0)
    return error_handler(SYNTAXERROR) ;

  if (! xps_resource_reference(filter, elementname, attrlocalname,
                               refname.codeunits, refname.unitlength) )
    return error_handler(UNDEFINED) ;

  scan.codeunits += refname.unitlength;
  scan.unitlength -= refname.unitlength ;

  if (! xml_match_unicode(&scan, '}') )
    return error_handler(SYNTAXERROR) ;

  (void)xml_match_whitespace(&scan) ; /* Possible trailing spaces */
  *value = scan ;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- Abbreviated Geometry grammar for Path.Data , clip and Geometries -->
    <xs:simpleType name="ST_AbbrGeomF">
        <xs:restriction base="xs:string">
            <xs:whiteSpace value="collapse" />
<!--
            <xs:pattern value="(F ?(0|1))?\
                                ( ?(M|m)( ?[rn][scs][rn]))\
                                (\
                                    ( ?(M|m)( ?[rn][scs][rn]))|\
                                    ( ?(L|l)( ?[rn][scs][rn])( [rn][scs][rn])*)|\
                                    ( ?(H|h|V|v)( ?[rn])( [rn])*)|\
                                    ( ?(Q|q|S|s)( ?[rn][scs][rn] [rn][scs][rn])(( [rn][scs][rn]){2})*)|\
                                    ( ?(C|c)( ?[rn][scs][rn]( [rn][scs][rn]){2})(( [rn][scs][rn]){3})*)|\
                                    ( ?(A|a)( ?[rn][scs][rn] [rn] [0-1] [0-1] [rn][scs][rn])\
                                                    ( [rn][scs][rn] [rn] [0-1] [0-1] [rn][scs][rn])*)|\
                                    ( ?(Z|z))\
                                )*"/>
-->
            <xs:pattern value="(F ?(0|1))?( ?(M|m)( ?((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|
                               (\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)))(( ?(M|m)( ?((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)
                               |(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)))|( ?(L|l)( ?((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)
                               |(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))( ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)
                               |(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))*)|( ?(H|h|V|v)( ?((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))( ((\-|\+)?(([0-9]+(\.[0-9]+)?)
                               |(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))*)|( ?(Q|q|S|s)( ?((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)
                               |(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?) ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)
                               |(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))(( ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)
                               |(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)){2})*)|( ?(C|c)( ?((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)
                               |(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|
                               (\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)){2})(( ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|
                               (\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)){3})*)|( ?(A|a)( ?((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|
                               (\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?) ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?) [0-1] [0-1] ((\-|\+)?(([0-9]+(\.[0-9]+)?)
                               |(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))( ((\-|\+)?(([0-9]+(\.[0-9]+)?)
                               |(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?) ((\-|\+)?(([0-9]+(\.[0-9]+)?)
                               |(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?) [0-1] [0-1] ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)
                               |(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))*)|( ?(Z|z)))*" />
        </xs:restriction>
    </xs:simpleType>
*/
static Bool convert_abbr_path(xmlGFilter *filter,
                              xmlGIStr *attrlocalname,
                              utf8_buffer* value,
                              PATHINFO *path,
                              int32 *fillrule)
{
  utf8_buffer scan ;
  int32 last_op = -1;
  SYSTEMVALUE args[6];
  SYSTEMVALUE currentpoint[2] = {0.0, 0.0};
  SYSTEMVALUE second_bezier_point[2] = {0.0, 0.0};
  Bool bezier_last = FALSE;
  Bool path_is_closed = FALSE ;
  Bool fill_rule_seen = FALSE ;
  Bool move_to_seen = FALSE ;
  OMATRIX inverse_ctm;

  HQASSERT(filter != NULL, "filter is NULL") ;

  /* The path is treated as completely transparent if the CTM is non-invertible . */
  if ( !matrix_inverse(&thegsPageCTM(*gstateptr), &inverse_ctm) ) {
    value->codeunits  = UTF_BUFFER_LIMIT(*value) ;
    value->unitlength = 0 ;
    return TRUE ;
  }

  HQASSERT(value != NULL, "No UTF-8 string to convert") ;
  scan = *value ;

  /* There may be white space before the very first op. */
  (void)xml_match_space(&scan) ;

  while ( scan.unitlength > 0 ) {
    Bool absolute = TRUE;
    utf8_buffer scan_retry = scan;
    int32 op;

    op = utf8_iterator_get_next(&scan);

    /* All op's can be followed by whitespace, with collapse */
    (void)xml_match_space(&scan) ;

   retry_path_op:

    path_is_closed = FALSE ;

    switch (op) {
    case 'F': {
      UTF32 rule;

      /* fillrule may or may not be allowed in this usage. */
      if ( fillrule == NULL || last_op != -1 )
        return error_handler(SYNTAXERROR);

      /* only one fill rules is allowed. */
      if (fill_rule_seen)
        return error_handler(SYNTAXERROR);

      rule = utf8_iterator_get_next(&scan);

      if ( rule == '0' )
        *fillrule = EOFILL_TYPE;
      else if ( rule == '1' )
        *fillrule = NZFILL_TYPE;
      else
        return error_handler(SYNTAXERROR) ;

      fill_rule_seen = TRUE ;
      break ;
    }

    case 'm': /* Moveto: m x,y */

      /* convert_point raises PS error. */
      if (! convert_point(filter, attrlocalname, &scan, &args[0]))
        return FALSE;

      currentpoint[0] += args[0];
      currentpoint[1] += args[1];

      if (! gs_moveto(TRUE, currentpoint, path))
        return FALSE;

      move_to_seen = TRUE ;
      break;

    case 'M': /* Moveto: M x,y */

      /* convert_point raises PS error. */
      if (! convert_point(filter, attrlocalname, &scan, &currentpoint[0]) ||
          ! gs_moveto(TRUE, currentpoint, path))
        return FALSE;

      move_to_seen = TRUE ;
      break;

    case 'l': /* Lineto: l x,y */

      /* convert_point raises PS error. */
      if (! convert_point(filter, attrlocalname, &scan, &args[0]))
        return FALSE;

      currentpoint[0] += args[0];
      currentpoint[1] += args[1];

      if (! gs_lineto(TRUE, TRUE, currentpoint, path))
        return FALSE;

      break;

    case 'L': /* Lineto: L x,y */

      if (! convert_point(filter, attrlocalname, &scan, &currentpoint[0]) ||
          ! gs_lineto(TRUE, TRUE, currentpoint, path))
        return FALSE;

      break;

    case 'h': /* Horizontal line: h x */

      if (! xps_convert_dbl_rn(filter, attrlocalname, &scan, &args[0]))
        return FALSE;

      currentpoint[0] += args[0];

      if (! gs_lineto(TRUE, TRUE, currentpoint, path))
        return FALSE;

      break;

    case 'H': /* Horizontal line: H x */

      if (! xps_convert_dbl_rn(filter, attrlocalname, &scan, &currentpoint[0]) ||
          ! gs_lineto(TRUE, TRUE, currentpoint, path))
        return FALSE;

      break;

    case 'v': /* Vertical line: v x */

      if (! xps_convert_dbl_rn(filter, attrlocalname, &scan, &args[1]))
        return FALSE;

      currentpoint[1] += args[1];

      if (! gs_lineto(TRUE, TRUE, currentpoint, path))
        return FALSE;

      break;

    case 'V': /* Vertical line: V x */

      if (! xps_convert_dbl_rn(filter, attrlocalname, &scan, &currentpoint[1]) ||
          ! gs_lineto(TRUE, TRUE, currentpoint, path))
        return FALSE;

      break;

    case 'c': /* Cubic bezier curve: c x1,y1 x2,y2 x3,y3 */
      absolute = FALSE;
      /* fall through */

    case 'C': /* Cubic bezier curve: C x1,y1 x2,y2 x3,y3 */

      if (! convert_point(filter, attrlocalname, &scan, &args[0]))
        return FALSE;

      if (xml_match_space(&scan) == 0)
        return error_handler(SYNTAXERROR) ;

      if (! convert_point(filter, attrlocalname, &scan, &args[2]))
        return FALSE;

      if (xml_match_space(&scan) == 0)
        return error_handler(SYNTAXERROR) ;

      if (! convert_point(filter, attrlocalname, &scan, &args[4]))
        return FALSE;

      if (! absolute) {
        args[0] += currentpoint[0];
        args[1] += currentpoint[1];
        args[2] += currentpoint[0];
        args[3] += currentpoint[1];
        args[4] += currentpoint[0];
        args[5] += currentpoint[1];
      }

      if (! gs_curveto(TRUE, TRUE, args, path))
        return FALSE;

      second_bezier_point[0] = args[2];
      second_bezier_point[1] = args[3];

      currentpoint[0] = args[4];
      currentpoint[1] = args[5];

      break;

    case 'q': /* Quadratic bezier curve: q x1,y1 x2,y2 */

      absolute = FALSE;
      /* fall through */

    case 'Q': /* Quadratic bezier curve: Q x1,y1 x2,y2 */

      if (! convert_point(filter, attrlocalname, &scan, &args[0]))
        return FALSE;

      if (xml_match_space(&scan) == 0)
        return error_handler(SYNTAXERROR) ;

      if (! convert_point(filter, attrlocalname, &scan, &args[2]))
        return FALSE;

      if (! absolute) {
        args[0] += currentpoint[0];
        args[1] += currentpoint[1];
        args[2] += currentpoint[0];
        args[3] += currentpoint[1];
      }

      if (! gs_quadraticcurveto(TRUE, TRUE, args, path))
        return FALSE;

      currentpoint[0] = args[2];
      currentpoint[1] = args[3];

      break;

    case 's': /* Smooth cubic bezier curve: s x1,y1 x2,y2 */

      absolute = FALSE;
      /* fall through */

    case 'S': /* Smooth cubic bezier curve: S x1,y1 x2,y2 */

      if (! convert_point(filter, attrlocalname, &scan, &args[2]))
        return FALSE;

      if (xml_match_space(&scan) == 0)
        return error_handler(SYNTAXERROR) ;

      if (! convert_point(filter, attrlocalname, &scan, &args[4]))
        return FALSE;

      if (bezier_last) {
        /* First control point of this bezier is a reflection of the previous
           bezier's second control point relative to the current point. */
        args[0] = currentpoint[0] + currentpoint[0] - second_bezier_point[0];
        args[1] = currentpoint[1] + currentpoint[1] - second_bezier_point[1];
      } else {
        args[0] = currentpoint[0];
        args[1] = currentpoint[1];
      }

      if (! absolute) {
        args[2] += currentpoint[0];
        args[3] += currentpoint[1];
        args[4] += currentpoint[0];
        args[5] += currentpoint[1];
      }

      if (! gs_curveto(TRUE, TRUE, args, path))
        return FALSE;

      second_bezier_point[0] = args[2];
      second_bezier_point[1] = args[3];

      currentpoint[0] = args[4];
      currentpoint[1] = args[5];

      break;

    case 'a': /* Elliptical arc: a xr,yr rx flag1 flag2 x,y */
      absolute = FALSE;
      /* fall through */

    case 'A': /* Elliptical arc: A xr,yr rx flag1 flag2 x,y */
    {
      Bool largearc, sweepflag;
      int32 flag;

      if (! convert_point_prn(filter, attrlocalname, &scan, &args[0]))
        return FALSE;

      if (xml_match_space(&scan) == 0)
        return error_handler(SYNTAXERROR) ;

      if (! xps_convert_dbl_rn(filter, attrlocalname, &scan, &args[2]))
        return FALSE;

      args[2] *= DEG_TO_RAD;

      if (xml_match_space(&scan) == 0)
        return error_handler(SYNTAXERROR) ;

      flag = utf8_iterator_get_next(&scan);

      if ( flag == '0' )
        largearc = FALSE;
      else if ( flag == '1' )
        largearc = TRUE;
      else
        return error_handler(SYNTAXERROR) ;

      if (xml_match_space(&scan) == 0)
        return error_handler(SYNTAXERROR) ;

      flag = utf8_iterator_get_next(&scan);

      if ( flag == '0' )
        sweepflag = FALSE;
      else if ( flag == '1' )
        sweepflag = TRUE;
      else
        return error_handler(SYNTAXERROR) ;

      if (xml_match_space(&scan) == 0)
        return error_handler(SYNTAXERROR) ;

      if (! convert_point(filter, attrlocalname, &scan, &args[3]))
        return FALSE;

      if (! absolute) {
        args[3] += currentpoint[0];
        args[4] += currentpoint[1];
      }

      if (! gs_ellipticalarcto(TRUE, TRUE, largearc, sweepflag,
                               args[0], args[1], args[2], args[3], args[4],
                               path))
        return FALSE;

      currentpoint[0] = args[3];
      currentpoint[1] = args[4];

      break;
    }

    case 'z':
    case 'Z':
      path_is_closed = TRUE ;

      if (! path_close(CLOSEPATH, path))
        return FALSE;

      if (! gs_currentpoint(path, &currentpoint[0], &currentpoint[1]))
        return FALSE;

      break;

    default:
      /* Handle the case where repeated commands can be omitted. If
         there hasn't been an operator yet then we're struggling. */
      if (last_op == -1)
        return error_handler(SYNTAXERROR);

      /* Only one at a time please */
      if (last_op == 'm' || last_op == 'M')
        return error_handler(SYNTAXERROR);

      /* Undo whatever was scanned in this iteration and assume
         the operator is the same as last time. */
      scan = scan_retry;
      op = last_op;
      goto retry_path_op;
    }

    (void)xml_match_space(&scan) ; /* Possible trailing spaces */

    bezier_last = (op == 's' || op == 'S' || op == 'c' || op == 'C');

    last_op = op;
  }

  if (! path_is_closed) {
    if (! path_close(MYCLOSE, path))
      return FALSE;
  }

  *value = scan ;

  /* require at least a single move to */
  if (! move_to_seen)
    return error_handler(SYNTAXERROR);

  return TRUE;
}

Bool xps_convert_ST_RscRefAbbrGeom(xmlGFilter *filter,
                                   xmlGIStr *attrlocalname,
                                   utf8_buffer* value,
                                   void *data /* xps_abbrevgeom_designator* */)
{
  xps_abbrevgeom_designator* abbrevgeom_designator = data;
  utf8_buffer scan ;

  HQASSERT(abbrevgeom_designator, "No abbrevgeom_designator missing") ;

  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  if ( xml_match_unicode(&scan, '{') ) {
    /* Must be a reference to a verbose path. */
    return xps_convert_ST_RscRef(filter, attrlocalname, value,
                                 abbrevgeom_designator->elementname);
  }
  else {
    /* Otherwise must be path mini language which will be scanned
       later after all possible ways of specifying a transform. */
    return xps_convert_utf8(filter, attrlocalname, value,
                            &abbrevgeom_designator->attributebuffer) ;
  }
}

Bool xps_convert_ST_AbbrGeomF(xmlGFilter *filter,
                              xmlGIStr *attrlocalname,
                              utf8_buffer* value,
                              void *data /* xps_path_designator* */)
{
  xps_path_designator* path_designator = data;

  HQASSERT(path_designator, "No path_designator missing") ;

  HQASSERT(value != NULL, "No UTF-8 string to convert");

  return convert_abbr_path(filter, attrlocalname, value,
                           path_designator->path,
                           &path_designator->fillrule);
}

/* s0schema.xsd 0.90
    <!-- Abbreviated Geometry grammar for PatGeometry.Figures -->
    <xs:simpleType name="ST_AbbrGeom">
        <xs:restriction base="xs:string">
            <xs:whiteSpace value="collapse" />
<!--
            <xs:pattern value="( ?(M|m)( ?[rn][scs][rn]))\
                                (\
                                    ( ?(M|m)( ?[rn][scs][rn]))|\
                                    ( ?(L|l)( ?[rn][scs][rn])( [rn][scs][rn])*)|\
                                    ( ?(H|h|V|v)( ?[rn])( [rn])*)|\
                                    ( ?(Q|q|S|s)( ?[rn][scs][rn] [rn][scs][rn])(( [rn][scs][rn]){2})*)|\
                                    ( ?(C|c)( ?[rn][scs][rn]( [rn][scs][rn]){2})(( [rn][scs][rn]){3})*)|\
                                    ( ?(A|a)( ?[rn][scs][rn] [rn] [0-1] [0-1] [rn][scs][rn])\
                                                    ( [rn][scs][rn] [rn] [0-1] [0-1] [rn][scs][rn])*)|\
                                    ( ?(Z|z))\
                                )*"/>
-->
            <xs:pattern value="( ?(M|m)( ?((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)
                               |(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)))(( ?(M|m)( ?((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)
                               ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)))|( ?(L|l)( ?((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)
                               ( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))( ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)
                               ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))*)|( ?(H|h|V|v)( ?((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))
                               ( ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))*)|( ?(Q|q|S|s)( ?((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)
                               ( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?) ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)
                               ( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))(( ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)
                               ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)){2})*)|( ?(C|c)( ?((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)
                               ( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)
                               ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)){2})(( ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)
                               ( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)){3})*)|( ?(A|a)( ?((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))
                               ((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?) ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)
                               (\-|\+)?[0-9]+)?) [0-1] [0-1] ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))
                               ((e|E)(\-|\+)?[0-9]+)?))( ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))
                               ((e|E)(\-|\+)?[0-9]+)?) ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?) [0-1] [0-1] ((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))
                               ((e|E)(\-|\+)?[0-9]+)?)( ?, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))*)|( ?(Z|z)))*" />
        </xs:restriction>
    </xs:simpleType>
 */
Bool xps_convert_ST_AbbrGeom(xmlGFilter *filter,
                             xmlGIStr *attrlocalname,
                             utf8_buffer* value,
                             void *data /* PATHINFO* */)
{
  HQASSERT(data, "abbreviated path data is missing") ;

  return convert_abbr_path(filter, attrlocalname, value, (PATHINFO*)data,
                           NULL /* fill rule not allowed */);
}

/* From rdkey.xsd 0.90

  <xs:attribute name="Key">
    <xs:simpleType>
      <xs:restriction base="xs:string">
        <!-- A Key (pattern restriction according to XPS spec) -->
        <xs:pattern value="(\p{Lu}|\p{Ll}|\p{Lt}|\p{Lo}|\p{Nl}|_)(\p{Lu}|\p{Ll}|\p{Lt}|\p{Lo}|\p{Nl}|\p{Mn}|\p{Mc}|\p{Nd}|_)*" />
      </xs:restriction>
    </xs:simpleType>
  </xs:attribute>
*/
Bool xps_convert_Key(xmlGFilter *filter,
                     xmlGIStr *attrlocalname,
                     utf8_buffer* value,
                     void *data /* utf8_buffer* */)
{
  utf8_buffer *utf8 = data ;
  utf8_buffer scan ;
  UTF32 c ;
  HqBool error_occured = FALSE ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(utf8, "Nowhere to put converted UTF8 string") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  if (! utf8_iterator_more(&scan))
    return error_handler(SYNTAXERROR) ;

  /* first char */
  c = utf8_iterator_get_next(&scan) ;

  if (c != '_' &&  /* Allow underscore */
      (! unicode_has_binary_property(c, UTF32_ID_START, &error_occured) ||
       (unicode_char_type(c, &error_occured) == UTF32_MODIFIER_LETTER) ) ) {  /* No Lm */

    return error_handler(error_occured ? VMERROR : SYNTAXERROR) ;
  }

  /* scan the rest of the characters to see that they are correct */
  while (utf8_iterator_more(&scan)) {
    c = utf8_iterator_get_next(&scan) ;

    if (c != '_' && /* Allow underscore */
        (! unicode_has_binary_property(c, UTF32_ID_CONTINUE, &error_occured) ||
         ((unicode_char_type(c, &error_occured) == UTF32_CONNECTOR_PUNCTUATION || /* No Pt */
           unicode_char_type(c, &error_occured) == UTF32_MODIFIER_LETTER)) ) ) { /* No Lm */
      return error_handler(error_occured ? VMERROR : SYNTAXERROR) ;
    }
  }

  *utf8 = *value ;

  /* Guaranteed to be at end of value. */
  value->codeunits  = UTF_BUFFER_LIMIT(*value) ;
  value->unitlength = 0 ;

  return TRUE ;
}

/* SO markup uses references to part names. In both cases, we
   canonicalize to the same in RIP representation. */
Bool xps_convert_part_reference(xmlGFilter *filter,
                                xmlGIStr *attrlocalname,
                                utf8_buffer* value,
                                void *data /* xps_partname_t** */)
{
  xps_partname_t **partname = data ;

  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  if (! xps_partname_new(partname,
                         xmlg_get_base_uri(filter),
                         value->codeunits,
                         value->unitlength,
                         XPS_NORMALISE_PARTREFERENCE))
    return error_handler(UNDEFINED) ;

  value->codeunits  = UTF_BUFFER_LIMIT(*value);
  value->unitlength = 0;

  return TRUE ;
}

/* The content type stream uses part names unlike the SO markup which
   uses references to part names. In both cases, we canonicalize to
   the same in RIP representation. */
Bool xps_convert_partname(xmlGFilter *filter,
                          xmlGIStr *attrlocalname,
                          utf8_buffer* value,
                          void *data /* xps_partname_t** */)
{
  xps_partname_t **partname = data ;

  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  if (! xps_partname_new(partname,
                         xmlg_get_base_uri(filter),
                         value->codeunits,
                         value->unitlength,
                         XPS_NORMALISE_PARTNAME))
    return error_handler(UNDEFINED) ;

  value->codeunits  = UTF_BUFFER_LIMIT(*value);
  value->unitlength = 0;

  return TRUE ;
}

/*
<xs:simpleType name="ST_Extension">
  <xs:restriction base="xs:string">
    <xs:pattern value="( [!$&amp;'\(\)\*\+,:=]
                        |(%[0-9a-fA-F][0-9a-fA-F])
                        |[:@]
                        |[a-zA-Z0-9\-_~]
                       )*"/>
  </xs:restriction>
</xs:simpleType>

There is also a requirement that the Extension be non-empty.
The pattern above in the latest 1.0 s0schema.xsd says one or more rather than
zero or more. We also believe the : in the first pattern ought to be a ;

So this function implements the following XSD segment.

<xs:simpleType name="ST_Extension">
  <xs:restriction base="xs:string">
    <xs:pattern value="( [!$&amp;'\(\)\*\+,;=]
                        |(%[0-9a-fA-F][0-9a-fA-F])
                        |[:@]
                        |[a-zA-Z0-9\-_~]
                       )+"/>
  </xs:restriction>
</xs:simpleType>
*/

Bool xps_convert_extension(xmlGFilter *filter,
                           xmlGIStr *attrlocalname,
                           utf8_buffer* value,
                           void *data /* xps_extension_t** */)
{
  xps_extension_t **extension = data ;

  /* Don't need the scanner as its done as part of
     xps_validate_partname_grammar() in xps_extension_new(), and does
     way more checks. Scan code for this function does exist in
     version trunk.113 of this file should we ever need it again. */
  *extension = NULL ;

  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;
  HQASSERT(value != NULL, "value is NULL") ;

  /* We are not allowed empty extensions. */
  if (value->unitlength < 1)
    return error_handler(SYNTAXERROR) ;


  if (! xps_extension_new(filter,
                          extension,
                          value->codeunits,
                          value->unitlength))
    return error_handler(UNDEFINED) ;

  value->codeunits  = UTF_BUFFER_LIMIT(*value);
  value->unitlength = 0;

  return TRUE ;
}



/* Cut from RFC 2616 which applies to the definition of mimetypes.

  implied *LWS
      The grammar described by this specification is word-based. Except
      where noted otherwise, linear white space (LWS) can be included
      between any two adjacent words (token or quoted-string), and
      between adjacent words and separators, without changing the
      interpretation of a field. At least one delimiter (LWS and/or

      separators) MUST exist between any two tokens (for the definition
      of "token" below), since they would otherwise be interpreted as a
      single token.


  LWS = [CRLF] 1*( SP | HT )
*/
static uint32 match_rfc2616_LWS(utf8_buffer* input)
{
  /* We are going to use the definition of XML whitespace as a close
     approximation. It at least covers all LWS patterns. */
  return xml_match_whitespace(input) ;
}

/* The mimetype scan code is in the XPS component because RFC 2616
   does not allow mimetypes with comments, but Microsoft do. As per
   RFC 2616:

   SP = <US-ASCII SP, space (32)>
   HT = <US-ASCII HT, horizontal-tab (9)>
   token = 1*<any CHAR except CTLs or separators>
   separators = "(" | ")" | "<" | ">" | "@"
                    | "," | ";" | ":" | "\" | <">
                    | "/" | "[" | "]" | "?" | "="
                    | "{" | "}" | SP | HT
   CTL = <any US-ASCII control character
         (octets 0 - 31) and DEL (127)>
   CHAR = <any US-ASCII character (octets 0 - 127)>
   media-type = type "/" subtype *( ";" parameter )
   type       = token
   subtype    = token

   The type, subtype, and parameter attribute names are case-
   insensitive. Parameter values might or might not be case-sensitive,
   depending on the semantics of the parameter name. Linear white
   space (LWS) MUST NOT be used between the type and subtype, nor
   between an attribute and its value. The presence or absence of a
   parameter might be significant to the processing of a media-type,
   depending on its definition within the media type registry.

   Comments can be included in some HTTP header fields by surrounding
   the comment text with parentheses. Comments are only allowed in
   fields containing "comment" as part of their field value definition.
   In all other fields, parentheses are considered part of the field
   value.

   comment = "(" *( ctext | quoted-pair | comment ) ")"
   ctext   = <any TEXT excluding "(" and ")"> */
static
Bool parse_mimetype_token(
  utf8_buffer *scan)
{
  int8  ch ;
  uint32 len ;
  utf8_buffer parse = *scan ;

#define MIMETYPE_MAX_CHAR 127

  /* TRUE/FALSE values for what is allowed/not allowed as a mime type
     char. */
  static uint8 valid_mimechar[MIMETYPE_MAX_CHAR + 1] = {
  /* NUL thru SP - 1 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  /* 032 SP */ 0,  /* 033 ! */ 1,  /* 034 " */ 0,  /* 035 # */ 1,
  /* 036 $ */ 1,  /* 037 % */ 1,  /* 038 & */ 1,  /* 039 ' */ 1,
  /* 040 ( */ 0,  /* 041 ) */ 0,  /* 042 * */ 1,  /* 043 + */ 1,
  /* 044 , */ 0,  /* 045 - */ 1,  /* 046 . */ 1,  /* 047 / */ 0,
  /* 048 0 */ 1,  /* 049 1 */ 1,  /* 050 2 */ 1,  /* 051 3 */ 1,
  /* 052 4 */ 1,  /* 053 5 */ 1,  /* 054 6 */ 1,  /* 055 7 */ 1,
  /* 056 8 */ 1,  /* 057 9 */ 1,  /* 058 : */ 0,  /* 059 ; */ 0,
  /* 060 < */ 0,  /* 061 = */ 0,  /* 062 > */ 0,  /* 063 ? */ 0,
  /* 064 @ */ 0,  /* 065 A */ 1,  /* 066 B */ 1,  /* 067 C */ 1,
  /* 068 D */ 1,  /* 069 E */ 1,  /* 070 F */ 1,  /* 071 G */ 1,
  /* 072 H */ 1,  /* 073 I */ 1,  /* 074 J */ 1,  /* 075 K */ 1,
  /* 076 L */ 1,  /* 077 M */ 1,  /* 078 N */ 1,  /* 079 O */ 1,
  /* 080 P */ 1,  /* 081 Q */ 1,  /* 082 R */ 1,  /* 083 S */ 1,
  /* 084 T */ 1,  /* 085 U */ 1,  /* 086 V */ 1,  /* 087 W */ 1,
  /* 088 X */ 1,  /* 089 Y */ 1,  /* 090 Z */ 1,  /* 091 [ */ 0,
  /* 092 \ */ 0,  /* 093 ] */ 0,  /* 094 ^ */ 1,  /* 095 _ */ 1,
  /* 096 a */ 1,  /* 098 b */ 1,  /* 099 c */ 1,  /* 100 d */ 1,
  /* 101 e */ 1,  /* 102 f */ 1,  /* 103 g */ 1,  /* 104 h */ 1,
  /* 105 i */ 1,  /* 106 j */ 1,  /* 107 k */ 1,  /* 108 l */ 1,
  /* 109 m */ 1,  /* 110 n */ 1,  /* 111 o */ 1,  /* 112 p */ 1,
  /* 113 q */ 1,  /* 114 r */ 1,  /* 115 s */ 1,  /* 116 t */ 1,
  /* 117 u */ 1,  /* 118 v */ 1,  /* 119 w */ 1,  /* 120 x */ 1,
  /* 121 y */ 1,  /* 122 z */ 1,  /* 123 { */ 1,  /* 124 | */ 1,
  /* 125 } */ 1,  /* 126 ~ */ 1,  /* 127 DEL */ 0} ;

  len = parse.unitlength ;
  while (parse.unitlength > 0) {
    ch = (int8)*(parse.codeunits) ;
    /* Handle 8 bit ascii as negative - Z flag set on load, test for
       free! */
    if ((ch < 0) || !valid_mimechar[ch]) {
      break ;
    }
    parse.codeunits++ ;
    parse.unitlength-- ;
  }

  /* Did we parse any valid token characters? */
  if (parse.unitlength < len) {
    *scan = parse ;
    return TRUE ;
  }

  return FALSE ;
}

/*
   A string of text is parsed as a single word if it is quoted using
   double-quote marks.

       quoted-string  = ( <"> *(qdtext | quoted-pair ) <"> )
       qdtext         = <any TEXT except <">>

   The backslash character ("\") MAY be used as a single-character
   quoting mechanism only within quoted-string and comment constructs.

       quoted-pair    = "\" CHAR

       CHAR           = <any US-ASCII character (octets 0 - 127)>
       TEXT           = <any OCTET except CTLs,
                        but including LWS>

       LWS            = [CRLF] 1*( SP | HT )
       CTL            = <any US-ASCII control character
                        (octets 0 - 31) and DEL (127)>
       OCTET          = <any 8-bit sequence of data>
*/

#define IS_CHAR(c) ((c) < 127)
#define IS_QDTEXT(c) (((c) > 31) && ((c) != '"') && ((c) != 127))

static
Bool parse_mimetype_quoted_string(
  utf8_buffer *scan)
{
  uint8  ch ;
  uint32 len ;
  utf8_buffer parse = *scan ;

  len = parse.unitlength ;

  if (len < 2)
    return FALSE ;

  ch = *(parse.codeunits) ;
  if (ch != '"') /* Start quote */
    return FALSE ;
  parse.codeunits++ ;
  parse.unitlength-- ;

  while (parse.unitlength > 0) {
    ch = *(parse.codeunits) ;

    if (ch == '\\') {
      ch = *(parse.codeunits) ;
      if (! IS_CHAR(ch)) /* <any US-ASCII character (octets 0 - 127)> */
        return FALSE ;
      parse.codeunits++ ;
      parse.unitlength-- ;
    } else if (! IS_QDTEXT(ch)) {
      break ;
    }
    parse.codeunits++ ;
    parse.unitlength-- ;
  }

  ch = *(parse.codeunits) ;
  if (ch != '"') /* End quote */
    return FALSE ;
  parse.codeunits++ ;
  parse.unitlength-- ;

  /* Did we parse any valid token characters? */
  if (parse.unitlength < len) {
    *scan = parse ;
    return TRUE ;
  }

  return FALSE ;
}

/* This ought to be large enough to hold most mime type
   strings. Certainly long enough for XPS mime types. */
#define MIMETYPE_BUF_SIZE 128

Bool xps_convert_mimetype(xmlGFilter *filter,
                          xmlGIStr *attrlocalname,
                          utf8_buffer* value,
                          void *data /* xmlGIStr** */)
{
  static uint8 copy_contenttype_buf[MIMETYPE_BUF_SIZE] ;
  xmlGIStr **intern = data ;
  uint8*  copy_contenttype = copy_contenttype_buf ;
  uint8*  pch ;
  uint32  len ;
  UTF32   ch ;
  Bool    ret, have_params ;
  utf8_buffer scan ;
  utf8_buffer copy ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(intern, "Nowhere to put interned string") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert") ;

  scan = *value ;
  have_params = FALSE ;

  /* Note utf8 buffer state before scanning */
  copy = scan ;

  /* Parse the type */
  if (! parse_mimetype_token(&scan))
    return error_handler(SYNTAXERROR) ;

  ch = utf8_iterator_get_next(&scan) ;

  if (ch == '/') {
    /* Parse the sub-type */
    if (! parse_mimetype_token(&scan))
      return error_handler(SYNTAXERROR) ;

    ch = utf8_iterator_get_next(&scan) ;
  }

  /* Only allowed trailing chars are start of param or whitespace
     before a param. Comments are no longer allowed. */
  if (ch != -1 && ch != ';') {

    if (match_rfc2616_LWS(&scan) > 0) { /* Needs to be whitespace. */

      /* If we matched whitespace, then we MUST have a param as we are
         not allowed trailing whitespace. */
      ch = utf8_iterator_get_next(&scan) ;
      if (ch != ';')
        return error_handler(SYNTAXERROR) ;

    } else {
      return error_handler(SYNTAXERROR) ;
    }
  }

  /* Allocate space for type/subtype copy for lowercasing */
  copy.unitlength -= scan.unitlength ;
  if (ch != -1) { /* We have a ; which we don't want. */
    copy.unitlength-- ;

    have_params = TRUE ;

    do {
      (void)match_rfc2616_LWS(&scan); /* LWS before ; */

      /* Must be a param separator */
      ch = utf8_iterator_get_next(&scan) ;
      if (ch != ';')
        return error_handler(SYNTAXERROR) ;

      (void)match_rfc2616_LWS(&scan) ; /* Whitespace after the ; */

      /* Scan parameters:
         parameter               = attribute "=" value
         attribute               = token
         value                   = token | quoted-string */
      if (! parse_mimetype_token(&scan))
        return error_handler(SYNTAXERROR) ;

      ch = utf8_iterator_get_next(&scan) ;
      if (ch != '=')
        return error_handler(SYNTAXERROR) ;

      if (! parse_mimetype_token(&scan) && ! parse_mimetype_quoted_string(&scan))
        return error_handler(SYNTAXERROR) ;

      ch = utf8_iterator_get_next(&scan) ;
    } while (ch != -1);

  }

  /* We should have consumed the entire mimetype when we get this
     far. */
  if (ch != -1)
    return error_handler(SYNTAXERROR) ;

  if (copy.unitlength > MIMETYPE_BUF_SIZE) {
    copy_contenttype = mm_alloc_with_header(mm_xml_pool, copy.unitlength,
                                            MM_ALLOC_CLASS_XPS_CONTENTTYPE) ;
    if ( copy_contenttype == NULL )
      return error_handler(VMERROR) ;
  }

  /* Copy type/subtype string forcing to lower case */
  len = copy.unitlength ;
  pch = copy_contenttype ;
  do {
    /* Gotta do this as tolower does double evaluation of its arg */
    ch = *copy.codeunits++ ;
    *pch++ = (uint8)tolower(ch) ;
  } while ( --copy.unitlength > 0 ) ;

  /* Create intern version of mime type/subtype string */
  ret = intern_create(intern, copy_contenttype, len) ;

  if (len > MIMETYPE_BUF_SIZE)
    mm_free_with_header(mm_xml_pool, copy_contenttype) ;

  if (! ret)
    return error_handler(VMERROR) ;

  if (have_params) { /* We have a parameter which is only allowed on
                        Non-OPC and Non-XPS mimetypes. */
    switch ( XML_INTERN_SWITCH (*intern) ) {
      /* XPS mime types */
    case XML_INTERN_CASE(mimetype_xps_fixeddocumentsequence):
    case XML_INTERN_CASE(mimetype_xps_fixeddocument):
    case XML_INTERN_CASE(mimetype_xps_fixedpage):
    case XML_INTERN_CASE(mimetype_xps_discard_control):
    case XML_INTERN_CASE(mimetype_xps_documentstructure):
    case XML_INTERN_CASE(mimetype_xps_resourcedictionary):
    case XML_INTERN_CASE(mimetype_xps_storyfragments):
      /* Generic package mime types */
    case XML_INTERN_CASE(mimetype_package_core_properties):
    case XML_INTERN_CASE(mimetype_package_relationships):
    case XML_INTERN_CASE(mimetype_package_signature_origin):
    case XML_INTERN_CASE(mimetype_package_signature_xmlsignature):
    case XML_INTERN_CASE(mimetype_package_signature_certificate):
      /* Printing mime types */
    case XML_INTERN_CASE(mimetype_printing_printticket):
      /* Font mime types */
    case XML_INTERN_CASE(mimetype_ms_opentype):
    case XML_INTERN_CASE(mimetype_package_obfuscated_opentype):
      /* Image mime types */
    case XML_INTERN_CASE(mimetype_jpeg):
    case XML_INTERN_CASE(mimetype_gif):
    case XML_INTERN_CASE(mimetype_png):
    case XML_INTERN_CASE(mimetype_tiff):
    case XML_INTERN_CASE(mimetype_ms_photo):
      /* ICC profile */
    case XML_INTERN_CASE(mimetype_iccprofile):
      return error_handler(SYNTAXERROR) ;
    }
  }

  /* Act as if consumed whole string */
  value->codeunits = UTF_BUFFER_LIMIT(*value) ;
  value->unitlength = 0 ;
  return TRUE ;
}


/*
    <!-- EvenArray: List with even number of entries of non-negative numbers.   -->
    <xs:simpleType name="ST_EvenArrayPos">
        <xs:restriction base="xs:string">
            <xs:whiteSpace value="collapse" />
<!--
            <xs:pattern value="[prn] [prn]( [prn] [prn])*"/>
-->
            <xs:pattern value="(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?) (\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)
                               ( (\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?) (\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))*" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_StrokeDashArray(xmlGFilter *filter,
                                 xmlGIStr *attrlocalname,
                                 utf8_buffer* value,
                                 void *data /* LINESTYLE* */)
{
  Bool success = FALSE ;
  LINESTYLE *linestyle = data ;
  uint16 dashlistlen = 0 ;
  utf8_buffer scan ;
  SYSTEMVALUE *dashlist ;
  void *map ;
  uint32 size, sema ;
  double d = 0.0;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(linestyle, "Nowhere to put converted dasharray") ;
  HQASSERT(theDashListLen(*linestyle) == 0, "linestyle dash list length has not been initialised") ;

  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  sema = get_basemap_semaphore(&map, &size) ;
  if ( sema == 0 )
    return error_handler( VMERROR ) ;

#define return DO_NOT_RETURN_use_goto_dasharray_cleanup_INSTEAD!!

  dashlist = map ;
  size = size / sizeof( SYSTEMVALUE ) ;
  /* dash list length can't exceed a uint16 */
  if ( size > 65535 )
    size = 65535 ;

  (void)xml_match_space(&scan) ; /* Possible leading spaces */

  while ( scan.unitlength > 0 ) {
    /* Dash array elements must be >= 0 in 0.8 */

    if (! xps_convert_dbl_prn(filter, attrlocalname, &scan, &d) )
      goto dasharray_cleanup ;

    if (dashlistlen == size) {
      HQFAIL("StrokeDashArray has exceeded basemap buffer or length exceeds uint16");
      ( void ) error_handler(LIMITCHECK);
      goto dasharray_cleanup ;
    }

    dashlist[dashlistlen++] = d;

    if ((dashlistlen & 0x1) == 1) {
      if (xml_match_space(&scan) == 0) {
        ( void ) error_handler(SYNTAXERROR);
        goto dasharray_cleanup ;
      }
    } else {
      (void)xml_match_space(&scan) ; /* Possible trailing spaces */
    }
  }

  /* StrokeDashArray is required to have an even number of entries. */
  if ( (dashlistlen & 1) == 1 || dashlistlen == 0 ) {
    ( void ) error_handler(RANGECHECK);
    goto dasharray_cleanup ;
  }

  if ( !gs_storedashlist(linestyle, dashlist, dashlistlen) )
    goto dasharray_cleanup ;

  *value = scan ;

  success = TRUE ;
 dasharray_cleanup :
  free_basemap_semaphore(sema) ;

#undef return
  return success ;
}

Bool xps_convert_prefix_no_colon(xmlGFilter *filter,
                                 xmlGIStr *attrlocalname,
                                 utf8_buffer* value,
                                 void *data /* utf8_buffer* */)
{
  utf8_buffer *utf8 = data ;
  utf8_buffer scan ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  *utf8 = scan ;
  while ( scan.unitlength > 0 ) {
    if (IS_XML_WHITESPACE(scan.codeunits[0]) || scan.codeunits[0] == ':')
      break ;
    ++scan.codeunits ;
    --scan.unitlength ;
  }

  utf8->unitlength = CAST_PTRDIFFT_TO_UINT32(scan.codeunits - utf8->codeunits) ;
  if (utf8->unitlength == 0)
    return error_handler(TYPECHECK) ;

  *value = scan ;

  return TRUE ;
}

Bool xps_convert_prefix(xmlGFilter *filter,
                        xmlGIStr *attrlocalname,
                        utf8_buffer* value,
                        void *data /* utf8_buffer* */)
{
  utf8_buffer *utf8 = data ;
  utf8_buffer scan ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  if (! xps_convert_prefix_no_colon(filter, attrlocalname, &scan, utf8))
    return FALSE ;

  if (scan.unitlength == 0)
    return error_handler(TYPECHECK) ;

  if ( !xml_match_unicode(&scan, ':') )
    return error_handler(SYNTAXERROR) ;

  *value = scan ;

  return TRUE ;
}

Bool xps_convert_local_name(xmlGFilter *filter,
                            xmlGIStr *attrlocalname,
                            utf8_buffer* value,
                            void *data /* utf8_buffer* */)
{
  utf8_buffer *utf8 = data ;
  utf8_buffer scan ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  *utf8 = scan ;
  while ( scan.unitlength > 0 ) {
    if (IS_XML_WHITESPACE(scan.codeunits[0]))
      break ;
    ++scan.codeunits ;
    --scan.unitlength ;
  }

  utf8->unitlength = CAST_PTRDIFFT_TO_UINT32(scan.codeunits - utf8->codeunits) ;
  if (utf8->unitlength == 0)
    return error_handler(TYPECHECK) ;

  *value = scan ;

  return TRUE ;
}

/* From s0schema.xsd 0.90
    <!-- Boolean with true and false only (no 0 or 1) -->
    <xs:simpleType name="ST_Boolean">
        <xs:restriction base="xs:boolean">
            <xs:pattern value="true|false" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_Boolean(xmlGFilter *filter,
                            xmlGIStr *attrlocalname,
                            utf8_buffer* value,
                            void *data /* Bool* */)
{
  utf8_buffer scan ;
  Bool *flag = data ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT((value != NULL),
           "xps_convert_boolean: NULL utf8 buffer pointer") ;
  HQASSERT(flag != NULL, "Nowhere to put converted boolean") ;

  scan = *value;
  if ( scan.unitlength == 0 )
    return error_handler(SYNTAXERROR);

  /* XPS XML boolean can take values true, false. Compare for an
     acceptable initial value. */
  if ( scan.unitlength >= 5 &&
       HqMemCmp(scan.codeunits, 5, UTF8_AND_LENGTH("false")) == 0 ) {
    *flag = FALSE ;
    scan.codeunits += 5 ;
    scan.unitlength -= 5 ;
  } else if ( scan.unitlength >= 4 &&
              HqMemCmp(scan.codeunits, 4, UTF8_AND_LENGTH("true")) == 0 ) {
    *flag = TRUE ;
    scan.codeunits += 4 ;
    scan.unitlength -= 4 ;
  } else {
    return error_handler(SYNTAXERROR) ;
  }

  *value = scan ;
  return TRUE ;
}

/* From s0schema.xsd 0.90

    <!-- UnicodeString grammar -->
    <xs:simpleType name="ST_UnicodeString">
        <xs:restriction base="xs:string">
            <xs:pattern value="(([^\{]|(\{\})).*)?" />
        </xs:restriction>
    </xs:simpleType>

  From 0.75 section 5.1.4:

  In order to use "{" at the beginning of the UnicodeString attribute,
  it MUST be escaped with a prefix of "{}". If the UnicodeString
  attribute starts with "{}", then a consumer MUST ignore those first
  two characters in processing the UnicodeString value and in
  calculating index positions for the characters of the
  UnicodeString. If the UnicodeString specifies an empty string ("" or
  "{}"), and the Indices attribute is missing or is also empty, no
  drawing occurs. */
Bool xps_convert_ST_UnicodeString(xmlGFilter *filter,
                                  xmlGIStr *attrlocalname,
                                  utf8_buffer* value,
                                  void *data /* utf8_buffer* */)
{
  utf8_buffer *utf8 = data ;
  utf8_buffer scan ;
  UTF32 c ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(utf8, "Nowhere to put converted UTF8 string") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  /* Not the empty string. */
  if (utf8_iterator_more(&scan)) {
    /* first char */
    c = utf8_iterator_get_next(&scan) ;

    if (c == (UTF32)'{') {
      if (! utf8_iterator_more(&scan))
        return error_handler(SYNTAXERROR) ;
      c = utf8_iterator_get_next(&scan) ;
      if (c != (UTF32)'}')
        return error_handler(SYNTAXERROR) ;
    } else {
      scan = *value ;
    }
  }

  *utf8 = scan ;

  /* Guaranteed to be at end of value. */
  value->codeunits  = UTF_BUFFER_LIMIT(*value) ;
  value->unitlength = 0 ;

  return TRUE ;
}

/* From s0schema.xsd 0.90

    <!-- A Name (ID with pattern restriction according to XPS spec) -->
    <xs:simpleType name="ST_Name">
        <xs:restriction base="xs:ID">
            <xs:pattern value="(\p{Lu}|\p{Ll}|\p{Lt}|\p{Lo}|\p{Nl}|_)(\p{Lu}|\p{Ll}|\p{Lt}|\p{Lo}|\p{Nl}|\p{Mn}|\p{Mc}|\p{Nd}|_)*" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_Name(xmlGFilter *filter,
                         xmlGIStr *attrlocalname,
                         utf8_buffer* value,
                         void *data /* utf8_buffer* */)
{
  utf8_buffer *utf8 = data ;
  utf8_buffer scan ;
  UTF32 c ;
  utf8_buffer id ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(utf8, "Nowhere to put converted UTF8 string") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  /* Scan that we have an Id. */
  if (! xml_convert_id(filter, attrlocalname, value, &id))
    return FALSE ;

  /* Now scan again to see that the additional MS restrictions still
     apply. */
  if (! utf8_iterator_more(&scan))
    return error_handler(SYNTAXERROR) ;

  /* first char */
  c = utf8_iterator_get_next(&scan) ;

  if (c != (UTF32)'_') {
    HqBool error_occured = FALSE ;
    if (! unicode_has_binary_property(c, UTF32_ID_START, &error_occured)) {
      return error_handler(error_occured ? VMERROR : SYNTAXERROR) ;
    }
  }

  /* scan the rest of the characters to see that they are correct */
  while (utf8_iterator_more(&scan)) {
    HqBool error_occured = FALSE ;
    c = utf8_iterator_get_next(&scan) ;
    if (c == (UTF32)'_')
      continue ;

    if (! unicode_has_binary_property(c, UTF32_ID_CONTINUE, &error_occured) ||
        unicode_char_type(c, &error_occured) == UTF32_CONNECTOR_PUNCTUATION) {
      return error_handler(error_occured ? VMERROR : SYNTAXERROR) ;
    }
  }

  *utf8 = *value ;

  /* Guaranteed to be at end of value. */
  value->codeunits  = UTF_BUFFER_LIMIT(*value) ;
  value->unitlength = 0 ;

  return TRUE ;
}

/* From http://dublincore.org/ Basically we ignore the property. */
Bool xps_convert_dublin_type(xmlGFilter *filter,
                             xmlGIStr *attrlocalname,
                             utf8_buffer* value,
                             void *data /* utf8_buffer* */)
{
  utf8_buffer *utf8 = data ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(utf8, "Nowhere to put converted UTF8 string") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");

  *utf8 = *value ;

  /* Guaranteed to be at end of value. */
  value->codeunits  = UTF_BUFFER_LIMIT(*value) ;
  value->unitlength = 0 ;

  return TRUE ;
}

/* From http://dublincore.org/ Basically we ignore the property. */
Bool xps_convert_dublin_value(xmlGFilter *filter,
                              xmlGIStr *attrlocalname,
                              utf8_buffer* value,
                              void *data /* utf8_buffer* */)
{
  utf8_buffer *utf8 = data ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(utf8, "Nowhere to put converted UTF8 string") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");

  *utf8 = *value ;

  /* Guaranteed to be at end of value. */
  value->codeunits  = UTF_BUFFER_LIMIT(*value) ;
  value->unitlength = 0 ;

  return TRUE ;
}

/* From Relationships.xsd 0.85

        <xsd:simpleType name="ST_TargetMode">
                <xsd:restriction base="xsd:string">
                        <xsd:enumeration value="External" />
                        <xsd:enumeration value="Internal" />
                </xsd:restriction>
        </xsd:simpleType>
*/
Bool xps_convert_ST_TargetMode(xmlGFilter *filter,
                               xmlGIStr *attrlocalname,
                               utf8_buffer* value,
                               void *data /* xmlGIStr** */)
{
  xmlGIStr* targetmode ;
  xmlGIStr **intern = data ;

  HQASSERT(intern, "Nowhere to put interned string") ;

  if (! xml_convert_string_enum(filter, attrlocalname, value, &targetmode))
    return FALSE ;

  /* Process relationship according to type */
  switch ( XML_INTERN_SWITCH (targetmode) ) {
  case XML_INTERN_CASE(Internal):
  case XML_INTERN_CASE(External):
    break ;
  default:
    return error_handler(RANGECHECK) ;
  }

  *intern = targetmode ;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- FillRule Mode enumeration -->
    <xs:simpleType name="ST_FillRule">
        <xs:restriction base="xs:string">
            <xs:enumeration value="EvenOdd" />
            <xs:enumeration value="NonZero" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_FillRule(xmlGFilter *filter,
                             xmlGIStr *attrlocalname,
                             utf8_buffer* value,
                             void *data /* xmlGIStr** */)
{
  xmlGIStr* fillrule ;
  xmlGIStr **intern = data ;

  HQASSERT(intern, "Nowhere to put interned string") ;

  if (! xml_convert_string_enum(filter, attrlocalname, value, &fillrule))
    return FALSE ;

  switch ( XML_INTERN_SWITCH(fillrule) ) {
  case XML_INTERN_CASE(NonZero):
    break ;
  case XML_INTERN_CASE(EvenOdd):
    break ;
  default:
    return error_handler(RANGECHECK) ;
  }

  *intern = fillrule ;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- Style Simulation Enumeration -->
    <xs:simpleType name="ST_StyleSimulations">
        <xs:restriction base="xs:string">
            <xs:enumeration value="None" />
            <xs:enumeration value="ItalicSimulation" />
            <xs:enumeration value="BoldSimulation" />
            <xs:enumeration value="BoldItalicSimulation" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_StyleSimulations(xmlGFilter *filter,
                                     xmlGIStr *attrlocalname,
                                     utf8_buffer* value,
                                     void *data /* int32* */)
{
  xmlGIStr* stylesimulations ;
  int32*    style = data ;

  HQASSERT(style, "Nowhere to put style") ;

  if (! xml_convert_string_enum(filter, attrlocalname, value, &stylesimulations))
    return FALSE ;

  switch ( XML_INTERN_SWITCH(stylesimulations) ) {
  case XML_INTERN_CASE(None):
    *style = STYLE_NONE;
    break ;
  case XML_INTERN_CASE(BoldSimulation):
    *style = STYLE_BOLD;
    break;
  case XML_INTERN_CASE(ItalicSimulation):
    *style = STYLE_ITALIC;
    break;
  case XML_INTERN_CASE(BoldItalicSimulation):
    *style = STYLE_BOLDITALIC;
    break ;
  default:
    return error_handler(RANGECHECK) ;
  }

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- Line Join enumeration -->
    <xs:simpleType name="ST_LineJoin">
        <xs:restriction base="xs:string">
            <xs:enumeration value="Miter" />
            <xs:enumeration value="Bevel" />
            <xs:enumeration value="Round" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_LineJoin(xmlGFilter *filter,
                             xmlGIStr *attrlocalname,
                             utf8_buffer* value,
                             void *data /* xmlGIStr** */)
{
  xmlGIStr* linejoin ;
  xmlGIStr **intern = data ;

  HQASSERT(intern, "Nowhere to put interned string") ;

  if (! xml_convert_string_enum(filter, attrlocalname, value, &linejoin))
    return FALSE ;

  switch ( XML_INTERN_SWITCH(linejoin) ) {
  case XML_INTERN_CASE(Miter):
  case XML_INTERN_CASE(Bevel):
  case XML_INTERN_CASE(Round):
    break ;
  default:
    return error_handler(RANGECHECK) ;
  }

  *intern = linejoin ;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- Dash Cap enumeration -->
    <xs:simpleType name="ST_DashCap">
        <xs:restriction base="xs:string">
            <xs:enumeration value="Flat" />
            <xs:enumeration value="Round" />
            <xs:enumeration value="Square" />
            <xs:enumeration value="Triangle" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_DashCap(xmlGFilter *filter,
                            xmlGIStr *attrlocalname,
                            utf8_buffer* value,
                            void *data /* xmlGIStr** */)
{
  xmlGIStr* dashcap ;
  xmlGIStr **intern = data ;

  HQASSERT(intern, "Nowhere to put interned string") ;

  if (! xml_convert_string_enum(filter, attrlocalname, value, &dashcap))
    return FALSE ;

  switch ( XML_INTERN_SWITCH(dashcap) ) {
  case XML_INTERN_CASE(Flat):
  case XML_INTERN_CASE(Round):
  case XML_INTERN_CASE(Square):
  case XML_INTERN_CASE(Triangle):
    break;
  default:
    return error_handler(RANGECHECK) ;
  }

  *intern = dashcap ;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- Line Cap enumeration -->
    <xs:simpleType name="ST_LineCap">
        <xs:restriction base="xs:string">
            <xs:enumeration value="Flat" />
            <xs:enumeration value="Round" />
            <xs:enumeration value="Square" />
            <xs:enumeration value="Triangle" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_LineCap(xmlGFilter *filter,
                            xmlGIStr *attrlocalname,
                            utf8_buffer* value,
                            void *data /* xmlGIStr** */)
{
  xmlGIStr **intern = data ;

  HQASSERT(intern, "Nowhere to put interned string") ;

  /* Just happens to be the same. */
  return xps_convert_ST_DashCap(filter, attrlocalname, value, intern) ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- Sweep Direction enumeration -->
    <xs:simpleType name="ST_SweepDirection">
        <xs:restriction base="xs:string">
            <xs:enumeration value="Clockwise" />
            <xs:enumeration value="Counterclockwise" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_SweepDirection(xmlGFilter *filter,
                                xmlGIStr *attrlocalname,
                                utf8_buffer* value,
                                void *data /* xmlGIStr** */)
{
  xmlGIStr* sweepdirection ;
  xmlGIStr **intern = data ;

  HQASSERT(intern, "Nowhere to put interned string") ;

  if (! xml_convert_string_enum(filter, attrlocalname, value, &sweepdirection))
    return FALSE ;

  switch ( XML_INTERN_SWITCH(sweepdirection) ) {
  case XML_INTERN_CASE(Clockwise):
  case XML_INTERN_CASE(Counterclockwise):
    break ;
  default:
    return error_handler(RANGECHECK) ;
  }

  *intern = sweepdirection ;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- MappingMode Enumeration -->
    <xs:simpleType name="ST_MappingMode">
        <xs:restriction base="xs:string">
            <xs:enumeration value="Absolute" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_MappingMode(xmlGFilter *filter,
                                xmlGIStr *attrlocalname,
                                utf8_buffer* value,
                                void *data /* xmlGIStr** */)
{
  xmlGIStr* mappingmode ;
  xmlGIStr **intern = data ;

  HQASSERT(intern, "Nowhere to put interned string") ;

  if (! xml_convert_string_enum(filter, attrlocalname, value, &mappingmode))
    return FALSE ;

  switch ( XML_INTERN_SWITCH(mappingmode) ) {
  case XML_INTERN_CASE(Absolute):
    break ;
  default:
    return error_handler(RANGECHECK) ;
  }

  *intern = mappingmode ;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- Color Interpolation Mode enumeration -->
    <xs:simpleType name="ST_ClrIntMode">
        <xs:restriction base="xs:string">
            <xs:enumeration value="ScRgbLinearInterpolation" />
            <xs:enumeration value="SRgbLinearInterpolation" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_ClrIntMode(xmlGFilter *filter,
                               xmlGIStr *attrlocalname,
                               utf8_buffer* value,
                               void *data /* xmlGIStr** */)
{
  xmlGIStr* interpolationmode ;
  xmlGIStr **intern = data ;

  HQASSERT(intern, "Nowhere to put interned string") ;

  if (! xml_convert_string_enum(filter, attrlocalname, value, &interpolationmode))
    return FALSE ;

  switch ( XML_INTERN_SWITCH(interpolationmode) ) {
  case XML_INTERN_CASE(ScRgbLinearInterpolation):
  case XML_INTERN_CASE(SRgbLinearInterpolation):
    break ;
  default:
    return error_handler(RANGECHECK) ;
  }

  *intern = interpolationmode ;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- SpreadMethod Mode enumeration -->
    <xs:simpleType name="ST_SpreadMethod">
        <xs:restriction base="xs:string">
            <xs:enumeration value="Pad" />
            <xs:enumeration value="Reflect" />
            <xs:enumeration value="Repeat" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_SpreadMethod(xmlGFilter *filter,
                                 xmlGIStr *attrlocalname,
                                 utf8_buffer* value,
                                 void *data /* xmlGIStr** */)
{
  xmlGIStr* spreadmethod ;
  xmlGIStr **intern = data ;

  HQASSERT(intern, "Nowhere to put interned string") ;

  if (! xml_convert_string_enum(filter, attrlocalname, value, &spreadmethod))
    return FALSE ;

  switch ( XML_INTERN_SWITCH(spreadmethod) ) {
  case XML_INTERN_CASE(Pad):
  case XML_INTERN_CASE(Reflect):
  case XML_INTERN_CASE(Repeat):
    break ;
  default:
    return error_handler(RANGECHECK) ;
  }

  *intern = spreadmethod ;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- ViewUnits Enumeration -->
    <xs:simpleType name="ST_ViewUnits">
        <xs:restriction base="xs:string">
            <xs:enumeration value="Absolute" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_ViewUnits(xmlGFilter *filter,
                              xmlGIStr *attrlocalname,
                              utf8_buffer* value,
                              void *data /* xmlGIStr** */)
{
  xmlGIStr* viewboxunits ;
  xmlGIStr **intern = data ;

  HQASSERT(intern, "Nowhere to put interned string") ;

  if (! xml_convert_string_enum(filter, attrlocalname, value, &viewboxunits))
    return FALSE ;

  switch ( XML_INTERN_SWITCH(viewboxunits) ) {
  case XML_INTERN_CASE(Absolute):
    break ;
  default:
    return error_handler(RANGECHECK) ;
  }

  *intern = viewboxunits ;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd
    <!-- Tile Mode enumeration -->
    <xs:simpleType name="ST_TileMode">
        <xs:restriction base="xs:string">
            <xs:enumeration value="None" />
            <xs:enumeration value="Tile" />
            <xs:enumeration value="FlipX" />
            <xs:enumeration value="FlipY" />
            <xs:enumeration value="FlipXY" />
        </xs:restriction>
    </xs:simpleType>
*/
Bool xps_convert_ST_TileMode(xmlGFilter *filter,
                             xmlGIStr *attrlocalname,
                             utf8_buffer* value,
                             void *data /* xmlGIStr** */)
{
  xmlGIStr* tilemode ;
  xmlGIStr **intern = data ;

  HQASSERT(intern, "Nowhere to put interned string") ;

  if (! xml_convert_string_enum(filter, attrlocalname, value, &tilemode))
    return FALSE ;

  switch ( XML_INTERN_SWITCH(tilemode) ) {
  case XML_INTERN_CASE(FlipXY):
  case XML_INTERN_CASE(FlipY):
  case XML_INTERN_CASE(FlipX):
  case XML_INTERN_CASE(Tile):
  case XML_INTERN_CASE(None):
    break ;
  default:
    return error_handler(RANGECHECK) ;
  }

  *intern = tilemode ;

  return TRUE ;
}

Bool xps_convert_ST_EdgeMode(xmlGFilter *filter,
                             xmlGIStr *attrlocalname,
                             utf8_buffer* value,
                             void *data /* xmlGIStr** */)
{
  xmlGIStr* stretch ;
  xmlGIStr **intern = data ;

  HQASSERT(intern, "Nowhere to put interned string") ;

  if (! xml_convert_string_enum(filter, attrlocalname, value, &stretch))
    return FALSE ;

  switch ( XML_INTERN_SWITCH(stretch) ) {
  case XML_INTERN_CASE(Aliased):
    break ;
  default:
    return error_handler(RANGECHECK) ;
  }

  *intern = stretch ;

  return TRUE ;
}

/* From XPS 0.75 s0schema.xsd
  <xs:restriction base="xs:string">
    <xs:whiteSpace value="collapse"/>
<!--
    <xs:pattern value="[prn][scs][prn][scs][prn][scs][prn]"/>
-->
    <xs:pattern value="(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?\, ?)(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)
                       ( ?\, ?)(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?\, ?)(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)"/>
  </xs:restriction> */
Bool xps_convert_contentbox(xmlGFilter *filter,
                            xmlGIStr *attrlocalname,
                            utf8_buffer* value,
                            void *data /* sbbox_t* */)
{
  sbbox_t *bbox = data ;
  SYSTEMVALUE *bbindexed ;
  utf8_buffer scan ;
  uint32 i ;

  HQASSERT(bbox, "Nowhere to put converted bbox") ;
  bbox_as_indexed(bbindexed, bbox) ;

  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  (void)xml_match_space(&scan) ;

  for (i = 0; i < 4; ++i) {
    if (i > 0 && ! xps_match_scs_collapse(&scan))
      return error_handler(SYNTAXERROR) ;

    if ( !xps_convert_dbl_rn(filter, attrlocalname, &scan, &bbindexed[i]) )
      return FALSE ;
  }
  (void)xml_match_space(&scan) ; /* Possible trailing spaces */
  *value = scan ;

  return TRUE;
}

/* From XPS 0.75 s0schema.xsd

  <xs:restriction base="xs:string">
    <xs:whiteSpace value="collapse"/>
<!--
    <xs:pattern value="[rn][scs][rn][scs][prn][scs][prn]"/>
-->
    <xs:pattern value="((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?\, ?)((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)
                       ( ?\, ?)(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)( ?\, ?)(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)"/>
    </xs:restriction> */
Bool xps_convert_bleedbox(xmlGFilter *filter,
                          xmlGIStr *attrlocalname,
                          utf8_buffer* value,
                          void *data /* sbbox_t* */)
{
  sbbox_t *bbox = data ;
  SYSTEMVALUE *bbindexed ;
  utf8_buffer scan ;
  uint32 i ;

  HQASSERT(bbox, "Nowhere to put converted bbox") ;
  bbox_as_indexed(bbindexed, bbox) ;

  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  (void)xml_match_space(&scan) ;

  for (i = 0; i < 2; ++i) {
    if (i > 0 && ! xps_match_scs_collapse(&scan))
      return error_handler(SYNTAXERROR) ;

    if ( !xps_convert_dbl_rn(filter, attrlocalname, &scan, &bbindexed[i]) )
      return FALSE ;
  }

  for (i = 2; i < 4; ++i) {
    if (i > 0 && ! xps_match_scs_collapse(&scan))
      return error_handler(SYNTAXERROR) ;

    if ( !xps_convert_dbl_prn(filter, attrlocalname, &scan, &bbindexed[i]) )
      return FALSE ;
  }

  (void)xml_match_space(&scan) ; /* Possible trailing spaces */
  *value = scan ;

  return TRUE;
}

/* We currently ignore this attribute == no validation */
Bool xps_convert_navigate_uri(xmlGFilter *filter,
                              xmlGIStr *attrlocalname,
                              utf8_buffer* value,
                              void *data /* NULL */)
{
  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;
  UNUSED_PARAM(void *, data) ;

  HQASSERT(data == NULL, "data is not NULL") ;

  value->codeunits = UTF_BUFFER_LIMIT(*value) ;
  value->unitlength = 0 ;

  return TRUE ;
}

/* From XPS 0.90 s0schema.xsd

    <!-- Indices grammar for Glyphs.CaretStops -->
    <xs:simpleType name="ST_CaretStops">
        <xs:restriction base="xs:string">
            <xs:whiteSpace value="collapse" />
            <xs:pattern value="[0-9A-Fa-f]*" />
        </xs:restriction>
    </xs:simpleType>

   We currently do not store the value anywhere, just scan it.
*/
Bool xps_convert_ST_CaretStops(xmlGFilter *filter,
                            xmlGIStr *attrlocalname,
                            utf8_buffer* value,
                            void *data /* NULL */)
{
  utf8_buffer scan ;
  UTF32 c ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;
  UNUSED_PARAM(void *, data) ;
  HQASSERT(data == NULL, "data is not NULL") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  (void)xml_match_space(&scan) ; /* Possible leading spaces */

  while (utf8_iterator_more(&scan)) {
    c = utf8_iterator_get_next(&scan) ;
    if (c >= (UTF32)'0' && c <= (UTF32)'9')
      continue ;
    else if (c >= (UTF32)'A' && c <= (UTF32)'F')
      continue ;
    else if (c >= (UTF32)'a' && c <= (UTF32)'f')
      continue ;

    return error_handler(SYNTAXERROR) ;
  }

  (void)xml_match_space(&scan) ; /* Possible trailing spaces */

  /* Guaranteed to be at end of value. */
  value->codeunits  = UTF_BUFFER_LIMIT(*value) ;
  value->unitlength = 0 ;

  return TRUE ;

}

/* ============================================================================
 * GGS XPS extensions. Scanner function names ought to be prefixed
 * with ggs_xps_convert_<suffux>.
 * ============================================================================
 */
Bool ggs_xps_convert_userlabel(xmlGFilter *filter,
                               xmlGIStr *attrlocalname,
                               utf8_buffer* value,
                               void *data /* Bool* */)
{
  return xps_convert_ST_Boolean(filter, attrlocalname, value, data) ;
}

/* ============================================================================
* Log stripped */
