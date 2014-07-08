/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:stickarc.h(EBDSDK_P.1) $
 * $Id: src:stickarc.h,v 1.4.4.1.1.1 2013/12/19 11:25:02 anon Exp $
 *
 * Copyright (C) 2008-2010 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implements Stick and Arc fonts for HPGL.
 */

#ifndef __STICKARC_H__
#define __STICKARC_H__

#include "matrix.h"


/* STICK_FONT_SIZE_FROM_CPI enables the size of a stick font to be determined
 * from its pitch.  In fact the font size chosen is effectively arbitrary,
 * and for e.g. a pitch of 9 cpi, giving an advance 8 points (8/72 inch) wide,
 * as 72/9 = 8, we nevertheless choose a font->size of 12, (8 * 1.5), via the
 * arbitrary value STICK_ADVANCE_IN_EMS.
 *
 * Although Stick fonts are defined in terms of a 48 * 48 grid, this grid does
 * not form a square when placed on the page.  The STICK_ADVANCE_IN_EMS and
 * the STICK_HEIGHT_IN_EMS are used to map from font size to the real
 * dimensions of this grid.
 *
 * The STICK_ADVANCE_IN_EMS gives the fraction of the font size taken up by
 * the 48 horizontal grid units combined.  This is because we previously used
 * this value to determine the font size from the required pitch.  Note that
 * if this value is e.g. increased, the STICK_HEIGHT_IN_EMS must be similarly
 * increased to preserve the aspect ratio.
 *
 * The STICK_HEIGHT_IN_EMS gives the fraction of the font size taken up by the
 * 48 vertical grid units combined.  Having arbitrarily set the relationship
 * between font->size and pitch, this value of (0.9605) had to be found by
 * experiment with our color reference printer, (since the aspect ratio of the
 * 48 * 48 grid is fixed in the printer).  The value appears to be between
 * (0.9603 and 0.9607), which is close to (11.5 /12.0) where 11.5 is the
 * default font height selection criteria, and 12.0 is the font size we
 * choose for a pitch of 9. However, (11.5 / 12.0) comes to (0.9583) which is
 * definitely too small.
 *
 * The STICK_CHARACTER_ADVANCE takes a font size and gives the width of the
 * 48 * 48 grid.  It is the character advance in horizontal text.  The font
 * size may be provided in e.g. points or HPGL plotter units, and the width
 * will be in the same units, (since STICK_ADVANCE_IN_EMS is dimensionless).
 *
 * The STICK_CHARACTER_HEIGHT takes a font size and gives the height of the
 * 48 * 48 grid.  The font size may be provided in e.g. points or HPGL plotter
 * units, and the height will be in the same units, (since STICK_HEIGHT_IN_EMS
 * is dimensionless).
 *
 * The STICK_CHARACTER_BODY_WIDTH takes a font size and gives the width of the
 * 32 * 32 grid containing the body of a stick character.  Clearly this is
 * (32/48) = (2/3) times the size of the STICK_CHARACTER_ADVANCE.  Although
 * this (2/3) ratio is the same as STICK_ADVANCE_IN_EMS this is coincidence,
 * as the meaning is totally different.  As with STICK_CHARACTER_ADVANCE, the
 * size may be provided in e.g. points or HPGL plotter units, and the value
 * calculated will be in the same units.
 *
 * The STICK_CHARACTER_BODY_HEIGHT takes a font size and gives the height of
 * the 32 * 32 grid containing the body of a stick character.  Clearly this is
 * (32/48) = (2/3) times the sie of the STICK_CHARACTER_HEIGHT, and again the
 * size may be provided in e.g. points or HPGL plotter units, and the value
 * calculated will be in the same units.
 *
 * The PCL5 Tech Ref suggests the DEFAULT_STICK_PITCH is 9, but our color
 * reference printer does not use this value.  It seems to use a value between
 * 9.059 and 9.0625, possibly about 9.06.
 *
 * N.B This is the value that would result if a pitch of 9 were being rounded
 *     to default PCL5 Units (300/inch), but only for the 32 of the 48 grid
 *     units making up a stick character, (not for the 16 units that form the
 *     space before the next character).  Whether this is relevant is unknown,
 *     (though stick font character advance is not rounded in general).
 */

#define DEFAULT_STICK_PITCH                (9.06)
#define STICK_ADVANCE_IN_EMS               (2.0 / 3.0)
#define STICK_HEIGHT_IN_EMS                (0.9605)

#define STICK_CHARACTER_ADVANCE(size)      ((size) * STICK_ADVANCE_IN_EMS)
#define STICK_CHARACTER_HEIGHT(size)       ((size) * STICK_HEIGHT_IN_EMS)

#define STICK_CHARACTER_BODY_WIDTH(size)   (STICK_CHARACTER_ADVANCE(size) * (2.0 / 3.0))
#define STICK_CHARACTER_BODY_HEIGHT(size)  (STICK_CHARACTER_HEIGHT(size)  * (2.0 / 3.0))

#define STICK_FONT_SIZE_FROM_CPI(cpi)      (72.0 / (cpi) / STICK_ADVANCE_IN_EMS)


/** stickarc_plotchar:
    Stick is fixed space and Arc is proportional.
    Roman-8 symbol set is assumed.
    Supports DOSHOW, DOCHARPATH and DOSTRINGWIDTH.
    The advance is returned in device space.
 */
Bool stickarc_plotchar(int32 char_id, OMATRIX *transform,
                       Bool fixedspace, int32 showtype,
                       SYSTEMVALUE pointsize, FVECTOR *advance) ;

/* =============================================================================
* Log stripped */
#endif
