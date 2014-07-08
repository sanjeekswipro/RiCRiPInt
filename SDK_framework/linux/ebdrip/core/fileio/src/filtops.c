/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!src:filtops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Filter operations
 */


#include "core.h"
#include "swerrors.h"
#include "swdevice.h"
#include "hqmemcpy.h"
#include "hqmemcmp.h"
#include "objects.h"
#include "objstack.h"
#include "dictscan.h"
#include "mm.h"
#include "mmcompat.h"
#include "mps.h"
#include "devices.h"
#include "namedef_.h"
#include "monitor.h"

#include "fileio.h"
#include "fileimpl.h"
#include "strfilt.h"

/* Debugging switch */

#if defined( ASSERT_BUILD )
Bool debug_filters = FALSE ;
#endif

/*
  new stuff for generic/external filters
*/

#define FILTER_ENCODE 1
#define FILTER_DECODE 2

typedef struct swstart_filter {
  uint8 *filter_name;    /* not necessarily null-terminated */
  int32 name_length;
  int32 params_flag;     /* does the filter have any parameters */
  int32 device_number;
  int32 type;            /* FILTER_ENCODE or FILTER_DECODE */
  struct swstart_filter *next;
} SWSTART_FILTER;


/** Creates a FILELIST structure (either by allocating a new one, or by
 * reusing a previous, suitably dead, one), based on a predefined filter
 * (nflptr), initialises it, and links it into the filter chain. On entry,
 * flptr points to the head of the chain to which the filter will be added
 * (and on which the routine will look for a re-usable filter). On successful
 * exit, the new filter will be at the head of the chain. The alloc function
 * will be called to allocate a new FILELIST if no reusable node is found.
 */
Bool filter_create_with_alloc(FILELIST *nflptr, FILELIST **flptr,
                              OBJECT *args, STACK *stack,
                              int32 pdf_contextid,
                              FILELIST *(*alloc)(mm_pool_t), mm_pool_t pool)
{
  corecontext_t* context = get_core_context();
  FILELIST *ru_fptr, **previous_files_next ;
  uint16 ru_fid ;
  Bool result ;
  uint8 glallocmode = FALSE ;

  HQASSERT(nflptr, "No template filter") ;
  HQASSERT(flptr, "No return filter pointer") ;

  if (!context->is_interpreter) {
    monitorf((uint8 *) "**** Aborting due to filter creation whilst rendering\n");
    return error_handler(UNDEFINED);
  }

  previous_files_next = flptr ;

  /* PDF allocations are treated as local, regardless of the savelevel or
     allocation mode. This prevents PDF file objects from being stored in
     global PS dictionaries. */
  if ( pool == mm_pool_ps_typed_global )
    glallocmode = TRUE ;

  /* If a closed filter exists at the required save level, we are going to
     reuse its memory. This is a somewhat unpleasant way of avoiding garbage
     collection. In order that any objects referring to this filter node do not
     get confused we store an identifying number in the node and in the length
     field of each object pointing to a filter. If these no longer agree, then
     the filter originally referred to by the object is known to be closed and
     no further reference to the filter node it is pointing to is needed or
     allowed, other than the Input/Outputness of the filter, so we check for
     that as well. */
  ru_fid = FIRSTFILTERID ;
  while ( (ru_fptr = *previous_files_next) != NULL ) {
    if ( isIFilter( ru_fptr ) &&
         !isIOpenFile( ru_fptr ) &&
         isIInputFile( ru_fptr ) == isIInputFile( nflptr ) &&
         theIFilterId( ru_fptr ) < LASTFILTERID &&
         (pdf_contextid != 0 ?
          /* The rewindable test came from pdf_createfilter; it's not obvious
             why it's needed, it prevents all input filters from being
             recycled (possibly to prevent losing the StreamDecode dict?). */
          (!isIRewindable(ru_fptr) && theIPDFContextID(ru_fptr) == pdf_contextid) :
          (glallocmode ?
           /* If doing a global allocation, any global filter will do.
              Otherwise require the savelevel to be the same. */
           (theISaveLevel(ru_fptr) & GLOBMASK) != ISLOCAL :
           theISaveLevel(ru_fptr) == context->savelevel)) ) {
      /* suitable for reuse - disconnect from list if not already at the head,
       * because a filter must always come before its underlying file in the
       * file list, otherwise if it writes data on a close caused by a restore,
       * the underlying file may already have been closed
       */
      ru_fid = CAST_UNSIGNED_TO_UINT16(theIFilterId( ru_fptr ) + 1) ; /* by definition != FIRSTFILTERID */
      *previous_files_next = ru_fptr->next ;
      break ;
    }
    previous_files_next = & ru_fptr->next ;
  }

  if ( ru_fptr == NULL ) {
    /* there wasn't a closed filter for which we could reuse the memory */
    if ( (ru_fptr = (*alloc)(pool)) == NULL )
      return error_handler( VMERROR ) ;
  }

  *ru_fptr = *nflptr ; /* copy the filter template */
  theIFilterId( ru_fptr ) = ru_fid ;
  /* We use the same technique for files as for objects, saving the
     globalness in the lowest bit and the savelevel in higher bits. Saving
     the globalness lets us reconstruct a file object with appropriate
     globalness when needed. */
  theISaveLevel( ru_fptr ) = CAST_TO_UINT8(context->savelevel | glallocmode) ;
  theIPDFContextID(ru_fptr) = pdf_contextid ;

  /* Call hook function to check this filter is OK to open. */
  result = filter_create_hook(ru_fptr, args, stack) ;

  if ( result ) /* Initialise the filter; this may create underlying filters */
    if ( ! (*theIMyInitFile(ru_fptr))(ru_fptr, args, stack) )
      result = (*theIFileLastError(ru_fptr))(ru_fptr) ;

  /* Link the filter back into the VM chain after initialisation. If the
     initialisation fails, the filter is marked as closed (by
     theIFileLastError) and it can be recycled. The initialisation routine
     may create underlying filters, which must be added to the file list
     before this filter is added, to maintain the invariant that underlying
     files appear after filters using them. */
  ru_fptr->next = *flptr ;
  *flptr = ru_fptr ;

  /* Mark filter as open if we succeeded */
  if ( result )
    SetIOpenFlag(ru_fptr) ;

  HQASSERT(!isIOpenFile(ru_fptr) ||
           theIUnderFile(ru_fptr) == NULL ||
           NUMBERSAVES(theISaveLevel(ru_fptr)) >= NUMBERSAVES(theISaveLevel(theIUnderFile(ru_fptr))) ||
           /* Overlying filter must be local, underlying must be global if
              savelevels are inverted. */
           (theISaveLevel(ru_fptr) & GLOBMASK) < (theISaveLevel(theIUnderFile(ru_fptr)) & GLOBMASK),
           "Underlying filter is at higher save level") ;

  /* Debug tracing for PS over PDF or PDF over PS at the same savelevel. This
     situation is potentially risky. */
  HQTRACE(debug_filters &&
          isIOpenFile(ru_fptr) &&
          theIUnderFile(ru_fptr) != NULL &&
          NUMBERSAVES(theISaveLevel(ru_fptr)) <= NUMBERSAVES(theISaveLevel(theIUnderFile(ru_fptr))) &&
          (theISaveLevel(theIUnderFile(ru_fptr)) & GLOBMASK) == ISLOCAL &&
          (theISaveLevel(ru_fptr) != theISaveLevel(theIUnderFile(ru_fptr)) ||
           theIPDFContextID(ru_fptr) != theIPDFContextID(theIUnderFile(ru_fptr))),
          ("%s filter %.*s over %s filter %.*s at same or smaller savelevel",
           theIPDFContextID(ru_fptr) ? "PDF" : "PostScript",
           ru_fptr->len, ru_fptr->clist,
           theIPDFContextID(theIUnderFile(ru_fptr)) ? "PDF" : "PostScript",
           theIUnderFile(ru_fptr)->len, theIUnderFile(ru_fptr)->clist)) ;

  return result ;
}

