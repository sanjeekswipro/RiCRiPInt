/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!export:fonth.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Font compound external routines and handle definition. Include fonts.h if
 * you need access to the structure details, fontt.h if you just need the
 * typedefs for forward declarations.
 */

#ifndef __FONTH_H__
#define __FONTH_H__

#include "fontt.h"
#include "fontparam.h"

struct OBJECT ;    /* from COREobjects */
struct FONTinfo ;  /* from SWv20/COREgstate */
struct core_init_fns ;


/** Initialisation and finalisation routines for the fonts compound. These must
   be called before any other font compound routines. */
void fonts_C_globals(struct core_init_fns *fns) ;

/** Restoring a savelevel. Clean out any caches that may reference objects at
   that savelevel. This is called from the restore commit, the fonts module has
   no opportunity to prevent a restore from happening. */
void fonts_restore_commit(int32 savelevel) ;

/** Are we in a recursive context that might restrict font purging? */
extern int32 no_purge ;


/** Clear all the font caches. */
void font_caches_clear(corecontext_t *context);


/* Define PostScript VM structures for fonts based on TrueType or CFF fonts.
   These routines take a (possibly-NULL) parameter dictionary and a file
   object, and define fonts or CID fonts based on the file. The parameter
   dictionary contains the font name to select, in case the file represents a
   CFF font set or a TrueType collection. The font selected and defined is
   returned through the newfont object. If there is no font name in the
   parameter dictionary, then all fonts in a TTC or CFF will be defined. */
Bool tt_definefont(struct OBJECT *newfont, struct OBJECT *params,
                   struct OBJECT *file, Bool * found_3_1) ;
Bool cff_definefont(struct OBJECT *newfont, struct OBJECT *params,
                    struct OBJECT *file) ;
Bool otf_definefont(struct OBJECT *newfont, struct OBJECT *params,
                    struct OBJECT *file) ;

/* Enquiry function to determine if protected characters can be returned to
   the user in any way. The reason codes determine why the outline or
   character definition should be returned. */
enum { FONT_OVERRIDE_CHARPATH,
       FONT_OVERRIDE_HDLT } ;
Bool font_protection_override(uint8 protection, uint32 reason) ;

/** Predicate to determine if we're inside a charpath. */
Bool char_doing_charpath(void) ;

/** Predicate to determine if we're inside a cached character. */
Bool char_doing_cached(void) ;

/** Predicate to determine if we're inside any BuildChar character
    (immediately or not; this will return TRUE if we're inside a non-BuildChar
    which is inside a BuildChar). */
Bool char_doing_buildchar(void) ;

/** Return the current character context. */
/*@null@*/ /*@dependent@*/
charcontext_t *char_current_context(void) ;

/* COREfonts internal functions. The rest of these functions shouldn't be
   exported to the outside world; when font modularisation is complete, it
   will be hidden in COREfonts. */

/* Methods for different font types supported by the fonts module. These are
   installed in the gstate FONTinfo by set_font() or the set_cid_subfont(),
   and provide a common interface to lookup the definition of a character,
   select a CID sub-font, and start and finish with a character definition. */
extern font_methods_t font_invalid_fns ;  /* Font type is not set */
extern font_methods_t font_type1_ps_fns ; /* Type 1 in PostScript VM */
extern font_methods_t font_dld1_fns ;     /* DLD1 dynamically loaded Type 1 */
extern font_methods_t font_cff_fns ;      /* Type 2 non-CID CFF font */
extern font_methods_t font_type3_fns ;    /* BuildChar/BuildGlyph in VM */
extern font_methods_t font_truetype_fns ; /* TrueType from Type 42 or file */
extern font_methods_t font_cid0_fns ;     /* Type 1 in CID Type 0 */
extern font_methods_t font_cid0c_fns ;    /* Type 1/2 in CID CFF */
extern font_methods_t font_cid1_fns ;     /* BuildChar/BuildGlyph in VM */
extern font_methods_t font_cid2_fns ;     /* TrueType from Type 42 or file */
extern font_methods_t font_cid4_fns ;     /* Bitmap cache insertion */

#if 0
/* NYI character providers. These will be used in one of the coming rounds of
   the font re-write to provide a homogeneous interface to character
   interpretation and rendering. The DIDL-based providers can be used as
   fallbacks for any font definition that does not want to provide
   specialised rendering or interpretation. There is no default DIDL provider,
   each charstring interpreter needs to interpret the format itself. */

struct FONTinfo ; /* from SWv20/COREgstate */
struct FORM ;     /* from SWv20/CORErender */

Bool didl_metrics_provider(struct FONTinfo *fontinfo,
                           charcontext_t *context,
                           charstring_methods_t *charfns,
                           char_escapement_t *escapement,
                           sbbox_t *bbox) ;
Bool didl_outline_provider(struct FONTinfo *fontinfo,
                           charcontext_t *context,
                           charstring_methods_t *charfns,
                           char_escapement_t *escapement,
                           char_outline_t *outline) ;
Bool didl_raster_provider(struct FONTinfo *fontinfo,
                          charcontext_t *context,
                          charstring_methods_t *charfns,
                          char_escapement_t *escapement,
                          struct FORM **raster) ;
#endif

/*
Log stripped */
#endif /* protection for multiple inclusion */
