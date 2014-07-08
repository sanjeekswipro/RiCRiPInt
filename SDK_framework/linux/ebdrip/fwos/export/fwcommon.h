/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __FWCOMMON_H__
#define __FWCOMMON_H__

/*
 * $HopeName: HQNframework_os!export:fwcommon.h(EBDSDK_P.1) $
 * FrameWork External Common definitions
 * Prefix definitions with "fw" F(rame)W(ork)
 *
 *   Platform       Platform
 *   Independent    Dependent
 * ------------------------------
 * |  fwcommon.h |  fxcommon.h  |
 * |  fw*.h      |  fx*.h       | External Interface
 * |  Fw_*       |  Fw_* Fx_*   |
 * |----------------------------|
 * |  fccommon.h |  fpcommon.h  |
 * |  fc*.h      |  fp*.h       | Internal Interface
 * |  Fc_*       |  Fc_* Fp_*   |
 * ------------------------------
 *
 * FrameWork Nested Include rules:
 *
 * 1) COMMON
 * Each header file ( fw*.h, fx*.h, fc*.h, fp*.h ) should include the
 * corresponding common definitions header file first
 * ( fwcommon.h, fxcommon.h, fccommon.h, fpcommon.h ) first.
 *
 * 2) EXTERNAL
 * Internal Interface header files ( fc*.h, fp*.h ) should include the
 * corresponding External Interface header file ( fw*.h, fx*.h ) next.
 *
 * 3) PLATFORM DEPENDENT
 * Platform Independent header files ( fw*.h, fc*.h ) should include the
 * corresponding Platform Dependent header file ( fx*.h, fp*.h ) next.
 *
 * std.h is included by fxcommon.h, which all files will include eventually.
 *
 * To avoid problems with include ordering:
 * a/ .c files should not include the platform dependent headers,
 *    ( fx*.h, fp*.h ) directly, but include them via the platform independent
 *    headers ( fw*.h, fc*.h )
 * b/ The platform dependent headers ( fx*.h, fp*.h ) should not include
 *    their corresponding platform independent header.
 */

/*
* Log stripped */


/* ----------------------- Includes ---------------------------------------- */

                        /* Is Common */
                        /* Is External */
#include "fxcommon.h"   /* Platform Dependent */


/* ----------------------- Deprecation ------------------------------------- */

/* The deprecation mechanism aims to cause failure at compile time when
 * functions which are unsafe, ( typically because of globalization issues ),
 * are used. For all of these a FrameWork replacement is provided. Access to
 * the deprecated functions can be obtained by defining the appropriate
 * FW<module>_ALLOW_DEPRECATED macro in the .c file ( never .h file ) that
 * requires the deprecated function. The only circumstances in which this is
 * allowed are:
 * 1) In implementing the FrameWork replacement functions, since these are
 *    assumed to understand the restrictions, and will be kept up to date
 *    with changes to these restrictions.
 * 2) Before the appropriate FrameWork module implementing the replacement
 *    has been booted.
 * 3) In order to implement a low level debugging mechanism which we dont want
 *    to reenter arbitrary FrameWork code.
 * In particular this does not include FrameWork clients claiming special
 * knowledge about when the deprecated functions are safe, since these
 * assumptions may become out of date.
 */

#ifndef FW_ALLOW_DEPRECATED     /* Master switch */

/* We need to include the original definitions first.
 * If that depends on the platform the include should be put in fxcommon.h.
 * This relies on the header files having multiple include protection.
 * If that ever is a problem just turn off the deprecation mechanism by
 * defining FW_ALLOW_DEPRECATED for that platform.
 */
#include <stdio.h>

/* Deprecate by converting to reference to non_existent variable */
#define FW_DEPRECATE( old, new ) \
 (* old##_deprecated_use_##new)

/***********
* FwString *
***********/

#ifndef FWSTR_ALLOW_DEPRECATED

/* The *printf and *scanf families of functions are generally
 * deprecated because thay take into account the locale, which is
 * probably not what is intended, or desirable given the FrameWork
 * model of translation at display time. However we dont have a
 * FrameWork replacement for sscanf yet.
 */

 /* printf, vprintf are allowed since their output is user visible. */
#ifdef sprintf
#undef sprintf
#endif
#define sprintf         FW_DEPRECATE( sprintf, FwStrPrintf )
#ifdef vsprintf
#undef vsprintf
#endif
#define vsprintf        FW_DEPRECATE( vsprintf, FwStrVPrintf )

 /* atof and strtod are deprecated because the programmer might
  * incorrectly make assumptions about the locale in which these
  * functions operate. In fact it is platform dependent - MacOS
  * uses the C locale for them, and others use the current locale.
  * FwStrToD will always use the C-locale.
  */
#define atof            FW_DEPRECATE( atof, FwStrToD )
#define strtod          FW_DEPRECATE( strtod, FwStrToD )

#endif /* ! FWSTR_ALLOW_DEPRECATED */


/*********
* FwFile *
*********/

#ifndef FWFILE_ALLOW_DEPRECATED

/* The posix file functions are deprecated since they may not handle
 * internationalised filenames. See fwstring.h for details of this problem
 */
#define rename          FW_DEPRECATE( rename, FwFileRename )
/* remove and unlink are not deprecated for C++ since CORBA object
 * methods of the same names exist.
 */
#ifndef __cplusplus
#define remove          FW_DEPRECATE( remove, FwFileDelete )
#define unlink          FW_DEPRECATE( unlink, FwFileDelete )
#endif

#endif /* ! FWFILE_ALLOW_DEPRECATED */


/***********
* FwStream *
***********/

#ifndef FWSTREAM_ALLOW_DEPRECATED

#define fopen           FW_DEPRECATE( fopen, FwStreams )
#define fclose          FW_DEPRECATE( fclose, FwStreams )
#define fflush          FW_DEPRECATE( fflush, FwStreams )

#define fprintf         FW_DEPRECATE( fprintf, FwStrPrintf )
#define fscanf          FW_DEPRECATE( fscanf, FwStreams )

#endif /* ! FWSTREAM_ALLOW_DEPRECATED */


#endif /* ! FW_ALLOW_DEPRECATED */


#endif /* ! __FWCOMMON_H__ */

/* eof fwcommon.h */
