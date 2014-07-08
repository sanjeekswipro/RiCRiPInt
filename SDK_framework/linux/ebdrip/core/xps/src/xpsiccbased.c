/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!src:xpsiccbased.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * xps iccbased colorspace cache and sRGB and scRGB ICCBased colorspaces
 */

#include "core.h"
#include "coreinit.h"
#include "gstate.h"
#include "swcopyf.h"
#include "swerrors.h"      /* TYPECHECK */
#include "xml.h"
#include "mps.h"           /* mps_root_t */
#include "gcscan.h"        /* ps_scan_field */
#include "gsc_icc.h"       /* get_iccbased_dimension */
#include "fileio.h"
#include "gschead.h"
#include "graphics.h"
#include "gschcms.h"

#include "xps.h"
#include "xpsiccbased.h"
#include "namedef_.h"
#include "dicthash.h"


/** \brief Default ICCBased sRGB colorspace for vector elements */
static OBJECT sRGBspace;
static mps_root_t sRGBRoot;

/** \brief Default ICCBased scRGB colorspace for vector elements */
static OBJECT scRGBspace;
static mps_root_t scRGBRoot;

/*---------------------------------------------------------------------------*/
/* Functions for the sRGB space */

void xps_sRGB_reset( void )
{
  sRGBspace = onull; /* Struct copy to set slot properties */
}

OBJECT* get_xps_sRGB( void )
{
  return &sRGBspace;
}

/* Open the sRGB profile and create the colorspace */
static Bool xps_sRGB_start( void )
{
  OBJECT   sRGBprofile = OBJECT_NOTVM_NOTHING;
  OBJECT   ofile = OBJECT_NOTVM_NOTHING;

  if ( !ps_string( &sRGBprofile, DEF_SRGB_PROFILE_STR, DEF_SRGB_PROF_STRLEN))
    return FALSE;

  if ( !file_open( &sRGBprofile, SW_RDONLY | SW_FROMPS, READ_FLAG, FALSE, 0, &ofile))
    return FALSE;

  if ( !ps_array( &sRGBspace, 2 ))
    return FALSE;

  object_store_name(&oArray(sRGBspace)[0], NAME_ICCBased, LITERAL);
  OCopy(oArray(sRGBspace)[1], ofile);

  return TRUE;
}

/* Set the default sRGB ICCBased colorspace */
Bool set_xps_sRGB( int32 colortype )
{
  if (oType(sRGBspace) == ONULL) {
    if ( !xps_sRGB_start())
      return FALSE;
  }

  if (!compare_objects(gsc_getcolorspaceobject(gstateptr->colorInfo, colortype),
                       &sRGBspace)) {
    if (!push(&sRGBspace, &operandstack) ||
        !gsc_setcolorspace(gstateptr->colorInfo, &operandstack, colortype))
      return FALSE;
  }

  return TRUE;
}

/* Set up a default sRGB Blend intercept space */
Bool set_xps_blendRGB( void )
{
  OBJECT dict = OBJECT_NOTVM_NOTHING;

  if (oType(sRGBspace) == ONULL) {
    if ( !xps_sRGB_start())
      return FALSE;
  }

  if (!ps_dictionary(&dict, 1))
    return FALSE;

  if (!fast_insert_hash_name(&dict, NAME_BlendRGB, &sRGBspace))
    return FALSE;

  if (!gsc_setinterceptcolorspace(gstateptr->colorInfo, &dict))
    return FALSE;

  return TRUE;
}

/* See if a profile is our default sRGB profile */
Bool xps_profile_is_sRGB( OBJECT *iccbasedspace, Bool *match )
{
  HQASSERT( iccbasedspace != NULL,
            "Null iccbased space in xps_profile_is_sRGB" );
  HQASSERT( match != NULL,
            "Null match in xps_profile_is_sRGB" );

  if (oType(sRGBspace) == ONULL) {
    if ( !xps_sRGB_start())
      return FALSE;
  }

  if ( !gsc_compare_md5s( gstateptr->colorInfo, iccbasedspace, &sRGBspace, match ))
    return FALSE;

  return TRUE;
}

