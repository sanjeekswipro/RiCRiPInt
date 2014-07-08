
/* Copyright (C) 2008 Monotype Imaging Inc. All rights reserved. */

/* Monotype Imaging Confidential */

/* sym.h */


#ifndef __SYM__
#define __SYM__

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
