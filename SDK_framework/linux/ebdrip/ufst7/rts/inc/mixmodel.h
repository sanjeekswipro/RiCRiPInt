
/* Copyright (C) 2008 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* mixmodel.h */


/* 
 * Map various I/O and string functions to the appropriate OS/compiler functions.
 * 
 */

#ifndef __MIXMODEL__
#define __MIXMODEL__

#if defined (_WIN32)
#define O_READFLAGS   O_BINARY | O_RDONLY
#define O_WRITEFLAGS  O_BINARY | O_WRONLY| O_CREAT
#elif defined (GCCx86)
#define O_READFLAGS   O_BINARY | O_RDONLY
#define O_WRITEFLAGS  O_BINARY | O_WRONLY| O_CREAT
#elif defined (__i960)
#define O_READFLAGS   O_BINARY | O_RDONLY
#define O_WRITEFLAGS  O_BINARY | O_WRONLY| O_CREAT
#elif defined (VXWORKS)
#define O_READFLAGS   O_RDONLY,0x644
#define O_WRITEFLAGS  WRITE
#elif defined (_WIN32_WCE)
#define O_READFLAGS   "rb"
#define O_WRITEFLAGS  "w+"
#elif defined (UFST_LINUX)
#define O_READFLAGS   O_RDONLY
#define O_WRITEFLAGS  O_WRONLY
#else

/*** This default case won't work if your environment's file system has the 
Text/Binary distinction (e.g. Windows/DOS). If this is the case, you will need
to clone a new case using BINARY, to prevent premature end-of-read. ***/

#define O_READFLAGS   O_RDONLY           /* deleted O_BINARY  -ss 6/24/91   */
#define O_WRITEFLAGS  O_WRONLY | O_CREAT /* deleted O_BINARY  -ss 6/24/91   */
#endif	/* MSVC */

#if defined _AM29K   /* 2-7-94 jfd,dbk */
#define OPEN       _open
#define CLOSE      _close
#define LSEEK      _lseek
#define READF      _read
#elif defined (_WIN32_WCE)
SL32 open(SB8 *, SB8 *);
SL32 lseek(UL32 , SL32, INTG);
SL32 read(UL32, VOID *, UL32);
SL32 close(UL32);

#define OPEN      open
#define CLOSE     close
#define LSEEK     lseek
#define READF     read
#elif _MSC_VER >= 1400 /* Visual C++ 2005 */
#define OPEN(a,b)  (_sopen_s(&file_handle, a, b, _SH_DENYNO, 0) != 0 ? -1 : file_handle)
#define CLOSE      _close
#define LSEEK      _lseek
#define READF      _read
#else
#define OPEN       open
#define CLOSE      close
#define LSEEK      lseek    /* correct define from SEEK to LSEEK -ss 6/24/91 */
#define READF      read
#endif  /* AM29K*/

#define STRLEN     strlen
#if defined (_WIN32_WCE) || (_MSC_VER >= 1400)
#define STRNCPY(a,b,c)  strncpy_s(a,_countof(a), b, c)
#define STRNCPY_PTR(a,b,c,d)  strncpy_s(a, b, c, d)
#define STRCPY(a,b)     strcpy_s(a,_countof(a),b)
#define STRCPY_PTR(a,b,c) strcpy_s(a, b, c);
#define SSCANF     sscanf_s
/* When scanning strings or characters, an additional parameter indicating buffer size is required */
#define SSCANF_S(a,b,c) sscanf_s(a,b,c,sizeof(c))
#define SSCANF_2S(a,b,c,d) sscanf_s(a,b,c,sizeof(c),d,sizeof(d))
#define SCANF_S(a,b)  scanf_s(a, b, sizeof(b))
#define FSCANF     fscanf_s
#define FSCANF_S(a,b,c)   fscanf_s(a,b,c,sizeof(c))
#define FSCANF_2S(a,b,c,d) fscanf_s(a,b,c,sizeof(c),d,sizeof(d))
#define SPRINTF(a,b)  sprintf_s(a,sizeof(a),b)
#define SPRINTF_D(a,b,c) sprintf_s(a,sizeof(a),b,c)
#define FOPEN(a,b) (fopen_s(&fpfh, a, b) != 0 ? 0 : fpfh)
#define STRTOK(a,b) strtok_s(a,b,&next_token)
#define STRCAT(a,b) strcat_s(a, sizeof(a), b)
#else
#define STRNCPY    strncpy
/* Changed defines for STRCPY() and STRCPY_PTR() to strncpy() for safety */
#define STRCPY(a,b)   \
   ( \
     (MEMSET(a, 0, sizeof(a))), \
     (strncpy(a,b, ( (STRLEN(b) >= sizeof(a)) ? sizeof(a)-1 : STRLEN(b)) ) ) \
   )
#define STRCPY_PTR(a,b,c) strncpy(a,c,b)
#define STRNCPY_PTR(a,b,c,d)  strncpy(a,c,d)
#define SSCANF     sscanf
#define SSCANF_S   sscanf
#define SSCANF_2S  sscanf
#define SCANF_S    scanf
#define FSCANF     fscanf
#define FSCANF_S   fscanf
#define FSCANF_2S  fscanf
#define SPRINTF    sprintf
#define SPRINTF_D  sprintf
#define FOPEN      fopen
#define STRTOK     strtok
#define STRCAT     strcat
#endif
#define STRNCMP    strncmp
#define STRCMP     strcmp
#define MEMCPY     memcpy
#ifdef FS_EDGE_TECH
#define SYS_MEMCPY memcpy
#endif	/* defined(FS_EDGE_TECH) */
#define WRITEF     write
#define QSORT      qsort
#define STRSTR     strstr
#define MEMCMP     memcmp
#define MEMSET     memset
#ifdef FS_EDGE_TECH
#define SYS_MEMSET memset
#endif	/* defined(FS_EDGE_TECH) */


/* customer code add */
#define PTR_ARRAY(x) x	/* stub to allow code to compile w/o full SAFE_PTRS implementation */
						/* this definition corresponds to non-SAFE_PTRS version */

#endif	/* __MIXMODEL__ */