static FILELIST *ps_typed_alloc(mm_pool_t pool)
{
  HQASSERT(pool != NULL, "No pool for typed PS alloc") ;
  return (FILELIST *)mm_ps_alloc_typed(pool, sizeof(FILELIST));
}

/** Creates a FILELIST structure (either by allocating a new one, or by
 * reusing a previous, suitably dead, one), based on a predefined
 * filter (nflptr), initialises it, and links it into the filter chain.
 * Allocations come from the PostScript typed memory pool.
 */
Bool filter_create(FILELIST *nflptr, FILELIST **flptr, OBJECT *args,
                   STACK *stack)
{
  corecontext_t *context = get_core_context_interp();
  FILELIST **filter_chain ;

  HQASSERT(flptr, "No return filter pointer") ;

  if ( context->glallocmode )
    filter_chain = &gvm_filefilters ;
  else
    filter_chain = &lvm_filefilters ;

  *flptr = NULL ; /* For safety */
  if ( !filter_create_with_alloc(nflptr, filter_chain, args, stack,
                                 0 /* Not PDF, so context id is zero */,
                                 ps_typed_alloc,
                                 (context->glallocmode
                                    ? mm_pool_ps_typed_global
                                    : mm_pool_ps_typed_local)) )
    return FALSE ;

  HQASSERT(*filter_chain != NULL, "No filter created") ;
  *flptr = *filter_chain ;

  HQASSERT(context->glallocmode == (theISaveLevel(*flptr) & GLOBMASK),
           "Filter globalness is inconsistent with global allocation mode") ;
  HQASSERT(context->savelevel == (theISaveLevel(*flptr) & SAVEMASK),
           "Filter savelevel is inconsistent with save level") ;

  return TRUE ;
}

/* Similar to filter_create, but fills out a filter object */
Bool filter_create_object(FILELIST *nflptr, OBJECT *filter, OBJECT *args,
                          STACK *stack)
{
  FILELIST *new_flptr;

  HQASSERT(nflptr, "No template filter") ;
  HQASSERT(filter, "No return object pointer") ;

  if ( !filter_create(nflptr, &new_flptr, args, stack) )
    return FALSE ;

  /* set up the file object */
  file_store_object(filter, new_flptr, LITERAL) ;

  HQASSERT(get_core_context_interp()->glallocmode == oGlobalValue(*filter),
           "Filter object globalness is inconsistent with global allocation mode") ;

  return TRUE ;
}


/*---------------------------------------------------------------------------*/

/* Support for external filters. External filters implement a connection
   between a filter and a device. */
static SWSTART_FILTER *Filter_List = NULL; /* External filters */

static SWSTART_FILTER *external_filter_find( uint8 *name, int32 len,
                                             SWSTART_FILTER **last )
/* for external filters only */
{
  register SWSTART_FILTER *flptr ;
  register SWSTART_FILTER *last_flptr ;

  for ( flptr = Filter_List, last_flptr = flptr ; flptr != NULL ;
       last_flptr = flptr, flptr = flptr->next ) {
    if ( len == flptr->name_length &&
         HqMemCmp(name, len, flptr->filter_name, len) == 0 ) {
      *last = last_flptr ;
      break ;
    }
  }
  return flptr ;
}

Bool filter_external_define(OBJECT *dict, OBJECT *name)
{
  enum {
    filterdict_FilterType,
    filterdict_FilterNumber,
    filterdict_dummy
  } ;
  static NAMETYPEMATCH filterdictmatch[filterdict_dummy + 1] = {
    { NAME_FilterType , 1, { OSTRING }},
    { NAME_FilterNumber , 1, { OINTEGER }},
    DUMMY_END_MATCH
  } ;

  SWSTART_FILTER *flptr;
  uint8 *s, *filter_name;
  int32 dev_number, type, name_length;

  switch ( oType(*name) ) {
  case ONAME:
    filter_name = theICList(oName(*name)) ;
    name_length = theINLen(oName(*name)) ;
    break ;
  case OSTRING:
    /* undocumented - but they allow strings as an argument */
    filter_name = oString(*name) ;
    name_length = theLen(*name) ;
    break ;
  default:
    return error_handler( TYPECHECK ) ;
  }

  if ( oType(*dict) != ODICTIONARY )
    return error_handler( TYPECHECK );

  if ( !dictmatch(dict, filterdictmatch) )
    return FALSE ;

  if ( theLen(*filterdictmatch[filterdict_FilterType].result) != 1 )
    return error_handler( INVALIDFILEACCESS );

  switch ( *oString(*filterdictmatch[filterdict_FilterType].result) ) {
  case 'r': case 'd' :
    type = FILTER_DECODE ;
    break ;
  case 'w': case 'e' :
    type = FILTER_ENCODE ;
    break ;
  default :
    return error_handler( INVALIDFILEACCESS );
  }
  dev_number = oInteger(*filterdictmatch[filterdict_FilterNumber].result);

  /* add filtername and device number to (head of) list */
  if ( NULL == ( flptr = ( SWSTART_FILTER * )mm_alloc(mm_pool_temp,
                                                      sizeof( SWSTART_FILTER ),
                                                      MM_ALLOC_CLASS_SWSTART_FILTER)))
    return error_handler( VMERROR );
  if ( NULL == ( s = ( uint8 * )mm_alloc_with_header(mm_pool_temp,
                                               sizeof( uint8 ) * name_length + 1,
                                               MM_ALLOC_CLASS_FILTER_NAME)))
  {
    mm_free(mm_pool_temp,(mm_addr_t)flptr, sizeof( SWSTART_FILTER ) );
    return error_handler( VMERROR );
  }
  HqMemCpy( s, filter_name, name_length );
  flptr->filter_name = s;
  flptr->name_length = name_length;
  flptr->device_number = dev_number;
  flptr->type = type;

  flptr->next = Filter_List;
  Filter_List = flptr;

  HQTRACE( debug_filters , ( "externalfilter finished - %.*s added to list." ,
                             name_length , filter_name )) ;

  return TRUE;
}

