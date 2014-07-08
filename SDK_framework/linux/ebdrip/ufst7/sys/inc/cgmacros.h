
/* Copyright (C) 2008, 2013 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* cgmacros.h */


#ifndef __CGMACROS__
#define __CGMACROS__

#define BITSPERBYTE 8

#if (BYTEORDER == HILO) /* Motorola format, don't swap bytes */

/* make long from words  */
#define UFST_MAKELONG( dest, srcp )  \
    hiword = *( (LPUW16)(srcp) ); \
    loword = *( (LPUW16)(srcp+2) ); \
    dest   =  ( (UL32)hiword << (2*BITSPERBYTE) ) + loword;

#else 

/* make long from words  */    
#define UFST_MAKELONG( dest, srcp )  \
    hiword = *( (LPUW16)(srcp+2) ); \
    loword = *( (LPUW16)(srcp) ); \
    dest   =  ( (UL32)hiword << (2*BITSPERBYTE) ) + loword;
#endif  /* BYTEORDER == HILO */


/* more swap macros (from tt/fscdefs.h) */
#if (BYTEORDER == LOHI)

#define SWAPW(a)        (UW16)(((UB8)((a) >> 8)) | ((UB8)(a) << 8))

#define SWAPL(a)        ((((a)&0xffL) << 24) | (((a)&0xff00L) << 8) | \
                         (((a)&0xff0000L) >> 8) | ((UL32)(a) >> 24))

#else                   /* byte order matches Motorola 680x0 */

#define SWAPW(a)		(a)

#define SWAPL(a)		(a)

#endif	/* Motorola byte order */

/* Processor-independent macros to read 1, 2, or 4 bytes from memory in
 * high, ..., low order. Alignment on 2- or 4-byte boundaries is not required.
 */

#define FS_ALIGN(p) p += (sizeof(FS_VOID *) - 1); p &= ~(sizeof(FS_VOID *) - 1)

#define GET_xWORD(a)  \
           ( ((UW16) *(UB8*)(a) << 8) |      \
              (UW16) *((UB8*)(a)+1) )


#define GET_x24BIT(a)  \
           ( ((UL32) *(UB8*)(a) << 16)     |  \
             ((UL32) *((UB8*)(a)+1) << 8)  |  \
              (UL32) *((UB8*)(a)+2) )
			  
#define GET_xLONG(a)  \
           ( ((UL32) *(UB8*)(a) << 24)     |  \
             ((UL32) *((UB8*)(a)+1) << 16) |  \
             ((UL32) *((UB8*)(a)+2) << 8)  |  \
              (UL32) *((UB8*)(a)+3) )

#define GET_xBYTE_OFF(a,OFFSET)  ( *((UB8*)(a)+OFFSET) )

#define GET_xWORD_OFF(a,OFFSET)  \
           ( ((UW16) *((UB8*)(a)+OFFSET) << 8) |    \
              (UW16) *((UB8*)(a)+OFFSET+1) )

#define GET_xLONG_OFF(a,OFFSET)  \
           ( ((UL32) *((UB8*)(a)+OFFSET) << 24)   |  \
             ((UL32) *((UB8*)(a)+OFFSET+1) << 16) |  \
             ((UL32) *((UB8*)(a)+OFFSET+2) << 8)  |  \
              (UL32) *((UB8*)(a)+OFFSET+3) )

#define SWAPWINC(a) (SW16)(((UW16)(((UB8*)(a))[0])) << 8 | ((UB8*)(a))[1] ); a++
#define PSWAPL(a)  (SL32)(((UL32)(((UB8*)(a))[0])) << 24 | ((UL32)(((UB8*)(a))[1])) << 16 \
                                                         | ((UL32)(((UB8*)(a))[2])) <<  8 |  (UL32)(((UB8*)(a))[3]) )
#define PSWAPW(a) (UW16)(((UW16)(((UB8*)(a))[0])) << 8 | ((UB8*)(a))[1] )

/* add new macro for standard operation = using MEM_ALIGN to align memory */
#define ALIGN_MEM(nbytes) ((nbytes + MEM_ALIGN) & ~MEM_ALIGN)

#endif	/* __CGMACROS__ */
