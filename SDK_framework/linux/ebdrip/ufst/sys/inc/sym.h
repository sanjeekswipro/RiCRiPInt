/* $HopeName: GGEufst5!sys:inc:sym.h(EBDSDK_P.1) $ */

/* 
 * Copyright (C) 2004 Agfa Monotype Corporation. All rights reserved.
 */
/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/sys:inc:sym.h,v 1.3.8.1.1.1 2013/12/19 11:24:03 rogerb Exp $ */
/* $Date: 2013/12/19 11:24:03 $ */
/* sym.h */




/*--------------------------------*/
/* Symbol set file directoy entry */
/*--------------------------------*/

/*  Modification History:
 *
 *  24 Jun 91  ss   Changed PBYTE SYMBOLSET.pss_directory to PUBYTE.
 *  28 Jun 91  rs   Added prototype for SYMmapR().
 *  25 Oct 91  ss   Added SS_COUNT_SIZE and SS_COUNT_TYPE defines.
 *  28-Oct-91  jfd  Changed typedef for PSS_ENTRY from * to FARPTR * to get
 *                  rid of compiler warning when building medium model.
 *  02-Apr-92  rs   Portability cleanup (see port.h).
 *  08-Jul-92  jfd  Code cleanup.
 *  24-Sep-92  mby  Added UFSS_DIRECTORY structure; file IDs for UIF.SS and
 *                  UTT.SS file formats; #defined offsets into SS_DIR... and
 *                  UFSS_DIR... structures.
 *  29-Sep-92  mby  SYMBOLSET has pointers/handles to two symbol set directories.
 *                  Added GET_xWORD... GET_xLONG... macros.
 *  30-Sep-92  mby  Change SYMBOLSET.pss_entry declaration from PSS_ENTRY to
 *                  LPUW16. Change function declaration to SYMnew(UW16, UW16);
 *  30-Sep-92  jfd  Removed conditional compiles based on SPARC and
 *                  replaced with conditional compiles based on
 *                  (NAT_ALIGN == 4) wherever possible.
 *  08-Oct-92  mby  #defined SYMBOLSET_IF, SYMBOLSET_TT, NSS_FILES, whose
 *                  values depend on which FSTs are enabled. Changed SYMBOLSET
 *                  structure. Added SYMBOLSET.platID & SYMBOLSET.specID
 *  21-Oct-92  mby  Moved GET_x...() macros into "cgmacros.h"
 *  04-Jun-93  jfd  Added define for T1ENCODING (Type 1 Encoding).
 *  06-Jun-94  mby  Added prototype for SYMnoMap().
 *  08-Sep-94  mby  #define SYMBOLSET_TT if FCO_RDR is enabled.
 *  15-Dec-94  SBM  Added  SYMmapReverse.
 *  16-Dec-94  SBM  Added FCO_TT conditional around SYMmapReverse. Only the
 *                  TrueType generator uses this code for now.
 *  04-Jan-95  jfd  Added support for MicroType symbol sets.
 *  19-Jan-95  SBM  Added SYMmapTT.
 *  01-Jan-97  dlk  Added tt_langID to SYMBOLSET structure definition to make
 *                  it possible to distinguish between GB and BIG5 encoded fonts.
 *	12-Jun-98  slg	Move fn prototypes to shareinc.h
 *	31-Aug-98  slg	Need to rename "class" elements of SS_DIRECTORY and
 *					UFSS_DIRECTORY to "sym_class" (avoid C++ keyword conflict) 
 * 20-Jan-99  dlk Added CFFENCODING symbol to function for CFF processing much
 *                as T1ENCODING functions for Type 1.
 * 25-Jan-99  dlk Added CFFSID symbol to function for CFF processing - chId
 *                values will be taken as CFF SIDs, and the Charset Array will
 *                be searched instead of the Encoding Array.
 * 18-Feb-99  dlk Changed CFFSID symbol to CFFCHARSET... same use and meaning.
 *  31-Jan-00  slg	Integrate disk/rom changes (for jd) - both "pss_directory"
 *					and "hss_directory" can be in SYMBOLSET at the same time.
 */

#ifndef __SYM__
#define __SYM__

/*
CFF related items
*/

/*  This Symbol is defined with the same value as T1ENCODING, but     */
/*  no conflict should arise because a font cannot be CFF and regular */
/*  Type 1 at the same time.  The processing code paths are separate. */
/*                                                                    */
#define     CFFENCODING 0x8100   /* CFF Type2 Encoding */
#define     CFFCHARSET  0x8200   /* CFF SID values, and Charset Array */


/*
Type 1 related items
*/
#define     T1ENCODING  0x8100   /* Type 1 Encoding */


