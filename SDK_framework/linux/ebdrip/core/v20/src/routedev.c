/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:routedev.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Indirection for sending low-level DL objects to different devices.
 */

#include "core.h"
#include "swerrors.h"

#include "params.h"     /* for psvm.h */
#include "psvm.h"
#include "control.h"    /* for extern OBJECT *topDictStackObj */
#include "bitblts.h"
#include "ndisplay.h"
#include "routedev.h"
#include "matrix.h"
#include "display.h"
#include "graphics.h"
#include "upath.h"

#include "dl_cell.h"
#include "dl_image.h"
#include "dl_bres.h"
#include "stacks.h"
#include "fcache.h"
#include "images.h"
#include "plotops.h"
#include "gu_rect.h"
#include "bresfill.h"
#include "shadex.h"
#include "gouraud.h" /* for dodisplaygouraud */
#include "system.h" /* gs_freecliprec */

Bool optional_content_on = TRUE;

/* there is a route_ok function for each set of parameters */
static Bool route_ok_setg(DL_STATE *page, int32 colorType , int32 options );

static Bool route_ok_gouraud(DL_STATE *page,
                             SHADINGvertex *v1,
                             SHADINGvertex *v2,
                             SHADINGvertex *v3,
                             SHADINGinfo *sinfo);
static Bool route_ok_bressfill(DL_STATE *page, int32 rule, NFILLOBJECT *nfill);
static Bool route_ok_rect(DL_STATE *page, dbbox_t *therect);
static Bool route_ok_char(DL_STATE *page, FORM *form, int32 x, int32 y);

/* similar numbers of route_error functions */
/* Not static, because any file can use the macro DEVICE_HAS_RECTS
   which does device_current_rect != route_error_1 to see whether the
   device has a rectangle handler:
*/
Bool route_error_rect(DL_STATE *page, dbbox_t *therect);

static Bool route_error_gouraud(DL_STATE *page,
                                SHADINGvertex *v1,
                                SHADINGvertex *v2,
                                SHADINGvertex *v3,
                                SHADINGinfo *sinfo);

static Bool route_ok_bressfill(DL_STATE *page, int32 rule, NFILLOBJECT *nfill)
{
  UNUSED_PARAM(DL_STATE *, page);
  UNUSED_PARAM(int32, rule);
  UNUSED_PARAM(NFILLOBJECT *, nfill);
  return TRUE;
}

static Bool route_ok_setg(DL_STATE *page, int32 colorType , int32 options)
{
  UNUSED_PARAM(DL_STATE *, page);
  UNUSED_PARAM( int32 , colorType ) ;
  UNUSED_PARAM( int32 , options ) ;
  return TRUE ;
}

static Bool route_ok_gouraud(DL_STATE *page,
                             SHADINGvertex *v1,
                             SHADINGvertex *v2,
                             SHADINGvertex *v3,
                             SHADINGinfo *sinfo)
{
  UNUSED_PARAM(DL_STATE *, page);
  UNUSED_PARAM(SHADINGvertex *, v1);
  UNUSED_PARAM(SHADINGvertex *, v2);
  UNUSED_PARAM(SHADINGvertex *, v3);
  UNUSED_PARAM(SHADINGinfo *, sinfo);
  return TRUE;
}

static Bool route_ok_rect(DL_STATE *page, dbbox_t *therect)
{
  UNUSED_PARAM(DL_STATE *, page);
  UNUSED_PARAM(dbbox_t *, therect);
  return TRUE;
}

static Bool route_ok_char(DL_STATE *page, FORM *form, int32 x, int32 y)
{
  UNUSED_PARAM(DL_STATE *, page);
  UNUSED_PARAM(FORM *, form);
  UNUSED_PARAM(int32, x);
  UNUSED_PARAM(int32, y);
  return TRUE;
}

Bool route_error_rect(DL_STATE *page, dbbox_t *therect)
{
  UNUSED_PARAM(DL_STATE *, page);
  UNUSED_PARAM(dbbox_t *, therect);
  return error_handler(UNREGISTERED);
}

static Bool route_error_gouraud(DL_STATE *page,
                                SHADINGvertex *v1,
                                SHADINGvertex *v2,
                                SHADINGvertex *v3,
                                SHADINGinfo *sinfo)
{
  UNUSED_PARAM(DL_STATE *, page);
  UNUSED_PARAM(SHADINGvertex *, v1);
  UNUSED_PARAM(SHADINGvertex *, v2);
  UNUSED_PARAM(SHADINGvertex *, v3);
  UNUSED_PARAM(SHADINGinfo *, sinfo);
  return error_handler(UNREGISTERED);
}

