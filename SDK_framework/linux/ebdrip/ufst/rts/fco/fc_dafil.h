/* $HopeName: GGEufst5!rts:fco:fc_dafil.h(EBDSDK_P.1) $ */

/* 
 * Copyright (C) 2003 Agfa Monotype Corporation. All rights reserved.
 */
/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/rts:fco:fc_dafil.h,v 1.3.8.1.1.1 2013/12/19 11:24:04 rogerb Exp $ */
/* $Date: 2013/12/19 11:24:04 $ */

/* fc_dafil.h */

/*-----------------------------------------------------------------*/

/* History:
 *
 * 25-Mar-94  mby  DAgetInt() defined as a macro.
 *                 Added code for DISKFILE = 0 (simulates ROM).
 * 31-Mar-94  mby  DAgetInt macro doesn't work on Microsoft 6 compiler. Make
 *                 it a function again.
 * 18-Jul-94  mby  DISKFILE is defined 0 or 1 in the source files.
 *                 Moved dmemAlloc and dmemFree macros here from fc_da.h
 * 04-Aug-94  mby  If DEBUG is on, declare dmemAlloc/Free as functions.
 * 02-Sep-94  mby  Added prototype for DAgetUWord().
 * 15-Sep-94  dbk  Added LINTARGS for non-ANSI compliance.
 * 29-Sep-94  dbk/jfd
 *                 Changed DAgetInt macro to resolve SUN error.
 * 03-Oct-94  jfd  Made variable "DAgetInt_tmp" a "static" to resolve
 *                 WATCOM compiler error.
 * 05-Oct-94  dbk  Rearranged parentheses in DAgetChar macro to resolve
 *                 MIPS compiler error.
 * 15-Jun-95  mby  Added prototype for DAgetSWord().
 * 19-Jun-95  mby  Fixed bug in DAgetInt() macro for !DISKFILE,
 *                 32-bit platform, default to unsigned char.
 * 20-Jul-95  mby  In DAgetInt macro replaced "signed char" with SB8 (SPARC compiler error).
 * 19-Jun-96  mby  Complete rewrite. Use only UFST defined data types. Add
 *                 more DAgetXXXX macros.
 * 14-Apr-97  mby  Replaced "LINTARGS" with "LINT_ARGS".
 * 10-Mar-98  slg  Don't use "long" dcls (incorrect if 64-bit platform);
 *					also get rid of unused macros
 * 31-Mar-98  slg  Replace !DISKFILE test by FCO_ROM.
 * 14-Apr-98  slg  Static vbl DAgetInt_tmp must now declared locally in all
 *					functions that use DAgetInt(), if FCO_ROM
 * 12-Jun-98  slg  Move some fn prototypes to shareinc.h (for reentrancy)
 * 31-Jan-00  slg  Integrate disk/rom changes (for jd) - #if-SIMULDISKROM
 *				   changes to DAFILE, DAgetSB/UB8(), DAgetPos/Char/Int(),
 *				   DA_err_Read, DAerror().
 * 08-Feb-00  slg  Move several prototypes to shareinc.h - required to make
 *					reentrant build work for disk/ROM.
 * 29-Aug-02  slg  Change DAFILE struct so that "FILE" type is only used if
 *					FCO_DISK is enabled - this is a customer request, so that
 *					file-I/O header files don't need to be included for a ROM-
 *					only build. Also needed to create alternate versions of 
 *					DAgetChar/Pos/SB8/UB8, DAerror (for compile reasons).
 */


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


/*********************************/
/*   File Structure Definition   */
/*********************************/
typedef struct
{
#if FCO_DISK
    FILE  *fh;
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

#define DAgetSB8(f)       ( (if_state.font_access == ROM_ACCESS) \
                          ? (*((LPSB8)(f->curPos)++))            \
                          : ((SB8)fgetc(f->fh)) )
#define DAgetUB8(f)       ( (if_state.font_access == ROM_ACCESS) \
                          ? (*((LPUB8)(f->curPos)++))            \
                          : ((UB8)fgetc(f->fh)) )

#elif FCO_DISK	/* use FILE-based definitions */

#define DAgetSB8(f)       ((SB8)fgetc(f->fh))
#define DAgetUB8(f)       ((UB8)fgetc(f->fh))

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
                          : ferror(f->fh) )
