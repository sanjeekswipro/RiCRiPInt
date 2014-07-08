/** \file
 * \ingroup halftone
 *
 * $HopeName: COREhalftone!src:htdisk.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Stores halftone xytables and general info in a disk cache.
 */

#include "core.h"

#include "halftone.h"
#include "chalftone.h"
#include "devices.h" /* osdevice */
#include "namedef_.h"
#include "params.h"
#include "mm.h"
#include "mmcompat.h"
#include "swerrors.h"
#include "swcopyf.h"
#include "security.h" /* fSECAllowLowResHDS, HqxCryptSecurityNo */
#include "gu_hsl.h" /* encrypt_halftone */
#include "dlstate.h" /* ydpi */
#include "control.h" /* handleLowMemory */

#include "htpriv.h"


/** Equivalent to "hhcf" on big-endian machines. */
#define HALFTONECACHE_NORMAL         0x68686366u

/** Equivalent to "hhcf" on little-endian machines. */
#define HALFTONECACHE_NORMAL_SWAP    0x66636868u

/** Equivalent to "hhce" on big-endian machines. */
#define HALFTONECACHE_ENCRYPTED      0x68686365u

/** Equivalent to "hhce" on little-endian machines. */
#define HALFTONECACHE_ENCRYPTED_SWAP 0x65636868u

/** Note this version number is meant to be the release version number,
  * so that we can define intermediate values if we so wish/need to.
  */
#define HALFTONECACHE_VERSION 6000u
/* The accurateScreen field could be moved to the flag byte at the next
   incompatible change. */


/* Because we need to remain backward-compatible and I want to keep
   halftone caches portable amongst all platforms (including 32 and
   64 bit), we need to store pointers as 32-bit values. They are not
   used on load so this is fine on 64-bit platforms.  --johnk */
#define CHALFTONE_ON_DISK_POINTER uint32

/* This structure is used as an intermediate fixed size structure to
   read and write halftone caches to/from disk. The structure size
   and all field positions MUST match the CHALFTONE structure below
   excatly for all platforms as if though the platform were a 32 bit
   platform. This is because someone decided to dump the C structure
   directly to disk and we need to remain backward compatible. */

typedef struct CHALFTONE_ON_DISK {
  SYSTEMVALUE               freqv ;
  SYSTEMVALUE               anglv ;
  SYSTEMVALUE               ofreq ;
  SYSTEMVALUE               oangle ;
  SYSTEMVALUE               dfreq ;
  SYSTEMVALUE               efreq ;
  SYSTEMVALUE               eangle ;
  SYSTEMVALUE               supcell_ratio ;
  CHALFTONE_ON_DISK_POINTER halfys ;
  CHALFTONE_ON_DISK_POINTER xcoords ;
  CHALFTONE_ON_DISK_POINTER ycoords ;
  CHALFTONE_ON_DISK_POINTER thxfer ;
  CHALFTONE_ON_DISK_POINTER levels ;
  CHALFTONE_ON_DISK_POINTER usage ;
  CHALFTONE_ON_DISK_POINTER forms ;
  CHALFTONE_ON_DISK_POINTER pattern_form ;
  CHALFTONE_ON_DISK_POINTER sfname ;
  CHALFTONE_ON_DISK_POINTER formclass ;
  CHALFTONE_ON_DISK_POINTER prev_chptr ;
  CHALFTONE_ON_DISK_POINTER next_chptr ;
  CHALFTONE_ON_DISK_POINTER spare4 ;
  int32                     halftype;
  int32                     halfrotate ;
  int32                     halfxdims ;
  int32                     halfydims ;
  int32                     halfexdims ;
  int32                     halfeydims ;
  int32                     halfmxdims ;
  int32                     halfmydims ;
  int32                     halfr1 ;
  int32                     halfr2 ;
  int32                     halfr3 ;
  int32                     halfr4 ;
  int32                     halfrepeatb ;
  int32                     spare5 ;
  int32                     num_deferred ;
  CHALFTONE_ON_DISK_POINTER path ;
  uint32                    duplicate ;
  int32                     accurateScreen ;
  int32                     supcell_multiplesize ;
  int32                     supcell_actual ;
  int32                     supcell_remainder ;
  int32                     screen_index ;
  uint16                    notones ;
  int16                     spare3 ;
  uint16                    lock ;
  uint16                    maxthxfer ;
  int16                     screenmark ;
  int16                     reportme ;
  int8                      reported ;
  int8                      reportcolor ;
  int8                      spare1;
  uint8                     flags;
  int8                      hpstwo ;
  int8                      screenprotection ;
  uint8                     depth_shift;
  uint8                     spare2;
} CHALFTONE_ON_DISK ;


/* Some convenience macros for the COPY_CHALFTONE_ON_DISK_TO_CHALFTONE
   macro. */
#define CH_COPY_FIELD(from, to, field) (((to)->field) = ((from)->field))
#define CH_COPY_PTR_FIELD(from, to, field) \
  (((to)->field) = ((void *)((uintptr_t)((from)->field))))
#define CH_NULL_PTR_FIELD(from, to, field) \
  (((to)->field) = ((int32)((intptr_t)NULL)))

