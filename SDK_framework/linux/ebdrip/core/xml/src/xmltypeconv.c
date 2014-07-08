/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!src:xmltypeconv.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of XML and XML schema common type converters and matching.
 */

#include "core.h"
#include "swctype.h"
#include "swerrors.h"
#include "hqmemcmp.h"
#include "xml.h"

#include "xmltypeconv.h"

/* Definitions as per:
   http://www.w3.org/TR/REC-xml/ */
#define unicode_is_BaseChar(c) ( \
(0x0041<= (c) && (c) <= 0x005A) || (0x0061<= (c) && (c) <= 0x007A) || \
(0x00C0<= (c) && (c) <= 0x00D6) || (0x00D8<= (c) && (c) <= 0x00F6) || \
(0x00F8<= (c) && (c) <= 0x00FF) || (0x0100<= (c) && (c) <= 0x0131) || \
(0x0134<= (c) && (c) <= 0x013E) || (0x0141<= (c) && (c) <= 0x0148) || \
(0x014A<= (c) && (c) <= 0x017E) || (0x0180<= (c) && (c) <= 0x01C3) || \
(0x01CD<= (c) && (c) <= 0x01F0) || (0x01F4<= (c) && (c) <= 0x01F5) || \
(0x01FA<= (c) && (c) <= 0x0217) || (0x0250<= (c) && (c) <= 0x02A8) || \
(0x02BB<= (c) && (c) <= 0x02C1) || (c) == 0x0386 || \
(0x0388<= (c) && (c) <= 0x038A) || (c) == 0x038C || \
(0x038E<= (c) && (c) <= 0x03A1) || (0x03A3<= (c) && (c) <= 0x03CE) || \
(0x03D0<= (c) && (c) <= 0x03D6) || (c) == 0x03DA || (c) == 0x03DC || \
(c) == 0x03DE || (c) == 0x03E0 || (0x03E2<= (c) && (c) <= 0x03F3) || \
(0x0401<= (c) && (c) <= 0x040C) || (0x040E<= (c) && (c) <= 0x044F) || \
(0x0451<= (c) && (c) <= 0x045C) || (0x045E<= (c) && (c) <= 0x0481) || \
(0x0490<= (c) && (c) <= 0x04C4) || (0x04C7<= (c) && (c) <= 0x04C8) || \
(0x04CB<= (c) && (c) <= 0x04CC) || (0x04D0<= (c) && (c) <= 0x04EB) || \
(0x04EE<= (c) && (c) <= 0x04F5) || (0x04F8<= (c) && (c) <= 0x04F9) || \
(0x0531<= (c) && (c) <= 0x0556) || (c) == 0x0559 || \
(0x0561<= (c) && (c) <= 0x0586) || (0x05D0<= (c) && (c) <= 0x05EA) || \
(0x05F0<= (c) && (c) <= 0x05F2) || (0x0621<= (c) && (c) <= 0x063A) || \
(0x0641<= (c) && (c) <= 0x064A) || (0x0671<= (c) && (c) <= 0x06B7) || \
(0x06BA<= (c) && (c) <= 0x06BE) || (0x06C0<= (c) && (c) <= 0x06CE) || \
(0x06D0<= (c) && (c) <= 0x06D3) || (c) == 0x06D5 || \
(0x06E5<= (c) && (c) <= 0x06E6) || (0x0905<= (c) && (c) <= 0x0939) || \
(c) == 0x093D || (0x0958<= (c) && (c) <= 0x0961) || \
(0x0985<= (c) && (c) <= 0x098C) || (0x098F<= (c) && (c) <= 0x0990) || \
(0x0993<= (c) && (c) <= 0x09A8) || (0x09AA<= (c) && (c) <= 0x09B0) || \
(c) == 0x09B2 || (0x09B6<= (c) && (c) <= 0x09B9) || \
(0x09DC<= (c) && (c) <= 0x09DD) || (0x09DF<= (c) && (c) <= 0x09E1) || \
(0x09F0<= (c) && (c) <= 0x09F1) || (0x0A05<= (c) && (c) <= 0x0A0A) || \
(0x0A0F<= (c) && (c) <= 0x0A10) || (0x0A13<= (c) && (c) <= 0x0A28) || \
(0x0A2A<= (c) && (c) <= 0x0A30) || (0x0A32<= (c) && (c) <= 0x0A33) || \
(0x0A35<= (c) && (c) <= 0x0A36) || (0x0A38<= (c) && (c) <= 0x0A39) || \
(0x0A59<= (c) && (c) <= 0x0A5C) || (c) == 0x0A5E || \
(0x0A72<= (c) && (c) <= 0x0A74) || (0x0A85<= (c) && (c) <= 0x0A8B) || \
(c) == 0x0A8D || (0x0A8F<= (c) && (c) <= 0x0A91) || \
(0x0A93<= (c) && (c) <= 0x0AA8) || (0x0AAA<= (c) && (c) <= 0x0AB0) || \
(0x0AB2<= (c) && (c) <= 0x0AB3) || (0x0AB5<= (c) && (c) <= 0x0AB9) || \
(c) == 0x0ABD || (c) == 0x0AE0 || (0x0B05<= (c) && (c) <= 0x0B0C) || \
(0x0B0F<= (c) && (c) <= 0x0B10) || (0x0B13<= (c) && (c) <= 0x0B28) || \
(0x0B2A<= (c) && (c) <= 0x0B30) || (0x0B32<= (c) && (c) <= 0x0B33) || \
(0x0B36<= (c) && (c) <= 0x0B39) || (c) == 0x0B3D || \
(0x0B5C<= (c) && (c) <= 0x0B5D) || (0x0B5F<= (c) && (c) <= 0x0B61) || \
(0x0B85<= (c) && (c) <= 0x0B8A) || (0x0B8E<= (c) && (c) <= 0x0B90) || \
(0x0B92<= (c) && (c) <= 0x0B95) || (0x0B99<= (c) && (c) <= 0x0B9A) || \
(c) == 0x0B9C || (0x0B9E<= (c) && (c) <= 0x0B9F) || \
(0x0BA3<= (c) && (c) <= 0x0BA4) || (0x0BA8<= (c) && (c) <= 0x0BAA) || \
(0x0BAE<= (c) && (c) <= 0x0BB5) || (0x0BB7<= (c) && (c) <= 0x0BB9) || \
(0x0C05<= (c) && (c) <= 0x0C0C) || (0x0C0E<= (c) && (c) <= 0x0C10) || \
(0x0C12<= (c) && (c) <= 0x0C28) || (0x0C2A<= (c) && (c) <= 0x0C33) || \
(0x0C35<= (c) && (c) <= 0x0C39) || (0x0C60<= (c) && (c) <= 0x0C61) || \
(0x0C85<= (c) && (c) <= 0x0C8C) || (0x0C8E<= (c) && (c) <= 0x0C90) || \
(0x0C92<= (c) && (c) <= 0x0CA8) || (0x0CAA<= (c) && (c) <= 0x0CB3) || \
(0x0CB5<= (c) && (c) <= 0x0CB9) || (c) == 0x0CDE || \
(0x0CE0<= (c) && (c) <= 0x0CE1) || (0x0D05<= (c) && (c) <= 0x0D0C) || \
(0x0D0E<= (c) && (c) <= 0x0D10) || (0x0D12<= (c) && (c) <= 0x0D28) || \
(0x0D2A<= (c) && (c) <= 0x0D39) || (0x0D60<= (c) && (c) <= 0x0D61) || \
(0x0E01<= (c) && (c) <= 0x0E2E) || (c) == 0x0E30 || \
(0x0E32<= (c) && (c) <= 0x0E33) || (0x0E40<= (c) && (c) <= 0x0E45) || \
(0x0E81<= (c) && (c) <= 0x0E82) || (c) == 0x0E84 || \
(0x0E87<= (c) && (c) <= 0x0E88) || (c) == 0x0E8A || (c) == 0x0E8D || \
(0x0E94<= (c) && (c) <= 0x0E97) || (0x0E99<= (c) && (c) <= 0x0E9F) || \
(0x0EA1<= (c) && (c) <= 0x0EA3) || (c) == 0x0EA5 || (c) == 0x0EA7 || \
(0x0EAA<= (c) && (c) <= 0x0EAB) || (0x0EAD<= (c) && (c) <= 0x0EAE) || \
(c) == 0x0EB0 || (0x0EB2<= (c) && (c) <= 0x0EB3) || (c) == 0x0EBD || \
(0x0EC0<= (c) && (c) <= 0x0EC4) || (0x0F40<= (c) && (c) <= 0x0F47) || \
(0x0F49<= (c) && (c) <= 0x0F69) || (0x10A0<= (c) && (c) <= 0x10C5) || \
(0x10D0<= (c) && (c) <= 0x10F6) || (c) == 0x1100 || \
(0x1102<= (c) && (c) <= 0x1103) || (0x1105<= (c) && (c) <= 0x1107) || \
(c) == 0x1109 || (0x110B<= (c) && (c) <= 0x110C) || \
(0x110E<= (c) && (c) <= 0x1112) || (c) == 0x113C || (c) == 0x113E || \
(c) == 0x1140 || (c) == 0x114C || (c) == 0x114E || (c) == 0x1150 || \
(0x1154<= (c) && (c) <= 0x1155) || (c) == 0x1159 || \
(0x115F<= (c) && (c) <= 0x1161) || (c) == 0x1163 || (c) == 0x1165 || \
(c) == 0x1167 || (c) == 0x1169 || (0x116D<= (c) && (c) <= 0x116E) || \
(0x1172<= (c) && (c) <= 0x1173) || (c) == 0x1175 || (c) == 0x119E || \
(c) == 0x11A8 || (c) == 0x11AB || (0x11AE<= (c) && (c) <= 0x11AF) || \
(0x11B7<= (c) && (c) <= 0x11B8) || (c) == 0x11BA || \
(0x11BC<= (c) && (c) <= 0x11C2) || (c) == 0x11EB || (c) == 0x11F0 || \
(c) == 0x11F9 || (0x1E00<= (c) && (c) <= 0x1E9B) || \
(0x1EA0<= (c) && (c) <= 0x1EF9) || (0x1F00<= (c) && (c) <= 0x1F15) || \
(0x1F18<= (c) && (c) <= 0x1F1D) || (0x1F20<= (c) && (c) <= 0x1F45) || \
(0x1F48<= (c) && (c) <= 0x1F4D) || (0x1F50<= (c) && (c) <= 0x1F57) || \
(c) == 0x1F59 || (c) == 0x1F5B || (c) == 0x1F5D || \
(0x1F5F<= (c) && (c) <= 0x1F7D) || (0x1F80<= (c) && (c) <= 0x1FB4) || \
(0x1FB6<= (c) && (c) <= 0x1FBC) || (c) == 0x1FBE || \
(0x1FC2<= (c) && (c) <= 0x1FC4) || (0x1FC6<= (c) && (c) <= 0x1FCC) || \
(0x1FD0<= (c) && (c) <= 0x1FD3) || (0x1FD6<= (c) && (c) <= 0x1FDB) || \
(0x1FE0<= (c) && (c) <= 0x1FEC) || (0x1FF2<= (c) && (c) <= 0x1FF4) || \
(0x1FF6<= (c) && (c) <= 0x1FFC) || (c) == 0x2126 || \
(0x212A<= (c) && (c) <= 0x212B) || (c) == 0x212E || \
(0x2180<= (c) && (c) <= 0x2182) || (0x3041<= (c) && (c) <= 0x3094) || \
(0x30A1<= (c) && (c) <= 0x30FA) || (0x3105<= (c) && (c) <= 0x312C) || \
(0xAC00<= (c) && (c) <= 0xD7A3) )

