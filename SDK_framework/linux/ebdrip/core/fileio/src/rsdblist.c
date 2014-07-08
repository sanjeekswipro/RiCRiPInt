/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:rsdblist.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1998-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * blist data storage abstraction for Reusable Stream Decode filter
 */

#include "core.h"
#include "swerrors.h"
#include "swdevice.h"
#include "swcopyf.h"
#include "swctype.h"
#include "mm.h"
#include "mps.h"
#include "gcscan.h"
#include "hqmemcpy.h"
#include "objects.h"
#include "devices.h"

#include "fileio.h"

#include "rsdstore.h"
#include "rsdblist.h"

/* -------------------------------------------------------------------------- */
struct RSD_BLOCKLIST {
  FILELIST *source ;   /* Source of data on disk (original or copy). */
  Hq32x2 offset ;      /* Rewind position of underlying source. */
  Hq32x2 blockoffset ; /* Pos of next block to be read from original source. */
  Bool seekable ;      /* Original source seekable. */
  Bool encoded ;       /* Original source encoded. */
  int accesshint;      /* rand/seqn affects size for data in block. */
  int32 blocksize ;    /* Size of block. */
  Bool purgetodisk ;   /* Forces a block to to be written to disk. Used when
                          have seqn access and a small difference between comp
                          and uncomp data size. */

  struct RSD_FILE *headfile ;     /* List of files where the blocks are
                                     written to. */
  struct RSD_FILE *tailfile ;
  int32 format ;                  /* Compression format for blocks written
                                     to disk. */

  int32 length ;                  /* Total number of bytes. */
  int32 bytesavailable ;          /* Bytes avaliable in the blocks. */

  struct RSD_BLOCK *lockblock ;   /* Block to currently being read from
                                     (block never purged). */
  struct RSD_BLOCK *readblock ;   /* Block to read from next. */
  struct RSD_BLOCK *fillblock ;   /* Block to write data into next. */
  struct RSD_BLOCK *purgblock ;   /* Block to have data buffer freed
                                     or recycled. */

  struct RSD_BLOCK *headblock ;
  struct RSD_BLOCK *tailblock ;
} ;

typedef struct RSD_BLOCK {
  FILELIST *file ;      /* Block file where the data is written to. */
  Hq32x2 offset ;       /* If non-zero gives the disk offset for the block. */

  uint8 *data ;         /* Block of memory where data stored. */
  int32 sbytes ;        /* Stored bytes in this block. */
  int32 fbytes ;        /* Bytes on File in this block. */
  int32 tbytes ;        /* Total bytes in this block. */
#ifdef RSD_STORE_STATS
  int32 purgecount ;    /* Number of times this block has been purged. */
#endif

  struct RSD_BLOCK *prev ;
  struct RSD_BLOCK *next ;
} RSD_BLOCK ;

typedef struct RSD_FILE {
  FILELIST *file ;                /* File data in the blocks is written to. */
  int32 fid ;                     /* File ID for filename generation. */
  int32 size ;                    /* File size is restricted. */
  struct RSD_FILE *next ;
} RSD_FILE ;

/* -------------------------------------------------------------------------- */
/* Device and name the RSD Store uses to write the data to disk if necs.
 */
static DEVICELIST *rsd_tmpdevice = NULL ;

/* Size of a block (for data) in the RSD Store.
 */
enum {
  RSD_SEQN_DIM = 16384 ,
  RSD_RAND_DIM = 1024
} ;

/* At the moment the data is written uncompressed to disk (unless it
 * is a compressed block that is being purged).
 */
enum {
  RSD_COMPRESS_NONE  = -1 ,
  RSD_COMPRESS_LZW   =  0 ,
  RSD_COMPRESS_CCITT =  1
} ;


mm_pool_t mm_pool_rsd;

/* -------------------------------------------------------------------------- */

#ifdef  RSD_STORE_STATS
static Bool debug_rsdmem = FALSE ;
#endif

void init_C_globals_rsdblist(void)
{
  rsd_tmpdevice = NULL ;
  mm_pool_rsd = NULL ;
#ifdef  RSD_STORE_STATS
  debug_rsdmem = FALSE ;
#endif
}

/* -------------------------------------------------------------------------- */
static mps_res_t rsd_scanblockfile( mps_ss_t scan_state, RSD_FILE *blockfile ) ;
static Bool rsd_fillblocks( RSD_BLOCKLIST *blocklist , Bool single ) ;
static Bool rsd_fillblock( RSD_BLOCKLIST *blocklist , FILELIST *flptr ) ;
static Bool rsd_skipblock( RSD_BLOCKLIST *blocklist ) ;
static Bool rsd_blocknew( RSD_BLOCKLIST *blocklist ) ;
static Bool rsd_allocdata( RSD_BLOCKLIST *blocklist ) ;
static Bool rsd_allocblockfile( RSD_BLOCKLIST *blocklist ,
                                RSD_FILE **rblockfile ) ;
static void  rsd_freeblockfile( RSD_FILE *blockfile ) ;
static Bool rsd_getblockfile( RSD_BLOCKLIST *blocklist , FILELIST **rflptr ) ;
static Bool rsd_blocktodisk( RSD_BLOCKLIST *blocklist , RSD_BLOCK *block ) ;
static Bool rsd_needrewind( RSD_BLOCKLIST *blocklist ) ;
static Bool rsd_rewindfile( FILELIST *flptr , Hq32x2 *offset ) ;
static Bool rsd_savefilepos( FILELIST *flptr , Hq32x2 *offset ) ;
static Bool rsd_restorefilepos( FILELIST *flptr , Hq32x2 *filepos ) ;

/* -------------------------------------------------------------------------- */
#define ASSERT_FILTER( _filter ) MACRO_START    \
  HQASSERT( _filter &&                          \
            isIOpenFile( _filter ) ,            \
            "RSD filter NULL or closed" ) ;     \
MACRO_END

#define ASSERT_BOOLEAN( _bool ) \
  HQASSERT(BOOL_IS_VALID(_bool), "Expected boolean value") ;

