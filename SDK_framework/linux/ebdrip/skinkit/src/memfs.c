/* Copyright (C) 2006-2012 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWskinkit!src:memfs.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */

/**
 * \file
 * \ingroup skinkit
 * \brief Implementation of an input/output device tied to in-memory data.
 *
 * This is used as the backend to the RAM device \c %ramdev% described
 * in ramdev.c.
 */

#include <string.h>
#include "file.h"  /* LONGESTFILENAME */
#include "hqstr.h"
#include "memfs.h"
#include "mem.h"
#include "swdevice.h"
#include "zlibutil.h"

/**
 * \brief The default allocation (in bytes) of RAM space for new files.
 */
#define INITIAL_BUFFER_SIZE  4096

/**
 * \brief The size (in bytes) below which we don't bother with
 * compression.
 */
#define COMPRESSION_THRESHOLD 1024

/**
 * \brief The factor by which file data buffers are expanded when
 * the current buffer is exhausted. Must be greater than 1.
 */
#define BUFFER_GROWTH_FACTOR 2

/**
 * \brief The default number of slots allocated for child nodes when a
 * new directory node is added.
 */
#define INITIAL_LIST_SIZE    4

/**
 * \brief The factor by which directory lists are grown when the
 * current list size is not large enough to accommodate a new entry.
 */
#define LIST_GROWTH_FACTOR   2

struct _MFSFILEDESC
{
  /** \brief The underlying file. */
  MFSFILE             *pFile;

  /** \brief The current position of the file pointer. */
  uint32               filePointer;

  /** \brief The level of access that was originally specified to
      <code>MFSOpen()</code>. */
  int32                openFlags;

  /** \brief Indicates that all writes to the file should be appends at
      its end. */
  uint32               fAppend;

  /** \brief Encapsulates the state for reading the file directly from
      its compressed data stream. If the file is opened just for
      reading, and is being consumed sequentially, we use this
      zstream to avoid decompressing the whole file. */
  z_stream             zlibStream;
};

/**
 * @brief Find or create a file node within the tree.
 *
 * <p>This function can locate existing file nodes, as well as create
 * new ones. It also returns enough information about the node's position
 * within its parent directory, so that the caller could decide to unlink
 * the node from the tree.
 *
 * <p>The function works recursively. Recursion is achieved by replacing
 * the first argument with an inner tree node, and correspondingly
 * advancing past the leftmost path element of <code>pszName</code>.
 *
 * <p>When creating new files, this function will automatically create
 * any new directories required to house the file according to its
 * specified pathname.
 *
 * @param pNode Root of the tree to be searched.
 *
 * @param pszName Pathname of the file to be located, which should be
 *  expressed relative to <code>pNode</code>. The caller should ensure
 *  that this string is in writable memory (it must not be a C literal
 *  string). In case of doubt, always make a copy of the string, but the
 *  caller must then also ensure that the copy is freed again later.
 *  The reason for this is that the implementation makes temporary
 *  changes within the string to assist its search algorithm. These
 *  changes are not persistent, but it is possible for the changes to
 *  trigger memory faults if the strings are in ROM (as literal strings
 *  may be).
 *
 * @param flags Configured as per the <code>open_file</code> function
 *  prototype in the Core RIP Device Interface, indicating the required
 *  access to the file. Crucially, this argument determines whether or
 *  not to create a new file.
 *
 * @param ppParent Upon success (including in the file-creation case), this
 *  receives a pointer to the parent node of the file. This will always
 *  be a node of type <code>MFS_Directory</code>.
 *
 * @param pIndex Upon success (including in the file-creation case), this
 *  receives the numeric index of the file within the parent
 *  directory's child array.
 *
 * @param pExistingFile Ignored, except in the file-creation case. When a file is
 *  being created, this can be used to specify an existing file, whose contents
 *  should be transferred to the new file. This feature is used, for example,
 *  when re-naming a file.
 *
 * @return A pointer to the located file node. Returns <code>NULL</code> if
 *  the file was not found (and was not required to be created), or if
 *  the creation attempt failed.
 */
static MFSNODE *findRelativeNode
  ( MFSNODE *pNode, char *pszName, int32 flags, MFSNODE **ppParent,
    uint32 *pIndex, MFSFILE *pExistingFile );

/**
 * \brief Ensure that the file data is ready for access according to the
 * requirements of the given file descriptor.
 *
 * If write access is required, this function calls \c inflateData()
 * to decompress the whole file.
 *
 * If read-only access is required, this function prepares a zlib
 * stream, so that the data can be read sequentially without decompressing
 * the whole file. This is an optimization aimed at saving memory
 * in some typical situations. The caller does not declare that all
 * reads will be sequential, but it is useful for us to start by assuming
 * that they will be. We can then avoid a full decompression until
 * the caller performs the first arbitrary seek operation. If this
 * happens, it will be detected by \c MFSSeek(), which will discard
 * the zlib stream, and call \c inflateData() to decompress the
 * whole file.
 *
 * \param pDesc A valid new file descriptor, which has not yet been
 * used for any I/O operations (unless the caller wishes to reset
 * access to the beginning of the file).
 */
static int32 prepareForAccess( MFSFILEDESC *pDesc );

/**
 * @brief Link a node into the tree.
 *
 * <p>Forms a link between a parent directory and a new node, which may
 * be either a directory or a file, but which <em>must not</em> already
 * be linked into the tree.
 *
 * <p>This function will automatically grow the directory's child node
 * array as necessary.
 *
 * @param pDir The parent directory, into which the new node is inserted.
 *
 * @param pChild The new node.
 *
 * @param pIndex Upon successful return, receives the index of
 *  <code>pChild</code> within the child node array of <code>pDir</code>.
 *  On successful return, <code>pDir->entries[ *pIndex ] == pChild</code>.
 *
 * @return TRUE on success, FALSE on failure. Failure would be due to
 *  memory exhaustion when attempting to grow the child array.
 */
static int32 linkNode( MFSDIR *pDir, MFSNODE *pChild, uint32 *pIndex );

