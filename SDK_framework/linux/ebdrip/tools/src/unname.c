/* $HopeName: SWtools!src:unname.c(EBDSDK_P.1) $ */
/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
/*
 * UNNAME.C - remove login names from files for issue to customers
 *
 * Format: soft tabs each 4 characters.
 *
* Log stripped */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#if !defined(SEEK_END) && defined(UNIX)
#include <unistd.h>
#endif

/* Define this for safety - CAVEAT: evaluates the least arg. twice */
#ifndef min
#define min(_a,_b)        (((_a) < (_b)) ? (_a) : (_b))
#endif


/*
The code may match more loosely than in this specification, if justified by
 speed constraints.  I can't remember BNF precisely, so here's what I mean:
 
     {}        groups a sequence or selection set into one item
     [thing]    0 or 1 of thing
     thing*    0 or more of thing
     thing+    1 or more of thing
     a|b        either a or b, but not both
    <thing>    an occurrence of the item with the name "thing"

     Only quoted ("quoted") strings are literal.  There is no implicit
     white-space.


# The following patterns are matched.  <logname> will be replaced by "anon".
# <eol> may be stripped.
patterns ::=  hopeStyle | oldStyle

hopeStyle ::=  <str> "Revision " <revnum> <2spc> <newdate> <2spc>
    <logname> <eol>

oldStyle ::= <str> <olddate> <spc> <logname> [<spc> <str>] <eol>

logname ::= <letter> {<letter> | <dig>}*
olddate ::= <year> "-" <mon> "-" <dig> [<dig>] "-" <2dig> ":" <2dig>
newdate ::= <year> "/" <2dig> "/" <2dig> <2spc> <2dig> ":" <2dig> ":" <2dig>
revnum ::= <num> {"." <num>}+
num ::= <dig>+
2spc ::= <spc> <spc>
year ::= {"19" | "20"} <2dig>
2dig ::= <dig> <dig>
str ::= <chr>*
eol ::= {<spc> | <tab>}*
mon ::= "Jan" | "Feb" | "Mar" | "Apr" | "May" | "Jun" | "Jul" | "Aug"
    | "Sep" | "Oct" | "Nov" | "Dec"

# Include the underscore as a "letter".
letter ::= ASCII(65..90) | ASCII(97..122) | ASCII(95)
dig ::= ASCII(48..57)
tab ::= ASCII(9)
spc ::= ASCII(32)
chr ::= ASCII(1..127)


The minimum length of an hopeStyle string is 37 characters.
The minimum length of an oldStyle string is 18 characters.
*/

#define HOPESTYLE_MIN_LEN       37
#define OLDSTYLE_MIN_LEN        18

#define MATCH_MIN_LEN           min(HOPESTYLE_MIN_LEN, OLDSTYLE_MIN_LEN)


#define TRUE                    (1==1)
#define FALSE                   (1==0)


/* The length of our buffer, and the maximum number of chars we'll read.
 * They're not the same since we're using the same buffer for input and
 * output, and we could translate the theoretical minimum login name
 * length of 1 into the four characters of "anon".
 */
#define LINEBUF_LEN             1024
#define LINEBUF_LEN_INPUT       (LINEBUF_LEN - 4 + 1)
    

/* Noted to make reporting of errors more friendly without having
 * to pass parameters all the while - main() points it to aargv[0].
 */
char        *pcOurName;

/* The fake user name we use, with and without \n. */
const char  acFakeUser[] = "anon",
            acFakeUserNL[] = "anon\n";



/* Print up a failure reason, possibly also show the correct command
 * format, then exit with a failure value.
 *
 * pcFilename    A string which can be inserted into the reason
 * pcReason        The reason for failure.  Can optionally contain one '%s',
 *                 wherein pcFilename is substituted.
 * fShowFormat    If TRUE, give a potted usage summary.
 *
 * THIS ROUTINE NEVER RETURNS.
 */
