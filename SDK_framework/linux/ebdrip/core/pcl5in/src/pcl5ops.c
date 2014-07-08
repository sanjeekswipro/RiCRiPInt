/** \file
 * \ingroup pcl5
 *
 * $HopeName: COREpcl_pcl5!src:pcl5ops.c(EBDSDK_P.1) $
 * $Id: src:pcl5ops.c,v 1.170.1.1.1.1 2013/12/19 11:25:01 anon Exp $
 *
 * Copyright (C) 2007-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS pcl5exec operator and friends.
 */

#include "core.h"
#include "swcopyf.h"
#include "pcl5ops.h"

#include "pcl5context_private.h"
#include "pcl5scan.h"
#include "macros.h"
#include "macrodev.h"
#include "pagecontrol.h"
#include "areafill.h"
#include "cursorpos.h"
#include "jobcontrol.h"
#include "pcl5color.h"
#include "pcl5fonts.h"
#include "pictureframe.h"
#include "printmodel.h"
#include "pcl5raster.h"
#include "status.h"
#include "misc.h"
#include "twocharops.h"
#include "pcl.h"
#include "printenvironment_private.h"

#include "namedef_.h"
#include "ascii.h"
#include "objects.h"
#include "control.h"
#include "mm.h" /* mm_alloc */
#include "lowmem.h" /* mm_memory_is_low */
#include "swerrors.h"
#include "fileio.h"
#include "stacks.h"
#include "params.h"
#include "monitor.h"
#include "progress.h"
#include "swctype.h"
#include "uelflush.h"
#include "miscops.h"
#include "graphict.h"
#include "gstack.h"
#include "gu_chan.h"
#include "display.h"
#include "timing.h"


/* ============================================================================
 * PCL5 commands
 * ============================================================================
 */

/* What type of argument does this operator take? PCL5_DATA implies
   that PCLValue will be an integer byte count for the data which
   follows the operator. */
enum {
  PCL5_INTEGER = 0,
  PCL5_REAL,
  PCL5_DATA,
  PCL5_NONE
} ;

/* Which modes is this operator allowed in? Short names to keep the
   table below narrow. If a command is not allowed in a particular
   interpreter mode, the command will be consumed and ignored. */
#define GM RASTER_GRAPHICS_MODE /* Allowed in graphics mode. i.e. Will
                                   not be ignored. */
#define PC PCL5C_MODE /* Allowed in PCL5c mode. */
#define PE PCL5E_MODE /* Allowed in PCL5e mode. */
#define MC PCL5C_MACRO_MODE /* Allowed while executing a macro in PCL5c mode. */
#define ME PCL5E_MACRO_MODE /* Allowed while executing a macro in PCL5c mode. */
#define CD CHARACTER_DATA_MODE /* Allowed while in character data mode. */

/* Further bits beyond the interpreter modes can be defined as
   follows. */
#define EG (LAST_INTERPRETER_MODE << 1)  /* This command ends raster graphics if
                                            found when in raster graphics mode.

                                            N.B. Ending raster graphics in this
                                                 way, counts as an explicit end,
                                                 like ESC*rC, ESC*rB commands,
                                                 as opposed to e.g. having seen
                                                 enough image data to end
                                                 graphics and deallocate the
                                                 image reader). */

#define ECD (LAST_INTERPRETER_MODE << 2) /* This command will
                                            implicitly stop the
                                            interpreter looking for a
                                            character continuation
                                            data block. */

#define CG (LAST_INTERPRETER_MODE << 3)  /* This command is likely to Change
                                            the Graphics state, so increment
                                            setg_required before calling
                                            the callback. */
                                         /* Just
                                            noting that this is not a full list
                                            of commands that may change the
                                            graphics state as it excludes
                                            commands that already set
                                            setg_required, e.g. due to a page
                                            throw.  In addition, for reasons
                                            of performance we should use this
                                            flag sparingly and instead try to
                                            avoid setting it until the PS state
                                            really is being changed.  Otherwise
                                            (especially in the case of the
                                            various font selection commands)
                                            we end up not using many slots
                                            on a DL form, and also doing
                                            avoidable DEVICE_SETG calls. */

#define SG (LAST_INTERPRETER_MODE << 4)  /* This command may require a SetG
                                            to update the graphics state
                                            before calling the callback.
                                            Just
                                            noting in passing that this does
                                            not make sense at present. */

#define PnCG (LAST_INTERPRETER_MODE << 5)/* Where it is
                                            used, we should generally just
                                            remove the CG in trunk.  (Also
                                            we should try to properly comment
                                            cases where we don't implement a
                                            command but if we did then CG
                                            would be appropriate - unless we
                                            can get rid of the CG system
                                            completely). */

#define PCG (LAST_INTERPRETER_MODE << 6) /* As well as for gstate changes it
                                            is used in start graphics as the
                                            color_type changes.  Where it is
                                            used we should just add CG in
                                            trunk. */

struct PCL5RegisterOp {
  uint8 *op_name ;
  uint32 op_name_length ;
  uint32 mode_mask ;
  uint8 arg_type ;
  PCL5Operator op ;
} ;

#define PCL5_LAST_ENTRY { NULL, 0, 0, 0, NULL }

static Bool suspend_pcl5_interpreter ;

/* \todo Review this list in terms of the CG flags.
   Also the CG settings for color should be replaced by PnCG and the setg_required flag
   set as required where we set the PS color as e.g. swapping between object types can result
   in a color(space) change without a command.  However the flag is currently set in these
   cases anyway for other reasons.
*/
static struct PCL5RegisterOp pcl5_ops[] = {
  { NAME_AND_LENGTH("&aC"), ECD | PC | PE | ME | MC | EG,       PCL5_REAL,    pcl5op_ampersand_a_C }, /* Move CAP Horizontal (Columns) */
  { NAME_AND_LENGTH("&aG"), ECD | PC | PE | ME | MC | EG,       PCL5_INTEGER, pcl5op_ampersand_a_G }, /* Duplex Page Side */
  { NAME_AND_LENGTH("&aH"), ECD | PC | PE | ME | MC | EG,       PCL5_REAL,    pcl5op_ampersand_a_H }, /* Move Cap Horizontal (Decipoints) */
  { NAME_AND_LENGTH("&aL"), ECD | PC | PE | ME | MC | EG,       PCL5_INTEGER, pcl5op_ampersand_a_L }, /* Left Margin */
  { NAME_AND_LENGTH("&aM"), ECD | PC | PE | ME | MC | EG,       PCL5_INTEGER, pcl5op_ampersand_a_M }, /* Right Margin */
  { NAME_AND_LENGTH("&aP"), ECD | PC | PE | ME | MC | EG,       PCL5_INTEGER, pcl5op_ampersand_a_P }, /* Print Direction */
  { NAME_AND_LENGTH("&aR"), ECD | PC | PE | ME | MC | EG,       PCL5_REAL,    pcl5op_ampersand_a_R }, /* Move CAP Vertical (Rows) */
  { NAME_AND_LENGTH("&aV"), ECD | PC | PE | ME | MC | EG,       PCL5_REAL,    pcl5op_ampersand_a_V }, /* Move CAP Vertical (Decipoints) */

