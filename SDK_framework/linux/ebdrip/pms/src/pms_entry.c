/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_entry.c(EBDSDK_P.1) $
 *
 */
/*! \file
 *  \ingroup PMS
 *  \brief PMS Entry Point.
 */

/**
 * \brief Entry point for PMS Simulator
 *
 * This routine is the main entry for PMS.\n
 */
#include<stdio.h>
#include<stdlib.h>
extern int g_argc;                       
extern char **g_argv;   
extern int PMS_main();


#ifdef VXWORKS
#include <string.h>

int GGSmain(char *szCL)
{
  char *asz[]={"ebdwrapper", "-x", "600", "-e", "bypass", "-v", "on", "-z", "2", "-o", "none", "-s", "9100"};
  char aszArgs[64][512];
  char *paszTmpArgs[64];
  unsigned int nCount, nSrcIndex, nDestIndex, n;

  if(szCL==NULL)
  {
    g_argc=sizeof(asz)/sizeof(asz[0]);
    g_argv=asz;
  }
  else
  {
    /* parse the parameter string */
    g_argc = 0;
    nCount = 0;
    nDestIndex = 0;
    for (nSrcIndex=0; nSrcIndex < strlen(szCL); nSrcIndex++)
    {
      /* spaces mark gaps between parameters */
      if (szCL[nSrcIndex] == ' ')
      {
        if (nDestIndex != 0)
        {
          aszArgs[nCount][nDestIndex++] = 0;
          nCount++;
        }
        nDestIndex = 0;
      }
      else
      {
        aszArgs[nCount][nDestIndex] = szCL[nSrcIndex];
        /* only increment parameter count once, when first char is seen */
        if (nDestIndex == 0)
          g_argc++;
        nDestIndex++;
      }
    }
    for (n=0; n<64; n++)
    {
      paszTmpArgs[n]=aszArgs[n];
    }
    g_argv=&paszTmpArgs[0];
  }
  PMS_main();
  return 0;
}
#else

int main(int argc, char *argv[])
{
  /* store command-line parameters */
  g_argc = argc;
  g_argv = argv;

  PMS_main();

  return 0;
} /*main()*/
#endif