#define unicode_is_Ideographic(c) ( \
(0x4E00<= (c) && (c) <= 0x9FA5) || (c) == 0x3007 || \
(0x3021<= (c) && (c) <= 0x3029) )

#define unicode_is_Letter(c) (unicode_is_BaseChar(c) || unicode_is_Ideographic(c))

#define unicode_is_Digit(c) ( \
(0x0030<= (c) && (c) <= 0x0039) || (0x0660<= (c) && (c) <= 0x0669) || \
(0x06F0<= (c) && (c) <= 0x06F9) || (0x0966<= (c) && (c) <= 0x096F) || \
(0x09E6<= (c) && (c) <= 0x09EF) || (0x0A66<= (c) && (c) <= 0x0A6F) || \
(0x0AE6<= (c) && (c) <= 0x0AEF) || (0x0B66<= (c) && (c) <= 0x0B6F) || \
(0x0BE7<= (c) && (c) <= 0x0BEF) || (0x0C66<= (c) && (c) <= 0x0C6F) || \
(0x0CE6<= (c) && (c) <= 0x0CEF) || (0x0D66<= (c) && (c) <= 0x0D6F) || \
(0x0E50<= (c) && (c) <= 0x0E59) || (0x0ED0<= (c) && (c) <= 0x0ED9) || \
(0x0F20<= (c) && (c) <= 0x0F29) )

