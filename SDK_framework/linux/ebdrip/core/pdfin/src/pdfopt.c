/** \file
 * \ingroup pdfin
 *
 * $HopeName: SWpdf!src:pdfopt.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF Optional Content Implementation
 */

#include "core.h"
#include "swerrors.h"
#include "hqmemset.h"
#include "objects.h"
#include "dictscan.h"
#include "pdfmatch.h"
#include "namedef_.h"
#include "pdfopt.h"
#include "pdfxref.h"
#include "pdfattrs.h"
#include "pdfin.h"
#include "routedev.h"
#include "gstack.h"
#include "pdfx.h"
#include "swpdfin.h" /*pdf_getStrictpdf*/

/*
  This file handles the optional content OC for >= PDF1.5
  Optional content is handled by declaring a set of OC groups
  which have notional OC status set to ON (TRUE) or OFF (FALSE).

  These groups hang off the oc_props structure (type pdf_ocproperties)
  in the PDF execution context.

  This database is interrogated by XObjects and marked-contexts that
  make reference to OC to determine if the object/stream should be visible.
*/
enum {
  e_printstate_not_set,
  e_printstate_OFF,
  e_printstate_ON
};

typedef struct oc_intent_s
{
  NAMECACHE * name;
  struct oc_intent_s * next;
} oc_intent_link_t;




enum {
  e_oc_config_OFF = 0,
  e_oc_config_BaseState,
  e_oc_config_Intent,
  e_oc_config_AS,
#if 0 /* values that are presently unused*/
  e_oc_config_ON,
  e_oc_config_Name,
  e_oc_config_Creator,
  e_oc_config_Order,
  e_oc_config_ListMode,
  e_oc_config_RBGroup,
#endif
  e_oc_config_max
};

