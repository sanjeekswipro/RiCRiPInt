/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!export:tt_sw.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1996-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Define an interface that is included on both sides of the SW/TrueType
 * fence that refers to only types defined by the compiler or built up here.
 */

#ifndef __TT_SW_H__
#define __TT_SW_H__

/* ========================================================================== */
/* Calls from SWv20 to SWTrueType */

struct core_init_fns ; /* from SWcore */

void tt_C_globals(struct core_init_fns *fns) ;

/** \brief
 * Build an outline for a TrueType character. Outlines are passed back to
 * the client through callbacks.
 *
 * \param[in] data
 *   Opaque pointer passed in for access to frames of data from the TT font.
 * \param[in] open_frame
 *   A function to open a frame of data for access by the TT code. The frame
 *   should be uint16-aligned. Multiple frames may be open simultaneously.
 * \param[in] close_frame
 *   A function to close a frame of data previously opened by the TT code.
 * \param[in] fontid
 *   An identifier used by the core to refer to the current TT font.
 * \param[in] local_index
 *   The glyph index of the character to access from the font.
 * \param[in] transform
 *   The transformation matrix used to generate the outline.
 * \param[in] wmode
 *   The writing mode used to select character metrics (0 = horizontal,
 *   1=vertical).
 * \return
 *   \c TRUE if successful, \c FALSE otherwise. Note the return type is int,
 *   because this is included by SWTrueType, which doesn't have access to the
 *   core Bool definition.
 */
int tt_do_char(void *data,
               uint8 *(*open_frame)(void *data, uint32 offset, uint32 length),
               void (*close_frame)(void *data, uint8 **frame),
               int fontid, int local_index, double transform[6],
               int wmode);

/** \brief
 * Inform the TrueType renderer that a particular font will no longer be
 * needed, and resources allocated for it can be released.
 *
 * \param[in] fontid
 *   The font ID of the font to release.
 */
void tt_clear_font(int fontid) ;

/** \brief
 * Ask the TrueType interpreter to adjust the scale of a transformation matrix
 * so that the values generated internally fit in the bounds of its fixed-point
 * arithmetic. The transform matrix will be passed to \c tt_do_char, the scale
 * factor will be used to re-scale the outlines generated.
 *
 * \param[out] tt_scale
 *   The inverse of the scale factor applied to the transformation. This will
 *   be 1.0 if no adjustment is performed.
 * \param[in, out] transform
 *   The transformation matrix to adjust.
 * \param[in] unitsPerEm
 *   The number of units per em in the TT font. This affects the maximum number
 *   of units in the scaled contour.
 */
void tt_adjust_transform(double *tt_scale, double transform[6], double unitsPerEm);

/* ========================================================================== */
/* Calls from SWTrueType to SWv20 */
/* -------------------------------------------------------------------------- */
/* TrueTypeHints FontParam interface */

/** \brief
 * Called from SWTrueType to determine whether hinting errors should be handled
 * or allowed to fail.
 *
 * \return
 *   \c TRUE if hinting errors should be handled, \c FALSE otherwise.
 *   Type is int because SWTrueType doesn't have the core Bool type.
 */
int tt_handle_hinting_errors(void);

/** \brief
 * Called from SWTrueType to determine whether hinting is disabled.
 *
 * \return
 *   \c TRUE if hinting is disabled, \c FALSE if it is enabled.
 *   Type is int because SWTrueType doesn't have the core Bool type.
 */
int tt_no_hinting(void);

/** \brief
 * Called from SWTrueType to determine whether the TrueTypeHints FontParams is
 * set to SafeFaults, and if so report the fact that invalid hinting is being
 * ignored.
 *
 * \param[in] report
 *   Whether to output the warning message when TrueTypeHints is SafeFaults.
 *
 * \return
 *   \c TRUE if TrueTypeHints is SafeFaults, \c FALSE otherwise.
 *
 * Types are int because SWTrueType doesn't have the core Bool type.
 */
int tt_safe_hinting_errors(int report);

/** \brief
 * Called from SWTrueType to report that a hinting error has occurred.
 */
void tt_report_error_during_hinting(void);

/* -------------------------------------------------------------------------- */
/* Memory interface */

/** \brief
 * Service requests from SWTrueType for memory allocation.
 *
 * \param[in] size
 *   The number of bytes to allocate.
 * \return
 *   A pointer to the memory allocated, NULL if the allocation failed.
 */
void *tt_malloc(unsigned int size);

/** \brief
 * Service requests from SWTrueType to free previously allocated memory.
 *
 * \param[in] ptr
 *   A pointer to the memory previously allocated.
 */
void tt_free(void *ptr);

/* -------------------------------------------------------------------------- */
/* GlyphDirectory interface */

/** \brief
 * Predicate to determine if a GlyphDirectory (for incrementally-downloaded
 * fonts) exists in the font dictionary.
 *
 * \return
 *   \c TRUE if a glyph directory exists, \c FALSE otherwise. Note the return
 *   type is int, because this is included by SWTrueType, which doesn't have
 *   access to all the core Bool definition.
 */
int tt_check_glyphdir_exists(void);

