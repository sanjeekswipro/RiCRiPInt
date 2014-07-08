/** \file
 * \ingroup cid
 *
 * $HopeName: COREfonts!export:cidfont.h(EBDSDK_P.1) $   
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Definitions for CID font interpretation
 */

#ifndef __CIDFONT_H__
#define __CIDFONT_H__

/** \defgroup cid CID Font interpretation
    \ingroup fonts */
/** \{ */

/* CID types 0,1,2,4 correspond to fonttypes 9,10,11,32. */
#define FONT_IS_CID(_ftype) \
  ( ((_ftype) >= CIDFONTTYPE0 && (_ftype) <= CIDFONTTYPE2) ||    \
    ((_ftype) == CIDFONTTYPE4) || ((_ftype) == CIDFONTTYPE0C) || \
    ((_ftype) == CIDFONTTYPEPFIN) )

/* CFF fonts are type 2 (with Type 2 charstrings) and CIDFONTTYPE0 */
#define FONT_IS_CFF(_ftype) \
  ((_ftype) == 2 || (_ftype) == CIDFONTTYPE0C)

#define CIDFONTTYPEU (-1)
#define CIDFONTTYPE0 ( 9)
#define CIDFONTTYPE1 (10)
#define CIDFONTTYPE2 (11)
#define CIDFONTTYPE4 (32)
#define CIDFONTTYPE0C (102) /* Hqn-introduced type for CID Type 0 CFF */
#define CIDFONTTYPEPFIN (127) /* Hqn-introduced type for PFIN CID fonts */

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