#define unicode_is_Extender(c) ( \
(c) == 0x00B7 || (c) == 0x02D0 || (c) == 0x02D1 || (c) == 0x0387 || \
(c) == 0x0640 || (c) == 0x0E46 || (c) == 0x0EC6 || (c) == 0x3005 || \
(0x3031<= (c) && (c) <= 0x3035) || (0x309D<= (c) && (c) <= 0x309E) || \
(0x30FC<= (c) && (c) <= 0x30FE) )

#define unicode_is_CombiningChar(c) ( \
(0x0300<= (c) && (c) <= 0x0345) || (0x0360<= (c) && (c) <= 0x0361) || \
(0x0483<= (c) && (c) <= 0x0486) || (0x0591<= (c) && (c) <= 0x05A1) || \
(0x05A3<= (c) && (c) <= 0x05B9) || (0x05BB<= (c) && (c) <= 0x05BD) || \
(c) == 0x05BF || (0x05C1<= (c) && (c) <= 0x05C2) || \
(c) == 0x05C4 || (0x064B<= (c) && (c) <= 0x0652) || (c) == 0x0670 || \
(0x06D6<= (c) && (c) <= 0x06DC) || (0x06DD<= (c) && (c) <= 0x06DF) || \
(0x06E0<= (c) && (c) <= 0x06E4) || (0x06E7<= (c) && (c) <= 0x06E8) || \
(0x06EA<= (c) && (c) <= 0x06ED) || (0x0901<= (c) && (c) <= 0x0903) || \
(c) == 0x093C || (0x093E<= (c) && (c) <= 0x094C) || (c) == 0x094D || \
(0x0951<= (c) && (c) <= 0x0954) || (0x0962<= (c) && (c) <= 0x0963) || \
(0x0981<= (c) && (c) <= 0x0983) || (c) == 0x09BC || (c) == 0x09BE || \
(c) == 0x09BF || (0x09C0<= (c) && (c) <= 0x09C4) || (0x09C7<= (c) && (c) <= 0x09C8) || \
(0x09CB<= (c) && (c) <= 0x09CD) || (c) == 0x09D7 || (0x09E2<= (c) && (c) <= 0x09E3) || \
(c) == 0x0A02 || (c) == 0x0A3C || (c) == 0x0A3E || (c) == 0x0A3F || \
(0x0A40<= (c) && (c) <= 0x0A42) || (0x0A47<= (c) && (c) <= 0x0A48) || \
(0x0A4B<= (c) && (c) <= 0x0A4D) || (0x0A70<= (c) && (c) <= 0x0A71) || \
(0x0A81<= (c) && (c) <= 0x0A83) || (c) == 0x0ABC || (0x0ABE<= (c) && (c) <= 0x0AC5) || \
(0x0AC7<= (c) && (c) <= 0x0AC9) || (0x0ACB<= (c) && (c) <= 0x0ACD) || \
(0x0B01<= (c) && (c) <= 0x0B03) || (c) == 0x0B3C || (0x0B3E<= (c) && (c) <= 0x0B43) || \
(0x0B47<= (c) && (c) <= 0x0B48) || (0x0B4B<= (c) && (c) <= 0x0B4D) || \
(0x0B56<= (c) && (c) <= 0x0B57) || (0x0B82<= (c) && (c) <= 0x0B83) || \
(0x0BBE<= (c) && (c) <= 0x0BC2) || (0x0BC6<= (c) && (c) <= 0x0BC8) || \
(0x0BCA<= (c) && (c) <= 0x0BCD) || (c) == 0x0BD7 || \
(0x0C01<= (c) && (c) <= 0x0C03) || (0x0C3E<= (c) && (c) <= 0x0C44) || \
(0x0C46<= (c) && (c) <= 0x0C48) || (0x0C4A<= (c) && (c) <= 0x0C4D) || \
(0x0C55<= (c) && (c) <= 0x0C56) || (0x0C82<= (c) && (c) <= 0x0C83) || \
(0x0CBE<= (c) && (c) <= 0x0CC4) || (0x0CC6<= (c) && (c) <= 0x0CC8) || \
(0x0CCA<= (c) && (c) <= 0x0CCD) || (0x0CD5<= (c) && (c) <= 0x0CD6) || \
(0x0D02<= (c) && (c) <= 0x0D03) || (0x0D3E<= (c) && (c) <= 0x0D43) || \
(0x0D46<= (c) && (c) <= 0x0D48) || (0x0D4A<= (c) && (c) <= 0x0D4D) || \
(c) == 0x0D57 || (c) == 0x0E31 || (0x0E34<= (c) && (c) <= 0x0E3A) || \
(0x0E47<= (c) && (c) <= 0x0E4E) || (c) == 0x0EB1 || (0x0EB4<= (c) && (c) <= 0x0EB9) || \
(0x0EBB<= (c) && (c) <= 0x0EBC) || (0x0EC8<= (c) && (c) <= 0x0ECD) || \
(0x0F18<= (c) && (c) <= 0x0F19) || (c) == 0x0F35 || (c) == 0x0F37 || \
(c) == 0x0F39 || (c) == 0x0F3E || (c) == 0x0F3F || \
(0x0F71<= (c) && (c) <= 0x0F84) || (0x0F86<= (c) && (c) <= 0x0F8B) || \
(0x0F90<= (c) && (c) <= 0x0F95) || (c) == 0x0F97 || \
(0x0F99<= (c) && (c) <= 0x0FAD) || (0x0FB1<= (c) && (c) <= 0x0FB7) || \
(c) == 0x0FB9 || (0x20D0<= (c) && (c) <= 0x20DC) || \
(c) == 0x20E1 || (0x302A<= (c) && (c) <= 0x302F) || \
(c) == 0x3099 || (c) == 0x309A )

