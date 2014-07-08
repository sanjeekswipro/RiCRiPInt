/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:imfile.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Image file storage implementation
 *
 * This code provides an API for the storage and retrieval of blocks of rip
 * image or backdrop data.  Data is forced to disk as the result of low memory
 * actions, or proactively by the rip for data it thinks will not be needed for
 * some time.  It is then re-loaded on demand as it becomes needed.
 *
 * There is no direct API for the opening and closing of these disk files.
 * Instead the open is done on-demand, and the close by a bulk call at the end
 * of processing.  Access to the files needs to be either read-write, during
 * creation, or read-only during usage. The switch-over in modes happens by a
 * im_filecloseall call that occurs at the end of interpretation or image
 * adjustment.  The renderer then re-opens the files in read-only mode when
 * required.
 *
 * There are two different usage patterns available.  Serial mode requires all
 * access to be single-threaded and any mutex locking requirement is handled by
 * the client.  Parallel mode allows multi-threaded reads (each thread has its
 * own file descriptor), but writes and other functions are still required to be
 * single threaded.  Parallel mode is used for image stores and serial for
 * backdrop.
 *
 * \todo BMJ 02-Jun-11 : Change the API away from a file abstraction
 * (open/seek/read/write) to a more data-centric approach, to better
 * encapsulate the atomic nature of certain operations. This will allow the
 * mutex to be made local to the implementation, and allow alternate data
 * storage and synchronisation methods to be more easily prototyped. Can then
 * also more easily add re-entrancy tests and asserts to ensure we are not
 * writing to the same file at the same time from two threads.
 */

#include "core.h"
#include "coreinit.h"
#include "devices.h"            /* device_error_handler */
#include "mm.h"                 /* mm_alloc */
#include "dlstate.h"            /* DL_STATE */
#include "render.h"
#include "ripmulti.h"           /* IS_INTERPRETER */
#include "swcopyf.h"            /* swcopyf */
#include "swerrors.h"           /* VMERROR */
#include "swstart.h"            /* SwThreadIndex() */
#include "imfile.h"

struct IM_FILE_CTXT {
  uint32 id;              /**< For generating unique file names */
  Bool parallel;          /**< parallel allows multi-threaded reads */
  IM_FILES *head;         /**< A list of paged out data files */
  uint32 count;           /**< The number of files */
};

struct IM_FILES {
  uint8 fname[32];
  int32 fsize;
  int16 falign;
  uint8 writeable;
  uint8 parallel;
  unsigned int ndesc;
  DEVICE_FILEDESCRIPTOR *fdesc;
  int32 *fseek;
  IM_FILES *next;
};

static DEVICELIST *im_tmpdev = NULL;

static uint32 next_ctxt_id = 0;

/** Pick up the %tmp% device after the bootup files have run. */
static Bool im_file_postboot(void)
{
  HQASSERT(im_tmpdev == NULL, "im_tmpdev should be NULL on bootup");
  im_tmpdev = find_device((uint8 *)"tmp");
  if ( im_tmpdev == NULL )
    return FAILURE(FALSE);

  /* Make device non-removable & disable to prevent PS opening files on it. */
  theIDeviceFlags(im_tmpdev) &= ~DEVICEREMOVABLE;
  theIDeviceFlags(im_tmpdev) &= ~DEVICEENABLED;

  return TRUE;
}

void im_file_C_globals(core_init_fns *fns)
{
  fns->postboot = im_file_postboot;
  im_tmpdev = NULL;
  next_ctxt_id = 0;
}

/**
 * Return the index that should be used to access the file description
 * within the passed IM_FILES structure.
 *
 * If we are in parallel mode, then one structure is used per thread, so
 * the index is the current thread-id. But in serial mode, the threads all
 * use a common file handle, stored at index 0.
 */