#define COPY_CHALFTONE_ON_DISK_TO_CHALFTONE(ch_on_disk, ch) \
  CH_COPY_FIELD(ch_on_disk, ch, freqv) ; \
  CH_COPY_FIELD(ch_on_disk, ch, anglv) ; \
  CH_COPY_FIELD(ch_on_disk, ch, ofreq) ; \
  CH_COPY_FIELD(ch_on_disk, ch, oangle) ; \
  CH_COPY_FIELD(ch_on_disk, ch, dfreq) ; \
  CH_COPY_FIELD(ch_on_disk, ch, efreq) ; \
  CH_COPY_FIELD(ch_on_disk, ch, eangle) ; \
  CH_COPY_FIELD(ch_on_disk, ch, supcell_ratio) ; \
  CH_COPY_PTR_FIELD(ch_on_disk, ch, halfys) ; \
  CH_COPY_PTR_FIELD(ch_on_disk, ch, xcoords) ; \
  CH_COPY_PTR_FIELD(ch_on_disk, ch, ycoords) ; \
  CH_COPY_PTR_FIELD(ch_on_disk, ch, thxfer) ; \
  CH_COPY_PTR_FIELD(ch_on_disk, ch, levels) ; \
  CH_COPY_PTR_FIELD(ch_on_disk, ch, usage) ; \
  CH_COPY_PTR_FIELD(ch_on_disk, ch, forms) ; \
  CH_COPY_PTR_FIELD(ch_on_disk, ch, pattern_form) ; \
  CH_COPY_PTR_FIELD(ch_on_disk, ch, sfname) ; \
  CH_COPY_PTR_FIELD(ch_on_disk, ch, formclass) ; \
  CH_COPY_PTR_FIELD(ch_on_disk, ch, prev_chptr) ; \
  CH_COPY_PTR_FIELD(ch_on_disk, ch, next_chptr) ; \
  CH_COPY_PTR_FIELD(ch_on_disk, ch, spare4) ; \
  CH_COPY_FIELD(ch_on_disk, ch, halftype) ; \
  CH_COPY_FIELD(ch_on_disk, ch, halfrotate) ; \
  CH_COPY_FIELD(ch_on_disk, ch, halfxdims) ; \
  CH_COPY_FIELD(ch_on_disk, ch, halfydims) ; \
  CH_COPY_FIELD(ch_on_disk, ch, halfexdims) ; \
  CH_COPY_FIELD(ch_on_disk, ch, halfeydims) ; \
  CH_COPY_FIELD(ch_on_disk, ch, halfmxdims) ; \
  CH_COPY_FIELD(ch_on_disk, ch, halfmydims) ; \
  CH_COPY_FIELD(ch_on_disk, ch, halfr1) ; \
  CH_COPY_FIELD(ch_on_disk, ch, halfr2) ; \
  CH_COPY_FIELD(ch_on_disk, ch, halfr3) ; \
  CH_COPY_FIELD(ch_on_disk, ch, halfr4) ; \
  CH_COPY_FIELD(ch_on_disk, ch, halfrepeatb) ; \
  CH_COPY_FIELD(ch_on_disk, ch, spare5) ; \
  CH_COPY_FIELD(ch_on_disk, ch, num_deferred) ; \
  CH_COPY_PTR_FIELD(ch_on_disk, ch, path) ; \
  CH_COPY_FIELD(ch_on_disk, ch, duplicate) ; \
  CH_COPY_FIELD(ch_on_disk, ch, accurateScreen) ; \
  CH_COPY_FIELD(ch_on_disk, ch, supcell_multiplesize) ; \
  CH_COPY_FIELD(ch_on_disk, ch, supcell_actual) ; \
  CH_COPY_FIELD(ch_on_disk, ch, supcell_remainder) ; \
  CH_COPY_FIELD(ch_on_disk, ch, screen_index) ; \
  CH_COPY_FIELD(ch_on_disk, ch, notones) ; \
  CH_COPY_FIELD(ch_on_disk, ch, spare3) ; \
  CH_COPY_FIELD(ch_on_disk, ch, lock) ; \
  CH_COPY_FIELD(ch_on_disk, ch, maxthxfer) ; \
  CH_COPY_FIELD(ch_on_disk, ch, screenmark) ; \
  CH_COPY_FIELD(ch_on_disk, ch, reportme) ; \
  CH_COPY_FIELD(ch_on_disk, ch, reported) ; \
  CH_COPY_FIELD(ch_on_disk, ch, reportcolor) ; \
  CH_COPY_FIELD(ch_on_disk, ch, spare1) ; \
  CH_COPY_FIELD(ch_on_disk, ch, flags) ; \
  CH_COPY_FIELD(ch_on_disk, ch, hpstwo) ; \
  CH_COPY_FIELD(ch_on_disk, ch, screenprotection) ; \
  CH_COPY_FIELD(ch_on_disk, ch, depth_shift) ; \
  CH_COPY_FIELD(ch_on_disk, ch, spare2) ;

#define COPY_CHALFTONE_TO_CHALFTONE_ON_DISK(ch, ch_on_disk) \
  CH_COPY_FIELD(ch, ch_on_disk, freqv) ; \
  CH_COPY_FIELD(ch, ch_on_disk, anglv) ; \
  CH_COPY_FIELD(ch, ch_on_disk, ofreq) ; \
  CH_COPY_FIELD(ch, ch_on_disk, oangle) ; \
  CH_COPY_FIELD(ch, ch_on_disk, dfreq) ; \
  CH_COPY_FIELD(ch, ch_on_disk, efreq) ; \
  CH_COPY_FIELD(ch, ch_on_disk, eangle) ; \
  CH_COPY_FIELD(ch, ch_on_disk, supcell_ratio) ; \
  CH_NULL_PTR_FIELD(ch, ch_on_disk, halfys) ; \
  CH_NULL_PTR_FIELD(ch, ch_on_disk, xcoords) ; \
  CH_NULL_PTR_FIELD(ch, ch_on_disk, ycoords) ; \
  CH_NULL_PTR_FIELD(ch, ch_on_disk, thxfer) ; \
  CH_NULL_PTR_FIELD(ch, ch_on_disk, levels) ; \
  CH_NULL_PTR_FIELD(ch, ch_on_disk, usage) ; \
  CH_NULL_PTR_FIELD(ch, ch_on_disk, forms) ; \
  CH_NULL_PTR_FIELD(ch, ch_on_disk, pattern_form) ; \
  CH_NULL_PTR_FIELD(ch, ch_on_disk, sfname) ; \
  CH_NULL_PTR_FIELD(ch, ch_on_disk, formclass) ; \
  CH_NULL_PTR_FIELD(ch, ch_on_disk, prev_chptr) ; \
  CH_NULL_PTR_FIELD(ch, ch_on_disk, next_chptr) ; \
  CH_NULL_PTR_FIELD(ch, ch_on_disk, spare4) ; \
  CH_COPY_FIELD(ch, ch_on_disk, halftype) ; \
  CH_COPY_FIELD(ch, ch_on_disk, halfrotate) ; \
  CH_COPY_FIELD(ch, ch_on_disk, halfxdims) ; \
  CH_COPY_FIELD(ch, ch_on_disk, halfydims) ; \
  CH_COPY_FIELD(ch, ch_on_disk, halfexdims) ; \
  CH_COPY_FIELD(ch, ch_on_disk, halfeydims) ; \
  CH_COPY_FIELD(ch, ch_on_disk, halfmxdims) ; \
  CH_COPY_FIELD(ch, ch_on_disk, halfmydims) ; \
  CH_COPY_FIELD(ch, ch_on_disk, halfr1) ; \
  CH_COPY_FIELD(ch, ch_on_disk, halfr2) ; \
  CH_COPY_FIELD(ch, ch_on_disk, halfr3) ; \
  CH_COPY_FIELD(ch, ch_on_disk, halfr4) ; \
  CH_COPY_FIELD(ch, ch_on_disk, halfrepeatb) ; \
  CH_COPY_FIELD(ch, ch_on_disk, spare5) ; \
  CH_COPY_FIELD(ch, ch_on_disk, num_deferred) ; \
  CH_NULL_PTR_FIELD(ch, ch_on_disk, path) ; \
  CH_COPY_FIELD(ch, ch_on_disk, duplicate) ; \
  CH_COPY_FIELD(ch, ch_on_disk, accurateScreen) ; \
  CH_COPY_FIELD(ch, ch_on_disk, supcell_multiplesize) ; \
  CH_COPY_FIELD(ch, ch_on_disk, supcell_actual) ; \
  CH_COPY_FIELD(ch, ch_on_disk, supcell_remainder) ; \
  CH_COPY_FIELD(ch, ch_on_disk, screen_index) ; \
  CH_COPY_FIELD(ch, ch_on_disk, notones) ; \
  CH_COPY_FIELD(ch, ch_on_disk, spare3) ; \
  CH_COPY_FIELD(ch, ch_on_disk, lock) ; \
  CH_COPY_FIELD(ch, ch_on_disk, maxthxfer) ; \
  CH_COPY_FIELD(ch, ch_on_disk, screenmark) ; \
  CH_COPY_FIELD(ch, ch_on_disk, reportme) ; \
  CH_COPY_FIELD(ch, ch_on_disk, reported) ; \
  CH_COPY_FIELD(ch, ch_on_disk, reportcolor) ; \
  CH_COPY_FIELD(ch, ch_on_disk, spare1) ; \
  CH_COPY_FIELD(ch, ch_on_disk, flags) ; \
  CH_COPY_FIELD(ch, ch_on_disk, hpstwo) ; \
  CH_COPY_FIELD(ch, ch_on_disk, screenprotection) ; \
  CH_COPY_FIELD(ch, ch_on_disk, depth_shift) ; \
  CH_COPY_FIELD(ch, ch_on_disk, spare2) ;