#define XML_UNDERSCORE 0x5f
#define XML_DOT 0x2e
#define XML_DASH 0x2d

/* ============================================================================
 * Match functions. Match functions should NOT raise a PS error, but
 * rather TRUE or FALSE. Match functions should never use convert
 * functions.
 * ============================================================================
 */

/* XML space matching */
uint32 xml_match_space(utf8_buffer* input)
{
  /* UNTIL WE GET FEEDBACK FROM MS, WE ARE REVERTING SPACE MATCHING TO
     MATCH XML WHITESPACE. THIS ALLOWS MANY QL JOBS TO PASS RATHER
     THAN FAIL */
  return xml_match_whitespace(input) ;

#if 0
  uint32 count = 0 ;

  HQASSERT((input != NULL),
           "xml_match_space: NULL utf8 buffer pointer");

  /* This matches the XML 1.0 definition of space. */
  while ( input->unitlength > 0 ) {
    if (input->codeunits[0] == ' ') {
      ++input->codeunits ;
      --input->unitlength ;
      count++ ;
    } else {
      break ;
    }
  }

  return count ;
#endif
}

/* XML whitespace matching */
uint32 xml_match_whitespace(utf8_buffer* input)
{
  uint32 count = 0 ;

  HQASSERT((input != NULL),
           "xml_match_whitespace: NULL utf8 buffer pointer");

  /* This matches the XML 1.0 definition of whitespace. XML 1.1 uses the same
     definition, its treatment of line endings is a little different, though.
     Since the string is UTF-8 encoded, we can compare single code units
     against ASCII. */
  while ( input->unitlength > 0 ) {
    switch ( input->codeunits[0] ) {
    case ' ': case '\r': case '\n': case '\t':
      ++input->codeunits ;
      --input->unitlength ;
      count++ ;
      continue ;
    }
    break ;
  }

  return count ;
}