/* scansRGBspace - scan the sRGB space */
static mps_res_t MPS_CALL scansRGBspace(mps_ss_t ss, void *p, size_t s)
{
  UNUSED_PARAM( void *, p );
  UNUSED_PARAM( size_t, s );

  return ps_scan_field( ss, &sRGBspace );
}

static Bool xps_sRGB_init( void )
{
  /* Create root last so we force cleanup on success. */
  if ( mps_root_create( &sRGBRoot, mm_arena, mps_rank_exact(),
      0, scansRGBspace, NULL, 0 ) != MPS_RES_OK ) {

    HQFAIL("xps_sRGB_init: Failed to register xps sRGB space root");
    return FAILURE(FALSE) ;
  }
  return TRUE;
}

static void xps_sRGB_finish( void )
{
  HQASSERT( oType( sRGBspace ) == ONULL,
            "Expected null object in xps_sRGB_finish" );

  mps_root_destroy( sRGBRoot );
}

/*---------------------------------------------------------------------------*/
/* Functions for the scRGB space */

void xps_scRGB_reset( void )
{
  scRGBspace = onull; /* Struct copy to set slot properties */
}

/* Open the scRGB profile and create the colorspace */
static Bool xps_scRGB_start( void )
{
  OBJECT   scRGBprofile = OBJECT_NOTVM_NOTHING;
  OBJECT   ofile = OBJECT_NOTVM_NOTHING;

  if ( !ps_string( &scRGBprofile, DEF_SCRGB_PROFILE_STR, DEF_SCRGB_PROF_STRLEN))
    return FALSE;

  if ( !file_open( &scRGBprofile, SW_RDONLY | SW_FROMPS, READ_FLAG, FALSE, 0, &ofile))
    return FALSE;

  if ( !ps_array( &scRGBspace, 2 ))
    return FALSE;

  object_store_name(&oArray(scRGBspace)[0], NAME_ICCBased, LITERAL);
  OCopy(oArray(scRGBspace)[1], ofile) ;

  return TRUE;
}

/* Set the default sRGB ICCBased colorspace */
Bool set_xps_scRGB( int32 colortype )
{
  if (oType(scRGBspace) == ONULL ) {
    if ( !xps_scRGB_start())
      return FALSE;
  }

  if (!push(&scRGBspace, &operandstack) ||
      !gsc_setcolorspace(gstateptr->colorInfo, &operandstack, colortype))
    return FALSE;

  return TRUE;
}

/* See if a profile is our default scRGB profile */
Bool xps_profile_is_scRGB( OBJECT *iccbasedspace, Bool *match )
{
  HQASSERT( iccbasedspace != NULL,
            "Null iccbased space in xps_profile_is_scRGB" );
  HQASSERT( match != NULL,
            "Null match in xps_profile_is_scRGB" );

  if (oType(scRGBspace) == ONULL) {
    if ( !xps_scRGB_start())
      return FALSE;
  }

  if ( !gsc_compare_md5s( gstateptr->colorInfo, iccbasedspace, &scRGBspace, match ))
    return FALSE;

  return TRUE;
}

/* scansRGBspace - scan the scRGB space */
static mps_res_t MPS_CALL scanscRGBspace(mps_ss_t ss, void *p, size_t s)
{
  UNUSED_PARAM( void *, p );
  UNUSED_PARAM( size_t, s );

  return ps_scan_field( ss, &scRGBspace );
}

static Bool xps_scRGB_init( void )
{
  /* Create root last so we force cleanup on success. */
  if ( mps_root_create( &scRGBRoot, mm_arena, mps_rank_exact(),
      0, scanscRGBspace, NULL, 0 ) != MPS_RES_OK ) {

    HQFAIL("xps_sRGB_init: Failed to register xps scRGB space root");
    return FAILURE(FALSE) ;
  }
  return TRUE;
}

static void xps_scRGB_finish( void )
{
  HQASSERT( oType( scRGBspace ) == ONULL,
            "Expected null object in xps_scRGB_finish" );

  mps_root_destroy( scRGBRoot );
}
/*---------------------------------------------------------------------------*/

