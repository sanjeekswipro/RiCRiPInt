/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!export:routedev.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Indirection for sending low-level DL objects to different devices.
 */

#ifndef __ROUTEDEV_H__
#define __ROUTEDEV_H__

#include "bitbltt.h"  /* FORM */
#include "displayt.h" /* LISTOBJECT */
#include "graphics.h" /* thegsDeviceType */
#include "gstack.h"   /* gstateptr */
#include "ndisplay.h" /* NFILLOBJECT */
#include "imaget.h"   /* IMAGEARGS */
#include "fonth.h"    /* char_doing_charpath() */

struct STACK ; /* from COREobjects */
struct SHADINGvertex ; /* from SWv20 */
struct SHADINGinfo ; /* from SWv20 */
struct CELL ; /* from SWv20 */

/* Route low-level painting stuff to the current `device' (in the
   PostScript sense of the word) */

enum {
  DEVICE_ILLEGAL = -1,
  DEVICE_MIN = 0,
  DEVICE_NULL = DEVICE_MIN,
  DEVICE_BAND,
  DEVICE_CHAR,
  DEVICE_PATTERN1, /* PaintType 1 patterns: all colour */
  DEVICE_PATTERN2, /* PaintType 2 patterns: mask */
  DEVICE_SUPPRESS,
  DEVICE_ERRBAND,  /* A band device which errored during creation */
  DEVICE_MAX = DEVICE_ERRBAND
} ;

/* High level jump functions. */
extern Bool (*device_current_setg)(DL_STATE *page, int32 colorType, int32 options) ;
extern Bool (*device_table_setg[])(DL_STATE *page, int32 colorType, int32 options) ;

extern Bool (*device_current_image)(
  /*@notnull@*/ /*@in@*/                  DL_STATE *page ,
  /*@notnull@*/ /*@in@*/                  struct STACK *stack ,
  /*@notnull@*/ /*@in@*/                  IMAGEARGS *image ) ;
extern Bool (*device_table_image[])(
  /*@notnull@*/ /*@in@*/                  DL_STATE *page ,
  /*@notnull@*/ /*@in@*/                  struct STACK *stack ,
  /*@notnull@*/ /*@in@*/                  IMAGEARGS *image ) ;

extern Bool (*device_current_gouraud)(
  /*@notnull@*/ /*@in@*/                  DL_STATE *page ,
  /*@notnull@*/ /*@in@*/                  struct SHADINGvertex *v1 ,
  /*@notnull@*/ /*@in@*/                  struct SHADINGvertex *v2 ,
  /*@notnull@*/ /*@in@*/                  struct SHADINGvertex *v3 ,
  /*@notnull@*/ /*@in@*/                  struct SHADINGinfo *sinfo ) ;
extern Bool (*device_table_gouraud[])(
  /*@notnull@*/ /*@in@*/                  DL_STATE *page ,
  /*@notnull@*/ /*@in@*/                  struct SHADINGvertex *v1 ,
  /*@notnull@*/ /*@in@*/                  struct SHADINGvertex *v2 ,
  /*@notnull@*/ /*@in@*/                  struct SHADINGvertex *v3 ,
  /*@notnull@*/ /*@in@*/                  struct SHADINGinfo *sinfo ) ;

/* Low level jump functions. */

extern int32 (*device_current_addtodl)(
  /*@notnull@*/ /*@in@*/                  DL_STATE *page ,
  /*@notnull@*/ /*@in@*/                  LISTOBJECT *lobj);
extern int32 (*device_table_addtodl[])(
  /*@notnull@*/ /*@in@*/                  DL_STATE *page ,
  /*@notnull@*/ /*@in@*/                  LISTOBJECT *lobj);

extern Bool (*device_current_bressfill)(
  /*@notnull@*/ /*@in@*/                  DL_STATE *page ,
                                          int32 rule ,
  /*@notnull@*/ /*@in@*/ /*@only@*/       NFILLOBJECT *nfill ) ;
extern Bool (*device_table_bressfill[])(
  /*@notnull@*/ /*@in@*/                  DL_STATE *page ,
                                          int32 rule ,
  /*@notnull@*/ /*@in@*/ /*@only@*/       NFILLOBJECT *nfill ) ;

extern Bool (*device_current_rect)(
  /*@notnull@*/ /*@in@*/                  DL_STATE *page ,
  /*@notnull@*/ /*@in@*/                  dbbox_t *therect ) ;
