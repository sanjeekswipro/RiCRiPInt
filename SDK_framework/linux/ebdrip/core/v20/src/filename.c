/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:filename.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS filename utility functions
 */

#include "core.h"
#include "swdevice.h"
#include "swerrors.h"
#include "objects.h"
#include "hqmemcmp.h"
#include "namedef_.h"
#include "mm.h"
#include "mmcompat.h"
#include "hqmemcpy.h"
#include "devices.h"

#include "control.h"
#include "psvm.h"
#include "stacks.h"
#include "dicthash.h"
#include "params.h"
#include "fileio.h"
#include "miscops.h"
#include "fileops.h"
#include "bitblts.h"
#include "display.h"
#include "matrix.h"
#include "graphics.h"
#include "swmemory.h"
#include "dictops.h"
#include "filename.h"

/* Argument block structure used to pass data to the collect_names()
   callback passed to walk_dictionary()
*/

typedef struct objlist {
  OBJECT *obkeys;
  struct objlist *next;
} OLIST;

typedef struct collect_names_params {
  OBJECT *pattern ;
  int32 class ;
  int32 status ;
  SLIST **Head_flist ;
  OLIST **Head_olist;
} collect_names_Params ;

static Bool add_if_not_there(SLIST **head, uint8 *name, int32 length);
static Bool add_objects(OLIST **head, OBJECT *newkey);
static Bool collect_names(OBJECT *thekey, OBJECT *theval, void *argBlockPtr);
static Bool enumerate_list( SLIST *Head_flist, OLIST *Head_olist, OBJECT *proc,
                            OBJECT *scratch, int32 *anexit );
static Bool test_non_text_objects(OBJECT *key, OBJECT *proc, int32 *anexit);
static Bool test_resource( uint8 *clist , int32 len ,
                           OBJECT *proc, OBJECT *scratch, int32 *anexit );
static Bool disk_resourceforall_callback(ps_context_t *pscontext);
static Bool disk_resourceforall(ps_context_t *pscontext, OBJECT *resourcefname,
                                OBJECT *impl_dict);



/* ----------------------------------------------------------------------------
   function:            match_files_on_device author:              Luke Tunmer
   creation date:       11-Oct-1991           last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool match_files_on_device(
  DEVICELIST *dev ,     /* perform filenameforall on this device */
  uint8 *file_pattern , /* matched with this pattern */
  OBJECT *scratch  ,    /* the scratch string object */
  int32 stat ,          /* JUSTFILE or DEVICEANDFILE */
  SLIST **flist )
{
  int32 length ;
  int32 devname_length ;
  uint8 *clist, *cliststart ;
  FILEENTRY entry ;
  void *handle ;
  int32 scratch_length ;/* length of the scratch string */
  SLIST *last_flist;

  if ( NULL == ( handle = (*theIStartList( dev ))( dev , file_pattern ))) {
    /*
     * If we cannot start enumeration of files on a device do not treat
     * this as an error. It probably just means the device is currently
     * not present, and its absence should be silently ignored. e.g. If we
     * are seraching the CD/DVD drive and there is no CD currently inserted.
     * But once the enumeration has successfully started, IOErrrors can be
     * propagated back up the call chain.
     */
    return TRUE;
  }

  devname_length = 0 ;
  if ( stat == DEVICEANDFILE )
    devname_length = strlen_int32( (char *)theIDevName( dev )) + 2 ;
  scratch_length = theLen(* scratch ) ;

  for (;;) {

    switch ((*theINextList( dev ))( dev , &handle , file_pattern , &entry )) {
    case FileNameMatch  :

      length = devname_length + theDFileNameLen( entry ) ;
      if ( length > scratch_length ) {
        (void) (*theIEndList( dev ))( dev , handle ) ;
        return error_handler( LIMITCHECK ) ;
      }

      cliststart = clist = oString(*scratch) ;
      if ( stat == DEVICEANDFILE ) {
        *clist++ = (uint8) '%' ;
        HqMemCpy( clist , theIDevName( dev ) , devname_length - 2 ) ;
        clist += devname_length - 2 ;
        *clist++ = (uint8) '%' ;
      }
      HqMemCpy( clist , theDFileName( entry ) , theDFileNameLen( entry )) ;

      /* add filename to list */
      last_flist = *flist;
      if ( NULL == (*flist = mm_alloc(mm_pool_temp,
                                      sizeof(SLIST),
                                      MM_ALLOC_CLASS_FILENAME_LIST))) {
        *flist = last_flist;
        (void) (*theIEndList( dev ))( dev , handle ) ;
        return error_handler(VMERROR);
      }
      if ( NULL == ((*flist)->text = mm_alloc_with_header(mm_pool_temp,
                                                          length+1,
                                                          MM_ALLOC_CLASS_FILENAME))) {
        mm_free(mm_pool_temp,(mm_addr_t)*flist, sizeof(SLIST) );
        *flist = last_flist;
        (void) (*theIEndList( dev ))( dev , handle ) ;
        return error_handler(VMERROR);
      }
      HqMemCpy ((*flist)->text, cliststart, length);
      *((*flist)->text+length) = '\0';
      (*flist)->next = last_flist;

      break ;

    case FileNameNoMatch :
      if ( (*theIEndList( dev ))( dev , handle ) < 0 )
        return device_error_handler( dev ) ;

      return TRUE ;

    case FileNameIOError :
      (void) (*theIEndList( dev ))( dev , handle ) ;
      return device_error_handler( dev ) ;

    case FileNameRangeCheck :
      (void) (*theIEndList( dev ))( dev , handle ) ;
      return error_handler( LIMITCHECK ) ;

    }
  }
}



