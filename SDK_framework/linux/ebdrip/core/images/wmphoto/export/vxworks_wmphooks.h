/** \file
 * \ingroup hdphoto
 *
 * $HopeName: COREwmphoto!export:vxworks_wmphooks.h(EBDSDK_P.1) $ 
 *
 * \brief
 * VXworks WMP customisations
 */

/*
 */
//*@@@+++@@@@******************************************************************
//
// VXworks WMP customisations
//
//*@@@---@@@@******************************************************************

#include <stdio.h>

/* Vxworks has EOS defined as end-of-string */
#undef EOS

//================================
// performance measurement support
//================================
// typedef clock_t TIMER_T;

//================================
// bitio functions
//================================
#define PACKETLENGTH (1U<<12)   // 4kB

#define readIS_L1(pSC, pIO) readIS(pSC, pIO)
#define readIS_L2(pSC, pIO) (void)(pSC, pIO)

#define writeIS_L1(pSC, pIO) writeIS(pSC, pIO)
#define writeIS_L2(pSC, pIO) (void)(pSC, pIO)

//================================
// common defines
//================================
#define FORCE_INLINE
#define CDECL
#define UINTPTR_T unsigned int
#define INTPTR_T int
#ifdef highbytefirst
#define _BIG__ENDIAN_ true
#else
#undef _BIG__ENDIAN_
#endif