/** \brief icc cache entry. */
typedef struct XPS_ICC_ELEMENT {

  /* Store a chain of icc colorspace objects tagged by savelevel.
   * Since the corresponding items in the icc_profile_info_cache
   * are purged down to the appropriate save level on a restore
   * these should be too.
   */

  int32                      sid;          /**< save level */
  struct XPS_ICC_ELEMENT     *next;        /**< ICC cache chain link. */
  int32                      uid;          /**< Part uid - unique within a job */
  int32                      n_colorants ; /**< Number of profile colorants */
  OBJECT                     colorspace;   /**< ICCBased colorspace */

} XPS_ICC_ELEMENT;

/** \brief icc cache head. */
static XPS_ICC_ELEMENT *xps_icc_cache_head;

static mps_root_t xps_icc_cache_root = NULL;


static mps_res_t MPS_CALL xps_icc_cache_root_scan(mps_ss_t ss, void *p, size_t s)
{
  mps_res_t   res;
  XPS_ICC_ELEMENT * p_node;

  UNUSED_PARAM( void *, p );
  UNUSED_PARAM( size_t, s );

  /* Scan colorspace objects in cache */
  p_node = xps_icc_cache_head;

  while (p_node != NULL)
  {
    res = ps_scan_field(ss, &p_node->colorspace);

    if (res != MPS_RES_OK) {
      return(res);
    }
    p_node = p_node->next;
  }

  return(MPS_RES_OK);

} /* xps_icc_cache_root_scan */



/* Bool xps_icc_cache_init() - initialize the colorspace cache for use */
static Bool xps_icc_cache_init(void)
{
  /* Create root last so we force cleanup on success. */
  if ( mps_root_create( &xps_icc_cache_root, mm_arena, mps_rank_exact(), 0,
                        xps_icc_cache_root_scan, NULL, 0 ) != MPS_RES_OK) {
    HQFAIL("xps_icc_cache_init: Failed to register xps icc cache root");
    return FAILURE(FALSE) ;
  }
  return TRUE;
}


/**
 * \brief
 * Make a new colorspace object and add it to the cache.
 *
 * \param[in] profile_uid
 * ICC profile Unique ID
 * \param[in] uri
 * Pointer to profile uri
 * \param[out] colorspace
 * Pointer to PS ICCBased colorspace array object made.
 * \param[out] dimensions
 * Number of profile colorspace dimensions
 *
 * \return
 * \c TRUE if the colorspace was created and added to the list, else \c
 * FALSE.
 */
static Bool xps_icc_cache_add(
int32     profile_uid,
/*@in@*/ /*@notnull@*/
hqn_uri_t *uri,
/*@in@*/ /*@notnull@*/
OBJECT **colorspace,
/*@in@*/ /*notnull@*/
int32 *dimensions)
{
  XPS_ICC_ELEMENT *p_node;
  OBJECT coloro = OBJECT_NOTVM_NOTHING, *arrayo = 0;
  int32 ok = TRUE;

  HQASSERT(uri != NULL, "uri is NULL");
  HQASSERT(profile_uid >= 0, "profile_uid is less than zero");

  p_node = ( XPS_ICC_ELEMENT * )mm_alloc( mm_xml_pool ,
                                          sizeof( XPS_ICC_ELEMENT ),
                                          MM_ALLOC_CLASS_XPS_ICC_CACHE );
  if (p_node == NULL)
    return error_handler( VMERROR );

  /* Fill in the details */
  *dimensions = 0;
  p_node->sid = CoreContext.savelevel;
  p_node->uid = profile_uid;

  /* Make colorspace array */
  ok = ps_array(&coloro, 3);

  if (ok) {
    arrayo = oArray(coloro);

    /* Add the colorspace type */
    object_store_name(&arrayo[0], NAME_ICCBased, LITERAL);

    /* Add the file object and param dict */
    ok = open_file_from_psdev_uri(uri, &arrayo[1], TRUE) &&
         ps_dictionary(&arrayo[2], 2);
  }

  /* Insert XRef into the param dict */
  if (ok) {
    oInteger(inewobj) = profile_uid;
    ok = fast_insert_hash_name(&arrayo[2], NAME_XRef, &inewobj);
  }

  /* Get the number of profile colorants */
  if (ok) {
    /* As there is as yet no /N in the param dict this will get profile
     * dimensions
     */
    ok = gsc_get_iccbased_dimension( gstateptr->colorInfo, &coloro, dimensions );
  }

  /* Insert /N into the param dict */
  if (ok) {
    oInteger(inewobj) = *dimensions;
    ok = fast_insert_hash_name(&arrayo[2], NAME_N, &inewobj);
  }

  /* Tidy up if any of the above went wrong */
  if (!ok) {
    mm_free(mm_xml_pool, p_node, sizeof(XPS_ICC_ELEMENT));
    return FALSE;
  }

  p_node->colorspace = coloro; /* Struct copy to set slot properties, coloro is NOTVM */
  p_node->n_colorants = *dimensions;

  /* Add to head of chain. */
  p_node->next = xps_icc_cache_head;
  xps_icc_cache_head = p_node ;

  /* Return new colorspace object */
  *colorspace = &p_node->colorspace;
  return TRUE;
}


