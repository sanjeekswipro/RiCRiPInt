/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
 /* $Id: export:customer.h,v 1.391.1.8.1.1 2013/12/19 11:26:07 anon Exp $

    $HopeName: SWcustomer!export:customer.h(EBDSDK_P.1) $

  This file gives customer details. The intention is that this will be the
  *only* place in the RIP in which the customer name is used; all other
  customisation will be done by defines set in this file.
*/


/*

Log stripped */


/*******************************************************************/

#ifndef _customer_h_
#define _customer_h_

/* Use these values against FULLRIPFONTS and MICRORIPFONTS */
#define NOFONTS 0
#define LINOTYPE 1
#define BITSTREAM 2
/* Largest valid number to use in defines above, so we can sanity check
   elsewhere */
#define FONT_MAX_VALID_NUM 2


#ifndef UVS
#define UVS(_str_)   ((uint8 *)_str_)
#endif


#define CMSNAME          UVS( "Color:" )
#define LONGCMSNAME      UVS( "Color Setup Manager" )
#define SECURITYCMSNAME  UVS( "HFCS - Harlequin Full Color System" )

#define HCMS_TYPE_ICC    UVS( "ICC" )       /* ICC Workflow */
#define HCMS_TYPE_LITE   UVS( "HSCS" )      /* Lite HCMS */
#define HCMS_TYPE_HCP    UVS( "HCP" )       /* HCP */
#define HCMS_TYPE_FULL   UVS( "HFCS" )      /* Full HCMS */




#ifdef  MACINTOSH


























#ifdef OBSOLETE
#endif

















#ifdef EBDEVAL
/* EBDEVAL */
#define PRODUCTABOUT       "About Embedded Evaluation RIP..."
#define PRODUCTNAME        "EmbeddedEvalRIP"
#define LONGPRODUCTNAME    "Embedded Evaluation RIP"
#define RIPCUSTOMERNUMBER  0x63
#define PRODUCTMENU        PRODUCTNAME
#define OEMTRADEMARK       "Harlequin¨ RIP by " THE_HARLEQUIN_GROUP "."
#define FULLRIPFONTS LINOTYPE
#define MICRORIPFONTS LINOTYPE
#endif


#ifndef PRODUCTNAME
/* i.e. generic */
#define PRODUCTABOUT      "About Harlequin MultiRIP..."
#define LONGPRODUCTNAME    "Harlequin MultiRIP"
#define PRODUCTNAME        "Harlequin MultiRIP"
#define RIPCUSTOMERNUMBER  0x0B
#define PRODUCTMENU       PRODUCTNAME
#define OEMTRADEMARK       "Harlequin¨ MultiRIPª by " THE_HARLEQUIN_GROUP "."
#define FULLRIPFONTS LINOTYPE
#define MICRORIPFONTS LINOTYPE

#define START_DLG_LOGO_AT_TOP TRUE
#define CAN_SUPPRESS_STARTUP_DIALOGS TRUE
#endif

#endif  /* MACINTOSH */

/*****************************************************************************/

#if defined(IBMPC) || defined(WIN32) || defined(RC_INVOKED)


























#ifdef OBSOLETE
#ifdef HARRIS
/* Harris */	/* not an OEM any more */
#define LONGPRODUCTNAME    "HarrisRIP"
#define PRODUCTNAME        "HarrisRIP"
#define RIPCUSTOMERNUMBER  0x4E
#define PRODUCTMENU        "HarrisRIP"
#define PRODUCTMENUMNEMONIC "H"
#define PRODUCTABOUT       "About HarrisRIP"
#define OEMTRADEMARK       "Harlequin (R) RIP by " THE_HARLEQUIN_GROUP "."
#define VENDOR             "Harris Publishing"
#define FULLRIPFONTS LINOTYPE
#define MICRORIPFONTS LINOTYPE
#endif
#endif


































#ifdef OBSOLETE
#ifdef NUR
/* NUR */	/* not an OEM any more */
#define LONGPRODUCTNAME    "NUR RIP"
#define PRODUCTNAME        "NUR RIP"
#define RIPCUSTOMERNUMBER  0x4B
#define PRODUCTMENU        "NUR RIP"
#define PRODUCTMENUMNEMONIC "N"
#define PRODUCTABOUT       "About NUR RIP"
#define OEMTRADEMARK       "NUR RIP for use with NUR Fresco wide format digital press"
#define VENDOR             "Nur"
#define FULLRIPFONTS LINOTYPE
#define MICRORIPFONTS LINOTYPE
#endif
#endif




















