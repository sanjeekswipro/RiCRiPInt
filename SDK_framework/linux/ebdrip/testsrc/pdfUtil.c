/** \file
 * \ingroup pdfdiff
 *
 * $HopeName: SWprod_hqnrip!testsrc:pdfUtil.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2009-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "pdfUtil.h"

void error(char* message, char* detail)
{
  fprintf(stderr, "%s\n", message);
  if (detail != NULL) {
    fprintf(stderr, "  %s\n", detail);
  }
  /* Still output 3 values. */
  printf("100 99999999 256");
  exit(1);
}

/* See header for doc. */
void scanInit(Scanner* self, FILE* file)
{
  self->file = file;
}

/* See header for doc. */
char* scanString(Scanner* self, Bool mustfind)
{
  int length = 0;
  int c = fgetc(self->file);

  /* Skip leading white-space. */
  while (c != EOF && isspace(c)) {
    c = fgetc(self->file);
  }

  while (c != EOF && ! isspace(c)) {
    self->buffer[length] = (char)c;
    length ++;
    /* Note that we need one character for the null terminator. */
    if (length >= SCAN_BUFFER_SIZE - 1) {
      self->buffer[SCAN_BUFFER_SIZE - 1] = 0;
      /* Do not output the string in the buffer as its likely to
         be binary data. */
      if (mustfind) {
        error("Buffer length exceeded.", NULL);
      } else {
        return NULL;
      }
    }
    c = fgetc(self->file);
  }

  if (c == EOF) {
    if (mustfind) {
      error("Reached end of file before reading a string.", NULL);
    } else {
      return NULL;
    }
  }

  self->buffer[length] = 0;
  return self->buffer;
}

/**
 * Return the next character.
 */
int scanChar(Scanner* self)
{
  return fgetc(self->file);
}

/* See header for doc. */
char* scanName(Scanner* self, Bool mustfind)
{
  int c = fgetc(self->file);

  while (c != EOF && c != '/') {
    c = fgetc(self->file);
  }

  if (c == EOF)
    return NULL;

  ungetc(c, self->file);
  return scanString(self, mustfind);
}

/* See header for doc. */
int scanInt(Scanner* self, Bool mustfind)
{
  char* string = scanString(self, mustfind);
  int value;
  if (sscanf(string, "%d", &value) == EOF) {
    if (mustfind) {
      error("Failed to parse int from string:", string);
    } else {
      value = 0;
    }
  }
  return value;
}

/* See header for doc. */
float scanFloat(Scanner* self, Bool mustfind)
{
  char* string = scanString(self, mustfind);
  float value;

  if (sscanf(string, "%f", &value) == EOF) {
    if (mustfind) {
      error("Failed to parse float from string:", string);
    } else {
      value = 0.0;
    }
  }
  return value;
}

/* See header for doc. */
Bool scanMatch(Scanner* self, char* string)
{
  int c = fgetc(self->file);
  size_t stringLength = strlen(string);
  size_t matchedLength = 0;

  while (c != EOF) {
    if ((char)c == string[matchedLength]) {
      matchedLength ++;
      if (matchedLength >= stringLength) {
        return TRUE;
      }
    }
    else {
      matchedLength = 0;
    }
    c = fgetc(self->file);
  }
  return FALSE;
}

/* See header for doc. */
void scanConsumeSpace(Scanner* self)
{
  int c = fgetc(self->file);
  while (c != EOF && isspace(c)) {
    c = fgetc(self->file);
  }
  if (c != EOF) {
    ungetc(c, self->file);
  }
}

/* Log stripped */

