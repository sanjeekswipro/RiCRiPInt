/* $HopeName: SWcustomer!src:custlib.c(EBDSDK_P.1) $ */
/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/* $Id: src:custlib.c,v 1.45.1.1.1.1 2013/12/19 11:26:07 anon Exp $
  This file provides a functional interface to customer details. Source files
  which do not *need* to macro-expand the customer details should include this
  header rather than customer.h.

Log stripped */


/* ------- Includes -------- */

#include "custlib.h"  /* includes std.h */

#include "product.h"

#include "security.h"

/*
 * Eek!  customer.h uses the THE_HARLEQUIN_GROUP #define, which is
 * defined in copyrite.h, but the include paths don't work the same for
 * C source as they do for resources, so we can't include copyrite.h in
 * there.  Blurgh!
 */
#include "copyrite.h" /* THE_HARLEQUIN_GROUP */
#include "customer.h"

#if defined(MORISAWA_ENABLED) && ! defined(MORISAWA_CUSTOMER_NO)
#error "Morisawa was enabled, but there is no Morisawa Customer No"
#endif
#if ! defined(MORISAWA_ENABLED) && defined(MORISAWA_CUSTOMER_NO)
#error "A Morisawa Customer No has been defined, but Morisawa has not been enabled"
#endif


/* -------- Data -------------------- */

typedef struct HCMSType
{
  uint8 *       atbzType;
  uint32        nType;
} HCMSType;


static HCMSType aHCMSTypes[] =
{
  { HCMS_TYPE_ICC,     HCMS_NTYPE_ICC },
  { HCMS_TYPE_LITE,    HCMS_NTYPE_LITE },
  { HCMS_TYPE_HCP,     HCMS_NTYPE_HCP },
  { HCMS_TYPE_FULL,    HCMS_NTYPE_FULL }
};


/* -------- Macros -------- */

/* Definition of RIP's version number */

#ifdef CORESKIN

#define CUST_RIP_VERSION_NUMBER   20

#else /* !CORESKIN */

#ifdef CORERIP

#define CUST_RIP_VERSION_NUMBER   20

#endif /* CORERIP */

#endif /* CORESKIN */

#ifndef CUST_RIP_VERSION_NUMBER
  "This must be defined"
#endif


/* -------- Functions -------- */

int32 CustomerOptionalStartupDlg(void)
{
#ifdef OPTIONAL_STARTUPDLG
  return TRUE;
#else
  return FALSE;
#endif
}

int32 CustomerOptionalCopyrightDlg(void)
{
#ifdef OPTIONAL_COPYRIGHTDLG
  return TRUE;
#else
  return FALSE;
#endif
}

uint32 CustomerHasLinotypeFonts(void) {
#if LINOTYPE_FONTS == 1
  return 1;
#else
  return 0;
#endif
}

uint32 CustomerHasBitstreamFonts(void) {
#if BITSTREAM_FONTS == 1
  return 1;
#else
  return 0;
#endif
}

int32 CustomerRipVersionNumber(void)
{
  return CUST_RIP_VERSION_NUMBER;
}    

uint8 *CustomerProductName(void)
{
  static char pn[] = PRODUCTNAME ;
                      
  /* CustomerProductName returns a static array of characters containing the
     product name. The string is reset using the following strcpy() each time
     this function is called, because the string may be visible from the
     PostScript world, and so may be altered inside a superexec context */

  strcpy(pn, PRODUCTNAME) ;
  return (uint8 *)pn ;
}

uint8 *CustomerLongProductName(void)
{
  static char lpn[] = LONGPRODUCTNAME ;

  /* CustomerLongProductName returns a static array of characters containing 
     the long product name. The string is reset using the following strcpy()
     each time this function is called, because the string may be visible from
     the PostScript world, and so may be altered inside a superexec context */

  strcpy(lpn, LONGPRODUCTNAME) ;
  return (uint8 *)lpn ;
}

uint8 *CustomerCustomisationName(void)
{
  static char cn[] = CUSTOMISATION_NAME ;
                      
  /* CustomerCustomisationName returns a static array of characters containing the
     customisation name, $(CUSTOMER) in the old build system and $(Variant_customised)
     in JAM. The string is reset using the following strcpy() each time
     this function is called, because the string may be visible from the
     PostScript world, and so may be altered inside a superexec context */

  strcpy(cn, CUSTOMISATION_NAME) ;
  return (uint8 *)cn ;
}

