/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!export:uniqueid.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Font UniqueID range definitions
 */

#ifndef __UNIQUEID_H__
#define __UNIQUEID_H__

/* -------------------------------------------------------------------------- */
/* UniqueIDs are defined to be 24bit. We support 32bit IDs with the top byte
   indicating ownership of that 24bit range. The values for this enum are
   therefore in the range -127 to +128:
*/
enum {
                        /* Ranges -127 to -2 are available */
  UID_RANGE_temp = -1,  /* IDs allocated by getnewuniqueid and discarded at
                           the end of the job. Note: uid=-1 must be avoided.
                           UID_RANGE_temp *MUST* be -1 - see fontcache.c */
  UID_RANGE_normal = 0, /* The normal range of IDs found in fonts */
                        /* Ranges 1 to 125 are available */
  UID_RANGE_XPS  = 126, /* XPS fonts */
  UID_RANGE_PFIN = 127, /* PFIN's fonts */
} ;

/* -----------------------------------------------------------------------------
Log stripped */
#endif /* __UNIQUEID_H__ */