/* XML string matching */
Bool xml_match_string(utf8_buffer* string,
                      const utf8_buffer* match)
{
  HQASSERT((string != NULL),
           "xml_match_string: NULL utf8 search string pointer");
  HQASSERT((match != NULL),
           "xml_match_string: NULL utf8 match string pointer");

  if ( string->unitlength < match->unitlength ||
       HqMemCmp(string->codeunits, match->unitlength,
                match->codeunits, match->unitlength) != 0 )
    return FALSE ;

  string->codeunits  += match->unitlength;
  string->unitlength -= match->unitlength;
  return TRUE ;
}

/* XML Unicode value matching */
Bool xml_match_unicode(utf8_buffer* string, UTF32 unicode)
{
  utf8_buffer iterator ;

  HQASSERT((string != NULL),
           "xml_match_unicode: NULL utf8 string pointer");
  HQASSERT(UNICODE_CODEPOINT_VALID(unicode), "Unicode codepoint is not valid") ;

  iterator = *string;

  if ( !utf8_iterator_more(&iterator) ||
       utf8_iterator_get_next(&iterator) != unicode )
    return FALSE ;

  *string = iterator;
  return TRUE ;
}

/* ============================================================================
 * Convert functions. Convert functions should NOT raise a PS error,
 * but rather TRUE or FALSE.
 * ============================================================================
 */

/*
 * This function implements the ID datatype from XML schema recommendation
 * http://www.w3.org/TR/2001/REC-xmlschema-2-20010502/.
 *
 * The Id value follows the NCName production in [Namespaces in XML].
 * http://www.w3.org/TR/1999/REC-xml-names-19990114/#NT-NCName
 *
 * which is equal to:
 *
 * NCName ::=  (Letter | '_') (NCNameChar)*
 * NCNameChar ::=  Letter | Digit | '.' | '-' | '_' | CombiningChar | Extender 
 */