static uint8 *hds_suffixes[] = {
  (uint8 *)"-0",
  (uint8 *)"-1",
  (uint8 *)"-2",
  (uint8 *)"-3",
  (uint8 *)"-0"
} ;


Bool build_hds_suffix(uint8 *hds_suffix, size_t hds_suffix_size,
                      NAMECACHE *sfcolor,
                      int32 detail_name, int32 detail_index)
{
  if ( detail_name == NAME_HDS )
    swcopyf( hds_suffix, (uint8 *)"%s", hds_suffixes[detail_index] );
  else if ( sfcolor ) {
    /* Too long to cache on disk?  Allow for a byte for null termination,
       hence -2.  Cast to compare even though len field is too small. */
    if ( (size_t)sfcolor->len > hds_suffix_size - 2 )
      return FALSE;
    swcopyf( hds_suffix, (uint8 *)"/%.*s", sfcolor->len, sfcolor->clist);
  }
  else
    hds_suffix[ 0 ] = '\0';
  return TRUE;
}


static uint8 screenFilePat[] = "Screens/%s%s%s/%02X%02X%02X%02X.%03X";

static uint8 screenFileWildcard[] = "Screens/%s%s%s/%02X%02X%02X%02X.*";


#define SECURITY_LOCK(c_,x_,y_,n_) ((uint16)(( (n_) + \
        theIHalfR1(c_) + theIHalfR3 (c_) + \
        (x_) [0] + (y_) [0]) ^ 0x9999))


static void cleanupDiskHtCacheLoad(DEVICE_FILEDESCRIPTOR tmpfd , uint8 *tmpbuf )
{
  if ( tmpfd >= 0 )
    (void)(*theICloseFile(osdevice))( osdevice , tmpfd ) ;

  /* tmpbuf only gets set on a read, implying that the read call failed
   * for some reason. This may be due to a corrupt file or due to something
   * else like an interrupt. We only delete the file if it might be the
   * corrupt file case.
   */
  if ( tmpbuf ) {
    switch ((*theILastErr(osdevice))( osdevice )) {
      /* On one of these errors we shouldn't delete the cache. */
    case DeviceInvalidAccess :
    case DeviceInterrupted:
    case DeviceLimitCheck :
    case DeviceTimeout:
     break ;

    case DeviceUndefined :
      HQFAIL("DeviceUndefined error when reading halftone cache(!?!)" ) ;
      break ;
    case DeviceUnregistered :
      HQFAIL("DeviceUnregistered error when deleting halftone cache" ) ;
      break ;
    case DeviceNoError : /* Possibly implies that the file got truncated. */
    case DeviceIOError : /* Possibly implies that error reading file. */
    default:
      ( void )(*theIDeleteFile(osdevice))( osdevice , tmpbuf ) ;
    }
  }
}


/** Macro to help build a constant table of field offsets and sizes; offsets
    are required in case of internal structure padding. */
#define CHFIELD(f) /* Note - no parens on macro expansion */          \
  {                                                                   \
    (uint16)((char*)&((CHALFTONE*)0)->f - (char*)0), /*offsetof(f)*/  \
    (uint16)sizeof(((CHALFTONE*)0)->f)                                \
  }

