/* $HopeName: GGEufst5!rts:inc:pcleomgt.h(EBDSDK_P.1) $ */

/* Copyright (C) 2004 Agfa Monotype Corporation. All rights reserved. */

/* $Header: /hope/man5/hope.0/compound/10/GGEufst5/RCS/rts:inc:pcleomgt.h,v 1.2.10.1.1.1 2013/12/19 11:24:05 rogerb Exp $ */
/* Log stripped */

/************************

The file pcleomgt.h is needed in order to build the sample PCLEO code in /demo/src/pcleomgt.c.
It is included in the standard header file /rts/inc/shareinc.h if PCLEOs are enabled
(and the sample PCLEO code is enabled via MTI_PCLEO_DEMO).
 
All the read-write global data used by the sample PCLEO code is contained in the PCLEO_CALLBACK_DATA
structure (defined below). An instance "pcd" of this structure is contained in the IF_STATE structure
(if PCLEOs are enabled, and the sample PCLEO code is enabled via MTI_PCLEO_DEMO). 

The if_state.pcd field is initialized by the function /demo/src/pcleomgt.c/init_pcleo_callback_data(), 
which is called from the function /rts/dep/cgif.c/init_former_globals() as part of the normal UFST
initialization sequence (if PCLEOs are enabled, and the sample PCLEO code is enabled via MTI_PCLEO_DEMO).
 
 ***********************/


/* error return codes from sample code in pcleomgt.c */

/** from installPcleoHdr **/
#define PCLErr_HDR_ALLOC        100   /* unable to alloc for header   */
#define PCLErr_HDR_READ         101   /* unable to read header        */
#define PCLErr_HDR_ENTRY_ALLOC  102   /* unable to alloc header entry */
#define PCLErr_HDR_REALLOC      103   /* bad realloc                  */

/** from installPcleoChar **/
#define PCLErr_CHR_ALLOC        201   /* can't alloc for char         */
#define PCLErr_CHR_READ         202   /* unable to read character     */

/** from installPcleo, escParser */
#define PCLErr_FILE_STAT        300   /* can't get status for filename */
#define PCLErr_OPEN             301   /* error on file open            */
#define PCLErr_PCLEO_CMD        302   /* can't handle pcl command found */
#define PCLErr_DIGIT            303   /* expected digit, didn't get it */
#define PCLErr_UNKNOWN           99   /* Unknown error in input        */
#define PCLErr_StrangeFormat    298   /* Unknown PCLEO format -- mby 6/19/92 */
#define PCLErr_BAD_PCLEO        304   /* PCLEO contains invalid data   */


/* Flag Masks for escParser() */
#define ESC         0x01
#define OPEN_PAREN  0x02
#define CLOSE_PAREN 0x04
#define SIZE        0x08
#define ASTERISK    0x10
#define C_CHAR      0x20
#define END_W       0x40
#define END_E       0x80

#define DWNLD_HDR   0x4D                 /*  Esc)s###W     */
#define DWNLD_CHAR  0x4B                 /*  Esc(s###W     */
#define CHAR_CODE   0xB1                 /*  ESC*c##E      */


/* Flag Masks for xlParser() */
#define BEGIN_FONT_HEADER     0x0001
#define READ_FONT_HEADER      0x0002
#define END_FONT_HEADER       0x0004
#define BEGIN_CHAR            0x0008
#define READ_CHAR             0x0010
#define END_CHAR              0x0020
#define SIZE8                 0x0040
#define SIZE32                0x0080
#define C1                    0x0100
#define MAX_READ_HDR          50


#if (FONTBBOX && IF_PCLEOI)         /* mby - 6/04/93 */

#define  PCLEO_IF_TYPE         1
#define  PCLEO_NOTIF_TYPE      0
#define  BAD_CHAR_ARRAY_SIZE   32

#endif /* (FONTBBOX && IF_PCLEOI) */


/*  Don't confuse this with the Library format character dir.         */
/*   There will be one of these for each PCLEO character downloaded.  */
typedef struct
{
    LPUB8 charp;         /*  location of Pcleo character data        */
    UW16  charcount;     /*  Byte count of data                      */
#if TT_PCLEOI
    UW16  glyphID;       /*  TrueType PCLEO rdr needs this           */
#endif
	UW16  char_code;	/* MSL # (IF) or UNICODE # (TT) of character */
} PCLEO_CH_DIR_ENTRY;
typedef PCLEO_CH_DIR_ENTRY FARPTR * PPCLEO_CH_DIR_ENTRY;

typedef struct {
     UL32               tfnum;
     LPUB8              loc;    /* pointer to font header data buffer */
	 UL32				nChars;	/* number of characters in pcleo */
	 UL32				fst_type;	/* IF = 0, TrueType = 1, PS = 2, PCL Bitmap = 3, EFM Bitmap = EFM_DL_FST_TYPE */
	 LPUB8				efm_tfname;	/* font name */
	 SW16				efm_dwidth;	/* EFM font-wide width in device pixels */
	 SW16				size;		/* point size of all glyphs in font */
	 SW16				xres;		/* output device x resolution */
	 SW16				yres;		/* output device y resolution */
     PPCLEO_CH_DIR_ENTRY char_dir;

} PCLEO_HDR_DIR_ENTRY;
typedef PCLEO_HDR_DIR_ENTRY FARPTR * PPCLEO_HDR_DIR_ENTRY;


typedef struct
{
	/* "*charDirectory" is essentially a listing of characters stored in */
	/* the downloaded font (sequence #).                                 */
	UL32 *charDirectory;

	PCLEO_HDR_DIR_ENTRY *cur_hdr;
	PCLEO_HDR_DIR_ENTRY *pcl_hdr_dir;
	UW16                nPcleos;				

#if TT_PCLEOI
	BOOLEAN             trueTypePcleo;
#endif

#if (FONTBBOX && IF_PCLEOI)
	UB8   bad_chars[BAD_CHAR_ARRAY_SIZE];
	UW16  badChars[CGIFP_MAX_BADCHARS];
	int   pmg_fst_type;
#endif

} PCLEO_CALLBACK_DATA;