static unsigned int im_index(IM_FILES *imf)
{
  unsigned int ii = 0;

  HQASSERT(imf, "NULL IM_FILES pointer");
  if ( imf->parallel )
    ii = SwThreadIndex();
  HQASSERT(ii < imf->ndesc, "Indexing off end of descriptor array");
  return ii;
}

/**
 * Free all the data in the linked list of im file structures.
 */
static void im_filefree(IM_FILES *imf)
{
  while ( imf != NULL ) {
    IM_FILES *next = imf->next;

    if ( imf->fdesc )
      mm_free(mm_pool_temp, imf->fdesc, imf->ndesc * sizeof(DEVICE_FILEDESCRIPTOR));
    if ( imf->fseek )
      mm_free( mm_pool_temp, imf->fseek, imf->ndesc * sizeof(int32));
    mm_free(mm_pool_temp, imf, sizeof(IM_FILES));

    imf = next;
  }
}

/**
 * Allocate an im-files data structure.
 */
static IM_FILES *im_filealloc(unsigned int ndesc)
{
  IM_FILES *imf, init = {0};

  imf = mm_alloc(mm_pool_temp, sizeof(IM_FILES), MM_ALLOC_CLASS_IMAGE_FILE);
  if ( imf == NULL ) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  *imf = init;
  imf->fdesc = mm_alloc(mm_pool_temp, ndesc * sizeof(DEVICE_FILEDESCRIPTOR),
                        MM_ALLOC_CLASS_IMAGE_FILE);
  imf->fseek = mm_alloc(mm_pool_temp, ndesc * sizeof(int32),
                        MM_ALLOC_CLASS_IMAGE_FILE);
  imf->next = NULL;
  imf->ndesc = ndesc;
  if ( imf->fdesc == NULL || imf->fseek == NULL ) {
    im_filefree(imf);
    (void)error_handler(VMERROR);
    return NULL;
  }
  return imf;
}

/**
 * Create a file on the tmpdev for storing data of the given type and
 * alignment.
 */
static Bool im_filecreate(IM_FILE_CTXT *imfile_ctxt,
                          int16 falign, IM_FILES **ffile, Bool parallel)
{
  DEVICE_FILEDESCRIPTOR fdesc;
  unsigned int i;
  IM_FILES *imf;
  unsigned int ii;

  HQASSERT(ffile != NULL, "ffile NULL");
  HQASSERT(im_tmpdev != NULL, "somehow didn't get im_tmpdev");

  if ( (imf = im_filealloc(NUM_THREADS())) == NULL )
    return error_handler(VMERROR);

  swcopyf(imf->fname, (uint8 *)"P%X_%X.IMG",
          imfile_ctxt->id, imfile_ctxt->count);
  fdesc = (*theIOpenFile(im_tmpdev))(im_tmpdev, imf->fname,
                                     SW_RDWR|SW_CREAT|SW_TRUNC);
  if ( fdesc < 0 )
    return device_error_handler(im_tmpdev);

  imf->fsize = 0;
  imf->falign = falign;
  imf->writeable = TRUE;
  imf->parallel = (uint8)parallel;
  ii = im_index(imf);
  imf->fdesc[ii] = fdesc;
  imf->fseek[ii] = 0;  /* images on disk now start at 0. */
  /* Mark the other threads as not open */
  for ( i = 0; i < imf->ndesc; ++i )
    if ( i != ii )
      imf->fdesc[i] = -1;

  imf->next = imfile_ctxt->head;
  imfile_ctxt->head = imf;
  ++imfile_ctxt->count;

  (*ffile) = imf;
  return TRUE;
}

#define IM_MAXFILESIZE 0x7FFFFFFFu

/**
 * Work out the file and offset where data of the given type, alignment, and
 * size will be stored. If the file does not yet exist, then lazily create it.
 * Not thread safe, and therefore it is up to the client to ensure thread
 * safety.
 */
