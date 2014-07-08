/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/* Motif Customized resources
 * $HopeName: SWcustomer!src:motifres.c(EBDSDK_P.1) $
 */
 

/*
* Log stripped */

/* ------------------------------- Includes -------------------------------- */

#include "motifres.h" /* includes product.h */

/* customer */
#include "customer.h" /* PRODUCTNAME */

/* v20iface */
#include "ripversn.h" /* PRODUCT_VERSION_QUOTED */


/* ---------------------------- Resources Data --------------------------- */

#ifdef PRODUCT_HAS_BUILTIN_GUI

/* NB. Changing this string will change the CRC checksum for all customers */
char * sudgCopyrightResources = "\n\
*Crightdg.wcConstructor:		XmCreateFormDialog\n\
*Crightdg.wcChildren:			oemmessage, message,  wmessage, ok, swsmalllogo, pantonelogo, rsalogo\n\
*Crightdg.wcManaged:			False\n\
*Crightdg.width:			560\n\
*Crightdg.height:			530\n\
*Crightdg.noResize:			True\n\
*Crightdg.dialogTitle:			" LONGPRODUCTNAME "\n\
*Crightdg.dialogStyle:			DIALOG_FULL_APPLICATION_MODAL\n\
*Crightdg.dialogType:			DIALOG_WARNING\n\
*Crightdg.defaultPosition:		False\n\
*Crightdg*background:			white\n\
*Crightdg*foreground:			black\n\
*Crightdg.oemmessage.wcConstructor:	XmCreateLabelGadget\n\
*Crightdg.oemmessage.leftAttachment:	ATTACH_FORM\n\
*Crightdg.oemmessage.topAttachment:	ATTACH_FORM\n\
*Crightdg.oemmessage.rightAttachment:	ATTACH_FORM\n\
*Crightdg.oemmessage.topOffset:		5\n\
*Crightdg.message.wcConstructor:	XmCreateLabelGadget\n\
*Crightdg.message.fontList:	        -*-helvetica-medium-r-normal--12-*\n\
*Crightdg.message.leftAttachment:	ATTACH_FORM\n\
*Crightdg.message.topAttachment:	ATTACH_WIDGET\n\
*Crightdg.message.topWidget:	        ^oemmessage\n\
*Crightdg.message.rightAttachment:	ATTACH_FORM\n\
*Crightdg.message.topOffset:		5\n\
*Crightdg.wmessage.wcConstructor:	XmCreateLabelGadget\n\
*Crightdg.wmessage.bottomAttachment:    ATTACH_FORM\n\
*Crightdg.wmessage.bottomOffset:        40\n\
*Crightdg.wmessage.leftAttachment:	ATTACH_FORM\n\
*Crightdg.wmessage.rightAttachment:	ATTACH_FORM\n\
*Crightdg.ok.wcConstructor:		XmCreatePushButton\n\
*Crightdg.ok.labelString:		OK\n\
*Crightdg.ok.width:			90\n\
*Crightdg.ok.bottomAttachment:		ATTACH_FORM\n\
*Crightdg.ok.bottomOffset:		15\n\
*Crightdg.ok.rightAttachment:		ATTACH_FORM\n\
*Crightdg.ok.rightOffset:		15\n\
*Crightdg.ok.activateCallback:		EndOkay\n\
*Crightdg*swsmalllogo.wcConstructor:		XmCreateLabelGadget\n\
*Crightdg*swsmalllogo.labelType:		PIXMAP\n\
*Crightdg*swsmalllogo.leftAttachment:		ATTACH_FORM\n\
*Crightdg*swsmalllogo.leftOffset:		20\n\
*Crightdg*swsmalllogo.topAttachment:		ATTACH_FORM\n\
*Crightdg*swsmalllogo.topOffset:		23\n\
*Crightdg*pantonelogo.wcConstructor:		XmCreateLabelGadget\n\
*Crightdg*pantonelogo.labelType:		PIXMAP\n\
*Crightdg*pantonelogo.leftAttachment:		ATTACH_FORM\n\
*Crightdg*pantonelogo.leftOffset:		20\n\
*Crightdg*pantonelogo.bottomAttachment:		ATTACH_FORM\n\
*Crightdg*pantonelogo.bottomOffset:		72\n\
*Crightdg*rsalogo.wcConstructor:		XmCreateLabelGadget\n\
*Crightdg*rsalogo.labelType:		        PIXMAP\n\
*Crightdg*rsalogo.rightAttachment:		ATTACH_FORM\n\
*Crightdg*rsalogo.rightOffset:		        35\n\
*Crightdg*rsalogo.bottomAttachment:		ATTACH_FORM\n\
*Crightdg*rsalogo.bottomOffset:		        100\n\
";


