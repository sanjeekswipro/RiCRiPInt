/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __FWFILE_H__
#define __FWFILE_H__

/* $HopeName: HQNframework_os!export:fwfile.h(EBDSDK_P.1) $
 * FrameWork External File Interface
 *
* Log stripped */

/* ----------------------- Overview ---------------------------------------- */

/* The FwFile convention is that you are intending to operate on a directory
 * rather than a file if and only if you pass a filename ending with a
 * directory separator. If the corresponding object on disk does not match
 * this intention, the operation will fail. The only exception to this is
 * FwFileGetInfo which you can use to find whether a given filename refers to
 * a file or a directory.
 */


/* ----------------------- Includes ---------------------------------------- */

/* see fwcommon.h */
#include "fwcommon.h"   /* Common */
                        /* Is External */
#include "fxfile.h"     /* Platform Dependent */

/* ggcore */
#include "fwerror.h"    /* FwErrorState */

#include "fwprog.h"     /* Progress counter. */

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------- Macros ------------------------------------------ */

/* see fxfile.h */
#define FW_DIRECTORY_SEPARATOR ((FwTextCharacter) FW_DIRECTORY_SEPARATOR_BYTE)

/* Many of the functions take a uint32 flags field with bits correspnding to
 * options as described below. These are organised into the following ranges.
 * bits  0 - 15 function specific but platform independent
 * bits 16 - 23 function independent - ie shared by several functions
 * bits 24 - 31 function and platform specific
 */

#define FWFILE_OP_SPECIFIC_MASK BITS_BELOW( 16 )
#define FWFILE_OP_GENERAL_MASK  BITS_FROM_TO_BEFORE( 16, 24 )
#define FWFILE_OP_PLATFORM_MASK BITS_STARTING_AT( 24 )

#define FWFILE_FLAG_RECURSIVE   BIT( 16 ) /* recurse in directory tree */
#define FWFILE_FLAG_CAN_EXIST   BIT( 17 ) /* allow to already exist */
#define FWFILE_FLAG_SET_COPY_WRITEABLE   BIT( 18 ) /* set copied file to be writeable */

#define FW_APPLERESOURCE        FWSTR_TEXTSTRING(".rsrc")

#define FWFILE_FLAG_APPLE_COPY     BIT( 19 )
#define FWFILE_FLAG_IGNORE_TICKLE  BIT( 20 )
#define FWFILE_FLAG_NO_OVERWRITE   BIT( 21 )

/* ----------------------- Types ------------------------------------------- */

#define FWFILE_ROGUE                   -1      /* Invalid file handle */
#define FWFILE_MIN_LEGAL_VALUE         0       /* For sanity checking */
typedef intptr_t                       FwFile; /* File handle */

#define FWDIR_ROGUE    ( (FwDir *) NULL )      /* Invalid dir enum handle */

/* All platform files have at least a data fork. See fxfile.h for platform specific forks. */ 
#define FWFILE_FORK_DATA     0  

typedef struct FwDir    FwDir;  /* Directory enumeration handle */

/*
 * Context for this library 
 */
typedef struct FwFileContext_s {
  int32                         _dummy_;
#ifdef FW_PLATFORM_FILE_CONTEXT
  FwPlatformFileContext         platform;
#endif
} FwFileContext;

/*
 * Extra open options
 */
typedef struct FwFileOpenExtras
{
  uint32        nFork;  /* see fxfile.h for FWFILE_FORKS */
  HqU32x2       size;   /* optional disk space to allocate on create */

  FwPlatformFileOpenExtras      platform;
} FwFileOpenExtras;

/*
 * File information
 */

/* type */
#define FWFILE_TYPE_DIR          0x0         /* Directory */
#define FWFILE_TYPE_FILE         0x1         /* File */
#define FWFILE_TYPE_LINK         0x2         /* Unix link, Mac alias */

#define FWFILE_TYPE_COMMON_TOTAL 0x3

/* See fxfile.h for:
 * - platform specific types, FWFILE_TYPE_TOTAL
 * - FWFILE_ATTRIB... macros to test attributes
 */

typedef struct FwFileInfo
{
  uint32        type;                   /* File or directory */
  uint32        attributes;             /* File attributes */

  HqU32x2       modifiedTime;           /* File last modified time */
  HqU32x2       size[ FWFILE_FORKS ];   /* logical fork size */
  
  FwPlatformFileInfo platform;          /* Platform specific information */
} FwFileInfo;

/* modifiedTime above and any other timestamps in the platform part are
 * only guaranteed to be well ordered (later times have higher values).
 * They are intended to be as raw as possible a representation of the
 * platform specific timestamp. Note particularly the following:
 * a/ difference in timestamp may not be proportional to difference in time
 * b/ Only HqU32x2 values returned as a timestamp are legal. You cannot
 *    generate one of your own by arithmetic operations.
 * c/ the epoch from which they are measured and the units are up to platform
 *
 * These timestamps can be converted to a more canonical format by using
 * FwFileTimeToSeconds, which frees you from the above restrictions at the cost
 * of possible rounding errors.
 */