/**
 * @brief Re-allocate a larger data buffer for the given file.
 *
 * @param pFile The file whose buffer is to be grown. The current buffer
 *  will be expanded by a factor <code>BUFFER_GROWTH_FACTOR</code>,
 *  memory-permitting. Otherwise, attempts will be made for less growth.
 *  If the re-allocation succeeds, the old buffer will automatically be
 *  freed (unless it is a static array). The old file contents are copied
 *  into the new buffer, and the <code>cbCapcity</code> and
 *  <code>fDynamicBuffer</code> fields of <code>pFile</code> are updated
 *  for the new buffer.
 *
 * @return A pointer to the new buffer, or <code>NULL</code> if the
 *  allocation failed due to memory exhaustion.
 */
static uint8* growFileBuffer( MFSFILE *pFile );

/**
 * @brief Set <code>pFile->pData</code> to NULL, and de-allocate the buffer
 * if it was runtime allocated.
 *
 * @param pFile The file whose buffer is being discarded.
 */
static void shedFileBuffer( MFSFILE *pFile );

/**
 * @brief Completely destroy a node and all of its descendants
 * inclusively.
 *
 * <p>Because nodes have no internal pointers to their parent, this function
 * <em>does not</em> unlink <code>pNode</code> from its directory. The
 * caller is assumed to have done this separately, by NULLing out the
 * appropriate slot in the directory's child node array, before calling
 * this function to destroy the unlinked sub-tree.
 *
 * <p>All dynamically-allocated memory associated with
 * <code>pNode</code> and its descendants will be freed, with the
 * exception of file data buffers if <code>fDestroyBuffer</code> is
 * <code>FALSE</code>.
 *
 * <p>Upon return from this function, <code>pNode</code> will be a
 * stale pointer, and should not be used again.
 *
 * @param pNode The root of the sub-tree to destroy. This node will
 * itself also be destroyed.
 *
 * @param fDestroyBuffer Controls whether the internal data buffers of
 * file nodes are de-allocated. Normally, you would pass <code>TRUE</code>.
 * The only sensible exception is where <code>pNode</code> is a
 * leaf file, whose buffer has just been assigned elsewhere. In this
 * case, operation is non-recursive. There is no value in recursively
 * destroying a sub-tree without de-allocating the file buffers: they
 * will simply leak.
 */
static void destroyNode( MFSNODE *pNode, uint32 fDestroyBuffer );

/**
 * @brief Construct and return a single new node, without linking it into
 * a directory tree.
 *
 * @param pszName The <em>leaf</em> name of the node. The implementation
 * will create a freshly-allocated copy of this string.
 *
 * @param fIsDirectory Determines whether the new node should be
 * of type <code>MFS_Directory</code> or <code>MFS_File</code>.
 *
 * @param pExistingFile When making a file node, this pointer can optionally
 * specify a pre-existing file, whose fields (including the data buffer pointers)
 * will be copied into the new file. If you pass <code>NULL</code>, then
 * a fresh file will be made, with a freshly-allocated, empty data buffer
 * of default initial size. This parameter is ignored completely if
 * <code>fIsDirectory == TRUE</code>.
 */
static MFSNODE *makeNode
   ( char *pszName, uint32 fIsDirectory, MFSFILE *pExistingFile );

/**
 * @brief Decompress the file's data buffer using zlib.
 *
 * <p>If the file's <code>fCompressed</code> field is <code>TRUE</code>,
 * use this function to decompress its data stream before trying to
 * read/write the file.
 *
 * <p>This function is idempotent. If you call it redundantly, it will
 * behave as a no-op and return <code>TRUE</code>.
 *
 * @param pFile The file to decompress.
 *
 * @return TRUE on success, FALSE on failure.
 */
static int32 inflateData( MFSFILE *pFile );

/**
 * @brief Compress the file's data buffer using zlib.
 *
 * <p>If the given file already has a compressed data stream, and it has
 * not been modified, then this function just throws away the uncompressed
 * data, since it is not necessary to re-compress an unmodified file.
 *
 * @param pFile The file to compress.
 *
 * @return TRUE on success, FALSE on failure.
 */
static int32 deflateData( MFSFILE *pFile );

/**
 * @brief Determine the degree of open file access beneath a particular
 * node.
 *
 * <p>This function can be used to determine whether it is currently
 * safe to delete a directory, for example.
 *
 * @param pNode The node to descend from.
 *
 * @param pnReaders Receives the number of open file descriptors for
 * read access. Should be initialized to zero by the caller.
 *
 * @param pnWriters Receives the number of open file descriptors for
 * write access. Should be initialized to zero by the caller.
 */
static void countOpenFiles( MFSNODE *pNode, uint32 *pnReaders, uint32 *pnWriters );

static void shedFileBuffer( MFSFILE *pFile )
{
  if ( pFile->fDynamicBuffer )
    MemFree( (void*) pFile->pData );

  pFile->pData = NULL;
  pFile->fDynamicBuffer = FALSE;
  pFile->cbCapacity = 0;
}

static int32 inflateData( MFSFILE *pFile )
{
  uint8 *pBuf = NULL;
  int32 result;

  /* No-op if we already have an uncompressed data buffer. */
  if ( pFile->pData != NULL )
    return TRUE;

  /* There is no uncompressed data, but the file is marked as non-compressed,
     so there won't be a compressed buffer either. The file is therefore in
     an inconsistent state, so this case is an error. */
  if ( !pFile->fCompressed )
    return FALSE;

  pBuf = (uint8*) MemAlloc( pFile->cbSize, FALSE, FALSE );

  if ( pBuf == NULL )
    return FALSE;

  result = gg_uncompress
    (
      pBuf,
      &pFile->cbSize,
      pFile->pCompressedData,
      pFile->cbCompressedSize
    );

  if ( result != Z_OK )
  {
    MemFree( (void*) pBuf );
    return FALSE;
  }
  else
  {
    pFile->pData = pBuf;
    pFile->fDynamicBuffer = TRUE;
    pFile->cbCapacity = pFile->cbSize;

    /* If the compressed stream is a dynamic array, free it off. We don't want
       the uncompressed AND the compressed arrays hogging memory at the same
       time. We can re-allocate the compressed stream again later. */
    if ( pFile->fDynamicCompressedBuffer )
    {
      MemFree( (void*) pFile->pCompressedData );
      pFile->fDynamicCompressedBuffer = FALSE;
      pFile->pCompressedData = NULL;
    }

    return TRUE;
  }
}

