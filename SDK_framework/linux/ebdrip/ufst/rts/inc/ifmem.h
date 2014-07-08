/* $HopeName: GGEufst5!rts:inc:ifmem.h(EBDSDK_P.1) $ */

/* Copyright (C) 2003 Agfa Monotype Corporation. All rights reserved. */

/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/rts:inc:ifmem.h,v 1.3.8.1.1.1 2013/12/19 11:24:03 rogerb Exp $ */
/* $Date: 2013/12/19 11:24:03 $ */
/* ifmem.h */
/*
 *
 *
 * History
 *
 *  17-Feb-91  awr  split off from cgif.h 
 *  23-Feb-91  awr  moved MEM_HANDLE offset and page macros from mem.c
 *   6-Jun-91  jfd  Added SUN support for OFFBITS() and OFF.
 *                  (FIX FROM EARLIER VERSION)
 *  24 Mar 92  ss   Added #define MEM_HANDLE_DEFINED needed in TT input code
 *                  to avoid redefine MEM_HANDLE if its already been done.
 *  03-Apr-92  rs   Portability cleanup (see port.h).
 *  13 Jul 92  ss   Changed condition compile of OFFBITS from ROM/SUN to
 *                  UNIX.  All UNIXs will now use more OFFBITS.
 *  13-Aug-92  rs   Added support for Watcom C386 (__FLAT__) model by
 *                  making OFFBITS same as for other 32 bit platforms.
 *  17-Dec-92  rs   Make OFFBITS depend only on 16 bit DOS else other.
 *  08-Dec-93  rs   General cleanup - MSDOS & FLAT -> CGFLAT32.
 *  21-Nov-94  jfd  Changed OFFBITS for 1200 DPI support.
 *  09-Jan-95  rob  Implement > 64KB support for internal memory mgt.
 *  06-Jul-95  mac  Basic 1200 dpi support.
 *  14-Apr-97  mby  Replaced "LINTARGS" with "LINT_ARGS".
 *	12-Jun-98  slg	All fn prototypes moved to shareinc.h
 *  03-Aug-01  jfd  If INT_MEM_MGT == 1, changed typedef for MEM_HANDLE
 *                  to UL64 for 64-bit platforms.
 */
/*
The internal functions of the Intellifont run time library require
a dynamic memory manager. This management may be supplied by the

    - c runtime library functions malloc() and free()
    - the module mem.c
    - an application specific memory manager

Source code for the first two options is supplied. Either option may be
selected by setting the define in cgconfig.h

    #define INT_MEM_MGT  0    Set to 1 for internal memory manager

The file ifmem.h then conditional defines one of the first two options.
For the last option, use the definition of external memory management at
the end of ifmem.h as a guide and replace it with your own.
 */

#ifndef __IFMEM__
#define __IFMEM__

#if INT_MEM_MGT   /*  If CG memory manager  */

/*
        I N T E R N A L    M E M O R Y    M A N A G E R

Initial memory is funded by the application. The application may provide
more than one block of memory for each pool. Each block of memory is
internally referred to as a PAGE and is assigned a page number.

Memory pages are subdivided into smaller blocks to fullfill MEMalloc()
requests. Each of these smaller blocks is refered to be a handle of type
MEM_HANDLE.

A memory handle is a ULONG interpreted as having two fields. A PAGE field
refers to the internally assigned page number and an offset field giving
the byte offset of the memory block from the start of the page.

The macros PAGE() and OFF() extract these fields from a MEM_HANDLE.
The maximum size for a memory block returned by MEMalloc() is determined
by the size of the offset field which is defined by OFFBITS.
*/

#if (USING_16_BIT_DOS && !HUGE_PTR_SUPPORT)
#define OFFBITS  16                   /* bits in offset field of handle */
#define OFF(x)   (x & 0xffff)
#else
  /*  need larger offset for addressing into font block on 32 bit machine  */
#define OFFBITS  25
#define OFF(x)   (x & 0x1ffffff)
#endif  /* USING_16_BIT_DOS && !HUGE_PTR_SUPPORT */

#define PAGE(x)  (UW16)(x >> OFFBITS)

#define MEM_HANDLE_DEFINED
#if (NAT_ALIGN == 8)		/* 08-03-01 jfd */
typedef UL64        MEM_HANDLE;
#else
typedef UL32        MEM_HANDLE;
#endif
typedef MEM_HANDLE FARPTR * PMEM_HANDLE;
#define NIL_MH       ((MEM_HANDLE)0L)

#else

/*   C    R U N    T I M E    M E M O R Y    M A N A G E R

We use malloc() and free() to provide the memory management.
Since handles aren't used we define a MEM_HANDLE to be a byte pointer
and the hndle to ptr conversion function is the identity macro.

*/

#define MEM_HANDLE_DEFINED
#if 1
typedef LPSB8        MEM_HANDLE;
#else	/* experimental code to resolve loose-to-strict alignment warnings (sandra, 26 nov 02) */
		/* part 1 of 2: must also use alternate version of MEMfree() in /dep/extmem.c */
typedef LPSL32        MEM_HANDLE;
/* typedef VOID *        MEM_HANDLE; */	/* this also works in Win32 environment */
/* would it be more correct to set the type as (pointer to union of all basic datatypes)? */
#endif	/* end experimental code */
typedef MEM_HANDLE FARPTR * PMEM_HANDLE;
#define NIL_MH       ((MEM_HANDLE)0)

#endif	/* INT_MEM_MGT */

#endif	/* __IFMEM__ */
