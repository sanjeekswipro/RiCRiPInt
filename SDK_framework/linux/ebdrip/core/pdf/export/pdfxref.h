/** \file
 * \ingroup pdf
 *
 * $HopeName: COREpdf_base!export:pdfxref.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1997-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Xref table API
 */

#ifndef __PDFXREF_H__
#define __PDFXREF_H__

#include "swpdf.h"
#include "objects.h"
#include "hq32x2.h"
#include "dictscan.h"

/* ----- External constants ----- */

/** Possible values for XREFCACHE->streamDict. */
enum { XREF_NotStream = 'X' , XREF_StreamDict = 'D' , XREF_FullStream = 'S' } ;

/* ----- External structures ----- */

/** An entry in the Xref Cache. */
typedef struct XREFCACHE {
  /** Object number of this section of table. */
  int32  objnum ;

  /** Next in the linked list. */
  struct XREFCACHE *xrefnxt ;

  /** PS object for this object number. */
  OBJECT pdfobj ;

  /** An ID of when this object was last used.
      For content-stream objects, this is set to the page#.
      For objects created outside the content stream, this is
      call-depth in the Page tree, set negative to allow the two
      cases to be easily distinguished. */
  int32  lastAccessId ;

  /** Generation number of this object. */
  uint16 objgen ; /* only used for assertions */

  /** This can have three possible states: XREF_StreamDict means the
      object currently cached is a dictionary from a stream object
      (i.e. the scanner saw the "stream" token immediately after the
      dictionary), XREF_FullStream means the object cached is a stream
      proper, and XREF_NotStream means that the object currently
      cached is neither a stream dictionary nor a stream (i.e. it's
      not affected by the \c streamDictOnly argument to lookups). */
  int8 streamDict ;

  /** Flag indicating that the cache entry is to be flushed next time
      around. */
  uint8 flushable ;
} XREFCACHE ;


typedef struct XREFTAB {
  /** Object number of this section of table. */
  int32 objnum ;

  /** Number of objects in this section. */
  int32 number ;

  /** Pointer to the next xref table in this section. */
  struct XREFTAB *xrefnxt ;

  /** Pointer to the array (or link) of xref values. */
  XREFOBJ *xrefobj ;
} XREFTAB ;


typedef struct XREFSEC {
  /** Byte offset for where this table exists. */
  Hq32x2 byteoffset ;

  /** Pointer to the next xref section in the file. */
  struct XREFSEC *xrefnxt ;

  /** Pointer to the xref table. */
  XREFTAB *xreftab ;
} XREFSEC ;


/* Walk the entire cache, freeing all cache objects which are no longer
   required. Returns TRUE if anything was deallocated. */
Bool pdf_sweepxref( PDFCONTEXT *pdfc , Bool closing , int32 depth ) ;


#if defined( DEBUG_BUILD )
void pdf_xrefpagetotals( PDFCONTEXT *pdfc ) ;
void pdf_xrefcache_dumppage( PDFCONTEXT *pdfc, int32 page ) ;
#endif

void pdf_sweepxrefpage( PDFCONTEXT *pdfc, int32 page ) ;
void pdf_xrefreset( PDFCONTEXT *pdfc ) ;
void pdf_xrefexplicitpurge( PDFCONTEXT *pdfc, int32 objnum ) ;

Bool pdf_xrefexplicitaccess( PDFXCONTEXT *pdfxc, int32 objnum ,
                             Bool permanent ) ;
Bool pdf_xrefexplicitaccess_stream( PDFXCONTEXT *pdfxc, OBJECT *stream ,
                                    Bool permanent ) ;
Bool pdf_xrefmakeseekable_stream( PDFXCONTEXT *pdfxc , OBJECT *stream ) ;
Bool pdf_xrefexplicitaccess_dictmatch( PDFXCONTEXT *pdfxc ,
                                       NAMETYPEMATCH *match ,
                                       OBJECT *dict , Bool permanent ) ;
void pdf_xrefthispageonly( PDFXCONTEXT *pdfxc , OBJECT *stream ) ;

/** Estimate the size of the xref cache objects that can be freed. */
size_t pdf_measure_sweepable_xrefs( PDFCONTEXT *pdfc ) ;


/* Call this to lookup an indirect object, given its number and generation,
and get an in-memory pointer to it. The object will be read into cache from
disk if necessary. The object will remain in the cache until the current page
has been rendered, allowing it to be accessed during rendering if needed. */
Bool pdf_lookupxref( PDFCONTEXT *pdfc , OBJECT **rpdfobj ,
                     int32 objnum , uint16 objgen , Bool streamDictOnly ) ;

/* Resolve any indirect references within the given object (see
pdf_lookupxref()), recursing as necessary. */
Bool pdf_resolvexrefs( PDFCONTEXT *pdfc , OBJECT *theo ) ;

Bool pdf_seek_to_xrefobj( PDFCONTEXT *pdfc , FILELIST *flptr ,
                          int32 objnum , int32 objgen , int32 *objuse,
                          FILELIST ** pstream ) ;
Bool pdf_getxrefobj( PDFCONTEXT *pdfc , int32 objnum , uint16 objgen ,
                     XREFOBJ **rxrefobj ) ;
Bool pdf_setxrefobjoffset( XREFOBJ *xrefobj , Hq32x2 objoff , uint16 objgen ) ;
Bool pdf_setxrefobjuse( XREFOBJ *xrefobj , uint16 objgen , uint8 objuse ) ;

void pdf_xref_release_stream( PDFXCONTEXT *pdfxc , OBJECT *stream ) ;
void pdf_deferred_xrefcache_flush( PDFXCONTEXT *pdfxc ) ;

void pdf_storecompressedxrefobj( XREFOBJ *xrefobj , int32 objnum ,
                                 uint16 streamindex ) ;
void pdf_storexrefobj( XREFOBJ *xrefobj , Hq32x2 objoff , uint16 objgen ) ;
void pdf_storefreexrefobj( XREFOBJ *xrefobj , int32 objnum , uint16 objgen ) ;

XREFCACHE *pdf_allocxrefcache( PDFCONTEXT *pdfc , int32 objnum ,
                               uint16 objgen , OBJECT *pdfobj ,
                               Bool streamDict ) ;

XREFOBJ *pdf_allocxrefobj( PDFCONTEXT *pdfc , XREFTAB *xreftab ,
                           int32 number ) ;
XREFTAB *pdf_allocxreftab( PDFCONTEXT *pdfc , XREFSEC *xrefsec , int32 objnum ,
                           int32 number ) ;
XREFSEC *pdf_allocxrefsec( PDFCONTEXT *pdfc , Hq32x2 byteoffset ) ;

void pdf_flushxrefsec( PDFCONTEXT *pdfc ) ;

#endif /* protection for multiple inclusion */


/* Log stripped */