typedef struct
{
    UW16 ss_code;    /* symbol set code */
    UW16 symSetCode; /* 32 * PCLnum + (short)PCLchar - 64 */
    UW16 type;
    UW16 sym_class;
    UW16 first_code; /* first code */
    UW16 num_codes;  /* number of codes */
    UB8 requirements[8]; /* keb 9/02/04 */
    UL32 offset;     /* offset to symbol set in symbol set file */

}  SS_DIRECTORY;

typedef SS_DIRECTORY FARPTR * PSS_DIRECTORY;

typedef struct
{
    UW16 ss_code;    /* symbol set code */
    UW16 symSetCode; /* 32 * PCLnum + (short)PCLchar - 64 */
    UW16 type;
    UW16 sym_class;
    UW16 first_code; /* first code */
    UW16 num_codes;  /* number of codes */
    UB8 requirements[8]; /* keb 9/02/04 */
    UL32 offset;     /* offset to symbol set in symbol set file */
    UW16 platformID; /* Character mapping information required */
    UW16 specificID; /*   when loading TrueType fonts. */

}  UFSS_DIRECTORY;

typedef UFSS_DIRECTORY FARPTR * PUFSS_DIRECTORY;


#define IFSSIDN   0x55494631L   /* UIF.SS file identifier -- 9-24-92 */
#define TTSSIDN   0x55545431L   /* UTT.SS file identifier */
#define MTSSIDN   0x554D5431L   /* UMT.SS file identifier */

/* Offsets into SS_DIRECTORY and UFSS_DIRECTORY structures.    */
#define SSDIR_SIZE   24         /* keb 9/02/04 */
#define UFSSDIR_SIZE 28         /* keb 9/02/04 */
#define SSD_FILEID    0         /* 'UIF1', 'UTT1 or 'UMT1' */
#define SSD_NSS       4         /* Number of symbol sets */
#define SSD_DIRSIZE   6         /* sizeof(SS_DIRECTORY or UFSS_DIRECTORY) */
#define SSD_DOFF      8         /* Offset to start of directory table */
#define SSD_CODE      0         /* ss_code    */
#define SSD_SYMCODE   2         /* symSetCode */
#define SSD_TYPE      4         /* type       */
#define SSD_CLASS     6         /* class      */
#define SSD_FIRSTC    8         /* first code */
#define SSD_NUMC     10         /* num_codes  */
#define SSD_REQUIRE  12         /* requirements keb 9/02/04 */
#define SSD_OFF      20         /* offset     keb 9/02/04 */
#define SSD_PLID     24         /* platformID keb 9/02/04 */
#define SSD_SPID     26         /* specificID keb 9/02/04 */


/*--------------------------------*/
/*        Symbol set entry        */
/*--------------------------------*/

typedef struct
{
    UW16 cgnum;
    UW16 bucket_type;   /* no longer used */
} SS_ENTRY;
typedef SS_ENTRY FARPTR * PSS_ENTRY;

#if (IF_RDR || PST1_RDR)

#if FCO_RDR
#define  SYMBOLSET_IF  0
#define  SYMBOLSET_TT  1
#define  SYMBOLSET_MT  2
#define  NSS_FILES     3
#endif
#if (TT_RDR && !FCO_RDR)
#define  SYMBOLSET_IF  0
#define  SYMBOLSET_TT  1
#define  NSS_FILES     2
#endif
#if !(TT_RDR || FCO_RDR)
#define  SYMBOLSET_IF  0
#define  NSS_FILES     1
#endif

#else

#if FCO_RDR
#define  SYMBOLSET_TT  0
#define  SYMBOLSET_MT  1
#define  NSS_FILES     2
#endif
#if (TT_RDR && !FCO_RDR)
#define  SYMBOLSET_TT  0
#define  NSS_FILES     1
#endif

#endif


typedef struct
{
#if ROM
    LPUB8        pss_directory[NSS_FILES]; /* jwd, 11-8-99 */
#endif
#if DISK_FONTS
    MEM_HANDLE   hss_directory[NSS_FILES]; /* directory of symbol sets */
#endif
    LPUW16       pss_entry[NSS_FILES];     /* array of entries for cur IF ss */
    UW16         curssnum;                 /* current symbol set number     */
    UW16         ss_first_code[NSS_FILES]; /* current symbol set first code */
    UW16         ss_last_code[NSS_FILES];  /*  "        "     "  last code  */
    UW16         ss_symSetCode[NSS_FILES];
    UW16         ss_type[NSS_FILES];
    UW16         ss_class[NSS_FILES];
    UB8          ss_enabled[NSS_FILES];    /* 1 = YES; 0 = NO */
	UB8          ss_requirements[NSS_FILES][8]; /* keb 10/21/04 */
#if TT_RDR
    UW16         tt_platID;
    UW16         tt_specID;
    UW16         tt_langID;
#endif

} SYMBOLSET;


#endif	/* __SYM__ */