typedef struct FwFileDiskFreeInfo
{
  HqU32x2       free;           /* disk free in bytes */
  HqU32x2       size;           /* total disk size in bytes */
  uint32        allocSize;      /* allocation block size in bytes */
} FwFileDiskFreeInfo;

/*
 * Structure for passing information back from FwFileTreeSize.
 */
typedef struct FwFileTreeSizeInfo
{
        HqU32x2 exact;  /* exact size of all files in bytes, not counting directories */
        HqU32x2 taken;  /* bytes taken for all files, i.e. rounded up to blocksize */
        uint32  files;  /* number of files in the tree */
        uint32  dirs;   /* number of directories */
} FwFileTreeSizeInfo;

/* Enumeration for FwFileGetPublicForm */
typedef enum EnumFwFilePublicForm
{
  FWFILE_FORM_NONE = 0, /* No public form (rogue value, for failures */
  FWFILE_FORM_URL,      /* Public form is a URL */
  FWFILE_FORM_PATH,     /* Public form is a file path (e.g. Windows UNC path) */
} FwFilePublicForm;

/* ----------------------- Functions --------------------------------------- */

/* FwAppDir             Find directory application resides in.
 *
 * This string is set up once on boot and will not be changed or freed.
 * If error or not possible returns NULL.
 * The string will have a trailing directory separator.
 */

extern FwTextString FwAppDir( void );


/* FwCurrentDir         Find current directory application ran from.
 *
 * On platforms that have no notion of current directory (Mac), this
 * will be the same as FwAppDir().
 *
 * This string is set up once on boot and will not be changed or freed.
 * If error or not possible returns NULL.
 * The string will have a trailing directory separator.
 */

extern FwTextString FwCurrentDir( void );


/* FwAppFileName        Find full name of the running appplication.
 *
 * The "full name" means the application name appended to its full
 * path, i.e. the absolute pathname in some platforms' terminology.
 *
 * This string is set up once on boot and will not be changed or freed.
 * If error or not possible returns NULL.
 */
extern FwTextString FwAppFileName( void );


/* The FwFile functions use the fwerror.h mechanism for error
 * reporting, and are guaranteed to return certain errors (or sub
 * errors of them) in particular cases as detailed after each
 * function.
 *
 * To tell whether a function has succeeded or failed you should test
 * ( pErrState->pError == FW_SUCCESS )
 *
 * To test which error has occurred use Fw_msg_IsDescendant rather
 * than comparing errors for equality, as the error heirarchy may be
 * extended in the future, and may distinguish subcases which were the
 * same when the client was written.
 *
 *
 * NOTE:
 * FwFileSeek takes a Hq32x2 so that it will still work with files larger
 * than 2GB (signed 32 bit value).  The other functions (read, write, ...)
 * assume we will only have a 32 bit address space, so take uint32 sizes.
 */


/* FwFileOpen           Open a file
 *
 * FwFileOpen returns handle >= 0 on success, FWFILE_ROGUE on failure.
 *
 * FwFileOpenExtended is a variant that allows you to specify extra optiions.
 * At the moment this only applies to the Mac, which if pExtras is NULL,
 * (or if calling via FwFileOpen) then data fork is defaulted, and
 * 'MPS' and 'TEXT' are defaulted as creator and type.
 */

#define FW_OPEN_RDONLY                  BIT( 0 ) /* read only */
#define FW_OPEN_WRONLY                  BIT( 1 ) /* write only */
#define FW_OPEN_RDWR                    BIT( 2 ) /* read and write */
#define FW_OPEN_APPEND                  BIT( 3 ) /* write guaranteed at end */
#define FW_OPEN_CREAT                   BIT( 4 ) /* create file */
#define FW_OPEN_TRUNC                   BIT( 5 ) /* truncate file */
#define FW_OPEN_EXCL                    BIT( 6 ) /* exclusive access */
/* Allow optimisation of sequential access (currently NT reading only).
 * IT ESSENTIAL TO UNDERSTAND THESE RESTRICTIONS:
 * - This may be ignored if you have requested FW_OPEN_EXCL, due to the need
 *   to close and reopen the file.
 * - It may exclude other writers to the file, to ensure local buffering
 *   does not get out of date.
 */
#define FW_OPEN_OPTIMISE_SEQUENTIAL     BIT( 7 )

#define FW_OPEN_MASK                    BITS_BELOW( 8 )

/* This flag currently must not be used directly for FwFileOpen[Extended] but
 * is used by Fw_msg_createStreamIOFile(), in a set of flags that are otherwise
 * FW_OPEN flags. This flag is deliberately not included in FW_OPEN_MASK.
 */