static void
BombOut(char *pcFilename, char *pcReason, int fShowFormat)
{
#define        JMAX_PATH        1024
#define        JMAX_REASON        80
    char        acBuf[JMAX_PATH + JMAX_REASON];

    sprintf(acBuf, pcReason, pcFilename);
    fprintf(stderr, "%s: %s\n", pcOurName, acBuf);
    if (fShowFormat)
    {
        fprintf(stderr,
                "\n"
                "    Format: %s <inputfile> <outputfile>\n\n"
                "    <inputfile> and <outputfile> should not be the same.\n",
                pcOurName);
    }
    exit(1);
}


/* Translate a string given by HopeStyle above.
 *
 * pcIOBuf    Pointer to start of i/o buffer
 * pcBufPtr    Pointer to start of scanned string in pcIOBuf
 *
 * Returns:
 *    0        Failed match
 *    >0        Length of new contents of pcIOBuf after name substitution
 */
static int
TranslateHopeStyle(char *pcIOBuf, char *pcBufPtr)
{
    /* Looking for:
     *
     *    "Revision " <num> { "." <num> }+ <2spc>
     *         <year> "/" <2dig> "/" <2dig> <2spc>
     *        <2dig> ":" <2dig> ":" <2dig> <2spc>
     *        <logname>
     *
     * In all likelihood, once we're past the Revision and have checked
     * the version number's valid, we need only check for the '/' date
     * separators and the ':' time separators to validate the string.
     * I'm assuming that doing isdigit() and range checks on numbers
     * other than in the revision number, in which they're not a fixed
     * length, is probably overkill.
     *
     * pcBufPtr points to the start of "Revision".
     */

    /* Start by skipping the bit we've validated (inc. trainling space) */
    char        *pcPtr = pcBufPtr + 9;

    /* States for the revision number state machine */
    enum
    {
        THS_START = 1,
        THS_MAJ_VER,
        THS_DOT,
        THS_MIN_VER,
        THS_SPACE,
        THS_DONE
    } eState = THS_START;

    /* Now skip the revision number and 2 trailing spaces.   No particular
     * reason for doing it as a state machine other than variety.
     */
    while (eState != THS_DONE)
    {
        char    c = *(pcPtr++);

        if (isdigit(c))
        {
            switch (eState)
            {
                case THS_START:         eState = THS_MAJ_VER;    break;
                case THS_MAJ_VER:       break;
                case THS_DOT:           eState = THS_MIN_VER;    break;
                case THS_MIN_VER:       break;
                default:                return 0;
            }
        }
        else if (c == '.')
        {
            if (eState != THS_MAJ_VER && eState != THS_MIN_VER)
                { return 0; }

            eState = THS_DOT;
        }
        else if (c == ' ')                
        {
            switch (eState)
            {
                case THS_MIN_VER:       eState = THS_SPACE;      break;
                case THS_SPACE:         eState = THS_DONE;       break;
                default:                return 0;
            }
        }
        else
            { return 0;    }
    }
    
    /* We're past the revision number and the two successive spaces.  pcPtr
     * points to the next character.  All spacings are fixed from hereonin.
     *
     *    "yyyy/mm/dd  HH:MM:SS  <logname><eol>"
     *   0123456789 123456789 12
     *
     * We don't bother preserving the <eol> bit.
     */
    
    if (pcPtr[4] != '/'
        || pcPtr[7] != '/'
        || pcPtr[14] != ':'
        || pcPtr[17] != ':')
        { return 0; }

    pcPtr += 22;
    strcpy(pcPtr, (char *)acFakeUserNL);
    return pcPtr + (sizeof(acFakeUserNL) - 1) - pcIOBuf;
}


/* Translate a string given by OldStyle above.
 *
 * pcIOBuf    Pointer to start of i/o buffer
 * pcBufPtr    Pointer to start of scanned string in pcIOBuf
 *
 * Returns:
 *    0        Failed match
 *    >0        Length of new contents of pcIOBuf after name substitution
 */