/* ----------------------------------------------------------------------------
   function:            match_on_device   author:              Luke Tunmer
   creation date:       09-Feb-1992       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
Bool match_on_device(DEVICELIST *dev, SLIST **flist)
{
  int32 length ;
  uint8 *clist ;
  SLIST *last_flist;

  length = strlen_int32( (char *)theIDevName( dev ) ) ;
  clist = mm_alloc_with_header(mm_pool_temp,length + 3,MM_ALLOC_CLASS_FILENAME) ;
    /* i.e. plus two '%'s and a null */
  if (! clist)
    return error_handler(VMERROR);

  clist [0] = '%' ;
  HqMemCpy( clist + 1 , theIDevName( dev ) , length ) ;
  clist [length + 1] = '%' ;
  clist [length + 2] = '\0' ;

  /* add devicename to list */
  last_flist = *flist;
  if ( NULL == (*flist = (SLIST *)mm_alloc(mm_pool_temp,sizeof(SLIST),
                                           MM_ALLOC_CLASS_FILENAME_LIST))) {
    mm_free_with_header(mm_pool_temp, clist);
    return error_handler(VMERROR);
  }
  (*flist)->text = clist ;
  (*flist)->next = last_flist;
  return TRUE ;
}

/* to append an object onto a list. peng-24/03/94 */

static Bool add_objects(OLIST **head, OBJECT *newkey)
{
  OLIST *obtmp;

  obtmp = (OLIST*)mm_alloc(mm_pool_temp, sizeof( OLIST ),
                           MM_ALLOC_CLASS_FILENAME_OLIST);
  if (! obtmp)
    return FALSE;
  obtmp->obkeys = newkey;
  obtmp->next = *head;
  *head = obtmp;
  return TRUE;
}


static Bool add_if_not_there( SLIST **head, uint8 *name, int32 length )
{
  SLIST *flist;
  uint8 *fname;

  for ( flist = *head ; flist != NULL ; flist = flist->next ) {
    if ( HqMemCmp( flist->text, strlen_int32((char*)flist->text), name, length ) == 0 ) {
      return TRUE;    /* already got this name */
    }
  }
  /* add to head of list */
  if ( NULL == (flist = (SLIST*)mm_alloc(mm_pool_temp, sizeof( SLIST ),
                                              MM_ALLOC_CLASS_FILENAME_LIST)))
    return FALSE;
  if ( NULL == (fname = (uint8*)mm_alloc_with_header(mm_pool_temp,length + 1,
                                                     MM_ALLOC_CLASS_FILENAME))) {
    mm_free( mm_pool_temp, flist, sizeof( SLIST ));
    return FALSE;
  }
  HqMemCpy( fname, name, length );
  fname[length] = '\0';

  flist->text = fname;
  flist->next = *head;
  *head = flist;
  return TRUE;
}


