/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:intscrty.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file contains common information between the interpreter
 * and the security mechanism that is in place.
 */

#ifndef __INTSCRTY_H__
#define __INTSCRTY_H__

/* give the security call a none obvious name */
#define doSecurityCheck buildMessage

/* an initialiser for the randomiser */
#define CODEINITIALISER 0x34CAE613

#define securityID(securityCheckArray) ((securityCheckArray)[2] & 0xFFFF)
#define maxResolution(securityCheckArray) (100 * (((securityCheckArray)[0] >> 16) & 0xFF))
#define custID(securityCheckArray) (((securityCheckArray)[0] >> 24) & 0x7F)
#define demoOnly(securityCheckArray) (((securityCheckArray)[0] >> 31) & 0x1)
#define versionAllowed(securityCheckArray) ((securityCheckArray)[0] & 0x3F)


/* the security check call */
extern uint32 doSecurityCheck(uint32 code, uint32 val[8]);

/* The dongle returns a feature level number, which allows the RIP to
 * disable certain features of the RIP.
 *
 * Currently the only feature of the RIP that can be disabled is HPS,
 * though, in principle, things like auto-separations, contone,
 * step-and-repeat are features that could be disabled.
 *
 * When the mapping between the feature level number and the feature
 * itself gets more complicated, the following can be replaced by a function.
 */

#define RIP_DenyFeature( feature )  (DongleFeatureLevel() > 0)

/* currently only supports: */
#define RIPFEATURE_HPS          1
/* other possibilities could be... */
#define RIPFEATURE_SEPARATIONS  2
#define RIPFEATURE_CONTONE      3
#define RIPFEATURE_STEPREPEAT   4
/* etc. */

#endif /* protection for multiple inclusion */

/* Log stripped */
