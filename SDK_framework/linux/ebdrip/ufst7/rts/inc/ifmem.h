
/* Copyright (C) 2008 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* ifmem.h */


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

#define OFFBITS  25	/* bits in offset field of handle */
#define OFF(x)   (x & 0x1ffffff)

#define PAGE(x)  (UW16)(x >> OFFBITS)

#define MEM_HANDLE_DEFINED
#if (NAT_ALIGN == 8)		/* 08-03-01 jfd */
typedef UL64        MEM_HANDLE;
#else
typedef UL32        MEM_HANDLE;
#endif
typedef MEM_HANDLE * PMEM_HANDLE;
#define NIL_MH       ((MEM_HANDLE)0L)

#else

/*   C    R U N    T I M E    M E M O R Y    M A N A G E R

We use malloc() and free() to provide the memory management.
Since handles aren't used we define a MEM_HANDLE to be a byte pointer
and the hndle to ptr conversion function is the identity macro.

*/

#define MEM_HANDLE_DEFINED
typedef LPSB8        MEM_HANDLE;
typedef MEM_HANDLE * PMEM_HANDLE;
#define NIL_MH       ((MEM_HANDLE)0)

#endif	/* INT_MEM_MGT */

#endif	/* __IFMEM__ */