static int32 deflateData( MFSFILE *pFile )
{
  int32 zresult;
  uint8 *pBuf;

  /* If the file was not compressed originally, or has been explicitly marked
     to remain uncompressed, then return immediately. */

  if ( !pFile->fCompressed )
    return TRUE;

  /* If the current size of the file is too small for compression to
     be worthwhile, then just preserve the existing data buffer. We don't
     set fCompressed to FALSE, though, because that would cause compression
     to be de-activated persistently, even if future transactions were
     to grow the file back above the threshold. The compression threshold
     is a lightweight decision, compared with the fCompressed flag. */

  if ( pFile->cbSize <= COMPRESSION_THRESHOLD )
    return TRUE;

  /* Strategy 1: If the buffer is unmodified, and there is already a
     compressed data array, then just shed the uncompressed buffer. */

  if ( !pFile->fModified && pFile->pCompressedData != NULL )
  {
    shedFileBuffer( pFile );
    return TRUE;
  }

  /* Strategy 2: If there is a compressed data buffer, then try to
     compress the (modified) data back into it. */

  if ( pFile->pCompressedData != NULL )
  {
    zresult = gg_compress
      (
        pFile->pCompressedData,
        &pFile->cbCompressedSize,
        pFile->pData,
        pFile->cbSize
      );

    if ( zresult == Z_OK )
    {
      shedFileBuffer( pFile );
      return TRUE;
    }
    else
    {
      /* The existing compression buffer is not big enough. Free it off, if
         it's dynamic. */
      if ( pFile->fDynamicCompressedBuffer )
        MemFree( (void*) pFile->pCompressedData );

      pFile->pCompressedData = NULL;
      pFile->fDynamicCompressedBuffer = FALSE;
    }
  }

  /* Strategy 3: Allocate a new compressed array, and try to
     compress into it. The new array is made equal to the uncompressed
     buffer size. If this fails, then we abort compression altogether. If
     it succeeds, then we allocate a precisely-sized buffer. */

  pBuf = (uint8*) MemAlloc( pFile->cbSize, FALSE, FALSE );
  if ( pBuf == NULL )
    return FALSE;

  pFile->cbCompressedSize = pFile->cbSize;

  zresult = gg_compress
    (
      pBuf,
      &pFile->cbCompressedSize,
      pFile->pData,
      pFile->cbSize
    );

  if ( zresult == Z_OK )
  {
    uint8 *pPrecise;

    /* We have a compressed array, so we can commit to freeing off the
       uncompressed data. */
    shedFileBuffer( pFile );

    /* Now try to slim down to a precisely-sized buffer. */
    pPrecise = (uint8*) MemAlloc( pFile->cbCompressedSize, FALSE, FALSE );

    /* Since we've just freed a buffer >cbCompressedSize, the above alloc
       really ought to succeed. If it doesn't, then just install pBuf
       as the compressed data. */

    if ( pPrecise != NULL )
    {
      memcpy( pPrecise, pBuf, CAST_UNSIGNED_TO_SIZET(pFile->cbCompressedSize) );
      MemFree( (void*) pBuf );
      pBuf = pPrecise;
    }

    pFile->pCompressedData = pBuf;
    pFile->fDynamicCompressedBuffer = TRUE;
    return TRUE;
  }
  else
  {
    /* No more strategies. Leave the file in an uncompressed state. */
    MemFree( (void*) pBuf );
    return FALSE;
  }
}

static MFSNODE *makeNode
  ( char *pszName, uint32 fIsDirectory, MFSFILE *pExistingFile )
{
  MFSNODE *pNew = NULL;
  MFSDIR *pNewDir = NULL;
  MFSFILE *pNewFile = NULL;
  MFSNODE **entries = NULL;
  uint8 *pBuf = NULL;

  char *pNameCopy = (char*) MemAlloc( strlen_uint32( pszName ) + 1, TRUE, FALSE );

  if ( pNameCopy == NULL )
    return NULL;

  strcpy( pNameCopy, pszName );

  if ( fIsDirectory )
  {
    /* We are creating a directory. */

    entries = (MFSNODE**) MemAlloc
      ( INITIAL_LIST_SIZE * sizeof( MFSNODE* ), TRUE, FALSE );
    pNewDir = (MFSDIR*) MemAlloc( sizeof( MFSDIR ), TRUE, FALSE );
    pNew = (MFSNODE*) MemAlloc( sizeof( MFSNODE ), TRUE, FALSE );

    if ( entries != NULL && pNewDir != NULL && pNew != NULL )
    {
      pNewDir->nEntries = INITIAL_LIST_SIZE;
      pNewDir->entries = entries;
      pNewDir->fDynamicList = TRUE;

      pNew->type = MFS_Directory;
      pNew->fReadOnly = FALSE;
      pNew->fDynamic = TRUE; /* Runtime-allocated node. */
      pNew->pszName = pNameCopy;
      pNew->pDir = pNewDir;
    }
  }
  else
  {
    if ( pExistingFile == NULL )
    {
      /* No existing file, so allocate a fresh initial data buffer. */
      pBuf = (uint8*) MemAlloc( INITIAL_BUFFER_SIZE, TRUE, FALSE );
    }

    /* Either: the above allocation succeeded, OR there is an existing file
       that made the allocation unnecessary. We can't proceed unless one
       of these conditions is met. */
    if ( pBuf != NULL || pExistingFile != NULL )
    {
      pNewFile = (MFSFILE*) MemAlloc( sizeof( MFSFILE ), TRUE, FALSE );
      pNew = (MFSNODE*) MemAlloc( sizeof( MFSNODE ), TRUE, FALSE );
    }

    if ( pNewFile != NULL && pNew != NULL )
    {
      if ( pExistingFile == NULL )
      {
        /* We are creating a fresh file with default initial data buffer. */
        pNewFile->cbSize = 0;
        pNewFile->pData = pBuf;
        pNewFile->nReaders = 0;
        pNewFile->nWriters = 0;
        pNewFile->fDynamicBuffer = TRUE;
        pNewFile->cbCapacity = INITIAL_BUFFER_SIZE;
        pNewFile->fCompressed = COMPRESS_NEW_FILES;
        pNewFile->cbCompressedSize = 0;
        pNewFile->fDynamicCompressedBuffer = FALSE;
        pNewFile->pCompressedData = NULL;
        pNewFile->fModified = FALSE;
      }
      else
      {
        /* We are copying all fields across from the existing file. */
        (*pNewFile) = (*pExistingFile);
      }

      pNew->type = MFS_File;
      pNew->fReadOnly = FALSE;
      pNew->fDynamic = TRUE; /* Runtime-allocated node. */
      pNew->pszName = pNameCopy;
      pNew->pFile = pNewFile;
    }
  }

  /* Tidy up any partially-allocated structures if the overall node creation
     has failed. */
  if ( pNew == NULL )
  {
    if ( pNameCopy != NULL ) MemFree( (void*) pNameCopy );
    if ( pNewDir != NULL ) MemFree( (void*) pNewDir );
    if ( entries != NULL ) MemFree( (void*) entries );
    if ( pNewFile != NULL ) MemFree( (void*) pNewFile );
    if ( pBuf != NULL ) MemFree( (void*) pBuf );
  }

  return pNew;
}