#define DAgetPos(f)       ( (if_state.font_access == ROM_ACCESS) \
                          ? ((SL32)(f->curPos - f->startPos))    \
                          : ftell(f->fh) )

#define DAgetChar(f)      ( (if_state.font_access == ROM_ACCESS) \
                          ? (*((UB8*)(f->curPos)++))             \
                          : fgetc(f->fh) )

#define DAgetBlock(f,p,l) ( (if_state.font_access == ROM_ACCESS) \
                          ? (VOID *)(memcpy(p,f->curPos,l),              \
                             f->curPos += l)                      \
                          : (VOID *)fread(p,l,1,f->fh ))


#if BYTEORDER == HILO

#define DAgetIntBlock(f,p,l)( (if_state.font_access == ROM_ACCESS) \
                          ? (VOID *)(memcpy(p,f->curPos,(l)*2),              \
                             f->curPos += (l)*2)                      \
                          : (VOID *)fread(p,(l)*2,1,f->fh ))
#else

#define DAgetIntBlock(f,p,l) {( (if_state.font_access == ROM_ACCESS) \
                          ? (VOID *)(memcpy(p,f->curPos,(l)*2),              \
                             f->curPos += (l)*2)                      \
                          : (VOID *)fread(p,(l)*2,1,f->fh )); \
	for(DAgetInt_tmp=0; (UL32)DAgetInt_tmp<(UL32)(l); DAgetInt_tmp++) \
       ((UW16 *)p)[DAgetInt_tmp] = ((((UW16 *)p)[DAgetInt_tmp]&0xff)<<8) + \
                                 (((UW16 *)p)[DAgetInt_tmp]>>8);}

#endif


#elif FCO_DISK	/* use FILE-based definitions */

#define DAerror(f)        (ferror(f->fh))
#define DAgetPos(f)       (ftell(f->fh))

#define DAgetChar(f)      (fgetc(f->fh))
#define DAgetBlock(f,p,l) fread(p,l,1,f->fh )

#if BYTEORDER == HILO
#define DAgetIntBlock(f,p,l) (fread(p,(l)*2,1,f->fh))
#else
#define DAgetIntBlock(f,p,l) {(fread(p,(l)*2,1,f->fh)); \
	for(DAgetInt_tmp=0; (UL32)DAgetInt_tmp<(UL32)(l); DAgetInt_tmp++) \
    ((UW16 *)p)[DAgetInt_tmp] = ((((UW16 *)p)[DAgetInt_tmp]&0xff)<<8) + \
                                 (((UW16 *)p)[DAgetInt_tmp]>>8);}
#endif

#else	/* FCO_DISK not defined: only use ROM-based definitions */

#define DAerror(f)        (f->error)

#define DAgetPos(f)       ((SL32)(f->curPos - f->startPos))

#define DAgetChar(f)      (*((UB8*)(f->curPos)++))

#define DAgetBlock(f,p,l) (memcpy(p,f->curPos,l),f->curPos += l)

#if BYTEORDER == HILO
#define DAgetIntBlock(f,p,l) (memcpy(p,f->curPos,(l)*2),f->curPos += (l)*2)
#else
#define DAgetIntBlock(f,p,l) {memcpy(p,f->curPos,(l)*2);      \
	                          f->curPos += (l)*2; \
	for(DAgetInt_tmp=0; (UL32)DAgetInt_tmp<(UL32)(l); DAgetInt_tmp++) \
    ((UW16 *)p)[DAgetInt_tmp] = ((((UW16 *)p)[DAgetInt_tmp]&0xff)<<8) + \
                                 (((UW16 *)p)[DAgetInt_tmp]>>8);}
#endif

                          

#endif	/* FCO_DISK */

#define DAgetInt(f)       (DAgetInt_tmp = (SB8)DAgetChar(f),       \
                            (DAgetInt_tmp<<8) | (UB8)DAgetChar(f))

#define DAgetUWord(f)       (DAgetInt_tmp = (SB8)DAgetChar(f),       \
                            ((UB8)DAgetInt_tmp<<8) | (UB8)DAgetChar(f))

#endif	/* __FC_DAFIL__ */
