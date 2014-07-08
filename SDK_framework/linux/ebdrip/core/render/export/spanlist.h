/** \file
 * \ingroup scanconvert
 *
 * $HopeName: CORErender!export:spanlist.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions to manage spanlists; pairs of spans on a single scanline.
 * Spanlists can be accumulated, merged, read out, and spans can be clipped
 * against them. Spanlists may be used for thread termination (cap) merging,
 * RLE output, contone clipping. This is a part of the CORErender interface.
 */

#ifndef __SPANLIST_H__
#define __SPANLIST_H__

#include "bitbltt.h" /* dcoord, BITBLT_FUNCTION */

struct render_blit_t ;

/** Opaque type for a spanlist */
typedef struct spanlist_t spanlist_t ;

/** Exposed simple span type; this is useful for estimating the amount of
    storage needed for spans. */
typedef struct {
  dcoord left, right ;
} span_t ;

/** Return the amount of memory required to store a certain number of spans.

    \param nspans The number of spans for which the storage size will be
    returned.

    \return The size (in bytes) of storage to store \a nspans.
 */
size_t spanlist_size(uint32 nspans) ;

/** Given a block of memory, and a size in bytes, initialise a spanlist as
    large as is possible within that memory and return an opaque pointer to
    it.

    \param memory A pointer to a block of memory in which a spanlist will be
    stored. The block of memory must be suitably aligned to store pointers at
    the start.

    \param nbytes The size of the memory block allocated to storing spans in.

    \return A pointer to an initialised spanlist, prepared to take span
    insertions. The memory pointer can be cast to a \c spanlist_t if the
    returned spanlist pointer cannot be conveniently retained. If NULL is
    returned, the memory size was too small to store any spans.
 */
spanlist_t *spanlist_init(void *memory, size_t nbytes) ;

/** Reset a spanlist to its initial state (no spans).

    \param spanlist A pointer to the spanlist to reset. */
void spanlist_reset(spanlist_t *spanlist) ;

/** Return the number of spans stored in a spanlist.

    \param spanlist A pointer to the spanlist to count.

    \return The number of spans actually stored in spanlist. */
uint32 spanlist_count(const spanlist_t *spanlist) ;

/** Return a pointer to the memory just past the end of a spanlist. This
    may be used when a set of spanlists are compressed into a form.

    \param spanlist A pointer to the spanlist whose limit will be returned.

    \return A pointer to the memory immediately following this spanlist. */
void *spanlist_limit(const spanlist_t *spanlist) ;

/** Add a span to a spanlist. This function is most efficient if spans are
    added left to right.

    \param spanlist A pointer to the spanlist in which a span will be
    inserted.

    \param left The left end of the span to insert. This must be greater than
    \c MINDCOORD.

    \param right The right end of the span to insert. This must be greater than
    or equal to the left end coordinate.

    \return TRUE Returned if the spanlist has more space after the insertion.
    \return FALSE Returned if the spanlist is full after insertion. The
    insertion is always performed, however the spanlist must be compressed by
    merging or emptied before any more insertions or deletions can be made.
*/
Bool spanlist_insert(spanlist_t *spanlist, dcoord left, dcoord right) ;

/** Determines the number of spans in a bitmap line without actually
    inserting any spans.

    \param bitmap A pointer to the start of the bitmap line.

    \param w The width of the bitmap, in pixels.

    \return The number of spans in the line of bitmap. */
uint32 spanlist_bitmap_spans(const blit_t *bitmap, dcoord w) ;

/** Encode the given bitmap into spans, assuming the spanlist has been
    initialised already.

    \param spanlist A spanlist which will receive spans extracted from the
    bitmap.

    \param bitmap A pointer to the start of the bitmap line.

    \param w The width of the bitmap, in pixels.

    \retval TRUE Returned if all of the spans extracted from the bitmap were
    successfully inserted into the spanlist.

    \retval FALSE Returned if the spanlist was too small to insert spans
    extracted from the bitmap.
 */
Bool spanlist_from_bitmap(spanlist_t *spanlist, const blit_t *bitmap, dcoord w) ;

/** Remove a span from a spanlist.

    \param spanlist A pointer to the spanlist from which a span will be
    deleted.

    \param left The left end of the span to delete. This must be greater than
    \c MINDCOORD.

    \param right The right end of the span to delete. This must be greater
    than or equal to the left end coordinate.

    \retval TRUE Returned if the spanlist has more space after the deletion.
    \retval FALSE Returned if the spanlist is full after deletion. The
    deletion is always performed, however the spanlist must be compressed by
    merging or emptied before any more insertions or deletions can be
    made. */
Bool spanlist_delete(spanlist_t *spanlist, dcoord left, dcoord right) ;

/** Merge all of the abutting and overlapping spans in the spanlist.

    \param spanlist A pointer to the spanlist in which spans will be merged.

    \retval TRUE Returned if the spanlist has space for insertions or
    deletions after the merging process.

    \retval FALSE Returned if the spanlist is full after merging all spans.
    The spanlist should not be used for any more insertions or deletions. */