  { NAME_AND_LENGTH("&bM"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_b_M }, /* Monochrome Print Mode [Ref printers end graphics] */
  { NAME_AND_LENGTH("&bW"), ECD | PC | PE | ME | MC | EG,      PCL5_DATA,    pcl5op_ampersand_b_W }, /* Peripheral Configuration (AppleTalk) */

  /* N.B. Font related changes are currently covered by pcl5_start_text_run */
  { NAME_AND_LENGTH("&cT"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_c_T }, /* Text Path Direction */

  { NAME_AND_LENGTH("&dD"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_d_D }, /* Enable Underline */
  { NAME_AND_LENGTH("&d@"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_d_at },/* Disable Underline */

  { NAME_AND_LENGTH("&fI"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_f_I }, /* Custom Paper width */
  { NAME_AND_LENGTH("&fJ"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_f_J }, /* Custom Paper length */
  { NAME_AND_LENGTH("&fS"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_f_S }, /* Push/Pop CAP */
  { NAME_AND_LENGTH("&fX"), ECD | PC | PE | ME | MC | EG | SG, PCL5_INTEGER, pcl5op_ampersand_f_X }, /* Macro Control */
  { NAME_AND_LENGTH("&fY"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_f_Y }, /* Macro ID */

  { NAME_AND_LENGTH("&kG"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_k_G }, /* Line Termination */
  { NAME_AND_LENGTH("&kH"), ECD | PC | PE | ME | MC | EG,      PCL5_REAL,    pcl5op_ampersand_k_H }, /* Horizontal Motion Index (HMI) */
  { NAME_AND_LENGTH("&kS"), ECD | PC | PE | ME | MC | EG | PnCG, PCL5_INTEGER, pcl5op_ampersand_k_S }, /* Primary and Secondary Pitch Control */

  { NAME_AND_LENGTH("&lA"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_l_A }, /* Page Size */
  { NAME_AND_LENGTH("&lC"), ECD | PC | PE | ME | MC | EG,      PCL5_REAL,    pcl5op_ampersand_l_C }, /* Vertical Motion Index (VMI) */
  { NAME_AND_LENGTH("&lD"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_l_D }, /* Line Spacing */
  { NAME_AND_LENGTH("&lE"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_l_E }, /* Top margin */
  { NAME_AND_LENGTH("&lF"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_l_F }, /* Text Length */
  { NAME_AND_LENGTH("&lG"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_l_G }, /* Media Destination */
  { NAME_AND_LENGTH("&lH"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_l_H }, /* Media Source */
  { NAME_AND_LENGTH("&lL"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_l_L }, /* Perforation Skip Mode */
  { NAME_AND_LENGTH("&lM"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_l_M }, /* Media Type */
  { NAME_AND_LENGTH("&lO"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_l_O }, /* Orientation */
  { NAME_AND_LENGTH("&lP"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_l_P }, /* Page Length */
  { NAME_AND_LENGTH("&lS"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_l_S }, /* Simplex/Duplex Mode */
  { NAME_AND_LENGTH("&lT"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_l_T }, /* Job Separation */
  { NAME_AND_LENGTH("&lU"), ECD | PC | PE | ME | MC | EG,      PCL5_REAL,    pcl5op_ampersand_l_U }, /* Left Registration */
  { NAME_AND_LENGTH("&lX"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_l_X }, /* Copies */
  { NAME_AND_LENGTH("&lZ"), ECD | PC | PE | ME | MC | EG,      PCL5_REAL,    pcl5op_ampersand_l_Z }, /* Top Registration */

  { NAME_AND_LENGTH("&nW"), ECD | PC | PE | ME | MC | EG,      PCL5_DATA,    pcl5op_ampersand_n_W }, /* Alphanumeric ID */

  { NAME_AND_LENGTH("&pC"), ECD | PC | MC | CG,                PCL5_INTEGER, pcl5op_ampersand_p_C }, /* Palette Control [5c only. Does not end graphics!] */
  { NAME_AND_LENGTH("&pI"), ECD | PC | MC,                     PCL5_INTEGER, pcl5op_ampersand_p_I }, /* Palette Control ID [5c only. Does not end graphics!] */
  { NAME_AND_LENGTH("&pS"), ECD | PC | MC | CG,                PCL5_INTEGER, pcl5op_ampersand_p_S }, /* Select Palette [5c only. Does not end graphics!] */
  { NAME_AND_LENGTH("&pX"), ECD | PC | PE | ME | MC | EG | SG, PCL5_DATA,    pcl5op_ampersand_p_X }, /* Transparent Data Transfer */

  { NAME_AND_LENGTH("&rF"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_r_F }, /* Flush All Pages */

  { NAME_AND_LENGTH("&sC"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_s_C }, /* End-of-Line Wrap */

  { NAME_AND_LENGTH("&tJ"), ECD | PC | MC,                     PCL5_INTEGER, pcl5op_ampersand_t_J }, /* Render algorithm [5c only. Does not end graphics!] */
  { NAME_AND_LENGTH("&tP"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_ampersand_t_P }, /* Text Parsing Method */

  { NAME_AND_LENGTH("&uD"), ECD | PC | PE | ME | MC | EG,      PCL5_REAL,    pcl5op_ampersand_u_D }, /* Unit of Measure */

  { NAME_AND_LENGTH("*bM"), ECD | GM | PC | PE | ME | MC,      PCL5_REAL,    pcl5op_star_b_M }, /* Compression Method */
  { NAME_AND_LENGTH("*bV"), ECD | GM | PC | MC,                PCL5_DATA,    pcl5op_star_b_V }, /* Transfer Raster by Plane [5c only. Does not end graphics!] */
  { NAME_AND_LENGTH("*bW"), ECD | GM | PC | PE | ME | MC,      PCL5_DATA,    pcl5op_star_b_W }, /* Transfer Raster by Row/Block */
  { NAME_AND_LENGTH("*bY"), ECD | GM | ME | MC,                PCL5_INTEGER, pcl5op_star_b_Y }, /* Raster Y Offset */

  { NAME_AND_LENGTH("*cA"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_star_c_A }, /* Horizontal Rectangle Size (PCL Units) */
  { NAME_AND_LENGTH("*cB"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_star_c_B }, /* Vertical Rectangle Size (PCL Units) */
  { NAME_AND_LENGTH("*cD"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_star_c_D }, /* Font ID */
  { NAME_AND_LENGTH("*cE"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_star_c_E }, /* Character Code */
  /* The setg_required flag is set from set_ps_font */
  { NAME_AND_LENGTH("*cF"), ECD | PC | PE | ME | MC | EG | PnCG, PCL5_INTEGER, pcl5op_star_c_F }, /* Font Control */
  /* Cases where pattern can change without a command are covered by image callback, mpe_restore, etc */
  { NAME_AND_LENGTH("*cG"), ECD | PC | PE | ME | MC | EG | CG, PCL5_INTEGER, pcl5op_star_c_G }, /* Pattern ID */
  { NAME_AND_LENGTH("*cH"), ECD | PC | PE | ME | MC | EG,      PCL5_REAL,    pcl5op_star_c_H }, /* Horizontal Rectangle Size (Decipoints) */
  { NAME_AND_LENGTH("*cK"), ECD | PC | PE | ME | MC | EG,      PCL5_REAL,    pcl5op_star_c_K }, /* Plot Size (Horizontal) */
  { NAME_AND_LENGTH("*cL"), ECD | PC | PE | ME | MC | EG,      PCL5_REAL,    pcl5op_star_c_L }, /* Plot Size (Vertical) */
  { NAME_AND_LENGTH("*cP"), ECD | PC | PE | ME | MC | EG | SG | PCG, PCL5_INTEGER, pcl5op_star_c_P }, /* Fill Rectangular Area */
  { NAME_AND_LENGTH("*cQ"), ECD | PC | PE | ME | MC | EG | CG, PCL5_INTEGER, pcl5op_star_c_Q }, /* Pattern Control */
  { NAME_AND_LENGTH("*cR"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_star_c_R }, /* Symbol Set Code */
  { NAME_AND_LENGTH("*cS"), ECD | PC | PE | ME | MC | EG | PnCG, PCL5_INTEGER, pcl5op_star_c_S }, /* Symbol Set Control */
  { NAME_AND_LENGTH("*cT"), ECD | PC | PE | ME | MC | EG,      PCL5_REAL,    pcl5op_star_c_T }, /* Picture Frame Anchor Point */
  { NAME_AND_LENGTH("*cV"), ECD | PC | PE | ME | MC | EG,      PCL5_REAL,    pcl5op_star_c_V }, /* Vertical Rectangle Size (Decipoints) */
  { NAME_AND_LENGTH("*cW"), ECD | PC | PE | ME | MC | EG,      PCL5_DATA,    pcl5op_star_c_W }, /* Download Pattern */
  { NAME_AND_LENGTH("*cX"), ECD | PC | PE | ME | MC | EG,      PCL5_REAL,    pcl5op_star_c_X }, /* Picture Frame Size (Horizontal) */
  { NAME_AND_LENGTH("*cY"), ECD | PC | PE | ME | MC | EG,      PCL5_REAL,    pcl5op_star_c_Y }, /* Picture Frame Size (Vertical) */

  { NAME_AND_LENGTH("*iW"), ECD | PC | MC | EG,                PCL5_DATA,    pcl5op_star_i_W }, /* Viewing Illuminant [5c only. Ref printer ends graphics] */

  { NAME_AND_LENGTH("*lO"), ECD | PC | PE | ME | MC | EG | PnCG, PCL5_INTEGER, pcl5op_star_l_O }, /* Logical Operation */
  { NAME_AND_LENGTH("*lR"), ECD | PC | PE | ME | MC | EG | PCG,  PCL5_INTEGER, pcl5op_star_l_R }, /* Pixel Placement */
  { NAME_AND_LENGTH("*lW"), ECD | PC | MC | EG | PnCG,           PCL5_DATA,    pcl5op_star_l_W }, /* Color Lookup Tables [5c only. Ref printer ends graphics] */

  { NAME_AND_LENGTH("*mW"), ECD | PC | MC | EG | PnCG,           PCL5_DATA,    pcl5op_star_m_W }, /* Download Dither Matrix [5c only. Ref printer ends graphics] */

  { NAME_AND_LENGTH("*oW"), ECD | PC | MC | EG,                  PCL5_DATA,    pcl5op_star_o_W }, /* Driver Function Configuration [5c only. Ref printer ends graphics] */

  { NAME_AND_LENGTH("*pP"), ECD | PC | MC | CG,                 PCL5_INTEGER, pcl5op_star_p_P }, /* Push/Pop Palette [5c only. Does not end graphics!] */
  { NAME_AND_LENGTH("*pR"), ECD | PC | PE | ME | MC | EG | CG,  PCL5_INTEGER, pcl5op_star_p_R }, /* Pattern Reference Point */
  { NAME_AND_LENGTH("*pX"), ECD | PC | PE | ME | MC | EG,       PCL5_INTEGER, pcl5op_star_p_X }, /* Move CAP Horizontal (PCL Units) */
  { NAME_AND_LENGTH("*pY"), ECD | PC | PE | ME | MC | EG,       PCL5_INTEGER, pcl5op_star_p_Y }, /* Move CAP Vertical (PCL Units) */

  /* Set the setg_required flag in start_raster_graphics callback instead as images can start without a command */
  { NAME_AND_LENGTH("*rA"), ECD | PC | PE | ME | MC | SG,            PCL5_INTEGER, pcl5op_star_r_A }, /* Start Raster */
  /* We allow end raster graphics in all modes because we need to reset state. */
  { NAME_AND_LENGTH("*rB"), ECD | GM | PC | PE | ME | MC | EG,       PCL5_REAL,    pcl5op_star_r_B }, /* End Raster Graphics (B) */
  { NAME_AND_LENGTH("*rC"), ECD | GM | PC | PE | ME | MC | EG,       PCL5_REAL,    pcl5op_star_r_C }, /* End Raster (C) */
  { NAME_AND_LENGTH("*rF"), ECD | PC | PE | ME | MC,                 PCL5_REAL,    pcl5op_star_r_F }, /* Raster Presentation Mode */
  { NAME_AND_LENGTH("*rS"), ECD | PC | PE | ME | MC,                 PCL5_INTEGER, pcl5op_star_r_S }, /* Raster Source Width */
  { NAME_AND_LENGTH("*rT"), ECD | PC | PE | ME | MC,                 PCL5_INTEGER, pcl5op_star_r_T }, /* Raster Source Height */
  { NAME_AND_LENGTH("*rU"), ECD | PC | MC | CG,                      PCL5_INTEGER, pcl5op_star_r_U }, /* Simple Color [5c only. Does not end graphics!] */

  { NAME_AND_LENGTH("*sI"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_star_s_I }, /* Inquire Entity */
  { NAME_AND_LENGTH("*sM"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_star_s_M }, /* Free Space */
  { NAME_AND_LENGTH("*sT"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_star_s_T }, /* Location Type */
  { NAME_AND_LENGTH("*sU"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_star_s_U }, /* Location Unit */
  { NAME_AND_LENGTH("*sX"), ECD | PC | PE | ME | MC | EG,      PCL5_INTEGER, pcl5op_star_s_X }, /* Echo */
  { NAME_AND_LENGTH("*tH"), ECD | PC | PE | ME | MC,           PCL5_REAL,    pcl5op_star_t_H }, /* Raster Destination Width */
  { NAME_AND_LENGTH("*tI"), ECD | PC | MC | PnCG,              PCL5_REAL,    pcl5op_star_t_I }, /* Gamma Correction [5c only.  Does not end graphics!] */
  { NAME_AND_LENGTH("*tJ"), ECD | PC | MC | EG | PnCG,         PCL5_INTEGER, pcl5op_star_t_J }, /* Render Algorithm [5c only. Ref printer ends graphics] */
  { NAME_AND_LENGTH("*tK"), ECD | PC | PE | ME | MC | EG,      PCL5_REAL,    pcl5op_star_t_K }, /* Scale Algorithm */
  { NAME_AND_LENGTH("*tR"), ECD | PC | PE | ME | MC,           PCL5_REAL,    pcl5op_star_t_R }, /* Raster Resolution */
  { NAME_AND_LENGTH("*tV"), ECD | PC | PE | ME | MC,           PCL5_REAL,    pcl5op_star_t_V }, /* Raster Destination Height */

  { NAME_AND_LENGTH("*vA"), ECD | PC | MC | CG,                PCL5_REAL,    pcl5op_star_v_A },   /* Color Component 1 [5c only. Does not end graphics!] */
  { NAME_AND_LENGTH("*vB"), ECD | PC | MC | CG,                PCL5_REAL,    pcl5op_star_v_B },   /* Color Component 2 [5c only. Does not end graphics!] */
  { NAME_AND_LENGTH("*vC"), ECD | PC | MC | CG,                PCL5_REAL,    pcl5op_star_v_C },   /* Color Component 3 [5c only. Does not end graphics!] */
  { NAME_AND_LENGTH("*vI"), ECD | PC | MC,                     PCL5_INTEGER, pcl5op_star_v_I },   /* Assign Color Index [5c only. Does not end graphics!] */
  { NAME_AND_LENGTH("*vN"), ECD | PC | PE | ME | MC | EG | PnCG, PCL5_INTEGER, pcl5op_star_v_N }, /* Transparency Mode (Source) */
  { NAME_AND_LENGTH("*vO"), ECD | PC | PE | ME | MC | EG | PnCG, PCL5_INTEGER, pcl5op_star_v_O }, /* Transparency Mode (Destination) */
  { NAME_AND_LENGTH("*vS"), ECD | PC | MC | CG,                PCL5_INTEGER, pcl5op_star_v_S },   /* Foreground color [5c only. Does not end graphics!] */
  { NAME_AND_LENGTH("*vT"), ECD | PC | PE | ME | MC | EG | CG, PCL5_INTEGER, pcl5op_star_v_T },   /* Current Pattern */
  { NAME_AND_LENGTH("*vW"), ECD | PC | MC | PCG,               PCL5_DATA,    pcl5op_star_v_W },   /* Configure Image Data (CID) [5c only. Does not end graphics!] */

  { NAME_AND_LENGTH("(fW"), ECD | PC | PE | ME | MC | EG,         PCL5_DATA,    pcl5op_left_paren_f_W }, /* Download Symbol Set */
  { NAME_AND_LENGTH("(sB"), ECD | PC | PE | ME | MC | EG  | PnCG, PCL5_INTEGER, pcl5op_left_paren_s_B }, /* Font Stroke Weight (Primary) */
  { NAME_AND_LENGTH("(sH"), ECD | PC | PE | ME | MC | EG  | PnCG, PCL5_REAL,    pcl5op_left_paren_s_H }, /* Font Pitch (Primary) */
  { NAME_AND_LENGTH("(sP"), ECD | PC | PE | ME | MC | EG  | PnCG, PCL5_INTEGER, pcl5op_left_paren_s_P }, /* Font Spacing (Primary) */
  { NAME_AND_LENGTH("(sS"), ECD | PC | PE | ME | MC | EG  | PnCG, PCL5_INTEGER, pcl5op_left_paren_s_S }, /* Font Style (Primary) */
  { NAME_AND_LENGTH("(sT"), ECD | PC | PE | ME | MC | EG  | PnCG, PCL5_INTEGER, pcl5op_left_paren_s_T }, /* Font Typeface (Primary) */
  { NAME_AND_LENGTH("(sV"), ECD | PC | PE | ME | MC | EG  | PnCG, PCL5_REAL,    pcl5op_left_paren_s_V }, /* Font Height (Primary) */
  { NAME_AND_LENGTH("(sW"),  CD | PC | PE | ME | MC | EG,         PCL5_DATA,    pcl5op_left_paren_s_W }, /* Download Character (Primary) [Only command allowed in char data mode] */
  /* left paren symbol set commands are dealt with specially. */

  { NAME_AND_LENGTH(")sB"), ECD | PC | PE | ME | MC | EG  | PnCG, PCL5_INTEGER, pcl5op_right_paren_s_B }, /* Font Stroke Weight (Secondary) */
  { NAME_AND_LENGTH(")sH"), ECD | PC | PE | ME | MC | EG  | PnCG, PCL5_REAL,    pcl5op_right_paren_s_H }, /* Font Pitch (Secondary) */
  { NAME_AND_LENGTH(")sP"), ECD | PC | PE | ME | MC | EG  | PnCG, PCL5_INTEGER, pcl5op_right_paren_s_P }, /* Font Spacing (Secondary) */
  { NAME_AND_LENGTH(")sS"), ECD | PC | PE | ME | MC | EG  | PnCG, PCL5_INTEGER, pcl5op_right_paren_s_S }, /* Font Style (Secondary) */
  { NAME_AND_LENGTH(")sT"), ECD | PC | PE | ME | MC | EG  | PnCG, PCL5_INTEGER, pcl5op_right_paren_s_T }, /* Font Typeface (Secondary) */
  { NAME_AND_LENGTH(")sV"), ECD | PC | PE | ME | MC | EG  | PnCG, PCL5_REAL,    pcl5op_right_paren_s_V }, /* Font Height (Secondary) */
  { NAME_AND_LENGTH(")sW"), ECD | PC | PE | ME | MC | EG,         PCL5_DATA,    pcl5op_right_paren_s_W }, /* Download Font */
  /* right paren symbol set commands are dealt with specially. */

  /* Operators with no group character. */
  { NAME_AND_LENGTH("%A"),  ECD | PC | PE | ME | MC | EG | SG | PCG, PCL5_REAL, pcl5op_percent_A },      /* Enter Language = PCL 5 */
  { NAME_AND_LENGTH("%B"),  ECD | PC | PE | ME | MC | EG | SG | PCG, PCL5_REAL, pcl5op_percent_B },      /* Enter Language = HP-GL/2 */
  { NAME_AND_LENGTH("%X"),  ECD | PC | PE | ME | MC | EG,        PCL5_REAL,     pcl5op_percent_X },      /* Universal End-Of-Language */
  { NAME_AND_LENGTH("(X"),  ECD | PC | PE | ME | MC | EG | PnCG, PCL5_INTEGER,  pcl5op_left_paren_X },   /* Font Selection by ID (Primary) */
  { NAME_AND_LENGTH("(@"),  ECD | PC | PE | ME | MC | EG | PnCG, PCL5_INTEGER,  pcl5op_left_paren_at },  /* Default Font (Primary) */
  { NAME_AND_LENGTH(")X"),  ECD | PC | PE | ME | MC | EG | PnCG, PCL5_INTEGER,  pcl5op_right_paren_X },  /* Font Selection by ID (Secondary) */
  { NAME_AND_LENGTH(")@"),  ECD | PC | PE | ME | MC | EG | PnCG, PCL5_INTEGER,  pcl5op_right_paren_at }, /* Default Font (Secondary) */

  PCL5_LAST_ENTRY
} ;

/* ============================================================================
 * PCL5 function lookup hash table
 * ============================================================================
 */

struct PCL5FunctEntry {
  uint32 op_name_key ; /* Key */
  uint32 mode_mask ;   /* Allowable modes for this command
                          amoungest other dispatch
                          information */
  int32 arg_type ;     /* What type of argument does this
                          operator take */
  PCL5Operator op ;    /* Function */
} ;

#define PACK_2_BYTES(b1, b2)     ((uint32)((b1) << 8 | (b2)))
#define PACK_3_BYTES(b1, b2, b3) ((uint32)((b1) << 16 | (b2) << 8 | (b3)))

struct PCL5FunctTable {
  /* Number of entries in hash table */
  unsigned int num_entries ;
  /* The hash table */
  struct PCL5FunctEntry **table ;
} ;

/* Constants for PJW hash function */
#define PJW_SHIFT        (4)            /* Per hashed char hash shift */
#define PJW_MASK         (0xf0000000u)  /* Mask for hash top bits */
#define PJW_RIGHT_SHIFT  (24)           /* Right shift distance for hash top bits */

#if 0
#define DEBUG_PCL5OPS_HASH 1
#endif

/* This prime happens to work with a PJW hash for the 3 operator
   bytes. Found by experimentation. Note that there MUST not be a hash
   clash for any of the valid PCL5 operators. This is asserted at
   runtime. */
#define FUNCTTABLE_HASH_SIZE 2357

static inline uint32 pjw_hash_low_3(uint32 key)
{
  uint32 hash = 0 ;
  uint32 bits = 0 ;

  hash = (hash << PJW_SHIFT) + (key & 0x00ff0000u) ;
  bits = hash & PJW_MASK ;
  hash ^= bits | (bits >> PJW_RIGHT_SHIFT) ;

  hash = (hash << PJW_SHIFT) + (key & 0x0000ff00u) ;
  bits = hash & PJW_MASK ;
  hash ^= bits | (bits >> PJW_RIGHT_SHIFT) ;

  hash = (hash << PJW_SHIFT) + (key & 0x000000ffu) ;
  bits = hash & PJW_MASK ;
  hash ^= bits | (bits >> PJW_RIGHT_SHIFT) ;

#if defined(DEBUG_PCL5OPS_HASH)
  monitorf((uint8*)"HASH = %d    %d\n", hash, hash % FUNCTTABLE_HASH_SIZE) ;
#endif

  return hash % FUNCTTABLE_HASH_SIZE ;
}

static
struct PCL5FunctEntry *pcl5_find_funct_entry(struct PCL5FunctTable *table,
                                             uint32 op_name_key, unsigned int *hval)
{
  struct PCL5FunctEntry *curr ;
  HQASSERT(table != NULL, "table is NULL") ;
  HQASSERT(hval != NULL, "hval is NULL") ;

  *hval = pjw_hash_low_3(op_name_key) ;
  curr = table->table[*hval] ;

  if ( curr != NULL && op_name_key == curr->op_name_key)
    return curr ;

  return NULL;
}

static
struct PCL5FunctEntry *pcl5_find_slot(struct PCL5FunctTable *table,
                                      uint32 op_name_key, unsigned int *hval)
{
  struct PCL5FunctEntry *curr ;
  HQASSERT(table != NULL, "table is NULL") ;
  HQASSERT(hval != NULL, "hval is NULL") ;

  *hval = pjw_hash_low_3(op_name_key) ;
  curr = table->table[*hval] ;

  return curr ;
}

Bool pcl5_funct_table_create(struct PCL5FunctTable **table)
{
  unsigned int i;
  HQASSERT(table != NULL, "table pointer is NULL");

  *table = mm_alloc(mm_pcl_pool, sizeof(struct PCL5FunctTable),
                    MM_ALLOC_CLASS_PCL_CONTEXT) ;

  if (*table == NULL)
    return FALSE;

  (*table)->table = mm_alloc(mm_pcl_pool, sizeof(struct PCL5FunctEntry*) * FUNCTTABLE_HASH_SIZE,
                             MM_ALLOC_CLASS_PCL_CONTEXT) ;

  if ((*table)->table == NULL) {
    mm_free(mm_pcl_pool, *table, sizeof(struct PCL5FunctTable)) ;
    *table = NULL;
    return FALSE;
  }

  /* Initialize the table structure. */
  (*table)->num_entries = 0;
  for (i=0; i<FUNCTTABLE_HASH_SIZE; i++) {
    (*table)->table[i] = NULL;
  }
  return TRUE;
}

void pcl5_funct_table_destroy(struct PCL5FunctTable **table)
{
  unsigned int i ;
  struct PCL5FunctEntry *curr ;

  HQASSERT(table != NULL, "table is NULL") ;
  HQASSERT(*table != NULL, "table pointer is NULL") ;

  for (i=0; i<FUNCTTABLE_HASH_SIZE; i++) {
    curr = (*table)->table[i] ;
    if (curr != NULL) {
      mm_free(mm_pcl_pool, curr, sizeof(struct PCL5FunctEntry)) ;
      (*table)->num_entries-- ;
    }
  }
  HQASSERT((*table)->num_entries == 0, "num_entries is not zero.") ;

  mm_free(mm_pcl_pool, (*table)->table, sizeof(struct PCL5FunctEntry*) * FUNCTTABLE_HASH_SIZE) ;
  mm_free(mm_pcl_pool, (*table), sizeof(struct PCL5FunctTable)) ;
  (*table) = NULL ;
  return ;
}

static Bool pcl5_funct_table_register(struct PCL5FunctTable *table,
                                      uint8 *op_name, uint32 op_name_length,
                                      uint32 mode_mask,
                                      int32 arg_type,
                                      PCL5Operator op,
                                      struct PCL5FunctEntry **entry)
{
  struct PCL5FunctEntry *curr;
  unsigned int hval;
  uint32 op_name_key ;

  HQASSERT(table != NULL, "table is NULL") ;

  *entry = NULL ;

  HQASSERT(op_name_length == 2 || op_name_length == 3,
           "op_name_length is invalid") ;

  if (op_name_length == 2) {
    op_name_key = PACK_2_BYTES(op_name[0], op_name[1]) ;
  } else {
    HQASSERT(op_name_length  == 3,
             "op_name_length is not three") ;
    op_name_key = PACK_3_BYTES(op_name[0], op_name[1], op_name[2]) ;
  }

  curr = pcl5_find_slot(table, op_name_key, &hval) ;

  if (curr == NULL) {
    curr  = mm_alloc(mm_pcl_pool, sizeof(struct PCL5FunctEntry),
                     MM_ALLOC_CLASS_PCL_CONTEXT) ;

    if (curr == NULL)
      return FALSE ;

    curr->op_name_key = op_name_key ;
    curr->arg_type = arg_type ;
    curr->mode_mask = mode_mask ;
    curr->op = op ;

    table->table[hval] = curr ;
    table->num_entries++ ;
  } else {
#if defined(DEBUG_PCL5OPS_HASH)
    monitorf((uint8*)"PCL5 operator hash clash!\n") ;
#endif

    HQFAIL("PCL5 operator hash clash not allowed.") ;
  }
  *entry = curr ;
  return TRUE ;
}

Bool register_pcl5_ops(struct PCL5FunctTable *table)
{
  struct PCL5RegisterOp *curr ;
  struct PCL5FunctEntry *entry ;

  for (curr = pcl5_ops; curr->op_name != NULL; curr++) {
    if (! pcl5_funct_table_register(table, curr->op_name, curr->op_name_length,
                                    curr->mode_mask,
                                    curr->arg_type, curr->op, &entry))
      return FALSE ;
  }
  return TRUE ;
}

#ifdef DEBUG_BUILD

/* Return pointer to buffer holding stringized version of parsed PCL 5 command.
 * Obviously, this is not thread safe.
 */
static
uint8* dump_pcl_command(
  uint8       operation,
  uint8       group_char,
  int32       explicit_sign,
  PCL5Numeric value,
  Bool        is_real,
  uint8       termination_char)
{
#define PCLDUMP_BUFSIZ  (64)
  static uint8  buffer[PCLDUMP_BUFSIZ];
  uint8*  b = buffer;
/* Pseudo macro func to track remaining space in the buffer */
#define BUF_REMAINING() (PCLDUMP_BUFSIZ - CAST_PTRDIFFT_TO_UINT32(b - buffer))
/* Add hex version of character if not printable */
#define PCLDUMP_ADD_CHAR(b, c) MACRO_START \
  if ( isprint(c) ) { \
    (b) += swncopyf((b), BUF_REMAINING(), (uint8*)"%c", (c)); \
  } else { \
    (b) += swncopyf((b), BUF_REMAINING(), (uint8*)"<%02x>", (uint32)(c)); \
  } \
MACRO_END

  b += swncopyf(b, BUF_REMAINING(), (uint8*)"ESC ");
  PCLDUMP_ADD_CHAR(b, operation);
  PCLDUMP_ADD_CHAR(b, group_char);
  if ( explicit_sign == EXPLICIT_POSITIVE ) {
    b += swncopyf(b, BUF_REMAINING(), (uint8*)"+");
  }
  if ( is_real ) {
    b += swncopyf(b, BUF_REMAINING(), (uint8*)"%f", value.real);
  } else {
    b += swncopyf(b, BUF_REMAINING(), (uint8*)"%d", value.integer);
  }
  PCLDUMP_ADD_CHAR(b, termination_char);
  return(buffer);

} /* dump_pcl_command */

#endif /* DEBUG_BUILD */

static
Bool pcl5op_unknown(PCL5Context *pcl5_ctxt, uint8 operation,
                    PCL5Operator op_callback, uint32 mode_mask,
                    uint8 group_char, PCL_VALUE* p_value,
                    uint8 termination_char)
{
  UNUSED_PARAM(PCL5Context*, pcl5_ctxt) ;
  UNUSED_PARAM(uint8, operation) ;
  UNUSED_PARAM(PCL5Operator, op_callback) ;
  UNUSED_PARAM(uint32, mode_mask) ;
  UNUSED_PARAM(uint8, group_char) ;
  UNUSED_PARAM(PCL_VALUE*, p_value) ;
  UNUSED_PARAM(uint8, termination_char) ;

#if defined(DEBUG_BUILD)
  {
    PCL5Numeric value;
    Bool is_real = p_value->decimal_place || p_value->ntrailing > 0;
    if ( is_real ) {
      double fract = (double)p_value->ftrailing/pow(10.0, (double)p_value->ntrailing);
      value.real = (double)p_value->fleading;
      if ( p_value->fleading < 0 ) {
        value.real -= fract;
      } else {
        value.real += fract;
      }
    } else {
      value.integer = p_value->fleading;
    }
    monitorf((uint8*)"%%%%[ Warning: Unknown PCL5 parameterized command: %s ] %%%%\n",
             dump_pcl_command(operation, group_char, p_value->explicit_sign, value,
                              is_real, termination_char));
  }
#endif

  return TRUE ;
}

/* ============================================================================
 *  Deal with the parameterized commands. Basically dispatch further
 *  functions depending on the group and termination character.
 *  ============================================================================
 */
static
Bool pcl5op_parameterized_escape(PCL5Context *pcl5_ctxt, uint8 operation,
                                 PCL5Operator op_callback, uint32 mode_mask,
                                 uint8 group_char, PCL_VALUE* p_value,
                                 uint8 termination_char)
{
  int32 ch ;
  unsigned int hval ;
  struct PCL5FunctEntry *entry ;
  uint32 op_name_key ;

  UNUSED_PARAM(PCL5Operator, op_callback) ;
  UNUSED_PARAM(uint32, mode_mask) ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  /* group_char will be zero when not set */
  if (group_char == 0) {
    op_name_key = PACK_2_BYTES(operation, termination_char) ;
  } else {
    op_name_key = PACK_3_BYTES(operation, group_char, termination_char) ;
  }

  entry = pcl5_find_funct_entry(pcl5_ctxt->pcl5_ops_table, op_name_key, &hval) ;

  if (entry != NULL) {
    PCL5PrintState *print_state = pcl5_ctxt->print_state ;
    PCL5Operator op = entry->op ;
    PCL5Numeric value ;
    value.integer = 0 ;

    HQASSERT(op != NULL, "Callback function is NULL") ;

    /* If we are recording a macro, the only escape sequence we will
       dispatch is the macro control and the enter HPGL2 command. */
    if (pcl5_recording_a_macro &&
        op != pcl5op_ampersand_f_X &&
        op != pcl5op_percent_B) {
      /* If the command is followed by data, slurp that in and
         record that in the current active macro. */
      if (entry->arg_type == PCL5_DATA) {
        int32 count = p_value->fleading ;
        int32 ch ;

        while (count-- > 0) {
          if ((ch = Getc(pcl5_ctxt->flptr)) == EOF)
            return TRUE ;
        }
      }
      return TRUE ;
    }

#ifdef RQ64403
    /* No need to reset font if just doing a horizontal cursor
       movement. This is a little idiom recognition to improve
       performance for the ISO suite 100 page job which prints a
       character, moves the cursor and then prints another single
       character 1000's of times. */
    if (op != pcl5op_star_p_X) {
      /* We want to minimise calling this function as much as possible so
         that the number of times fonts etc.. get set up before a text run
         starts is reduced. We can delay the ending of a text run as some
         commands have no effect should they reside between consequtive
         text runs. */
      if (! pcl5_set_text_run_restart_required(pcl5_ctxt, FALSE))
        return FALSE ;
    }
#endif
    /* Are we in raster graphics mode or character data mode? */
    if (pcl5_ctxt->interpreter_mode == RASTER_GRAPHICS_MODE ||
        pcl5_ctxt->interpreter_mode == CHARACTER_DATA_MODE) {
      if ( (entry->mode_mask & EG && pcl5_ctxt->interpreter_mode == RASTER_GRAPHICS_MODE) ||
           (entry->mode_mask & ECD && pcl5_ctxt->interpreter_mode == CHARACTER_DATA_MODE) ) {
        /* Cache command for execution after the raster graphics has
           finished. */
        pcl5_ctxt->is_cached_command = TRUE ;
        pcl5_ctxt->cached_operation = operation ;
        pcl5_ctxt->cached_group_char = group_char ;
        pcl5_ctxt->cached_value = *p_value;
        pcl5_ctxt->cached_termination_char = termination_char ;

        if (entry->mode_mask & EG && pcl5_ctxt->interpreter_mode == RASTER_GRAPHICS_MODE) {
          /* End raster graphics */
          return end_raster_graphics_callback(pcl5_ctxt, TRUE) ;
        } else {
          /* Implicit end character data mode. */
          return implicit_end_character_data(pcl5_ctxt) ;
        }
      }
    }

    /* Should we ignore this command? */
    if ( (pcl5_ctxt->interpreter_mode & entry->mode_mask) == 0) {
      /* If the command is followed by data, slurp that in and throw
         it out. */
      if (entry->arg_type == PCL5_DATA) {
        int32 count = p_value->fleading ;
        while (count-- > 0) {
          if ((ch = Getc(pcl5_ctxt->flptr)) == EOF)
            return TRUE ;
        }
      }
#if defined(DEBUG_BUILD)
      if ( debug_pcl5 & PCL5_CONTROL ) {
        PCL5Numeric value;
        value.integer = 0;
        monitorf((uint8*)"ignore %s\n",
                 dump_pcl_command(operation, group_char, p_value->explicit_sign,
                                  value, TRUE, termination_char));
      }
#endif
      return TRUE ;
    }

    switch (entry->arg_type) {
    case PCL5_REAL:
      /* Convert scanned value integers into a double. */
      if (p_value->ntrailing > 0) {
        double fract = (double)p_value->ftrailing / pow(10.0, (double)p_value->ntrailing) ;
        if (p_value->fleading < 0) {
          value.real = (double)p_value->fleading - fract ;
        } else {
          value.real = (double)p_value->fleading + fract ;
        }
      } else {
        value.real = p_value->fleading ;
      }

#if defined(DEBUG_BUILD)
      if ( debug_pcl5 & PCL5_CONTROL ) {
        monitorf((uint8*)"%s\n",
                 dump_pcl_command(operation, group_char, p_value->explicit_sign,
                                  value, TRUE, termination_char));
      }
#endif

      break ;
    case PCL5_INTEGER:
      value.integer = p_value->fleading ;
#if defined(DEBUG_BUILD)
      if ( debug_pcl5 & PCL5_CONTROL ) {
        monitorf((uint8*)"%s\n",
                 dump_pcl_command(operation, group_char, p_value->explicit_sign,
                                  value, FALSE, termination_char));
      }
#endif
      break ;
    case PCL5_DATA:
      value.integer = abs(p_value->fleading) ;
#if defined(DEBUG_BUILD)
      if ( debug_pcl5 & PCL5_CONTROL ) {
        monitorf((uint8*)"%s\n",
                 dump_pcl_command(operation, group_char, p_value->explicit_sign,
                                  value, FALSE, termination_char));
      }
#endif
      break ;
    case PCL5_NONE:
      break ;
    default:
      HQFAIL("Invalid arg type") ;
    }

   /* Just adding this comment.  In PCL5 we have so far always worked on the
    * principle of setting the required PS gstate just before an object is
    * drawn, (although this may be done lazily - and there are a couple of
    * things (e.g. rops) where we keep the underlying state up to date the
    * whole time).  This means that it does not make sense to actually do the
    * SETG before calling e.g. the start raster graphics command as the
    * necessary PS state will not yet be set up.
    *
    * N.B. We could flush the text run and increment setg_required here if we
    *      wanted.
    */
#if 0
    /* todo It doesn't appear that much requires SETG that doesn't
       already do it but the font code. This approach would allow
       optimisations for areafills etc, but isn't significant
       right now. */

    /* If this operation requires SETG to have been done, do it now */
    if ((entry->mode_mask & SG) == SG &&
        print_state->setg_required) {
      print_state->setg_required = 0 ;

      /* Flush any pending text run before SETG */
      pcl5_flush_text_run(pcl5_ctxt,1) ;

      if (! DEVICE_SETG(FSC_FILL, DEVICE_SETG_NORMAL))
        return FALSE ;
    }
#endif

    /* If this operation probably changes graphic state, note */
    /* Do not set this flag so eagerly,
     * but wait to see whether we actually change the PS gstate.
     * todo We should probably avoid doing setg before several more
     * ops, and wait until the PS gstate changes, although in some
     * cases it may be more efficient to do it based on the commands
     * that we have seen.
     */
    if ( (entry->mode_mask & CG) == CG ||
         ((entry->mode_mask & PCG) == PCG) ) {
      print_state->setg_required += 1 ;
    }

    return op(pcl5_ctxt, p_value->explicit_sign, value) ;
  }

#if defined(DEBUG_BUILD)
  /* Force call on the unknown op handler */
  (void)pcl5op_unknown(pcl5_ctxt, operation, op_callback, mode_mask, group_char,
                       p_value, termination_char);
#endif

  return TRUE ;
}

/* Regrettably we need to deal with the symbol set commands specially
   because their termination character changes. */
static
Bool pcl5op_possible_symbol_set(PCL5Context *pcl5_ctxt, uint8 operation,
                                PCL5Operator op_callback, uint32 mode_mask,
                                uint8 group_char, PCL_VALUE* p_value,
                                uint8 termination_char)
{
  HQASSERT(operation == '(' || operation == ')',
           "Symbol set callback called with invalid operation character.") ;

  UNUSED_PARAM(uint32, mode_mask) ;

#ifdef RQ64403
  if (! pcl5_set_text_run_restart_required(pcl5_ctxt, FALSE))
    return FALSE ;
#endif
  if (group_char == 0) {
    uint8 normalised_termination_char = termination_char;
    if ( is_param_char(termination_char) && !is_terminating_char(termination_char) ) {
      normalised_termination_char = make_termination_char(termination_char);
    }
    if (normalised_termination_char != '@' && normalised_termination_char != 'X') {
      PCL5Numeric value ;
      /* Looks very much like a symbol set command. */

      /* Symbol set value validation. Ignore the command if anything is out of range. */
      if (p_value->decimal_place || p_value->ntrailing > 0 ||
          p_value->explicit_sign == EXPLICIT_POSITIVE || p_value->fleading > 1023 ) {
#if defined(DEBUG_BUILD)
        if ( debug_pcl5 & PCL5_CONTROL ) {
          monitorf((uint8*)"ignored ESC %c # %c (invalid symbol set command?)\n",
                   (char)operation,
                   (char)termination_char) ;
        }
#endif
        return TRUE ;
      }

      value.integer = p_value->fleading * 32 + termination_char - 64 ;

      /* Looks very much like a symbol set command. */
#if defined(DEBUG_BUILD)
      if ( debug_pcl5 & PCL5_CONTROL ) {
        monitorf((uint8*)"ESC %c %d %c (symbol set command)\n",
                 (char)operation,
                 value.integer,
                 (char)termination_char) ;
      }
#endif

      if (! pcl5_recording_a_macro) {
        /* Keep callback signatures the same as all the others for consistency. */
        if (operation == '(') {
          return pcl5op_left_paren_symbolset(pcl5_ctxt, p_value->explicit_sign, value) ;
        } else {
          return pcl5op_right_paren_symbolset(pcl5_ctxt, p_value->explicit_sign, value) ;
        }
      } else {
        return TRUE ;
      }
    }
  }

  return pcl5op_parameterized_escape(pcl5_ctxt, operation, op_callback, mode_mask,
                                     group_char, p_value, termination_char) ;
}

Bool pcl5op_two_char_escape(PCL5Context *pcl5_ctxt, uint8 operation,
                            PCL5Operator op_callback, uint32 mode_mask,
                            uint8 group_char, PCL_VALUE* p_value,
                            uint8 termination_char)
{
  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;


  if (op_callback != NULL) {
    PCL5Numeric value ;
    value.integer = 0 ;

    /* Are we recording a macro? */
    if (pcl5_recording_a_macro) {
      /* We need to kill the macro definition and execute the
         ESC-E. */
      if (operation == (uint8)'E') {
        MacroInfo *macro_info = get_macro_info(pcl5_ctxt) ;

        if (! stop_macro_definition(pcl5_ctxt, macro_info))
          return FALSE ;

        remove_macro_definition(pcl5_ctxt, macro_info) ;

        if (! pcl5op_E(pcl5_ctxt, FALSE, value))
          return FALSE ;
      }
      return TRUE ;
    }

    /* Are we in raster graphics mode or character data mode? */
    if (pcl5_ctxt->interpreter_mode == RASTER_GRAPHICS_MODE ||
        pcl5_ctxt->interpreter_mode == CHARACTER_DATA_MODE) {
      if ( (mode_mask & EG && pcl5_ctxt->interpreter_mode == RASTER_GRAPHICS_MODE) ||
           (mode_mask & ECD && pcl5_ctxt->interpreter_mode == CHARACTER_DATA_MODE) ) {
        /* Cache command for execution after the raster graphics has
           finished. */
        pcl5_ctxt->is_cached_command = TRUE ;
        pcl5_ctxt->cached_operation = operation ;
        pcl5_ctxt->cached_group_char = group_char ;
        pcl5_ctxt->cached_value = *p_value;
        pcl5_ctxt->cached_termination_char = termination_char ;

        if (mode_mask & EG && pcl5_ctxt->interpreter_mode == RASTER_GRAPHICS_MODE) {
          /* End raster graphics */
          return end_raster_graphics_callback(pcl5_ctxt, TRUE) ;
        } else {
          /* Implicit end character data mode. */
          return implicit_end_character_data(pcl5_ctxt) ;
        }

      }
    }

    /* Should we ignore this command? */
    if ( (pcl5_ctxt->interpreter_mode & mode_mask) == 0) {
#if defined(DEBUG_BUILD)
      if ( debug_pcl5 & PCL5_CONTROL ) {
        if ( isprint(operation) ) {
          monitorf((uint8*)"ignore ESC %c\n", (char)operation) ;
        } else {
          monitorf((uint8*)"ignore ESC <%02x>\n", (uint32)operation) ;
        }
      }
#endif
      return TRUE ;
    }

#ifdef RQ64403
    /* We want to minimise calling this function as much as possible so
       that the number of times fonts etc.. get set up before a text run
       starts is reduced. We can delay the ending of a text run as some
       commands have no effect should they reside between consequtive
       text runs. */
    if (! pcl5_set_text_run_restart_required(pcl5_ctxt, FALSE))
      return FALSE ;
#endif
    return op_callback(pcl5_ctxt, FALSE, value) ;
  }

#if defined(DEBUG_BUILD)
  if ( isprint(operation) ) {
    monitorf((uint8*)"%%%%[ Warning: Unknown PCL5 two character command: ESC %c ] %%%%\n",
             (char)operation) ;
  } else {
    monitorf((uint8*)"%%%%[ Warning: Unknown PCL5 two character command: ESC <%02x> ] %%%%\n",
             (uint32)operation) ;
  }
#endif

  return TRUE ;
}

/* ============================================================================
 * PCL5 dispatch
 * ============================================================================
 */

/* What type of PCL5 operator do we have? */
enum {
  PCL5_UNKNOWN = 0,
  PCL5_TWO_CHAR_ESCAPE,
  PCL5_PARAMETERIZED_ESCAPE
} ;

typedef Bool (*PCL5_OperatorTypeCallback)(PCL5Context *pcl5_ctxt, uint8 operation,
                                          PCL5Operator op_callback, uint32 mode_mask,
                                          uint8 group_char, PCL_VALUE* p_value,
                                          uint8 termination_char) ;

struct PCL5ops {
  uint8 type ;
  uint32 mode_mask ;
  PCL5_OperatorTypeCallback op_type_callback ;
  PCL5Operator op_callback ;
} ;

/* List of all possible command characters for quick lookup. Function
   callback should never be NULL. */
#define MAX_NUM_PCL5_ESCAPE_CHARS 256
static struct PCL5ops escape_chars[MAX_NUM_PCL5_ESCAPE_CHARS] =
  {
    /*  0 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*  1 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*  2 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*  3 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*  4 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*  5 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*  6 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*  7 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*  8 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*  9 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 10 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 11 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 12 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 13 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 14 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 15 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 16 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 17 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 18 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 19 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 20 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 21 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 22 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 23 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 24 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 25 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 26 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 27 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 28 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 29 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 30 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 31 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 32 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /* 33 */ { PCL5_PARAMETERIZED_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 34 */ { PCL5_PARAMETERIZED_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 35 */ { PCL5_PARAMETERIZED_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 36 */ { PCL5_PARAMETERIZED_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 37 */ { PCL5_PARAMETERIZED_ESCAPE, 0, pcl5op_parameterized_escape, NULL}, /* % */
    /* 38 */ { PCL5_PARAMETERIZED_ESCAPE, 0, pcl5op_parameterized_escape, NULL}, /* & */
    /* 39 */ { PCL5_PARAMETERIZED_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 40 */ { PCL5_PARAMETERIZED_ESCAPE, 0, pcl5op_possible_symbol_set, NULL}, /* ( */
    /* 41 */ { PCL5_PARAMETERIZED_ESCAPE, 0, pcl5op_possible_symbol_set, NULL}, /* ) */
    /* 42 */ { PCL5_PARAMETERIZED_ESCAPE, 0, pcl5op_parameterized_escape, NULL}, /* * */
    /* 43 */ { PCL5_PARAMETERIZED_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 44 */ { PCL5_PARAMETERIZED_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 45 */ { PCL5_PARAMETERIZED_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 46 */ { PCL5_PARAMETERIZED_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 47 */ { PCL5_PARAMETERIZED_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 48 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 49 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_two_char_escape, NULL}, /* 1 */
    /* 50 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_two_char_escape, NULL}, /* 2 */
    /* 51 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_two_char_escape, NULL}, /* 3 */
    /* 52 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_two_char_escape, NULL}, /* 4 */
    /* 53 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_two_char_escape, NULL}, /* 5 */
    /* 54 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 55 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 56 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 57 */ { PCL5_TWO_CHAR_ESCAPE,      ECD | PC | PE | ME | MC | EG, pcl5op_two_char_escape, pcl5op_9}, /* 9 */
    /* 58 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 59 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 60 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 61 */ { PCL5_TWO_CHAR_ESCAPE, ECD | PC | PE | ME | MC | EG, pcl5op_two_char_escape, pcl5op_equals}, /* = */
    /* 62 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 63 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_two_char_escape, NULL}, /* ? */
    /* 64 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 65 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 66 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 67 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 68 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 69 */ { PCL5_TWO_CHAR_ESCAPE,      ECD | PC | PE | EG,           pcl5op_two_char_escape, pcl5op_E}, /* E */
    /* 70 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 71 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 72 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 73 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_two_char_escape, NULL}, /* I */
    /* 74 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 75 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 76 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 77 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 78 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 79 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 80 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 81 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 82 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 83 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 84 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 85 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 86 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 87 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 88 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 89 */ { PCL5_TWO_CHAR_ESCAPE,      ECD | PC | PE | ME | MC | EG, pcl5op_two_char_escape, pcl5op_Y}, /* Y */
    /* 90 */ { PCL5_TWO_CHAR_ESCAPE,      ECD | PC | PE | ME | MC | EG, pcl5op_two_char_escape, pcl5op_Z}, /* Z */
    /* 91 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 92 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 93 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 94 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_two_char_escape, NULL}, /* ^ */
    /* 95 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 96 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 97 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 98 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /* 99 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*100 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*101 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*102 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*103 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*104 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*105 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*106 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*107 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*108 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*109 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*110 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_two_char_escape, NULL}, /* n */
    /*111 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_two_char_escape, NULL}, /* o */
    /*112 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*113 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*114 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*115 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*116 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*117 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*118 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*119 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*120 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*121 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*122 */ { PCL5_TWO_CHAR_ESCAPE,      ECD | PC | PE | ME | MC | EG, pcl5op_two_char_escape, pcl5op_z}, /* z */
    /*123 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*124 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*125 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*126 */ { PCL5_TWO_CHAR_ESCAPE, 0, pcl5op_unknown, NULL},
    /*127 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*128 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*129 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*130 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*131 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*132 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*133 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*134 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*135 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*136 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*137 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*138 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*139 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*140 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*141 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*142 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*143 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*144 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*145 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*146 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*147 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*148 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*149 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*150 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*151 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*152 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*153 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*154 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*155 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*156 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*157 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*158 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*159 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*160 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*161 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*162 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*163 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*164 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*165 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*166 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*167 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*168 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*169 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*170 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*171 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*172 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*173 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*174 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*175 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*176 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*177 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*178 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*179 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*180 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*181 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*182 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*183 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*184 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*185 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*186 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*187 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*188 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*189 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*190 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*191 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*192 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*193 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*194 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*195 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*196 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*197 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*198 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*199 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*200 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*201 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*202 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*203 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*204 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*205 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*206 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*207 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*208 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*209 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*210 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*211 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*212 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*213 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*214 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*215 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*216 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*217 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*218 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*219 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*220 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*221 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*222 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*223 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*224 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*225 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*226 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*227 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*228 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*229 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*230 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*231 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*232 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*233 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*234 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*235 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*236 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*237 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*238 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*239 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*240 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*241 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*242 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*243 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*244 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*245 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*246 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*247 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*248 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*249 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*250 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*251 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*252 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*253 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*254 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
    /*255 */ { PCL5_UNKNOWN, 0, pcl5op_unknown, NULL},
} ;

/* ============================================================================
 */

/* Parametterized Escape Sequences or a Two-Character Escape
   Sequence. We don't know yet. */
Bool pcl5_dispatch_escape_sequence(PCL5Context *pcl5_ctxt, uint8 operation)
{
  PCL5PrintState *print_state ;
  PCL5_OperatorTypeCallback op_type_callback ;
  PCL5Operator op_callback = escape_chars[operation].op_callback ;
  uint32 mode_mask = escape_chars[operation].mode_mask ;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;


  switch (escape_chars[operation].type) {
  case PCL5_TWO_CHAR_ESCAPE: /* Two-Character Escape Sequences. */
    print_state = pcl5_ctxt->print_state ;
    op_type_callback = escape_chars[operation].op_type_callback ;

    if (pcl5_ctxt->is_cached_command) {
      pcl5_ctxt->is_cached_command = FALSE ;
    }

    HQASSERT((is_2char_cmd(operation)),
             "invalid two char operation") ;

#if defined(DEBUG_BUILD)
    if ( debug_pcl5 & PCL5_CONTROL ) {
      monitorf((uint8*)"ESC %c\n", (char)operation) ;
    }
#endif

    return (op_type_callback)(pcl5_ctxt, operation,
                              op_callback, mode_mask,
                              /* group char */ 0,
                              &pcl_zero_value,
                              /* termination char */ 0) ;

  case PCL5_PARAMETERIZED_ESCAPE:
    {
      int32 res ;
      uint8 termination_char, group_char ;
      PCL_VALUE value;

      group_char = pcl5_ctxt->cached_group_char ;

      if (pcl5_ctxt->is_cached_command) {
        value = pcl5_ctxt->cached_value;
        termination_char = pcl5_ctxt->cached_termination_char ;
        pcl5_ctxt->is_cached_command = FALSE ;

      } else {
        /* Scan in parameters. */
        if ( (res = pcl5_scan_params(pcl5_ctxt, operation, &group_char, &value, &termination_char)) < 0) {
          return FALSE ;
        } else if (res == 0) {
          pcl5_ctxt->is_combined_command = FALSE ;
          pcl5_ctxt->is_cached_command = FALSE ;
          return TRUE ;
        }
      }

      /* Deal with combined escape sequences. */
      for (;;) {
        /* Parametterized Escape Sequences. */
        HQASSERT((is_param_cmd(operation)),
                 "invalid parameterized operation") ;
        op_type_callback = escape_chars[operation].op_type_callback ;
        HQASSERT(op_type_callback != NULL, "op_type_callback is NULL") ;

        if (! (op_type_callback)(pcl5_ctxt, operation, op_callback, mode_mask, group_char, &value, termination_char)) {
          return FALSE ;
        }

        if (pcl5_ctxt->is_cached_command)
          return TRUE ;

        if (pcl5_ctxt->is_combined_command) {
          operation = pcl5_ctxt->cached_operation ;
          group_char = pcl5_ctxt->cached_group_char ;
        } else {
          return TRUE ;
        }

        if (suspend_pcl5_interpreter)
          return TRUE ;

        /* Scan in next combined escape sequence parameters. */
        if ( (res = pcl5_scan_params(pcl5_ctxt, operation, &group_char, &value, &termination_char)) < 0) {
          return FALSE ;
        } else if (res == 0) {
          pcl5_ctxt->is_combined_command = FALSE ;
          pcl5_ctxt->is_cached_command = FALSE ;
          return TRUE ;
        }
        /* We were unable to scan another set of parameters, so we
           best abort out. */
        if (res == 0 || ! termination_char) {
          pcl5_ctxt->is_combined_command = FALSE ;
          return TRUE ;
        }

      }
      /* HQFAIL("Should never reach here.") ; Commented out to avoid
         "Unreachable code" warning from compilers. */
    }
    break ;
  default:
    /* We don't care about invalid operators (for now). */
    break ;
  }

  return TRUE ;
}

static Bool pcl5_dispatch_characters(PCL5Context *pcl5_ctxt, int32 byte)
{
  PCL5PrintState *print_state = pcl5_ctxt->print_state;
  FontSelInfo *font_sel_info ;
  FontInfo *font_info = NULL ;
  Font *font ;
  uint16 wide_ch ;
  Bool eof_reached = FALSE ;
  Bool keep_processing = TRUE ;
  Bool status = TRUE ;

  if (! pcl5_recording_a_macro) {
    if (! pcl5_start_text_run(pcl5_ctxt, &font_sel_info, &font_info, &font))
      return FALSE ;
  }

  while (keep_processing) {

    if (byte == ESCAPE) {
      UnGetc(byte, pcl5_ctxt->flptr) ;
      break ;

    } else {
      /* If we are recording a macro, Getc will have recorded the byte
         so just move onto the next byte. */
      if (pcl5_recording_a_macro) {
        if ((byte = Getc(pcl5_ctxt->flptr)) == EOF)
          break ;
        continue ;
      }

      /* Are we in raster graphics mode or character data mode? */
      if (pcl5_ctxt->interpreter_mode == RASTER_GRAPHICS_MODE ||
          pcl5_ctxt->interpreter_mode == CHARACTER_DATA_MODE) {

        UnGetc(byte, pcl5_ctxt->flptr) ;

        if (pcl5_ctxt->interpreter_mode == RASTER_GRAPHICS_MODE) {
          /* End raster graphics */
          status = end_raster_graphics_callback(pcl5_ctxt, TRUE) ;
        } else {
          /* Implicit end character data mode. */
          status = implicit_end_character_data(pcl5_ctxt) ;
        }
        break ;
      }

      wide_ch = (uint8)byte ;

      /* Only dispatch the 2 byte char if two byte character support
         is enabled. */
      if ( pcl5_ctxt->config_params.two_byte_char_support_enabled ) {
        /* Deal with two byte characters. */
        if ( is_first_byte_of_two_byte_char(pcl5_ctxt, wide_ch)) {
          add_second_byte_to_two_byte_char(pcl5_ctxt, &wide_ch, &eof_reached) ;
          if (eof_reached)
            break ;
        }
      }

      /* Control codes. */
      switch (wide_ch) {
      case SPACE:
        keep_processing = status = pcl5_text_character(pcl5_ctxt, font_info, &font, SPACE) ;
        break ;

      case CR:
        keep_processing = status = pcl5op_control_CR(pcl5_ctxt) ;
        break ;

      case LF:
        keep_processing = status = pcl5op_control_LF(pcl5_ctxt) ;
        break ;

      case BS:
        keep_processing = status = pcl5op_control_BS(pcl5_ctxt) ;
        break ;

      case HT:
        keep_processing = status = pcl5op_control_HT(pcl5_ctxt) ;
        break ;

      case FF:
#ifdef RQ64403
        /* We need to ensure that the font gets setup again, hence
           exit out of text run loop and flag that a restart is
           required. */
        status = pcl5op_control_FF(pcl5_ctxt) &&
                 pcl5_set_text_run_restart_required(pcl5_ctxt, FALSE) ;
        keep_processing = FALSE ;
#else
        keep_processing = status = pcl5op_control_FF(pcl5_ctxt);
#endif
        break ;

      case SI:
#ifdef RQ64403
        /* We need to ensure that the font gets setup again, hence
           exit out of text run loop and flag that a restart is
           required. */
        status = pcl5op_control_SI(pcl5_ctxt) &&
                 pcl5_set_text_run_restart_required(pcl5_ctxt, FALSE) ;
        keep_processing = FALSE ;
#else
        keep_processing = status = pcl5op_control_SI(pcl5_ctxt);
#endif
        break ;

      case SO:
#ifdef RQ64403
        /* We need to ensure that the font gets setup again, hence
           exit out of text run loop and flag that a restart is
           required. */
        status = pcl5op_control_SO(pcl5_ctxt) &&
                 pcl5_set_text_run_restart_required(pcl5_ctxt, FALSE) ;
        keep_processing = FALSE ;
#else
        keep_processing = status = pcl5op_control_SO(pcl5_ctxt);
#endif
        break ;

        /* Control codes which have no effect at all. */
      case NUL:
      case BEL:
      case VT:
        break;

      default:
        /* Its not a control code so dispatch the character. */
        keep_processing = status = pcl5_text_character(pcl5_ctxt, font_info, &font, wide_ch) ;
      }

    } /* end is not an escape character */

    /* Something has ended the text run so get out of this loop */
    /* N.B. todo Confirm this is needed.  Assuming it is then surely
     * it would have been needed before anyway?
     */
    if (! pcl5_recording_a_macro && !print_state->font_state.within_text_run)
      keep_processing = FALSE ;

    /* Get next character. */
    if (keep_processing && (byte = Getc(pcl5_ctxt->flptr)) == EOF)
      break ;

  } /* end while keep processing loop */

  /* Let's not end the text run if we already have */
  if (! pcl5_recording_a_macro && print_state->font_state.within_text_run) {
    status = status && pcl5_end_text_run(pcl5_ctxt, FALSE) ;
  }

  return status ;
}

/* ============================================================================
 * PCL5 interpreter
 * ============================================================================
 */

void pcl5_suspend_execops()
{
  suspend_pcl5_interpreter = TRUE ;
}

Bool pcl5_get_suspend_state()
{
  return suspend_pcl5_interpreter ;
}

/* Start interpreting the provided stream as PCL5. Note that this
   function gets called recursively when executing macros and reading
   raster graphics etc.. */
Bool pcl5_execops(PCL5Context *pcl5_ctxt)
{
  int32 res ;
  uint8 operation ;
  int32 ch ;
  Bool status = TRUE ;
  int saved_dl_safe_recursion = dl_safe_recursion ;
  int saved_gc_safety_level = gc_safety_level;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  suspend_pcl5_interpreter = FALSE ;

  /* If we are not done we either have control codes, PCL commands or
     more text. */
  while (! suspend_pcl5_interpreter && status) {
    gc_unsafe_from_here_on();
    /* This means we suspended the interpreter within a combined
       command, so we must not look for an escape etc. */
    if (pcl5_ctxt->is_combined_command || pcl5_ctxt->is_cached_command) {
      operation = pcl5_ctxt->cached_operation ;

      /* dispatch escape sequence */
      if (! pcl5_dispatch_escape_sequence(pcl5_ctxt, operation)) {
        status = FALSE ;
        continue ;
      }

    } else {

      if ((ch = Getc(pcl5_ctxt->flptr)) == EOF) {
        suspend_pcl5_interpreter = TRUE ;

        if ( (pcl5_ctxt->interpreter_mode == RASTER_GRAPHICS_MODE ||
              pcl5_ctxt->interpreter_mode == CHARACTER_DATA_MODE) ) {

          if (pcl5_ctxt->interpreter_mode == RASTER_GRAPHICS_MODE) {
            status = end_raster_graphics_callback(pcl5_ctxt, TRUE) ;

            /* In this case ensure the interpreter mode is definitely reset */
            if (pcl5_ctxt->interpreter_mode_on_start_graphics != MODE_NOT_SET) {
              pcl5_ctxt->interpreter_mode = pcl5_ctxt->interpreter_mode_on_start_graphics ;
              pcl5_ctxt->interpreter_mode_on_start_graphics = MODE_NOT_SET ;
              pcl5_ctxt->interpreter_mode_end_graphics_pending = FALSE ;
            }
          } else {
            status = implicit_end_character_data(pcl5_ctxt) ;
          }
        }

      } else {

        switch (ch) {
        case ESCAPE:
          if ((res = pcl5_scan_escape_sequence(pcl5_ctxt, &operation)) > 0) {
            /* dispatch escape sequence */
            if (! pcl5_dispatch_escape_sequence(pcl5_ctxt, operation)) {
              status = FALSE ;
              continue ;
            }
          } else if (res < 0) {
              status = FALSE ;
              continue ;
          }
          break ;
        default:
          /* Its going to be either a control code or character. */
          if (! pcl5_dispatch_characters(pcl5_ctxt, ch)) {
            status = FALSE ;
            continue ;
          }
          break ;
        }
      }

      /* Are we done? */
      if ( pcl5_ctxt->UEL_seen ) {
        suspend_pcl5_interpreter = TRUE ;
      }
    }

    dl_safe_recursion = saved_dl_safe_recursion ;
    gc_safety_level = saved_gc_safety_level;

    /* Handle low memory, interrupts, timeouts, etc. */
    if ( mm_memory_is_low || dosomeaction ) {
      if ( !handleNormalAction() )
        return FALSE ;
    }
  }

  /* Reset the FILELIST. */
  suspend_pcl5_interpreter = FALSE ;

  return status ;
}

/**
 * Disable PCL rendering.
 */
static void deconfigureRenderer(corecontext_t* corecontext)
{
  pclGstateEnable(corecontext, FALSE, FALSE);
}

Bool pcl5_execute_stream(corecontext_t* corecontext,
                         FILELIST* flptr,
                         OBJECT* odict,
                         int32 pass_through_type,
                         PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO* state_info)
{
  PCL5Context *pcl5_ctxt ;
  PCL5PrintState *print_state ;
  Bool status = TRUE ;
  PCL5PrintEnvironment *mpe ;
  JobCtrlInfo *job_info ;
  PageCtrlInfo *page_info ;
  FontInfo *font_info ;
  FontSelInfo* font_sel_info;
  PrintModelInfo *print_info ;
  PCL5Numeric value ;
  Bool first_passthrough_this_page ;

  HQASSERT(pass_through_type == PCLXL_NO_PASS_THROUGH ||
           pass_through_type == PCLXL_SNIPPET_JOB_PASS_THROUGH ||
           pass_through_type == PCLXL_WHOLE_JOB_PASS_THROUGH,
           "Unexpected XL passthrough type") ;

  /* We should only have a cached context if we're doing an XL passthrough */
  HQASSERT(((pass_through_type == PCLXL_NO_PASS_THROUGH) || (state_info != NULL)),
           "PCL5 Passthrough needs a non-NULL PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO") ;

  if ( (state_info != NULL) &&
       (state_info->pcl5_ctxt) != NULL )
  {
    /*
     * There is already a PCL5 "context" stored
     * in the passthrough state info.
     * So we can simply go ahead and re-use this one
     */

    pcl5_ctxt = state_info->pcl5_ctxt;

    HQASSERT((pcl5_ctxt->state_info == state_info),
             "Previous passthrough state is corrupt");
  }
  else if ( !pcl5_context_create(&pcl5_ctxt,
                                 corecontext,
                                 odict,
                                 state_info,
                                 pass_through_type) )
  {
    /*
     * There was no PCL5 context in the passthrough state info
     * So we attempted to create one,
     * but we failed for some reason.
     * Let's hope we logged some meaningful reason for this failure
     */

    return FALSE ;
  }
  else
  {
    /*
     * We have successfully created brand new PCL5 "context"
     * and it is already accessible via pcl5_ctxt
     *
     * Remember to initialize the "state_info" for pass through
     * Note that in the "normal" (i.e. non-passthrough case)
     * this will be NULL.
     */

    pcl5_ctxt->state_info = state_info;

    /*
     * Note that we only initialize the state_info->pcl5_ctxt
     * when we have *finished* setting up the pcl5_ctxt
     *
     * This is so that we can spot that this is the very first
     * PCL[5] Passthrough that we have encountered
     */
  }

  /* Sanitise CTM after setpagedevice to ensure sufficient accuracy in CTMs. */
  if ( !ctm_sanitise_default_matrix(pcl5_ctxt) )
    return FALSE;

  /* We now have a PCL5Context though it may need some adjustment */
  pcl5_ctxt->flptr = flptr;
  pcl5_ctxt->pass_through_mode = pass_through_type ;

  /* Record the current device settings in the working MPE if necessary,
   * then make any other necessary adjustments to the MPE.
   */
  if ( (pass_through_type == PCLXL_SNIPPET_JOB_PASS_THROUGH) ||
       (pass_through_type == PCLXL_WHOLE_JOB_PASS_THROUGH) )
  {
    HQASSERT(pcl5_ctxt->state_info != NULL, "PCL5 Passthrough needs a non-NULL PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO");

    /* We do this lazily now */
    if (! pcl5_flush_text_run(pcl5_ctxt, 1))
      return FALSE ;

    print_state = pcl5_ctxt->print_state ;
    HQASSERT(print_state != NULL, "print_state is NULL") ;
    mpe = get_current_mpe(pcl5_ctxt) ;
    HQASSERT(mpe != NULL, "mpe is NULL") ;
    font_info = &(mpe->font_info) ;
    HQASSERT(font_info != NULL, "font_info is NULL") ;
    font_sel_info = get_font_sel_info(font_info, PRIMARY);
    HQASSERT(font_sel_info, "font_sel_info is NULL") ;
    job_info = get_job_ctrl_info(pcl5_ctxt) ;
    HQASSERT(job_info != NULL, "job_info is NULL") ;
    page_info = get_page_ctrl_info(pcl5_ctxt) ;
    HQASSERT(page_info != NULL, "page_info is NULL") ;
    print_info = get_print_model(pcl5_ctxt) ;
    HQASSERT(print_info != NULL, "print_info is NULL") ;

    /* Find out if this is the first XL passthrough (of any type) on this page */
    first_passthrough_this_page =
      ((pcl5_ctxt->state_info->pcl5_ctxt == NULL) ||
       (pcl5_ctxt->state_info->pclxl_duplex_page_side_count != pcl5_ctxt->print_state->duplex_page_number));

    if (first_passthrough_this_page)
    {
      /* N.B. This sets up the working MPE whereas the PCL5 context creation
       * above will have set up the base level MPE.
       * The distinction is important because it will allow any
       * subsequent whole job passthrough using this PCL5Context to
       * perform a meaningful reset.
       *
       * Seems we need to re-establish any pagecontrol stuff and HMI and jobcontrol stuff
       * to be the same as it was at the end of the previous Passthrough
       * But some other items, including num_copies and VMI are reset back to the PCL5 defaults
       *
       * Therefore for those items that we must preserve/carry-over from the previous Passthrough
       * we take a local on-stack copy. Then we perform a full reset of the MPE back to the PCL5 defaults.
       * And then we re-establish the to-be-preserved values back into the MPE
       *
       * We have empirically found that the HP4700 preserves the following values
       * from one Passthrough onto the next (page):
       *
       *    perforation skip
       *    line termination
       *    pcl unit size
       *    HMI
       */
      {
        Bool         perforation_skip = page_info->perforation_skip ;
        uint32       line_termination = page_info->line_termination ;
        uint32       pcl_unit_size = page_info->pcl_unit_size ;
        int32        hmi = page_info->hmi ;

        *job_info = get_default_mpe(pcl5_ctxt->print_state)->job_ctrl ;
        *page_info = get_default_mpe(pcl5_ctxt->print_state)->page_ctrl ;

        page_info->perforation_skip = perforation_skip ;
        page_info->line_termination = line_termination ;
        page_info->pcl_unit_size = pcl_unit_size ;
        page_info->hmi = hmi ;
      }

      if (! set_MPE_from_device(pcl5_ctxt, PCL5_STATE_CHANGE_FOR_XL_PASSTHROUGH))
        return FALSE ;

      /* N.B. These requested duplex settings are strictly not needed for snippet
       *      passthrough, but probably worth getting anyway for debug reasons.
       */
      /** \todo Do we want to make these page device values instead? */
      job_info->requested_duplex = pcl5_ctxt->state_info->pclxl_requested_duplex ;

      if (job_info->requested_duplex) {
        if (pcl5_ctxt->state_info->pclxl_requested_duplex_binding == 0)
          job_info->requested_binding = SHORT_EDGE ;
        else if (pcl5_ctxt->state_info->pclxl_requested_duplex_binding == 1)
          job_info->requested_binding = LONG_EDGE ;
      }
      else
        job_info->requested_binding = SIMPLEX ;
    }

    /* Now make any further MPE adjustments required */
    if (pass_through_type == PCLXL_WHOLE_JOB_PASS_THROUGH) {

      /* Grab the save object */
      HQASSERT(pcl5_ctxt->state_info->pclxl_save_object != NULL,
               "Null XL save object pointer") ;
      HQASSERT(oType(*pcl5_ctxt->state_info->pclxl_save_object) != ONULL,
               "Null object type for XL save object") ;

      Copy(&(pcl5_ctxt->print_state->save), pcl5_ctxt->state_info->pclxl_save_object) ;
    }

    /* Remainder of changes are for both passthrough types */
    if ( pcl5_ctxt->state_info->pcl5_ctxt == NULL )
    {

      /*
       * Set the unit of measure
       *
       * Note that PCLXL has separate units of measure in the x and y directions
       * but PCL5 only has a single unit of measure for both page axies
       */
      value.real = pcl5_ctxt->state_info->units_per_measure_x ;
      (void)pcl5op_ampersand_u_D(pcl5_ctxt, NOSIGN, value) ;

      /** \todo Check this should only be done in this case, and is not
       *   meant to affect subsequent PCL5 pixel placement changes.
       */
      set_pixel_placement(print_info, PCL5_GRID_CENTERED) ;
    }

    /*
     * The last PCL5 fontselection criteria from PCLXL (if these have been
     * initialized and have changed since we were last in PCL5).
     * These are only applied to the primary font.
     */
    /** \todo The passthrough interface needs serious review.  It seems
     *  likely that PCL5 fontselection criteria should be passed back to
     *  PCLXL, (which is currently rather awkward), and then copied in here
     *  every time, rather than using the pcl5_informed field.
     */
    /** \todo Check that this is supposed to be copied into the primary
     *  font, rather than the currently active PCL5 font.
     */
    if ( state_info->font_sel_initialized &&
         state_info->font_sel_criteria_changed ) {
      font_sel_info->symbol_set = state_info->font_sel_symbol_set;
      font_sel_info->spacing = state_info->font_sel_spacing;
      font_sel_info->pitch = (PCL5Real) state_info->font_sel_pitch;
      font_sel_info->height = (PCL5Real) state_info->font_sel_height;
      font_sel_info->style = state_info->font_sel_style;
      font_sel_info->stroke_weight = state_info->font_sel_weight;
      font_sel_info->typeface = state_info->font_sel_typeface;

      handle_sel_criteria_change(pcl5_ctxt, font_info, PRIMARY) ;

      state_info->font_sel_criteria_changed = FALSE;
      font_sel_info->criteria_changed = FALSE;
    }

    handle_ps_font_change(pcl5_ctxt) ;
    reset_last_pcl5_and_hpgl_colors();

    if (first_passthrough_this_page) {
      PageCtrlInfo *page_ctrl = get_page_ctrl_info(pcl5_ctxt) ;
      HQASSERT(page_ctrl != NULL, "page_ctrl is NULL") ;

      if (page_ctrl->vmi != 0) {
        set_top_margin(pcl5_ctxt, PASS_THROUGH_MARGIN / page_ctrl->vmi) ;
        reset_text_length(pcl5_ctxt) ;
        set_text_length(pcl5_ctxt, (page_ctrl->max_text_length - PASS_THROUGH_MARGIN) / page_ctrl->vmi) ;

        /** \todo Should this change if we already had a cached context?
         *  (The HPGL2 picture frame wouldn't normally change on a change
         *   of margins).
         */

        hpgl2_handle_PCL_page_change(pcl5_ctxt) ;
      }
    }

     /* Ensure that we do any necessary DEVICE_SETG in case we are in the middle of
     * a PCL5 text run, and will not go through pcl5_start_text_run again.
     */
    /** \todo Can this occur? If so, this could be moved up to one of the cases
     *  above, (e.g. it will not apply if we are creating a whole new PCL5 context).
     */
    print_state->setg_required += 1 ;

    /*
     * Finally we store a reference to this by now fully initialized
     * pcl5_ctxt into the passthrough state info
     */

    pcl5_ctxt->state_info->pcl5_ctxt = pcl5_ctxt ;

#ifdef RQ64403
    if (! pcl5_set_text_run_restart_required(pcl5_ctxt, FALSE))
      return FALSE ;
#endif
  } /* End of PCL[5] Passthrough specific initialization */

  /*
   * Beginning of "normal" *or* PCL[5] Passthrough
   * PCL5 operator stream handling
   */

  if ( !reset_page(pcl5_ctxt))
  {
    (void)pcl5_context_destroy(&pcl5_ctxt);

    if ( pass_through_type == PCLXL_NO_PASS_THROUGH )
    {
      /*
       * If we were *NOT* handling a PCL[5] Passthrough
       * then we can go ahead and "deconfigure" the renderer
       */

      deconfigureRenderer(corecontext);
    }
    else
    {
      /*
       * Otherwise we must have just failed to handle
       * a PCL[5] Passthrough (and thus destroyed the PCL5 context)
       * So we had better make sure that the [PCLXL] caller
       * doesn't ask us to destroy it again
       */

      state_info->pcl5_ctxt = NULL;
    }

    return FALSE ;
  }

  /*
   * If doing passthrough inherit the XL cursor position if valid
   * and set up the page numbering.
   */

  if ( (pass_through_type == PCLXL_SNIPPET_JOB_PASS_THROUGH) ||
       (pass_through_type == PCLXL_WHOLE_JOB_PASS_THROUGH) )
  {
    OMATRIX inverse_ctm ;
    OMATRIX *ctm ;
    double xpos, ypos ;

    /*
     * If there is a current PCLXL Cursor position
     * Then we move the PCL5 cursor to this same place
     */
    if (pcl5_ctxt->state_info->current_point) {
      ctm = ctm_current(get_pcl5_ctms(pcl5_ctxt)) ;

      (void)matrix_inverse(ctm, &inverse_ctm) ;
      MATRIX_TRANSFORM_XY(pcl5_ctxt->state_info->current_point_x,
                          pcl5_ctxt->state_info->current_point_y,
                          xpos, ypos, &inverse_ctm) ;

      set_cursor_position(pcl5_ctxt, xpos, ypos) ;
    }

    /*
     * Synchronize PCL5 page number and duplex page number
     * to start with the same values as PCLXL
     *
     * This allows us to simply transfer back these page numbers
     * at the end of the passthrough
     * (see pcl5_update_passthrough_state_info() below)
     */

    pcl5_ctxt->print_state->page_number = pcl5_ctxt->state_info->pclxl_page_number ;
    pcl5_ctxt->print_state->duplex_page_number = pcl5_ctxt->state_info->pclxl_duplex_page_side_count ;
  }

  /******************************************
   * Interpret the PCL5 stream.
   ******************************************/

  status = status && pcl5_execops(pcl5_ctxt) ;

  if ( pass_through_type == PCLXL_NO_PASS_THROUGH )
  {
    /* Seems that whole job pass through does not throw a page when
       its the last page? */
    status = status && throw_page(pcl5_ctxt, FALSE, FALSE) ;

    /* Destroy the PCL5 execution context. */
    status = pcl5_context_destroy(&pcl5_ctxt) && status ;

    HQASSERT(((state_info == NULL) || (state_info->pcl5_ctxt == NULL)),
             "We were not expecting to have any PCL[5] Passthrough state information here");
  }

  if ( (state_info == NULL) ||
       (state_info->pcl5_ctxt == NULL) )
  {
    /*
     * We either never had any PCL[5] Passthrough state info
     * or we have already been asked to end/clean-up the passthrough
     *
     * So we (PCL5) can make the decision to "deconfigure" the renderer
     */

    deconfigureRenderer(corecontext);
  }

  return status;
}

Bool
pcl5_update_passthrough_state_info(PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO* state_info)
{
  PCL5PrintState* print_state;
  JobCtrlInfo* job_info;
  PCL5Ctms* ctms;
  PCL5PrintEnvironment *mpe;
  FontInfo *font_info;
  FontSelInfo* font_sel_info;
  PageCtrlInfo *page_info ;

  HQASSERT(state_info != NULL, "Cannot update a NULL Passthrough state_info");
  HQASSERT(state_info->pcl5_ctxt != NULL, "state_info has not been passed through PCL5");
  HQASSERT(state_info->pcl5_ctxt->state_info == state_info, "state_info does not match PCL5 state_info");

  print_state = state_info->pcl5_ctxt->print_state;

  job_info = get_job_ctrl_info(state_info->pcl5_ctxt);

  ctms = get_pcl5_ctms(state_info->pcl5_ctxt);

  mpe = get_current_mpe(state_info->pcl5_ctxt);

  page_info = get_page_ctrl_info(state_info->pcl5_ctxt) ;

  HQASSERT(print_state != NULL, "print_state is NULL");
  HQASSERT(job_info != NULL, "job_info is NULL");
  HQASSERT(ctms != NULL, "ctms is NULL");
  HQASSERT(mpe != NULL, "mpe is NULL");
  HQASSERT(page_info != NULL, "page_info is NULL") ;

  font_info = &(mpe->font_info);

  HQASSERT(font_info != NULL, "font_info is NULL");

  font_sel_info = get_font_sel_info(font_info, PRIMARY);

  HQASSERT(font_sel_info, "font_sel_info is NULL") ;

  MATRIX_COPY(&state_info->physical_page_ctm, ctm_base(ctms));

  if ( displaylistisempty(state_info->pcl5_ctxt->corecontext->page) )
  {
    /*
     * This passthrough doesn't seem to have made any marks on the page
     * In this case, even if there has been a PCL5 reset command
     * or a page size or page orientation change,
     * we none-the-less revert to the PCLXL page size and orientation
     */

    state_info->use_pcl5_page_setup = FALSE;
  }

  state_info->pcl5_page_clip_offsets[0] = page_info->clip[0];
  state_info->pcl5_page_clip_offsets[1] = page_info->clip[1];
  state_info->pcl5_page_clip_offsets[2] = page_info->physical_page_width - page_info->clip[2];
  state_info->pcl5_page_clip_offsets[3] = page_info->physical_page_length - page_info->clip[3];

  state_info->pclxl_page_number = print_state->page_number;

  state_info->pclxl_duplex = job_info->duplex;

  state_info->pclxl_duplex_page_side_count = print_state->duplex_page_number;

  if ( font_sel_info->criteria_changed )
  {
    state_info->font_sel_criteria_changed =
      state_info->font_sel_initialized = TRUE;

    state_info->font_sel_symbol_set =  font_sel_info->symbol_set;
    state_info->font_sel_spacing =  font_sel_info->spacing;
    state_info->font_sel_pitch = (float) font_sel_info->pitch;
    state_info->font_sel_height = (float) font_sel_info->height;
    state_info->font_sel_style =  font_sel_info->style;
    state_info->font_sel_weight =  font_sel_info->stroke_weight;
    state_info->font_sel_typeface =  font_sel_info->typeface;

    font_sel_info->criteria_changed = FALSE;
  }

  return TRUE;
}

void pcl5_destroy_passthrough_context(PCLXL_TO_PCL5_PASSTHROUGH_STATE_INFO* state_info)
{
  HQASSERT((state_info != NULL), "Passthrough State Info is NULL");

  if (state_info->pcl5_ctxt != NULL)
  {
    (void) pcl5_context_destroy(&state_info->pcl5_ctxt);
  }

  HQASSERT((state_info->pcl5_ctxt == NULL), "Failed to destroy PCL5 context") ;
}

/* ============================================================================
 * PCL5 PS operators
 * ============================================================================
 */
Bool pcl5exec_(ps_context_t *pscontext)
{
  int32 num_args = 1 ;
  int32 stack_size ;
  Bool status = TRUE ;
  OBJECT *ofile ;
  OBJECT *odict = NULL ;
  FILELIST *flptr ;
  int32 save_ps_interpreter_level = ps_interpreter_level ;

  /* need some arguments... */
  if ( isEmpty(operandstack) )
    return error_handler(STACKUNDERFLOW) ;

  gc_safe_in_this_operator();
  /* All PS objects below are parts of the arguments, so GC finds them through
     the operand stack. */

  /* assume just file on stack */
  stack_size = theStackSize(operandstack) ;
  ofile = TopStack(operandstack, stack_size) ;

  if ( oType(*ofile) == ODICTIONARY ) {
    /* got parameter dict - look for file next */
    odict = ofile ;
    num_args++ ;

    /* should be a file under the dict */
    if ( stack_size < 1 )
      return error_handler(STACKUNDERFLOW) ;

    ofile = (&odict[-1]) ;
    if ( ! fastStackAccess(operandstack) )
      ofile = stackindex(1, &operandstack) ;
  }

  /* check we have a readable input file */
  if ( oType(*ofile) != OFILE )
    return error_handler(TYPECHECK) ;

  flptr = oFile(*ofile) ;
  if ( ! isIOpenFileFilter(ofile, flptr) || !isIInputFile(flptr) || isIEof(flptr) )
    return error_handler(IOERROR) ;

  /** \todo TODO - at this point we have a live input stream and
   * optionally a parameters dictionary.  Any default MPE params will
   * be dealt with later, so here we just need to unpack any other
   * global params.
   */
  if ( odict != NULL ) {
  }

  /* We have all our parameters, start processing the PCL5 stream. */

  status = setReadFileProgress(flptr) ;

  if ( status  ) {
    PROBE(SW_TRACE_INTERPRET_PCL5, 0,
          status = pcl5_execute_stream(ps_core_context(pscontext),
                                         flptr, odict, PCLXL_NO_PASS_THROUGH,
                                       NULL /* No state info. */)) ;
  }

  /* Pop arguments from stack only if PCL5 parsed without error */
  if (status) {
    (void)file_close(ofile) ;

    npop(num_args, &operandstack) ;
    HQASSERT(ps_interpreter_level == save_ps_interpreter_level,
             "ps interpreter level has become corrupt") ;

  } else {
    /* Flush input stream up to the next UEL */
    (void)uelflush(ofile);
    (void)file_close(ofile);
    /* Under error condition we do not know if the PS interpreter
       level has been decremented correctly, so reset it to what it
       was when xmlexec started. */
    HQASSERT(ps_interpreter_level >= save_ps_interpreter_level,
             "ps interpreter level has become corrupt") ;
    ps_interpreter_level = save_ps_interpreter_level ;
  }

  return status ;
}

void init_C_globals_pcl5ops(void)
{
  suspend_pcl5_interpreter = FALSE ;
}

/* ============================================================================
* Log stripped */
