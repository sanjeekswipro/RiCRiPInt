/* Copyright (C) 2005-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_psconfig.h(EBDSDK_P.1) $
 * 
 */
/*! \file
 *  \ingroup OIL
 *  \brief This header file declares the interface used for creating the PostScript 
 * language snippets required for RIP configuration.
 *
 */
#ifndef _OIL_PSCONFIG_H_
#define _OIL_PSCONFIG_H_


extern unsigned char szQuitPS[];


/* interface functions */
extern BOOL SetupPDLType(BOOL bSendFlag, char *pBuff, unsigned int nBuffSize);
extern void UpdateOILComment(char *comment);

extern unsigned char * GetConfigPS( OIL_eTyPDLType ePDLType );

int SetupPCLMedia( char *pBuff );

#endif /* _OIL_PSCONFIG_H_ */