#define FW_OPEN_NO_TICKLE_WRITE         BIT( 15 )

extern FwFile FwFileOpen
 ( FwErrorState * pErrState, FwTextString ptbzFilename, uint32 flags );

extern FwFile FwFileOpenExtended
 (
   FwErrorState *       pErrState,
   FwTextString         ptbzFilename,
   uint32               flags,
   FwFileOpenExtras *   pExtras
 );

/* FwFileOpen[Type] errors:
 * fwErrorParameter     bad parameter
 * fwErrorString        bad filename
 * fwErrorNonExistent   file doesnt exist when it should
 * fwErrorAlreadyExists file exists when it shouldnt
 * fwErrorAccessDenied  file attributes deny open
 * fwErrorInUse         file opened exclusively by something else
 * fwErrorNoMemory      memory exhausted
 * fwErrorDiskFull      disk full
 * fwErrorSoftwareLimit OS file handles exhausted
 * fwErrorHardware      file IO error
 */


/* FwFileFromPlatformFileHandle   Create a FwFile from the primary
 *                                platform-dependent file handle type
 *
 * Returns handle >= 0 on success, FWFILE_ROGUE on failure.
 * You must observe all the precautions mentioned for FwFileOpen() together
 * will the following additional restrictions:
 *
 *  o Performing any operation using the platform dependent file handle or
 *    any other file handle associated with the same data will result in
 *    undefined behaviour.  In particular, you must not read, write, seek or
 *    close the platform dependent file descriptor or use it with another call
 *    to FwFileFromPlatformFileHandle().
 *
 *  o All further operations (in particular, closing the file) should be
 *    performed through this framework interface.
 *
 *  o Note that creating a FwFile from a platform dependent file handle may
 *    involve allocating memory.  Be sure to call FwFileClose() as usual.
 */
extern FwFile FwFileFromPlatformFileHandle( FwPlatformFileHandle );


/* FwFileRead           Read from a file
 *
 * Returns number of bytes read.
 * On failure this is the amount that was definitely read safely, although
 * more than this can actually have been read.
 * The file pointer is advanced by the return value.
 * When the request cannot be met because of EOF the Size is automatically
 * reduced to the remaining bytes before EOF.
 */
extern uint32 FwFileRead
 ( FwErrorState * pErrState, FwFile Handle, void * Buffer, size_t Size );


/* FwFileReadRecord     Read from a file in to a record
 *
 * Reads from current file pointer in to pRecord.
 * Read until EOF, or until a fixed record is full.
 * Returns number of bytes added to record.
 * On failure this is the amount that was definitely read safely, although
 * more than this can actually have been read.
 * The file pointer is advanced by the return value.
 */
extern uint32 FwFileReadRecord
 ( FwErrorState * pErrState, FwFile Handle, FwStrRecord * pRecord );


/* FwFileReadTextFile   Read from a file in to a record
 *
 * Reads the named file in to pRecord.
 * Read until EOF, or until a fixed record is full.
 * Returns number of bytes added to record.
 * On failure this is the amount that was definitely read safely, although
 * more than this can actually have been read.
 * Converts line endings to those for the current platform.
 */
extern uint32 FwFileReadTextFile
 ( FwErrorState * pErrState, FwTextString ptbzFilename, FwStrRecord * pRecord );


/* FwFileWrite          Write to a file
 *
 * Returns number of bytes written.
 * On failure this is the amount that was definitely written safely, although
 * more than this can actually have been written.
 * The file pointer is advanced by the return value.
 */
extern uint32 FwFileWrite
 ( FwErrorState * pErrState, FwFile Handle, void * Buffer, uint32 Size );


/* FwFileWriteEx        Write to a file
 *
 * Returns number of bytes written.
 * On failure this is the amount that was definitely written safely, although
 * more than this can actually have been written.
 * The file pointer is advanced by the return value.
 */
#define FWFILE_NO_TICKLES   BIT( 0 )   /* Disallow tickles during write */

extern uint32 FwFileWriteEx
 ( FwErrorState * pErrState, FwFile Handle, void * Buffer, uint32 Size, uint32 flags );


/* FwFileWriteTextFile   Write from a record in to a file
 *
 * Writes from pRecord in to the named file, creating it or runcating it as
 * appropriate.
 * Returns number of bytes written.
 * On failure this is the amount that was definitely written safely, although
 * more than this can actually have been written.
 * Converts line endings to those for the current platform.
 */
extern uint32 FwFileWriteTextFile
 ( FwErrorState * pErrState, FwTextString ptbzFilename, FwStrRecord * pRecord );


/* FwFileSeek           Seek within a file
 *
 * Returns TRUE <=> successful
 * Updates *pOffset to the new absolute offset within file, even on error where possible
 * or failing that to -1
 */