static void free_olist( OLIST *olist )
{
  OLIST *next_olist;

  while (olist) {
    next_olist = olist->next;
    mm_free(mm_pool_temp,(mm_addr_t)olist, sizeof(OLIST) );
    olist=next_olist;
  }
}


static Bool collect_names(OBJECT *thekey, OBJECT *theval, void *argBlockPtr)
{                         /* save names in a list if they match the pattern */

  collect_names_Params *args = argBlockPtr ;
  OBJECT *pattern = args->pattern ;
  int32 class = args->class ;
  int32 status = args->status ;
  SLIST **Head_flist = args->Head_flist ;
  OLIST **Head_olist = args->Head_olist ;

  int32 len, stat;
  uint8 *pat_string ;
  int32 pat_length ;
  int32 quick_match ;
  uint8 *new_name;

  pat_string = oString(*pattern) ;
  pat_length = theLen(* pattern ) ;
  quick_match = FALSE ;
  len = 0;                      /* init to keep compiler quiet */
  new_name = NULL;     /* init to keep compiler quiet */
  if ( pat_length == 1 && *pat_string == (uint8)'*' )
    quick_match = TRUE ;

  if ( oType(*thekey) == OSTRING ) {
    new_name = oString(*thekey) ;
    len = theLen(* thekey ) ;
  } else if ( oType(*thekey) == ONAME ) {
    new_name = theICList(oName(*thekey)) ;
    len = theINLen(oName(*thekey)) ;
  } else if ( ! quick_match ) {
    return TRUE; /* there is no match */
  }

  if ( class == 2 ) {
    if ( oType(*theval) != OARRAY )
      return error_handler( UNREGISTERED ) ;
    theval = oArray(*theval) ;
    if ( oType(*theval) != OINTEGER )
      return error_handler( UNREGISTERED ) ;
    stat = oInteger(*theval) ;
  } else {
    stat = status ;
  }

  /* now check the status of the resource */
  if ( status == stat ) {
    /* now check that the pattern matches */
    if ( oType(*thekey) == OSTRING || oType(*thekey) == ONAME ) { /* text-typed keys*/
      if ( quick_match || SwLengthPatternMatch( pat_string , pat_length , new_name , len )) {
        if (! add_if_not_there( Head_flist, new_name, len )) {
          free_flist(*Head_flist); /* free the space already claimed. */
          return error_handler( VMERROR );
        }
      }
    } else { /* non-texted keys only add to list when quick_match */
      if (quick_match) {
        if (! add_objects( Head_olist, thekey)) {
          free_olist(*Head_olist); /* free the space already claimed. */
          return error_handler( VMERROR );
        }
      }
    }
  }
  return TRUE;
}