static void countOpenFiles( MFSNODE *pNode, uint32 *pnReaders, uint32 *pnWriters )
{
  if ( pNode != NULL )
  {
    if ( pNode->type == MFS_File )
    {
      (*pnReaders) += pNode->pFile->nReaders;
      (*pnWriters) += pNode->pFile->nWriters;
    }
    else
    {
      MFSDIR *pDir = pNode->pDir;
      uint32 i;
      for ( i = 0; i < pDir->nEntries; i++ )
      {
        countOpenFiles( pDir->entries[ i ], pnReaders, pnWriters );
      }
    }
  }
}

static MFSNODE *findRelativeNode
  ( MFSNODE *pNode, char *pszName, int32 flags, MFSNODE **ppParent,
    uint32 *pIndex, MFSFILE *pExistingFile )
{
  (*ppParent) = NULL;
  (*pIndex) = 0;

  if ( pNode == NULL )
    return NULL;

  if ( pszName[ 0 ] == '\0' )
    return NULL;

  if ( pszName[ 0 ] == '/' )
    pszName ++;

  if ( pNode->type == MFS_File )
  {
    return NULL;
  }
  else
  {
    char *pTmp = pszName;
    int32 fSlash = FALSE;
    uint32 i;
    MFSNODE *pNew = NULL;

    while ( *pTmp != '\0' )
    {
      if ( *pTmp == '/' )
      {
        *pTmp = '\0';
        fSlash = TRUE;
        break;
      }
      else
      {
        pTmp++;
      }
    }

    /* Temporarily, pszName is now a null-terminated string containing just
       the first path element. */

    for ( i = 0; i < pNode->pDir->nEntries; i++ )
    {
      MFSNODE *pChild = pNode->pDir->entries[ i ];
      if ( pChild != NULL ) /* Skip deleted entries. */
      {
        if ( !strcmp( pszName, pChild->pszName ) )
        {
          if ( fSlash )
          {
            *pTmp = '/'; /* Replace the slash.
                            Recursive call will advance past it. */
            return findRelativeNode
              ( pChild, pTmp, flags, ppParent, pIndex, pExistingFile );
          }
          else
          {
            (*ppParent) = pNode;
            (*pIndex) = i;
            return pChild;
          }
        }
      }
    }

    /* No matching directory entry. Bomb out, unless we have been asked to
       create a new file (in a writable directory). */
    if ( ( flags & SW_CREAT ) && !pNode->fReadOnly )
    {
      if ( fSlash )
      {
        /* A file that does not exist has been specified in a directory that
           does not exist. Make the new directory en route to making the
           file. Recursion is the cleanest (if perhaps not the most efficient)
           way of building up the new nested directories and eventually making
           the file. */
        pNew = makeNode( pszName, TRUE, NULL );
        *pTmp = '/';
        linkNode( pNode->pDir, pNew, pIndex );
        return findRelativeNode( pNew, pTmp, flags, ppParent, pIndex, pExistingFile );
      }
      else
      {
        /* Just make a new leaf-case file. */
        pNew = makeNode( pszName, FALSE, pExistingFile );
        (*ppParent) = pNode;
        linkNode( pNode->pDir, pNew, pIndex );
      }
    }

    if ( fSlash )
      *pTmp = '/';

    /* If we were not asked to create a new file, or we failed to create
       it, pNew holds NULL. Otherwise, pNew is the new file node. */
    return pNew;
  }
}

static int32 linkNode( MFSDIR *pDir, MFSNODE *pChild, uint32 *pIndex )
{
  uint32 i;
  MFSNODE **newList;
  uint32 newSize;

  for ( i = 0; i < pDir->nEntries; i++ )
  {
    if ( pDir->entries[ i ] == NULL )
    {
      /* Found an unused slot. */
      pDir->entries[ i ] = pChild;
      (*pIndex) = i;
      return TRUE;
    }
  }

  /* No free space. Need to grow the array. */
  newSize = pDir->nEntries * LIST_GROWTH_FACTOR;
  newSize = newSize ? newSize : 1 ; /* if pDir->nEntries is 0 set new size to 1 */

  newList = (MFSNODE**) MemAlloc( newSize * sizeof(MFSNODE*), TRUE, FALSE );

  if ( newList == NULL )
  {
    /* Memory exhaustion. */
    return FALSE;
  }

  /* Copy original list. */
  for ( i = 0; i < pDir->nEntries; i++ )
  {
    newList[ i ] = pDir->entries[ i ];
  }

  /* Add new entry to end. */
  newList[ pDir->nEntries ] = pChild;
  (*pIndex) = pDir->nEntries;

  /* Free the old list, if it was dynamic */
  if ( pDir->fDynamicList )
  {
    MemFree( (void*) pDir->entries );
  }

  /* Replace the list and size. */
  pDir->fDynamicList = TRUE; /* This one must be freed on the next growth. */
  pDir->entries = newList;
  pDir->nEntries = newSize;

  return TRUE;
}