/* seek from options */
#define FW_SEEK_SET     0x0             /* absolute offset */
#define FW_SEEK_INCR    0x1             /* relative to current offset */
#define FW_SEEK_XTND    0x2             /* relative to end of file */

#define FW_SEEK_TOTAL   0x3             /* number of options */

extern int32 FwFileSeek
 ( FwErrorState * pErrState, FwFile Handle, Hq32x2 * pOffset, uint32 From );


/* FwFileExtent         Size of an open file
 *
 * Returns TRUE <=> successful
 * Updates *pExtent to the total size of the file, or -1 on error.
 */

extern int32 FwFileExtent
 ( FwErrorState * pErrState, FwFile Handle, Hq32x2 * pExtent );


/* FwFileClose          Close a file
 *
 * If fails handle is closed and all possible tidying up done.
 * Returns TRUE <=> success.
 */
extern int32 FwFileClose( FwErrorState * pErrState, FwFile Handle );


/* FwFileCopy           Copies a file.
 *
 * By default this will fail if the destination already exists.
 *
 * FWFILE_FLAG_CAN_EXIST => allow overwriting an existing ptbzNewname as long
 * as it has the correct "directoryness" and is not the same file.
 *
 * Returns TRUE <=> success.
 */

/* FwFileCopy flags */
#define FW_COPY_NON_EXCL        BIT( 0 )  /* target file opened non-exclusively */
#define FW_COPY_PRESERVE_TIME   BIT( 1 )  /* preserve source file date/time in copy created */
#define FWFILE_COPY_MASK        (FW_COPY_NON_EXCL | FW_COPY_PRESERVE_TIME | FWFILE_FLAG_CAN_EXIST | FWFILE_FLAG_SET_COPY_WRITEABLE | FWFILE_FLAG_APPLE_COPY | FWFILE_FLAG_IGNORE_TICKLE | FWFILE_FLAG_NO_OVERWRITE)

extern int32 FwFileCopy
 (
   FwErrorState *       pErrState,
   FwTextString         ptbzOldname,
   FwTextString         ptbzNewname,
   uint32               flags
 );


/* FwFileCopyTree       Copies a tree, i.e. a file or directory.
 *
 * Returns TRUE <=> success.
 */

/* FwFileCopyTree flags */
#define FW_COPYTREE_SETWRITE_USER   BIT( 3 ) /* set copied file to be user-writeable */ 
#define FW_COPYTREE_SETWRITE_GROUP  BIT( 4 ) /* set copied file to be group-/user-writeable */
#define FW_COPYTREE_SETWRITE_WORLD  BIT( 5 ) /* set copied file to be world-/group-/user-writeable */
#define FW_COPYTREE_SETWRITE_MASK  (FW_COPYTREE_SETWRITE_USER | FW_COPYTREE_SETWRITE_GROUP | FW_COPYTREE_SETWRITE_WORLD)

#define FWFILE_COPY_TREE_MASK      (FW_COPY_PRESERVE_TIME | FWFILE_FLAG_CAN_EXIST | FWFILE_FLAG_SET_COPY_WRITEABLE | FW_COPYTREE_SETWRITE_MASK | FWFILE_FLAG_IGNORE_TICKLE | FWFILE_FLAG_NO_OVERWRITE)

extern int32
FwFileCopyTree(
   FwErrorState *err,
   FwTextString oldstr,
   FwTextString newstr,
   FwObj *progress,
   uint32 flags
);


extern int32
FwFileCopyTreeEx(
   FwErrorState *err, 
   FwTextString oldstr, 
   FwTextString newstr, 
   FwObj *progress, 
   uint32 flags, 
   FwTextString ptbzExcept
);


/* FwFileRename         Renames a file or directory.
 *
 * Files will be copied across media if necessary.
 * By default this will fail if the destination already exists unless it is
 * the same, eg case change.
 *
 * FWFILE_FLAG_CAN_EXIST => allow overwriting an existing ptbzNewname as long
 * as it has the correct "directoryness".
 *
 * Returns TRUE <=> success.
 */

/* FwFileRename flags */
#define FWFILE_RENAME_MASK      FWFILE_FLAG_CAN_EXIST

extern int32 FwFileRename
 (
   FwErrorState *       pErrState,
   FwTextString         ptbzOldname,
   FwTextString         ptbzNewname,
   uint32               flags
 );


/* FwFileDelete         Deletes a file orr directory.
 *
 * Returns TRUE <=> success.
 */

/* FwFileDelete flags */
#define FWFILE_DELETE_READONLY    BIT( 0 ) /*  allow deletion of read-only  */

#define FWFILE_DELETE_MASK        BITS_BELOW( 1 )

extern int32 FwFileDelete
 ( FwErrorState * pErrState, FwTextString ptbzFilename, uint32 flags );