static Bool enumerate_list(SLIST *Head_flist,OLIST *Head_olist, OBJECT *proc,
                           OBJECT *scratch, Bool *anexit)
{
  SLIST *flist;
  OLIST *olist;
  int32 t;

#ifdef debug_filename
  monitorf( "Enumerating" );
#endif
  if (Head_flist != NULL) /* the keys are texts--string or name*/
  {
    for ( flist = Head_flist ; flist != NULL ; flist = flist->next ) {
      t = test_resource( flist->text, strlen_int32( (char*)flist->text ),
                         proc, scratch, anexit );
      if ( !t ) {
        free_flist( Head_flist ) ;
        return FALSE ;
      }
#ifdef debug_filename
      monitorf( " %.*s", strlen( (char*)flist->text ), flist->text );
#endif
    }
#ifdef debug_filename
    monitorf( "\n" );
#endif
    free_flist( Head_flist );

  }
  if (Head_olist != NULL ) {  /* for non-text keys */
    for (olist = Head_olist; olist != NULL ; olist = olist->next) {
      t = test_non_text_objects(olist->obkeys, proc, anexit);
      if ( !t ) {
        free_olist( Head_olist);
        return FALSE;
      }
    }
    free_olist( Head_olist) ;
  }
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            genericresourceforall_ author:              Luke Tunmer
   creation date:       10-Feb-1992            last modification:   ##-###-####
   arguments:
   description:

   Do the hard work for resourceforall. See resource.pss for the PostScript
   that calls this operator. On top of the stack there should be an integer:
   0 - Category resourceforall
   1 - Implicit resourceforall
   2 - Regular resourceforall

---------------------------------------------------------------------------- */

/* the following global vars are used in disk_resourceforall and its callback
   routine, they are set in GenericResourceforall_
*/

static SLIST *Global_Head_flist ;
static uint8 *Global_Pattern_String ;
static int32 Global_Pattern_Length ;
static int32 Global_Resource_Length ;

Bool GenericResourceforall_(ps_context_t *pscontext)
{
  corecontext_t *context = ps_core_context(pscontext);
  OBJECT *pattern , *proc , *scratch , *impl_dict ;
  OBJECT *theo ;
  OBJECT *resourcename ;
  OBJECT *ldict , *gdict ;
  OBJECT *resourcefname ;
  SLIST *Head_flist ;
  OLIST *Head_olist ;
  int32 class;
  Bool anexit ;
  collect_names_Params name_param;

  if ( theStackSize( operandstack ) < 3 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  resourcefname = NULL; /* init to keep compiler quiet */
  ldict = NULL; /* init to keep compiler quiet */
  gdict = NULL; /* init to keep compiler quiet */

  if ( oType(*theo) != OINTEGER )
    return error_handler( TYPECHECK ) ;
  class = oInteger(*theo) ;
  pop( &operandstack ) ;

  HQASSERT( class >= 0 && class <= 2, "resourceforall class out of range" );

  scratch = theTop( operandstack ) ;
  if ( oType(*scratch) != OSTRING )
    return error_handler( TYPECHECK ) ;
  if ( ! oCanWrite(*scratch) && !object_access_override(scratch) )
    return error_handler( INVALIDACCESS ) ;

  proc = stackindex(1, &operandstack) ;
  if ( ! oCanExec(*proc) && !object_access_override(proc) )
    return error_handler( INVALIDACCESS ) ;

  pattern = stackindex(2, &operandstack) ;
  if ( oType(*pattern) != OSTRING )
    return error_handler( TYPECHECK ) ;
  if ( ! oCanRead(*pattern) && !object_access_override(pattern) )
    return error_handler( INVALIDACCESS ) ;

  /* get the implementation dictionary */
  impl_dict = theTop( dictstack ) ;

  /* find the resource name */
  if ( NULL == (resourcename = fast_extract_hash_name(impl_dict, NAME_Category)) )
    return error_handler( UNREGISTERED ) ;

  if ( class == 0 ) { /* a category resourceforall */
    /* get the ImplementationDirectory */
    if ( NULL == (gdict = fast_extract_hash_name(&internaldict, NAME_ImplementationDirectory)) )
      return error_handler( UNREGISTERED ) ;
  }

  if ( class == 2 ) { /* regular resource */
    /* get the ResourceFileName procedure */
    resourcefname = fast_extract_hash_name(impl_dict, NAME_ResourceFileName) ;
  }

  if ( class != 0 ) { /* implicit and regular resource */
    /* find the global resource directory */
    if ( NULL == (gdict = fast_extract_hash_name(&internaldict, NAME_GlobalResourceDirectory)) )
      return error_handler( UNREGISTERED ) ;

    /* get the global instances dictionary */
    if ( NULL == ( gdict = extract_hash( gdict , resourcename )))
      return error_handler( UNDEFINED ) ;
  }

  if ( class == 2 ) { /* regular resource */
    /* find the local resource directory */
    if ( NULL == (ldict = fast_extract_hash_name(&internaldict, NAME_LocalResourceDirectory)) )
      return error_handler( UNREGISTERED ) ;

    /* get the local instances dictionary */
    ldict = extract_hash( ldict , resourcename ) ;
  }

  if ( ! push4( pattern , proc , scratch , impl_dict , &temporarystack ))
    return FALSE ;
  impl_dict = theTop( temporarystack ) ;
  scratch = stackindex(1, &temporarystack) ;
  proc = stackindex(2, &temporarystack) ;
  pattern = stackindex(3, &temporarystack) ;

  npop( 3 , &operandstack ) ;

  Head_flist = NULL;
  Head_olist = NULL;

  /* do all the resources with a status of 0 */

  /* set up collect_names_Params structure */
  name_param.pattern = pattern;
  name_param.class = class;
  name_param.status = 0;
  name_param.Head_flist = &Head_flist;
  name_param.Head_olist = &Head_olist;

  if ( ! walk_dictionary( gdict , collect_names , &name_param )) {
    npop( 4 , &temporarystack ) ;
    return FALSE ;
  }

  if ( class == 2) {
    if ( ldict && !context->glallocmode ) {
      /* in local mode */

      if ( ! walk_dictionary( ldict , collect_names , &name_param )) {
        npop( 4 , &temporarystack ) ;
        return FALSE ;
      }
    }

    /* do all the resources with status 1 */
    name_param.status = 1;
    if ( ! walk_dictionary( gdict , collect_names , &name_param)) {
      npop( 4 , &temporarystack ) ;
      return FALSE ;
    }

    if ( ldict && !context->glallocmode ) {
      /* in local mode */

      if ( ! walk_dictionary( ldict , collect_names , &name_param)) {
        npop( 4 , &temporarystack ) ;
        return FALSE ;
      }
    }
  }

  if ( class == 2) {
    /* do all resources on disk */
    if ( resourcefname ) {
      /* First get the filename from the ResourceFileName procedure */
      /* set up global vars needed in disk_resourceforall_callback() */

      Global_Head_flist = Head_flist ;
      Global_Pattern_String = oString(*pattern);
      Global_Pattern_Length = theLen(* pattern );
      if ( ! disk_resourceforall(pscontext, resourcefname, impl_dict) ) {
        npop( 4 , &temporarystack ) ;
        return FALSE ;
      }
      Head_flist = Global_Head_flist ;
    }
  }
  if ( ! end_(pscontext) ) {
    HQFAIL( "end should always succeed here." ) ;
  }
  anexit = TRUE ;
  if ( !enumerate_list( Head_flist, Head_olist, proc, scratch, &anexit ) ) {
    if ( !anexit ) {
      npop( 4 , &temporarystack ) ;
      if ( ! begininternal( impl_dict ) ) {
        HQFAIL( "begin should always succeed here." ) ;
      }
      return FALSE ;
    }
    else
      error_clear_context(context->error) ;
  }
  npop( 4 , &temporarystack ) ;
  if ( ! begininternal( impl_dict ) ) {
    HQFAIL( "begin should always succeed here." ) ;
  }
  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            test_resource     author:              Luke Tunmer
   creation date:       10-Feb-1992       last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
static Bool test_resource(uint8 *clist , int32 len ,
                          OBJECT *proc, OBJECT *scratch, Bool *anexit)
{
  int32 scratch_length = (int32)theLen(* scratch ) ;

  if ( len > scratch_length )
    return error_handler( LIMITCHECK ) ;

  HqMemCpy( oString(*scratch) , clist , len ) ;
  theLen(* scratch ) = (uint16)len ;
  if ( ! push( scratch , &operandstack ))
    return FALSE ;

  if ( ! push( proc , &executionstack )) {
    pop( &operandstack ) ;
    return FALSE ;
  }

  if ( ! interpreter( 1 , anexit ))
    return FALSE;

  /* restore the scratch string length */
  theLen(* scratch ) = (uint16)scratch_length ;

  return TRUE ;
}

/* equivalent to test_resource but works on non-text keys. peng-25/03/94 */

static Bool test_non_text_objects(OBJECT *key, OBJECT *proc, int32 *anexit)
{
  if ( ! push(key,  &operandstack ))
    return FALSE;
  if ( ! push( proc , &executionstack )) {
    pop( &operandstack ) ;
    return FALSE ;
  }
  if ( ! interpreter( 1 , anexit ))
    return FALSE;
  return TRUE;
}



static OPERATOR disk_resourceforall_op ;


/* ----------------------------------------------------------------------------
   function:            disk_resourceforall author:              Luke Tunmer
   creation date:       11-Feb-1992         last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
static Bool disk_resourceforall(ps_context_t *pscontext,
                                OBJECT *resourcefname, OBJECT *impl_dict)
{
  uint8 scratchstring[ LONGESTFILENAME ] ;
  OBJECT stro = OBJECT_NOTVM_NOTHING ;
  OBJECT pato = OBJECT_NOTVM_STRING("*") ;
  OBJECT proco = OBJECT_NOTVM_NOTHING ;
  OBJECT *theo ;
  uint16 pattern_length ;

  theTags( stro ) = OSTRING | UNLIMITED | LITERAL ;
  SETGLOBJECTTO(stro, FALSE) ;
  theLen( stro ) = LONGESTFILENAME ;
  oString(stro) = scratchstring ;

  if ( ! push( &pato , &operandstack ))
    return FALSE ;

  if ( ! push( &stro , &operandstack )) {
    pop( &operandstack ) ;
    return FALSE ;
  }

  if (!push( impl_dict , &operandstack ) ||
      !begin_(pscontext))
    return FALSE ;

  if ( ! push( resourcefname , &executionstack )) {
    npop( 2 , &operandstack ) ;
    return FALSE ;
  }

  if ( ! interpreter(1, NULL) ) {
    /* We've already handled the interpreter error, we don't want to raise
       another error on cleanup. */
    (void)end_(pscontext) ;
    return FALSE ;
  }

  if ( ! end_(pscontext))
    return error_handler( UNREGISTERED ) ;

  /* We should now have the completed pattern on the stack.
   * Save it away.
   */
  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;
  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OSTRING )
    return error_handler( TYPECHECK ) ;
  if ( ! oCanRead(*theo) && !object_access_override(theo) )
    return error_handler( INVALIDACCESS ) ;
  pattern_length = theLen(* theo ) ;

  if ( pattern_length > LONGESTFILENAME )
    return error_handler( LIMITCHECK ) ;

  if ( !ps_string(&pato, oString(*theo), pattern_length) ) {
    pop( &operandstack ) ;
    return FALSE ;
  }
  Global_Resource_Length = pattern_length ; /* used in callback */
  pop( &operandstack ) ;

   /* Set up the stack for a filenameforall call. */
  if ( ! push( &pato , &operandstack ))
    return FALSE ;

  theTags( proco ) = OOPERATOR | EXECUTE_ONLY ;
  oOp(proco) = &disk_resourceforall_op ;
  theIOpCall(&disk_resourceforall_op) = &disk_resourceforall_callback ;
  theIOpName(&disk_resourceforall_op) = theIOpName(oOp(errobject)) ;

  if ( ! push( &proco , &operandstack ))
    return FALSE ;

  if ( ! push( &stro , &operandstack )) {
    pop( &operandstack  ) ;
    return FALSE ;
  }

  if ( ! filenameforall_(pscontext)) {
    return FALSE ;
  }

  return TRUE ;
}