static Bool route_ok_cell(DL_STATE *page, CELL *cell)
{
  UNUSED_PARAM(DL_STATE *, page);
  UNUSED_PARAM(CELL *, cell);
  return TRUE;
}

static Bool route_error_cell(DL_STATE *page, CELL *cell)
{
  UNUSED_PARAM(DL_STATE *, page);
  UNUSED_PARAM(CELL *, cell);
  return error_handler( UNREGISTERED ) ;
}

/* Forward declarations for suppress page functions, below */
static Bool route_suppress_setg(DL_STATE *page, int32 colorType, int32 options);
static Bool route_suppress_gouraud(DL_STATE *page,
                                   SHADINGvertex *v1,
                                   SHADINGvertex *v2,
                                   SHADINGvertex *v3,
                                   SHADINGinfo *sinfo);
static int32 route_suppress_addobjecttodl(DL_STATE *page, LISTOBJECT * lobj);
static Bool route_suppress_nbressdisplay(DL_STATE *page, int32 rule, NFILLOBJECT * nfill);
static Bool route_suppress_rectdisplay(DL_STATE *page, dbbox_t * therect);
static Bool route_suppress_chardisplay(DL_STATE *page, FORM * form, int32 x, int32 y);
static Bool route_suppress_image(DL_STATE *page, STACK *stack, IMAGEARGS *image);
static Bool route_suppress_celldisplay(DL_STATE *page, CELL *cell);

/* High level jump functions. */

Bool (*device_current_setg)(DL_STATE *page, int32 colorType, int32 options);
Bool (*device_table_setg[])(DL_STATE *page, int32 colorType, int32 options) = {
  route_ok_setg,                /* null */
  setg,                         /* band */
  csetg,                        /* char */
  setg,                         /* pattern1 */
  setg,                         /* pattern2 */
  route_suppress_setg,          /* suppress */
  route_ok_setg                 /* errband */
};

Bool (*device_current_image)(DL_STATE *page, STACK *stack, IMAGEARGS *image);
Bool (*device_table_image[])(DL_STATE *page, STACK *stack, IMAGEARGS *image) = {
  donullimage,                  /* null */
  dodisplayimage,               /* band */
  docharimage,                  /* char */
  dodisplayimage,               /* pattern1 */
  dodisplayimage,               /* pattern2 */
  route_suppress_image,         /* suppress */
  donullimage,                  /* errband */
};

Bool (*device_current_gouraud)(DL_STATE *page,
                               SHADINGvertex *v1, SHADINGvertex *v2,
                               SHADINGvertex *v3, SHADINGinfo *sinfo);
Bool (*device_table_gouraud[])(DL_STATE *page,
                               SHADINGvertex *v1, SHADINGvertex *v2,
                               SHADINGvertex *v3, SHADINGinfo *sinfo) = {
  route_ok_gouraud,             /* null */
  dodisplaygouraud,             /* band */
  route_error_gouraud,          /* char */
  dodisplaygouraud,             /* pattern1 */
  dodisplaygouraud,             /* pattern2 */
  route_suppress_gouraud,       /* suppress */
  route_ok_gouraud              /* errband */
};

/* Low level jump functions. */

int32 (*device_current_addtodl)(DL_STATE *page, LISTOBJECT *lobj);
int32 (*device_table_addtodl[])(DL_STATE *page, LISTOBJECT *lobj) = {
  addobjecttonodl,              /* null */
  addobjecttopagedl,            /* band */
  addobjecttoerrordl,           /* char */
  addobjecttopatterndl,         /* pattern1 */
  addobjecttopatterndl,         /* pattern2 */
  route_suppress_addobjecttodl, /* suppress */
  addobjecttonodl               /* errband */
};

Bool (*device_current_bressfill)(DL_STATE *page, int32 rule, NFILLOBJECT *nfill);
Bool (*device_table_bressfill[])(DL_STATE *page, int32 rule, NFILLOBJECT *nfill) = {
  route_ok_bressfill,           /* null */
  add2dl_nfill,                 /* band */
  fillnbressdisplay,            /* char */
  add2dl_nfill,                 /* pattern1 */
  add2dl_nfill,                 /* pattern2 */
  route_suppress_nbressdisplay, /* suppress */
  route_ok_bressfill            /* errband */
};

