
/* Copyright (C) 2008 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* fc_dafil.h */


/*-----------------------------------------------------------------*/

/* All low-level access to the font collection data is through a set of
 * macros and functions defined below and in module FC_DAFIL.C. A font
 * collection may be either disk or ROM resident.  These routines either use
 * standard C stream i/o functions or use their ROM equivalents of these
 * functions.
 *
 * These routines are accessed only from modules FC_DAFIL.C and FC_DA.C.
 * They are never called directly by high-level code.
 * If the "UFST_ON" switch in the .C files is defined, then the DISKFILE
 *   compiler switch is based on FCO_DISK/ROM in the UFST cgconfig.h file.
 * For non-UFST applications, UFST_ON is not defined. Set appropriate
 * definitions in "fcconfig.h".
 */

#ifndef __FC_DAFIL__
#define __FC_DAFIL__
#if defined(_WIN32_WCE)
EXTERN int errno;
#endif
/*********************************/
/*   File Structure Definition   */
/*********************************/
typedef struct
{
#if FCO_DISK
	INTG   fh;
	UB8    byte_read;	/* used by DISK versions of DAgetSB8(), DAgetUB8() and DAgetChar() macros */
#endif
#if FCO_ROM
    SL32  fileLength;
    LPUB8 startPos;
    LPUB8 curPos;
    INTG  error;
#endif
} DAFILE;


/*************************************/
/*   Data Access Routines & Macros   */
/*************************************/

/* These routines grab 1, 2, or 4 bytes of data.
 * There is a "DAgetXXX" macro/function for every standard UFST data type:
 *   SB8, UB8, SW16, UW16, SL32, UL32, INTG, UINTG
 * getUINTG and getINTG read 2 bytes and return (unsigned) int data type.
 * The getSB8 and getUB8 routines are the fundamental code that pull a byte
 *   of data from the stream. They DO NOT test for the EOF condition.
 * The other routines pull 2 or 4 bytes from the stream. These are always
 *   interpreted as Big-Endian.
 */
/* Extract 1 byte from file */

#if FCO_DISK && FCO_ROM	/* use FILE-or-ROM definitions */

#define DAgetSB8(f)       ( errno = 0, (if_state.font_access == ROM_ACCESS) \
                          ? (*((LPSB8)(f->curPos)++))            \
                          : (READF(f->fh, &f->byte_read, 1) == -1) ? 0 : (SB8)f->byte_read )
#define DAgetUB8(f)       ( errno = 0, (if_state.font_access == ROM_ACCESS) \
                          ? (*((LPUB8)(f->curPos)++))            \
                          : (READF(f->fh, &f->byte_read, 1) == -1) ? 0 : (UB8)f->byte_read )
#elif FCO_DISK	/* use FILE-based definitions */

#define DAgetSB8(f)       (errno = 0, READF(f->fh, &f->byte_read, 1) == -1) ? 0 : (SB8)f->byte_read
#define DAgetUB8(f)       (errno = 0, READF(f->fh, &f->byte_read, 1) == -1) ? 0 : (UB8)f->byte_read

#else	/* FCO_DISK not defined: only use ROM-based definitions */

#define DAgetSB8(f)       (*((LPSB8)(f->curPos)++)) 
#define DAgetUB8(f)       (*((LPUB8)(f->curPos)++))

#endif


/*************************************/
/*   File Access Routines & Macros   */
/*************************************/

#if FCO_ROM
#define DAerr_Read        101
#endif

#if FCO_DISK && FCO_ROM	/* use FILE-or-ROM definitions */
#define DAerror(f)        ( (if_state.font_access == ROM_ACCESS) \
                          ? (f->error)                           \
                          : errno )
#define DAgetPos(f)       ( errno = 0, (if_state.font_access == ROM_ACCESS) \
                          ? ((SL32)(f->curPos - f->startPos))    \
						  : LSEEK(f->fh, 0L, SEEK_CUR) )

#define DAgetChar(f)      ( errno = 0, (if_state.font_access == ROM_ACCESS) \
                          ? (*((UB8*)(f->curPos)++))             \
                          : (READF(f->fh, &f->byte_read, 1) == -1) ? 0 : (INTG)f->byte_read )