/** Swap the byte order of a CHALFTONE structure and its associated arrays. */
static void byte_swap_chalftone(CHALFTONE *chptr)
{
  uint32 i, size, offset = 0 ;

  static const struct {
    uint16 offset, size ;
  } CHALFTONE_fields[] = {
    CHFIELD(freqv), CHFIELD(anglv),
    CHFIELD(ofreq), CHFIELD(oangle),
    CHFIELD(dfreq),
    CHFIELD(efreq), CHFIELD(eangle),
    CHFIELD(supcell_ratio),
    CHFIELD(halfys), /* Not needed */
    CHFIELD(xcoords), CHFIELD(ycoords), /* Not needed */
    CHFIELD(thxfer), /* Needed for threshold array test. */
    CHFIELD(levels), CHFIELD(usage), CHFIELD(forms), /* Not needed */
    CHFIELD(pattern_form), /* Not needed */
    CHFIELD(sfname), /* Not needed */
    CHFIELD(formclass), /* Not needed */
    CHFIELD(prev_chptr), CHFIELD(next_chptr), CHFIELD(spare4), /* Not needed */
    CHFIELD(halftype),
    CHFIELD(halfrotate),
    CHFIELD(halfxdims), CHFIELD(halfydims),
    CHFIELD(halfexdims), CHFIELD(halfeydims),
    CHFIELD(halfmxdims), CHFIELD(halfmydims),
    CHFIELD(halfr1), CHFIELD(halfr2), CHFIELD(halfr3), CHFIELD(halfr4),
    CHFIELD(halfrepeatb),
    CHFIELD(spare5), CHFIELD(num_deferred),
    CHFIELD(path), /* Not needed */
    CHFIELD(duplicate),
    CHFIELD(accurateScreen),
    CHFIELD(supcell_multiplesize),
    CHFIELD(supcell_actual),
    CHFIELD(supcell_remainder),
    CHFIELD(screen_index),
    CHFIELD(notones),
    CHFIELD(spare3),
    CHFIELD(lock),
    CHFIELD(maxthxfer),
    CHFIELD(screenmark),
    CHFIELD(reportme), CHFIELD(reported), CHFIELD(reportcolor),
    CHFIELD(spare1),
    CHFIELD(flags),
    CHFIELD(hpstwo),
    CHFIELD(screenprotection),
    CHFIELD(depth_shift),
    CHFIELD(spare2),
    { 0, 0 }
  } ;

  HQASSERT(sizeof(CHALFTONE) < 65536 /* 2^16 */,
           "Halftone offset too small.") ;

  for ( i = 0 ; (size = CHALFTONE_fields[i].size) != 0 ; ++i ) {
    uint8 *lo, *hi ;

    /* ANSI C allows padding between and after fields. We're going to assert
       that this isn't the case, because screen caches from different
       compilers won't load if they don't match in size and padding. */
    HQASSERT(offset == CHALFTONE_fields[i].offset,
             "Structure alignment/packing does not match expected.") ;

    /* Byte-swap the useful part of the field. */
    lo = (uint8 *)chptr + CHALFTONE_fields[i].offset ;
    hi = lo + size ;

    while ( --hi > lo ) {
      uint8 tmp = *lo ;
      *lo++ = *hi ;
      *hi = tmp ;
    }

    /* Set end of field. */
    offset = CHALFTONE_fields[i].offset + size ;
  }

  /* We haven't taken into account any padding at the end of the structure,
     which C may put there. It's hard to do this without negating the
     usefulness of this assert. */
  HQASSERT(offset == sizeof(CHALFTONE),
           "Size of halftone structure differs from expected") ;
}


/*
  Encryption order: Reading;

  (1) Load from disk
  (2) Decrypt (ie get to Windows format Screen Cache)
  (3) Fix byte ordering if needed
  (4) RIP-native screen cache

  Encryption order: Writing;

   (1) RIP-native screen cache
   (2) Encrypt
   (3) Save to disk
 */


/* ---------------------------------------------------------------------- */
typedef struct {
  CHALFTONE_ON_DISK ch_ondisk_ptr ;
  uint8 sc_passkey[PASS_LENGTH] ;
} CHALFTONE_ON_DISK_KEYED ;


