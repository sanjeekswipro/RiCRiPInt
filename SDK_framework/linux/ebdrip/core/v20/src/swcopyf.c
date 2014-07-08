/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:swcopyf.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Erstatz sprintf for ScriptWorks internal use
 *
 * Extended to support snprintf like functionality.
 */

#include <stdarg.h>

#include "core.h"
#include "swctype.h"

#include "swcopyf.h"
#include "swstart.h"

#define ALTERNATE               1
#define LEFTJUSTIFY             2
#define FORCESIGN               4
#define SIGNSPACE               8
#define PADZERO                16
#define ESCAPECHARS            32

#define SHORT                   1
#define NORMAL                  2
#define LONG                    3
#define POINTER                 4

#define BINARY                  1
#define CLEAN7BIT               2
#define CLEAN8BIT               3

#define TEMP_BUFFER_SIZE        34

static void cvt_to_string( uint8 *str, uintptr_t val, int32 mindigits, int32 base, int32 fUpper)
{
    uint8 temp[TEMP_BUFFER_SIZE];  /* 64 bits in octal - should be enough */
    int count;
    char *pszDigits;

    static char szDigitsLower[] = "0123456789abcdef";
    static char szDigitsUpper[] = "0123456789ABCDEF";

    if(fUpper)
      pszDigits = szDigitsUpper;
    else
      pszDigits = szDigitsLower;

    count = sizeof(temp);
    temp[--count] = '\0';
    do
    {
        temp[--count] = pszDigits[val % base];
        val /= base;
        mindigits --;
    }
    while ((count > 0) && (val > 0 || mindigits > 0));
    ( void )strcpy( (char *)str, (char *)(temp + count) );
}

#define WRITE_CHAR(_dest, _ch, _n) \
MACRO_START \
  if ( --(_n) > 0 ) { \
    *(_dest)++ = (_ch); \
  } \
MACRO_END