Bool im_fileoffset(IM_FILE_CTXT *imfile_ctxt, int16 falign,
                   int32 tbytes, IM_FILES **ffile, int32 *offset)
{
  IM_FILES *imf;

  HQASSERT(ffile, "ffile NULL");
  HQASSERT(offset, "offset NULL");
  HQASSERT(tbytes > 0, "tbytes should be > 0");
  HQASSERT(tbytes < IM_MAXFILESIZE, "tbytes >= IM_MAXFILESIZE");

  for ( imf = imfile_ctxt->head; imf != NULL; imf = imf->next ) {
    unsigned int ii = im_index(imf);
    size_t newsize = (size_t)imf->fsize + (size_t)tbytes;

    if ( imf->fdesc[ii] >= 0 && imf->falign == falign &&
         imf->writeable && newsize < IM_MAXFILESIZE )
      break;
  }

  if ( imf == NULL &&
       !im_filecreate(imfile_ctxt, falign, &imf, imfile_ctxt->parallel) )
    return FALSE;

  *ffile  = imf;
  *offset = imf->fsize;
  imf->fsize += tbytes;

  return TRUE;
}

/**
 * Check to see if the this file was closed at the end of interpretation or
 * image adjustment, and if it was re-open it in read-only mode.
 */
static Bool im_filevalidate(IM_FILES *ffile, unsigned int ii)
{
  HQASSERT(ffile, "ffile NULL");
  HQASSERT(ffile->fdesc, "Somehow lost fdesc array");
  HQASSERT(ffile->fseek, "Somehow lost fseek array");

  if ( ffile->fdesc[ii] < 0 ) {
    DEVICE_FILEDESCRIPTOR fd;

    fd = (*theIOpenFile(im_tmpdev))(im_tmpdev, ffile->fname, SW_RDONLY);
    if ( fd < 0 )
      return FAILURE(device_error_handler(im_tmpdev));

    ffile->fdesc[ii] = fd;
    ffile->fseek[ii] = 0;
    ffile->writeable = FALSE;
  }
  HQASSERT(ffile->fdesc[ii] >= 0, "Failed to clone file");
  return TRUE;
}

/**
 * Seek to the given offset in the given file. Each file keeps track of
 * its current location for each thread, so the physical seek can be avoided
 * if we are already there.
 */
Bool im_fileseek(IM_FILES *ffile, int32 foffset)
{
  unsigned int ii = im_index(ffile);

  HQASSERT(foffset >= 0, "foffset should be >= 0");

  if ( !im_filevalidate(ffile, ii) )
    return FALSE;

  if ( ffile->fseek[ii] != foffset ) {
    Hq32x2 filepos;
    Hq32x2FromInt32(&filepos, foffset);

    if ( (*theISeekFile(im_tmpdev))(im_tmpdev, ffile->fdesc[ii],
                                    &filepos, SW_SET) == 0 )
      return FAILURE(device_error_handler(im_tmpdev));

    ffile->fseek[ii] = foffset;
  }
  return TRUE;
}

/**
 * Read the given amount of image data in from the specified file.  Thread safe
 * for reads providing the 'parallel' option was enabled.
 */
Bool im_fileread(IM_FILES *ffile, uint8 *fdata, int32 fbytes)
{
  unsigned int ii = im_index(ffile);

  HQASSERT(fdata, "fdata NULL");
  HQASSERT(fbytes > 0, "fbytes should be > 0");

  if ( !im_filevalidate(ffile, ii) )
    return FALSE;

  HQASSERT(ffile->fseek[ii] + fbytes <= ffile->fsize,
           "Tried to read off end of file");

  if ( (*theIReadFile(im_tmpdev))(im_tmpdev, ffile->fdesc[ii],
                                  fdata, fbytes) != fbytes )
    return FAILURE(device_error_handler(im_tmpdev));

  ffile->fseek[ii] += fbytes;
  return TRUE;
}

/**
 * Write the given amount of image data out to the specified file.  Not thread
 * safe, and therefore it is up to the client to ensure thread safety.
 */