/* -------------------------------------------------------------------------- */
RSD_BLOCKLIST *rsd_blistnew( FILELIST *source ,
                             Bool origsource ,
                             Bool seekable ,
                             Bool encoded ,
                             int accesshint )
{
  RSD_BLOCKLIST *blocklist ;

  ASSERT_BOOLEAN( origsource ) ;
  ASSERT_BOOLEAN( seekable ) ;
  ASSERT_BOOLEAN( encoded ) ;
  HQASSERT( accesshint == RSD_ACCESS_SEQN || accesshint == RSD_ACCESS_RAND ,
            "rsd_blistnew: accesshint not seqn/rand" ) ;

  blocklist =
    ( RSD_BLOCKLIST * ) mm_alloc( mm_pool_temp ,
                                  sizeof( RSD_BLOCKLIST ) ,
                                  MM_ALLOC_CLASS_RSDSTORE ) ;
  if ( blocklist == NULL ) {
    (void)error_handler( VMERROR );
    return NULL ;
  }

  blocklist->source         = source ;
  if ( origsource && seekable ) {
    if ( !rsd_savefilepos(source, &blocklist->offset) ) {
      mm_free( mm_pool_temp, (mm_addr_t) blocklist, sizeof(RSD_BLOCKLIST) );
      (void)error_handler( IOERROR );
      return NULL;
    }
    blocklist->blockoffset  = blocklist->offset ;
  } else {
    Hq32x2FromInt32(&blocklist->offset, 0);
    Hq32x2FromInt32(&blocklist->blockoffset, 0);
  }
  blocklist->seekable       = seekable ;
  blocklist->encoded        = encoded ;
  blocklist->accesshint     = RSD_ACCESS_SEQN ;
  blocklist->blocksize      =
    ( accesshint == RSD_ACCESS_SEQN ? RSD_SEQN_DIM : RSD_RAND_DIM ) ;
  blocklist->purgetodisk    = FALSE ;

  blocklist->headfile       = NULL ;
  blocklist->tailfile       = NULL ;
  blocklist->format         = RSD_COMPRESS_NONE ;

  blocklist->length         = RSD_LENGTH_UNKNOWN ;
  blocklist->bytesavailable = 0 ;

  blocklist->lockblock      = NULL ;
  blocklist->readblock      = NULL ;
  blocklist->fillblock      = NULL ;
  blocklist->purgblock      = NULL ;

  blocklist->headblock      = NULL ;
  blocklist->tailblock      = NULL ;

  return blocklist ;
}

/* -------------------------------------------------------------------------- */
void rsd_blistfree( RSD_BLOCKLIST **pblocklist )
{
  RSD_BLOCKLIST *blocklist ;
  RSD_BLOCK *block ;

  HQASSERT( pblocklist , "rsd_blistfree: pblocklist NULL" ) ;

  blocklist = *pblocklist ;
  HQASSERT( blocklist , "rsd_blistfree: blocklist NULL" ) ;

  block = blocklist->tailblock ;
  while ( block ) {
    RSD_BLOCK *tblock ;
    tblock = block ;
    block = block->prev ;
    if ( tblock->data )
      mm_free( mm_pool_rsd ,
               ( mm_addr_t ) tblock->data ,
               tblock->tbytes ) ;
    mm_free( mm_pool_temp ,
             ( mm_addr_t ) tblock ,
             sizeof( RSD_BLOCK )) ;
  }
  if ( blocklist->headfile ) {
    RSD_FILE *blockfile ;
    blockfile = blocklist->headfile ;
    do {
      RSD_FILE *tblockfile ;
      tblockfile = blockfile ;
      blockfile = blockfile->next ;
      rsd_freeblockfile( tblockfile ) ;
    } while ( blockfile ) ;
  }
  mm_free( mm_pool_temp ,
           ( mm_addr_t ) blocklist ,
           sizeof( RSD_BLOCKLIST )) ;
  *pblocklist = NULL ;
}


/* rsd_blistscan - scan a block list */
mps_res_t rsd_blistscan( mps_ss_t scan_state, RSD_BLOCKLIST *blocklist )
{
  RSD_FILE *blockfile;
  mps_res_t res = MPS_RES_OK;

  blockfile = blocklist->headfile;
  while ( blockfile != NULL && res == MPS_RES_OK ) {
    res = rsd_scanblockfile( scan_state, blockfile );
    blockfile = blockfile->next;
  }
  return res;
}