char *CustomerCopyrightOemMessage(void) {
#ifdef OEMTRADEMARK
  return OEMTRADEMARK;
#else
  return "ScriptWorks by Harlequin Limited.";
#endif
}

char **CustomerCopyrightOemAdditional(void) {
#ifdef COPYRIGHTOEMADDITIONAL
  static char *copyrightOemAdditional[] = COPYRIGHTOEMADDITIONAL;
  return copyrightOemAdditional;
#else
  return NULL;
#endif
}

uint8 *ProductNameParam(void)
{
  /* ProductNameParam returns a static array of characters containing the
     product name, or the Morisawa product name if (and only if) there is one.
     The string is reset using the following strcpy() each time
     this function is called, because the string may be visible from the
     PostScript world, and so may be altered inside a superexec context */

#ifdef MORISAWA_PRODUCTNAME
  static char pn[] = MORISAWA_PRODUCTNAME ;
  strcpy(pn, MORISAWA_PRODUCTNAME) ;
#else
  static char pn[] = PRODUCTNAME ;
  strcpy(pn, PRODUCTNAME) ;
#endif  /* MORISAWA_PRODUCTNAME */

  return (uint8 *)pn ;
}

STATIC uint32 g_RipCustomerNumber = RIPCUSTOMERNUMBER;

void SetRipCustomerNumber(uint32 NewRipCustomerNumber)
{
  g_RipCustomerNumber = NewRipCustomerNumber;
}
  
int32 RipCustomerNumber(void)
{
  return g_RipCustomerNumber ;
}

uint8 * CustomerCMSName(void)
{
  return CMSNAME;
}

uint8 * CustomerSecurityCMSName(void)
{
  return SECURITYCMSNAME;
}

uint8 * CustomerLongCMSName(void)
{
  return LONGCMSNAME;
}

uint8 * CustomerProductMenu(void)
{
#ifdef MACOSX
  static char cpm[] = "File" ;
#else
  static char cpm[] = PRODUCTMENU ;
#endif
  return (uint8 *)cpm ;
}

uint8 * CustomerProductAbout(void)
{
  static char cpa[256] ;
  cpa[0] = '\0' ;
  
  if (strlen (PRODUCTABOUT)>0){
    /* Comment giving translation template:- UVM( "A,,About %s" ) */
    /* This template assumes every PRODUCTABOUT string begins with A */
    /* More templates will need adding if this is ever not the case */
    cpa [0] = * PRODUCTABOUT ;
    cpa [1] = ',' ;
    cpa [2] = ',' ;
    strcpy (&cpa[3], PRODUCTABOUT) ;
  }
  HQASSERT (strlen(cpa) < 256, "The Product About String is too long.") ;
  return (uint8 *)cpa ;
}

uint8 * CustomerProductMenuMnemonic(void)
{
#ifdef PRODUCTMENUMNEMONIC
  static char pmm[] = PRODUCTMENUMNEMONIC ;
  return (uint8 *)pmm ;
#else
  return NULL ;
#endif
}

int32 CustomerLookupHCMSType( uint8 * ptbzType )
{
  int32 nType;

  for ( nType = HCMS_NTYPE_TOTAL - 1; nType >= 0; nType-- )
    if ( strcmp((char *)ptbzType, (char *)aHCMSTypes[ nType ].atbzType ) == 0)
      break;

  return nType;
}


uint8 * CustomerHCMSTypeName( int32 nType )
{
  if ( nType < 0 || nType >= HCMS_NTYPE_TOTAL )
  {
    HQFAIL( "bad HCMS type" );
    return NULL;
  }

  return aHCMSTypes[ nType ].atbzType;
}


int32 WatermarkLicenseDuration(void)
{
#ifndef WM_LICENSE_DURATION
#define WM_LICENSE_DURATION  30
#endif

  return WM_LICENSE_DURATION;
}


/* Macintosh only: OEMTRADEMARK, USUALSIZE */