static int
TranslateOldStyle(char *pcIOBuf, char *pcBufPtr)
{
    /* Looking for:
     *
     *    <year> "-" <mon> "-" <dig>[dig] "-" <2dig> <2dig> <spc> <logname>
     *
     * We should validate the year and month at least, then check for
     * the separator characters.
     *
     * pcBufPtr points to the start of <year>, the first two digits of which
     * have been established as "19" or "20".  The first two '-' characters
     * have been checked, but <mon> has not.
     */

    static
    char        aacMonths[12][4] =
    {
        "Jan",    "Feb",    "Mar",    "Apr",    "May",    "Jun",
        "Jul",    "Aug",    "Sep",    "Oct",    "Nov",    "Dec"
    };

    char        *pcPtr, *pcNameStart, c;
    char        acTrailingBuf[LINEBUF_LEN];
    int            i;

    /* Check last two digits of the year first */
    if (!isdigit(pcBufPtr[2])  ||  !isdigit(pcBufPtr[3]))
        { return 0; }

    /* Now validate the month */
    pcPtr = pcBufPtr + 5;

    for (i = 0; i < 12; i++)
    {
        if (strncmp(pcPtr, &aacMonths[i][0], 3) == 0)
            { break; }
    }

    if (i == 12)
        { return 0; }

    /* Validate the day - one or two digits */
    pcPtr += 4;

    if (!isdigit(*(pcPtr++)))
        { return 0; }

    /* Skip an optional second digit */
    if (isdigit(*pcPtr))
        { pcPtr++; }

    /* Should now be pointing at "-HH:MM " */
    if (*pcPtr != '-'  ||  pcPtr[3] != ':'  ||  pcPtr[6] != ' ')
        { return 0; }

    pcPtr += 7;

    /* We're now looking at the logname.  Now, there may be a trailing
     * string which we should preserve, so wander past the name and stop
     * at the first character not a valid logname character, then copy
     * the rest.
     */
    pcNameStart = pcPtr;

    do
        { c = *(pcPtr++); }
    while (isalnum(c)  ||   c == '_');

    /* Cache the end-of-line stuff, because we're about to overwrite it.
     * Don't forget the first character, which we've overstepped above.
     */
    strcpy((char *)acTrailingBuf, pcPtr-1);

    /* Tack on our fake user name, then the cached remainder of the line */
    strcpy(pcNameStart, (char *)acFakeUser);
    strcat(pcNameStart, (char *)acTrailingBuf);

    return strlen(pcIOBuf);
}


/* Given a string in an IO buffer, perform any prescribed substitutions
 * upon it, and return the result in the same string.
 *
 * pcIOBuf    Pointer to start of i/o buffer
 * iInLen    Value of strlen(pcIOBuf)
 *
 * Return the number of characters to write out, after substitution.
 */
static int
Translate(char *pcIOBuf, int iInLen)
{
    char        *pcBufPtr;
    int            iOutLen;

    /* We do reasonably salutory checks for things that _might_ presage a
     * matched string, then - if weakly matched - turn over the line to a
     * specific routine which confirms the match and returns either <= 0
     * for a failed match, or > 0 for the number of characters in the buffer
     * to be written out to the output file.
     *
     * To save unncessary buffer copies, and because the string formats make
     * it easy, use the same buffer for input and output.
     */
    if (iInLen >= MATCH_MIN_LEN)
    {
        /* We don't need to search beyond pcILimit; if we get to this point
         * in the string, we're not going to match anything.
         */
        char        *pcStrEnd = pcIOBuf + iInLen;
        char        *pcILimit = pcStrEnd - MATCH_MIN_LEN + 1;
        char        c;

        for (pcBufPtr = pcIOBuf;
             pcBufPtr < pcILimit;
             pcBufPtr++)
        {
            c = *pcBufPtr;
            
            /* First iteration: look for "Revision <dig>" */
            if (c == 'R'  &&  pcStrEnd-pcBufPtr >= HOPESTYLE_MIN_LEN)
            {
                if (   strncmp(pcBufPtr+1, "evision ", 8) == 0
                    && isdigit(pcBufPtr[1+8]))
                {
                    iOutLen = TranslateHopeStyle(pcIOBuf, pcBufPtr);

                    if (iOutLen > 0)
                        { return iOutLen; }
                }
            }
            else
            {
                /* Nope, that didn't work: check for a date in old style.
                 * Look for "19??-???-" or "20??-???-" 
                 */
                if (
                        pcStrEnd-pcBufPtr >= OLDSTYLE_MIN_LEN
                        &&  (
                                (c == '1'  &&  pcBufPtr[1] == '9')
                             || (c == '2'  &&  pcBufPtr[1] == '0')
                            )
                        &&  pcBufPtr[4] == '-'
                        &&  pcBufPtr[8] == '-'
                   )
                {
                    iOutLen = TranslateOldStyle(pcIOBuf, pcBufPtr);
                    
                    if (iOutLen > 0)
                        { return iOutLen; }
                }
            }
        }
    }

    /* If we got here, we didn't match anything. The string stands as stet. */
    return iInLen;
}

        
/* Translate from the input file to the output file, removing all those
 * pesky login names.
 *
 * Caveat:  some versions of Unix have a bug wherein the file is considered
 * by fgets() to be terminated at the first NUL character.
 */
