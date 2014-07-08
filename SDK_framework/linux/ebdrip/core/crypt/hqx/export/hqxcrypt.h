/** \file
 * \ingroup hqx
 *
 * $HopeName: COREcrypt!hqx:export:hqxcrypt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Encryption program for RIP-ID keyed composite fonts.
 *
 * This is a standard-type unix filter, with one argument; the RIP-ID.
 * This should be a 16 bit number, preferably unique.
 *
 * This module is also usable within the matching application; if you do
 * not define MAKEMAIN you get useful routines.
 */

#ifndef __HQXCRYPT_H__
#define __HQXCRYPT_H__

struct FILELIST ;

/**
 * \defgroup hqx HQX encryption.
 * \ingroup core
 * This is the HQX encryption method.
 */

#if defined( MAKEMAIN ) || defined( MAKETOOL )  /* Stand-alone build */

#include <stdio.h>

int main (int argc, char *argv[]) ;

#else /* ! (defined( MAKEMAIN ) || defined( MAKETOOL )) */

#ifndef MAKE_HQXLIB

#ifdef getc
#undef getc
#endif
#define getc(f) Getc(f)

#ifdef ferror
#undef ferror
#endif
#define ferror(f) isIIOError(f)

#ifdef feof
#undef feof
#endif
#define feof(f) isIEof(f)

#endif /* MAKE_HQXLIB */

#endif /* !( defined( MAKEMAIN ) || defined( MAKETOOL )) */

#define HQX_LEADERFLAG (1)
#define HQX_DLDFLAG (3)

/* NOW ALL THE DEFINITIONS REQUIRED FOR THE ENCRYPTION: */

/** hqx file format and interface to PostScript */

typedef union hqx_tab {
  uint32           words[32];
  unsigned char    chars[128];
} hqxtab;

#define CHARINDEX(n) ((n)&0x7F)
#define STOPLENGTH ( sizeof(hqx_stop_text) - 1 )

/** then follows the seed, 3 bytes of it only */
#define SEEDLENGTH (3)

#define SPECIALSTOPLENGTH ( sizeof(hqx_specstop_text) - 1 )

/** relate the keys used to the seed and key (rip-id) */

/** this gets a value with
 *   v[31-29] = rid[9-7]
 *   v[25-21] = rid[4-0]
 *   v[12-11] = rid[6-5]
 *   v[6-1] = rid[15-10]   and the rest zero.
 */
#define SHUFFLERIPID(x) (       \
  (((x) & 0xFC00 ) >> 9 ) +     \
  (((x) & 0x0380 ) << 22) +     \
  (((x) & 0x0060 ) << 6 ) +     \
  (((x) & 0x001F ) << 21)    )

/** this gets a value suitable for filling in the gaps in the ripid value */
#define SHUFFLESEED(s) (        \
  (((s) & 0x000FFFFF ) ^ ((s) << 11 )) & 0x1C1FE781 )

/** This has been chosen to interact usefully with pieces of ripid
 *  as selected by shuffleripid.
 */
#define EORCONSTANT  (uint32)(0x905e9321)

/** How to get a first value for the sequence from a rip id and seed. */
#define KEY(x,s) ((SHUFFLERIPID(x) + SHUFFLESEED(s)) ^ EORCONSTANT)

/** These are used to initialise the 32 word table. */
#define ROL(e,n) (((uint32)(e)<<(n))|((uint32)(e)>>(32-(n))))
#define NEXT(e) ( ROL((e),23) ^ ROL((e),17) ^ ROL((e),5) )

#define big_end(pp) if ( big_end_flag ) big_end_proc( (pp) );

/** the number from 39 to 70 */
#define SKIPCHARS( id )         (((((id)^((id) >> 11))&31)^18)+39)
#define SKIPLENGTH              ( sizeof(hqx_recognition_text) - 1 )

#ifndef MAKE_HQXLIB

#ifndef DECRYPT30761
/* means not debugging, so camouflage (sp?) */

#define hqx_check_leader        chs_printer_colr
#define my_hqx_check_leader     chs_one_printer_colr
#define hqx_crypt               flag_dirct
#define hqx_crypt_region        do_md_assert2
#define hqx_check_leader_buffer change_to_origin
#define hqx_setup_table         chk_font_setup
#define hqx_setup_postable      chk_loc_font
#define hqxcurrentpos           localcntstat
#define hqxdatastart            extraldflag
#define hqxdataextra            font_dist_int

#endif /* DECRYPT30761 */

/**
 * Set-up the encryption exclusive-or-ing table, and note the leader length
 * \param[in] key  Encryption key
 * \param[in] seed Initial encryption seed
 */
void  hqx_setup_table(int32 key, int32 seed);

/**
 * Precalculate a table containing the position dependent pattern
 * \param[in] pos  Starting position
 * \param[in] flag Table creation mode
 */
void  hqx_setup_postable(int32 pos, int32 flag);

/**
 * Encrypt a single byte
 * \param[in] c    Byte to encrypt
 * \param[in] flag Encryption mode
 * \return         The encrpted byte
 */
uint8 hqx_crypt(uint8 c, int32 flag);

/**
 * Encrpt a block of data
 * \param[in,out] cc      Block of data to encrypt
 * \param[in]     pos     Offset in block
 * \param[in]     length  Length of data to encrypt
 * \param[in]     flag    Encryption mode
 */
void hqx_crypt_region(uint8 *cc, int32 pos, int32 length, int32 flag);

/**
 * Check Leader buffer
 * \param[in]  buffer   Data to be checked
 * \param[out] pos      Index of end of buffer processed
 * \param[in]  key      Initial encryption key
 * \param[out] seedout  Resulting encryption key
 * \return              Success status
 */
int32 hqx_check_leader_buffer(uint8 *buffer, int32 *pos, int32 key, int32 * seedout);

#endif /* MAKE_HQXLIB */

#if defined( MAKEMAIN ) || defined( MAKETOOL )
int32 hqx_write_trailer(FILE *outfile) ;
int32 hqx_check_leader(FILE *infile, int32 security_no, int32 customer_no, int32 *seedout ) ;
int32 my_hqx_check_leader(FILE *infile, int32 key, int32 *seedout ) ;
int32 hqx_write_leader(FILE *outfile, int32 rip_id, int32 seed) ;
#else /* !( defined( MAKEMAIN ) || defined( MAKETOOL )) */

#ifndef MAKE_HQXLIB
int32 hqx_check_leader(struct FILELIST *infile, int32 security_no, int32 customer_no, int32 *seedout) ;
int32 my_hqx_check_leader(struct FILELIST *infile, int32 key, int32 *seedout ) ;
#endif

#endif /* !( defined( MAKEMAIN ) || defined( MAKETOOL )) */

struct core_init_fns ;

void hqx_C_globals(struct core_init_fns *fns) ;

int32 HqxCryptSecurityNo_from_DongleSecurityNo(int32 dongle_security_no) ;
int32 HqxCryptCustomerNo_from_DongleCustomerNo(int32 dongle_customer_no) ;

extern int32 hqxcurrentpos;
extern int32 hqxdatastart;
extern int32 hqxdataextra;

#endif /* protection for multiple inclusion */


/* Log stripped */