Bool filter_external_exists(uint8 *filter_name, int32 name_length)
{
  SWSTART_FILTER *last;

  return (external_filter_find( filter_name, name_length, &last ) != NULL) ;
}

Bool filter_external_undefine(uint8 *filter_name, int32 name_length)
{
  SWSTART_FILTER *flptr, *last = NULL;

  if ( (flptr = external_filter_find(filter_name, name_length, &last)) != NULL) {
    HQTRACE( debug_filters , ( "%.*s found, removing it",
                               name_length, filter_name )) ;
    last->next = flptr->next ;
    if ( flptr == Filter_List )    /* deleting head of list */
      Filter_List = flptr->next ;

    mm_free_with_header(mm_pool_temp,(mm_addr_t)flptr->filter_name );
    mm_free(mm_pool_temp,(mm_addr_t)flptr, sizeof( SWSTART_FILTER ) );
  }

  return TRUE ;
}

Bool filter_external_forall(STACK *restack)
{
  SWSTART_FILTER *flptr;

  HQTRACE( debug_filters , ( "Enumerating filters -" )) ;

  for ( flptr = Filter_List ; flptr != NULL ; flptr = flptr->next ) {
    HQTRACE(debug_filters, (" %.*s", flptr->name_length, flptr->filter_name)) ;
    if ( (oName(nnewobj) = cachename(flptr->filter_name,
                                     flptr->name_length)) == NULL )
      return FALSE;
    if ( !push( &nnewobj, restack ) )
      return FALSE ;
  }

  HQTRACE( debug_filters , ( "" )) ;

  return TRUE ;
}

/*
 * filter_external_find()
 *
 * if successful return filter found
 * if fails returns NULL and *find_error is set, NOT_AN_ERROR => not found
 */

FILELIST *filter_external_find(uint8 *name,
                               int32 len,
                               int32 *find_error,
                               Bool fromPS)
{
  SWSTART_FILTER *ptr, *last;      /* external filter pointer */
  FILELIST       *flptr;           /* to be returned */
  DEVICELIST     *dlist;
  uint8          *filter_name = NULL;  /* for "GENERIC" + En/Decode */

  *find_error = NOT_AN_ERROR;
  if ( (ptr = external_filter_find(name, len, &last) ) != NULL ) {
    HQTRACE( debug_filters , ( "found %.*s - ", len, (char*)name )) ;

    switch ( ptr->type ) {
    case FILTER_ENCODE:
      filter_name = (uint8*)"GENERICEncode" ;
      break ;
    case FILTER_DECODE:
      filter_name = (uint8*)"GENERICDecode" ;
      break ;
    default:
      HQFAIL("Unrecognised filter type on external list") ;
    }

    if ( NULL == (flptr = filter_standard_find(filter_name,
                                               strlen_int32( (char*)filter_name))) )
      return NULL;

    /* allocate new devicelist for transform device */
    if ( NULL == (dlist = device_alloc(ptr->filter_name, ptr->name_length)) ) {
      *find_error = newerror ;
      return NULL ;
    }

    if ( !device_connect(dlist, ptr->device_number,
                         (char *)theIDevName(dlist), 0, !fromPS) ) {
      if ( !device_error_handler(dlist) ) {
        /* Pick up newerror setup by device_error_handler() - Note: FALSE
         * implies error in device! */
        *find_error = newerror;
      }
      device_free(dlist) ;
      return NULL ;
    }

    /* connect up structures */
    theIDeviceList( flptr ) = dlist;

    HQTRACE( debug_filters , ( "successful" )) ;

    return flptr;
  }
  return NULL;
}


/* ----------------------------------------------------------------------------
   function:            get_target_or_source author:              Luke Tunmer
   creation date:       12-Jul-1991          last modification:   ##-###-####
   arguments:  filter index
   description:

   This routine takes a string/file/procedure object and plugs it in as
   the underlying source/target for the filter.
   ---------------------------------------------------------------------------- */
Bool filter_target_or_source( FILELIST *filter, OBJECT *theo )
{
  FILELIST *flptr = NULL ;
  uint16    under_filter_id = 0 ;

  HQASSERT(filter, "No filter to add underlying to") ;
  HQASSERT(theo, "No object for underlying filter") ;

  /* check here for the types of the data stream */
  if ( oType(*theo) == OFILE ) {
    flptr = oFile(*theo) ;
    under_filter_id = theLen(*theo);

    /* open and not dead filter checked below - beware of accessing fields in
       flptr until that is done, other than input/output mode which is valid to
       test */
    if ( isIInputFile( filter )) { /* decode filter */
      if ( !oCanRead(*theo) && !object_access_override(theo) )
        return error_handler(INVALIDACCESS ) ;
    } else { /* encode filter */
      if ( !oCanWrite(*theo) && !object_access_override(theo))
        return error_handler(INVALIDACCESS ) ;
    }
  } else {
    char *base_name ;
    int32 base_len ;

    switch ( oType(*theo) ) {
    case OSTRING :
      /* Permissions checked in stringFilterInit */
      base_name = (isIOutputFile(filter) ? "%stringEncode" : "%stringDecode") ;
      base_len = 13 ;
      break ;
    case OARRAY :
    case OPACKEDARRAY :
      /* Permissions checked in procedureFilterInit */
      base_name = (isIOutputFile(filter) ? "%procedureEncode" : "%procedureDecode") ;
      base_len = 16 ;
      break ;
    default :
      return error_handler(TYPECHECK ) ;
    }

    if ( NULL == (flptr = filter_standard_find((uint8 *)base_name, base_len)) )
      return error_handler(UNDEFINED) ;

    if ( ! filter_create(flptr, &flptr, theo, NULL) )
      return FALSE ;

    under_filter_id = theIFilterId(flptr) ;

    /* Always close target or source for underlying string/procedure filters */
    SetICSTFlag(filter) ;
  }

  /* if decode: is file readable, if encode: is file writeable */
  if ( (isIOutputFile(filter) && !isIOutputFile(flptr)) ||
       (isIInputFile(filter) && !isIInputFile(flptr)) ) {
    return error_handler(IOERROR ) ;
  }

  /* are we trying to attach the filter to a closed file ? */
  if ( ! isIOpenFileFilterById( under_filter_id, flptr )) {
    return error_handler(IOERROR ) ;
  }

  theIUnderFile( filter ) = flptr ;
  theIUnderFilterId( filter ) = under_filter_id ;

  return TRUE ;
}