Bool xml_convert_id(xmlGFilter *filter,
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

  if (! utf8_iterator_more(&scan))
    return error_handler(SYNTAXERROR) ;

  /*  first char */
  c = utf8_iterator_get_next(&scan) ;

  if (! unicode_is_Letter(c) && c != XML_UNDERSCORE)
    return error_handler(RANGECHECK) ;

  /* scan the rest of the characters to see that they are correct */
  while (utf8_iterator_more(&scan)) {
    c = utf8_iterator_get_next(&scan) ;
    if (! unicode_is_Letter(c) && ! unicode_is_Digit(c) &&
        c != XML_DOT && c != XML_DASH && c != XML_UNDERSCORE &&
        ! unicode_is_CombiningChar(c) && ! unicode_is_Extender(c))
      return error_handler(RANGECHECK) ;
  }

  *utf8 = *value ;

  /* Guaranteed to be at end of value, since we stripped trailing whitespace. */
  value->codeunits  = UTF_BUFFER_LIMIT(*value) ;
  value->unitlength = 0 ;

  return TRUE ;
}

/*
 * This function implements the boolean datatype from XML schema recommendation
 * http://www.w3.org/TR/2001/REC-xmlschema-2-20010502/.
 */
Bool xml_convert_boolean(xmlGFilter *filter,
                         xmlGIStr *attrlocalname,
                         utf8_buffer* value,
                         void *data /* Bool* */)
{
  utf8_buffer scan ;
  Bool *flag = data ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT((value != NULL),
           "xml_convert_boolean: NULL utf8 buffer pointer") ;
  HQASSERT(flag != NULL, "Nowhere to put converted boolean") ;

  scan = *value;
  if ( scan.unitlength == 0 )
    return error_handler(SYNTAXERROR);

  /* XML boolean can take values true, false, 0, 1. Compare for an acceptable
     initial value. */
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
  } else if ( scan.unitlength >= 1 ) {
    if ( scan.codeunits[0] == '0' )
      *flag = FALSE ;
    else if ( scan.codeunits[0] == '1' )
      *flag = TRUE ;
    else
      return error_handler(SYNTAXERROR) ;

    scan.codeunits += 1 ;
    scan.unitlength -= 1 ;
  } else
    return error_handler(SYNTAXERROR) ;

  *value = scan ;
  return TRUE ;
}

/* This function implements the xml:lang datatype from
 * http://www.ietf.org/rfc/rfc3066.txt. It only tests the syntax as
 * per that specification, not if the language is a currently
 * registered language code.
 *
 * From that spec:
 *
 * 2.1 Language tag syntax
 *
 * The language tag is composed of one or more parts: A primary
 * language subtag and a (possibly empty) series of subsequent
 * subtags. The syntax of this tag in ABNF [RFC 2234] is:
 *
 *  Language-Tag = Primary-subtag *( "-" Subtag )
 *
 *  Primary-subtag = 1*8ALPHA
 *
 *  Subtag = 1*8(ALPHA / DIGIT)
 *
 * The productions ALPHA and DIGIT are imported from RFC 2234; they
 * denote respectively the characters A to Z in upper or lower case
 * and the digits from 0 to 9.  The character "-" is HYPHEN-MINUS
 * (ABNF: %x2D).
 *
 * All tags are to be treated as case insensitive; there exist
 * conventions for capitalization of some of them, but these should
 * not be taken to carry meaning.  For instance, [ISO 3166] recommends
 * that country codes are capitalized (MN Mongolia), while [ISO 639]
 * recommends that language codes are written in lower case (mn
 * Mongolian).
 */

/* As defined in RFC 2234 */
#define LANG_IS_ALPHA(c) ((c >= 0x41 && c <= 0x5A) || \
                          (c >= 0x61 && c <= 0x7A)) /* A-Z / a-z */
#define LANG_IS_DIGIT(c) (c >= 0x30 && c <= 0x39) /* 0-9 */