#ifdef OBSOLETE
#ifdef SEI
/* SeiNet (was ESE) */	/* not an OEM any more */
#define LONGPRODUCTNAME    "PageRIP"
#define PRODUCTNAME        "PageRIP"
#define RIPCUSTOMERNUMBER  0x1D
#define PRODUCTMENU        "PageRIP"
#define PRODUCTMENUMNEMONIC "R"
#define PRODUCTABOUT       "About PageRIP"
#define OEMTRADEMARK       "Harlequin (R) RIP by " THE_HARLEQUIN_GROUP "."
#define MORISAWA_CUSTOMER_NO 0xc49823b6
#define MORISAWA_PRODUCTNAME    "ESE PC RIP"
#define VENDOR             "ESE"
#define FULLRIPFONTS LINOTYPE
#define MICRORIPFONTS LINOTYPE
#endif
#endif





















#ifdef EBDEVAL
/* EBDEVAL */
#define LONGPRODUCTNAME    "Embedded Evaluation RIP"
#define PRODUCTNAME        "EmbeddedEvalRIP"
#define RIPCUSTOMERNUMBER  0x63
#define PRODUCTMENU        "EmbeddedEvalRIP"
#define PRODUCTMENUMNEMONIC "E"
#define PRODUCTABOUT       "About Embedded Evaluation RIP"
#define OEMTRADEMARK       "Harlequin (R) RIP by " THE_HARLEQUIN_GROUP "."
#define VENDOR             "Global Graphics Software Ltd"
#define FULLRIPFONTS LINOTYPE
#define MICRORIPFONTS LINOTYPE
#endif



#ifndef LONGPRODUCTNAME    /* default is "defined(HARLQN)" */
#define LONGPRODUCTNAME    "PC Harlequin MultiRIP Level 3"
#define PRODUCTNAME        "Harlequin MultiRIP"
#define RIPCUSTOMERNUMBER  0x0B
#define PRODUCTMENU        "Harlequin MultiRIP"
#define PRODUCTMENUMNEMONIC "S"
#define PRODUCTABOUT       "About Harlequin MultiRIP"
#define OEMTRADEMARK       "Harlequin (R) MultiRIP (TM) by " THE_HARLEQUIN_GROUP "."
#define VENDOR             "Harlequin"
#define FULLRIPFONTS LINOTYPE
#define MICRORIPFONTS LINOTYPE

#define CAN_SUPPRESS_STARTUP_DIALOGS TRUE
#endif

#endif  /* IBMPC || WIN32 */

/*****************************************************************************/

#ifdef	UNIX


#ifdef OBSOLETE
#ifdef SEI
/* SeiNet (was ESE) */	/* not an OEM any more */
#define PRODUCTNAME		"PageRIP"
#define LONGPRODUCTNAME		"PageRIP"
#define RIPCUSTOMERNUMBER       0x1D
#define OEMTRADEMARK            "Harlequin (R) RIP by " THE_HARLEQUIN_GROUP "."
#define PRODUCTMENU             "PageRIP"
#define PRODUCTMENUMNEMONIC     "R"
#define PRODUCTABOUT            "About PageRIP"
#define FULLRIPFONTS LINOTYPE
#define MICRORIPFONTS LINOTYPE
#endif
#endif


#ifdef MORISAWA
/* Morisawa */	/* not an OEM any more */
/* This is the 'real' morisawa customer */
#define PRODUCTNAME		"RISARIP-HQ"
#define LONGPRODUCTNAME		"RISARIP-HQ"
#define RIPCUSTOMERNUMBER       0x38
#define PRODUCTMENU		"RISARIP-HQ"
#define PRODUCTMENUMNEMONIC     "R"
#define PRODUCTABOUT		"About RISARIP-HQ"
#define MORISAWA_CUSTOMER_NO 0xc49823b5
#define FULLRIPFONTS LINOTYPE
#define MICRORIPFONTS LINOTYPE
#endif


#ifdef FUSIONRIPSMP
/* LDR */
/* LDR's single and parallell rips have the same customization for now */
#define PRODUCTNAME		"Fusion RIP"
#define LONGPRODUCTNAME		"Fusion RIP"
#define RIPCUSTOMERNUMBER	0x42
#define OEMTRADEMARK            "The Fusion Systems International product and service names are pending trademarks\nor service marks of LDR International, Inc.,and may be registered in certain jurisdictions."
#define PRODUCTMENU		"Fusion RIP"
#define PRODUCTMENUMNEMONIC     "N"
#define PRODUCTABOUT		"About Fusion RIP"
#define FULLRIPFONTS LINOTYPE
#define MICRORIPFONTS LINOTYPE
#endif