Bool (*device_current_rect)(DL_STATE *page, dbbox_t *therect);
Bool (*device_table_rect[])(DL_STATE *page, dbbox_t *therect) = {
  route_ok_rect,                /* null */
  addrectdisplay,               /* band */
  fillrectdisplay,              /* char */
  addrectdisplay,               /* pattern1 */
  addrectdisplay,               /* pattern2 */
  route_suppress_rectdisplay,   /* suppress */
  route_ok_rect                 /* errband */
};

Bool (*device_current_char)(DL_STATE *page, FORM *form, int32 x, int32 y);
Bool (*device_table_char[])(DL_STATE *page, FORM *form, int32 x, int32 y) = {
  route_ok_char,                /* null */
  addchardisplay,               /* band */
  fillchardisplay,              /* char */
  addchardisplay,               /* pattern1 */
  addchardisplay,               /* pattern2 */
  route_suppress_chardisplay,   /* suppress */
  route_ok_char                 /* errband */
};

/* Cells can't be part of patterns or chars, or be clipped. */

Bool (*device_current_cell)(DL_STATE *page, CELL *cell) ;
Bool (*device_table_cell[])(DL_STATE *page, CELL *cell) = {
  route_ok_cell,                /* null */
  addcelldisplay,               /* band */
  route_error_cell,             /* char */
  route_error_cell,             /* pattern1 */
  route_error_cell,             /* pattern2 */
  route_suppress_celldisplay,   /* suppress */
  route_ok_cell                 /* errband */
};

/* ====================================================================== */

/** This flag is set when DL state changes (such as clipping) are to
    be skipped - it's a way of returning early in certain places and
    avoiding more work when we know we're going to be suppressing the
    whole page rather than just some objects. It doesn't make sense to
    do this if you're not also going to suppress DL creation, but
    there are times when doing it the other way around is useful. */

static Bool gfSuppressDeviceStateChange = FALSE;

/** This flag is used to suppress the creation and addition to display
    list of DL objects. It is designed to be turned on and off within
    a single page - so clipping and setg operations go on regardless
    in order that we're ready to add DL objects when the flag
    transitions from on to off. */

static Bool gfSuppressDLCreation = FALSE;

/** Hook to be called during suppress device routing. */

static Bool (*gAddObjectToDLCallback)(LISTOBJECT *lobj, void *priv_data,
                                      Bool *suppress) = NULL;
static void *gAddObjectToDLCallbackPrivData = NULL;


/* ---------------------------------------------------------------------- */
void routedev_setDSCsuppression(Bool fSuppress)
{
  gfSuppressDeviceStateChange = fSuppress;
}

Bool routedev_currentDSCsuppression(void)
{
  return gfSuppressDeviceStateChange;
}

/* ---------------------------------------------------------------------- */
void routedev_setDLsuppression(Bool fSuppress)
{
  gfSuppressDLCreation = fSuppress;
}

Bool routedev_currentDLsuppression(void)
{
  return gfSuppressDLCreation;
}

/* ---------------------------------------------------------------------- */
void routedev_setAddObjectToDLCallback(
  Bool (*fn)(LISTOBJECT *lobj, void *priv_data, Bool *suppress),
  void *priv_data)
{
  gAddObjectToDLCallback = fn;
  gAddObjectToDLCallbackPrivData = priv_data;
}

/* ---------------------------------------------------------------------- */
static Bool route_suppress_setg(DL_STATE *page, int32 colorType, int32 options)
{
  if (! gfSuppressDeviceStateChange)
    return (device_table_setg[DEVICE_BAND])(page, colorType, options);
  else
    return (device_table_setg[DEVICE_NULL])(page, colorType, options);
}

/* ---------------------------------------------------------------------- */
static Bool route_suppress_gouraud(DL_STATE *page,
                                   SHADINGvertex *v1,
                                   SHADINGvertex *v2,
                                   SHADINGvertex *v3,
                                   SHADINGinfo *sinfo)
{
  if (! gfSuppressDLCreation)
    return (device_table_gouraud[DEVICE_BAND])(page, v1, v2, v3, sinfo);
  else
    return (device_table_gouraud[DEVICE_NULL])(page, v1, v2, v3, sinfo);
}