#define DAgetBlock(f,p,l) ( errno = 0, (if_state.font_access == ROM_ACCESS) \
                          ? (VOID *)(MEMCPY(p,f->curPos,l),              \
                             f->curPos += l)                      \
                          : (VOID *)READF(f->fh, p, l))


#if BYTEORDER == HILO

#define DAgetIntBlock(f,p,l)( errno = 0, (if_state.font_access == ROM_ACCESS) \
                          ? (VOID *)(MEMCPY(p,f->curPos,(l)*2),              \
                             f->curPos += (l)*2)                      \
                          : (VOID *)READF(f->fh, p, (l)*2))
#else

#define DAgetIntBlock(f,p,l) {( errno = 0, (if_state.font_access == ROM_ACCESS) \
                          ? (VOID *)(MEMCPY(p,f->curPos,(l)*2),              \
                             f->curPos += (l)*2)                      \
                          : (VOID *)READF(f->fh, p, (l)*2)); \
	for(DAgetInt_tmp=0; (UL32)DAgetInt_tmp<(UL32)(l); DAgetInt_tmp++) \
       ((UW16 *)p)[DAgetInt_tmp] = ((((UW16 *)p)[DAgetInt_tmp]&0xff)<<8) + \
                                 (((UW16 *)p)[DAgetInt_tmp]>>8);}

#endif



#elif FCO_DISK	/* use FILE-based definitions */

#define DAerror(f)        (errno)
#define DAgetPos(f)       (errno = 0, LSEEK(f->fh, 0L, SEEK_CUR) )

#define DAgetChar(f)      (errno = 0, (READF(f->fh, &f->byte_read, 1) == -1) ? 0 : (INTG)f->byte_read)
#define DAgetBlock(f,p,l) (errno = 0, READF(f->fh,p,l))

#if BYTEORDER == HILO
#define DAgetIntBlock(f,p,l) (errno = 0, (READF(f->fh,p,(l)*2)))
#else
#define DAgetIntBlock(f,p,l) {errno = 0, (READF(f->fh,p,(l)*2)); \
	for(DAgetInt_tmp=0; (UL32)DAgetInt_tmp<(UL32)(l); DAgetInt_tmp++) \
    ((UW16 *)p)[DAgetInt_tmp] = ((((UW16 *)p)[DAgetInt_tmp]&0xff)<<8) + \
                                 (((UW16 *)p)[DAgetInt_tmp]>>8);}
#endif

#else	/* FCO_DISK not defined: only use ROM-based definitions */

#define DAerror(f)        (f->error)

#define DAgetPos(f)       ((SL32)(f->curPos - f->startPos))

#define DAgetChar(f)      (*((UB8*)(f->curPos)++))

#define DAgetBlock(f,p,l) (MEMCPY(p,f->curPos,l),f->curPos += l)

#if BYTEORDER == HILO
#define DAgetIntBlock(f,p,l) (MEMCPY(p,f->curPos,(l)*2),f->curPos += (l)*2)
#else
#define DAgetIntBlock(f,p,l) {MEMCPY(p,f->curPos,(l)*2);      \
	                          f->curPos += (l)*2; \
	for(DAgetInt_tmp=0; (UL32)DAgetInt_tmp<(UL32)(l); DAgetInt_tmp++) \
    ((UW16 *)p)[DAgetInt_tmp] = ((((UW16 *)p)[DAgetInt_tmp]&0xff)<<8) + \
                                 (((UW16 *)p)[DAgetInt_tmp]>>8);}
#endif

                          

#endif	/* FCO_DISK */

#define DAgetInt(f)       (errno = 0, DAgetInt_tmp = (SB8)DAgetChar(f),       \
                            (DAgetInt_tmp<<8) | (UB8)DAgetChar(f))

#define DAgetUWord(f)       (errno = 0, DAgetInt_tmp = (SB8)DAgetChar(f),       \
                            ((UB8)DAgetInt_tmp<<8) | (UB8)DAgetChar(f))

#endif	/* __FC_DAFIL__ */
