/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swcopyf.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Cross-platform sprintf support
 */

#ifndef __SWCOPYF_H__
#define __SWCOPYF_H__

#include <stdarg.h> /* for va_list */
#include "ripcall.h" /* Default definition of RIPCALL */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * \brief Equivalent to vsprintf.
 */
extern void RIPCALL vswcopyf RIPPROTO((
  /*@notnull@*/ /*@out@*/       uint8 *destination ,
  /*@notnull@*/ /*@in@*/        uint8 *format ,
                                va_list ap )) ;
typedef void (RIPCALL *vswcopyf_fp_t) RIPPROTO((uint8 *destination,
                                               uint8 *format ,
                                               va_list ap )) ;

/**
 * \brief Equivalent to sprintf.
 */
extern void RIPCALL swcopyf RIPPROTO((
  /*@notnull@*/ /*@out@*/       uint8 *destination ,
  /*@notnull@*/ /*@in@*/        uint8 *format ,
                                ... )) ;
typedef void (RIPCALL *swcopyf_fp_t) RIPPROTO((uint8 *destination ,
                                              uint8 *format ,
                                              ... )) ;

/**
 * \brief Equivalent to vsnprintf.
 */
extern int32 RIPCALL vswncopyf RIPPROTO((
                /*@out@*/       uint8 *destination,
                                int32 len,
  /*@notnull@*/ /*@in@*/        uint8 *format,
                                va_list ap));
typedef int32 (RIPCALL *vswncopyf_fp_t) RIPPROTO((int8 *destination,
                                                 int32 len, uint8 *format,
                                                 va_list ap));

/**
 * \brief Equivalent to snprintf.
 */
extern int32 RIPCALL swncopyf RIPPROTO((
                /*@out@*/       uint8 *destination,
                                int32 len,
  /*@notnull@*/ /*@in@*/        uint8 *format,
                                ... ));
typedef int32 (RIPCALL *swncopyf_fp_t) RIPPROTO((uint8 *destination,
                                                int32 len, uint8 *format,
                                                ... ));
#ifdef __cplusplus
}
#endif


#endif /* protection for multiple inclusion */