static int32 vswncopyf_internal(
    uint8*  destination,
    int32   n,
    uint8*  format,
    va_list ap)
{
    uint8 *str;
    int32 i;
    intptr_t val;
    int32 count;
    int32 precision;
    int32 fieldwidth;
    int32 flags;
    int32 done;
    int32 size;
    int32 length;
    int32 write_nul;
    int32 chars;
    int32 escape = 0;
    uint8 buffer[TEMP_BUFFER_SIZE];

    HQASSERT(((destination != NULL) || (n == 0)),
             "vswncopyf_internal: destination ptr NULL");

    HQASSERT(sizeof(int32) <= sizeof(intptr_t),
             "vswncopyf_internal: int32 is larger than intptr_t");

    write_nul = n > 0;
    chars = 0;

    while ( (char)*format != '\0')
    {
        if ( (char)*format == '%')
        {
            format ++;

            done = FALSE;
            flags = 0;
            do
            {
                switch (*format++)
                {
                  case ' ':
                    flags |= SIGNSPACE;
                    break;

                  case '+':
                    flags |= FORCESIGN;
                    break;

                  case '-':
                    flags |= LEFTJUSTIFY;
                    break;

                  case '#':
                    flags |= ALTERNATE;
                    break;

                  case '0':
                    flags |= PADZERO;
                    break;

                  default:
                    format --;
                    done = TRUE;
                    break;
                }
            }
            while (!done);

            if ( (char)*format == '*')
            {
                format ++;
                fieldwidth = va_arg(ap, int32);
                if (fieldwidth < 0)
                {
                    fieldwidth = - fieldwidth;
                    flags |= LEFTJUSTIFY;
                }
            }
            else
            {
                for (fieldwidth = 0; isdigit( ((int32)(uint8)*format) ); format ++)
                {
                    fieldwidth = fieldwidth * 10 + (char)*format - '0';
                }
            }

            if ( (char)*format == '.')
            {
                format ++;
                if ( (char)*format == '*')
                {
                    precision = va_arg(ap, int32);
                    format++;
                }
                else
                {
                    for (precision = 0; isdigit( ((int32)(uint8)*format) ); format ++)
                    {
                        precision = precision * 10 + (char)*format - '0';
                    }
                }
            }
            else precision = -1;

            if ( (char)*format == 'h')
            {
                size = SHORT;
                format++;
            }
            else if ( (char)*format == 'l')
            {
                size = LONG;
                format++;
            }
            else if ( (char)*format == 'P')
            {
                size = POINTER;
                format++;
            }
            else
            {
                size = NORMAL;

                /* Check for escaping output */
                if ( (char)*format == '!' ) {
                    flags |= ESCAPECHARS;
                    format++;

                    escape = BINARY;
                    if ( (char)*format == '7' ) {
                      escape = CLEAN7BIT;
                      format++;

                    } else if ( (char)*format == '8') {
                      escape = CLEAN8BIT;
                      format++;
                    }
                }
            }

            switch (*format)
            {
              case 's':
                str = va_arg(ap, uint8 *);

                if ( !(flags & ESCAPECHARS) || (precision == -1) ) {
                  if ( precision == -1 )
                    precision = MAXINT;

                  /* set length to the minimum of strlen(str)
                     and precision, avoiding possible problems
                     if str is not zero terminated - so much for
                     re-using code from the Standard Library book.
                     Note: the only way to include NULs in escaped
                           strings is to specify a data length */
                  for (length = 0;
                       length < precision && (int32)str[length] != '\0' ;
                       length++)
                      /* empty */;

                } else
                  length = precision;
                break;

              case 'c':
                buffer[0] = (uint8) va_arg(ap, int);
                str = buffer;
                length = 1;
                break;


              case 'o':
              case 'X':
              case 'x':
                if (size == SHORT)
                {
                    val = (unsigned short)va_arg(ap, int);
                }
                else if (size == LONG)
                {
                    val = (int32) va_arg(ap, long);
                }
                else if (size == POINTER)
                {
                    val = (intptr_t) va_arg(ap, void *);
                }
                else
                {
                    val = (unsigned int)va_arg(ap, int);
                }
                if (flags & ALTERNATE)
                {
                    buffer[0] = '0';
                    if ( (char)*format == 'X' || (char)*format == 'x')
                    {
                        buffer[1] = *format;
                        str = buffer + 2;
                    }
                    else
                    {
                        str = buffer + 1;
                    }
                }
                else
                    str = buffer;
                cvt_to_string(str, val, precision < 0 ? 0 : precision,
                              (( (char)*format == 'x') || ( (char)*format == 'X')) ? 16 : 8,
                              (char)*format == 'X');
                length = strlen_int32( (char *)buffer);
                str = buffer;
                break;

              case 'p':
                buffer[0] = '0';
                buffer[1] = 'x';
                str = buffer + 2;
                val = (intptr_t)va_arg(ap, void *);
                cvt_to_string(str, val, precision < 0 ? 0 : precision , 16 , FALSE);
                length = strlen_int32( (char *)buffer);
                str = buffer;
                break;

              case 'i':
              case 'd':
              case 'u':
                if (size == SHORT)
                {
                    val = (short)va_arg(ap, int);
                }
                else if (size == LONG)
                {
                    val = (int32)va_arg(ap, long);
                }
                else if (size == POINTER)
                {
                    val = (intptr_t)va_arg(ap, void *);
                }
                else
                {
                    val = (int)va_arg(ap, int);
                }
                if ((val < 0) && ((char)*format != 'u'))
                {
                    val = -val;
                    buffer[0] = '-';
                    str = buffer + 1;
                }
                else if (flags & SIGNSPACE)
                {
                    buffer[0] = ' ';
                    str = buffer + 1;
                }
                else if (flags & FORCESIGN)
                {
                    buffer[0] = '+';
                    str = buffer + 1;
                }
                else
                    str = buffer;
                cvt_to_string(str, val, precision < 0 ? 0 : precision , 10 , FALSE);
                length = strlen_int32( (char *)buffer);
                str = buffer;
                break;

              case 'e':
              case 'f':
              case 'g':
              case 'E':
              case 'F':
              case 'G':
                /* FOR ALL PLATFORMS: use the promoted_real type,
                 * defined in platform.h.
                 */
                str = cvt_real(va_arg(ap, promoted_real), *format, precision);
                length = strlen_int32( (char *)str);
                break;

              case 'N':
                /* ScriptWorks extension.
                 * Used to force skipping the writing of the terminating NUL, used
                 * when wanting to write to a PS string */
                write_nul = FALSE;

                length = 0;     /* don't actually output anything */
                fieldwidth = 0; /* ensures no padding blanks are output if specified */
                str = NULL;     /* keeps the compiler happy */
                break;

              default:
                buffer[0] = *format;
                str = buffer;
                length = 1;
                break;
            }

            format++;
            /* Now 'str' points at what to insert, 'length' is the number of
               characters to copy */
            if (! (flags & LEFTJUSTIFY))
            {
                for (count = 0; count < fieldwidth - length; count ++) {
                  WRITE_CHAR(destination, (uint8)((flags & PADZERO) ? '0' : ' '), n);
                  chars++;
                }
            }
            for (count = 0; count < length; count ++)
            {
                if ( flags & ESCAPECHARS ) {
                    switch ( *str ) {
                    case '(':
                    case ')':
                    case '\\':
                        /* These chars should be escaped always */
                        WRITE_CHAR(destination, (uint8)'\\', n);
                        chars++;
                        break;

                    default:
                        if ( (escape != BINARY) &&
                             ((*str < 0x1B) ||
                              ((escape == CLEAN7BIT) && (*str & 0x80))) ) {
                          /* Doing 7 or 8 bit clean with dirty char - convert to octal */
                          WRITE_CHAR(destination, (uint8)'\\', n);
                          chars++;
                          cvt_to_string(buffer, *str, 3, 8, FALSE);
                          for ( i = 0; i < 3; i++ ) {
                              WRITE_CHAR(destination, (uint8)buffer[i], n);
                              chars++;
                          }
                          /* move on to next character in format string */
                          str++;
                          continue;
                        }
                    }
                }
                WRITE_CHAR(destination, *str, n);
                str++;
                chars++;
            }
            if (flags & LEFTJUSTIFY)
            {
                for (count = 0; count < fieldwidth - length; count ++) {
                    WRITE_CHAR(destination, (uint8)((flags & PADZERO) ? '0' : ' '), n);
                    chars++;
                }
            }
        }
        else {
            WRITE_CHAR(destination, *format, n);
            format++;
            chars++;
        }
    }
    /* Write the terminating NUL - not included in count returned */
    if ( write_nul ) {
        *destination = '\0';
    }

    return(chars);
}

void RIPCALL vswcopyf( uint8 *destination, uint8 *format, va_list ap)
{
    /* Setting an output limit of MAXINT should be enough ;-) and since we did
     * not go as far as returning the number of characters written as per
     * printf() ignore the returned value */
    (void)vswncopyf_internal(destination, MAXINT, format, ap);
}

void swcopyf(uint8 *destination, uint8 *format, ...)
{
    va_list ap;

    va_start(ap, format);

    vswcopyf(destination, format, ap);
    va_end(ap);
}

int32 RIPCALL vswncopyf(
    uint8*  destination,
    int32   n,
    uint8*  format,
    va_list ap)
{
    return(vswncopyf_internal(destination, n, format, ap));
}

int32 swncopyf(
    uint8*  destination,
    int32   len,
    uint8*  format,
    ...)
{
    int32 n;
    va_list ap;

    va_start(ap, format);
    n = vswncopyf(destination, len, format, ap);
    va_end(ap);

    return(n);
}

/* end of sw/v20 swcopyf.c */

/* Log stripped */
