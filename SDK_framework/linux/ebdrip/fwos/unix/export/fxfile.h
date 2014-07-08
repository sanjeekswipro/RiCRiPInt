/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __FXFILE_H__
#define __FXFILE_H__

/*
 * $HopeName: HQNframework_os!unix:export:fxfile.h(EBDSDK_P.1) $
 * FrameWork External, Unix specific, File definitions.
 */

/*
* Log stripped */

/* ----------------------- Includes ---------------------------------------- */

#include <sys/stat.h>   /* struct stat */

/* see fwcommon.h */
#include "fxcommon.h"   /* Common */
                        /* Is External */
                        /* Is Platform Dependent */

/* ----------------------- Macros ------------------------------------------ */

#define FW_DIRECTORY_SEPARATOR_BYTE     '/'

#define FWFILE_FORKS            1 /* Unix files have just one fork */

/* No platform-specific flag values for DirOpen */
#define FWDIR_OPEN_PLATFORM_MASK   0


/* Unix specific special file types, see stat */
#define FXFILE_TYPE_FIFO        ( FWFILE_TYPE_COMMON_TOTAL + 0 )
#define FXFILE_TYPE_CHR         ( FWFILE_TYPE_COMMON_TOTAL + 1 )
#define FXFILE_TYPE_BLK         ( FWFILE_TYPE_COMMON_TOTAL + 2 )
#define FXFILE_TYPE_SOCK        ( FWFILE_TYPE_COMMON_TOTAL + 3 )
#define FXFILE_TYPE_UNKNOWN     ( FWFILE_TYPE_COMMON_TOTAL + 4 )

#define FWFILE_TYPE_TOTAL       ( FWFILE_TYPE_COMMON_TOTAL + 5 )


/* unix has changed, access, and modified times
 * and modifed is in platform independent FWFILE_SET_INFO_COMMON_MASK
 */
#define FXFILE_SET_INFO_ACCESS_TIME   BIT( 31 ) 
#define FWFILE_SET_INFO_PLATFORM_MASK BITS_STARTING_AT(31)


/* Readable reflects user read permission bit
 * Writeable reflects user write permission bit
 *
 * 'All' versions change user, group & other
 */
#define FWFILE_INFO_READABLE(info)   FWFILE_ATTRIB_READABLE((info).attributes)
#define FWFILE_INFO_WRITEABLE(info)  FWFILE_ATTRIB_WRITEABLE((info).attributes)
#define FWFILE_INFO_WRITEABLE_GROUP(info)  FWFILE_ATTRIB_WRITEABLE((info).attributes & S_IWGRP)
#define FWFILE_INFO_WRITEABLE_ALL(info)    FWFILE_ATTRIB_WRITEABLE((info).attributes & S_IWOTH)

#define FWFILE_INFO_MAKE_READABLE(info) \
   (((info).attributes) |= (S_IRUSR))
#define FWFILE_INFO_MAKE_NOT_READABLE(info) \
   (((info).attributes) &= ~(S_IRUSR))
#define FWFILE_INFO_MAKE_READABLE_MASK  ( FWFILE_SET_INFO_ATTRIBUTES )

#define FWFILE_INFO_MAKE_READABLE_ALL(info) \
   (((info).attributes) |= (S_IRUSR|S_IRGRP|S_IROTH))
#define FWFILE_INFO_MAKE_READABLE_ALL_MASK  ( FWFILE_SET_INFO_ATTRIBUTES )

#define FWFILE_INFO_MAKE_WRITEABLE(info) \
   (((info).attributes) |= (S_IWUSR))
#define FWFILE_INFO_MAKE_NOT_WRITEABLE(info) \
   (((info).attributes) &= ~(S_IWUSR))
#define FWFILE_INFO_MAKE_WRITEABLE_MASK  ( FWFILE_SET_INFO_ATTRIBUTES )

#define FWFILE_INFO_MAKE_WRITEABLE_ALL(info) \
   (((info).attributes) |= (S_IWUSR|S_IWGRP|S_IWOTH))
#define FWFILE_INFO_MAKE_WRITEABLE_ALL_MASK  ( FWFILE_SET_INFO_ATTRIBUTES )

#define FWFILE_INFO_MAKE_WRITEABLE_GROUP(info) \
   (((info).attributes) |= (S_IWUSR|S_IWGRP))
#define FWFILE_INFO_MAKE_WRITEABLE_GROUP_MASK  ( FWFILE_SET_INFO_ATTRIBUTES )



/* FWFILE_ATTRIB_READABLE, FWFILE_ATTRIB_WRITEABLE are deprecated
 * Use FWFILE_INFO_READABLE, FWFILE_INFO_WRITEABLE instead
 */
#define FWFILE_ATTRIB_READABLE( attributes ) \
 ( ( attributes & S_IRUSR ) != 0 )
#define FWFILE_ATTRIB_WRITEABLE( attributes ) \
 ( ( attributes & S_IWUSR ) != 0 )

/* ----------------------- Types ------------------------------------------- */

typedef struct FwPlatformFileInfo
{
#define FXFILE_INFO_HAS_CHANGED_TIME
  HqU32x2       changedTime;     /* File last access time */
#define FXFILE_INFO_HAS_ACCESS_TIME
  HqU32x2       accessTime;      /* File last access time */
  struct stat   status;
} FwPlatformFileInfo;

/* Not applicable to Unix */
typedef struct FwPlatformFileOpenExtras
{
  int32 _dummmy_;
} FwPlatformFileOpenExtras;

/*
 * Primary platform-dependent file handle type (here, POSIX file descriptor)
 */
typedef int FwPlatformFileHandle;

#endif /* ! __FXFILE_H__ */

/* eof fxfile.h */
