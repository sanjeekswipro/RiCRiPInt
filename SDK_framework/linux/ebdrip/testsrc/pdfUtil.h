/** \file
 * \ingroup testsrc
 *
 * $HopeName: SWprod_hqnrip!testsrc:pdfUtil.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief Utility methods for working with PDFs.
 */
#ifndef _pdfUtil_h_
#define _pdfUtil_h_

#include <stdio.h>

#define min(_a, _b) (((_a) < (_b)) ? (_a) : (_b))
#define max(_a, _b) (((_a) > (_b)) ? (_a) : (_b))

typedef int Bool;
typedef unsigned char uint8;

enum {
  FALSE = 0,
  TRUE = 1
};

#define SCAN_BUFFER_SIZE 1024

typedef struct {
  FILE* file;
  char buffer[SCAN_BUFFER_SIZE];
} Scanner;

/**
 * Print the passed message and optional detail, then exit the application.
 */
void error(char* message, char* detail);

/**
 * Initialise the passed scanner to read from file. Reading will start at the
 * current position.
 */
void scanInit(Scanner* self, FILE* file);

/**
 * Read a white space delimited string. Any leading white space will be ignored.
 *
 * If mustfind is TRUE, will cause an error if the string is not found.
 * If mustfind is FALSE, will return NULL if the string is not found.
 */
char* scanString(Scanner* self, Bool mustfind);

/**
 * Read the next character.
 *
 * @return The next character, exactly as returned by fgetc().
 */
int scanChar(Scanner* self);

/**
 * Return the next name, including the leading '/' character.
 *
 * If mustfind is TRUE, will cause an error if the name is not found.
 * If mustfind is FALSE, will return NULL if the name is not found.
 *
 * @return The next name, or NULL if EOF is read. The returned string is only
 *         valid until the next call to this method.
 */
char* scanName(Scanner* self, Bool mustfind);

/**
 * Read the next string and parse it as an integer.
 *
 * If mustfind is TRUE, will cause an error if an int is not found.
 * If mustfind is FALSE, will return 0 if the int is not found.
 */
int scanInt(Scanner* self, Bool mustfind);

/**
 * Read the next string and parse it as an integer.
 *
 * If mustfind is TRUE, will cause an error if a float is not found.
 * If mustfind is FALSE, will return 0 if the float is not found.
 */
float scanFloat(Scanner* self, Bool mustfind);

/**
 * Consume input until the specified string, or EOF, is matched.
 *
 * @return TRUE if 'string' was matched before EOF was reached, otherwise FALSE.
 */
Bool scanMatch(Scanner* self, char* string);

/**
 * Consume all continuous white space.
 */
void scanConsumeSpace(Scanner* self);

#endif

/* Log stripped */