Bool im_filewrite(IM_FILES *ffile, uint8 *fdata, int32 fbytes)
{
  unsigned int ii = im_index(ffile);

  HQASSERT(fdata, "fdata NULL");
  HQASSERT(fbytes > 0, "fbytes should be > 0");
  HQASSERT(ffile->writeable, "file must be writeable");

  if ( !im_filevalidate(ffile, ii) )
    return FALSE;

  if ( (*theIWriteFile(im_tmpdev))(im_tmpdev, ffile->fdesc[ii],
                                   fdata, fbytes) != fbytes )
    return device_error_handler(im_tmpdev);

  ffile->fseek[ii] += fbytes;
  return TRUE;
}

/**
 * Close all the file handles opened during interpretation or image adjustment
 * with read/write access.  Renderer threads reopen files with read only access.
 * Not thread safe, but only ever called by a single thread.
 */
Bool im_filecloseall(IM_FILE_CTXT *imfile_ctxt)
{
  Bool result = TRUE;
  IM_FILES *imf;

  HQASSERT(imfile_ctxt != NULL, "No imfile_ctxt");

  for ( imf = imfile_ctxt->head; imf != NULL; imf = imf->next ) {
    unsigned int ii;

    HQASSERT(imf->fdesc, "Somehow lost fdesc array");

    for ( ii = 0; ii < imf->ndesc; ii++ ) {
      if ( imf->fdesc[ii] >= 0 && imf->writeable ) {
        if ( (*theICloseFile(im_tmpdev))(im_tmpdev, imf->fdesc[ii]) < 0 )
          result = device_error_handler(im_tmpdev) && result;
        imf->fdesc[ii] = -1;
        imf->writeable = FALSE;
      }
    }
  }
  return result;
}

/**
 * Create an imfile context to track all the files created for a particular
 * usage. imfile.c is not thread safe, except for multi-threaded reads when the
 * parallel flag is set.
 */
Bool im_filecreatectxt(IM_FILE_CTXT **imfile_ctxt, Bool parallel)
{
  IM_FILE_CTXT *ctxt, init = {0};

  HQASSERT(*imfile_ctxt == NULL, "imfile_ctxt already exists");
  if ( next_ctxt_id == MAXUINT32 ) {
    HQFAIL("Run out of imfile ids");
    return error_handler(LIMITCHECK);
  }

  ctxt = mm_alloc(mm_pool_temp, sizeof(IM_FILE_CTXT),
                  MM_ALLOC_CLASS_IMAGE_FILE);
  if ( ctxt == NULL )
    return error_handler(VMERROR);

  *ctxt = init;
  ctxt->id = next_ctxt_id++;
  ctxt->parallel = parallel;
  *imfile_ctxt = ctxt;
  return TRUE;
}

/**
 * Close and delete any files that have been used to purge image data to disk
 * and free all the associated state.  Not thread safe, but only ever called by
 * a single thread.
 */
void im_filedestroyctxt(IM_FILE_CTXT **imfile_ctxt)
{
  IM_FILES *imf;

  if ( *imfile_ctxt == NULL )
    return;

  for ( imf = (*imfile_ctxt)->head; imf != NULL; imf = imf->next ) {
    unsigned int ii;

    HQASSERT(imf->fdesc, "Somehow lost fdesc array");

    for ( ii = 0; ii < imf->ndesc; ++ii ) {
      if ( imf->fdesc[ii] >= 0 )
        (void)(*theICloseFile(im_tmpdev))(im_tmpdev, imf->fdesc[ii]);
    }
    (void)(*theIDeleteFile(im_tmpdev))(im_tmpdev, imf->fname);

  }
  im_filefree((*imfile_ctxt)->head);
  mm_free(mm_pool_temp, *imfile_ctxt, sizeof(IM_FILE_CTXT));
  *imfile_ctxt = NULL;
}

/* Log stripped */
