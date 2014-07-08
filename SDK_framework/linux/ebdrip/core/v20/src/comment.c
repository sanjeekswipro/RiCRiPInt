/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:comment.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * C-based comment parser to replace Postscript version.
 */

#include "core.h"
#include "mm.h"
#include "objects.h"
#include "swerrors.h"
#include "swctype.h"
#include "hqmemcpy.h"
#include "namedef_.h"
#include "fileio.h"

#include "control.h"
#include "stacks.h"
#include "dictops.h"
#include "dicthash.h"
#include "genhook.h"
#include "graphics.h"
#include "execops.h"
#include "chartype.h"
#include "comment.h"
#include "psvm.h"


/* Check if there's a binding for a given tag.  Returns FALSE if
 * there's an error, binding returned as NULL if no binding is found.
 * "adict" parameter allows re-use of previous dictionary lookups. */

static Bool try_lookup(uint8 *nm, int32 ln, int32 is_pp, OBJECT **adict,
                       OBJECT **binding)
{
  OBJECT *pdict;
  NAMECACHE *tag_nm ;

  HQASSERT( *binding == NULL,
    "outgoing parameter already set in try_lookup" ) ;
  HQASSERT( is_pp == 0 || is_pp == 1 , "is_pp should be TRUE or FALSE here." ) ;
  HQASSERT( ln > 0 && ln <= MAXPSNAME , "try_lookup: ln out of range" ) ;
  if ( ( tag_nm = lookupname(nm,ln) ) == NULL )
    return TRUE ;
  if ( adict[is_pp] == NULL ) {
    /* The spec says %%dict has to be in userdict */
    oName( nnewobj ) =
      system_names + (is_pp ? NAME_PercentPercent_Dict : NAME_Percent_Dict);
    pdict = fast_user_extract_hash( &nnewobj ) ;
    if ( pdict == NULL )
      return TRUE ;
    if ( oType(*pdict) != ODICTIONARY )
      return error_handler( TYPECHECK ) ;
    /* try to find the %actions sub-dictionary */
    oName( nnewobj ) =
      system_names + (is_pp ? NAME_PercentPercent_Actions : NAME_Percent_Actions);
    error_clear_newerror();
    adict[is_pp] = extract_hash( pdict, &nnewobj ) ;
    if ( adict[is_pp] == NULL ) {
      if ( newerror )
        return FALSE ;
      return error_handler( UNDEFINED ) ;
    }
    if ( oType(*adict[is_pp]) != ODICTIONARY )
      return error_handler( TYPECHECK ) ;
  }
  oName( nnewobj ) = tag_nm ;
  error_clear_newerror();
  *binding = extract_hash( adict[is_pp], &nnewobj ) ;
  return ! newerror ;
}

/* Does the next line start with %?  We could loop discarding white space
 * if that would be useful... */

static Bool peek_ahead(register FILELIST * flptr)
{
  register int32 c ;

 /* Check file still open,
    may have been closed by the binding */
  if ( ! isIOpenFile( flptr ))
    return FALSE ;
  if (( c = Getc( flptr )) == EOF )
    return FALSE ; /* let the scanner notice the EOF and any error */
  if ( c == LF && isICRFlags( flptr ) ) {
    ClearICRFlags( flptr ) ;
    /* Try again for a legit character */
    if (( c = Getc( flptr )) == EOF )
      return FALSE ; /* let the scanner notice the EOF and any error */
  }
  if ( c == '%' ) {
    ClearICRFlags( flptr ) ;
    return TRUE ;
  }
  UnGetc( c , flptr ) ;
  return FALSE ;
}

/* Arrange for one CommentChunk_t to take up 256 bytes, which is
 * easier on the memory system. */

#define COMMENT_CHUNK_LEN (256 - sizeof(void *))

typedef struct CommentChunk_t {
  struct CommentChunk_t *prev;
  uint8 chunk[COMMENT_CHUNK_LEN] ;
} CommentChunk_t ;

enum DeferredProblem_t {
  DP_NO_PROBLEM = 0, DP_TOO_BIG, DP_ALLOC_FAILED, DP_FILE_ERROR
} ;

