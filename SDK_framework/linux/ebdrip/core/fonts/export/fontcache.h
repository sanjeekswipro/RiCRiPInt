/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!export:fontcache.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions and structures for the font cache interface. Some of these
 * functions will move into COREfonts private data when font modularisation
 * is complete.
 */

#ifndef __FONTCACHE_H__
#define __FONTCACHE_H__

#include "fontt.h"
#include "fontparam.h"

struct CHARCACHE ; /* Currently in SWv20, will move to COREfonts */
struct FONTinfo ;  /* from SWv20/COREgstate */
struct OBJECT ;    /* from COREobjects */
struct OMATRIX ;   /* from SWv20/COREgstate */


/* Default dynamic font cache limits */
#define BLIMIT          750000  /* Limits per 2 Meg of NonPageable VM. */
#define CLIMIT          7500
#define MLIMIT          750

#define LOWER_CACHE     256
#define UPPER_CACHE     32768

/* Create a new CHARCACHE entry, inserting it into the fontcache. The entry
   is keyed by glyphname. */
struct CHARCACHE *fontcache_new_char(struct FONTinfo *fontInfo,
                                     struct OBJECT *glyphname) ;

/* Set the lookup font in a FONTinfo structure. The FID variant searches by
   FID only, and will usually be used as a first test. The font variant
   matches UniqueIDs, PaintTypes, and other details. It may match different
   encodings of the same font. These functions return TRUE if the font entry
   was found.*/
Bool fontcache_lookup_fid(struct FONTinfo *fontInfo);
Bool fontcache_lookup_font(struct FONTinfo *fontInfo);

/* Set the lookup matrix in a FONTinfo structure. The type32 variant does not
   compare translational components of the matrix entry; they are not
   necessary to match the character. These routines return TRUE if the matrix
   entry was found. */
Bool fontcache_lookup_matrix(struct FONTinfo *fontInfo);
Bool fontcache_lookup_matrix_t32(struct FONTinfo *fontInfo);

/* HDLT sometimes needs the actual font matrix used in a lookup. */
struct OMATRIX *fontcache_current_matrix(struct FONTinfo *fontInfo) ;

/* Lookup a character in the font cache, using the lookup font and matrix set
   by the previous lookup routines. The WMode variant will prefer the cache
   form for the correct WMode, but will return the other WMode's cache if
   that one is not found. The Type32 master lookup will find the identity
   matrix form, as defined by the job. */
struct CHARCACHE *fontcache_lookup_char(struct FONTinfo *fontInfo,
                                        struct OBJECT *glyphname);
struct CHARCACHE *fontcache_lookup_char_wmode(struct FONTinfo *fontInfo,
                                              struct OBJECT *glyphname);
struct CHARCACHE *fontcache_lookup_char_t32(struct FONTinfo *fontInfo,
                                            struct OBJECT *glyphname);

/* Mark the fontcache entries after this savelevel as being restorable */
void fontcache_restore(int32 savelevel) ;

/* Note if new fontcache entries should be compressed */
Bool fontcache_is_compressing(void) ;

/* Font cache key functions for CID fonts (using selector CID) and base fonts
   (using Encoding lookup). These are used by various font method structures.
   They shouldn't be exported to the outside world; when font modularisation
   is complete, it will be hidden in COREfonts. */
Bool fontcache_cid_key(struct FONTinfo *fontInfo,
                       char_selector_t *selector,
                       charcontext_t *context) ;
Bool fontcache_base_key(struct FONTinfo *fontInfo,
                        char_selector_t *selector,
                        charcontext_t *context) ;


/** Clear the font cache.

  NB. Doesn't actually clear any glyphs used on the current page.
 */
void fontcache_clear(FONTSPARAMS *fontsparams);


/** Check if some limit is exceeded, and purge if so. */
void fontcache_check_limits(FONTSPARAMS *fontsparams);


/* Mark a font or glyph such that it cannot be referenced again and will be
   discarded. */
void fontcache_make_useless(int32 UniqueID, struct OBJECT *glyphname) ;

/* Remove fonts from the fontcache that cannot be referenced again. */
void fontcache_purge_useless(int32 erasenumber);

/* Returns how much memory is available if the fontcache were completely
   purged. */
size_t fontcache_available_memory(void);

/* When restoring global memory, we need to remove all name references from
   the fontcache. This makes the characters unreferencable, they will be
   purged after they are rendered. */
void fontcache_restore_names(int32 slevel) ;

/* Type 32 fonts manipulate the font cache directly. As well as adding
   entries, they need to remove entries. This routine deletes all of the
   characters in a range for a specified font, including Type 32 master
   characters. */
void fontcache_remove_chars(int32 fid, int32 firstcid, int32 lastcid) ;

/* Errors during construction may result in invalid cache forms. This function
   removes a character known to be the last entered into a font cache chain,
   freeing its forms and cache entry. The font info's lookup matrix is used
   to identify the matrix list. */
void fontcache_free_char(FONTSPARAMS *fontparams,
                         struct FONTinfo *fontInfo, struct CHARCACHE *cptr) ;

/* Allocate a new CHARCACHE structure and initialise to sensible defaults, i.e.
 * numbers to zero, pointers to NULL. */
struct CHARCACHE* alloc_ccache(void) ;

/* Raw function to free a charcache entry. This disposes of any forms or Type 32
   data hanging off it too. */
void free_ccache(struct CHARCACHE *cptr) ;

/* Do the characters come from the same font and matrix size? */
Bool chars_have_same_matrix(struct CHARCACHE *char1, struct CHARCACHE *char2) ;


struct core_init_fns; /* from SWcore */

void fontcache_C_globals(struct core_init_fns *fns);


/*
Log stripped */
#endif /* protection for multiple inclusion */