Bool filter_layer(FILELIST *infile,
                  uint8 *name, uint32 namelen,
                  OBJECT *args,
                  FILELIST **filtered)
{
  OBJECT fileo = OBJECT_NOTVM_NOTHING ;

  /* Set up an object for the underlying file, so we can share code with
     filter_layer_object(). */
  file_store_object(&fileo, infile, LITERAL) ;
  if ( !filter_layer_object(&fileo, name, namelen, args, &fileo) )
    return FALSE ;

  HQASSERT(oType(fileo) == OFILE, "Returned object is not a file") ;
  *filtered = oFile(fileo) ;

  return TRUE;
}

Bool filter_layer_object(OBJECT *infile,
                         uint8 *name, uint32 namelen,
                         OBJECT *args,
                         OBJECT *filtered)
{
  FILELIST *flptr, **filter_chain ;
  SFRAME myframe ;
  STACK mystack = { EMPTY_STACK, NULL, FRAMESIZE, STACK_TYPE_OPERAND } ;
  int32 find_error ;
  mm_pool_t pool ;

  mystack.fptr = &myframe ;

  HQASSERT(infile != NULL && oType(*infile) == OFILE, "No input file/filter") ;
  HQASSERT(filtered != NULL, "Nowhere for output filter") ;
  HQASSERT(name != NULL && namelen > 0, "No filter name") ;

  if ( !isIOpenFileFilter(infile, oFile(*infile)) )
    return error_handler(IOERROR);

  flptr = filter_external_find(name, namelen, &find_error, TRUE) ;
  if ( flptr == NULL ) {
    if ( find_error != NOT_AN_ERROR )
      return error_handler(find_error) ;
    flptr = filter_standard_find(name, namelen) ;
    if ( flptr == NULL )
      return error_handler(UNDEFINED) ;
  }
  HQASSERT(isIFilter(flptr), "Not a filter") ;

  if ( oFile(*infile)->pdf_context_id != 0 )
    /* PS filter created over PDF, ask for callback at purge. */
    oFile(*infile)->flags |= PURGE_NOTIFY_FLAG;

  /* Underlying file object is global; need to create our file
     objects as global objects to ensure validity. */
  if ( oGlobalValue(*infile) ) {
    HQASSERT((theISaveLevel(oFile(*infile)) & GLOBMASK) != ISLOCAL,
             "File object is global, but file struct is local") ;
    filter_chain = &gvm_filefilters ;
    pool = mm_pool_ps_typed_global ;
  } else {
    HQASSERT((theISaveLevel(oFile(*infile)) & GLOBMASK) == ISLOCAL,
             "File object is local, but file struct is global") ;
    filter_chain = &lvm_filefilters ;
    pool = mm_pool_ps_typed_local ;
  }

  if ( !push(infile, &mystack) ||
       !filter_create_with_alloc(flptr, filter_chain, args, &mystack,
                                 0, ps_typed_alloc, pool) )
    return FALSE ;

  /* Retain the executability of the original file object. */
  file_store_object(filtered, *filter_chain,
                    CAST_UNSIGNED_TO_UINT8(oExec(*infile))) ;

  return TRUE;
}

/*---------------------------------------------------------------------------*/
/* Unspecialised filter implementation functions; filter implementations can
   use these functions instead of rolling their own. */

int32 FilterReset( FILELIST *filter )
{
  theICount( filter ) = 0 ;
  theIPtr( filter ) = theIBuffer( filter ) ;
  ClearIEofFlag( filter ) ;
  return 0 ;
}


/* Modified to allow rewinding of streams based on a StreamDecode filter */

int32 FilterSetPos( FILELIST *filter , const Hq32x2* position )
{
  FILELIST *uflptr ;
  int32 result = EOF ;

  HQASSERT((position != NULL),
          "FilterSetPos: NULL position pointer");
  HQASSERT( filter , "filter NULL in FilterSetPos." ) ;

  uflptr = theIUnderFile( filter ) ;

  if ( isIInputFile( filter ) &&
       isIRewindable( filter ) &&
       (Hq32x2CompareInt32(position, 0) == 0) &&
       uflptr ) {
    result = 0 ;
    if ( isIFilter( uflptr )) {
      if ( (*theIMySetFilePos(uflptr))(uflptr, position) != EOF ) {
        if ( Hq32x2CompareInt32(position, 0) != 0 )
          return 0 ;
      } else {
        return EOF ;
      }
    }
  }

  if ( result == 0 ) {
    /* Underlying filter(s) have rewound - close and reinitialise */

    if ( isIOpenFile( filter )) {
      Bool presCST = isICST( filter ) ;
      ClearICSTFlag( filter ) ;
      ( void )( *theIMyCloseFile( filter ))( filter, CLOSE_EXPLICIT ) ;
      if ( presCST )
        SetICSTFlag( filter ) ;
      ClearIOpenFlag( filter ) ;
      SetIEofFlag( filter ) ;
    }

    if ( !( *theIMyInitFile( filter ))( filter ,
                                        & theIParamDict( filter ) ,
                                        NULL ))
      return ( *theIFileLastError( filter ))( filter ) ;

    SetIOpenFlag( filter ) ;
    ClearIEofFlag( filter ) ;
  }

  return result ;
}


int32 FilterPos( FILELIST *filter, Hq32x2* p )
{
  UNUSED_PARAM(FILELIST *, filter);
  UNUSED_PARAM(Hq32x2*, p);
  return EOF ;
}

int32 FilterBytes( FILELIST *filter, Hq32x2* n )
{
  UNUSED_PARAM(FILELIST *, filter);
  UNUSED_PARAM(Hq32x2*, n);
  return EOF ;
}

int32 FilterError( FILELIST *filter )
{
  UNUSED_PARAM( FILELIST *, filter );

  HQASSERT( isIFilter(filter), "FilterError called on file" ) ;

  return EOF ;
}

Bool FilterDecodeError( FILELIST *filter, int32* pb )
{
  UNUSED_PARAM( FILELIST *, filter );
  UNUSED_PARAM( int32*, pb );

  HQASSERT( isIFilter(filter), "FilterDecodeError called on file" ) ;

  return error_handler(UNREGISTERED);
}

Bool FilterEncodeError( FILELIST *filter )
{
  UNUSED_PARAM( FILELIST *, filter );

  HQASSERT( isIFilter(filter), "FilterEncodeError called on file" ) ;

  return error_handler(UNREGISTERED);
}

Bool FilterDecodeInfoError(FILELIST* filter,
                           imagefilter_match_t *match)
{
  UNUSED_PARAM(FILELIST*, filter);
  UNUSED_PARAM(imagefilter_match_t *, match);

  HQASSERT(isIFilter(filter), "FilterDecodeInfoError called on file");

  HQFAIL("The FilterDecodeInfo method is not defined, or has "
         "not been implemented, for this filter type");

  return error_handler(UNREGISTERED);
}