/** Saves a precompiled halftone on disk. */
Bool saveHalftoneToDisk( corecontext_t *context,
                         CHALFTONE *chptr,
                         NAMECACHE *name,
                         NAMECACHE *sfcolor, uint8 objtype,
                         int32 detail_name, int32 detail_index )
{
  DEVICE_FILEDESCRIPTOR tmpfd ;
  uint32  filecacheindex;
  uint32  ftype = HALFTONECACHE_NORMAL;
  uint8   tmpbuf[ LONGESTFILENAME + 2 + LONGESTSCREENNAME + sizeof(screenFilePat) + 1 ];
    /* Long enough since each field in screenFilePat is shorter than the
       pattern that represents it. +1 for the terminator. */
  uint8  nambuf[ LONGESTSCREENNAME ] ;
  uint8 hds_suffix[ LONGESTFILENAME ] ;
  uint8 type_suffix[3];
  int32 local_encryption_level = context->systemparams->EncryptScreens;
  int r1, r2, r3, r4;

  HQASSERT((theITHXfer(chptr) == NULL && theIMaxTHXfer(chptr) == 0) ||
           (theITHXfer(chptr) != NULL && theIMaxTHXfer(chptr) != 0),
           "saveHtToDsk: Threshold pointer and size out of step");

  /* Ensure we comply with any minimum encryption requirement of the screen */
  switch (chptr->screenprotection & SCREENPROT_ENCRYPT_MASK) {
  case SCREENPROT_ENCRYPT_NONE:
    /* In this case, we may now want to enforce some encryption */
    switch (local_encryption_level) {
    case 0:
        /* sysparam isn't asking for any */
        break;
    case 1:
    default:
      chptr->screenprotection |= SCREENPROT_ENCRYPT_ANYHQN;
      break;
    case 2:
      chptr->screenprotection |= SCREENPROT_ENCRYPT_CUSTOMER;
      break;
    case 3:
      chptr->screenprotection |= SCREENPROT_ENCRYPT_DONGLE;
      break;
    }
    break;
  case SCREENPROT_ENCRYPT_ANYHQN:
    if (local_encryption_level < 1) local_encryption_level = 1;
    break;
  case SCREENPROT_ENCRYPT_CUSTOMER:
    if (local_encryption_level < 2) local_encryption_level = 2;
    break;
  case SCREENPROT_ENCRYPT_DONGLE:
  default:
    if (local_encryption_level < 3) local_encryption_level = 3;
    break;
  }

  if (local_encryption_level != 0) {
    ftype = HALFTONECACHE_ENCRYPTED; /* Set this on the pre-byte swap side */
  }
  if ( !build_hds_suffix(hds_suffix, sizeof(hds_suffix),
                         sfcolor, detail_name, detail_index) )
    return error_handler(UNDEFINED);
  if ( objtype != HTTYPE_ALL )
    swcopyf(type_suffix, (uint8*)"/%1u", objtype);
  else
    type_suffix[0] = '\0';
  mknamestr(nambuf, name) ;

  /* Use unscaled dimensions as the keys */
  r1 = (chptr->halfr1 >> chptr->depth_shift) & 255;
  r2 = chptr->halfr2 & 255;
  r3 = chptr->halfr3 & 255;
  r4 = (chptr->halfr4 >> chptr->depth_shift) & 255;
  /* Use unrotated dimensions as the keys */
  if ( chptr->thxfer != NULL && fmod(chptr->oangle, 180.0) == 90.0 ) {
    int32 tmp; /* orientation did an x<->y swap */
    tmp = r1; r1 = r3; r3 = tmp;
    tmp = r2; r2 = r4; r4 = tmp;
  }
  /* Find a unique name for the file */
  for (filecacheindex = 1 ; ; filecacheindex++ ) {
    STAT stat;

    swcopyf( tmpbuf ,
             (uint8 *) screenFilePat ,
             nambuf, type_suffix, hds_suffix, r1, r2, r3, r4,
             filecacheindex );
    if ((* theIStatusFile(osdevice)) (osdevice, tmpbuf, & stat) != 0)
      break;
  }
  HQTRACE(debug_halftonecache, ("saveHtToDsk: %s", tmpbuf));

  /* Caches encrypted for a customer-number/Hqn should be sharable between RIPs,
     whereas un-encrypted or dongle-number-encrypted ones should not. */
  chptr->lock = SECURITY_LOCK(chptr, theIXCoords(chptr), theIYCoords(chptr),
                              HqxCryptSecurityNo());
  /* The first assignment (above) is always done, for slightly more obscure assemblese */
  switch (chptr->screenprotection & SCREENPROT_ENCRYPT_MASK) {
  case SCREENPROT_ENCRYPT_ANYHQN:
    chptr->lock = SECURITY_LOCK(chptr, theIXCoords(chptr), theIYCoords(chptr),
                                0x3b);
    break;

  case SCREENPROT_ENCRYPT_CUSTOMER:
    chptr->lock = SECURITY_LOCK(chptr, theIXCoords(chptr), theIYCoords(chptr),
                                HqxCryptCustomerNo());
    break;

  case SCREENPROT_ENCRYPT_NONE:
  case SCREENPROT_ENCRYPT_DONGLE:
  default:
    break;
  }

  tmpfd = (*theIOpenFile(osdevice))( osdevice, tmpbuf ,
                                     SW_RDWR | SW_CREAT | SW_TRUNC );

  if ( tmpfd == EOF )
    return device_error_handler(osdevice);
  else {
    CHALFTONE_ON_DISK_KEYED chsave_ondisk = {0};
    int32 res, gridsize;
    uint32 written = 0, writesize ;
    uint32 version = HALFTONECACHE_VERSION ;
    uint8 sc_passkey[ PASS_LENGTH ] = {0};

    writesize = 2 * sizeof(uint32) ;
    written += (*theIWriteFile(osdevice))(osdevice , tmpfd ,
                                          ( uint8 * )( & ftype ) ,
                                          sizeof(uint32)) ;
    written += (*theIWriteFile(osdevice))(osdevice , tmpfd ,
                                          ( uint8 * )( & version ) ,
                                          sizeof(uint32)) ;


    /* The disk structure is used for distributing encrypted screen caches. It
       must be reliable for distributing to systems other than the one on which
       the caches were created. In particular, ensure that padding won't cause
       problems. */
    HQASSERT(sizeof(CHALFTONE_ON_DISK) % 16 == 0 &&
             sizeof(CHALFTONE_ON_DISK_KEYED) % 16 == 0,
             "The disk halftone structure must be compatible for all systems");

    /* Copy the cache structure and clear the pointers and run-time
       fields in it before saving. Note that the copy macro NULL's all
       pointer fields. */
    COPY_CHALFTONE_TO_CHALFTONE_ON_DISK(chptr, &chsave_ondisk.ch_ondisk_ptr)

    if (chptr->thxfer) {  /* this allows for the non-NULL thxfer check to work */
      chsave_ondisk.ch_ondisk_ptr.thxfer = 1 ;
    }

    /* Shouldn't save temporary fields */
    chsave_ondisk.ch_ondisk_ptr.reported = 0 ;

    if ( local_encryption_level != 0 ) {
      uint16 local_key_no = 0x3b;

      if (local_encryption_level == 2)
        local_key_no = (uint16)HqxCryptCustomerNo();

      if (local_encryption_level == 3)
        local_key_no = (uint16)HqxCryptSecurityNo();

      /* This call sets up sc_passkey, and adds a copy after CHALFTONE before
         encrypting. */
      (void)encrypt_halftone((uint8 *)&chsave_ondisk,
                             sizeof(CHALFTONE_ON_DISK_KEYED),
                             sc_passkey, local_key_no, ENCRYPT_FIRST) ;
    }

    writesize += sizeof(CHALFTONE_ON_DISK) ;
    written += (*theIWriteFile(osdevice))(osdevice , tmpfd ,
                                          (uint8 *)&chsave_ondisk.ch_ondisk_ptr, sizeof(CHALFTONE_ON_DISK)) ;
    if ( local_encryption_level != 0 ) {
      writesize += sizeof(chsave_ondisk.sc_passkey) ;
      written += (*theIWriteFile(osdevice))(osdevice , tmpfd ,
                                            (uint8 *)&chsave_ondisk.sc_passkey, sizeof(chsave_ondisk.sc_passkey)) ;
    }

    if ( theITHXfer( chptr )) {
      int32 size = sizeof(int32) * (theIMaxTHXfer(chptr) + 1) ;

      /* Encrypt transfer array before and decrypt after saving. */
      if ( local_encryption_level != 0 ) {
        (void)encrypt_halftone((uint8 *)theITHXfer(chptr), size,
                               sc_passkey, 0, EN_DE_CRYPT_NEXT);
      }

      writesize += size ;
      written += (*theIWriteFile(osdevice))(osdevice, tmpfd,
                                            (uint8 *)theITHXfer(chptr), size) ;

      if ( local_encryption_level != 0 ) {
        (void)encrypt_halftone((uint8 *)theITHXfer(chptr), size,
                               sc_passkey, 0, EN_DE_CRYPT_NEXT);
      }
    }
    gridsize = theISuperCellActual( chptr ) * sizeof( int16 ) ;
    if (gridsize != 0) {
      /* Encrypt grid before and decrypt after saving. */

      if ( local_encryption_level != 0 ) {
        (void)encrypt_halftone(( uint8 * )theIXCoords( chptr ),
                               gridsize,
                               sc_passkey, 0, EN_DE_CRYPT_NEXT);

        (void)encrypt_halftone(( uint8 * )theIYCoords( chptr ),
                               gridsize,
                               sc_passkey, 0, EN_DE_CRYPT_NEXT);
      }

      writesize += gridsize + gridsize ;
      written += (*theIWriteFile(osdevice))( osdevice ,  tmpfd ,
                                             ( uint8 * )theIXCoords( chptr ) ,
                                             gridsize ) ;
      written += (*theIWriteFile(osdevice))( osdevice ,  tmpfd ,
                                             ( uint8 * )theIYCoords( chptr ) ,
                                             gridsize ) ;

      if ( local_encryption_level != 0 ) {
        (void)encrypt_halftone(( uint8 * )theIXCoords( chptr ),
                               gridsize,
                               sc_passkey, 0, EN_DE_CRYPT_NEXT);

        (void)encrypt_halftone(( uint8 * )theIYCoords( chptr ),
                               gridsize,
                               sc_passkey, 0, EN_DE_CRYPT_NEXT);
      }
    }
    res = (*theICloseFile(osdevice))( osdevice , tmpfd ) ;

    if ( res < 0 || written != writesize ) {
      (void)device_error_handler(osdevice);
      (void)(*theIDeleteFile(osdevice))( osdevice , tmpbuf ) ;
      return FALSE;
    }
    chptr->path = (uint8 *)mm_alloc_with_header(mm_pool_temp,
                                                strlen((char *) tmpbuf) + 1,
                                                MM_ALLOC_CLASS_HALFTONE_PATH);
    if (chptr->path == NULL)
      return error_handler(VMERROR);

    (void)strcpy((char *)chptr->path, (char *)tmpbuf);

    return TRUE;
  }
  /* not reached */
}