/* ---------------------------------------------------------------------- */
static int32 route_suppress_addobjecttodl(DL_STATE *page, LISTOBJECT * lobj)
{
  Bool fSuppressDLCreation = gfSuppressDLCreation;

  if (gAddObjectToDLCallback != NULL) {
    if ( !gAddObjectToDLCallback(lobj, gAddObjectToDLCallbackPrivData,
                                 &fSuppressDLCreation)) {
      return DL_Error;
    }
  }

  if (! fSuppressDLCreation)
    return (device_table_addtodl[DEVICE_BAND])(page, lobj);
  else
    return (device_table_addtodl[DEVICE_NULL])(page, lobj);
}

/* ---------------------------------------------------------------------- */
static Bool route_suppress_nbressdisplay(DL_STATE *page,
                                         int32 rule, NFILLOBJECT * nfill)
{
  if (! gfSuppressDLCreation)
    return (device_table_bressfill[DEVICE_BAND])(page, rule, nfill);
  else
    return (device_table_bressfill[DEVICE_NULL])(page, rule, nfill);
}

/* ---------------------------------------------------------------------- */
static Bool route_suppress_rectdisplay(DL_STATE *page, dbbox_t * therect)
{
  if (! gfSuppressDLCreation)
    return (device_table_rect[DEVICE_BAND])(page, therect);
  else
    return (device_table_rect[DEVICE_NULL])(page, therect);
}

/* ---------------------------------------------------------------------- */
static Bool route_suppress_chardisplay(DL_STATE *page,
                                       FORM * form, int32 x, int32 y)
{
  if (! gfSuppressDLCreation)
    return (device_table_char[DEVICE_BAND])(page, form, x, y);
  else
    return (device_table_char[DEVICE_NULL])(page, form, x, y);
}

/* ---------------------------------------------------------------------- */
static Bool route_suppress_image(DL_STATE *page,
                                 STACK *stack, IMAGEARGS *image)
{
  if (! gfSuppressDLCreation)
    return (device_table_image[DEVICE_BAND])(page, stack, image);
  else
    return (device_table_image[DEVICE_NULL])(page, stack, image);
}

/* ---------------------------------------------------------------------- */
static Bool route_suppress_celldisplay(DL_STATE *page, CELL *cell)
{
  if (! gfSuppressDLCreation)
    return (device_table_cell[DEVICE_BAND])(page, cell);
  else
    return (device_table_cell[DEVICE_NULL])(page, cell);
}

/* ---------------------------------------------------------------------- */
void init_C_globals_routedev(void)
{
  optional_content_on = TRUE;
  gfSuppressDeviceStateChange = FALSE;
  gfSuppressDLCreation = FALSE;
  gAddObjectToDLCallback = NULL;
  gAddObjectToDLCallbackPrivData = NULL;
  device_current_addtodl = NULL;
  device_current_bressfill = NULL;
  device_current_image = NULL;
  device_current_gouraud = NULL;
  device_current_rect = NULL;
  device_current_setg = NULL;
  device_current_char = NULL;
  device_current_cell = NULL;
}

/* ====================================================================== */

void gs_device_set(int32 devicetype)
{
  thegsDeviceType(*gstateptr) = devicetype;
  gs_freecliprec(&gstateptr->thePDEVinfo.initcliprec) ;
  device_current_addtodl = device_table_addtodl[devicetype];
  device_current_bressfill = device_table_bressfill[devicetype];
  device_current_image = device_table_image[devicetype];
  device_current_gouraud = device_table_gouraud[devicetype];
  device_current_rect = device_table_rect[devicetype];
  device_current_setg = device_table_setg[devicetype];
  device_current_char = device_table_char[devicetype];
  device_current_cell = device_table_cell[devicetype];
}

/*
 * Is the given device one of the possible flavours of banddevice.
 *
 * DEVICE_SUPPRESS was really a bit of a misnomer. It was created as an
 * independent device type, but really it is just an attribute of bandevice.
 * i.e. DEVICE_SUPPRESS is a suppressed band-device. Now want to add a second
 * attribute to bandevice, whether it errored or not during its creation.
 * Will again have to do it as another device type, which is getting a bit
 * messy. So try and centralise the tests for "are you really some flavour
 * of band device" to keep all the mess in one place. Have the function name
 * use 'bandtype' to try and differentiate between a type of band device and
 * the one that is explicitly labelled DEVICE_BAND.
 */
Bool dev_is_bandtype(int32 devicetype)
{
  return ( devicetype == DEVICE_BAND ||
           devicetype == DEVICE_SUPPRESS ||
           devicetype == DEVICE_ERRBAND );
}

/*
Log stripped */