int32 FilterFlushBufError( int32 c, FILELIST *filter)
{
  UNUSED_PARAM( int32, c );
  UNUSED_PARAM( FILELIST *, filter );

  HQASSERT( isIFilter(filter), "FilterFlushBufError called on file" ) ;

  return EOF ;
}

#if defined( ASSERT_BUILD ) /* { */

/* Check that a chain of filters isn't cirular: pretty silly if it
 * happens, but nasty because it will force FilterFillBuf to go
 * infinite, amongst other things.
 */

static Bool FilterCircularRefCheck( FILELIST *filter )
{
  FILELIST *base = filter ;
  FILELIST *current = filter ;

  HQASSERT( filter, "filter null in FilterCircularRefCheck." ) ;

  while ( theIUnderFile( current ) &&
          ( theIUnderFilterId( current ) ==
            theIFilterId( theIUnderFile( current )))) {
    current = current->underlying_file ;

    if ( current == base )
      return FALSE ;
  }

  return TRUE ;
}

#endif /* } */

/**
 * UnGet a character back onto the filter. If the filter has been closed,
 * Then we reset it to open so getc still work. Also push the pointers
 * and the count around.
 */
uint8 *FilterUnGetc(FILELIST *filter)
{
  /* If we're at EOF, Bring us back from EOF */

  if (!isIOpenFile(filter)) {
    /* If this is TRUE, then we must have just got the last char from the filter */
    HQASSERT( theICount(filter) == 0 , "FilterUnGetc buffer should be empty" ) ;
    HQASSERT( theIFilterState( filter ) == FILTER_EOF_STATE , "FilterUnGetc in unknown state" ) ;
    ClearIEofFlag(filter);
    SetIOpenFlag(filter);
    theIFilterState( filter ) = FILTER_LASTCHAR_STATE ;
    /* Don't update theICount() so the next getc will find it at 0 and will call
     * FilterFillBuff() which will update filter state correctly for reading the last
     * character. */
    return theIPtr(filter) - 1;
  } else {
    /* march the pointer back, and kick the count */
    theICount(filter)++;
    return --theIPtr(filter);
  }
}

/** FilterFillBuff

   This fillbuff routine is used by all the filters, except RSD, to
   implement the close-on-last-char-read semantics and to handle
   consuming the EOF marker.

   In order to make sure we always consume the EOF marker we need to hang
   on to the last character read in each theIFilterDecode call so that we
   will always guarantee that we go back for more data and therefore read
   the EOF marker.

   Because we hang on to this last character, there are several cases in
   the FILTER_EMPTY_STATE state that we need to consider:

   LastChar     FilterDecode    Closefile       State machine           Return
A  0             0              TRUE            FILTER_EOF_STATE        EOF
B  1             0              TRUE            FILTER_EOF_STATE        last  char
C  0             n, n == 1      Move char to last char, go back round loop
D  0             n, n >= 2      FALSE           FILTER_EMPTY_STATE      first char
E  1             n              FALSE           FILTER_EMPTY_STATE      last  char
F  0            -n, n == 1      TRUE            FILTER_EOF_STATE        first char
G  0            -n, n >= 2      FALSE           FILTER_LASTCHAR_STATE   first char
H  1            -n              FALSE           FILTER_LASTCHAR_STATE   last  char
*/
int32 FilterFillBuff( register FILELIST *filter )
{
  int32 bytes     = 0 ;
  int32 ioerror_seen = FALSE;
  int32 last_byte = EOF ; /* Handle Return value for case A */
  int32 old_bytes = 0 ;
  int32 new_bytes = 1 ;   /* Use new NOT old because of loop for case C */
  FILELIST *uflptr ;

  /* Deliberate function call inside assert: no-op for release builds. */
  HQASSERT( filter , "filter NULL in FilterFillBuff." ) ;
  HQASSERT( isIFilter(filter) , "Not a filter." ) ;
  HQASSERT( FilterCircularRefCheck( filter ) ,
      "Filter chain is circular: danger of infinite loop!" ) ;
  HQASSERT( ! isIRSDFilter( filter ) , "RSD filter should not be in here!" ) ;

  switch ( theIFilterState( filter )) {

  case FILTER_INIT_STATE:
    new_bytes = 0 ;       /* Use new NOT old because of loop for case C */

    /* Fall through to the FILTER_EMPTY_STATE */

  case FILTER_EMPTY_STATE :
    uflptr = theIUnderFile( filter ) ;

    HQASSERT( uflptr , "uflptr NULL in FilterFillBuff." ) ;

    if ( isISkipLF( uflptr )) {
      int32 tmp_byte ;
      if (( tmp_byte = Getc( uflptr )) != EOF ) {
        if ( tmp_byte != LF ) {
          UnGetc( tmp_byte , uflptr ) ;
        }
      }
    }
    ClearICRFlags( uflptr ) ;

    do {
      old_bytes = new_bytes ;

      /* Sanity check: if the underlying file has been closed and the slot
       * re-used, that's an error. We can't use isIOpenFileFilterById here,
       * becase it's valid to try to read from a filter whose underlying file
       * has closed but the slot hasn't yet been recylced: the top filter
       * might have data squirreled away in its private state. (request 11688)
       * Don't do this test for external filters, because EOF detection
       * mechanism is different. External filters have a device attached; no
       * other filters have a device attached. See bug 30635.
       */

      if (( theIDeviceList( filter ) == NULL ) &&
          ( isIOpenFile( uflptr ) &&
            ( theFilterIdPart( theIUnderFilterId( filter )) !=
              theIFilterId( uflptr ))))
        return ioerror_handler( filter ) ;

      if ( ! ( *theIFilterDecode( filter ))( filter , & bytes )) {
        if ( old_bytes ) {
          corecontext_t *context = get_core_context() ;
          error_context_t *errcontext = context->error ;

          /* Put the last_char into the first byte ready to return (when necessary). */
          * ( theIBuffer( filter ) - 1 ) = theILastChar( filter ) ;
          /* don't return IOERROR if we still have a character to return */
          bytes = 0 ;
          new_bytes = 0 ;
          ioerror_seen = TRUE ;

          /* Cache the error before clearing it. */
          filter->error = CAST_SIGNED_TO_INT16( errcontext->new_error ) ;
          error_clear_context(errcontext) ;
          break ;
        }
        else {
          theIFilterState( filter ) = FILTER_ERR_STATE ;
          return ioerror_handler( filter ) ;
        }
      }

      /* Put the last_char into the first byte ready to return (when necessary). */
      if ( old_bytes )
        * ( theIBuffer( filter ) - 1 ) = theILastChar( filter ) ;

      /* We may have been in the INIT state; switch to the EMPTY state.
       * The init state allows decoders to initialize by looking at the state */
      /* Handle State machine change (cases D & E) */
      theIFilterState(filter) = FILTER_EMPTY_STATE ;

      new_bytes = ( bytes < 0 ) ? -bytes : bytes ;

      /* Always put the last read char into the last_char, so picked up by
         FILTER_LASTCHAR_STATE when necessary */
      theILastChar( filter ) = * ( theIBuffer( filter ) + new_bytes - 1 ) ;

      /* Loop around again when State machine is case C */
    } while ( old_bytes == 0 && bytes == 1 ) ;
    /* State machine for case C should never get here; no easy way to assert it. */

    /* Set the read size; used in SwReplaceFilterBytes */
    theIReadSize(filter) = old_bytes + new_bytes - 1 ;

    /* Set the count of bytes in the buffer. Note we must subtract 2 as we keep
       one byte back and we return one byte as well. */
    theICount( filter ) = old_bytes + new_bytes - 2 ;
    theIPtr  ( filter ) = theIBuffer( filter ) + 1 - old_bytes ;

    /* Handle State machine change (cases G & H) */
    if ( bytes < 0 &&
         new_bytes + old_bytes > 1 )
      theIFilterState( filter ) = FILTER_LASTCHAR_STATE ;

    /* Handle Return value for cases D, E, G & H */
    if ( new_bytes + old_bytes > 1 )
      return theIPtr(filter)[-1] ;

    /* Fall through to the FILTER_LASTCHAR_STATE */

  case FILTER_LASTCHAR_STATE:
    /* Handle Return value for cases B & F, or last character when in
       FILTER_LASTCHAR_STATE state) */
    if ( old_bytes + new_bytes == 1 )
      last_byte = theILastChar( filter ) ;

    /* Handle Closefile for cases A, B & F. */
    if (theIBuffer(filter)) {
      FILELIST *f = theIUnderFile(filter);
      HQASSERT( f , "No underlying file in FilterFillBuff" ) ;

      /* Filter doesn't get closed if an error was seen, since technically
       * there are still bytes available on the filter (and this seems to
       * match what Adobe does).
       */
      if (!ioerror_seen) {
        /* Close the Filter */
        (void)(*theIMyCloseFile(filter))(filter, CLOSE_IMPLICIT);
        theIBuffer(filter) = NULL;
      }

      /* Close any underlying file(if its at EOF)
       *  - Underlying (non-rewindable) filters should already be closed
       */
      if ( isIEof( f ) && isIOpenFileFilterById( theIUnderFilterId( filter ) , f )) {
        HQASSERT( ! ( isIFilter( f ) && ! isIRewindable( f )),
                  "Trying to close a non-rewindable filter in FilterFillBuff" ) ;
        (void)(*theIMyCloseFile( f ))( f, CLOSE_IMPLICIT ) ;
      }
    }
    if (!ioerror_seen) {
      /* Clear the Open flag; set the EOF flag */
      if ( !isIRewindable( filter ))
        ClearIOpenFlag(filter);
      SetIEofFlag(filter);

      /* Handle State machine change (cases A, B & F) */
      theIFilterState(filter) = FILTER_EOF_STATE ;
    }
    else {
      theIFilterState(filter) = FILTER_ERR_STATE;
    }

    /* The buffer contains only one character! */
    theIReadSize(filter) = old_bytes + new_bytes ;

    /* Correct return values. */
    theIPtr(filter) = & theILastChar( filter ) + old_bytes + new_bytes ;
    theICount(filter) = 0 ;

    /* return either EOF or the character */
    return last_byte ;

  case FILTER_EOF_STATE:
    /* We get here if we are reading past the EOF. All we have to do is
     * set/clear the flags, and stay in this state. */
    if ( ! isIRewindable( filter ))
      ClearIOpenFlag(filter);
    SetIEofFlag( filter ) ;
    return EOF ;

  case FILTER_ERR_STATE:
    return ioerror_handler( filter ) ;

  default:
    HQFAIL("FilterFillBuff: illegal filter state");
  } /* also return EOF to default arm of switch */
  return EOF ;
}