/** \brief
 * Look up a TrueType glyph ID from an incrementally-downloaded GlyphDirectory.
 *
 * \param[in] index
 *   The Glyph ID of the character.
 * \param[out] gdata
 *   The glyph program.
 * \param[out] glen
 *   The length of the glyph program.
 * \param[out] width
 *   The horizontal advance width of the glyph, in EM space. This is set to a
 *   negative value if metrics are not supplied in the glyph description.
 * \param[out] left
 *   The left sidebearing of the glyph, in EM space. This should not be used
 *   if \c width is negative.
 * \param[out] height
 *   The advance height of the glyph for vertical writing, in EM space. This
 *   is set to a negative value if vertical writing metrics are not supplied
 *   in the glyph description.
 * \param[out] top
 *   The top sidebearing of the glyph for vertical writing, in EM space. This
 *   should not be used if \c height is negative.
 * \return
 *   \c TRUE if the character was found and extracted, \c FALSE otherwise.
 *   Note the return type is int, because this is included by SWTrueType,
 *   which doesn't have access to the core Bool definition.
 */
int tt_glyphdir_lookup(int32 index, uint8 **gdata, int32 *glen,
                       int32 *width, int32 *left, int32 *height, int32 *top);

/* -------------------------------------------------------------------------- */
/* Glyph outline interface */

/** \brief
 * Callback from the TrueType renderer to start a new character outline.
 *
 * \param[in] wx
 *   The X component of the advance width of the character, in EM space.
 * \param[in] wy
 *   The Y component of the advance width of the character, in EM space.
 * \param[in] minx
 *   The smallest X coordinate in the character, in EM space.
 * \param[in] maxx
 *   The largest X coordinate in the character, in EM space.
 * \param[in] miny
 *   The smallest Y coordinate in the character, in EM space.
 * \param[in] maxy
 *   The largest Y coordinate in the character, in EM space.
 */
void tt_open_outline(int32 wx, int32 wy, int32 minx, int32 maxx, int32 miny, int32 maxy);

/** \brief
 * Callback from the TrueType renderer to start a contour within a character
 * outline.
 *
 * \param[in] x
 *   The contour's start point X component.
 * \param[in] y
 *   The contour's start point Y component.
 * \param[in] outside
 *   A flag indicating if this is an outside (anti-clockwise) or inside
 *   (clockwise) contour. This is a true value if the contour is anti-clockwise.
 * \return
 *   \c TRUE if the contour was started successfully, \c FALSE otherwise. Note
 *   the return type is int, because this is included by SWTrueType, which
 *   doesn't have access to the core Bool definition.
 */
int tt_start_contour(int32 x, int32 y, int outside);

/** \brief
 * Callback from the TrueType renderer to add a line to a contour.
 *
 * \param[in] x
 *   The X component of the point to connect with a line.
 * \param[in] y
 *   The Y component of the point to connect with a line.
 * \return
 *   \c TRUE if the line was added successfully, \c FALSE otherwise. Note
 *   the return type is int, because this is included by SWTrueType, which
 *   doesn't have access to the core Bool definition.
 */
int tt_line_to(int32 x, int32 y);

/** \brief
 * Callback from the TrueType renderer to add a curve to a contour.
 *
 * \param[in] x1
 *   The X component of the first control point of the curve.
 * \param[in] y1
 *   The Y component of the first control point of the curve.
 * \param[in] x2
 *   The X component of the second control point of the curve.
 * \param[in] y2
 *   The Y component of the second control point of the curve.
 * \param[in] x3
 *   The X component of the final control point of the curve.
 * \param[in] y3
 *   The Y component of the final control point of the curve.
 * \return
 *   \c TRUE if the curve was added successfully, \c FALSE otherwise. Note
 *   the return type is int, because this is included by SWTrueType, which
 *   doesn't have access to the core Bool definition.
 */
int tt_curve_to(int32 x1, int32 y1, int32 x2, int32 y2, int32 x3, int32 y3);

/** \brief
 * Callback from the TrueType renderer finish a contour within an outline.
 *
 * \return
 *   \c TRUE if the contour was closed successfully, \c FALSE otherwise. Note
 *   the return type is int, because this is included by SWTrueType, which
 *   doesn't have access to the core Bool definition.
 */
int tt_close_contour(void);

/** \brief
 * Callback from the TrueType renderer finish an outline.
 *
 * \return
 *   \c TRUE if the outline was closed successfully, \c FALSE otherwise. Note
 *   the return type is int, because this is included by SWTrueType, which
 *   doesn't have access to the core Bool definition.
 */
int tt_close_outline(void);

/* ========================================================================== */
/* Variables shared between SWTrueType and SWv20 */
extern int tt_global_error;     /**< last error code from sp_report_error */

/* Debugging interface */
#if defined(DEBUG_BUILD) || defined(ASSERT_BUILD)
enum {
  DEBUG_TT_FAILURES = 1,
  DEBUG_TT_INFO = 2,
  DEBUG_TT_FRAMES = 4,
  DEBUG_TT_PICTURE = 8
} ;

extern int32 debug_tt ;
#endif

/*
* Log stripped */
#endif