static void
UnName(char *pcInFile, char *pcOutFile)
{
    FILE        *pInFile, *pOutFile;
    char        acLine[LINEBUF_LEN];
    int            fHaveWarned = FALSE;
    int            iInLen;
    size_t         iOutLen;
    long        lLogEOF;

    /* Open files in text "t" mode, for MS-DOS mostly. */
    if (strcmp(pcInFile, "-") == 0)
    {
        setbuf(stdin, 0);
        pInFile = stdin;
        pcInFile = "(standard input)";
    }
    else
    {
        pInFile = fopen(pcInFile, "rt");
    }

    if (pInFile == NULL)
        { BombOut(pcInFile, "The input file %s cannot be opened", TRUE); }

    if (strcmp(pcOutFile, "-") == 0)
    {
        setbuf(stdout, 0);
        pOutFile = stdout;
        pcOutFile = "(standard output)";
    }
    else
    {
        pOutFile = fopen(pcOutFile, "wt");
    }

    if (pOutFile == NULL)
    {
        fclose(pInFile);
        BombOut(pcOutFile, "The output file %s cannot be created", TRUE);
    }

    while (!feof(pInFile))
    {
        if (fgets(acLine, LINEBUF_LEN_INPUT, pInFile) == NULL)
            { break; }

        iInLen = strlen(acLine);
        
        if (!fHaveWarned && (iInLen == LINEBUF_LEN_INPUT-1))
        {
            fprintf(stderr,
                    "%s: WARNING: The input file %s contains lines over %d\n"
                    " characters long.  The results may not be correct.\n",
                    pcOurName, pcInFile, LINEBUF_LEN_INPUT-1);
            fHaveWarned = TRUE;
        }
        
        iOutLen = Translate(acLine, iInLen);

        if (fwrite((void *)acLine, 1, iOutLen, pOutFile) != iOutLen)
        {
            fclose(pInFile);
            fclose(pOutFile);
            BombOut(pcOutFile,
                    "Not all the data could be written to %s.\n"
                    " Perhaps the disk is full?",
                    FALSE);
        }
    }

    if (pInFile != stdin)
    {
        /* Check for a UNIXy spurious end-of-file and bomb */
        lLogEOF = ftell(pInFile);
            
        fseek(pInFile, 0L, SEEK_END);
        if (ftell(pInFile) != lLogEOF)
        {
            fprintf(stderr, 
                    "%s: WARNING: The input file %s may contain NUL characters\n"
                    " and have terminated early.  The results may not be correct.\n",
                    pcOurName, pcInFile);
        }
        fclose(pInFile);
    }

    if (pOutFile != stdout)
        { fclose(pOutFile); }
}
        

int
main(int aargc, char **aargv)
{
    /* We expect exactly two arguments:  the input filename and the output
     * filename.  Either can be '-', in which case standard input/output
     * are used.  The two should not be the same file (although it is
     * permissible to use unname as a pass-through filter by making both
     * arguments "-").
     */

    pcOurName = aargv[0];
    
    if (aargc != 3)
        { BombOut(NULL, "Exactly two arguments are expected", TRUE); }

    if ((strcmp(aargv[1], aargv[2]) == 0)  &&  (strcmp(aargv[1], "-") != 0))
        { BombOut(NULL, "Filename arguments were the same", TRUE); }
        
    UnName(aargv[1], aargv[2]);
    
    return 0;
}
        