/* Read and discard a block of comments from flptr; if scan_comments is
 * TRUE then see if any comment parser have been registered for them.
 * The second and subsequent comments in a "block" have to start at the
 * beginning of a line, or they'll be handled in subesequent calls to
 * this routine. That could easily be changed if necessary. */

Bool handle_comments(register FILELIST * flptr, int32 scan_comments)
{
  int32 c ;
  OBJECT *adict[2] ;
  int32 last_taglen = -1, last_is_pp = -1;
  uint8 tag_name[MAXPSNAME + 2] ;

  /* We don't have the action dictionaries yet */
  adict[0] = NULL;
  adict[1] = NULL;
  do {
    int32 taglen, is_pp ;
    OBJECT *binding = NULL ;

    HQASSERT ( !isICRFlags( flptr ),
      "Preceding character should be % not CR" ) ;
    c = Getc( flptr ) ;
    /* c always contains an unconsumed char right until the end of the
     * loop; everything that consumes it refills it.  If c == EOF, handling that
     * is also mostly deferred. */
    if ( scan_comments ) {
      int32 remlen = 0 ;
      is_pp = ( c == '%' ) ;
      if ( is_pp )
        c = Getc( flptr ) ; /* Consume it, get another */
      taglen = 0;
      while ( c != EOF && ! IsWhiteSpace(c) &&
        c != ':' && c != '+' && taglen < MAXPSNAME +1 ) {
        tag_name[taglen++] = (uint8) c ;
        c = Getc( flptr ) ;
      }
      /* We don't treat "c" as properly consumed yet, so
       * we defer handling line numbers, CR flags etc */
      if ( c == EOF && isIIOError( flptr ) )
        return (*theIFileLastError( flptr )) (flptr) ;
      /* otherwise process this comment and notice any EOF later */

      if ( c == ':' ) {
        /* Append the colon */
        tag_name[taglen++] = ':' ;
        c = Getc( flptr ) ; /* consume the : */
      }
      if ( taglen == 0 ) {
        /* Is this a %%+ (or %+) ? */
        if ( c == '+' ) {
          c = Getc( flptr ) ; /* Consume the + */
          /* We have to do another lookup because a restore could destroy
           * the thing we found last time */
          if ( last_is_pp != -1 &&
               ! try_lookup( tag_name, last_taglen, last_is_pp, adict, &binding ) )
            return FALSE ;
        }
      } else {
        /* Start with the complete token and then
         * try progressively shorter sub-tokens */
        int32 readlen = taglen ;
        if ( taglen > MAXPSNAME )
          taglen = MAXPSNAME ;
        do {
          if ( ! try_lookup( tag_name, taglen, is_pp, adict, &binding ) )
            return FALSE;
        } while ( binding == NULL && --taglen > 0 ) ;
        remlen = readlen - taglen ; /* Remainder of token not bound */
        last_taglen = taglen; last_is_pp = is_pp;
      }

      if ( binding == NULL ) {
        /* Don't try this name again */
        last_taglen = last_is_pp = -1;
      } else  {
        /* OK, we have a binding, now read in the rest of the comment */
        CommentChunk_t init_chunk, *curchunk, *newchunk ;
        uint8 *bp, *bplim, *body = NULL;
        int32 bodylen = 0 ; /* keep compiler quiet */
        int32 chunk_offset, deferred_problem = DP_NO_PROBLEM ;

        chunk_offset = remlen ;
        curchunk = &init_chunk ;
        curchunk->prev = NULL ;
        bp = curchunk->chunk ;
        bplim = bp + COMMENT_CHUNK_LEN ;
        while ( c != EOF && !IsEndOfLine(c) && c != FF ) {
          if (bp == bplim) {
            if ( chunk_offset + ( bp - curchunk->chunk ) == MAXPSSTRING ) {
              deferred_problem = DP_TOO_BIG ;
              break;
            }
            /* Create another node in the linked list of comment chunks */
            newchunk = (CommentChunk_t *) mm_alloc( mm_pool_temp,
              sizeof(CommentChunk_t), MM_ALLOC_CLASS_GENERAL ) ;
            if ( newchunk == NULL ) {
              deferred_problem = DP_ALLOC_FAILED ;
              break;
            }
            newchunk->prev = curchunk;
            curchunk = newchunk;
            chunk_offset += COMMENT_CHUNK_LEN;
            bp = curchunk->chunk;
            if ( chunk_offset + COMMENT_CHUNK_LEN > MAXPSSTRING ) {
              bplim = bp + (MAXPSSTRING - chunk_offset);
            } else {
              bplim = bp + COMMENT_CHUNK_LEN;
            }
          }
          *bp++ = (uint8) c ;
          c = Getc( flptr ) ;
        }
        if ( c == EOF && isIIOError( flptr ) )
          deferred_problem = DP_FILE_ERROR ;

        if ( deferred_problem == DP_NO_PROBLEM ) {
          bodylen = CAST_PTRDIFFT_TO_INT32(chunk_offset + bp - curchunk->chunk) ;
          if ( bodylen > 0 && (body = get_smemory( bodylen )) == NULL )
            deferred_problem = DP_ALLOC_FAILED ;
        }
        /* Comparison of booleans */
        HQASSERT( ( deferred_problem != DP_TOO_BIG) == (bodylen <= MAXPSSTRING),
          "Incorrect detection of too-large comment string" ) ;
        if ( body != NULL ) {
          if ( remlen > 0 ) /* Store remainder of token not included in bind */
            HqMemCpy( body, tag_name + taglen, remlen ) ;
          if ( bodylen > chunk_offset )
            HqMemCpy( body + chunk_offset, curchunk->chunk,
                      bodylen - chunk_offset ) ;
        }
        while ( ( newchunk = curchunk->prev ) != NULL ) {
          /* Hop backwards through the linked list, copying the comments in
           * and freeing the chunks. */
          mm_free( mm_pool_temp, curchunk,
            sizeof(CommentChunk_t) ) ;
          curchunk = newchunk;
          chunk_offset -= COMMENT_CHUNK_LEN ;
          if ( body != NULL )
            HqMemCpy( body + chunk_offset, curchunk->chunk, COMMENT_CHUNK_LEN ) ;
        }
        switch ( deferred_problem ) {
        case DP_ALLOC_FAILED:
          return error_handler( VMERROR ) ;
        case DP_FILE_ERROR:
          return (*theIFileLastError( flptr )) (flptr) ;
        case DP_TOO_BIG:
          /* Don't process this or subsequent %%+ lines. */
          binding = NULL ;
          last_taglen = last_is_pp = -1 ;
          break ;
        case DP_NO_PROBLEM:
          /* OK, make a string out of it. */
          oString( snewobj ) = body;
          theLen( snewobj ) = (uint16) bodylen ;

          /* Push the string onto the stack */
          if ( !push( &snewobj, &operandstack ) )
            return FALSE ;
          /* The binding isn't executed until the line ending is consumed */
          break;
        default:
          HQFAIL( "deferred_problem took on unexpected value" ) ;
          return FALSE;
        }
        /* Leave last_is_pp untouched when doing a comment extension */
        if ( taglen != 0 )
          last_is_pp = is_pp ;
      }
    }
    /* Consume c, handling CRflags etc. */
    for (;;) {
      if ( c == EOF ) {
        if ( isIIOError( flptr ))
          return (*theIFileLastError( flptr )) (flptr) ;
        else
          break ;
      }
      if ( IsEndOfLine( c ) ) {
        if ( c == CR ) {
          SetICRFlags( flptr ) ;
        } else  {
          HQASSERT( c == LF, "Line ending should be CR or LF" ) ;
          HQASSERT( !isICRFlags( flptr ),
            "CRFlags became set before a line ending was processed" ) ;
        }
        theIFileLineNo( flptr )++ ;
        break ;
      }
      if ( c == FF )
        break;
      HQASSERT( binding == NULL,
        "A good binding should mean we found the end of the line" ) ;
      c = Getc( flptr );
    }
    if ( binding ) {
      /* The file pointer is in the right state; execute the binding. */
      if ( ! push( binding , & executionstack ))
        return FALSE ;
      if ( ! interpreter( 1 , NULL ))
        return FALSE ;
      /* Interpreter might do anything, even restore, so
       * invalidate those lookups. */
      adict[0] = NULL ;
      adict[1] = NULL ;
    }
  } while ( c != EOF && peek_ahead( flptr ) );
  return TRUE ;
}

/* Log stripped */