/* NB. Changing this string will change the CRC checksum for all customers */
/* NB, this is a format string. The PRODUCT_VERSION_QUOTED
 * needs to be sprintf'ed into it.
 */
char * CopyrightOEMMessageString = 
#if defined(AII)
"APS GRAFIX RIP is a registered trademark of Autologic Information International, Inc."
#else
#if defined(TRIPLE_I)
"The triple-i Level 3 RIP is a trademark of Informational International, Inc."
#else
#if defined(SEI)
"ScriptWorks by Harlequin Limited."
#else
#if defined(CORTRON)
"ImageRip is a registered trademark of Cortron Corporation."
#else
#if defined(LDRRIP) || defined(LDRRIPSMP)
"FusionDFE is a registered trademark of LDR"
#else
"ScriptWorks by Harlequin Limited."
#endif
#endif
#endif
#endif
#endif
"\n\
ScriptWorks  RIP   Version %s\n\
Copyright 1989-%s Harlequin Limited.";

/* NB. Changing this string will change the CRC checksum for all customers */
char * CopyrightMessageString = 
"Type 1 font renderer contains licensed third party software\n\
Copyright 1991 International Business Machines, Corp.\n\
Copyright 1991 Lexmark International, Inc.\n\
Portions copyright 1990 Adobe Systems Incorporated.\n\
TrueType (R) font renderer copyright 1997 Bitstream, Inc.\n"
#if defined (LINOTYPE_FONTS)
"Font data copyright 1991-1995 Linotype Hell Corp.\n\
Other portions licensed under U.S. Patent No. 4,500,919.\n"
#elif defined(BITSTREAM_FONTS)
"Typeface Outlines copyright Bitstream 1991-1997\n\
ITC Typeface Outlines copyright International Typeface Corporation\n"
#else
"\n\
\n"
#endif
"All Rights Reserved.\n\
\n\
LZW licensed under U.S. Patent No. 4,558,302 and foreign counterparts.\n\
\n\
TrapWorks licensed under one or more of the following U.S. Patents:\n\
Nos 5,113,249, 5,323,248, 5,420,702, 5,481,379.\n\
\n\
Harlequin and ScriptWorks are registered trademarks of Harlequin Limited.\n\
Harlequin Full Color System (HFCS), Harlequin ICC Profile Processor (HIPP),\n\
Harlequin Standard Color System (HSCS), Harlequin Precision Screening (HPS),\n\
Harlequin Dispersed Screening (HDS), Harlequin Micro Screening (HMS),\n\
Harlequin Chain Screening (HCS), Harlequin Display List Technology (HDLT),\n\
TrapMaster and TrapWorks are all trademarks of Harlequin Limited.\n\
TrueType is a registered trademark of Apple Computer, Inc.\n\
PANTONE (R) and PANTONE CALIBRATED (TM) are trademarks of Pantone, Inc.";


/* NB. Changing this string will change the CRC checksum for all customers */
char * CopyrightWarningMessageString =
"Harlequin Limited accepts no responsibility or liability for\n\
any claims whatsoever arising out of the use of this software.";

char * CopyrightVersionString = PRODUCT_VERSION_QUOTED;


/* NB. Changing this string will change the CRC checksum for all customers */
char * sudgLogoResources = "\n\
*Logo.wcConstructor:			XmCreateFormDialog\n\
*Logo.wcChildren:			picture, message, starting\n\
*Logo*resizable:			False\n\
*Logo*noResize:				True\n\
*Logo.wcManaged:			False\n\
*Logo.defaultPosition:			False\n\
*Logo.dialogTitle:			" PRODUCTNAME

#if defined(TRIPLE_I)
"\n\
*Logo.width:				420\n\
*Logo.height:				335"
#else
#if defined(SEI)
"\n\
*Logo.width:				450\n\
*Logo.height:				365"
#else
#if defined(CORTRON)
"\n\
*Logo.width:				880\n\
*Logo.height:				480"
#else
#if defined(AII)
"\n\
*Logo.width:				870\n\
*Logo.height:				400"
#else
#if defined(LDRRIP) || defined(LDRRIPSMP)
"\n\
*Logo.width:				618\n\
*Logo.height:				245"
#else
"\n\
*Logo.width:				470\n\
*Logo.height:				250"
#endif
#endif
#endif
#endif
#endif
"\n\
*Logo*picture.wcConstructor:		XmCreateLabelGadget\n\
*Logo*picture.labelType:		PIXMAP\n\
*Logo*picture.leftAttachment:		ATTACH_FORM\n"