static uint8* growFileBuffer( MFSFILE *pFile )
{
  /* Grow the data buffer. */
  uint8 *pNewBuffer = NULL;
  uint32 cbNewCapacity = 0;
  int32 i;

  for (i = 4; i > 0; i--)
  {
    /* We gradually reduce our attempt by a quarter of requested growth */
    cbNewCapacity = pFile->cbCapacity +
                    pFile->cbCapacity * (BUFFER_GROWTH_FACTOR - 1) * i / 4 ;
    /* If empty file, set new capacity to default size */
    cbNewCapacity = cbNewCapacity ? cbNewCapacity : INITIAL_BUFFER_SIZE ;
    pNewBuffer = (uint8*) MemAlloc( cbNewCapacity, TRUE, FALSE );
    if (pNewBuffer != NULL)
      break;
  }

  if ( pNewBuffer != NULL )
  {
    /* Copy the original data. */
    memcpy( pNewBuffer, pFile->pData, CAST_UNSIGNED_TO_SIZET(pFile->cbSize) );

    /* If the old buffer was dynamically-allocated, free it now. */
    if ( pFile->fDynamicBuffer )
    {
      MemFree( (void*) pFile->pData );
    }

    /* Update for new dynamic buffer. */
    pFile->fDynamicBuffer = TRUE;
    pFile->cbCapacity = cbNewCapacity;
    pFile->pData = pNewBuffer;
  }

  return pNewBuffer; /* Will be NULL if allocation failed. */
}

static int32 prepareForAccess( MFSFILEDESC *pDesc )
{
  if ( pDesc->pFile->pData != NULL )
  {
    /* No preparations needed, because the uncompressed data buffer is
       already available, and all I/O operations can be performed
       directly on that data. */
    return TRUE;
  }
  else
  {
    /* The uncompressed data buffer is not available. */
    if ( pDesc->pFile->pCompressedData == NULL )
    {
      /* No compressed data either! This is an error. */
      return FALSE;
    }
    else if ( pDesc->openFlags & SW_RDONLY )
    {
      /* Read-only access. Prepare to read the file through a zlib
         stream. As long as all reads are sequential, we will not
         need to fully decompress the file data. Full decompression will
         only take place if the reader attempts arbitrary seeks
         within the file. */
      pDesc->zlibStream.avail_in = 0;
      pDesc->zlibStream.next_in = Z_NULL;
      if ( gg_inflateInit( &pDesc->zlibStream) != Z_OK )
        return FALSE;

      /* Make the entire compressed file available as input. */
      pDesc->zlibStream.next_in = pDesc->pFile->pCompressedData;
      pDesc->zlibStream.avail_in = pDesc->pFile->cbCompressedSize;
      return TRUE;
    }
    else
    {
      /* We need to fully decompress the file. */
      return inflateData( pDesc->pFile );
    }
  }
}

static void destroyNode( MFSNODE *pNode, uint32 fDestroyBuffer )
{
  if ( pNode != NULL )
  {
    if ( pNode->type == MFS_File )
    {
      /* Destroy file-specific data. */
      MFSFILE *pFile = pNode->pFile;

      if ( pFile->fDynamicBuffer && fDestroyBuffer )
        MemFree( (void*) pFile->pData );

      if ( pFile->fDynamicCompressedBuffer && fDestroyBuffer )
        MemFree( (void*) pFile->pCompressedData );

      if ( pNode->fDynamic )
        MemFree( (void*) pFile );
    }
    else
    {
      /* Recursively destroy child nodes. */
      uint32 i;
      MFSDIR *pDir = pNode->pDir;
      for ( i = 0; i < pDir->nEntries; i++ )
      {
        destroyNode( pDir->entries[ i ], fDestroyBuffer );
        pDir->entries[ i ] = NULL;
      }

      /* Destroy directory-specific data. */
      if ( pDir->fDynamicList )
        MemFree( (void*) pDir->entries );

      if ( pNode->fDynamic )
        MemFree( (void*) pDir );
    }

    /* De-allocate this node, if it is a runtime object. */
    if ( pNode->fDynamic )
    {
      MemFree( (void*) pNode->pszName );
      MemFree( (void*) pNode );
    }
  }
}

MFSNODE *MFSNewRoot( char *pszRootName )
{
  return makeNode( pszRootName, TRUE, NULL );
}

void MFSReleaseRoot( MFSNODE *pMFSRoot )
{
  destroyNode( pMFSRoot, TRUE );
}

MFSNODE *MFSCopyTree( MFSNODE *pMFSRoot, uint32 fCopyFileData )
{
  MFSNODE *pRootOfCopy = MFSNewRoot( pMFSRoot->pszName );

  if ( pRootOfCopy != NULL )
  {
    MFSITERSTATE *pState = MFSIterBegin( pMFSRoot, NULL );

    uint32 fSucceeded = TRUE;
    while( fSucceeded && MFSIterNext( pState ) )
    {
      char szThisFilename[ LONGESTFILENAME ];
      MFSNODE *pThisFile;

      /* We are visiting a file in the existing tree. Get its full
         path name, and a pointer to the file node. */
      MFSIterName( pState, szThisFilename, sizeof( szThisFilename ), TRUE );
      pThisFile = MFSIterNode( pState );

      if ( fCopyFileData )
      {
        MFSFILEDESC *pFromFile = NULL;
        MFSFILEDESC *pToFile = NULL;
        int32 errcode;

        /* Open the file for reading in the existing tree. */
        if ( MFSOpen( pMFSRoot, szThisFilename, SW_RDONLY, &pFromFile, &errcode ) )
        {
          /* Create and open the file for writing in the new tree. */
          if ( MFSOpen( pRootOfCopy, szThisFilename, SW_CREAT | SW_WRONLY,
                        &pToFile, &errcode ) )
          {
            /* Now just chunk-wise copy the data via a 1K buffer. */
            uint8 buffer[ 1024 ];
            uint32 fEof = FALSE;

            while ( fSucceeded && !fEof )
            {
              int32 cbRead = MFSRead( pFromFile, buffer, sizeof( buffer ),
                                      &errcode );
              if ( cbRead == 0 )
                fEof = TRUE;
              else if ( cbRead < 0 )
                fSucceeded = FALSE;
              else
                fSucceeded =
                  ( MFSWrite( pToFile, buffer, cbRead, &errcode ) == cbRead );
            }

            MFSClose( pToFile );
          }
          else
          {
            fSucceeded = FALSE;
          }

          MFSClose( pFromFile );
        }
        else
        {
          fSucceeded = FALSE;
        }
      }
      else
      {
        /* We are not going to copy the actual data in the file, so use a simpler
           method of creating a new file with the same name, and passing the
           descriptor of the existing file, so that its fields can be inherited
           by the copy. */
        MFSNODE *pThisFileCopy;
        MFSNODE *pParent;
        uint32 idx;

        pThisFileCopy = findRelativeNode
          ( pRootOfCopy, szThisFilename, SW_CREAT, &pParent, &idx, pThisFile->pFile );

        if ( pThisFileCopy == NULL )
          fSucceeded = FALSE;
      }
    }

    MFSIterEnd( pState );

    if ( !fSucceeded )
    {
      MFSReleaseRoot( pRootOfCopy );
      pRootOfCopy = NULL;
    }
  }

  return pRootOfCopy;
}

