/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swctype.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Since the core RIP is always running in a "virtual C * locale", this file
 * provides simple character type macros, after undef-ing them first in case
 * standard headers have been included.
 */

#ifndef __SWCTYPE_H__
#define __SWCTYPE_H__

#ifdef __cplusplus
extern "C" {
#endif

#define CTYPE_U 01
#define CTYPE_L 02
#define CTYPE_N 04
#define CTYPE_S 010
#define CTYPE_P 020
#define CTYPE_C 040
#define CTYPE_X 0100
#define CTYPE_B 0200

extern  uint8 swctype[256];

#undef isalpha
#undef isupper
#undef islower
#undef isdigit
#undef isxdigit
#undef isspace
#undef ispunct
#undef isalnum
#undef isprint
#undef isgraph
#undef iscntrl
#undef isascii
#undef toupper
#undef tolower
#undef toascii

#define isalpha(c)  ((int32)(swctype[c])&(CTYPE_U|CTYPE_L))
#define isupper(c)  ((int32)(swctype[c])&CTYPE_U)
#define islower(c)  ((int32)(swctype[c])&CTYPE_L)
#define isdigit(c)  ((int32)(swctype[c])&CTYPE_N)
#define isxdigit(c) ((int32)(swctype[c])&CTYPE_X)
#define isspace(c)  ((int32)(swctype[c])&CTYPE_S)
#define ispunct(c)  ((int32)(swctype[c])&CTYPE_P)
#define isalnum(c)  ((int32)(swctype[c])&(CTYPE_U|CTYPE_L|CTYPE_N))
#define isprint(c)  ((int32)(swctype[c])&(CTYPE_P|CTYPE_U|CTYPE_L|CTYPE_N|CTYPE_B))
#define isgraph(c)  ((int32)(swctype[c])&(CTYPE_P|CTYPE_U|CTYPE_L|CTYPE_N))
#define iscntrl(c)  ((int32)(swctype[c])&CTYPE_C)
#define isascii(c)  ((unsigned)(c)<=0177)
#define toupper(c)  (islower(c) ? (c)-'a'+'A' : (c))
#define tolower(c)  (isupper(c) ? (c)-'A'+'a' : (c))
#define toascii(c)  ((c)&0177)

#ifdef __cplusplus
}
#endif


#endif /* protection for multiple inclusion */