static NAMETYPEMATCH pdf_OCConfig_dict[e_oc_config_max + 1] = {
  { NAME_OFF|OOPTIONAL,           3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
  { NAME_BaseState|OOPTIONAL,     2, { ONAME, OINDIRECT }},
  { NAME_Intent|OOPTIONAL,        4, { ONAME, OARRAY, OPACKEDARRAY,
                                       OINDIRECT }},
  { NAME_AS|OOPTIONAL,            3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
#if 0 /* values that are presently unused*/
  { NAME_ON|OOPTIONAL,            3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
  { NAME_Name|OOPTIONAL,          2, { OSTRING, OINDIRECT }},
  { NAME_Creator|OOPTIONAL,       2, { OSTRING, OINDIRECT }},
  { NAME_Order|OOPTIONAL,         3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
  { NAME_ListMode|OOPTIONAL,      2, { ONAME, OINDIRECT }},
  { NAME_RBGroup|OOPTIONAL,       3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
#endif
  DUMMY_END_MATCH
};


static oc_intent_link_t oc_default_intent = { system_names + NAME_View, NULL};


typedef struct pdf_ocgroup_s
{
 Bool     state; /* TRUE=ON */
 int32    print;
 oc_intent_link_t * intent; /* a linked list of intent names */
 /* all OCGs are indirect so objnum and objgen are used as unique
    identifiers since the name may not be unique (and this is quicker) */
 int32  objnum;
 uint16 objgen;
}pdf_ocgroup;

struct pdf_ocproperties_s
{
  pdf_ocgroup *  groups;
  uint32         num_groups;
  uint32         max_num_groups; /* some expected groups may be null */
};

enum {
  e_OCG_DICT_type,
  e_OCG_DICT_name,
  e_OCG_DICT_intent,
  e_OCG_DICT_usage,

  e_OCG_DICT_max
};

static NAMETYPEMATCH pdf_OCG_dict[e_OCG_DICT_max + 1] = {
  { NAME_Type,               2, { ONAME, OINDIRECT }},
  { NAME_Name,               2, { OSTRING, OINDIRECT }},
  { NAME_Intent | OOPTIONAL, 4,  { OARRAY, OPACKEDARRAY, ONAME, OINDIRECT }},
  { NAME_Usage  | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
  DUMMY_END_MATCH
};

static Bool pdf_OCMD_on_VE(PDFCONTEXT *pdfc,
                          Bool * state,
                          OBJECT * VE);



/* destroy the intent list if used. */
static void pdf_oc_freeIntent(oc_intent_link_t ** p_intent, mm_pool_t pool)
{
  oc_intent_link_t * intent;
  oc_intent_link_t * next;

  HQASSERT(p_intent != NULL, "bad intent pointer");
  intent = *p_intent;
  *p_intent = NULL;

  if (intent == &oc_default_intent)
    return;

  while (intent) {
    next = intent->next;
    mm_free( pool,
             ( mm_addr_t )( intent ) ,
             sizeof(oc_intent_link_t) );
    intent = next;
  }
}

/* Return a linked list identifying supported intents (currently just
   View and Design) */
static Bool pdf_oc_getIntentmask( OBJECT * obj,
  oc_intent_link_t ** p_intent,
  mm_pool_t pool)
{
  oc_intent_link_t * intent;

  if ( oType(*obj) == ONAME ) {
    *p_intent = intent = mm_alloc( pool ,
                                   sizeof( oc_intent_link_t ) ,
                                   MM_ALLOC_CLASS_PDF_OC ) ;
    if ( intent == NULL )
      return error_handler( VMERROR ) ;

    intent->name = oName(*obj);
    intent->next = NULL;

  } else {
    /* Its an array */
    uint32 index;
    uint32 max;

    max = theLen(*obj);

    *p_intent = NULL;

    for (index = 0;index < max;index++) {

      if ( oType(oArray(*obj)[index]) != ONAME) {
        pdf_oc_freeIntent(p_intent, pool);
        return error_handler( TYPECHECK ) ;
      }

      intent = mm_alloc( pool ,
                         sizeof( oc_intent_link_t ) ,
                         MM_ALLOC_CLASS_PDF_OC ) ;
      if ( intent == NULL ) {
        pdf_oc_freeIntent(p_intent, pool);
        return error_handler( VMERROR ) ;
      }

      intent->name = oName(oArray(*obj)[index]);
      intent->next = *p_intent;
      *p_intent = intent;
    }
  }

  return TRUE;
}


/* Fill out a new group in the list of OC groups
   setting its default OC state and remembering its name
   return FALSE on error.
*/
static Bool pdf_OCG_fill( PDFCONTEXT *pdfc,
  pdf_ocgroup * groups,
  int32 objnum,
  uint16 objgen,
  OBJECT * OCG_dict,
  int32 index,
  mm_pool_t pool)
{
  pdf_ocgroup * item;
  Bool print = e_printstate_not_set;

  /* to check */
  if ( !pdf_dictmatch( pdfc, OCG_dict, pdf_OCG_dict ))
    return FALSE;

  if ( oNameNumber(*pdf_OCG_dict[e_OCG_DICT_type].result) != NAME_OCG )
    return error_handler( SYNTAXERROR ) ;

  item = groups + index;
  item->objnum = objnum;
  item->objgen = objgen;
  item->state = TRUE;
  item->print = print;
  item->intent = &oc_default_intent;

  if ( pdf_OCG_dict[e_OCG_DICT_intent].result ) {
    if ( ! pdf_oc_getIntentmask( pdf_OCG_dict[e_OCG_DICT_intent].result,
                                 &item->intent, pool ) )
      return FALSE;
  }

  return TRUE;
}

/* Update the state of a group as requested */
static Bool pdf_OCG_changestate( PDFCONTEXT *pdfc,
    pdf_ocproperties * props,
    int32 objnum,
    uint16 objgen,
    OBJECT * dict,
    Bool state)
{
  uint32 u;
  pdf_ocgroup * item = props->groups;

  if ( !pdf_dictmatch( pdfc, dict, pdf_OCG_dict ))
    return FALSE;

  if ( oNameNumber( *pdf_OCG_dict[e_OCG_DICT_type].result ) != NAME_OCG )
    return error_handler( SYNTAXERROR ) ;

  for ( u = 0; u < props->num_groups; u++ ){
    if ( objnum == item->objnum && objgen == item->objgen ) {
      item->state = state;
      return TRUE;
    }
    item++;
  }

  return TRUE;
}


/* Update the state of a group as requested */
static void pdf_OCG_changeprint( pdf_ocproperties * props,
    int32 objnum,
    uint16 objgen,
    int32 print)
{
  uint32 u;
  pdf_ocgroup * item;

  HQASSERT(props != NULL,"pdf_OCG_changeprint: props is null");

  item = props->groups;

  for ( u = 0; u < props->num_groups; u++ ){
    if ( objnum == item->objnum && objgen == item->objgen ) {
      if (print == e_printstate_ON)
        item->print = (item->print == e_printstate_OFF)?
          e_printstate_OFF:e_printstate_ON;
      else
        item->print = e_printstate_OFF;
      break;
    }
    item++;
  }
}

/* Find the OC group and get its state. If no group is
   found then return FALSE to indicate this case but
   set the state to TRUE (i.e. default to ON).
*/
static Bool pdf_OCG_getstate( pdf_ocproperties * props,
  int32 objnum,
  uint16 objgen,
  Bool * state)
{
  uint32 u;
  pdf_ocgroup * groups;

  *state = TRUE; /* default */

  /* dict is missing so assume same as empty dict case and return */
  if (!props)
    return FALSE;

  groups = props->groups;

  for ( u = 0; u < props->num_groups; u++ ){
    if ( groups[u].objnum == objnum &&  groups[u].objgen == objgen ) {
      *state = groups[u].state;
      switch ( groups[u].print ) {
        case e_printstate_not_set:
          *state = groups[u].state;
          break;
        case e_printstate_OFF:
          *state = FALSE;
          break;
        case e_printstate_ON:
          *state = TRUE;
          break;
        default:
          HQFAIL("pdf_OCG_getstate: unknown print state");
      }
      return TRUE;
    }
  }

  return FALSE;
}





/* If the group expresses a list of intents and these don't match with
   the list of intents we are prepared to show, then ignore the
   group. This is done by removing its name so it is never found. If
   no intent is expressed in the group then leave it alone. */
static void pdf_oc_limitwithIntent( pdf_ocgroup * group,
                                    oc_intent_link_t * intent )
{
  oc_intent_link_t * gintent;

  if ( group->intent == NULL)
    return;

  /* now see if intent appears in group->intent */
  while (intent) {
    gintent = group->intent;

    while (gintent) {
      if ( gintent->name == intent->name)
        return;
      gintent = gintent->next;
    }

    intent = intent->next;
  }

  /* no overlap found so remove from OC by setting objnum to -1 */
  group->objnum = -1;
}


/* Given a category apply an OCGs usage dict
   Return FALSE on error */
static Bool pdf_oc_limitgroupwithCategory(PDFCONTEXT *pdfc,
                                          NAMECACHE * category,
                                          OBJECT * OCG)
{
  OBJECT * OCG_dict;
  int32 objnum;
  uint16 objgen;
  int32  usagetype = 0;
  int32  usageindex = 0;

  static NAMETYPEMATCH pdf_OCGusage_dict[] = {
    { NAME_Usage  | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
    DUMMY_END_MATCH
  };

  /* we have requested a usage event so act on it */
  enum {
    e_usage_Print,
    e_usage_View,
    e_usage_CreatorInfo,
    e_usage_Language,
    e_usage_Export,
    e_usage_Zoom,
    e_usage_User,
    e_usage_PageElement,

    e_usage_max
  };

  /* OC usage dict (p342) */
  static NAMETYPEMATCH pdf_OC_usage_dict[e_usage_max + 1] = {
    { NAME_Print  | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
    { NAME_View  | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
    { NAME_CreatorInfo  | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
    { NAME_Language  | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
    { NAME_Export  | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
    { NAME_Zoom  | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
    { NAME_User  | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
    { NAME_PageElement  | OOPTIONAL, 2, { ODICTIONARY, OINDIRECT }},
    DUMMY_END_MATCH
  };

  enum {e_usagetype_subtype = 0, e_usagetype_view, e_usagetype_print,
        e_usagetype_export, e_usagetype_max};
  static NAMETYPEMATCH pdf_OC_usage_type_dict[e_usagetype_max + 1] = {
    /* subtype only for Print case */
    { NAME_Subtype| OOPTIONAL,    2, { ONAME, OINDIRECT }},

    /* Expect ONE of the following three only (this is a contraction of 3 dicts:
       View, Print and Export, others may follow in future). (p342) */
    { NAME_ViewState| OOPTIONAL,  2, { ONAME, OINDIRECT }},
    { NAME_PrintState| OOPTIONAL, 2, { ONAME, OINDIRECT }},
    { NAME_ExportState| OOPTIONAL,  2, { ONAME, OINDIRECT }},
    DUMMY_END_MATCH
  };

  if (oType(*OCG) == ONULL)
    return TRUE ;

  if (oType(*OCG) != OINDIRECT)
    return error_handler( TYPECHECK ) ;

  /* find the usage dict */
  objnum = oXRefID(*OCG);
  objgen = theGen(*OCG);

  if ( ! pdf_lookupxref( pdfc, &OCG_dict, objnum, objgen, FALSE ) )
    return FALSE;

  if (OCG_dict == NULL || oType(*OCG_dict) == ONULL)
    return TRUE ;

  if (oType(*OCG_dict) != ODICTIONARY)
    return error_handler( TYPECHECK ) ;

  if ( !pdf_dictmatch( pdfc, OCG_dict, pdf_OCGusage_dict ))
    return FALSE;

  if (pdf_OCGusage_dict[0].result == NULL )
    return TRUE;

  if ( !pdf_dictmatch( pdfc, pdf_OCGusage_dict[0].result, pdf_OC_usage_dict ))
    return FALSE;

  /* select the appropriate category */
  switch (category->namenumber) {
    case NAME_View:
      if ( pdf_OC_usage_dict[e_usage_View].result != NULL) {
        usageindex = e_usagetype_view;
        usagetype = e_usage_View;
      }
      break;
    case NAME_Print:
      if ( pdf_OC_usage_dict[e_usage_Print].result != NULL) {
        /* There is a usage dict. Check to see if Print options
           are specified */
        usageindex = e_usagetype_print;
        usagetype = e_usage_Print;
      }
      break;
    case NAME_Export:
      if ( pdf_OC_usage_dict[e_usage_Export].result != NULL) {
        usageindex = e_usagetype_export;
        usagetype = e_usage_View;
      }
      break;
    default:
      /* unsupported optional content event type, so ignore */
      return TRUE ;
  }

  if ( !pdf_dictmatch( pdfc,
        pdf_OC_usage_dict[usagetype].result,
        pdf_OC_usage_type_dict )) {
    return FALSE;
  } else {
    int32 print;
    PDFXCONTEXT *pdfxc ;
    PDF_IXC_PARAMS *ixc ;
    PDF_GET_XC( pdfxc ) ;
    PDF_GET_IXC( ixc ) ;

    print = e_printstate_not_set;

    if ( pdf_OC_usage_type_dict[usageindex].result != NULL) {
      switch ( oNameNumber( *pdf_OC_usage_type_dict[usageindex].result )) {
        case NAME_OFF:
          print = e_printstate_OFF;
          break;
        case NAME_ON:
          print = e_printstate_ON;
          break;
        default:
          HQFAIL( "PDF OC usage state should be ON or OFF\n" );
          return error_handler( RANGECHECK ) ;
      }
    }

    /* Given the print state based on the category AND this to the
     current print state for this OCG */
    pdf_OCG_changeprint( ixc->oc_props, objnum, objgen, print);
  }

  return TRUE;
}

/* Limit OCGs using Usage dicts categories
   return FALSE on error */
static Bool pdf_oc_limitwithUsage(PDFCONTEXT *pdfc, NAMECACHE * event,
                                  OBJECT * AS)
{
  int32 len;
  int32 i,j,k;
  OBJECT * dict;
  OBJECT * groups;
  OBJECT * categories;
  OBJECT * category;

  /* OC Usage Application Dictionary (p344) */
  enum {e_oc_useapp_Event = 0,
       e_oc_useapp_OCGs,
       e_oc_useapp_Category,
       e_oc_useapp_max};

  static NAMETYPEMATCH pdf_UsageApplication_dict[e_oc_useapp_max + 1] = {
    { NAME_Event,  2, { ONAME, OINDIRECT }},
    { NAME_OCGs | OOPTIONAL,  3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_Category, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},

    DUMMY_END_MATCH
  };

  HQASSERT((oType(*AS) == OARRAY)||(oType(*AS) == OPACKEDARRAY),
          "pdf_oc_limitwithUsage: Object /AS is not an array.");

  len = theLen(*AS);

  /* work through the array of usage application dicts
     looking for those that use this event */
  dict = oArray(*AS);
  for (i = 0;i < len;i++,dict++) {
    if (oType(*dict) != ODICTIONARY)
      return error_handler(TYPECHECK);
    if ( !pdf_dictmatch( pdfc, dict, pdf_UsageApplication_dict ) )
      return FALSE;

    groups = pdf_UsageApplication_dict[e_oc_useapp_OCGs].result;
    if (groups) {
      if ( oName(*pdf_UsageApplication_dict[e_oc_useapp_Event].result) ==
           event) {
        /* apply all the categories to all the groups */
        categories = pdf_UsageApplication_dict[e_oc_useapp_Category].result;
        category = oArray(*categories);
        for (j = 0;j < theLen(*categories);j++,category++) {
          if (oType(*category) != ONAME)
            return error_handler(TYPECHECK);
          for (k = 0;k < theLen(*groups); k++) {
            if (!pdf_oc_limitgroupwithCategory(pdfc, oName(*category),
                                               oArray(*groups) + k))
              return FALSE;
          }
        }
      }
    }
  }
  return TRUE;
}

/* Convert the OC Properties dict to a pdf_ocproperties structure
   (passed back in oc_props). This will form the OC database for
   XObjects and marked context tags that handle OC to interrogate.
   Return FALSE on error.
*/
Bool pdf_oc_getproperties( PDFCONTEXT *pdfc,
    mm_pool_t pool,
    pdf_ocproperties ** oc_props,
    OBJECT *dict )
{
  OBJECT * arr;
  OBJECT * AS;
  OBJECT * default_config_dict;
  OBJECT * required_intents;
  pdf_ocproperties * props;
  uint32 u;
  Bool ret;
  oc_intent_link_t * intent = &oc_default_intent;
  uint32 arrlen;

  /* OC properties dict (page 337)*/
  enum {e_ocprops_OCGs, e_ocprops_D, e_ocprops_configs, e_ocprops_max};
  static NAMETYPEMATCH pdf_OCProperties_dict[e_ocprops_max + 1] = {
    { NAME_OCGs | OOPTIONAL ,  3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    { NAME_D | OOPTIONAL ,  2, { ODICTIONARY, OINDIRECT }},
    { NAME_Configs | OOPTIONAL, 3, { OARRAY, OPACKEDARRAY, OINDIRECT }},
    DUMMY_END_MATCH
  };

  GET_PDFXC_AND_IXC;

  if ( !pdfxOptionalContentDetected( pdfc ) )
    return FALSE;

  if ( !pdf_dictmatch( pdfc, dict, pdf_OCProperties_dict ) )
    return FALSE;

  /* allocate OC properties struct (freed in pdf_in_end_execution_context)*/
  props = *oc_props = mm_alloc( pool ,
                                sizeof( pdf_ocproperties ) ,
                                MM_ALLOC_CLASS_PDF_OC ) ;
  if ( props == NULL )
    return error_handler( VMERROR ) ;
  HqMemZero((uint8 *)props, (uint32)sizeof(pdf_ocproperties));

  arr = pdf_OCProperties_dict[e_ocprops_OCGs].result;
  if (arr) {
    arrlen = theLen(*arr);
    props->max_num_groups = arrlen;

    /* Note we alloc enough space for all the groups but some PDF
       files have groups set to null so we may have some extra space
       when we ignore these values. */
    props->groups = mm_alloc( pool ,
                              sizeof( pdf_ocgroup ) * props->max_num_groups ,
                              MM_ALLOC_CLASS_PDF_OC ) ;
    if ( props->groups == NULL ) {
      ret = error_handler( VMERROR ) ;
      goto error_oc_getproperties ;
    }
  } else {
    arrlen = 0;
    props->max_num_groups = 0;
    props->groups = NULL;
  }

  /* start adding groups and counting the legit ones */
  props->num_groups = 0;

  for ( u = 0; u < arrlen; u++ ) {
    OBJECT *OCG_dict = &(oArray(*arr)[u]);
    int32 objnum;
    uint16 objgen;

    /* null objects are skipped */
    if ( oType(*OCG_dict) == ONULL )
      continue;

    if ( oType(*OCG_dict) != OINDIRECT ) {
      ret = error_handler( TYPECHECK ) ;
      goto error_oc_getproperties ;
    }

    objnum = oXRefID(*OCG_dict);
    objgen = theGen(*OCG_dict);

    /* Resolve the indirect reference to the OutputIntent dictionary. */
    if ( ! pdf_lookupxref( pdfc, &OCG_dict, objnum, objgen, FALSE ) ) {
      ret = FALSE;
      goto error_oc_getproperties ;
    }

    /* missing or null objects are skipped */
    if (OCG_dict == NULL || oType(*OCG_dict) == ONULL)
        continue;

    if ( oType(*OCG_dict) != ODICTIONARY ) {
      ret = error_handler( TYPECHECK ) ;
      goto error_oc_getproperties ;
    }

    if ( !pdf_OCG_fill( pdfc, props->groups, objnum, objgen, OCG_dict,
                        props->num_groups++, pool ) ) {
      ret = FALSE;
      goto error_oc_getproperties ;
    }
  }

  /* just use the default configuration */
  default_config_dict = pdf_OCProperties_dict[e_ocprops_D].result;

  /* for some reason some jobs allow the (required) default_config_dict
     to be optional, probably because all its members are optional. This
     is not playing the game, but we have to live with it. So we'll treat
     it as optional unless strict is on. */
  if (default_config_dict != NULL) {
    if ( !pdf_dictmatch( pdfc, default_config_dict, pdf_OCConfig_dict ) ) {
        ret = FALSE;
        goto error_oc_getproperties ;
    }

    if ( pdf_OCConfig_dict[e_oc_config_BaseState].result ) {
      /* The default OC config dict may define BaseState but only as "ON" */
      if ( oNameNumber(*pdf_OCConfig_dict[e_oc_config_BaseState].result) !=
           NAME_ON ) {
        if (pdf_getStrictpdf(pdfxc)) {
          ret = error_handler( RANGECHECK ) ;
          goto error_oc_getproperties ;
        }
        /* If the BaseState is OFF then we ignore it and assume it is
           ON (as Adobe) */
      }
    }

    required_intents = pdf_OCConfig_dict[e_oc_config_Intent].result;
    arr = pdf_OCConfig_dict[e_oc_config_OFF].result;
    AS = pdf_OCConfig_dict[e_oc_config_AS].result;
  } else {
    PDFXCONTEXT *pdfxc ;
    PDF_GET_XC( pdfxc ) ;

    if (pdf_getStrictpdf(pdfxc)) {
      ret = error_handler( SYNTAXERROR ) ;
      goto error_oc_getproperties ;
    }

    required_intents = NULL;
    arr = NULL;
    AS = NULL;
  }

  /* we may set a new one via a pdfparam */
  if (ixc->OptionalContentOptions.flags & OC_INTENT)
    required_intents = &ixc->OptionalContentOptions.Intent;

  if (required_intents != NULL) {
    if ( ! pdf_oc_getIntentmask( required_intents, &intent, pool ) ) {
      ret = FALSE;
      goto error_oc_getproperties ;
    }
  }

  ret = TRUE;
  if ( arr != NULL ) {
    /* turn these OCGs OFF */
    for ( u = 0; u < theLen(*arr); u++ ) {
      OBJECT *OCG_dict = &(oArray(*arr)[u]);
      int32 objnum;
      uint16 objgen;

      if ( oType(*OCG_dict) == ONULL )
        continue;

      if ( oType(*OCG_dict) != OINDIRECT ) {
        ret = error_handler( TYPECHECK ) ;
        goto error_oc_getproperties ;
      }

      objnum = oXRefID(*OCG_dict);
      objgen = theGen(*OCG_dict);

      /* Resolve the indirect reference to the OutputIntent dictionary. */
      if ( ! pdf_lookupxref( pdfc, &OCG_dict, objnum, objgen, FALSE ) ) {
        ret = FALSE;
        goto error_oc_getproperties ;
      }

      /* skip null objects */
      if (OCG_dict == NULL || oType(*OCG_dict) == ONULL)
        continue;

      /* skip wrong objects */
      if ( oType(*OCG_dict) != ODICTIONARY )
        continue;

      if ( !pdf_OCG_changestate( pdfc, props, objnum, objgen,
                                 OCG_dict, FALSE ) ) {
        ret = FALSE ;
        goto error_oc_getproperties ;
      }
    }
  }

  /* turn off groups that don't use this intent */
  for (u = 0;u < props->num_groups;u++) {
    pdf_oc_limitwithIntent( props->groups + u, intent );
  }

  pdf_oc_freeIntent(&intent, pool);


  if ((ixc->OptionalContentOptions.flags & OC_EVENT) &&
      (AS != NULL )) {

    /* a PDFParam has expressed an event. Check the AS entry
       (page 339) and act on the event if it is listed */

    /* first to emulate Adobe Acrobat we apply the View usage */
    if (!pdf_oc_limitwithUsage(pdfc, NAME_View + system_names,AS) ) {
      ret = FALSE ;
      goto error_oc_getproperties ;
    }
    /* next we apply the required usage on-top of View */
    if (!pdf_oc_limitwithUsage(pdfc, ixc->OptionalContentOptions.Event,AS) ) {
      ret = FALSE ;
      goto error_oc_getproperties ;
    }
  }


/* jump here to clean up heap for errors*/
error_oc_getproperties:

  if ( !ret ) {
    if ( *oc_props != NULL ) {
      if ( props->groups != NULL) {
        mm_free( pool,
                 ( mm_addr_t )( props->groups ) ,
                 sizeof( pdf_ocgroup ) * props->max_num_groups );
        props->groups = NULL;
      }

      mm_free( pool,
               ( mm_addr_t )( *oc_props ) ,
               sizeof(pdf_ocproperties) );
      *oc_props = NULL;
    }
  }

  return ret ;
}

/* destructor for the OC groups database */
void pdf_oc_freedata( mm_pool_t pool, pdf_ocproperties * props )
{
    if ( props->groups != NULL ) {
      uint32 count;

      for (count = 0;count < props->num_groups;count++)
        pdf_oc_freeIntent(&props->groups[count].intent, pool);

      mm_free( pool,
             ( mm_addr_t )( props->groups ) ,
             sizeof(pdf_ocgroup) * props->max_num_groups );
    }
    mm_free( pool,
             ( mm_addr_t )( props ) ,
             sizeof(pdf_ocproperties) );
}


/* Apply P state on OCMD,
   return FALSE on error */
static Bool pdf_OCMD_on_P(PDFCONTEXT *pdfc,
                          pdf_ocproperties * props,
                          Bool * state,
                          int32 P,
                          OBJECT * OCGs,
                          OBJECT * dictobj)
{
  Bool tstate;
  int32 objnum;
  uint16 objgen;

  HQASSERT(BOOL_IS_VALID(*state) && *state,
           "expecting OCMD state to be TRUE by default");

  if ( oType(*OCGs) == ODICTIONARY ) {
    static NAMETYPEMATCH OCMD_indirect_dict[] = {
      { NAME_OCGs|OOPTIONAL,  1, { OINDIRECT }},
      DUMMY_END_MATCH
    };

    OBJECT * ocgobj;

    /*The case where we have only a single dictionary
      note this is dictmatch and not pdf_dictmatch so we
      can recover the object number. (All OCGs are indirect references) */
    if (!dictmatch(dictobj, OCMD_indirect_dict))
      return FALSE;

    ocgobj = OCMD_indirect_dict->result;

    if (ocgobj == NULL) {
      /* no groups*/
      return TRUE;
    }

    objnum = oXRefID(*ocgobj);
    objgen = theGen(*ocgobj);

    /* find current state and act on it if found
       otherwise return (i.e. treat like not OC) */
    if ( !pdf_OCG_getstate( props, objnum, objgen, &tstate) )
      return TRUE;

    if ( !tstate ) {
      if ( (P == NAME_AnyOn) || (P == NAME_AllOn) )
        *state = FALSE;
    } else {
      if ( (P == NAME_AnyOff) || (P == NAME_AllOff) )
        *state = FALSE;
    }
  } else {
    uint32 u;
    int32 on, off;

    on = off = 0;

    /* we expect an array of indirect references to OCGs */
    for ( u = 0; u < theLen(*OCGs); u++ ) {
      OBJECT *OCG_dict = &(oArray(*OCGs)[u]);

      if ( oType(*OCG_dict) == ONULL )
        continue;

      if ( oType(*OCG_dict) != OINDIRECT ) {
        return error_handler( TYPECHECK ) ;
      }

      objnum = oXRefID(*OCG_dict);
      objgen = theGen(*OCG_dict);

      /* Resolve the indirect reference to the OutputIntent dictionary. */
      if ( ! pdf_lookupxref( pdfc, &OCG_dict, oXRefID(*OCG_dict),
                             theGen(*OCG_dict), FALSE ) )
         return FALSE;

      /* Page 330 PDF1.5 spec
         "Note: Null values or references to deleted objects
         are ignored. If this entry is not present, is an
         empty array, or contains references only to null or
         deleted objects, the membership dictionary has no
         effect on the visibility of any content." */

      if (OCG_dict == NULL || oType(*OCG_dict) == ONULL)
        continue;

      if ( oType(*OCG_dict) != ODICTIONARY ) {
        return error_handler( TYPECHECK ) ;
      }

      /* fetch the OCG state or skip if missing (treat like not OC) */
      if ( !pdf_OCG_getstate( props, objnum, objgen, & tstate))
        continue;

      if (tstate)
        on++;
      else
        off++;

      /* quick exit conditions */
      switch ( P ) {
        case NAME_AnyOff:
          if ( !tstate )
            return TRUE;
          break;
        case NAME_AllOn:
          if ( !tstate ) {
            *state = FALSE;
            return TRUE;
          }
          break;
        case NAME_AnyOn:
          if ( tstate )
            return TRUE;
          break;
        case NAME_AllOff:
          if ( tstate ) {
            *state = FALSE;
            return TRUE;
          }
          break;
        default:
          HQFAIL( "unexpected state" );
      }
    }

    /* apply P condition to the results */
    switch ( P ) {
      case NAME_AnyOff:
        *state = (off > 0);
        break;
      case NAME_AllOn:
        *state = (off == 0) ;
        break;
      case NAME_AnyOn:
        *state = (on > 0);
        break;
      case NAME_AllOff:
        *state = (on == 0);
        break;
      default:
        HQFAIL( "unexpected state" );
    }
  }

  return TRUE;
}


/* return FALSE for error */
static Bool pdf_get_VE_element( PDFCONTEXT *pdfc,
                                Bool * state,
                                OBJECT * element)
{
  int32 objnum;
  uint16 objgen;
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  /* a direct VE sub-array */
  if (oType(*element) == OARRAY) {
    return pdf_OCMD_on_VE(pdfc, state, element);
  }

  if (oType(*element) != OINDIRECT) {
    *state = FALSE;
    return TRUE;
  }

  objnum = oXRefID(*element);
  objgen = theGen(* element);

  if ( ! pdf_lookupxref( pdfc,
                         & element,
                         objnum,
                         objgen,
                         FALSE ))
    return FALSE;

  if ((element == NULL) || oType(*element) == ONULL) {
    *state = FALSE;
    return TRUE;
  }

  /* an indirect VE sub-array */
  if (oType(*element) == OARRAY) {
    return pdf_OCMD_on_VE(pdfc, state, element);
  }

  /* otherwise is must be an OCG. If it is missing then we assume
     the state is FALSE */
  if (!pdf_OCG_getstate( ixc->oc_props, objnum, objgen, state))
    *state = FALSE;

  return TRUE;
}


/* return FALSE on error */
static Bool pdf_OCMD_on_VE(PDFCONTEXT *pdfc,
                          Bool * state,
                          OBJECT * VE)
{
  OBJECT * element;
  int32 objnum;
  uint16 objgen;
  int32 count,len;

  *state = TRUE;

  HQASSERT(oType(*VE) == OARRAY,"expecting an array for VE");

  /* VE is an array that specifies the structural logic of the OCMD
     the first element is always And Not or Or */
  len = theLen(*VE);
  if (len == 0)
    return error_handler(SYNTAXERROR) ;

  element = oArray(*VE);
  if (oType(*element) == OINDIRECT) {
    objnum = oXRefID(*element);
    objgen = theGen(* element);
    if ( ! pdf_lookupxref( pdfc,
                           & element,
                           objnum,
                           objgen,
                           FALSE ))
          return FALSE;
  }

  /* ignore null values */
  if (element == NULL || oType(*element) == ONULL) {
    *state = TRUE;
    return TRUE;
  }

  if (oType(*element) != ONAME)
    return error_handler(TYPECHECK) ;

  switch (oNameNumber(*element)) {
    case NAME_Not:
      if (len < 2)
        return error_handler(SYNTAXERROR) ;
      element++;

      if (!pdf_get_VE_element(pdfc, state, element))
        return FALSE;

       *state = ! (*state);
      break;
    case NAME_And:
      for (count = 1;count < len;count++) {
          element++;

        if (!pdf_get_VE_element(pdfc, state, element))
          return FALSE;

        if (!*state)
          return TRUE;
      }
      break;
    case NAME_Or:
      for (count = 1;count < len;count++) {
          element++;
        if (!pdf_get_VE_element(pdfc, state, element))
          return FALSE;

        if (*state)
          return TRUE;
      }

      *state = FALSE;
      break;
    default:
      HQFAIL("unknown option for VE in OCMD\n");
      return error_handler(SYNTAXERROR) ;
  }

  return TRUE;
}


/* Fetch the group state or OCMD state.
   Return FALSE if an error. */
Bool pdf_oc_getstate_OCG_or_OCMD(PDFCONTEXT *pdfc, OBJECT *dictobj, Bool *state)
{
  int32 objnum = 0 ;
  uint16 objgen = 0 ;
  Bool indirect = FALSE ;
  static NAMETYPEMATCH pdf_OC_type[] = {
    { NAME_Type |OOPTIONAL,   2, { ONAME, OINDIRECT }},
    DUMMY_END_MATCH
  };
  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  *state = TRUE;

  if (oType(*dictobj) == OINDIRECT) {
    indirect = TRUE ;
    objnum = oXRefID(*dictobj) ;
    objgen = theGen(*dictobj) ;

    if (!pdf_lookupxref( pdfc, &dictobj, objnum, objgen, FALSE ))
      return FALSE ;
  }

  /* skip groups which are NULL or deleted */
  if ( dictobj == NULL || oType( *dictobj ) == ONULL )
    return TRUE ;

  if ( oType( *dictobj ) != ODICTIONARY )
    return error_handler(TYPECHECK) ;

  if ( !pdf_dictmatch( pdfc, dictobj, pdf_OC_type ))
    return FALSE;

  if (pdf_OC_type->result == NULL)
    return TRUE ;

  switch ( oNameNumber(*pdf_OC_type->result) )
  {
    case NAME_OCG:
      /* fetch optional content group state (set to TRUE if missing) */
      if (!indirect) {
        /* Dictionary was a direct object, so can't do this! */
        return error_handler(TYPECHECK) ;
      }
      (void) pdf_OCG_getstate( ixc->oc_props, objnum, objgen, state);
      break;
    case NAME_OCMD:
    {
      enum {  e_ocmd_OCGs = 0,  e_ocmd_P, e_ocmd_VE, e_ocmd_max };

      static NAMETYPEMATCH pdf_OCMD_dict[e_ocmd_max + 1] = {
        { NAME_OCGs|OOPTIONAL,  4, { ODICTIONARY, OARRAY, OPACKEDARRAY,
                                     OINDIRECT }},
        { NAME_P|OOPTIONAL,     2, { ONAME, OINDIRECT }},
        { NAME_VE|OOPTIONAL,    2, { OARRAY, OINDIRECT }},
        DUMMY_END_MATCH
      };

      /* optional content membership dict */
      if ( !pdf_dictmatch( pdfc, dictobj, pdf_OCMD_dict ))
        return FALSE;

      if ( pdf_OCMD_dict[e_ocmd_VE].result != NULL ) {
        return pdf_OCMD_on_VE(pdfc, state, pdf_OCMD_dict[e_ocmd_VE].result);
      } else {
        OBJECT * OCGs = pdf_OCMD_dict[e_ocmd_OCGs].result;

        if ( OCGs != NULL ) {
          int32 P = NAME_AnyOn;

          if ( pdf_OCMD_dict[e_ocmd_P].result != NULL )
            P = oNameNumber(*pdf_OCMD_dict[e_ocmd_P].result);

          return pdf_OCMD_on_P(pdfc, ixc->oc_props, state, P, OCGs, dictobj);
        }
      }
    }
    break;
    default:
      HQFAIL("unexpected value when expecting an OC group or OCMD\n");
      return error_handler( TYPECHECK ) ;
  }

  return TRUE;
}

/* Find the current state of a requested OC group or OCMD group of groups.
   return FALSE if an error */
Bool pdf_oc_getstate_fromprops( PDFCONTEXT *pdfc, OBJECT * properties,
                                Bool * state)
{
  OBJECT ocgprops;

  PDFXCONTEXT *pdfxc ;
  PDF_IXC_PARAMS *ixc ;
  PDF_IMC_PARAMS *imc ;

  PDF_CHECK_MC( pdfc ) ;
  PDF_GET_IMC( imc ) ;
  PDF_GET_XC( pdfxc ) ;
  PDF_GET_IXC( ixc ) ;

  *state = TRUE; /* default state */

  /*if no optional content defined then default is ON (TRUE)*/
  if ( ixc->oc_props == NULL )
    return TRUE;

  /* if the DP mark content operator has set OC to OFF then obey */
  if ( !imc->mc_DP_oc_status ) {
    *state = FALSE;
    return TRUE;
  }

  if ( oType( *properties ) != ONAME )
    return error_handler( TYPECHECK ) ;

  /* Get the resource. */
  if ( ! pdf_get_resourceid( pdfc , NAME_Properties ,properties , & ocgprops ))
    return FALSE;

  return pdf_oc_getstate_OCG_or_OCMD(pdfc, &ocgprops, state);
}

/* Log stripped */

/* end of file pdfopt.c */