/** Check a filter arguments dictionary for CloseSource/CloseTarget */
int32 FilterCheckArgs( FILELIST *filter , OBJECT *args )
{
  OBJECT *thef ;

  HQASSERT( filter , "filter NULL in FilterCheckArgs." ) ;

  thef = fast_extract_hash_name(args, isIInputFile(filter) ? NAME_CloseSource : NAME_CloseTarget) ;

  ClearICSTFlag( filter ) ;

  if ( thef ) {
    if ( oType(*thef) != OBOOLEAN )
      return error_handler( TYPECHECK ) ;

    if ( oBool(*thef) )
      SetICSTFlag( filter ) ;
  }

  return TRUE ;
}

int32 FilterFlushFile( FILELIST *filter )
{
  HQASSERT( filter , "filter NULL in FilterFlushFile." ) ;

  if ( isIInputFile( filter )) {
    /* consume(flush) remaining filter data */
    while ( ! ( isIEof( filter ) || isIIOError( filter ))) {
      int32 fc = theICount( filter ) ;
      theIPtr( filter ) += fc ;
      theICount( filter ) -= fc ;
      if ( Getc( filter ) == EOF )
        return isIIOError( filter ) ? EOF : 0 ;
    }
    /* Note we don't need to call the close, as this happens in the filter state machine */
    return isIIOError( filter ) ? EOF : 0 ;
  }
  else {
    FILELIST *uflptr = theIUnderFile( filter ) ;

    HQASSERT( uflptr , "uflptr NULL in FilterFlushFile." ) ;
    HQASSERT( theIFilterEncode( filter ) ,
              "no encode routine for FilterFlushFile." ) ;

    if ( ! ( *theIFilterEncode( filter ))( filter ))
      return EOF ;

    return ( *theIMyFlushFile( uflptr ))( uflptr ) ;
  }
}

int32 FilterFlushBuff( int32 c, FILELIST *filter )
{
  HQASSERT( filter , "filter NULL in FilterFlushBuff." ) ;
  HQASSERT( theIFilterEncode( filter ) ,
            "Null encode routine for FilterFlushBuff." ) ;

  *theIPtr( filter ) = ( uint8 )c ;

  if ( ! ( *theIFilterEncode( filter ))( filter ))
    return EOF ;

  return 0 ;
}

void FilterDispose( FILELIST *flptr )
{
  UNUSED_PARAM(FILELIST *, flptr);
}

int32 FilterNoOp( FILELIST *flptr )
{
  UNUSED_PARAM(FILELIST *, flptr);

  return 0 ;
}

int32 FilterCloseNoOp( FILELIST *flptr, int32 flag )
{
  UNUSED_PARAM(FILELIST *, flptr);
  UNUSED_PARAM(int32, flag);

  return 0 ;
}

