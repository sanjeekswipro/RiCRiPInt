/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!export:xps.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * XPS initialise and finish interface.
 */

#ifndef __XPS_H__
#define __XPS_H__

/**
 * \defgroup xps XPS
 * \ingroup core
 */

#include "objects.h"

struct core_init_fns ; /* from SWcore */

typedef struct XPSPARAMS {
  OBJECT    ImageGrayProfile;
  OBJECT    ImageRGBProfile;
  OBJECT    ImageScRGBProfile;
  OBJECT    ImageCMYKProfile;
  OBJECT    Image3ChannelProfile;
  OBJECT    Image4ChannelProfile;
  OBJECT    Image5to8ChannelProfile;
} XPSPARAMS ;

/** Compound runtime initialisation */
void xps_C_globals(struct core_init_fns *fns);

/** \brief Valid type values for xps_normalise_name. */
enum {
  XPS_NORMALISE_PARTNAME,
  XPS_NORMALISE_ZIPNAME,
  XPS_NORMALISE_PARTREFERENCE,
  XPS_NORMALISE_EXTENSION
} ;

/**
 * \brief Helper function to perform part XPS part name grammar checking.
 *
 * This function is exported so that the ZIP device can check to see
 * if a ZIP item ought to be ignored or is part of the XPS open
 * package.
 *
 * \param in Pointer to an input string.
 * \param inlen Length of input string.
 * \param type A value from the XPS_NORMALISE_* enum indicating the type of
 *             the input string.
 *
 * \returns TRUE/FALSE result indicating whether the string adheres to
 * the XPS part naming grammar.
 */
Bool xps_validate_partname_grammar(uint8 *in, uint32 inlen,
                                   uint32 type) ;

/**
 * \brief
 * Purge the xps icc cache.
 *
 * \param[in] slevel Save level to purge down to.
 */
void xps_icc_cache_purge(int32 slevel);

/* ============================================================================
* Log stripped */
#endif