#ifdef MAILCOM
/* EasyLink (was Mail.com) */
#define PRODUCTNAME		"MailComRip"
#define LONGPRODUCTNAME		"MailComRip"
#define RIPCUSTOMERNUMBER       0x54
#define OEMTRADEMARK            "Harlequin (R) RIP by " THE_HARLEQUIN_GROUP "."
#define PRODUCTMENU             "MailComRip"
#define PRODUCTMENUMNEMONIC     "M"
#define PRODUCTABOUT            "About MailComRip"
#define FULLRIPFONTS LINOTYPE
#define MICRORIPFONTS LINOTYPE
#endif






#ifdef EBDEVAL
/* EBDEVAL */
#define PRODUCTNAME        "EmbeddedEvalRIP"
#define LONGPRODUCTNAME    "Embedded Evaluation RIP"
#define RIPCUSTOMERNUMBER  0x63
#define PRODUCTMENU        "EmbeddedEvalRIP"
#define PRODUCTMENUMNEMONIC     "M"
#define PRODUCTABOUT       "About Embedded Evaluation RIP"
#define FULLRIPFONTS LINOTYPE
#define MICRORIPFONTS LINOTYPE
#endif













#ifndef PRODUCTNAME
#define PRODUCTNAME		"Harlequin MultiRIP"
#define LONGPRODUCTNAME		"Unix Harlequin MultiRIP Level 3"
#define RIPCUSTOMERNUMBER       0x0B
#define PRODUCTMENU		"Harlequin MultiRIP"
#define PRODUCTMENUMNEMONIC     "S"
#define PRODUCTABOUT		"About Harlequin MultiRIP"
#define FULLRIPFONTS LINOTYPE
#define MICRORIPFONTS LINOTYPE

#define CAN_SUPPRESS_STARTUP_DIALOGS TRUE
#endif

#endif	/* UNIX */

/*****************************************************************************/

#ifdef	VXWORKS

#ifdef EBDEVAL
/* EBDEVAL */
#define PRODUCTABOUT       "About Embedded Evaluation RIP..."
#define PRODUCTNAME        "EmbeddedEvalRIP"
#define LONGPRODUCTNAME    "Embedded Evaluation RIP"
#define RIPCUSTOMERNUMBER  0x63
#define PRODUCTMENU        PRODUCTNAME
#define OEMTRADEMARK       "Harlequin¨ MultiRIP by " THE_HARLEQUIN_GROUP "."
#define FULLRIPFONTS LINOTYPE
#define MICRORIPFONTS LINOTYPE
#endif

#ifndef PRODUCTNAME
/* i.e. generic */
#define PRODUCTABOUT      "About Harlequin MultiRIP..."
#define LONGPRODUCTNAME    "Harlequin MultiRIP"
#define PRODUCTNAME        "Harlequin MultiRIP"
#define RIPCUSTOMERNUMBER  0x0B
#define PRODUCTMENU       PRODUCTNAME
#define OEMTRADEMARK       "Harlequin¨ MultiRIP by " THE_HARLEQUIN_GROUP "."
#define FULLRIPFONTS LINOTYPE
#define MICRORIPFONTS LINOTYPE

#define START_DLG_LOGO_AT_TOP TRUE
#define CAN_SUPPRESS_STARTUP_DIALOGS TRUE
#endif

#endif	/* VXWORKS */

#ifdef idtr30xx
#endif

#ifdef arm200
#endif


/*
 * Temporarily define stuff for ARM/Threadx port
 * for use during development.
 */
#ifdef	THREADX

#ifndef PRODUCTNAME
/* i.e. generic */
#define PRODUCTABOUT      "About Harlequin MultiRIP..."
#define LONGPRODUCTNAME    "Harlequin MultiRIP"
#define PRODUCTNAME        "Harlequin MultiRIP"
#define RIPCUSTOMERNUMBER  0x0B
#define PRODUCTMENU       PRODUCTNAME
#define OEMTRADEMARK       "Harlequin¨ MultiRIP by " THE_HARLEQUIN_GROUP "."
#define FULLRIPFONTS LINOTYPE
#define MICRORIPFONTS LINOTYPE

#define START_DLG_LOGO_AT_TOP TRUE
#endif

#endif	/* THREADX */

/*
 * Additional font definitions, initially for the benefit of Mac .r files
 * to get around the limitations of rez
 */
#if (FULLRIPFONTS == LINOTYPE) || (MICRORIPFONTS == LINOTYPE)
#define LINOTYPE_FONTS 1
#endif
#if (FULLRIPFONTS == BITSTREAM) || (MICRORIPFONTS == BITSTREAM)
#define BITSTREAM_FONTS 1
#endif

#endif /* ! _customer_h_ */