int32 FilterBytesAvailNoOp( FILELIST *flptr, Hq32x2* n )
{
  UNUSED_PARAM(FILELIST *, flptr);

  Hq32x2FromUint32(n, 0u);
  return 0 ;
}

int32 FilterFilePosNoOp( FILELIST *flptr, Hq32x2* n )
{
  UNUSED_PARAM(FILELIST *, flptr);

  Hq32x2FromUint32(n, 0u);
  return 0 ;
}

int32 FilterSetFilePosNoOp(FILELIST *flptr, const Hq32x2 *n)
{
  UNUSED_PARAM(FILELIST *, flptr);
  UNUSED_PARAM(const Hq32x2 *, n);

  return 0 ;
}

/** Generic close function, used for most "regular" filters. Sets
 * the closing flag to denote the next encode should include
 * bytes arising from the filter state as well as bytes from the
 * buffer (e.g. EOD code), then does a flush and a dispose.
 */
int32 FilterCloseFile( FILELIST *filter, int32 flag )
{
  int32 result = 0 ;
  FILELIST *uflptr ;

  UNUSED_PARAM(int32, flag);

  HQASSERT( filter , "filter NULL in FilterCloseFile." ) ;

  SetIClosingFlag( filter ) ;

  if ( isIOutputFile( filter ))
    result = ( *theIMyFlushFile( filter ))( filter ) ;

  uflptr = theIUnderFile( filter ) ;

  if ( uflptr && isICST( filter ) &&
       isIOpenFileFilterById( theIUnderFilterId( filter ) , uflptr )) {
    /* While this filter may be being closed implicitly, the closing
     * of the source is explicit if and only if the filter is not
     * rewindable. For RSDs which underly PS filters, the close is
     * explicit so that we don't have lots of dangling RSD buffers
     * hanging around until the next restore, whereas an RSD
     * underlying a PDF filter stays around in its dormant state so
     * things work properly when the filter chain gets rewound on the
     * next xref cache lookup. */

    if ( ! isIRewindable( filter ))
      flag = CLOSE_EXPLICIT ;

    if (( *theIMyCloseFile( uflptr ))( uflptr, flag ) == EOF )
      result = EOF ;
  }

  (*theIMyDisposeFile(filter))(filter) ;

  ClearIClosingFlag( filter ) ;
  SetIEofFlag( filter ) ;
  if ( ! isIRewindable( filter ))
    ClearIOpenFlag( filter ) ;

  if ( result == EOF )
    ( void )error_handler( IOERROR ) ;

  return result ;
}

/** Generic last error function: clean up and raise an error.
 * Throws an IOERROR to catch cases where an error hasn't already been thrown.
 * When an error has already been thrown, that will be handled in preference
 * to the IOERROR thrown here. In particular there are several cases where a
 * filter can raise a VMERROR during IO and it's useful to report it as such.
 */
Bool FilterLastError( FILELIST *filter )
{
  int32 errnum ;

  HQASSERT( filter , "filter NULL in FilterLastError." ) ;

  /* For cases where no explicit error was set up, just raise an IOERROR. */
  if (filter->error == NOT_AN_ERROR )
    errnum = IOERROR ;
  else
    errnum = filter->error ;

  filter->error = CAST_SIGNED_TO_INT16( NOT_AN_ERROR ) ;

  /* Note this is called unconditionally: the dispose routine
   * must be well-behaved and cope with partially constructed
   * filters.
   */

  (*theIMyDisposeFile(filter))(filter) ;

  ClearIOpenFlag( filter ) ;
  SetIEofFlag( filter ) ;

  return error_handler( errnum ) ;
}


/* ----------------------------------------------------------------------------
   function:            ioerror_handler   author:              Luke Tunmer
   creation date:       26-Jul-1991       last modification:   ##-###-####
   arguments:     flptr
   description:

   This routine sets the ioerror flag in the file structure *flptr and
   sets the error state if it's a filter before returning with EOF.
---------------------------------------------------------------------------- */
int32 ioerror_handler( FILELIST *flptr )
{
  SetIIOErrFlag( flptr ) ;
  if ( isIFilter( flptr ))
    theIFilterState( flptr ) = FILTER_ERR_STATE ;
  return EOF ;
}

/*---------------------------------------------------------------------------*/
/** SwReplaceFilterBytes is used to backtrack the underlying file ptr over
 * unused bytes obtained by SwReadFilterBytes.
 * Returns 0 if OK, or EOF if consistency check error.
 */
int32 RIPCALL SwReplaceFilterBytes( DEVICELIST *dev, int32 len )
{
  FILELIST *flptr ;
  FILELIST *uflptr ;

  flptr = ( FILELIST *)theINextDev( dev ) ;
  uflptr = theIUnderFile( flptr ) ;

  /* consistency checks */

  if ( len == 0 )
    return 0 ;

  if ( ! uflptr || ! isIOpenFile( flptr ))
    return ioerror_handler( flptr ) ;

  if ( isIFilter( uflptr ) &&
       theIFilterId( uflptr ) != theIUnderFilterId( flptr ))
    return ioerror_handler( flptr ) ;

  if (( len < 0 ) || ( len > theIReadSize( uflptr )))
    return ioerror_handler( flptr ) ;

  /* If a filter has hit EOF and closed, we can back up one byte only.
   * Not applicable to files, since EOF is only reached after reading 0
   * bytes. The basic assumption is that the byte(s) being replaced has/
   * have just been read. A filter at EOF can have a single byte replaced,
   * but it should be in a state where the last read was of a single byte,
   * and that it has reached the EOF state via the last char state.
   */

  if ( isIEof( uflptr ))
  {
    if ( ! isIFilter( uflptr ) ||
         ( theIFilterState( uflptr ) != FILTER_EOF_STATE ) ||
         ( len != 1 ))
      return ioerror_handler( flptr ) ;
    HQASSERT( theICount( uflptr ) == 0 ,
              "Count should be zero here in SwReplaceFilterBytes." ) ;
    SetIOpenFlag( uflptr ) ;
    ClearIEofFlag( uflptr ) ;
    theIFilterState( uflptr ) = FILTER_LASTCHAR_STATE ;
  }
  else {
    theIPtr( uflptr ) -= len ;
    theICount( uflptr ) += len ;
  }

  return 0 ;
}

/** SwReadFilterBytes()
 * get data from underlying file by setting *ret_buf to point to next byte
 * in the buffer. Reads some more data into buffer if empty and not EOF.
 * Data in buffer is marked as consumed.
 * Returns
 * >0   number of bytes in the buffer
 * 0    EOF
 * -1   IO error
 */
