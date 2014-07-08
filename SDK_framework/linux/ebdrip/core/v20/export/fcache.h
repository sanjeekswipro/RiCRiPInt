/** \file
 * \ingroup fontcache
 *
 * $HopeName: SWv20!export:fcache.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file is part of the font subsystem interface,
 */

#ifndef __FCACHE_H__
#define __FCACHE_H__

/** \defgroup fontcache Font caching
    \ingroup fonts */
/** \{ */

#include "graphics.h"
#include "stacks.h"
#include "charsel.h"

struct charcontext_t ; /* from COREfonts */

/* --- Exported Constants --- */
/* flags for whether various bits of the metrics array are set or not
   at various times: corresponding to the 5 pairs of parameters to
   setcachedevice2 (or the first 6 of setcachedevice) */
#define MF_W0 0x003
#define MF_LL 0x00c
#define MF_UR 0x030
#define MF_W1 0x0c0
#define MF_VO 0x300

/* Decryption constants. */
#define DECRYPT_SEED 55665
#define DECRYPT_ADD  22719
#define DECRYPT_MULT 52845

/* --- Exported Macros --- */
/* Decryption macros. */
#define DECRYPT_BYTE(byte,state)          (uint8)( (byte) ^ ( (state) >> 8 ))
#define DECRYPT_CHANGE_STATE(byte,state,add,mul) MACRO_START \
 (state) = (uint16) ( (state) + (byte) ) ; \
 (state) = (uint16) ( (state) * (mul) ) ; \
 (state) = (uint16) ( (state) + (add) ) ; \
MACRO_END
#define ENCRYPT_BYTE(byte,state)          (uint8)( (byte) ^ ( (state) >> 8 ))
#define ENCRYPT_CHANGE_STATE(byte,state,add,mul) MACRO_START \
 (state) = (uint16) ( (state) + (byte) ) ; \
 (state) = (uint16) ( (state) * (mul) ) ; \
 (state) = (uint16) ( (state) + (add) ) ; \
MACRO_END

/* Computation of stringwidth from metrics array */
#define COMPUTE_STRINGWIDTH(metrics_, charcontext_) MACRO_START         \
  SYSTEMVALUE *_mm_ = (metrics_);                                       \
  MATRIX_TRANSFORM_DXY( _mm_[0], _mm_[1],                               \
                        (charcontext_)->xwidth,                         \
                        (charcontext_)->ywidth,                         \
                        & theFontMatrix( theIFontInfo( gstateptr ))) ;  \
MACRO_END

/* A safe call to the interpreter that ensures we do not purge the font
   cache */
#define NO_PURGE_INTERPRETER() MACRO_START \
  Bool _result_ ; \
  ++no_purge ; \
  _result_ = interpreter(1, NULL); \
  --no_purge ; \
  if (! _result_ ) \
    return FALSE; \
MACRO_END

/* Flags for plotchar() */
/* N.B. These flags form a bit field */
typedef enum {
  CHAR_NORMAL = 0,       /* Normal plotchar */
  CHAR_INVERSE = 1,      /* Do as inverse */
  CHAR_NO_SETG = 2,      /* Caller does not require DEVICE_SETG */
  CHAR_SETG_BLANK = 4,   /* Caller requires DEVICE_SETG for blank forms */
} CHAR_OPTIONS ;

/* --- Exported Functions --- */
Bool gs_setcharwidth( STACK *stack ) ;
Bool gs_setcachedevice( STACK *stack , int32 want_extra_args ) ;

USERVALUE fcache_flatness(DL_STATE *page) ;

void char_bearings(struct charcontext_t *charcontext,
                   SYSTEMVALUE offset[4], SYSTEMVALUE bbox[4],
                   OMATRIX *matrix) ;
Bool char_metrics(struct charcontext_t *charcontext,
                  SYSTEMVALUE metrics[10], int32 *mflags, SYSTEMVALUE bbox[4]) ;
CHARCACHE *char_cache(struct charcontext_t *charcontext,
                      SYSTEMVALUE metrics[10], int32 mflags,
                      SYSTEMVALUE offset[4], Bool blank) ;
Bool char_accurate(SYSTEMVALUE *glyphsize, uint32 *accurate) ;
Bool char_draw(struct charcontext_t *charcontext,
               LINELIST *currpt, CHARCACHE *cptr,
               SYSTEMVALUE metrics[10], int32 mflags,
               SYSTEMVALUE xoff, SYSTEMVALUE yoff,
               Bool blank, uint32 accurate,
               PATHINFO *cpath, OMATRIX *dtransform) ;

Bool plotchar(/*@notnull@*/ char_selector_t *selector,
              int32 showtype, int32 charCount,
              /*@notnull@*/
              Bool (*notdef_fn)(/*@notnull@*/ char_selector_t *selector,
                                int32 showtype, int32 charCount,
                                /*@out@*/ /*@notnull@*/ FVECTOR *advance,
                                /*@null@*/ void *notdef_data),
              /*@null@*/ void *notdef_data,
              /*@out@*/ /*@notnull@*/ FVECTOR *advance,
              CHAR_OPTIONS options) ;

Bool set_font(void);
Bool set_matrix(void);
Bool fillchardisplay(DL_STATE *page, FORM *tempf, int32 sx, int32 sy);
Bool bracket_plot(struct charcontext_t *charcontext, int32 *pgid);
Bool get_metrics( OBJECT *glyphname ,
                  SYSTEMVALUE mvalues[ 10 ] ,
                  int32 *mflags_p ) ;

/* Common routine to lookup CharStrings for a character definition, applying
   .notdef lookup if necessary */
Bool get_sdef(FONTinfo *fontInfo, OBJECT *glyphname, OBJECT *charstring) ;

/** \} */

#endif /* protection for multiple inclusion */

/*
Log stripped */
