/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/*
 * $HopeName: SWtools!src:psspp.c(EBDSDK_P.1) $
 *
 * Author:      Derek Knight
 * Date:        10 March 1993
 *
 * Purpose:     Create an Include dependency list for the specified file(s)
 *
 *      Usage PSSPP
 *      {-D<define name>}      - The name of a #define variable
 *      {-U<define name>}      - The name of a #undef variable
 *      {-I<dir>;<dir>}        - The name of extra include dirs
 *      {-?}                   - Display this help
 *      <wildcard file>        - Source files to scan
 */

/*
* Log stripped */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(WIN32)
#include <dos.h>
#include <ctype.h>
#include <io.h>
#define DIR_SEP '\\'
#else
#define DIR_SEP '/'
#endif

#include "cparse.h"
extern FILE *OutChannel;

/* #define DEBUG */

int main (int argc, char **argv)
{
        int i;
        int num_files_found = 0;

        ResetIfdefs ();
    AddSystemIncludes ();
    DefineExpansion ();

#ifdef MACINTOSH
        AddIncludeDirs("", FALSE);
#else
        AddIncludeDirs(".", FALSE);
#endif

        if (argc)
        {
                int i, start;
                char c;

                for (i = 1; i < argc; i++)
                {
                        if (argv[i][0] == '-'
#ifndef UNIX
                                || argv[i][0] == '/'
#endif
                        )
                        {

                                /* Want to allow "-Dsymbol" as well as "-D symbol"
                                   Also, want to be able to assign a value to the
                                   symbol other than "1" */

                                c = argv [i][1];        /* Assume "-Dsymbol" format */
                                start = 2;

                                if ((argv [i][2] == ' ') || (argv[i][2] == '\0'))       /* Separated args, take the next arg */
                                        {
                                        start = 0;
                                        i++;
                                        }

                                switch (c)
                                {
                                        char *str;


                                        case 'I':
                                        case 'i':
                                            AddIncludeDirs (&(argv [i][start]), TRUE);
                                            break;
                                        case 'D':
                                        case 'd':
                                                if ((str = strchr(&(argv[i][start]),  '=')) != NULL)
                                                        {
                                                        int pos;

                                                        str++;
                                                        pos = strcspn(&(argv[i][start]),  "=");
                                                        argv[i][pos] = '\0';
                                                        }
                                                else
                                                        str = "1";
                                                SetDefine (&(argv [i][start]), str, TRUE, TRUE, E_ExpandPS);
                                                break;
                                        case 'U':
                                        case 'u':
                                                UnsetDefine (&(argv [i][start]));
                                                break;
                                        case '?':
                                                printf ("PSPPP: PSS file pre-processor\n"
                                                                "Derek Knight. 1993\n\n"
                                                                "Usage %s\n"
                                                                " {-D<define name>}      - The name of a #define variable\n"
                                                                " {-U<define name>}      - The name of a #undef variable\n"
                                                                " {-I<dir>;<dir>}        - The name of extra include dirs\n"
                                                                " {-?}                   - Display this help\n"
                                                                " <wildcard file>        - Postscript files to scan", argv[0]);
                                                return 1;
                                        default:
                                                printf ("Use -? option for help");
                                                return 1;
                                }
                        }
                }
        }

        for (i = 1; i < argc; i++)
        {
                /* Check to see if argument is a flag (or a flag's argument)
                        Need to check if current argument starts with a flag or
                        if previous argument was the flag only
                        i.e. -DARGUMENT vs. -D ARGUMENT */

                if (argv [i][0] != '\\' && argv [i][0] != '-')                  /* Make sure no preceding - or \ */
                  if ((argv [i-1][0] != '-') || (argv[i-1][2] != '\0')) /* No preceding -x expression */
                {
                        int     result;

                        num_files_found++;

#if defined(WIN32) || defined(IBMPC)
                        result = ParseFiles( argv[i], NULL, NULL, E_ExpandPS );
#else
                        OutChannel = stdout;
                        result = ParseFile( argv[i], E_ExpandPS );
#endif
                        if ( result <= 0 )
                        {
                                if ( result == 0 )
                                        fprintf (stderr, "Can't find %s", argv[i] );
                                return 1;
                        }
                }
        }

        /*
         * If no files were found on the command line, we shall become a simple
         * filter.  This hack is to get around the evil fact that we normally
         * send our output to different destinations, depending on whether we
         * have been compiled for the PC or mac.  Ugh!  I'm glad I used 'ed' to
         * hack this or I would have seen more nastiness than I could have coped
         * with!
         */

        if (num_files_found == 0) {
                OutChannel = stdout;
                if (0 >= ParseFile(NULL, E_ExpandPS))
                        return 1;
        }

        FreeIncludeDirs ();
        FreeDefines (TRUE);
        FreeMacros (TRUE);

        return 0;
}

/* eof psspp.c */