extern Bool (*device_table_rect[])(
  /*@notnull@*/ /*@in@*/                  DL_STATE *page ,
  /*@notnull@*/ /*@in@*/                  dbbox_t *therect ) ;

extern Bool (*device_current_char)(
  /*@notnull@*/ /*@in@*/                  DL_STATE *page ,
  /*@notnull@*/ /*@in@*/ /*@only@*/       FORM *form ,
                                          int32 x ,
                                          int32 y ) ;
extern Bool (*device_table_char[])(
  /*@notnull@*/ /*@in@*/                  DL_STATE *page ,
  /*@notnull@*/ /*@in@*/ /*@only@*/       FORM *form ,
                                          int32 x ,
                                          int32 y ) ;

extern Bool (*device_current_cell) (
  /*@notnull@*/ /*@in@*/                  DL_STATE *page ,
  /*@notnull@*/ /*@in@*/                  struct CELL *cell) ;
extern Bool (*device_table_cell[]) (
  /*@notnull@*/ /*@in@*/                  DL_STATE *page ,
  /*@notnull@*/ /*@in@*/                  struct CELL *cell) ;

void gs_device_set(int32 devicetype) ;

Bool dev_is_bandtype(int32 devicetype) ;

#define SET_DEVICE(_devicenumber_) gs_device_set(_devicenumber_)

extern Bool optional_content_on;

#define CURRENT_DEVICE() (thegsDeviceType( *gstateptr ))

#define CURRENT_DEVICE_SUPPRESSES_MARKS()                      \
    ((thegsDeviceType( *gstateptr )) == DEVICE_NULL ||         \
     (thegsDeviceType( *gstateptr )) == DEVICE_ERRBAND ||      \
     ((thegsDeviceType( *gstateptr )) == DEVICE_SUPPRESS &&    \
      (routedev_currentDLsuppression())))

#define CURRENT_DEVICE_SUPPRESSES_STATE()                      \
    ((thegsDeviceType( *gstateptr )) == DEVICE_NULL ||         \
     (thegsDeviceType( *gstateptr )) == DEVICE_ERRBAND ||      \
     ((thegsDeviceType( *gstateptr )) == DEVICE_SUPPRESS &&    \
      (routedev_currentDSCsuppression())))

/* High level jump functions. */

/* Bit-mask options for DEVICE_SETG (none any more, but the mechanism is left in for future) */
enum {
  DEVICE_SETG_NORMAL = 0,       /* No options, use defaults */
  DEVICE_SETG_GROUP = 1,        /* Grouping structure, no StartPainting */
  DEVICE_SETG_RETRY = 2         /* Retrying setg for this object */
} ;
#define DEVICE_SETG(_page, _colorType, _options)         \
  ((*device_current_setg)(_page, _colorType, _options))

#define DEVICE_IMAGE(_page, _stack,_args) \
  ((*device_current_image)((_page), (_stack),(_args)))

#define DEVICE_GOURAUD(_page, _v1, _v2, _v3, _sinfo) \
  ((*device_current_gouraud)((_page), (_v1), (_v2), (_v3), (_sinfo)))

/* Low level jump functions. */

#define DEVICE_BRESSFILL(_page, _type_rule, _nfill) \
  ((*device_current_bressfill)((_page), (_type_rule),(_nfill)))

#define DEVICE_RECT(_page, _therect) \
  ((*device_current_rect)((_page), (_therect)))

#define DEVICE_DOCHAR(_page, _form,_x,_y) \
  ((*device_current_char)((_page), (_form),(_x),(_y)))

#define DEVICE_CELL(_page, _thecell)\
  ((*device_current_cell)((_page), (_thecell)))

/* Miscellaneous... */

/* Determine if a device context cannot set colours. The prototype for the
   char context inquiry function is here even though the function is defined
   in COREfonts, so that users of this macro won't have to include fonts.h. */
#define DEVICE_INVALID_CONTEXT() \
  ((CURRENT_DEVICE() == DEVICE_CHAR && !char_doing_charpath()) || \
   CURRENT_DEVICE() == DEVICE_PATTERN2)

/* Suppress device state change? */
void routedev_setDSCsuppression(Bool fSuppress);
Bool routedev_currentDSCsuppression(void);

/* Suppress DL creation? */
void routedev_setDLsuppression(Bool fSuppress);
Bool routedev_currentDLsuppression(void);

void routedev_setAddObjectToDLCallback(
  Bool (*fn)(LISTOBJECT *lobj, void *priv_data, Bool *suppress),
  void *priv_data);

#endif /* protection for multiple inclusion */

/*
Log stripped */