int32 loadHalftoneFromDisk(corecontext_t *context,
                           SPOTNO spotno, HTTYPE objtype, COLORANTINDEX color,
                           CHALFTONE *ch_template,
                           uint8 depth_shift, uint8 default_depth_shift,
                           SYSTEMVALUE orientation,
                           NAMECACHE *htname,
                           NAMECACHE *sfcolor,
                           NAMECACHE *alternativeName,
                           NAMECACHE *alternativeColor,
                           HTTYPE cacheType,
                           int32 detail_name, int32 detail_index,
                           int32 phasex, int32 phasey)
{
  DEVICE_FILEDESCRIPTOR tmpfd ;
  int32   ftype = 0;
  int32   txfercnt = 0 ;
  int32   gridsize ;
  int16  *xcoords ;
  int16  *ycoords ;
  uint32 *txfer = NULL;
  CHALFTONE ch = { 0 } ;
  CHALFTONE *chptr = &ch ;
  CHALFTONE *ch_new = NULL;
  uint8   tmpbuf[ LONGESTFILENAME + 2 + LONGESTSCREENNAME + sizeof(screenFilePat) + 1 ] ;
          /* now long enough since each substituted field in screenFilePat
             is shorter than the pattern that represents it + 1 for the NUL. */
  uint8   patbuf[ sizeof (tmpbuf) ];
  uint8   nambuf[ LONGESTSCREENNAME ] ;
  uint8   hds_suffix[ LONGESTFILENAME ] ;
  uint8   type_suffix[3];
  FILEENTRY fileentry;
  void *  handle;
  uint8   sc_passkey[ PASS_LENGTH ] = { 0 };
  Bool byteswap = FALSE ;
  Bool needs_depth_adjustment;

  /* These mult-allocs will be WITH HEADER because they are variable size */
  mm_addr_t memAllocHdrResult[ 3 ] ;
  static mm_alloc_class_t memAllocHdrClass[ 3 ] = {
    MM_ALLOC_CLASS_HALFTONE_XY,
    MM_ALLOC_CLASS_HALFTONE_PATH,
    MM_ALLOC_CLASS_TRANSFER_ARRAY,
  } ;
  static mm_size_t memAllocHdrSize[ 3 ] ;
  size_t hdrCount;

  HQASSERT (sfcolor != NULL, "new world expects sfcolor to be set");

  if (alternativeColor == NULL)
    alternativeColor = sfcolor;
  if (alternativeName == NULL)
    alternativeName = ch_template->sfname;

  ch_template->reportcolor = (detail_name == NAME_HDS) ? (int8)detail_index : -1;
  if ( !build_hds_suffix(hds_suffix, sizeof(hds_suffix), alternativeColor,
                         detail_name, detail_index) )
    return 0;
  if ( cacheType != HTTYPE_ALL )
    swcopyf(type_suffix, (uint8*)"/%1u", cacheType);
  else
    type_suffix[0] = '\0';
  mknamestr(nambuf, alternativeName) ;

  tmpfd = EOF;

  /* Use unscaled and unrotated dimensions as the keys */
  swcopyf (patbuf, (uint8 *)screenFileWildcard, nambuf, type_suffix, hds_suffix,
           ch_template->halfr1 & 255,
           ch_template->halfr2 & 255, ch_template->halfr3 & 255,
           ch_template->halfr4 & 255);
  HQTRACE(debug_halftonecache,
          ("loadHalftoneFromDisk: %d %u %d %s", spotno, objtype, color, patbuf));
  handle = (* theIStartList(osdevice))(osdevice, patbuf);
  if (handle == NULL)
    return 0;

  for (;;) {

    if ((* theINextList(osdevice)) (osdevice, & handle, patbuf, & fileentry) !=
        FileNameMatch)
      break;

    (void)strncpy((char *) tmpbuf, (char *) fileentry.name, fileentry.namelength);
    tmpbuf [fileentry.namelength] = 0;

    tmpfd = (*theIOpenFile(osdevice))(osdevice, tmpbuf, SW_RDONLY);

    if (tmpfd != EOF) {
      CHALFTONE_ON_DISK_KEYED chload = { 0 } ;
      Bool success = TRUE;
      uint32 version = 0 ;

      /* The disk structure is used for distributing encrypted screen caches. It
         must be reliable for distributing to systems other than the one on which
         the caches were created. In particular, ensure that padding won't cause
         problems. */
      HQASSERT(sizeof(CHALFTONE_ON_DISK) % 16 == 0 &&
               sizeof(CHALFTONE_ON_DISK_KEYED) % 16 == 0,
               "The disk halftone structure must be compatible for all systems");

      if ( (*theIReadFile(osdevice))(osdevice, tmpfd, (uint8*)&ftype, sizeof(uint32)) != sizeof(uint32) ||
           (*theIReadFile(osdevice))(osdevice, tmpfd, (uint8*)&version, sizeof(uint32)) != sizeof(uint32) ||
           (*theIReadFile(osdevice))(osdevice, tmpfd, (uint8*)&chload.ch_ondisk_ptr, sizeof(CHALFTONE_ON_DISK)) != sizeof(CHALFTONE_ON_DISK) ) {
        success = FALSE;
      }
      else {
        /* Check encryption before byte-swapping. */
        switch ( ftype ) {
        case HALFTONECACHE_ENCRYPTED_SWAP:
          byteswap = TRUE ;
          /*@fallthrough@*/
        case HALFTONECACHE_ENCRYPTED:
          /* Additional 16 bytes needed for checking correct password. */
          if ( (*theIReadFile(osdevice))(osdevice, tmpfd,
                                         (uint8 *)(&chload.sc_passkey),
                                         sizeof(chload.sc_passkey)) != sizeof(chload.sc_passkey) ) {
            /* Failed to read the extra password data. */
            success = FALSE ;
          } else {
            CHALFTONE_ON_DISK_KEYED chtemp ;
            uint16  keys[ 3 ];
            int32   i = 0;

            keys[0] = (uint16)HqxCryptSecurityNo();
            keys[1] = (uint16)HqxCryptCustomerNo();
            keys[2] = 0x3b;  /* Generic Key */
            do {
              chtemp = chload ;
              /* This call to encrypt_halftone always sets up sc_passkey. */
              success = encrypt_halftone((uint8 *)&chtemp,
                                         sizeof(CHALFTONE_ON_DISK_KEYED),
                                         sc_passkey, keys[ i ], DECRYPT_FIRST);
            } while ( !success && ++i < 3 );

            if ( success ) /* Copy decrypted cache back to loaded area. */
              chload = chtemp ;
          }
          break ;
        case HALFTONECACHE_NORMAL_SWAP:
          byteswap = TRUE ;
          /*@fallthrough@*/
        case HALFTONECACHE_NORMAL:
          break ;
        default: /* Not a known halftone cache type */
          success = FALSE;
          break ;
        }

        COPY_CHALFTONE_ON_DISK_TO_CHALFTONE(&chload.ch_ondisk_ptr, chptr)

        if ( byteswap ) {
          version = BYTE_SWAP32_UNSIGNED(version) ;
          ftype = BYTE_SWAP32_UNSIGNED(ftype) ;
          byte_swap_chalftone(chptr) ;
        }

        if ( version != HALFTONECACHE_VERSION )
          success = FALSE ;
      }

      if (!success) {
        cleanupDiskHtCacheLoad(tmpfd,
                               (detail_name == NAME_InvalidFile ||
                                detail_name == NAME_HDS) ? NULL : tmpbuf);
        tmpfd = EOF;
        continue;
      }

      HQASSERT(ftype == HALFTONECACHE_NORMAL ||
               ftype == HALFTONECACHE_ENCRYPTED,
               "Incorrect halftone cache type after loading and decryption") ;

      /* We now have a decrypted CHALFTONE in platform order. We'll test if
         it is the screen we want before reloading, decrypting and
         byte-swapping the internal data. */

      txfercnt = 0 ;
      HQASSERT(HALFTONECACHE_VERSION == 6000,
               "loadHalftoneFromDisk: HALFTONECACHE_VERSION changed");

      if ( theITHXfer(chptr) != NULL ) {

        /* To avoid invalidating the caches, treat the old 0 value from
           when the slot was a spare as the 'usual' case.  This can be
           removed when HALFTONECACHE_VERSION is next updated. */
        if ( theIMaxTHXfer( chptr ) == 0 )
          theIMaxTHXfer( chptr ) = 255;

        txfercnt = ((int32)theIMaxTHXfer( chptr )) + 1;
      }

      HQASSERT(ch_template->depth_shift == 0, "Nonsense depth");
      if ( chptr->depth_shift > depth_shift
           || (chptr->depth_shift != depth_shift && chptr->depth_shift != 0) )
        success = FALSE;
      else if ( !ht_equivalent_render_params(
                  chptr, ch_template,
                  /* match with cached shift, later shift to current */
                  chptr->depth_shift,
                  /* match with cached angle, later rotate to current */
                  chptr->oangle, TRUE) ) {
        success = FALSE ;
      } else if (chptr->screenprotection & SCREENPROT_PASSREQ_MASK) {
        /* if the cache is marked protected, only allow it if the
           appropriate feature(password) is enabled */
        switch (chptr->screenprotection & SCREENPROT_PASSREQ_MASK) {
          case SCREENPROT_PASSREQ_HDS:
            switch ( context->systemparams->HDS ) {
            case PARAMS_HDS_HIGHRES:
              break;

            case PARAMS_HDS_DISABLED:
              success = FALSE;
              break;

            case PARAMS_HDS_LOWRES:
              if ( !fSECAllowLowResHDS( context->page->ydpi ) )
                success = FALSE;
              break;

            default:
              HQFAIL("Unexpected HDS param value.");
              success = FALSE ; /* Fail access on unexpected condition. */
              break ;
            }
            break;

          case SCREENPROT_PASSREQ_HXM:
            if ( depth_shift > 0 ) { /* multi-bit HXM is not allowed */
              success = FALSE; break;
            }
            switch ( context->systemparams->HXM ) {
            case PARAMS_HXM_HIGHRES:
              break;

            case PARAMS_HXM_DISABLED:
              success = FALSE;
              break;

            case PARAMS_HXM_LOWRES:
              if ( !fSECAllowLowResHXM( context->page->xdpi, context->page->ydpi ) )
                success = FALSE;
              break;

            default:
              HQFAIL("Unexpected HXM param value.");
              success = FALSE ; /* Fail access on unexpected condition. */
              break ;
            }
            break;
        }
      } /* screenprotection */

      if ( !success ) {
        cleanupDiskHtCacheLoad(tmpfd, NULL);
        tmpfd = EOF;
        continue;
      }
      break; /* Out of search loop; we've found the right screen cache. */
    }
  }

  (void) (* theIEndList(osdevice)) (osdevice, handle);

  /* So did we find a matching screen cache? */
  if (tmpfd == EOF)
    return 0;

  /* read the rest of the cache */
  HQTRACE(debug_halftonecache,
          ("loadHalftoneFromDisk: %d %u %d %s", spotno, objtype, color, tmpbuf));

  /* Allocate substructures to read in */
  gridsize = theISuperCellActual( chptr ) * sizeof( int16 ) ;
  {
    int32 actionNumber = 0 ;
    mm_result_t res ;

    memAllocHdrSize[ 0 ] = 2 * gridsize ;
    memAllocHdrSize[ 1 ] = strlen((char *) tmpbuf) + 1;
    hdrCount = 2;
    if ( txfercnt != 0 ) {
      memAllocHdrSize[ 2 ] = txfercnt * sizeof( uint32 );
      hdrCount++ ;
    }

    do {
      res = mm_alloc_multi_hetero_with_headers( mm_pool_temp, hdrCount,
                                                memAllocHdrSize,
                                                memAllocHdrClass,
                                                memAllocHdrResult ) ;
      if ( res == MM_SUCCESS ) {
        ch_new = (CHALFTONE *)mm_alloc( mm_pool_temp, sizeof(CHALFTONE),
                                        MM_ALLOC_CLASS_CHALFTONE );
        if ( ch_new == NULL ) {
          size_t i;
          for ( i = 0 ; i < hdrCount ; i++ )
            mm_free_with_header( mm_pool_temp, memAllocHdrResult[ i ] ) ;
          res = MM_FAILURE;
        }
        else
          break ;
      }

      HQTRACE( debug_lowmemory, ( "CALL(handleLowMemory): loadHalftoneFromDisk" )) ;
      actionNumber = handleLowMemory( actionNumber, TRY_MOST_METHODS, NULL ) ;
    } while ( actionNumber > 0 ) ;

    if ( res != MM_SUCCESS ) { /* then the allocation failed */
      cleanupDiskHtCacheLoad( tmpfd , NULL ) ;
      if ( actionNumber == 0 ) /* didn't fail with error */
        (void)error_handler( VMERROR );
      return ( -1 ) ;
    }
  }

  /* Build new CHALFTONE using template and allocated blocks */
  *ch_new = *ch_template;
  ch_new->xcoords = xcoords = memAllocHdrResult[0];
  ch_new->ycoords = ycoords = (int16*)((uint8 *)xcoords + gridsize);
  ch_new->path = memAllocHdrResult[1];
  (void)strcpy((char *)ch_new->path, (char *)tmpbuf);
  if ( txfercnt != 0 )
    ch_new->thxfer = txfer = memAllocHdrResult[2];
  /* Some fields are not filled in yet in the template */
  ch_new->halfxdims = chptr->halfxdims; ch_new->halfydims = chptr->halfydims;
  ch_new->screenprotection = chptr->screenprotection;
  /* Take depth/rotation-adjusted fields from the cache, like the xytable */
  ch_new->halfr1 = chptr->halfr1; ch_new->halfr4 = chptr->halfr4;
  ch_new->halfr2 = chptr->halfr2; ch_new->halfr3 = chptr->halfr3;
  ch_new->depth_shift = chptr->depth_shift; ch_new->oangle = chptr->oangle;
  needs_depth_adjustment = chptr->depth_shift != depth_shift;

  if ( ((txfercnt != 0) &&
        ((*theIReadFile(osdevice))(osdevice, tmpfd, (uint8*)txfer,
                                   txfercnt * sizeof(uint32))
         != (int32)(txfercnt * sizeof(uint32)))) ||
       ((*theIReadFile(osdevice))(osdevice, tmpfd, (uint8*)xcoords, gridsize) != gridsize) ||
       ((*theIReadFile(osdevice))(osdevice, tmpfd, (uint8*)ycoords, gridsize) != gridsize) ) {
    size_t i;
    for ( i = 0 ; i < hdrCount ; ++i )
      mm_free_with_header( mm_pool_temp, memAllocHdrResult[ i ] ) ;
    mm_free( mm_pool_temp, ch_new, sizeof(CHALFTONE) );
    cleanupDiskHtCacheLoad( tmpfd,
                            (detail_name == NAME_InvalidFile ||
                             detail_name == NAME_HDS) ? NULL : tmpbuf) ;
    return 0 ;
  }

  if ( ftype == HALFTONECACHE_ENCRYPTED ) {
    if ( txfercnt != 0 ) {
      (void)encrypt_halftone((uint8*)txfer, sizeof(int32) * txfercnt,
                             sc_passkey, 0, EN_DE_CRYPT_NEXT);
    }
    if ( gridsize != 0 ){
      (void)encrypt_halftone((uint8*)xcoords, gridsize,
                             sc_passkey, 0, EN_DE_CRYPT_NEXT);
      (void)encrypt_halftone((uint8*)ycoords, gridsize,
                             sc_passkey, 0, EN_DE_CRYPT_NEXT);
    }
  }

  if ( byteswap ) {
    if ( txfercnt != 0 ) {
      BYTE_SWAP32_BUFFER(txfer, txfer, sizeof(uint32) * txfercnt);
    }
    if ( gridsize != 0 ) {
      BYTE_SWAP16_BUFFER(xcoords, xcoords, gridsize) ;
      BYTE_SWAP16_BUFFER(ycoords, ycoords, gridsize) ;
    }
  }

  /* Old 4 color HDS screens are not protected in the file */
  if (   (detail_name != NAME_HDS)
      && (chptr->screenprotection != (SCREENPROT_ENCRYPT_NONE | SCREENPROT_PASSREQ_HDS))) {
    uint16 lock= SECURITY_LOCK(chptr, xcoords, ycoords, HqxCryptSecurityNo());
    /* The first assignment (above) is always done, for slightly more obscure assemblese */
    switch (chptr->screenprotection & SCREENPROT_ENCRYPT_MASK) {
      case SCREENPROT_ENCRYPT_ANYHQN:
        lock = SECURITY_LOCK(chptr, xcoords, ycoords, 0x3b);
        break;

      case SCREENPROT_ENCRYPT_CUSTOMER:
        lock = SECURITY_LOCK(chptr, xcoords, ycoords, HqxCryptCustomerNo());
      break;

      case SCREENPROT_ENCRYPT_NONE:
      case SCREENPROT_ENCRYPT_DONGLE:
      default:
        break;
    }

    if (lock != chptr->lock) {
      size_t i;
      for ( i = 0 ; i < hdrCount ; ++i )
        mm_free_with_header( mm_pool_temp, memAllocHdrResult[i] );
      mm_free( mm_pool_temp, ch_new, sizeof(CHALFTONE) );
      cleanupDiskHtCacheLoad( tmpfd,
                              (( detail_name == NAME_InvalidFile ||
                                 detail_name == NAME_HDS ) ?
                               NULL : tmpbuf )) ;
      return 0 ;
    }
  }

  (void)(*theICloseFile(osdevice))(osdevice, tmpfd);

  return ht_insertchentry(context, spotno, objtype, color, ch_new,
                          depth_shift, default_depth_shift,
                          needs_depth_adjustment, orientation,
                          -1, /* patterngraylevel */
                          htname, sfcolor,
                          detail_name, detail_index,
                          FALSE, /* don't save to disk */
                          NULL, NULL, objtype, /* unused */
                          phasex, phasey)
    ? 1 : -1 ;
}

/*
Log stripped */
