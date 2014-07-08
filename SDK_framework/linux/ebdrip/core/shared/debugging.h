/** \file
 * \ingroup debug
 *
 * $HopeName: SWcore!shared:debugging.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Debugging functions to print structures and objects to the monitor.
 *
 * Do NOT include any other header files in this one, and do NOT make it depend
 * on any headers except core.h.
 */

#ifndef __DEBUGGING_H__
#define __DEBUGGING_H__

#if defined(DEBUG_BUILD)

struct FILELIST ;   /* from COREfileio */
struct OBJECT ;     /* from COREobjects */
struct STACK ;      /* from COREobjects */
struct dl_color_t ; /* from SWv20 */
struct color_entry_t; /*  from SWv20 */
struct p_ncolor_t_opaque ; /* from SWv20 */
struct GUCR_COLORANTSET ; /* from SWv20 */
struct GUCR_COLORANT ; /* from SWv20 */
struct GUCR_CHANNEL ; /* from SWv20 */
struct GUCR_SHEET ; /* from SWv20 */
struct GUCR_RASTERSTYLE ; /* from SWv20 */
struct PATHINFO ; /* from SWv20 */
struct SHADINGvertex ; /* from SWv20 */
struct SHADINGinfo ; /* from SWv20 */
struct NFILLOBJECT ; /* from SWv20 */
struct rcba_color_t ; /* from SWv20 */
struct LISTOBJECT ;   /* from SWv20 */
struct FORM ; /* from SWv20 */
struct DL_STATE ; /* from SWv20 */
struct IM_STORE ; /* from SWv20 */
struct BackdropTable ; /* from SWv20 */
struct GUCR_PHOTOINK_INFO ; /* from gstate */
struct DEVICEPARAM ; /* from COREinterface */

/**
 * \defgroup debug Debugging assistance functions.
 * \ingroup core
 * \{
 */

/** Capacity of debug output buffers. */
#define DEBUG_BUFFER_SIZE 4000

/** Debug buffer type. Debug buffers allow multiple monitorf() calls to
    be accumulated before writing out, which makes inter-thread interactions
    easier to see. */
typedef struct debug_buffer_t {
  size_t count ;   /**< Number of characters in buffer. */
  uint8 chars[DEBUG_BUFFER_SIZE] ; /**< Characters in buffer. */
} debug_buffer_t ;

/** Debug version of monitorf(). This function adds to the debug buffer,
    writing as many whole lines at a time as it can to the monitor if the
    buffer overflows. It always consumes all of the format. */
void dmonitorf(debug_buffer_t *buffer, char *format, ...) ;

/** Flush any remainder in debug buffer to the monitor. */
void dflush(debug_buffer_t *buffer) ;

/** Print object recursively. */
void debug_print_object(struct OBJECT *theo) ;

/** Print object recursively with a prefix and postfix. Indent depth is
    derived from the prefix. */
void debug_print_object_indented(struct OBJECT *theo, char *pre, char *post,
                                 struct FILELIST *flptr) ;

/** Print objects in a stack. */
void debug_print_stack(struct STACK *stack) ;

/** Print a single FILELIST. */
void debug_print_file(struct FILELIST *flptr) ;

/** Print all files in a list. */
void debug_print_filelist(struct FILELIST *flptr) ;

/** Print all PostScript files. */
void debug_print_psfiles(void) ;

/** Print an color_entry structure. */
void debug_print_ce(struct color_entry_t *pce, Bool verbose);

/** Print an ncolor structure. */
void debug_print_ncolor(struct p_ncolor_t_opaque *pncolor) ;

/** Print an unpacked DL color. */
void debug_print_dlc(struct dl_color_t *pdlc) ;

/** Get string representation of an interleaving style. */
char *debug_get_interleavingstyle(int32 style) ;

/** Get string representation of a separation style. */
char *debug_get_separationstyle(int32 style) ;

/** Get string representation of a colorant type. */
char *debug_get_coloranttype(int32 type) ;

/** Get string representation of trapping special handling. */
char *debug_get_specialhandling(int32 type) ;

/** Get string representation of render properties. */
char *debug_get_renderprops(uint32 props) ;

/** Get string representation of a device color space id. */
char *debug_get_devicespace(int32 id) ;

/** Get string representation of a colorspace. */
char *debug_get_colorspace(int32 id) ;

/** Get string representation of PCL pattern colors. */
const char *debug_string_pclPatternColors(int pc) ;

/** Get string representation of PCL xor state. */
const char *debug_string_pclXORState(int pc) ;

/** Print a rasterstyle colorant set. */
void debug_print_gucr_colorantset(struct GUCR_COLORANTSET *set) ;

/** Print a rasterstyle colorant. */
void debug_print_gucr_colorant(struct GUCR_COLORANT *colorant, int32 indent) ;

/** Print a rasterstyle channel. */
void debug_print_gucr_channel(struct GUCR_CHANNEL *channel, int32 indent) ;

/** Print a rasterstyle sheet. */
void debug_print_gucr_sheet(struct GUCR_SHEET *sheet, int32 indent) ;

/** Print a rasterstyle. */
void debug_print_gucr_rasterstyle(struct GUCR_RASTERSTYLE *hr, Bool underlying) ;

/** Print a path. */
void debug_print_path(struct PATHINFO *path) ;

/** Print a shfill vertex. */
void debug_print_vertex(struct SHADINGvertex *v, struct SHADINGinfo *i) ;

/** Print an NFILL (active edge table) structure. */
void debug_print_nfill(struct NFILLOBJECT *nfill, int32 vlevel);

/** Print an quad (2, 3, or 4 edge restricted fill). */
void debug_print_quad(uint32 quad, const dbbox_t *bbox);

/** Print a vignette. */
void debug_print_vignette(struct LISTOBJECT *lobj);

/** Print a recombine color. */
void debug_print_rcba_color(struct rcba_color_t *rcbacolor) ;

/** Print recombine info for a LISTOBJECT. */
void debug_print_rcba_info(struct rcba_color_t *rcbacolor,
                           struct LISTOBJECT *lobj) ;

/** Print a form. */
void debug_print_form(struct FORM *form) ;

/** Print a display list. */
void debug_print_dl(const struct DL_STATE *page, int level) ;

/** Print the tables in a backdrop region. */
void debug_print_bd(struct IM_STORE *ims, dcoord x1, dcoord y1, dcoord x2,
                    dcoord y2) ;

/** Print a single backdrop table. */
void debug_print_bd_table(struct BackdropTable *bdt, int32 nComps,
                          uint32 indent) ;

void debug_print_gucr_photoink(struct GUCR_PHOTOINK_INFO *photoink, int32 indent) ;

/** Output a file with a Graphviz dot format graph of the current tasks. This
    should be only called when the task mutex is not locked. */
void debug_graph_tasks(void) ;

/** Output a file with a Graphviz dot format graph of the current tasks. This
    should be only called when the task mutex is locked. */
void debug_graph_tasks_locked(void) ;

/** Print a device param name and value. */
void debug_print_deviceparam(struct DEVICEPARAM *param) ;

/** Print a device param name and value with a prefix and postfix. Indent
    depth is derived from the prefix. */
void debug_print_deviceparam_indented(struct DEVICEPARAM *param,
                                      char *pre, char *post) ;

/** Print the current Timeline hierarchy */
void debug_print_timelines(int32 tl) ;

/** \} */

#endif /* DEBUG_BUILD */

/*
Log stripped */
#endif /* Protection from multiple inclusion */