MFSNODE *MFSFindRelative
  ( MFSNODE *pRoot, char *pszFilename, MFSNODE **ppParent, uint32 *pIndex )
{
  char szFilenameCopy[ LONGESTFILENAME ];
  strcpy( szFilenameCopy, pszFilename );
  return findRelativeNode( pRoot, szFilenameCopy, SW_RDONLY, ppParent, pIndex, NULL );
}

int32 MFSDelete( MFSNODE *pRoot, char *pszFilename, int32 *err )
{
  MFSNODE *pNode;
  MFSNODE *pParent;
  uint32 i;
  uint32 nReaders = 0;
  uint32 nWriters = 0;
  char szFilenameCopy[ LONGESTFILENAME ];

  /* Copy the string as required by findRelativeNode() */
  strcpy( szFilenameCopy, pszFilename );

  /* Ensure that the copy is always used instead of the original */
  pszFilename = szFilenameCopy;

  pNode = findRelativeNode( pRoot, pszFilename, SW_RDONLY, &pParent, &i, NULL );

  if ( pNode == NULL )
  {
    *err = DeviceUndefined;
    return -1;
  }

  countOpenFiles( pNode, &nReaders, &nWriters );

  if ( ( nReaders + nWriters ) > 0 )
  {
    *err = DeviceInvalidAccess;
    return -1;
  }

  pParent->pDir->entries[ i ] = NULL;
  destroyNode( pNode, TRUE );

  return 0;
}

int32 MFSRename( MFSNODE *pRoot, char *pszFromName, char *pszToName, int32 *err )
{
  MFSNODE *pFromNode, *pToNode;
  MFSNODE *pFromParent, *pToParent;
  uint32 iFrom, iTo;
  char szFromNameCopy[ LONGESTFILENAME ];
  char szToNameCopy[ LONGESTFILENAME ];

  /* Copy strings as required by findRelativeNode() */
  strcpy( szFromNameCopy, pszFromName );
  strcpy( szToNameCopy, pszToName );

  /* Ensure that the copies will always be used instead of the originals. */
  pszFromName = szFromNameCopy;
  pszToName = szToNameCopy;

  pFromNode =
    findRelativeNode( pRoot, pszFromName, SW_RDONLY, &pFromParent, &iFrom, NULL );

  if ( pFromNode == NULL || pFromNode->type != MFS_File )
  {
    /* Source does not exist, or is not a file. */
    *err = DeviceUndefined;
    return -1;
  }

  if ( ( pFromNode->pFile->nReaders + pFromNode->pFile->nWriters ) > 0 )
  {
    /* Source is a file, but it is open. */
    *err = DeviceInvalidAccess;
    return -1;
  }

  pToNode = findRelativeNode( pRoot, pszToName, SW_RDONLY, &pToParent, &iTo, NULL );

  if ( pToNode != NULL )
  {
    /* Target already exists, regardless of whether it is a directory
       or a file. */
    *err = DeviceInvalidAccess;
    return -1;
  }

  /* Create the destination file as a new file, but pass the original file
     so that its data fields can be re-used. */
  pToNode = findRelativeNode
    ( pRoot, pszToName, SW_CREAT, &pToParent, &iTo, pFromNode->pFile );

  if ( pToNode == NULL )
  {
    *err = DeviceVMError;
    return -1;
  }

  pToNode->fReadOnly = pFromNode->fReadOnly;

  /* Unlink and destroy the source file, but not including the data buffers,
     because they has been re-assigned to the new file. */
  pFromParent->pDir->entries[ iFrom ] = NULL;
  destroyNode( pFromNode, FALSE );

  return 0;
}

int32 MFSOpen
  ( MFSNODE *pRoot, char *pszFilename, int32 openFlags, MFSFILEDESC **ppDesc,
    int32 *err )
{
  MFSNODE *pParent, *pNode;
  uint32 index;
  char szFilenameCopy[ LONGESTFILENAME ];

  /* Copy the filename as required by contract of findRelativeNode() */
  strcpy( szFilenameCopy, pszFilename );

  /* Ensure that the copy is always used instead of the original. */
  pszFilename = szFilenameCopy;

  pNode =
    findRelativeNode( pRoot, pszFilename, openFlags, &pParent, &index, NULL );

  (*ppDesc) = NULL;

  if ( pNode == NULL )
  {
    /* We did not find the named file. The nature of this error depends on
       whether file creation was specified. If it was, then it must have been
       a permissions problem. Otherwise, it is just an undefined filename. */
    if ( openFlags & SW_CREAT )
      *err = DeviceInvalidAccess;
    else
      *err = DeviceUndefined;

    return FALSE;
  }
  else if ( pNode->type != MFS_File )
  {
    *err = DeviceUndefined;
    return FALSE;
  }
  else
  {
    MFSFILE *pFile = pNode->pFile;
    MFSFILEDESC *pDesc;

    if ( !(openFlags & SW_RDONLY ) )
    {
      /* Write access is desired. Block this if the file is a read-only node,
         or if someone else is already writing. */
      if ( pNode->fReadOnly || pFile->nWriters > 0 )
      {
        /* Collision on write. */
        *err = DeviceInvalidAccess;
        return FALSE;
      }
    }

    pDesc = (MFSFILEDESC*) MemAlloc( sizeof( MFSFILEDESC ), FALSE, FALSE );

    if ( pDesc == NULL )
    {
      *err = DeviceVMError;
      return FALSE;
    }
    else
    {
      pDesc->pFile = pFile;
      pDesc->filePointer = 0;
      pDesc->openFlags = openFlags;
      pDesc->fAppend = FALSE;

      if ( !prepareForAccess( pDesc ) )
      {
        /* Preparations for accessing the file failed. */
        MemFree( (void*) pDesc );
        *err = DeviceIOError;
        return FALSE;
      }

      if ( !( openFlags & SW_WRONLY ) )
        pFile->nReaders++;

      if ( !( openFlags & SW_RDONLY ) )
      {
        pFile->nWriters++;
        if ( openFlags & SW_APPEND )
        {
          /* All writes will add content to end of file. */
          pDesc->fAppend = TRUE;
        }
        if ( openFlags & SW_TRUNC )
        {
          /* Set file size to 0. */
          pFile->cbSize = 0;
        }
      }

      (*ppDesc) = pDesc;
      return TRUE;
    }
  }
}