int32 RIPCALL SwReadFilterBytes( DEVICELIST *dev, uint8 **ret_buff )
{
  FILELIST *flptr, *uflptr;
  int32    c;

  flptr = ( FILELIST *)theINextDev( dev );
  uflptr = theIUnderFile( flptr );

  /* consistency checks */
  if ( !uflptr  ) return ioerror_handler(flptr);

  if ( isIFilter( uflptr ) &&
       theIFilterId( uflptr ) != theIUnderFilterId( flptr ))
    return ioerror_handler (flptr);

  if ( isIEof( uflptr ) ) return 0;

  if (! GetFileBuff( uflptr, MAXINT32, ret_buff, &c)) {
    if ( isIIOError( uflptr ) )
    {
      HQTRACE( debug_filters , ( "Getc failed" )) ;

      return ioerror_handler(flptr);
    }
    else
    {
      HQTRACE( debug_filters , ( "EOF" )) ;

      return 0;
    }
  }

  HQTRACE( debug_filters , ( "swreadfilterbytes %d", c )) ;

  return c;
}

int32 RIPCALL SwWriteFilterBytes( DEVICELIST *dev, uint8 *buff, int32 len )
/* dump buffer to underlying file */
{
  FILELIST *flptr, *uflptr;
  uint8    *ch = buff;

  HQTRACE( debug_filters , ( "swwritefilterbytes is getting there" )) ;

  flptr = ( FILELIST *)theINextDev( dev );
  uflptr = theIUnderFile( flptr );

  /* consistency checks */
  if ( !uflptr ) return ioerror_handler(flptr);

  if ( isIFilter( uflptr ) &&
       theIFilterId( uflptr ) != theIUnderFilterId( flptr ))
    return ioerror_handler (flptr);

  if (uflptr->flags & LB_FLAG) {
     while ( ch < buff + len ) {
        if ( Putc( *ch, uflptr ) == EOF ) {
          HQTRACE( debug_filters , ( "if Putc failed " )) ;

          return ioerror_handler(flptr);
        }
        ch++;
     }
  } else {  /* use HqMemCpy if not line-buffered */
    int32 scpyLen, countLen;
#define LOCALBUFFERSIZE 1024
    /* LOCALBUFFERSIZE chosen carefully to match the temporary buffer allocated for
       custom encode filters, so generally we'll always flush in one go */
    uint8 localBuffer [LOCALBUFFERSIZE];
    int32 ubuffsize = uflptr->buffersize ;

    if ( ubuffsize == 0 ) {
      HQASSERT(HqMemCmp(uflptr->clist, uflptr->len,
                        NAME_AND_LENGTH("%procedureEncode")) == 0,
               "Not a procedure target, but no underlying buffer") ;

      /* It is a procedure filter, and that doesn't have a buffer associated
         with uflptr. We could do it using the line buffered method above,
         but that means passing the data to the proicedure one character at a
         time (as PutC always flushes with a zero size buffer). Instead use a
         local buffer temporarily. Note this could be unsafe if the
         PostScript tries to hang onto the string, but not really less so
         than passing allocated file buffers in as PostScript strings which
         we already do regularly */

      uflptr->ptr = uflptr->buffer = localBuffer;
      uflptr->buffersize = LOCALBUFFERSIZE;
      uflptr->count = 0;
    } else {
      HQASSERT (uflptr->buffer, "custom filter uflptr has no buffer");
      HQASSERT (uflptr->buffersize > 0, "custom filter buffer size invalid");

      HQASSERT(HqMemCmp(uflptr->clist, uflptr->len,
                        NAME_AND_LENGTH("%procedureEncode")) != 0,
               "Underlying procedure target has buffer") ;
    }

    countLen = len;
    while (countLen) {
      scpyLen = (uflptr->buffersize - uflptr->count) - 1; /* last for Putc */
      if (countLen < scpyLen)
        scpyLen = countLen;
      HqMemCpy(uflptr->ptr, ch, scpyLen); /* copy one big chunk */
      countLen -= scpyLen;
      ch += scpyLen;
      uflptr->ptr += scpyLen;
      uflptr->count += scpyLen;
      if (countLen) {
        /* put the last, which may flush */
        if ( Putc( *ch, uflptr ) == EOF ) {
          HQTRACE( debug_filters , ( "Else Putc failed " )) ;

          if ( ubuffsize == 0 ) {
            uflptr->ptr = uflptr->buffer = NULL;
            uflptr->buffersize = 0;
            uflptr->count = 0;
          }

          return ioerror_handler(flptr);
        }
        countLen--;
        ch++;
      }
    }

    if ( ubuffsize == 0 ) {
      /* Get rid of any remaining data and reference to the temporary buffer */
      if (uflptr->count > 0) {
        uflptr->buffersize = uflptr->count;
        uflptr->ptr--;
        (void)(*theIFlushBuffer(uflptr))(uflptr->buffer[uflptr->count - 1], uflptr) ;
      }
      uflptr->ptr = uflptr->buffer = NULL;
      uflptr->buffersize = 0;
      uflptr->count = 0;
    }
  }

  HQTRACE( debug_filters , ( "swwritten %d chars", len )) ;

  return len ;
}

/** SwSeekFilterBytes()
 * Seek on the underlying file ptr, if seekable.
 * Returns 0 if OK, -1 if consistency check error or IO error.
 */
int32 RIPCALL SwSeekFilterBytes( DEVICELIST *dev, int32 offset )
{
  FILELIST *flptr, *uflptr;
  Hq32x2   file_offset ;

  flptr = ( FILELIST *)theINextDev( dev );
  uflptr = theIUnderFile( flptr );

 /* consistency checks */
  if ( !uflptr  ) return ioerror_handler(flptr);

  if ( isIFilter( uflptr ) &&
       theIFilterId( uflptr ) != theIUnderFilterId( flptr ))
    return ioerror_handler (flptr);

  /* do the equivalent of setfileposition on uflptr */
  if ( !( isIRealFile( uflptr ) || isIRSDFilter( uflptr ) )
       || ! isIOpenFile( uflptr ))
  {
    (void)error_handler( IOERROR );
    return EOF;
  }

  if ( isIOutputFile( uflptr )) {
    if ((*theIMyFlushFile( uflptr ))( uflptr ) == EOF )
    {
      (void)(*theIFileLastError( uflptr ))( uflptr );
      return EOF;
    }
  } else {
    if ((*theIMyResetFile( uflptr ))( uflptr ) == EOF )
    {
      (void)(*theIFileLastError( uflptr ))( uflptr );
      return EOF;
    }
  }

  Hq32x2FromInt32(&file_offset, offset) ;
  if ((*theIMySetFilePos( uflptr ))( uflptr, &file_offset ) == EOF )
  {
    (void)(*theIFileLastError( uflptr ))( uflptr );
    return EOF;
  }

  return 0;
}

void init_C_globals_filtops(void)
{
#if defined( ASSERT_BUILD )
  debug_filters = FALSE ;
#endif
  Filter_List = NULL ;
}

/* Log stripped */