Bool xml_convert_lang(xmlGFilter *filter,
                      xmlGIStr *attrlocalname,
                      utf8_buffer* value,
                      void *data /* xmlGIStr** */)
{
  xmlGIStr **intern = data ;
  utf8_buffer scan ;
  UTF32 c ;
  uint32 i ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(intern, "Nowhere to put interned string") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");
  scan = *value ;

  /* Must be at least one char. */
  if (! utf8_iterator_more(&scan))
    return error_handler(SYNTAXERROR) ;

  /* Check first char. */
  c = utf8_iterator_get_next(&scan) ;
  if (! LANG_IS_ALPHA(c))
    return error_handler(RANGECHECK) ;

  /* We can have only 7 more alpha's */
  for (i=0; i<7; i++) {
    if (! utf8_iterator_more(&scan))
      break ;
    c = utf8_iterator_get_next(&scan) ;

    if (LANG_IS_ALPHA(c))
      continue ;

    if (c == '-') {
      break ;
    } else {
      return error_handler(SYNTAXERROR) ;
    }
  }

  /* We have consumed all 8 ALPHA's, have no more data or have found a
     -. */
  while (utf8_iterator_more(&scan)) {
    /* If we don't have a - and there are more chars left, it MUST
       be a - */
    if (c != '-') {
      if (utf8_iterator_get_next(&scan) != '-')
        return error_handler(SYNTAXERROR) ;
    }

    /* Must be at least one char. */
    if (! utf8_iterator_more(&scan))
      return error_handler(SYNTAXERROR) ;
    c = utf8_iterator_get_next(&scan) ;
    if (! LANG_IS_ALPHA(c) && ! LANG_IS_DIGIT(c))
      return error_handler(RANGECHECK) ;

    /* Scan subtag */
    for (i=0; i<7; i++) {
      if (! utf8_iterator_more(&scan))
        break ;
      c = utf8_iterator_get_next(&scan) ;

      if (LANG_IS_ALPHA(c) || LANG_IS_DIGIT(c))
        continue ;

      if (c == '-') {
        break ;
      } else {
        return error_handler(SYNTAXERROR) ;
      }
    }
  }

  /* We have a Subtag ending in -. */
  if (c == '-')
    return error_handler(SYNTAXERROR) ;

  /* We always consume the whole string. */
  if ( !intern_create(intern, value->codeunits, value->unitlength) )
    return FALSE ;

  *value = scan ;
  return TRUE ;
}

/* XML string enumerated type converter with a base of string. We
   simply intern the whole value. */
Bool xml_convert_string_enum(xmlGFilter *filter,
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

  value->codeunits  = UTF_BUFFER_LIMIT(*value) ;
  value->unitlength = 0 ;

  return TRUE ;
}

Bool xml_convert_integer(xmlGFilter *filter,
                         xmlGIStr *attrlocalname,
                         utf8_buffer* value,
                         void *data /* int32* */)
{
  int32* p_int = data ;
  utf8_buffer scan ;
  uint32 digits ;
  int32 intvalue = 0, negate_xor = 0, negate_add = 0 ;

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(p_int, "Nowhere to put scanned integer") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");

  scan = *value ;
  if ( scan.unitlength == 0 )
    return error_handler(SYNTAXERROR) ;

  /* Since the string is UTF-8, we can compare single codepoints to ASCII. We
     cannot use strtol() because (a) the string is not null terminated, and
     (b) we want to cope with an arbitrary number of digits, clipping the
     range of values to MAXINT32/MININT32. */
  switch ( scan.codeunits[0] ) {
  case '-':
    /* Conditionally negate digits using -x = ~x + 1 to avoid multiplication. */
    negate_xor = -1 ;
    negate_add = 1 ;
    /*@fallthrough@*/
  case '+':
    ++scan.codeunits ;
    --scan.unitlength ;
    break ;
  }

  for ( digits = 0 ; scan.unitlength > 0 ;
        ++scan.codeunits, --scan.unitlength, ++digits ) {
    int32 digit = scan.codeunits[0] - '0' ;

    if ( digit < 0 || digit > 9 )
      break ;

    digit ^= negate_xor ;
    digit += negate_add ;

    if ( intvalue >= MAXINT32 / 10 ) {
      intvalue = MAXINT32 ;
      HQFAIL("xml_to_int: integer out of range, limiting to max int") ;
    } else if ( intvalue <= MININT32 / 10 ) {
      intvalue = MININT32 ;
      HQFAIL("xml_to_int: integer out of range, limiting to min int") ;
    } else
      intvalue = intvalue * 10 + digit ;
  }

  if ( digits == 0 ) {
    return error_handler(SYNTAXERROR) ;
  }

  *p_int = intvalue ;
  *value = scan ;

  return TRUE ;
}

#define BIGGEST_REAL_DIV_10 (DBL_MAX / 10)