#if defined(TRIPLE_I)
"*Logo*picture.leftOffset:		3\n"
#endif
"*Logo*picture.topAttachment:		ATTACH_FORM\n"
#ifndef TRIPLE_I
"*Logo*picture.bottomAttachment:	ATTACH_FORM\n"
#endif

"*Logo*message.wcConstructor:		XmCreateLabelGadget\n\
*Logo*message.topAttachment:		ATTACH_FORM\n\
*Logo*message.rightAttachment:		ATTACH_FORM\n"
#if defined(TRIPLE_I)
"*Logo*message.topOffset:		270\n\
*Logo*message.rightOffset:		50\n"
#else
#if defined(SEI)
"*Logo*message.topOffset:		130\n\
*Logo*message.rightOffset:		20\n"
#else
#if defined(CORTRON)
"*Logo*message.topOffset:		135\n\
*Logo*message.rightOffset:		30\n"
#else
#if defined(MORISAWA)
"*Logo*message.topOffset:		66\n\
*Logo*message.rightOffset:		30\n"
#else
#if defined(AII)
"*Logo*message.topOffset:		135\n\
*Logo*message.rightOffset:		30\n"
#else
#if defined(LDRRIP) || defined(LDRRIPSMP)
"*Logo*message.topOffset:		50\n\
*Logo*message.rightOffset:		45\n"
#else
"*Logo*message.topOffset:		80\n\
*Logo*message.rightOffset:		30\n"
#endif
#endif
#endif
#endif
#endif
#endif
"\n\
*Logo*starting.wcConstructor:		XmCreateLabel\n\
*Logo*starting.labelString:		Starting Up ...\n\
*Logo*starting.topAttachment:		ATTACH_WIDGET\n\
*Logo*starting.topWidget:		^message\n\
*Logo*starting.topOffset:		13\n\
*Logo*starting.rightAttachment:		ATTACH_FORM\n"
#if defined(TRIPLE_I)
"*Logo*starting.rightOffset:		165\n"
#else
#if defined(SEI)
"*Logo*starting.rightOffset:		38\n"
#else
#if defined(CORTRON)
"*Logo*starting.rightOffset:		72\n"
#else
#if defined(MORISAWA)
"*Logo*starting.rightOffset:		45\n"
#else
#if defined(AII)
"*Logo*starting.rightOffset:		72\n"
#else
#if defined(LDRRIP) || defined(LDRRIPSMP)
"*Logo*starting.rightOffset:		61\n"
#else
"*Logo*starting.rightOffset:		45\n"
#endif
#endif
#endif
#endif
#endif
#endif
"\n\
*Logo*dismiss.wcConstructor:		XmCreatePushButton\n\
*Logo*dismiss.labelString:		Dismiss\n\
*Logo*dismiss.bottomAttachment:		ATTACH_FORM\n\
*Logo*dismiss.bottomOffset:		15\n\
*Logo*dismiss.rightAttachment:		ATTACH_FORM\n\
*Logo*dismiss.rightOffset:		83\n\
*Logo*dismiss.activateCallback:		sudg_dismissCB()\n\
";

/* NB. Changing this string will change the CRC checksum for all customers */
char * LogoMessageString = 
#if defined(SEI)
"Level 3\n\
\n\
\n\
\n\
Copyright %s\n\
\n\
Harlequin Limited\n\
"
#else
#if defined(TRIPLE_I)
"Level 3, Copyright %s, Harlequin Limited\n"
#else
#if defined(CORTRON)
"Level 3\n\
\n\
\n\
\n\
Copyright %s\n\
\n\
Harlequin Limited\n"
#else
#if defined(MORISAWA)
"Level 3\n\
\n\
\n\
Copyright %s\n\
\n\
Harlequin Limited\n"
#else
#if defined(AII)
"Level 3\n\
\n\
\n\
\n\
Copyright %s\n\
\n\
Harlequin Limited\n"
#else
#if defined(LDRRIP) || defined(LDRRIPSMP)
"Level 3\n\
\n\
\n\
\n\
Copyright %s\n\
\n\
Harlequin Limited\n"
#else
"Level 3\n\
\n\
\n\
\n\
Copyright %s\n\
\n\
Harlequin Limited\n"
#endif
#endif
#endif
#endif
#endif
#endif
;

/* ---------------------------- Icon Data --------------------------- */

#include "icon.h"


/* ---------------------------- Logo Data --------------------------- */

#include "logo.h"

#else /* PRODUCT_HAS_BUILTIN_GUI */

int _dummy_motifres_;

#endif /* PRODUCT_HAS_BUILTIN_GUI */

/* eof motifres.c */
