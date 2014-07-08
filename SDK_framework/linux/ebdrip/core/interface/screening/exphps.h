/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_screening!exphps.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1993-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 *
 * \brief
 * Exporting screens from the RIP into the Harlequin screening device
 * and Harlequin RLE plugins.
 *
 * This file should NOT be made known to core rip OEMs or to plugin OEMs.
 */

#ifndef __EXPHPS_H__
#define __EXPHPS_H__

#ifdef __cplusplus
extern "C" {
#endif

typedef struct exportscreen {
  int32 r1 ;
  int32 r2 ;
  int32 r3 ;
  int32 r4 ;
  int32 halftype ;
  int32 accurateScreen ;
  double supcell_ratio ;         /* this must be double-aligned */
  int32 supcell_multiplesize ;
  int32 supcell_actual ;
  int32 notones ;
  int16 *xs ;
  int16 *ys ;
  int32 ScreenDotCentered ;
  int32 supcell_remainder ;
  int32 dummy2 ;
} ExportScreen ;


#define HQN_SCREEN_SECRET       ( 14 * 7 * 1962 )
#define HQN_SCREEN_DESCRIPTOR   ( 1066 * HQN_SCREEN_SECRET )

#ifdef __cplusplus
}
#endif


#endif /* protection for multiple inclusion */