Bool xml_convert_double(xmlGFilter *filter,
                        xmlGIStr *attrlocalname,
                        utf8_buffer* value,
                        void *data /* double* */)
{
  double *p_double = data ;
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

  UNUSED_PARAM(xmlGFilter *, filter) ;
  UNUSED_PARAM(xmlGIStr *, attrlocalname) ;

  HQASSERT(p_double, "Nowhere to put scanned double") ;
  HQASSERT(value != NULL, "No UTF-8 string to convert");

  scan = *value ;
  if ( scan.unitlength == 0 )
    return error_handler(RANGECHECK) ;

  ch = *scan.codeunits++ ;
  --scan.unitlength ;

  /* Since the string is UTF-8, we can compare single codepoints to ASCII. We
     cannot use strtod() because (a) the string is not null terminated,
     (b) we want to cope with an arbitrary number of digits, and (c) strtod() is
     locale sensitive, i.e. the decimal separator may be other than a period,
     while XML double is always a period. */

  if (ch == '-' || ch == '+') {
    if (ch == '-') {
      sign = -1 ;
    }
    if (scan.unitlength == 0)
      return error_handler(SYNTAXERROR) ;
    ch = *scan.codeunits++ ;
    --scan.unitlength ;
  }

  /* Check for NaN and (-)INF. */
  if ( scan.unitlength >= 3 ) {
    if ( (scan.codeunits[0] == 'N' && scan.codeunits[1] == 'a' && scan.codeunits[2] == 'N') ||
         (scan.codeunits[0] == 'I' && scan.codeunits[1] == 'N' && scan.codeunits[2] == 'F') )
      return error_handler(UNDEFINEDRESULT) ;
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
      return error_handler(RANGECHECK) ;

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
    switch(ch) {

    case 'E':
    case 'e':
      if (ntotal == 0)
        return error_handler(SYNTAXERROR) ;
      if (scan.unitlength == 0)
        return error_handler(SYNTAXERROR) ;
      have_exp = TRUE ;
      ch = *scan.codeunits++ ;
      --scan.unitlength ;

      if (ch == '-' || ch == '+') {
        if (ch == '-')
          exp_sign = -1 ;
        if (scan.unitlength == 0)
          return error_handler(SYNTAXERROR) ;
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
          return error_handler(RANGECHECK) ;

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
        return error_handler(SYNTAXERROR) ;
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
            return error_handler(RANGECHECK) ;

          fleading = 10.0 * fleading + ch ;
        }
        ntrailing = ntotal - nleading ;

        /* According to the pattern, . must be followed by some
           characters. */
        if (ntrailing == 0)
          return error_handler(SYNTAXERROR) ;
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
      return error_handler(SYNTAXERROR) ;
    if (scan.unitlength == 0)
      return error_handler(SYNTAXERROR) ;
    have_exp = TRUE ;
    ch = *scan.codeunits++ ;
    --scan.unitlength ;

    if (ch == '-' || ch == '+') {
      if (ch == '-')
        exp_sign = -1 ;
      if (scan.unitlength == 0)
        return error_handler(SYNTAXERROR) ;
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
        return error_handler(RANGECHECK) ;

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
      return error_handler(SYNTAXERROR) ;
  } else {
    if (! all_consumed) {
      scan.codeunits-- ; /* unget this char */
      ++scan.unitlength ;
    }
  }

  /* Bad number; probably just a {+,-,.,+.,-.}. */
  if ( ntotal == 0 )
    return error_handler(SYNTAXERROR) ;

  /* Convert number to a real. */
  if ( ntotal == nleading && nleading > 0 ) {
    /* We scanned (m) or (m.). */
  } else {
    /* We scanned (.n) or (m.n). */
    if ( ntrailing < 10 ) {
      /* Avoid underflow - be careful with 0.0 case */
      if (fleading > 0.0 && (DBL_MIN * ntrailing * 10) > fleading)
        return error_handler(RANGECHECK) ;
      fleading = fleading * fdivs[ ntrailing ] ;
    } else {
      /* Avoid pow() raising an overfow */
      if (ntrailing < DBL_MIN_10_EXP || ntrailing > DBL_MAX_10_EXP)
        return error_handler(RANGECHECK) ;

      power = pow( 10.0 , ( double )( ntrailing )) ;

      /* Avoid division raising an overflow. This is not efficient but
         I can't think of another way to do this. */
      if ((DBL_MIN * power) > fleading)
        return error_handler(RANGECHECK) ;

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
        return error_handler(RANGECHECK) ;

      if (exp_sign < 0)
        eleading = -eleading ;

      power = pow(10.0, eleading) ;
    }

    /* Avoid multiplication raising an overfow. This is not efficient
       but I can't think of another way to do this. */
    if (power > 1.0 && ((DBL_MAX / power) < fleading))
      return error_handler(RANGECHECK) ;

    fleading = fleading * power ;
  }

  if ( sign < 0 )
    fleading = -fleading ;

  *p_double = fleading;
  *value = scan ;

  return TRUE;
}

Bool xml_convert_float(xmlGFilter *filter,
                       xmlGIStr *attrlocalname,
                       utf8_buffer* value,
                       void *data /* float* */)
{
  double number ;

  if ( !xml_convert_double(filter, attrlocalname, value, &number) )
    return FALSE ;

  if ( number > FLT_MAX || -number > FLT_MAX )
    return error_handler(RANGECHECK) ;

  *(float*)data = (float)number ;

  return TRUE ;
}

/* ============================================================================
Log stripped */
