/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!export:charsel.h(EBDSDK_P.1) $
 * $Id: export:charsel.h,v 1.10.9.1.1.1 2013/12/19 11:24:51 anon Exp $
 *
 * Copyright (C) 2000-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Char selector interface. This is part of the font subsystem interface, and
 * will move into COREfonts when that module is created.
 */

#ifndef __CHARSEL_H__
#define __CHARSEL_H__

#include "objects.h"
#include "fontt.h"
#include "cmap.h"

/*-----------------------------------------------------------------------------
  Character selectors; according to the PLRM3, a "character selector" is a
  name or a CID referring to a character in a leaf font. This structure
  encapsulates the notion of what a character is, where it came from, and
  its alternate representations. If the font is a CID font, then the cid
  field is the CID and the name field should be NULL. If the font is a base
  font, then either the cid field will be the character code and the name will
  be NULL, or the cid field will be -1 and the name be a valid NAMECACHE. */
struct char_selector_t {
  int32 cid ;             /* Either charcode or CID... (-1 if not valid) */
  NAMECACHE *name ;       /* ...or name. (NULL if not valid) */
  OBJECT string ;         /* String used to select base font character */
  OBJECT complete ;       /* Complete string used to select character */

  OBJECT font ;           /* Composite font leaf or base font. */

  OBJECT cmap ;           /* CMap is required for notdef lookups, PDF out */
  OBJECT parent ;         /* Parent font object which includes CMap.
                             Resolver function handles FDepVector and FDArray,
                             or PDF equivalents. */

  int32 index ;           /* Char index computed for (a)widthshow. PS only.
                             This may be better in the show context, however
                             it is required to be updated when the special
                             cshow or Type 3 CID rules apply, and so will
                             stay here for for ease of updating for now. */
  int32 type3cid ;        /* CID for special Type 3 replacements. I'm unhappy
                             about this being part of the selector too,
                             because it's really context information. I don't
                             want to change this right now though. */
  int32 pdf ;             /* Flag to indicate to tt_base_key that it should do
                             the special TT PDF lookup thing. */

  uint8 codes[CMAP_MAX_CODESPACE_LEN] ; /* Storage for CMap string mappings */
} ;

/*-----------------------------------------------------------------------------
  Char selector providers. These functions are called repeatedly to get
  character selectors (NAME/CID, Font tuples) from a source (usually a string
  from one of the show operator variants). The provider should return TRUE in
  all successful cases, including no more selectors. The provider should set
  the flag pointed at by the eod pointer to indicate whether the selector is
  valid or not.

  get_base_selector gets a character selector for a base font from a string.

  get_stored_selector gets a character selector from a mutable pointer to a
  stored selector.

  Other character selectors are specific to PS or PDF, and are defined locally
  in appropriate files. These global selector functions may be forked into PS
  and PDF versions if the show context information is separated from the
  selector structure. */
Bool char_base_selector(void *data, char_selector_t *selector, Bool *eod) ;
Bool char_ancestor_selector(void *data, char_selector_t *selector, Bool *eod) ;
Bool char_transitive_selector(void *data, char_selector_t *selector, Bool *eod) ;

typedef Bool (*char_selector_fn)(void *data, char_selector_t *selector,
                                 Bool *eod) ;

/*
Log stripped */
#endif /* protection for multiple inclusion */
