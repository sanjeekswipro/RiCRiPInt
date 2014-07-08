/** \file
 * \ingroup filters
 *
 * $HopeName: COREfileio!export:filterinfo.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interface functions for image filters to provide filter info for image
 * context operations.
 *
 * The imagefilter decode info interface allows callbacks to be registered to
 * extract well-known and image specific information from image filters. The
 * callbacks are registered by passing an imagefilter_match_t list into the
 * filter's FILELIST_DECODEINFO function, through theIFilterDecodeInfo().
 * The imagefilter_match_t list contains a key object, the callback to be
 * called, and a generic data pointer for the callback. The callback function
 * can return one of three values to indicate failure, a partial success where
 * the callee still wants more information from the image, and a short-circuit
 * success indicating the callee has all the details it wants about the image.
 *
 * Well-known keys are used for information that is common across many image
 * formats. They key names are the same as those used to set up image
 * dictionaries, where possible. Well-known key names currently supported
 * are:
 *
 * /BitsPerComponent  (integer)
 * /ColorSpace        (name or array)
 * /DataSource        (may be an array)
 * /Decode            (will be an array)
 * /Height            (integer)
 * /ImageMatrix       (will be an array; expected to be normal PS image matrix)
 * /ImageType         (1, 3, or 12 for normal, masked or alpha channel images)
 * /InterleaveType    (for alpha channel images)
 * /MultipleDataSources (boolean)
 * /PreMult           (boolean; indicates if alpha values are pre-multiplied)
 * /Width             (integer)
 * /XResolution       (real number in dpi)
 * /YResolution       (real number in dpi)
 *
 * /ICCProfile will be added to the supported list sometime, because it is
 * sufficiently common in image formats (JPEG, PNG, TIFF at least).
 *
 * Image filter info functions do not need to return values for all
 * well-known tags (such as InterleaveType and PreMult if alpha channel data
 * isn't supported), but if they don't, some callers may be inefficient and
 * keep asking for data.
 *
 * Image-specific information could include raw tag values, allowing extra
 * functionality to be built into PostScript procsets. A callback interface
 * is used because some image formats support multiple instances for some tag
 * types.
 *
 * The callback function MUST copy any non-simple objects into appropriate
 * object memory, rather than take references to them; the object values
 * passed may be temporary stack variables.
 */

#ifndef __FILTERINFO_H__
#define __FILTERINFO_H__

#include "fileioh.h"

struct OBJECT ; /* from COREobjects */

/** \brief Return values for imagefilter_callback_fn. */
enum {
  IMAGEFILTER_MATCH_FAIL = 0, /**< Image filter info error (must be zero). */
  IMAGEFILTER_MATCH_DONE,     /**< Image filter can return early. */
  IMAGEFILTER_MATCH_MORE      /**< Image filter should scan for more info. */
} ;
/** \brief Image filter decode info callback function type. */
typedef int32 (*imagefilter_callback_fn)(
  /*@notnull@*/ /*@in@*/                 imagefilter_match_t *match,
  /*@notnull@*/ /*@in@*/                 struct OBJECT *value) ;

/** Image filter matching structure. */
struct imagefilter_match_t {
  OBJECT key ;                       /**< Match key is ONAME or OSTRING. */
  /*@dependent@*/
  imagefilter_callback_fn callback ; /**< Function to call for match. */
  /*@dependent@*/ void *data ;       /**< Callback-specific data. */
  /*@dependent@*/ struct imagefilter_match_t *next ; /**< Next match. */
} ;


/** \brief Find the imagecontext match for a particular key in a list. */
imagefilter_match_t *filter_info_match(
  /*@notnull@*/ /*@in@*/               struct OBJECT *key,
  /*@null@*/ /*@in@*/                  imagefilter_match_t *list) ;

/** Generic filter decode callback, used by multiple filters. Note that the
    return value IS NOT the normal success/failure value; this routine
    returns TRUE if the calling routine should exit, but the return value
    should be the value of the done parameter to indicate success/failure. */
Bool filter_info_callback(/*@null@*/ /*@in@*/ imagefilter_match_t *list,
                          int32 namenum,
                          /*@notnull@*/ /*@in@*/ struct OBJECT *value,
                          /*@notnull@*/ /*@in@*/ Bool *done) ;

/** Filter decode callback for Matrices. This callback constructs a matrix
    object and calls the callback function for /ImageMatrix in the filter
    match list (if one exists). Note that the return value IS NOT the normal
    success/failure value; this routine returns TRUE if the calling routine
    should exit, but the return value should be the value of the done
    parameter to indicate success/failure. */
Bool filter_info_ImageMatrix(/*@null@*/ /*@in@*/ imagefilter_match_t *list,
                             SYSTEMVALUE m00, SYSTEMVALUE m01,
                             SYSTEMVALUE m10, SYSTEMVALUE m11,
                             SYSTEMVALUE m20, SYSTEMVALUE m21,
                             /*@notnull@*/ /*@in@*/ Bool *done) ;

/** Filter decode callback for Decode arrays. This callback constructs a
    Decode array object and calls the callback function for /Decode in the
    filter match list (if one exists). The array is a number of copies of
    d0,d1 pairs. Note that the return value IS NOT the normal success/failure
    value; this routine returns TRUE if the calling routine should exit, but
    the return value should be the value of the done parameter to indicate
    success/failure. */
Bool filter_info_Decode(/*@null@*/ /*@in@*/ imagefilter_match_t *list,
                        uint32 ncomps, USERVALUE d0, USERVALUE d1,
                        /*@notnull@*/ /*@in@*/ Bool *done) ;

/** Filter decode callback for Lab colourspace and Decode. This callback
    constructs a Decode array object and calls the callback function for
    /Decode in the filter match list (if one exists), then it constructs a
    ColorSpace array object with an Lab dictionary, and calls the callback
    function for /ColorSpace in the filter match list. Note that the return
    value IS NOT the normal success/failure value; this routine returns TRUE
    if the calling routine should exit, but the return value should be the
    value of the done parameter to indicate success/failure. */
Bool filter_info_Lab_CSD(imagefilter_match_t *match,
                         USERVALUE wp_x, USERVALUE wp_y, USERVALUE wp_z,
                         int32 amin, int32 amax, int32 bmin, int32 bmax,
                         Bool *done) ;

/*
Log stripped */
#endif /* Protection from multiple inclusion */
