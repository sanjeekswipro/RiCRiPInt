#ifndef __LINUX_WMP_H__
#define __LINUX_WMP_H__ (1)

/** \file
 * \ingroup hdphoto
 *
 * $HopeName: COREwmphoto!src:linux_wmp.h(EBDSDK_P.1) $ 
 *
 * \brief
 * MicroSoft WMPhoto Linux
 */


//================================
// performance measurement support
//================================
typedef clock_t TIMER_T;

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

/* 64bit Linux */
#if defined(__LP64__)

#  define UINTPTR_T unsigned long int
#  define INTPTR_T long int

/* 32bit Linux */
#else

#  define UINTPTR_T unsigned int
#  define INTPTR_T int

#endif

#ifdef highbytefirst
#define _BIG__ENDIAN_ true
#else
#undef _BIG__ENDIAN_
#endif
//================================
// quantization optimization
//================================
//#define RECIP_QUANT_OPT

#endif