MFSFILE *MFSGetFile( MFSFILEDESC *pDesc )
{
  return pDesc->pFile;
}

int32 MFSClose( MFSFILEDESC *pDesc )
{
  MFSFILE *pFile = pDesc->pFile;

  if ( ( pDesc->openFlags & SW_RDONLY ) && ( pFile->pData == NULL ) )
    /* If we had read-only access, and never decompressed the file, we
       will have open zlib state that needs to be closed. */
    (void) inflateEnd( &pDesc->zlibStream );

  if ( !( pDesc->openFlags & SW_WRONLY ) )
    pFile->nReaders--;

  if ( !( pDesc->openFlags & SW_RDONLY ) )
    pFile->nWriters--;

  MemFree( (void*) pDesc );

  if ( ( pFile->nReaders + pFile->nWriters ) == 0 )
  {
    if ( pFile->pData != NULL )
      deflateData( pFile );
    pFile->fModified = FALSE;
  }

  return TRUE;
}

int32 MFSRead( MFSFILEDESC *pDesc, uint8 *buffer, int32 cbLen, int32 *err )
{
  MFSFILE *pFile = pDesc->pFile;
  uint32 cbAvail = pFile->cbSize - pDesc->filePointer;

  if ( pDesc->openFlags & SW_WRONLY )
  {
    *err = DeviceInvalidAccess;
    return -1;
  }

  if ( cbLen < 0 )
  {
    *err = DeviceLimitCheck;
    return -1;
  }

  if ( cbAvail == 0 )
    return 0;

  if ( (uint32) cbLen > cbAvail )
    cbLen = cbAvail;

  if ( pFile->pData != NULL )
  {
    /* The file data is uncompressed, so we can read from the file with a
       simple memcpy. */
    memcpy( buffer, pFile->pData + pDesc->filePointer, CAST_SIGNED_TO_SIZET(cbLen) );
  }
  else if ( pFile->pCompressedData == NULL )
  {
    /* There is no compressed data either, which is an error. */
    *err = DeviceIOError;
    return -1;
  }
  else
  {
    /* The file data is compressed. We must be reading it through its
       zlib stream. */
    int zret;
    pDesc->zlibStream.avail_out = cbLen;
    pDesc->zlibStream.next_out = buffer;

    zret = inflate( &pDesc->zlibStream, Z_NO_FLUSH );

    if ( zret != Z_OK && zret != Z_STREAM_END )
    {
      *err = DeviceIOError;
      return -1;
    }
  }

  pDesc->filePointer += cbLen;

  return cbLen;
}

int32 MFSWrite( MFSFILEDESC *pDesc, uint8 *buffer, int32 cbLen, int32 *err )
{
  MFSFILE *pFile = pDesc->pFile;
  uint32 cbAvail;

  if ( pDesc->openFlags & SW_RDONLY )
  {
    *err = DeviceInvalidAccess;
    return -1;
  }

  if ( cbLen < 0 )
  {
    *err = DeviceLimitCheck;
    return -1;
  }

  if ( pDesc->fAppend )
  {
    /* Write appends data to end of file */
    pDesc->filePointer = pFile->cbSize;
  }

  cbAvail = pFile->cbCapacity - pDesc->filePointer;

  while ( (uint32) cbLen > cbAvail )
  {
    /* Grow the data buffer. */
    if ( growFileBuffer( pFile ) == NULL )
    {
      *err = DeviceIOError;
      return -1;
    }

    cbAvail = pFile->cbCapacity - pDesc->filePointer;
  }

  memcpy( pFile->pData + pDesc->filePointer, buffer, CAST_SIGNED_TO_SIZET(cbLen) );
  pDesc->filePointer += cbLen;
  pFile->fModified = TRUE;

  if ( pDesc->filePointer > pFile->cbSize )
    pFile->cbSize = pDesc->filePointer;

  return cbLen;
}

int32 MFSWriteString( MFSFILEDESC *pDesc, char *pszString, int32 *err )
{
  return MFSWrite( pDesc, (uint8*) pszString, strlen_int32( pszString ), err );
}

int32 MFSSeek( MFSFILEDESC *pDesc, Hq32x2 *pDestination, int32 flags, int32 *err )
{
  int32 delta;
  uint32 destination;

  if ( !Hq32x2ToInt32( pDestination, &delta ) )
    return FALSE;

  switch( flags )
  {
    case SW_SET:
      destination = delta;
      break;

    case SW_XTND:
      destination = pDesc->pFile->cbSize + delta - 1;
      break;

    case SW_INCR:
      destination = pDesc->filePointer + delta;
      break;

    default:
      return FALSE;
  }

  if ( destination <= pDesc->pFile->cbSize )
  {
    /* Seek is in range of the file's current size. */
    if ( pDesc->pFile->pData == NULL && destination != pDesc->filePointer )
    {
      /* The uncompressed file data is not available, meaning we are consuming
         the file through a zlib stream. Since the seek has modified the current
         file pointer, we need to discard any current zlib state. */
      (void) inflateEnd( &pDesc->zlibStream );

      if ( destination == 0 )
      {
        /* Re-seek to beginning of file. This is okay. We can just reset the
           zlib stream. It's like a lightweight re-opening of the file. */
        if ( !prepareForAccess( pDesc ) )
          return FALSE;
      }
      else
      {
        /* The seek is arbitrary, so just inflate the whole file. */
        if ( !inflateData( pDesc->pFile ) )
          return FALSE;
      }
    }

    pDesc->filePointer = destination;
    Hq32x2FromUint32( pDestination, destination );
    return TRUE;
  }
  else
  {
    /* Seek out of range for file's current size. If it is writable, then it's
       okay, but we may need to grow the data buffer. */
    if ( pDesc->openFlags & SW_RDONLY )
    {
      return FALSE;
    }
    else
    {
      MFSFILE *pFile = pDesc->pFile;
      while ( destination > pFile->cbCapacity )
      {
        /* Grow the data buffer. */
        if ( growFileBuffer( pFile ) == NULL )
        {
          *err = DeviceIOError;
          return FALSE;
        }
      }
      pDesc->filePointer = destination;
      Hq32x2FromUint32( pDestination, destination );
      return TRUE;
    }
  }
}