/* ----------------------------------------------------------------------------
   function:            disk_resource_callback author:              Luke Tunmer
   creation date:       11-Feb-1992            last modification:   ##-###-####
   arguments:
   description:
---------------------------------------------------------------------------- */
static Bool disk_resourceforall_callback(ps_context_t *pscontext)
{
  OBJECT *stro ;
  int32 pattern_length ;
  int32 str_length ;
  uint8 *str, *pattern_string ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* The top of the exec stack will be the operator for this func.
   * The second object is the resursive interpreter marker.
   */

  pattern_string = Global_Pattern_String ;   /* globals set earlier in */
  pattern_length = Global_Pattern_Length ;   /* GenericResourceforall_() */

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;
  stro = theTop( operandstack ) ;

  if ( oType(*stro) != OSTRING )
    return error_handler( TYPECHECK ) ;
  if ( ! oCanRead(*stro) && !object_access_override(stro) )
    return error_handler( INVALIDACCESS ) ;

  str_length = theLen(* stro ) ;
  str = oString(*stro) ;

  str += ( Global_Resource_Length - 1 ) ;
  str_length -= ( Global_Resource_Length - 1 ) ;

  pop( &operandstack ) ;

  if ( SwLengthPatternMatch( pattern_string, pattern_length, str ,
                            str_length )) {
    if (! add_if_not_there( &Global_Head_flist, str , str_length )) {
      free_flist(Global_Head_flist); /* free the space already claimed */
      return error_handler( VMERROR );
    }
  }
  return TRUE ;
}

void init_C_globals_filename(void)
{
  OPERATOR opinit = { 0 } ;
  Global_Head_flist = NULL ;
  Global_Pattern_String = NULL ;
  Global_Pattern_Length = 0 ;
  Global_Resource_Length = 0 ;
  disk_resourceforall_op = opinit ;
}

/* Log stripped */
