/** \file
 * \ingroup ps
 *
 * $HopeName: SWcore!shared:uvms.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2001-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * U V S and U V M macros for localisation of messages. Enclose all
 * user-visible messages which should be translated (basically, all messages
 * except debugging messages and messages which are part of the language).
 * These macros are dummys for functionality found in the FrameWork.
 */

#ifndef __UVMS_H__
#define __UVMS_H__

/* U(ser)V(isible)M(essage) is the default way for clients to mark
 * literal strings used as sprintf templates in their source code that
 * need translation (allowing for variable field substitution).
 */

#define UVM( stringConstant ) ((uint8 *) stringConstant)

/* U(ser)V(isible)S(tring) is the default way for clients to mark fixed
 * literal strings in their source code that need translation.  These
 * strings can be single words, phrases, or message fragments that have
 * a fixed translation.
 */

#define UVS( stringConstant ) ((uint8 *) stringConstant)


#endif /* Protection from multiple inclusion */

/* Log stripped */