int32 MFSSetCompression( MFSFILEDESC *pDesc, uint32 fCompress )
{
  MFSFILE *pFile = pDesc->pFile;

  if ( pFile->pData )
  {
    pFile->fCompressed = fCompress;
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

int32 MFSAvail( MFSFILEDESC *pDesc, Hq32x2 *pAvail, int32 reasonFlag )
{
  MFSFILE *pFile = pDesc->pFile;

  switch ( reasonFlag )
  {
     case SW_BYTES_AVAIL_REL:
       Hq32x2FromUint32( pAvail, pFile->cbSize - pDesc->filePointer );
       return TRUE;

     case SW_BYTES_TOTAL_ABS:
       Hq32x2FromUint32( pAvail, pFile->cbSize );
       return TRUE;

     default:
       return FALSE;
  }
}

MFSITERSTATE *MFSIterBegin( MFSNODE *pRoot, void *pPrivate )
{
  MFSITERSTATE *pState = NULL;

  if ( pRoot->type != MFS_Directory )
    return NULL;

  pState = MemAlloc( sizeof( MFSITERSTATE ), TRUE, FALSE );

  if ( pState == NULL )
    return NULL;

  pState->stack[ 0 ].pDirNode = pRoot;
  pState->stack[ 0 ].index = 0;
  pState->depth = 1;
  pState->pPrivate = pPrivate;

  return pState;
}

int32 MFSIterNext( MFSITERSTATE *pState )
{
  while ( TRUE )
  {
    MFSITERELEMENT *pElement = &(pState->stack[ pState->depth - 1 ]);
    MFSDIR *pDir = pElement->pDirNode->pDir;
    MFSNODE *pNode;

    if ( pElement->index == pDir->nEntries )
    {
      if ( pState->depth == 1 )
      {
        /* Iteration exhausted. */
        return FALSE;
      }
      else
      {
        /* Pop the current directory. */
        pState->depth--;
      }
    }
    else
    {
      pElement->index++;

      /* Look at the current entry. */
      pNode = pDir->entries[ pElement->index - 1 ];

      if ( pNode != NULL ) /* Check for empty entries. */
      {
        if ( pNode->type == MFS_File )
        {
          return TRUE;
        }
        else
        {
          /* Push directory. */
          pState->stack[ pState->depth ].pDirNode = pNode;
          pState->stack[ pState->depth ].index = 0;
          pState->depth++;
        }
      }
    }
  }

  /* Keep compiler happy, but we will return before this. */
  return FALSE;
}

void MFSIterEnd( MFSITERSTATE *pState )
{
  MemFree( (void*) pState );
}

uint32 MFSIterNameLength( MFSITERSTATE *pState, uint32 fLeadingSlash )
{
  MFSNODE *pNode = MFSIterNode( pState );

  uint32 len = fLeadingSlash ? 1 : 0; /* Initial slash. */
  uint32 i = 0;

  /* Add up directory names, but skip the root. */
  for ( i = 1; i < pState->depth; i++ )
  {
    len += strlen_uint32( pState->stack[ i ].pDirNode->pszName );
    len++; /* Account for a separator. */
  }

  /* Add on the leaf. */
  len += strlen_uint32( pNode->pszName );

  /* And a NULL-terminator */
  len++;

  return len;
}

int32 MFSIterName
  ( MFSITERSTATE *pState, char *pszFilenameBuf, uint32 bufLen, uint32 fLeadingSlash )
{
  MFSNODE *pNode = MFSIterNode( pState );
  uint32 i;

  if ( MFSIterNameLength( pState, fLeadingSlash ) > bufLen )
    return FALSE;

  pszFilenameBuf[ 0 ] = '\0';

  if ( fLeadingSlash )
    strcat( pszFilenameBuf, "/" ); /* Initial slash. */

  /* Concat directory names, but skip the root. */
  for ( i = 1; i < pState->depth; i++ )
  {
    strcat( pszFilenameBuf, pState->stack[ i ].pDirNode->pszName );
    strcat( pszFilenameBuf, "/" );
  }

  /* Add on the leaf. */
  strcat( pszFilenameBuf, pNode->pszName );

  return TRUE;
}

MFSNODE *MFSIterNode( MFSITERSTATE *pState )
{
  MFSITERELEMENT *pElement = &(pState->stack[ pState->depth - 1 ]);
  MFSDIR *pDir = pElement->pDirNode->pDir;
  MFSNODE *pNode = pDir->entries[ pElement->index - 1 ];

  return pNode;
}

void MFSMemUsage( MFSNODE *pNode, uint32 *pROMSize, uint32 *pRAMSize )
{
  if ( pNode != NULL )
  {
    if ( pNode->type == MFS_File )
    {
      MFSFILE *pFile = pNode->pFile;

      if ( pFile->pData != NULL )
      {
        if ( pFile->fDynamicBuffer )
        {
          /* When the uncompressed buffer is RAM-allocated, its true RAM consumption
             is the current capacity of the buffer, which may be greater than
             the file's actual size. */
          *pRAMSize += pFile->cbCapacity;
        }
        else
        {
          /* If the data buffer is in ROM, it will be sized exactly according
             to the file. */
          *pROMSize += pFile->cbSize;
        }
      }

      if ( pFile->pCompressedData != NULL )
      {
        if ( pFile->fDynamicCompressedBuffer )
        {
          *pRAMSize += pFile->cbCompressedSize;
        }
        else
        {
          *pROMSize += pFile->cbCompressedSize;
        }
      }
    }
    else
    {
      /* We are visiting a directory. Only files are deemed to contribute
         to the memory usage, so this is just a recursion case. Of course,
         directories do have data structures that consume memory, but
         it is negligible compared to file data buffers, and we are
         not required to produce a byte-accurate result. */
      MFSDIR *pDir = pNode->pDir;
      uint32 i;
      for ( i = 0; i < pDir->nEntries; i++ )
      {
        MFSMemUsage( pDir->entries[ i ], pROMSize, pRAMSize );
      }
    }
  }
}