/* FwFileGetInfo        Get information about a file or directory
 *
 * Fills pInfo with the information taking into account options from flags
 *
 * Returns TRUE if all requested information found
 *         FALSE if any information cannot be obtained
 */

/* FwFileGetInfo flags */
#define FWFILE_INFO_LINK        BIT( 0 ) /* If link info on link itself */

#define FWFILE_INFO_MASK        BITS_BELOW( 1 )

extern int32 FwFileGetInfo
 (
   FwErrorState *       pErrState,
   FwTextString         ptbzFilename,
   FwFileInfo *         pInfo, /* Can pass NULL to just test existence */
   uint32               flags
 );


/* FwFileSetInfo        Sets information about a file or directory
 *
 * Sets ths information specified by flags
 *
 * Returns TRUE <=> success
 */

/* FwFileSetInfo flags */

/* platform independant permission attributes plus timestamps */
#define FWFILE_SET_INFO_ATTRIBUTES    BIT( 0 ) 
#define FWFILE_SET_INFO_MODIFY_TIME   BIT( 1 ) 

/* ones that can be set on all platforms */
#define FWFILE_SET_INFO_COMMON_MASK BITS_BELOW(2)

/* ones that can be set on current platform, see fxfile.h */
#define FWFILE_SET_INFO_MASK \
 ( FWFILE_SET_INFO_COMMON_MASK | FWFILE_SET_INFO_PLATFORM_MASK)

extern int32 FwFileSetInfo
 (
   FwErrorState *       pErrState,
   FwTextString         ptbzFilename,
   FwFileInfo *         pInfo,
   uint32               flags
 );


/* FwFileSetInfoTree    Sets information about all files and directories in a tree
 *
 * See FwFileSetInfo
 *
 * Returns TRUE <=> success
 */

#define FWFILE_SET_INFO_TREE_WRITEABLE  BIT( 0 )

/* ones that can be set on all platforms */
#define FWFILE_SET_INFO_TREE_COMMON_MASK BITS_BELOW(1)

extern int32 FwFileSetInfoTree
 (
   FwErrorState *err, 
   FwTextString target, 
   uint32 flags
  );



/*
 * FwFileTimeToSeconds
 *
 * Converts a platform specific file timestamp into seconds since 
 * 00:00:00 UTC, Jan. 1, 1970.
 * There may be a subsecond fractional part.
 * The conversion may involve unavoidable floating point rounding errors.
 * See comment after definition of FwFileInfo for rationale.
 */
extern long double FwFileTimeToSeconds( HqU32x2 * pFileTime );

/* 
 * FwFilenamesReferToSameFile
 *
 * Returns FALSE with error set if either file does not exist or error occurs.
 * Returns FALSE with no error set if they refer to different EXISTING files
 * Returns TRUE if both refer to the same EXISTING file.
 */
extern int32 FwFilenamesReferToSameFile
 (
   FwErrorState *       pErrState,
   FwTextString         ptbzFilename1,
   FwTextString         ptbzFilename2
 );

/* 
 * FwFilenamesReferToSameDisk
 *
 * Seeing whether two devices have the same root part is not a reliable way
 * of telling if the are on the same disk. It may be possible to mount
 * multiple logical volumes on the same disk partition, or for links to cross
 * onto other disk partitions.
 *
 * Returns FALSE with error set if either file does not exist or error occurs.
 * Returns FALSE with no error set if both exist but on different disks
 * Returns TRUE if both files exist on to the same disk.
 */
extern int32 FwFilenamesReferToSameDisk
 (
   FwErrorState *       pErrState,
   FwTextString         ptbzFilename1,
   FwTextString         ptbzFilename2
 );


/* FwDirCreate          Create a directory (heirarchy)
 *
 * Returns TRUE <=> success.
 * If file of same name exists
 * or ( directory of same name exists and not FWFILE_FLAG_CAN_EXIST )
 * then sets error fwErrorAlreadyExists.
 */

#define FWDIR_CREATE_MASK \
 ( FWFILE_FLAG_RECURSIVE | FWFILE_FLAG_CAN_EXIST | FWFILE_FLAG_APPLE_COPY )

extern int32 FwDirCreate
 ( FwErrorState * pErrState, FwTextString ptbzDirname, uint32 flags );

/* FwDirCreate errors:
 * fwErrorAlreadyExists file/directory already exists
 * fwErrorNonExistent   root directory does not exist
 */


/* FwDirDelete          Recursively walk directory and delete all files and
 *                      sub-directories along the way.
 *
 * If any part of the process fails then it gives up at that point, and a partially
 * deleted tree might remain. The ptbzDirname must be terminated by a directory separator.
 * Returns TRUE  <==> success.
 */
#define FWDIR_DELETE_READONLY    BIT( 0 ) /* allow deletion of read-only */

