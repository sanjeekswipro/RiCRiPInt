/* $HopeName: SWsecurity!export:donglib.h(EBDSDK_P.1) $ */
/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/*
Log stripped */

/************************************************************
 *   Header file for retrieving the dongle serial number    *
 *   Usage:                                                 *
 *      #include "donglib.h"                                *
 *      int  dongle_serialnumber ( int  *dsn);              *
 *   Return value:                                          *
 *      1: successful: serial number is put in dsn;         *
 *      0: failure.                                         *
 *   ----------------------------------------------------   *
 *   Example:                                               *
 *                                                          *
 *	 #include <stdio.h>	                            *
 *	 #include "donglib.h"	                            *
 *	                                                    *
 *	 main()                                             *
 *	 {                                                  *
 *	 int sn;                                            *
 *                                                          *
 *	 if (dongle_serialnumber (&sn) == 1 )               *
 *	   printf ("the serial number is %d\n\n", sn);      *
 *	 else                                               *
 *	   printf ("dongle test failed\n\n");               *
 *	 }                                                  *
 ************************************************************/


extern int dongle_serialnumber(int *);
