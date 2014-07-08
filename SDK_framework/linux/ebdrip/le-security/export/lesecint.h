/*
 * $HopeName: SWle-security!export:lesecint.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */

/**
 * @file
 *
 * @brief Internal header for LE security (cf external interfaces in lesec.h)
 */

#ifndef __LESECINT_H__
#define __LESECINT_H__


/* ----------------------------- Includes ---------------------------------- */

#include "product.h"


/* ----------------------------- Functions --------------------------------- */

/**
 * @brief Perform full security test
 */
extern int32 fullTestLeSec( int32 * resultArray );

/**
 * @brief Perform quick security test
 */
extern int32 testLeSec( void );

/**
 * @brief Report error
 */
extern void reportLeSecError( void );


/*
* Log stripped */

#endif /* __LESECINT_H__ */

/* end of lesecint.h */