/* ones that can be set on all platforms */
#define FWDIR_DELETE_COMMON_MASK BITS_BELOW(1)

/* ones that can be set on current platform, see fxfile.h */
#define FWDIR_DELETE_MASK ( FWDIR_DELETE_COMMON_MASK | FWDIR_OPEN_PLATFORM_MASK)

extern int32 FwDirDelete
  (
    FwErrorState * pErrState,
    FwTextString   ptbzDirname,
    uint32         flags
  );

 
/* =====================
 * Directory Enumeration
 * =====================
 *
 * FwDirOpen            Open a directory for enumeration
 *
 * Returns a directory enumeration handle on success, FWDIR_ROGUE on failure.
 */

#define FWDIR_OPEN_SNAPSHOT     BIT(0) /* take snapshot of dir contents */

/* ones that can be set on all platforms */
#define FWDIR_OPEN_COMMON_MASK BITS_BELOW(1)

/* ones that can be set on current platform, see fxfile.h */
#define FWDIR_OPEN_MASK ( FWDIR_OPEN_COMMON_MASK | FWDIR_OPEN_PLATFORM_MASK)

extern FwDir * FwDirOpen
 ( FwErrorState * pErrState, FwTextString ptbzDirname, uint32 flags );


/* FwDirNext            Get next leafname within directory.
 * 
 * FwDirNext outputs the next leafname if any to FwStrRecord.
 * Executes an implicit call of FwFileGetInfo for the returned leafname,
 * even if enumerating a snapshot.
 *
 * Returns TRUE <=> got next leafname
 *         FALSE <=> no more leafnames or error,
 */
extern int32 FwDirNext
 (
   FwErrorState *       pErrState,
   FwDir *              DirHandle,
   FwStrRecord *        pRecord,
   FwFileInfo *         pInfo,          /* as for FwFileGetInfo */
   uint32               flags           /* as for FwFileGetInfo */
 );


/* FwDirClose           Must call to end enumeration after successful FwDirOpen
 *
 * Returns TRUE <=> success
 */
extern int32 FwDirClose( FwErrorState * pErrState, FwDir * DirHandle );


/* ============
 * Disk volumes
 * ============
 */

/* Fills in FwFileDiskFreeInfo struct with bytes free on disk specified via
 * arbitrary file name ptbzFilename.
 */
extern int32 FwFileDiskFree
 (
   FwErrorState *       pErrState,
   FwTextString         ptbzFilename,
   FwFileDiskFreeInfo * pDFInfo
 );


/* ============
 * Statistics
 * ============
 */

/*
 * Estimate how much space a directory or file occupies on disk.
 * Puts the total size in bytes, the size with each file rounded
 * up to the block size, the number of files and the number of directory
 * nodes into *sizeinfo. Returns success or failure.
 *
 * The disk block size is passed in, rather than the function using the size
 * of the volume containing "path". This is useful when copying files to
 * another volume: pass the block size of the destination, and the result
 * will be the space that the files would take if copied there.
 *
 * Non-existent files are not counted as an error. This is useful when
 * copying files, because the amount of space that the files would take,
 * less the amount of space taken up by old versions of the files, is
 * the amount of free space needed for the copy. So get the size of the
 * source, then get the size of the target, ignoring non-existent files,
 * and subtract. Any program that would like to be warned of non-existent
 * files can test for them itself before calling FwFileTreeSize.
 *
 * Because it is not possible in general to say how many disk blocks
 * will be used in creating a new directory, or adding a file to an
 * existing directory, counts of files and directories are returned
 * so that the caller can  make its own estimate.
 */
extern uint32 FwFileTreeSize(
  FwErrorState *err,
  FwTextString path,
  FwFileTreeSizeInfo *sizeinfo,
  uint32 blocksize
);


/* =================
 * Filename handling
 * =================
 *
 * Syntax
 * ======
 * Filenames are assumed to have the following syntax:
 *
 * [<root part>][<directory name><directory separator>]*[<leafname>]
 *
 * FwFile functions which must have a filename, rather than a directory name
 * will reject root directories, and pathnames with a trailing directory
 * separator by setting the error fwErrorStringSyntax. However directory names
 * can be supplied with or without a trailing directory separator, except
 * where required by the syntax of root directories.
 *
 * Clients should always parse filenames using the functions below,
 * rather than writing there own routines, becuase of various awkward
 * cases eg:
 *
 * 1) PC directory separator '\\' is also a trail byte for some multibyte
 * characters.
 *
 * 2) When handling trailing directory separators, the root directory is a
 * a special case.
 *
 * 3) You cannot decide whether a Mac filename is absolute by looking at
 * first character.
 *
 * The platform directory separator character can be assumed to
 * be a single byte character, and a macro for it is given in
 * fxfile.h.
 *
 *
 * Relative filenames
 * ==================
 * Relative filenames will be prepended by the directory the application ran
 * from as returned by FwAppDir(). No notion of current working directory
 * is supported by the platform dependent interface.
 *
 *
 * Multibyte encoding and filenames
 * ===============================

 * FwFile presents an interface to the platform dependent file system
 * as if the file system handles filenames containing international
 * characters using the encoding described in fwstring.h. On Mac and
 * Unix, and PC under Windows ( but not Windows95 and NTFS ) filenames
 * are actually held by the OS as raw byte strings. This raises the
 * following problems for these platforms:
 *
 * 1) The filename may contain invalid multibyte encodings. See fwstring.h
 * for implications of this.
 *
 * 2) If a character encoding ever used the platform directory
 * separator as a trail bytes of a character encoding ), and this
 * character was legal in filenames, the OS could chop up the filename
 * in mid character. This is known not to be a problem with present
 * encodings, and is unlikely to be true for future encodings, because
 * of this problem.
 */

