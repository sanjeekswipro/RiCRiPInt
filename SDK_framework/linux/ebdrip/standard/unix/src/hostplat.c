/** \file
 * \ingroup platform
 *
 * $HopeName: HQNc-standard!unix:src:hostplat.c(EBDSDK_P.1) $ 
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Host platform detection code for Unix
 */

#include <sys/types.h>
#include <sys/utsname.h>
#if !defined(linux) && !defined(__NetBSD__)
#include <sys/systeminfo.h>
#endif
#ifdef SGI
#include <invent.h>
#endif
#include <string.h>
#include "hqosarch.h"

#include <stdio.h>

void
host_platform(char *result)
{
        struct utsname uts;
        char arch[OSARCH_NAMESIZE];
        char *rel = uts.release;
        char *cp;

        if (uname(&uts) == -1) {
          strcpy(result, "unix-unknown");
          return;
        }

	/* IRIX */
        if (strncmp(uts.sysname, "IRIX", 4) == 0) {
          strcpy(result, "irix");

	/* SunOS */
        } else if ((strcmp(uts.sysname, "SunOS") == 0) ||
                   (strcmp(uts.sysname, "sunos") == 0)) {
          if (strncmp(rel, "5", 1) == 0) {
            strcpy(result, "solaris_2");
            rel += 1 + (rel[1] == '.');
          } else if (strncmp(rel, "6", 1) == 0) {
            strcpy(result, "solaris_3");
            rel += 1 + (rel[1] == '.');
          } else {
            strcpy(result, "sunos");
          }

        /* Linux */
        } else if ((strcmp(uts.sysname, "Linux") == 0) ||
                   (strcmp(uts.sysname, "linux") == 0)) {
          strcpy(result, "linux");

        /* NetBSD */
        } else if (strcmp(uts.sysname, "NetBSD") == 0) {
          strcpy(result, "netbsd");

        /* Unknown */
        } else {
          strcpy(result, "unknown");
        }

        cp = result + strlen(result);

        for (*cp++ = '_'; (*cp = *rel); cp++, rel++) {
          if (*cp == '.' || *cp == '-') {
            *cp = '_';
          }
        }

        *cp++ = '-';
        *cp = '\0';

#if defined(linux) || defined(__NetBSD__)
        {
          char *mac = uts.machine;
          if (strlen(mac) == 4 && mac[0] == 'i' /* ix86 */
              && mac[2] == '8' && mac[3] == '6') {
            int family = mac[1] - '0';
            if (family < 5)
              strcpy(arch, mac+1); /* eg 386, 486 */
            else if (family == 5)
              strcpy(arch, "pentium");
            else
              sprintf(arch, "pentium_%d", family-4);
          } else if (strcmp(uts.machine, "x86_64") == 0) {
            sprintf(arch, "amd64");
          } else {
            strcpy(arch, mac);
          }
        }
#else
        {
          char processor[OSARCH_NAMESIZE];
          if (sysinfo(SI_ARCHITECTURE, processor, sizeof(processor)) == -1) {
            strcpy(arch, "unknown");
          } else {
            strcpy(arch, processor);
          }
        }
#endif

        strcat(result, arch);
}

/*
* Log stripped */