/* -------------------------------------------------------------------------- */
Bool rsd_blistread( RSD_BLOCKLIST *blocklist ,
                    Bool saverestorefilepos,
                    uint8 **rbuf , int32 *rbytes )
{
  RSD_BLOCK *readblock ;
  FILELIST *flptr ;
  Hq32x2 sfilepos, blockstart;

  Hq32x2FromInt32(&sfilepos, -1);
  Hq32x2FromInt32(&blockstart, -1);

#ifdef  RSD_STORE_STATS
  if ( debug_rsdmem && IS_INTERPRETER() ) {
    monitorf((uint8*)"RSD memory stats: no pool: %lu  temp: %lu  rsd: %lu\n",
             (unsigned long)mm_no_pool_size(),
             (unsigned long)mm_pool_free_size( mm_pool_temp ),
             (unsigned long)mm_pool_free_size( mm_pool_rsd ));
  }
#endif

  readblock = blocklist->readblock ;
  HQASSERT( readblock == NULL ||
            blocklist->lockblock != readblock ,
            "rsd_blistread: Current and required block are the same!" ) ;

  /* Null this so the last read block can be recycled if run out of mem. */
  blocklist->lockblock = NULL ;

  flptr = blocklist->source ;

  if ( readblock == NULL ||
       readblock->data == NULL ) {

    if ( readblock && readblock->file ) {
      /* Can read the data from a blockfile. */
      blocklist->fillblock = blocklist->readblock ;
    }
    else {
      /* Need to read the data from source. */
      HQASSERT( flptr , "rsd_blistread: blocklist->source NULL" ) ;

      if ( readblock == NULL ) {
        /* First time reading the data. */
        if ( saverestorefilepos ) {
          if ( !rsd_savefilepos( flptr, &sfilepos) )
            return FALSE ;
          if ( !rsd_restorefilepos( flptr , &blocklist->blockoffset ))
            return FALSE ;
        }
        if ( (!blocklist->encoded) && blocklist->seekable ) {
          if ( blocklist->headblock == NULL ) {
            if ( !rsd_savefilepos( flptr , &blockstart) )
              return FALSE ;
          } else {
            HQASSERT( blocklist->tailblock != NULL , "tailblock NULL" ) ;
            Hq32x2AddInt32(&blockstart, &blocklist->tailblock->offset,
                           blocklist->tailblock->sbytes);
          }
        }
      }
      else {
        /* Already read the data once before. */
        HQASSERT( blocklist->seekable , "rsd_blistread: not seekable" ) ;
        if ( saverestorefilepos ) {
          if ( !rsd_savefilepos( flptr, &sfilepos ) )
            return FALSE ;
        }
        if ( blocklist->encoded ) {
          if ( rsd_needrewind( blocklist )) {
            /* fillblock occurrs after readblock, need to rewind. */
            if ( !rsd_rewindfile( flptr , &blocklist->offset ))
              return FALSE ;
            blocklist->fillblock = blocklist->headblock ;
          }
          else {
            /* Still need to re-position source. */
            if ( !rsd_restorefilepos( flptr , &blocklist->blockoffset ))
              return FALSE ;
          }
        }
        else {
          /* Can seek source directly to the readblock. */
          if ( !rsd_restorefilepos( flptr , &readblock->offset ))
            return FALSE ;
          blocklist->fillblock = readblock ;
        }
      }
      ClearIEofFlag( flptr ) ;
    }

    /* Read from source or blockfile. */
    if ( ! rsd_fillblocks( blocklist , readblock == NULL ))
      return FALSE ;

    if ( readblock == NULL )
      readblock = blocklist->tailblock ;
  }

  if ( Hq32x2CompareInt32(&blockstart, -1) != 0 && readblock ) {
    HQASSERT(!blocklist->encoded && blocklist->seekable ,
              "rsd_blistread: Must be seekable and not encoded" ) ;
    readblock->offset = blockstart ;
  }
  if ( Hq32x2CompareInt32(&sfilepos, -1) != 0 ) {
    HQASSERT( saverestorefilepos ,
              "rsd_blistread: sfilepos set, but not saverestorefilepos" ) ;
    if ( ! isIEof( flptr )) {
      if ( !rsd_savefilepos( flptr , &blocklist->blockoffset ) )
        return FALSE ;
    }
    if ( !rsd_restorefilepos( flptr , &sfilepos ))
      return FALSE ;
  }

  if ( readblock != NULL ) {
    /* Only return data if we actually read something */
    blocklist->lockblock = readblock ;
    blocklist->readblock = readblock->next ;

    *rbuf = readblock->data ;
    *rbytes = readblock->sbytes ;

    if ( blocklist->readblock == NULL &&
         blocklist->length != RSD_LENGTH_UNKNOWN )
      *rbytes = -(*rbytes) ; /* EOD */

  } else { /* Data source was empty! */
    *rbytes = 0;
  }

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
Bool rsd_blistseek( RSD_BLOCKLIST *blocklist , int32 offset ,
                    int32 *roffset )
{
  RSD_BLOCK *block ;
  int32 toffset ;

  HQASSERT( blocklist , "rsd_blistseek: No decoded data blocks" ) ;
  HQASSERT( blocklist && blocklist->headblock , "block NULL" ) ;

  if (!(blocklist && blocklist->headblock))
    return error_handler( IOERROR ) ;

  HQASSERT( blocklist->length != RSD_LENGTH_UNKNOWN ,
            "rsd_blistseek: Must know length by now" ) ;
  HQASSERT( roffset , "rsd_blistseek: roffset NULL" ) ;

  block = blocklist->headblock ;

  toffset = 0 ;

  do {
    HQASSERT( block->sbytes > 0 , "rsd_blistseek: sbytes <= 0" ) ;
    toffset += block->sbytes ;
    if ( offset < toffset ) {
      toffset -= block->sbytes ;
      break ;
    }
    block = block->next ;
  } while ( block ) ;

  HQASSERT( block , "duff offset given to rsd_blistseek" ) ;

  if (!block || (toffset > offset))
    return error_handler( IOERROR );

  if ( blocklist->accesshint == RSD_ACCESS_SEQN ) {

    if ( blocklist->readblock == NULL )
      /* Currently at the end, for seqn access to continue must be
         seeking to the first block. */
      rsd_blistreset( blocklist ) ;

    if ( block != blocklist->readblock ) {
      /* Want to read from a different block than just the next one,
               therefore no longer a sequential access pattern, switch over
               to random access caching strategy. Note that if compressed
               data is stored as well, it remains seqn access. */
      blocklist->accesshint = RSD_ACCESS_RAND ;
      blocklist->blocksize  = RSD_RAND_DIM ;
    }
  }

  blocklist->readblock = block ;

  *roffset = toffset ;
  return TRUE;
}

/* -------------------------------------------------------------------------- */
int32 rsd_blistlength( RSD_BLOCKLIST *blocklist )
{
  HQASSERT( blocklist , "rsd_blistlength: blocklist NULL" ) ;
  return blocklist->length ;
}

/* -------------------------------------------------------------------------- */
void rsd_blistdolength( RSD_BLOCKLIST *blocklist )
{
  RSD_BLOCK *block ;
  int32 length ;

  HQASSERT( blocklist , "rsd_blistdolength: blocklist NULL" ) ;
  HQASSERT( blocklist->length == RSD_LENGTH_UNKNOWN ,
            "rsd_blistdolength: length already calculated" ) ;

  block = blocklist->headblock ;
  length = 0 ;
  while ( block ) {
    HQASSERT( block->sbytes != 0 , "rsd_blistdolength: sbytes == 0" ) ;
    length += block->sbytes ;
    block = block->next ;
  }
  HQASSERT( length >= 0 , "rsd_blistdolength: length < 0" ) ;
  blocklist->length = length ;
}


/* -------------------------------------------------------------------------- */
void rsd_blistsetsource( RSD_BLOCKLIST *blocklist , FILELIST *flptr )
{
  HQASSERT( blocklist , "rsd_blistsetsource: blocklist NULL" ) ;
  blocklist->source = flptr ;
}

/* -------------------------------------------------------------------------- */
FILELIST *rsd_blistgetsource( RSD_BLOCKLIST *blocklist )
{
  HQASSERT( blocklist , "rsd_blistgetsource: blocklist NULL" ) ;
  return blocklist->source ;
}

/* -------------------------------------------------------------------------- */
void rsd_blistclearlock( RSD_BLOCKLIST *blocklist )
{
  /* Clear lock: Not using the data in the block last read from.
     Nulling lockblock allows the an extra block of memory to be
     recycled if necessary. */
  HQASSERT( blocklist , "rsd_blistclearlock: blocklist NULL" ) ;
  blocklist->lockblock = NULL ;
}

/* -------------------------------------------------------------------------- */
void rsd_blistreset( RSD_BLOCKLIST *blocklist )
{
  HQASSERT( blocklist , "rsd_blistreset: blocklist NULL" ) ;
  blocklist->readblock = blocklist->headblock ;
}

/* -------------------------------------------------------------------------- */
void  rsd_blistforcetodisk( RSD_BLOCKLIST *blocklist )
{
  HQASSERT( blocklist , "rsd_blistforcetodisk: blocklist NULL" ) ;
  blocklist->purgetodisk = TRUE ;
}

/* -------------------------------------------------------------------------- */
Bool rsd_blistcomplete( RSD_BLOCKLIST *blocklist )
{
  RSD_BLOCK *block ;

  HQASSERT( blocklist , "rsd_blistcomplete: blocklist NULL" ) ;

  if ( blocklist->length == RSD_LENGTH_UNKNOWN )
    /* Obviously not got to the end of the data yet. */
    return FALSE ;

  block = blocklist->headblock ;
  HQASSERT( block , "rsd_blistcomplete: block NULL" ) ;

  do {
    if ( block->data == NULL &&
         block->file == NULL )
      /* Data not in memory and not written to a blockfile either. */
      return FALSE ;
    block = block->next ;
  } while ( block ) ;

  /* Have all the data, in memory or on blockfile. */
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
Bool rsd_blistfindreclaim( corecontext_t *context,
                           RSD_BLOCKLIST *blocklist , int32 tbytes ,
                           int accesstype, int action, Bool allow_open,
                           Bool *gotone, size_t *purge_size )
{
  Bool fromfillblock = ( action & RSD_FROMFILLBLOCK ) != 0 ;
  Bool exactbytes = ( action & RSD_EXACTBYTES ) != 0 ;
  Bool allowdiskwrite = ( action & RSD_ALLOWDISKWRITE ) != 0;
  int32 saved_error_code = 0;

  HQASSERT( blocklist , "rsd_blistfindreclaim: blocklist NULL" ) ;
  HQASSERT( tbytes > 0 || tbytes == -1 ,
            "rsd_blistfindreclaim: tbytes <= 0 && tbytes != -1" ) ;
  HQASSERT( accesstype == RSD_ACCESS_SEQN || accesstype == RSD_ACCESS_RAND ,
            "rsd_blistfindreclaim: accesstype not RSD_ACCESS_SEQN/RAND" ) ;

  *gotone = FALSE ;

  if ( blocklist->bytesavailable > 0 && accesstype == blocklist->accesshint ) {
    RSD_BLOCK *block ;
    block = ( blocklist->purgblock ?
              blocklist->purgblock : /* start from where left off. */
              ( fromfillblock ? blocklist->fillblock : blocklist->headblock )) ;
    for ( ; block && ( fromfillblock || block != blocklist->fillblock )
            ; block = block->next ) {
      if ( block != blocklist->lockblock && block->data &&
           ( tbytes == -1 || /* Don't care what size it is. */
             ( block->tbytes == tbytes ||
               ( !exactbytes && block->tbytes > tbytes )))) {
        /* Work out if the data requires writing to disk. */
        Bool writetodisk = ( block->file == NULL &&
                             ( accesstype == RSD_ACCESS_RAND ||
                               blocklist->source == NULL ||
                               !blocklist->seekable ||
                               blocklist->purgetodisk )) ;
        if ( !writetodisk
             || (allowdiskwrite
                 /* Either have a file, or allowed to open one. */
                 && (blocklist->tailfile != NULL || allow_open)) ) {
          if ( writetodisk && (action & RSD_NO_WRITE) == 0 )
            if ( !rsd_blocktodisk( blocklist, block )) {
              /* No fatal errors, just can't recover space. */
              error_save_context(context->error, &saved_error_code);
              allow_open = FALSE; /* try without opening files */
              continue;
            }
          blocklist->purgblock = block ;
#ifdef RSD_STORE_STATS
          if ( (action & RSD_NO_WRITE) == 0 )
            /* RSD_NO_WRITE indicates just a check, not a purge */
            block->purgecount += 1;
#endif
          *gotone = TRUE; *purge_size = (size_t)block->tbytes;
          if ( saved_error_code != 0 ) {
            error_restore_context(context->error, saved_error_code);
            error_clear_context(context->error);
          }
          return TRUE ;
        }
      }
    }
  }
  if ( saved_error_code != 0 ) {
    error_restore_context(context->error, saved_error_code);
    return FALSE;
  } else
    return TRUE;
}


#ifdef NOT_YET_USED
static Bool rsd_purge_block_to_disk(RSD_BLOCKLIST *blocklist, int accesstype)
{
  RSD_BLOCK *block = blocklist->purgblock;

  if ( block->file == NULL
       && ( accesstype == RSD_ACCESS_RAND || blocklist->source == NULL
            || !blocklist->seekable || blocklist->purgetodisk ))
    /* If it ended up as purgblock, disk write must have been allowed. */
    if ( !rsd_blocktodisk( blocklist, block ))
      return FALSE;
  return TRUE;
}
#endif

/* -------------------------------------------------------------------------- */
void rsd_blistrecycle( RSD_BLOCKLIST *blocklist ,
                       uint8 **rdata , int32 *rbytes )
{
  RSD_BLOCK *block ;

  HQASSERT( blocklist , "rsd_blistrecycle: blocklist NULL" ) ;
  block = blocklist->purgblock ;
  HQASSERT( block , "rsd_blistrecycle: purgblock NULL" ) ;

  *rdata = block->data ;
  *rbytes = block->tbytes ;
  block->data = NULL ;
  blocklist->bytesavailable -= block->tbytes ;
  block->tbytes = 0 ;
}

/* -------------------------------------------------------------------------- */
void rsd_blistpurge( RSD_BLOCKLIST *blocklist )
{
  RSD_BLOCK *block ;

  HQASSERT( blocklist , "rsd_blistpurge blocklist NULL" ) ;

  block = blocklist->purgblock ;
  HQASSERT( block , "rsd_blistpurge: purgblock NULL" ) ;

  blocklist->purgblock = blocklist->purgblock->next ;

  mm_free( mm_pool_rsd , ( mm_addr_t ) block->data , block->tbytes ) ;
  block->data = NULL ;

  blocklist->bytesavailable -= block->tbytes ;
  block->tbytes = 0 ;
}

/* -------------------------------------------------------------------------- */
/* 1. Fill blocks until hit EOF (if reading from a file). */
/* 2. Fill blocks until filled readblock. */
/* 3. Fill a single block, the next block only. */
static Bool rsd_fillblocks( RSD_BLOCKLIST *blocklist , Bool single )
{
  FILELIST *srcflptr ;
  RSD_BLOCK *readblock , *fillblock , *fillblockprev ;

  HQASSERT( blocklist , "rsd_fillblocks: blocklist NULL" ) ;
  HQASSERT(BOOL_IS_VALID(single),
           "rsd_fillblocks: single not boolean" ) ;

  srcflptr = blocklist->source ;

  readblock = blocklist->readblock ;
  fillblock = blocklist->fillblock ;

  do {

    HQASSERT( fillblock == blocklist->fillblock ,
              "rsd_fillblocks: fillblock inconsistent" ) ;

    if ( fillblock == NULL ) {
      if ( ! rsd_blocknew( blocklist ))
        return FALSE ;
      fillblock = blocklist->tailblock ;
    }

    if ( fillblock->data == NULL ) {
      FILELIST *flptr ;

      if ( ! rsd_allocdata( blocklist ))
        return FALSE ;

      flptr = srcflptr ;
      if ( fillblock->file ) {
        /* Have a blockfile so use in preference to the source
           (if still present). */
        if ( ! rsd_getblockfile( blocklist , & flptr ))
          return FALSE ;
      }

      ASSERT_FILTER( flptr ) ;
      HQASSERT( ! isIEof( flptr ) , "rsd_fillblocks: flptr at EOF" ) ;
      /* Alloc buffer and fill with data (or to EOD). */
      if ( ! rsd_fillblock( blocklist , flptr ))
        return FALSE ;
    }
    else if ( srcflptr ) {
      ASSERT_FILTER( srcflptr ) ;
      HQASSERT( ! isIEof( srcflptr ) , "rsd_fillblocks: srcflptr at EOF" ) ;
      /* Already have the data, advance the source. */
      if ( ! rsd_skipblock( blocklist ))
        return FALSE ;
    }

    fillblockprev = fillblock ;
    fillblock = fillblock->next ;

    /* Stop filling blocks when hit EOF, or the block that has just
       been filled is the one that is to be read from, or it is just
       one block required. */
  } while ( ( srcflptr == NULL || ! isIEof( srcflptr )) &&
            ( readblock == NULL || fillblockprev != readblock ) &&
            ! single ) ;

  if ( srcflptr && isIEof( srcflptr ) &&
       blocklist->length == RSD_LENGTH_UNKNOWN ) {
    if ( !blocklist->seekable )
      /* Not seekable so cannot continue use the source again. */
      blocklist->source = NULL ;

    if ( fillblockprev->sbytes == 0 ) {
      /* Hit EOD on first read last block - remove it from list */
      mm_free(mm_pool_rsd, fillblockprev->data, fillblockprev->tbytes);
      if ( fillblockprev->prev != NULL ) {
        fillblockprev->prev->next = blocklist->fillblock;
      } else {
        blocklist->headblock = NULL;
      }
      blocklist->lockblock = blocklist->tailblock = fillblockprev->prev;
      mm_free(mm_pool_temp, fillblockprev, sizeof(RSD_BLOCK));
    }

    /* Should have cached all the data now, so work out length. */
    rsd_blistdolength( blocklist ) ;
  }

  /* Start from scratch when next purge. */
  blocklist->purgblock = NULL ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool rsd_fillblock( RSD_BLOCKLIST *blocklist , FILELIST *flptr )
{
  RSD_BLOCK *fillblock ;
  int32 rbytes , bytes ;
#if defined( ASSERT_BUILD )
  int32 oldsbytes ;
#endif
  uint8 *buf ;

  HQASSERT( blocklist , "rsd_fillblock: blocklist NULL" ) ;

  ASSERT_FILTER( flptr ) ;

  fillblock = blocklist->fillblock ;
  HQASSERT( fillblock , "rsd_fillblock: fillblock NULL" ) ;
  HQASSERT( fillblock->data != NULL ,
            "rsd_fillblock: fillblock->data NULL" ) ;
  HQASSERT( fillblock->tbytes > 0 , "rsd_fillblock: tbytes <= 0" ) ;

#if defined( ASSERT_BUILD )
  oldsbytes = fillblock->sbytes ;
#endif

  /* If block was purged (sbytes > 0) then data should be exactly the
     right size, or if recycled, it may be larger. */
  HQASSERT( fillblock->tbytes >= fillblock->sbytes || fillblock->sbytes == 0 ,
            "rsd_fillblock: Allocated data is the wrong size" ) ;

  rbytes = fillblock->fbytes != 0 ? fillblock->fbytes
           : (fillblock->sbytes != 0 ? fillblock->sbytes : fillblock->tbytes);
  HQASSERT( rbytes > 0 , "rsd_fillblock: No free bytes remaining" ) ;

  fillblock->sbytes = 0 ;

  while ( rbytes > 0 && GetFileBuff( flptr , rbytes , & buf , & bytes )) {
    HqMemCpy( fillblock->data + fillblock->sbytes , buf , bytes ) ;
    fillblock->sbytes += bytes ;
    rbytes -= bytes ;
  }

  if ( !isIEof(flptr) &&
       (fillblock->sbytes == 0 ||
        (fillblock->fbytes != 0 && fillblock->fbytes != fillblock->sbytes)) )
    return error_handler( IOERROR ) ;

  HQASSERT( isIEof(flptr) ||
            ( fillblock->sbytes > 0 &&
              ( fillblock->sbytes == oldsbytes ||
                oldsbytes == 0 )) ,
            "rsd_fillblock: Unexpected sbytes value" ) ;

  blocklist->fillblock = fillblock->next ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool rsd_skipblock( RSD_BLOCKLIST *blocklist )
{
  FILELIST *flptr ;
  RSD_BLOCK *fillblock ;
  int32 bytes ;

  HQASSERT( blocklist , "rsd_skipblock: blocklist NULL" ) ;

  flptr = blocklist->source ;
  ASSERT_FILTER( flptr ) ;

  fillblock = blocklist->fillblock ;
  HQASSERT( fillblock , "rsd_skipblock: fillblock NULL" ) ;
  HQASSERT( fillblock->data , "rsd_skipblock: fillblock->data NULL" ) ;

  /* Already have the data in this block, FastForward the flptr on
     to the next block. */
  bytes = fillblock->sbytes ;
  HQASSERT( bytes > 0 , "rsd_skipblocks: sbytes <= 0" ) ;
  do {
    if ( theICount( flptr ) <= 0 ) {
      if ( GetNextBuf( flptr ) == EOF ) {
        HQFAIL( "rsd_skipblock: bytes available mis-match" ) ;
        return FALSE ;
      }
      bytes -= 1 ; /* Just thrown one away. */
    }
    if ( bytes > theICount( flptr )) {
      /* Skip this buffer. */
      bytes -= theICount( flptr ) ;
      theICount( flptr ) = 0 ;
    }
    else {
      /* Next block starts in this buffer. */
      theICount( flptr ) -= bytes ;
      theIPtr( flptr ) += bytes ;
      bytes = 0 ;
    }
  } while ( bytes > 0 ) ;

  blocklist->fillblock = fillblock->next ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool rsd_blocknew( RSD_BLOCKLIST *blocklist )
{
  RSD_BLOCK *block ;

  HQASSERT( blocklist , "rsd_blocknew: blocklist NULL" ) ;
  HQASSERT( blocklist->fillblock == NULL &&
            ( blocklist->tailblock == NULL ||
              blocklist->tailblock->next == NULL ) ,
            "rsd_blocknew: Unexpected tailblock/fillblock state" ) ;

  block =
    ( RSD_BLOCK * ) mm_alloc( mm_pool_temp ,
                              sizeof( RSD_BLOCK ) ,
                              MM_ALLOC_CLASS_RSDSTORE ) ;
  if ( block == NULL )
    return error_handler( VMERROR ) ;

  block->file = NULL ;
  Hq32x2FromInt32(&block->offset, 0);

  block->data = NULL ;
  block->sbytes = 0 ;
  block->fbytes = 0 ;
  block->tbytes = 0 ;
#ifdef RSD_STORE_STATS
  block->purgecount = 0 ;
#endif

  if ( blocklist->tailblock )
    blocklist->tailblock->next = block ;

  block->prev = blocklist->tailblock ;
  block->next = NULL ;
  blocklist->tailblock = block ;

  if ( blocklist->headblock == NULL )
    blocklist->headblock = block ;

  blocklist->fillblock = block ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool rsd_allocdata( RSD_BLOCKLIST *blocklist )
{
  RSD_BLOCK *fillblock ;
  int32 bytes ;

  HQASSERT( blocklist , "rsd_allocdata: blocklist NULL" ) ;
  HQASSERT( mm_pool_rsd != NULL , "mm_pool_rsd not setup properly" ) ;

  fillblock = blocklist->fillblock ;
  HQASSERT( fillblock , "rsd_allocdata: fillblock NULL" ) ;
  HQASSERT( fillblock->tbytes == 0 , "rsd_allocdata: tbytes != 0" ) ;
  HQASSERT( fillblock->data == NULL ,
            "rsd_allocdata: fillblock->data must be NULL" ) ;

  if ( fillblock->sbytes > 0 ) {
    bytes = fillblock->sbytes ;
  }
  else {
    bytes = blocklist->blocksize ;
  }
  HQASSERT( bytes > 0 , "rsd_allocdata: bytes <= 0" ) ;

  /* Stop this block from being stolen by underlying blocklist
     (for compressed data). */
  blocklist->lockblock = fillblock ;

  /* Try allocation with no purging or extending/reserve use, because
     it's cheapest to recycle another RSD block. */
  fillblock->data = mm_alloc_cost(mm_pool_rsd, bytes,
                                  mm_cost_none, MM_ALLOC_CLASS_RSDSTORE);

  if ( fillblock->data == NULL ) {
    int32 reqbytes = bytes ;
    if ( ! rsd_recycle( blocklist , reqbytes , blocklist->accesshint ,
                        & fillblock->data , & bytes ))
      return FALSE ;
    if ( fillblock->data == NULL ) {
      if ( ! rsd_recycle( NULL , reqbytes , RSD_ACCESS_SEQN ,
                          & fillblock->data , & bytes ))
        return FALSE ;
      if ( fillblock->data == NULL ) {
        if ( ! rsd_recycle( NULL , reqbytes , RSD_ACCESS_RAND ,
                            & fillblock->data , & bytes ))
          return FALSE ;
        if ( fillblock->data == NULL ) {
          /* Now allow the mm to extend or use reserve and retry */
          fillblock->data = mm_alloc(mm_pool_rsd, bytes,
                                     MM_ALLOC_CLASS_RSDSTORE);
          if ( fillblock->data == NULL )
            return error_handler( VMERROR ) ;
        }
      }
    }
  }

  fillblock->tbytes = bytes ;
  blocklist->bytesavailable += bytes ;

  return TRUE ;
}


/* -------------------------------------------------------------------------- */
static Bool rsd_allocblockfile( RSD_BLOCKLIST *blocklist ,
                                RSD_FILE **rblockfile )
{
  corecontext_t *context = get_core_context_interp();
  static int32 fid = 0 ;
  RSD_FILE *blockfile ;
  OBJECT    theo = OBJECT_NOTVM_NOTHING, fileo = OBJECT_NOTVM_NOTHING ;
  uint8     filename[ 32 ] ;
  Bool      disable = FALSE ;
  Bool      gallocmode;
  Bool      status;

  blockfile = mm_alloc( mm_pool_temp ,
                        sizeof( RSD_FILE ) ,
                        MM_ALLOC_CLASS_RSDSTORE ) ;
  if ( blockfile == NULL )
    return error_handler(VMERROR);

  blockfile->file = NULL ;
  blockfile->fid  = ++fid ;
  blockfile->size = 0 ;
  blockfile->next = NULL ;

  if ( rsd_tmpdevice == NULL ) {
    rsd_tmpdevice = find_device( (uint8 *) "tmp" ) ;
    if ( rsd_tmpdevice == NULL ) {
      rsd_freeblockfile( blockfile ) ;
      return error_handler( IOERROR ) ;
    }
  }

  /* Create file path string, eg "%tmp%RSD/0001.RSD". */
  swcopyf( filename , ( uint8 * )"%%tmp%%RSD/%04X.RSD" , blockfile->fid ) ;

  /* ensure file internal uses global VM (restore_ proof) */
  gallocmode = setglallocmode(context,  TRUE ) ;

  theTags( theo ) = OSTRING | UNLIMITED | LITERAL ;
  SETGLOBJECTTO(theo, gallocmode) ;
  theLen( theo ) = ( uint16 ) strlen( ( char * ) filename ) ;
  oString(theo) = filename ;

  if ( ( theIDeviceFlags( rsd_tmpdevice ) & DEVICEENABLED ) == 0 ) {
    /* Enable the device temporarily. */
    theIDeviceFlags( rsd_tmpdevice ) |= DEVICEENABLED ;
    disable = TRUE ;
  }

  status = file_open(& theo , SW_RDWR | SW_CREAT | SW_TRUNC ,
                     READ_FLAG | WRITE_FLAG , FALSE , 0 , & fileo);

  setglallocmode(context, gallocmode ) ;

  if (!status) {
    if ( disable )
      theIDeviceFlags( rsd_tmpdevice ) &= ~DEVICEENABLED ;
    rsd_freeblockfile( blockfile ) ;
    return error_handler( IOERROR ) ;
  }

  if ( disable )
    theIDeviceFlags( rsd_tmpdevice ) &= ~DEVICEENABLED ;

  blockfile->file = oFile(fileo) ;

  if ( blocklist->headfile == NULL )
    blocklist->headfile = blockfile ;

  if ( blocklist->tailfile )
    blocklist->tailfile->next = blockfile ;
  blocklist->tailfile = blockfile ;

  *rblockfile = blockfile ;
  return TRUE ;
}

/* -------------------------------------------------------------------------- */
/* Does not unlink the blockfile from the chain. */
static void rsd_freeblockfile( RSD_FILE *blockfile )
{
  HQASSERT( blockfile , "rsd_freeblockfile: blockfile NULL" ) ;

  if ( blockfile->file ) {
    FILELIST *flptr ;
    uint8 filename[ 32 ] ;
    flptr = blockfile->file ;
    HQASSERT( rsd_tmpdevice , "rsd_freeblockfile: rsd_tmpdevice NULL" ) ;
    if ( isIOpenFile( flptr ))
      ( void ) (*theIMyCloseFile( flptr ))( flptr, CLOSE_EXPLICIT ) ;
    swcopyf( filename , ( uint8 * )"RSD/%04X.RSD" , blockfile->fid ) ;
    ( void ) (*theIDeleteFile( rsd_tmpdevice ))( rsd_tmpdevice , filename ) ;
  }
  mm_free( mm_pool_temp , ( mm_addr_t ) blockfile , sizeof( RSD_FILE )) ;
}


/* rsd_scanblockfile - scan a block file */
static mps_res_t rsd_scanblockfile( mps_ss_t scan_state, RSD_FILE *blockfile )
{
  MPS_SCAN_BEGIN( scan_state )
    MPS_RETAIN( &blockfile->file, TRUE );
  MPS_SCAN_END( scan_state );
  return MPS_RES_OK;
}


/* -------------------------------------------------------------------------- */
#define RSD_MAXFILESIZE 0x7FFFFFFFu
static Bool rsd_getblockfile( RSD_BLOCKLIST *blocklist , FILELIST **rflptr )
{
  RSD_BLOCK *fillblock ;
  FILELIST *flptr ;

  HQASSERT( blocklist , "rsd_getblockfile: blocklist NULL" ) ;

  fillblock = blocklist->fillblock ;
  HQASSERT( fillblock , "rsd_getblockfile: fillblock NULL" ) ;
  HQASSERT( fillblock->data != NULL , "rsd_getblockfile: data NULL" ) ;

  if ( fillblock->file ) {
    /* File already written to disk, must be reading. */
    flptr = fillblock->file ;
    HQASSERT( blocklist->format == RSD_COMPRESS_NONE ,
              "Only expect RSD_COMPRESS_NONE for now" ) ;
  }
  else {
    /* File not already written to disk, must be writing. */
    RSD_FILE *blockfile ;
    blockfile = blocklist->tailfile ;
    if ( blockfile == NULL ||
         ( blockfile->size + fillblock->sbytes ) > RSD_MAXFILESIZE ) {
      if ( ! rsd_allocblockfile( blocklist , & blockfile ))
        return FALSE ;
      HQASSERT(Hq32x2CompareInt32(&fillblock->offset, 0) == 0, "offset != 0");
    }
    else
      Hq32x2FromInt32(&fillblock->offset, blockfile->size);
    blockfile->size += ( fillblock->sbytes + RSD_RAND_DIM - 1 ) &
                      ~( RSD_RAND_DIM - 1 ) ;
    flptr = fillblock->file = blockfile->file ;
    HQASSERT( blocklist->format == RSD_COMPRESS_NONE ,
              "Only expect RSD_COMPRESS_NONE for now" ) ;
  }

  /* Rewind filter chain (if present) and put file in the right position. */
  if ( !rsd_rewindfile( flptr , &fillblock->offset ))
    return FALSE ;

  *rflptr = flptr ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool rsd_blocktodisk( RSD_BLOCKLIST *blocklist , RSD_BLOCK *block )
{
  RSD_BLOCK *sfillblock ;
  FILELIST *flptr ;
  int32 buffersize ;
  int32 bytes ;
  uint8 *buf ;

  HQASSERT( blocklist , "rsd_blocktodisk: blocklist NULL" ) ;

  sfillblock = blocklist->fillblock ; /* save/restore fillblock */
  blocklist->fillblock = block ;      /* the one to be purged. */

  if ( ! rsd_getblockfile( blocklist , & flptr ))
    return FALSE ;

  blocklist->fillblock = sfillblock ;

  HQASSERT( block , "rsd_blocktodisk: block NULL" ) ;
  HQASSERT( block->data , "rsd_blocktodisk: block->data NULL" ) ;

  bytes = block->sbytes ;
  HQASSERT( bytes > 0 , "rsd_blocktodisk: bytes <= 0" ) ;

  buf = block->data ;

  /* Opening a new file (as rsd_getblockfile does) and immediately reading the
   * buffersize is not compatible with lazy file buffering, so enure the buffer
   * is allocated by writing a byte to the file. We'll then overwrite it in the
   * buffer filling loop anyway. Need to also ungetc that extra byte we have
   * written, otherwise the byte count will be off by one. */
  if ( Putc( buf[0] , flptr ) == EOF )
    return FALSE ;
  UnGetc(buf[0], flptr ) ;

  buffersize = theIBufferSize( flptr ) ;
  HQASSERT( buffersize , "rsd_blocktodisk: buffersize == 0" ) ;

  /* Write block data to disk. */
  do {
    int32 tbytes , c ;

    tbytes = min( bytes, buffersize );

    HqMemCpy( theIBuffer( flptr ) , buf , tbytes ) ;

    theICount( flptr ) += tbytes ;
    theIPtr( flptr ) += (tbytes - 1) ;
    c = ( int32 ) buf[tbytes - 1] ;

    if ( (*theIFlushBuffer( flptr ))( c , flptr ) == EOF )
      return FALSE ;

    buf += tbytes ;
    bytes -= tbytes ;
  } while ( bytes > 0 ) ;

  block->fbytes = block->sbytes ;

  return TRUE ;
}

/* -------------------------------------------------------------------------- */
static Bool rsd_needrewind( RSD_BLOCKLIST *blocklist )
{
  Bool rewind ;

  HQASSERT( blocklist , "rsd_needrewind: blocklist NULL" ) ;

  if ( blocklist->fillblock ) {
    RSD_BLOCK *block , *fillblock ;
    /* If fillblock occurs after readblock will need to do rewind. */
    HQASSERT( blocklist->readblock , "rsd_needrewind: readblock NULL" ) ;
    block = blocklist->readblock->next ;
    fillblock = blocklist->fillblock ;
    rewind = FALSE ;
    while ( block && ! rewind ) {
      if ( block == fillblock )
        rewind = TRUE ;
      block = block->next ;
    }
  }
  else {
    /* At EOF */
    rewind = TRUE ;
  }
  return rewind ;
}

/* -------------------------------------------------------------------------- */
static Bool rsd_rewindfile( FILELIST *flptr , Hq32x2 *offset )
{
  HQASSERT( flptr , "flptr NULL" );
  HQASSERT( Hq32x2CompareInt32(offset, 0) >= 0, "offset < 0" );

  if ( theIUnderFile( flptr ) != NULL ) {
    /* Have a filter chain , rewind it. */
    Hq32x2 filepos;

    Hq32x2FromInt32(&filepos, 0);
    if ( (*theIMySetFilePos( flptr ))( flptr , &filepos ) == EOF )
      return FALSE;
  }
  /* Put the file at the bottom of the chain in the right position. */
  return rsd_restorefilepos( flptr , offset );
}

/* -------------------------------------------------------------------------- */
static Bool rsd_savefilepos(FILELIST *flptr, Hq32x2 *offset)
{
  int32 filepos = 0;
  int32 posok;

  HQASSERT(flptr, "flptr NULL");
  while ( theIUnderFile( flptr ) != NULL ) {
    if ( !isIOpenFileFilterById( theIUnderFilterId(flptr),
                                 theIUnderFile(flptr))) {
      return FALSE;
    }
    filepos -= theICount(flptr);
    flptr = theIUnderFile(flptr);
  }

  posok = (*theIMyFilePos(flptr))(flptr, offset);
  HQASSERT(posok != EOF, "totally enexpected EOF");
  Hq32x2AddInt32(offset, offset, filepos);
  return TRUE;
}

/* -------------------------------------------------------------------------- */
static Bool rsd_restorefilepos( FILELIST *flptr , Hq32x2 *filepos )
{
  HQASSERT( flptr , "flptr NULL" );
  HQASSERT( Hq32x2CompareInt32(filepos, 0) >= 0, "filepos < 0" );

  while ( theIUnderFile( flptr ) != NULL ) {
    if ( !isIOpenFileFilterById( theIUnderFilterId(flptr) ,
                                 theIUnderFile(flptr))) {
      return (FALSE);
    }
    flptr = theIUnderFile(flptr);
  }

  if ( (*theIMyResetFile(flptr))(flptr) == EOF ) {
    return (FALSE);
  }
  return ((*theIMySetFilePos(flptr))(flptr, filepos) != EOF);
}

/* -------------------------------------------------------------------------- */

/* Iterators to allow removal of any temporary files created by the store
   that have not been deleted for some reason. */
DEVICELIST *rsd_device_first(rsd_device_iterator_h iter)
{
  HQASSERT(iter, "No RSD device iterator") ;

  *iter = 0 ;

  return find_device((uint8 *) "tmp") ;
}

DEVICELIST *rsd_device_next(rsd_device_iterator_h iter)
{
  UNUSED_PARAM(rsd_device_iterator_h, iter);

  /* Stub function which should be removed */
  return NULL ;
}

/* -------------------------------------------------------------------------- */
Bool rsd_datapools_create( void )
{
  /* Create RSD data pool if not already encountered an RSD filter */

  if ( mm_pool_rsd != NULL )
    return TRUE ;

  if ( mm_pool_create( & mm_pool_rsd, RSD_POOL_TYPE,
                         RSD_POOL_PARAMS ) != MM_SUCCESS )
    return error_handler(VMERROR);

  return TRUE;
}

void rsd_datapools_destroy( void )
{
  /* Destroy RSD data pool (if created) */
  if ( mm_pool_rsd != NULL ) {
/* I don't want to add the v20 export directory to fileio. See leave
   turned off for now. */
#if defined(METRICS_BUILD)
    { /* Track peak memory allocated in pool. */
      size_t max_size = 0, max_frag = 0;
      int32 max_objects ;
      mm_debug_total_highest(mm_pool_rsd, &max_size, &max_objects, &max_frag);
      if (rsd_metrics.rsd_pool_max_size < CAST_SIZET_TO_INT32(max_size))
        rsd_metrics.rsd_pool_max_size = CAST_SIZET_TO_INT32(max_size) ;
      if (rsd_metrics.rsd_pool_max_objects < max_objects)
        rsd_metrics.rsd_pool_max_objects = max_objects ;
      if (rsd_metrics.rsd_pool_max_frag < CAST_SIZET_TO_INT32(max_frag))
        rsd_metrics.rsd_pool_max_frag = CAST_SIZET_TO_INT32(max_frag) ;
    }
#endif
    mm_pool_destroy( mm_pool_rsd ) ;
    mm_pool_rsd = NULL ;
  }
}

/* -------------------------------------------------------------------------- */
#ifdef RSD_STORE_STATS
int32 rsd_bliststats( RSD_BLOCKLIST *blocklist, Bool dumpstore,
                      int *purge0, int *purge1, int *purge2, int *purgen )
{
  RSD_BLOCK *block ;
  int32 totalbytes , nblocks , sbytes , tbytes , fbytes ;

  totalbytes = nblocks = sbytes = tbytes = fbytes = 0 ;
  block = blocklist->headblock ;
  while ( block ) {
    nblocks += 1 ;
    sbytes += block->sbytes ;
    tbytes += block->tbytes ;
    fbytes += block->fbytes ;
    switch ( block->purgecount ) {
    case 0 :  *purge0 += 1 ; break ;
    case 1 :  *purge1 += 1 ; break ;
    case 2 :  *purge2 += 1 ; break ;
    default : *purgen += 1 ;
    }
    if ( dumpstore )
      monitorf((uint8*)"%d\tsbytes: %d\ttbytes: %d\tfbytes: %d\tpurgen: %d\n",
               (int)nblocks, (int)block->sbytes, (int)block->tbytes,
               (int)block->fbytes, (int)block->purgecount);
    block = block->next ;
  }
  HQASSERT(blocklist->bytesavailable == tbytes, "bytesavailable != tbytes\n");
  totalbytes += nblocks * sizeof(RSD_BLOCK) + tbytes + sizeof(RSD_BLOCKLIST);
  monitorf((uint8*)"%d blocks (%d bytes for blocks):\n" ,
           (int)nblocks, (int)(nblocks * sizeof( RSD_BLOCK )));
  monitorf((uint8*)"Data (sbytes): %d \t Mem (tbytes): %d \t File (fbytes): %d\n" ,
           (int)sbytes, (int)tbytes, (int)fbytes );
  return totalbytes ;
}
#endif


/* Log stripped */