Bool spanlist_merge(spanlist_t *spanlist) ;

/** Return the spanlist as a sequence of left,right coordinate pairs. Spans
    may abut, but will not overlap in the output. Abutting spans may be
    merged by calling \c spanlist_merge() before this function.

    \param spanlist A pointer to the spanlist which is to be converted to
    coordinate pairs.

    \param coords A pointer to the address where output coordinate pairs will
    be stored. The address used to store the coordinate pairs may be the
    memory address originally passed to \c spanlist_init() if desired.

    \return The number of coordinate pairs stored in \a coords is returned.
 */
uint32 spanlist_to_dcoords(const spanlist_t *spanlist, dcoord *coords) ;

/** Call a function for each span in the spanlist. Spans may abut each other;
    use \c spanlist_merge() to join abutting spans if necessary before
    calling this function. The callback function is designed to be compatible
    with BITBLT_FUNCTIONs, so a suitable Y coordinate and render info are
    be passed directly to the iterator.

    \param spanlist A pointer to the spanlist to iterate.

    \param callback A \c BITBLT_FUNCTION compatible callback, which will be
    called for every span in the spanlist.

    \param rb A \c render_blit_t pointer which will be passed through to the
    \c callback function.

    \param y The y coordinate which will be passed through to the \c callback
    function.
*/
void spanlist_iterate(spanlist_t *spanlist,
                      BITBLT_FUNCTION callback,
                      struct render_blit_t *rb, dcoord y) ;

/** Call functions for each span or partial span intersecting the a
    coordinate range. The black callback function is called once for each
    span or part of a span in the range. The optional white callback function
    is called once for each gap or part of a gap in the range, if present.
    Spans may abut each other; use \c spanlist_merge() to join abutting spans
    if necessary before calling this function. The callback functions are
    designed to be compatible with BITBLT_FUNCTIONs, so a suitable Y
    coordinate and render info are passed directly to the clip iterator.

    \param spanlist A pointer to the spanlist to iterate.

    \param black A \c BITBLT_FUNCTION compatible callback, which will be
    called for every black (marked) span in the spanlist.

    \param white A \c BITBLT_FUNCTION compatible callback, which will be
    called for every white (unmarked) span in the spanlist.

    \param rb A \c render_blit_t pointer which will be passed through to the
    \a black and \a white callback functions.

    \param y The y coordinate which will be passed through to the \a black and
    \a white callback functions.

    \param left The minimum X coordinate which will be passed in spans to the
    \a black and \a white callback functions.

    \param right The maximum X coordinate which will be passed in spans to
    the \a black and \a white callback functions.

    \param xoffset The offset of the spans stored in \a spanlist relative to
    the \a left, \a right span. If the spanlist was generated by a clipping
    operation, it will have the X separation offset applied to it. This
    offset allows the caller to undo the effect of the offset before passing
    spans on to the blit \a black and \a white functions, which usually take
    the separation offset into effect themselves.
*/
void spanlist_intersecting(/*@notnull@*/ const spanlist_t *spanlist,
                           /*@notnull@*/ BITBLT_FUNCTION black,
                           /*@null@*/ BITBLT_FUNCTION white,
                           struct render_blit_t *rb,
                           dcoord y, dcoord left, dcoord right,
                           dcoord xoffset) ;

/** Clip spans in one a spanlist to span limits supplied in another list.

    \param dest A pointer to the spanlist which will be clipped.

    \param clipto A pointer to the spanlist which will be used to clip the \a
    dest spans.

    \retval TRUE Returned if all of the spans in \a dest were clipped
    successfully.

    \retval FALSE Returned if the destination spanlist ran out of space
    during the clipping operation. The destination spanlist is partially
    clipped, but is left in a consistent state so that the operation can be
    retried after either \c spanlist_merge(), or convertion to a bitmap.
 */
Bool spanlist_clipto(spanlist_t *dest, const spanlist_t *clipto) ;

/** Copy the spans from one spanlist to another.

    \param dest The spanlist to which spans are copied.

    \param src A spanlist from which to copy spans.

    \retval TRUE Returned if the spans were all copied from the source to
    the destination spanlist.

    \retval FALSE Returned if the destination spanlist did not have enough
    space to fit the source spans in.
*/
Bool spanlist_copy(spanlist_t *dest, const spanlist_t *src) ;

#if defined(ASSERT_BUILD)
/** Assert that a spanlist is well-formed.

    \param spanlist A pointer to the spanlist to check for validity. */
void spanlist_assert_valid(spanlist_t *spanlist) ;

/** Unit test asserts that spanlist functions work correctly */
void spanlist_unit_test(void) ;
#else
#define spanlist_assert_valid(s_) EMPTY_STATEMENT()
#define spanlist_unit_test() EMPTY_STATEMENT()
#endif

/* Log stripped */
#endif