/* Bool xps_icc_cache_define() - look for colorspace in cache or add it */
Bool xps_icc_cache_define(
/*@in@*/ /*@notnull@*/
xps_partname_t *color_profile_partname,
int32 n_colorants,
/*@in@*/ /*@notnull@*/
OBJECT **colorspace)
{
  Bool found = FALSE;
  XPS_ICC_ELEMENT *p_node, *p_last_node;
  int32 profile_dimensions = 0;

  HQASSERT(color_profile_partname != NULL, "color_profile_partname is NULL") ;

  /* Search for the uid */
  p_node = p_last_node = xps_icc_cache_head;

  while (p_node != NULL)
  {
    if (p_node->uid == color_profile_partname->uid) {
      *colorspace = &p_node->colorspace;

      /* Keep the list in most recently used order */
      if (p_node != xps_icc_cache_head)
      {
        p_last_node->next = p_node->next;
        p_node->next = xps_icc_cache_head;
        xps_icc_cache_head = p_node;
      }

      profile_dimensions = p_node->n_colorants;
      found = TRUE;
      break;
    }

    p_last_node = p_node;
    p_node=p_node->next;
  }

  if ( !found )
  {
    /* The entry did not exist so make one */
    if ( !xps_icc_cache_add(color_profile_partname->uid,
                           color_profile_partname->uri,
                           colorspace,
                           &profile_dimensions))
      return FALSE;
  }

  /* Check the profile has the right number of dimensions for the ContextColor */
  if ( profile_dimensions != n_colorants )
    return error_handler( SYNTAXERROR );

  return TRUE;
}


/* xps_icc_cache_finish() - finish with the icc colorspace cache */
static void xps_icc_cache_finish(void)
{
  HQASSERT( xps_icc_cache_head == NULL, "Items left over in icc cache");

  mps_root_destroy(xps_icc_cache_root);
}

/* xps_icc_cache_purge() - purge the cache down to the savelevel */
void xps_icc_cache_purge(int32 slevel)
{
  XPS_ICC_ELEMENT *p_node, *p_last_node, *u = NULL;

  /* Go through the whole list purging down to the save level */
  p_node = p_last_node = xps_icc_cache_head;

  while (p_node != NULL)
  {
    u = p_node->next;

    if (p_node->sid > slevel)
    {
      if (p_node != xps_icc_cache_head)
      {
        mm_free(mm_xml_pool, p_node, sizeof(XPS_ICC_ELEMENT));
        p_last_node->next = u;
      }
      else
      {
        mm_free(mm_xml_pool, p_node, sizeof(XPS_ICC_ELEMENT));
        p_last_node = xps_icc_cache_head = u;
      }
    }
    else
    {
      if (p_node != xps_icc_cache_head)
      {
        p_last_node = p_node;
      }
    }

    p_node = u;
  }
}

void init_C_globals_xpsiccbased(void)
{
  sRGBspace = onull ;
  sRGBRoot = NULL ;
  scRGBspace = onull ;
  scRGBRoot = NULL ;
  xps_icc_cache_head = NULL ;
  xps_icc_cache_root = NULL ;
}

Bool xps_colorspace_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  if ( xps_sRGB_init() ) {
    if ( xps_scRGB_init() ) {
      if ( xps_icc_cache_init() )
        return TRUE ;

      xps_scRGB_finish() ;
    }
    xps_sRGB_finish() ;
  }

  return FALSE ;
}

void xps_colorspace_finish(void)
{
  xps_icc_cache_finish() ;
  xps_scRGB_finish() ;
  xps_sRGB_finish() ;
}

/* Log stripped */