/* FwFileParseRoot() parses the root part of an absolute filename
 * *pptbzFilename.
 * If the filename is relative FALSE is returned.
 * If pRecord is non NULL the variable part of the root, excluding any
 * platform specific root prefixes and suffices is sent to it.
 * (Note that the variable part of the root might be modified, for
 * example by mapping to upper case, to make it canonical.)
 * *pptbzFilename is updated to point to first unconsumed character of input.
 * eg:
 *      Input                   Output                  Remaining input
 * PC   "C:\..."                "C"                     "..."
 *      "\\machine\drive\..."   "machine\drive"         "..."
 *
 * Mac  "volume:..."            "volume"                "..."
 *
 * Unix "/..."                  ""                      "..."
 *
 */
extern int32 FwFileParseRoot
 ( FwStrRecord * pRecord, FwTextString * pptbzFilename );

/* FwFileSkipRoot() simply calls FwFileParseRoot() with pRecord NULL */
extern int32 FwFileSkipRoot( FwTextString * pptbzFilename );

/* FwFileBuildRoot() performs the inverse of FwFileParseRoot() and builds a
 * complete root part of a platform dependent filename from the variable part
 * as previously output by FwFileParseRoot.
 * (Note that the variable part of the root might be modified, for
 * example by mapping to upper case, to make it canonical.)
 * If the input is badly formed FALSE is returned and no output is generated.
 */
extern int32 FwFileBuildRoot( FwStrRecord * pRecord, FwTextString ptbzInput );

/* FwFileCopyRoot copies the root part of an absolute platform dependent
 * filename. That is everything before the first character of the first
 * directory in the filename.
 * The consumed input is sent to pRecord.
 * It returns a pointer to first unconsumed character of input.
 * If the filename is relative no input is consumed.
 */
extern FwTextString FwFileCopyRoot
 ( FwStrRecord * pRecord, FwTextString ptbzFilename );

/* FwFileCopyLink() asserts that the old link is a link, creates a link with FpCopyLink
 * then does a setinfo on the new link with the permissions from the old link
 */
extern int32 FwFileCopyLink
  ( FwErrorState * pErrState, FwTextString ptbzOldname, FwTextString ptbzNewname, FwFileInfo * pOldInfo, uint32 flags);

/* FwFileSkipPathElement() returns
 *   - a pointer to the next FW_DIRECTORY_SEPARATOR, or if none,
 *   - a pointer to the terminating zero.
 */
extern FwTextString FwFileSkipPathElement( FwTextString ptbzInput );

/* FwFileCopyPathElement() is the same as FwFileSkipPathElement but outputs
 * the skipped input to pRecord.
 */
extern FwTextString FwFileCopyPathElement
 ( FwStrRecord * pRecord, FwTextString ptbzInput );

/* FwFileSkipToLeafname() skips to the first character after the final
 * directory separator. If the filename ends in a directory separator,
 * or is just a root part this will be the terminating zero.
 */
extern FwTextString FwFileSkipToLeafname( FwTextString ptbzFilename );

/* FwFileRemoveLeaf() Remove the leaf part of a name. If the filename ends in a directory separator,
 * or is just a root part this will be the terminating zero.
 */
extern FwTextString FwFileRemoveLeafname( FwTextString ptbzFilename );

/* FwFileHasTrailingSeparator returns:
 *  filename just root                          - ptr to the terminating zero
 *  filename ending in directory separator      - ptr to final separator
 *  filename not ending in directory separator  - NULL
 * Note this cannot always just look at the last byte of the name but may have
 * to scan the whole name from the beginning.
 */
extern FwTextString FwFileHasTrailingSeparator( FwTextString ptbzFilename );

/* FwFileEnsureTrailingSeparator() adds a terminating directory separator
 * unless:
 * - the filename is an empty string,
 * - or FwFileHasTrailingSeparator returns TRUE 
 * It returns TRUE <=> directory separator added.
 */
