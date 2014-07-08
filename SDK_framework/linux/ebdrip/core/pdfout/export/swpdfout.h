/** \file
 * \ingroup pdfout
 *
 * $HopeName: SWpdf_out!export:swpdfout.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2000-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Exported hooks and routines for PDF output.
 */

#ifndef __SWPDFOUT_H__
#define __SWPDFOUT_H__


/* ----- External structures ----- */

struct OBJECT ;     /* from COREobjects */
struct IMAGEARGS ;  /* from SWv20 */
struct PDFCONTEXT ; /* from COREpdf_base */
struct PATHINFO ;   /* from SWv20/COREgstate */
struct CLIPRECORD ; /* from SWv20/COREgstate */
struct FILELIST ;   /* from SWv20/COREgstate */
struct STROKE_PARAMS ;  /* from SWv20 */
struct char_selector_t ; /* from SWv20/COREfonts */
struct core_init_fns ; /* from SWcore */

/** \defgroup pdfout PDF output.
    \ingroup pdf
    \{ */

/* ----- External global variables ----- */

extern int32 pdfout_debug ;

/* ----- Exported macros ----- */

/* ----- Exported functions ----- */

Bool pdfout_enabled(void) ;

void pdfout_C_globals(struct core_init_fns *fns) ;

/* Job. */

Bool pdfout_beginjob(corecontext_t *corecontext) ;
Bool pdfout_endjob(corecontext_t *corecontext) ;
Bool pdfout_setparams(struct PDFCONTEXT *pdfoc, struct OBJECT *paramdict) ;
Bool pdfout_endpage(struct PDFCONTEXT **pdfoc, Bool discard) ;
void  pdfout_seterror( struct PDFCONTEXT *pdfc, int32 error ) ;

/* Clipping. */

uint32 pdfout_pushclip( struct PDFCONTEXT *pdfoc, struct CLIPRECORD *newclip ) ;

/* Fills. */

Bool pdfout_dofill(struct PDFCONTEXT *pdfc,
                   struct PATHINFO *thepath, uint32 type, uint32 rectp,
                   int32 colorType);

/* Strokes. */

Bool pdfout_dostroke(struct PDFCONTEXT *pdfoc,
                     struct STROKE_PARAMS *params,
                     int32 colorType);

/* Text. */

Bool pdfout_outputchar( struct PDFCONTEXT *pdfoc,
                        struct char_selector_t *selector,
                        SYSTEMVALUE x, SYSTEMVALUE y,
                        SYSTEMVALUE wx, SYSTEMVALUE wy ) ;
Bool pdfout_begincharpath( struct PDFCONTEXT *pdfoc, struct PATHINFO *lpath ) ;
Bool pdfout_recordchar( struct PDFCONTEXT *pdfoc,
                        struct char_selector_t *selector,
                        SYSTEMVALUE x, SYSTEMVALUE y,
                        SYSTEMVALUE wx, SYSTEMVALUE wy,
                        int32 textop) ;
Bool pdfout_endcharpath(struct PDFCONTEXT *pdfoc, Bool result) ;

/* Characters. This is slightly tricky, in that a new marking context is
   only started in the case that a Type 3 character is used, and is not
   already in the cache. The textop interface to pdfout_recordchar and
   pdfout_charcached is a hack, to avoid problems with trying to output text
   ops from charpaths for Type 3 fonts (PDF 1.3 p.343). */

Bool pdfout_charcached(struct PDFCONTEXT* pdfc,       /* I */
                       int32 charpath,                /* I */
                       int32 *contextid,              /* O */
                       int32 *textop) ;               /* O */
Bool pdfout_beginchar(struct PDFCONTEXT** pdfoc, SYSTEMVALUE metrics[6],
                      Bool cached);
Bool pdfout_endchar(struct PDFCONTEXT** pdfoc,        /* I */
                    int32 *contextid,                 /* I/O */
                    Bool result);                    /* I */
#define PDFOUT_INVALID_CONTEXT (-1)

/* Images. */

Bool pdfout_beginimage(struct PDFCONTEXT* pdfc, int32 colorType,
                       struct IMAGEARGS* image, int32** idecodes);
Bool pdfout_endimage( struct PDFCONTEXT *pdfoc, int32 Height,
                      struct IMAGEARGS* image ) ;
Bool pdfout_imagedata( struct PDFCONTEXT *pdfoc, int32 nprocs,
                       uint8 *buf[], int32 len, Bool maskdata) ;

/* Patterns. */

Bool pdfout_patterncached(struct PDFCONTEXT* pdfc, int32 patternId);
Bool pdfout_beginpattern(struct PDFCONTEXT** pdfoc,
                          int32 patternId, struct OBJECT* poPatternS);
Bool pdfout_endpattern(struct PDFCONTEXT** pdfoc,
                       int32 patternId, struct OBJECT* poPatternS,
                       Bool fOkay);

/* Smooth shading. */

Bool pdfout_doshfill(struct PDFCONTEXT* pdfoc, struct OBJECT *poShadingS,
                     struct OBJECT *poDataSource);

void pdfout_endshfill(struct PDFCONTEXT* pdfc);

/* Vignette Candidates. */

Bool pdfout_vignettefill(struct PDFCONTEXT* pdfc,
                         struct PATHINFO*          thepath,
                         uint32             type,
                         uint32             rectp,
                         int32              colorType);
Bool pdfout_vignettestroke(struct PDFCONTEXT*    pdfc,
                           struct STROKE_PARAMS*        params,
                           int32                 colorType);
Bool pdfout_vignetteend(struct PDFCONTEXT* pdfc,
                        Bool fTreatAsVignette);

/* Stream filter callback. */

Bool pdfout_streamclosing( struct FILELIST *filter , int32 length ,
                           int32 checksum ) ;


/* for passing document info in from PS memory*/
Bool pdfout_UpdateInfoString(struct PDFCONTEXT* pdfc,
                             int32  keynum,
                             struct OBJECT* data) ;

Bool pdfout_annotation(struct PDFCONTEXT * pdfc,
                       struct OBJECT* contents,
                       struct OBJECT* open,
                       struct OBJECT* rect) ;

Bool pdfout_setcropbox(struct PDFCONTEXT * pdfc,
                       struct OBJECT* cropbox,
                       Bool fPages) ;

/* Harlequin specific commands passed from pdfmark */
Bool pdfout_Hqn_mark(struct PDFCONTEXT* pdfc,
                     int32 feature,
                     int32 name,
                     struct OBJECT* value) ;

/** \} */

/* ----------------------------------------------------------------------------
* Log stripped */
#endif /* protection for multiple inclusion */
