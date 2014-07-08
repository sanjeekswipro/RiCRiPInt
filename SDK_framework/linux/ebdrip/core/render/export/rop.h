/** \file
 * \ingroup rendering
 *
 * $HopeName: CORErender!export:rop.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 */
#ifndef _rop_h_
#define _rop_h_

/**
 * Apply the PCL rop to the specified components.
 * \param s The Source term.
 * \param t The Texture term.
 * \param d The Destination term.
 * \param rop Raster operation code.
 */
uint32 rop(uint32 s, uint32 t, uint32 d, uint8 rop);

#endif

/* Log stripped */