extern int32 FwFileEnsureTrailingSeparator( FwStrRecord * pRecord );

/* Receives pointer to pointer to an FwMem-allocated string buffer.
 * If ptbzFilename needs a trailing separator, free original buffer and 
 * allocate a new buffer containing the name with directory separator 
 * added and update caller's string pointer to point to it.
 * It returns TRUE <=> directory separator added.
 */
extern int32 FwFileEnsureTrailingSeparatorRealloc
 ( FwTextString * pptbzFilename );

/* FwFileRemoveTrailingSeparator() removes a terminating directory separator
 * unless the filename consists of just a root part.
 * It returns TRUE <=> directory separator removed.
 * Note this cannot just look at the last byte of the name but has to scan the
 * whole name from the beginning.
 */
extern int32 FwFileRemoveTrailingSeparator( FwTextString ptbzFilename );

/* FwFilenameConcatenate is used to append a relative path to an
 * absolute directory name held in pRecord, ensuring there will be
 * exactly one directory separator in between, irrespective of whether the
 * directory name ends with a separator, or ptbzRel begins with one.
 */
extern void FwFilenameConcatenate
 ( FwStrRecord * pRecord, FwTextString ptbzRel );

/* As FwFilenameConcatenate but allocates a buffer to hold the result */
extern FwTextString FwFilenameConcatenateAlloc
 ( FwTextString ptbzAbs, FwTextString ptbzRel );

/* Returns TRUE <=> the filename (coerced to absolute if necessary)
 * doesnt exceed filename length limits, and is sufficiently well
 * formed to do the test.  This may depend on the length in
 * characters, and hence cannot be deduced from the length in bytes.
 */
extern int32 FwFilenameLengthOK( FwTextString ptbzFilename );

/* Copies ptbzFilename to pRecord converting it to any external
 * form for the platform.  Currently only Mac OS X has an external
 * form different from the FW path form, and this function
 * converts HFS -> POSIX.
 * Pass TRUE for fIsDir to indicate that ptbzFilename is a directory.
 * Returns the number of bytes written to pRecord.
 */
extern uint32 FwFileCopyPathToExternalForm
  ( FwStrRecord * pRecord, FwTextString ptbzFilename, int32 fIsDir );

/* Copies ptbzFilename to pRecord converting it from any external
 * form for the platform.  Currently only Mac OS X has an external
 * form different from the FW path form, and this function
 * converts POSIX -> HFS.
 * Pass TRUE for fIsDir to indicate that ptbzFilename is a directory.
 * Returns the number of bytes written to pRecord.
 */
extern uint32 FwFileCopyExternalFormToPath
  ( FwStrRecord * pRecord, FwTextString ptbzFilename, int32 fIsDir );

/* Returns flag that is true if the given string is positively identified
 * as being a path in the external form appropriate to the platform on
 * which we are running. If it is not definitely of that form, FALSE is
 * returned. FALSE is always returned for platforms that do not
 * differentiate between external and internal forms.
 *
 * Because this function relies on heuristics, which might conceivably not
 * work in some unusual cases, it is far better, whenever possible, to
 * write code that "knows" what kind of path it is dealing with. This
 * function is only for when that really is not possible.
 */
extern int32 FwFileIsExternalForm
  (FwTextString ptbzFilename, int32 fIsDir);

/* Converts the given file path to public form, making a best effort
 * to ensure that the it will work from a remote machine. Because of
 * the vagaries of networked file systems, no guarantees of
 * remote access can be made.
 *
 * Pass TRUE for fIsDir to indicate that ptbzFilename is a directory.
 *
 * Returns value indicating what, if any, type of public form was
 * created.
 */
extern FwFilePublicForm FwFileGetPublicForm
  (FwErrorState* pErrState, FwStrRecord* pPublicRecord,
   FwTextString ptbzFilename, int32 fIsDir);

/* ---------------- Extension handling ----------------------- */

/* 
 * Ensure the file extension of pRecord is equal to that pointed to by
 * ptbzWantedExtension.
 */
int32 FwFileCheckExtension( FwStrRecord* pRecord,
                            FwTextString ptbzWantedExtension );

/*
 * Return a pointer to the extension of the filename in pRecord. 
 * Returns NULL if no extension in filename.
 */
FwTextByte* FwFileFindExtension( FwStrRecord* pRecord );

/* 
 * Take the filename in ptbzFilename, and replace the extension with
 * ptbzNewExtension.  If ptbzNewExtension is NULL, remove the extension,
 * including the dot character.
 */
int32 FwFileChangeExtension( FwTextString ptbzFilename,
                             FwStrRecord* pResult,
                             FwTextString ptbzNewExtension );


#ifdef __cplusplus
}
#endif

#endif /* ! __FWFILE_H__ */

/* eof fwfile.h */
