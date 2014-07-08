/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_checksum!gentab.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 */

#ifndef __GENTAB_H__
#define __GENTAB_H__

#ifdef __cplusplus
extern "C" {
#endif

/* These tables are shared between the rip and the key generator program
   for securing product options like HSL. These are definitions so
   should only be included once in each program.
*/

/* security key table for product HDS */
static uint8 aHds [16] = {
   5, 12, 10,  3,  1,  8,  6,  0, 13,  4, 14, 11,  2,  9,  7, 15 };

/* security key table for product HMS */
static uint8 aHms [16] = {
   2,  6, 14,  1, 11,  5, 10,  8, 13,  0, 15,  3, 12,  7,  4,  9 };

/* security key table for product HCS */
static uint8 aHcs [16] = {
   9,  3, 10, 15,  8, 12,  1,  4, 14, 11,  7,  2,  6,  0, 13,  5 };

/* security key table for product HCMS */
static uint8 aHcms [16] = {
   1,  6,  8,  7,  5, 10, 11,  2,  4, 13,  3,  0,  9, 14, 15, 12 };

/* security key table for calibration (micro-rip only). */
static uint8 aCalibrationSecTable[16] = {
   2,  1,  8, 15, 14,  3,  5,  4,  9, 10,  7,  6,  12, 0, 13, 11 };

/* Security key table for protection of major revision updates prior to rev 6 */
static uint8 aMajorRevisionSecTable[16] = {
   3, 15, 12,  1,  2, 14,  8,  13, 6,  4,  5,  9,  0, 11, 7,  10 };

/* Security key for IDLOM */
static uint8 aIDLOMSecurityTable[16] = {
  13, 12,  1, 10,  3,  9,  7,  14, 8,  4,  2,  5,  6, 15, 0,  11 };

/* Security key for cp2rsh */
static uint8 aCp2rshSecurityTable[16] = {
   7, 11, 13,  4,  5, 12,  3,  14, 2,  6,  1,  9, 10,  0, 15,  8 };

/* Security key for Miramar */
static uint8 aMiramarSecTable[16] = {
   0,  7, 13, 11, 15,  2,  9,  14, 3,  4, 12, 10,  8,  6,  5,  1 };

/* Security key for EasyTrap / TrapWorks / Trap Pro-Lite */
static uint8 aEasyTrapSecTable[16] = {
   4, 13,  1, 15, 14,  3,  8,  10, 5, 11, 12,  9,  2,  6,  7,  0 };

/* Security key for TIFF-IT */
static uint8 aTIFF_ITSecTable[16] = {
   12, 3,  4, 10,  8,  0,  13, 11, 6,  1,  7,  2,  9,  14, 5, 15 };

/* Security key for TIFF 6.0 */
static uint8 aTIFF6SecTable[16] = {
   12, 3, 14, 10,  8,  0,  13, 7, 6,  1,  11,  2,  9,  4,  5, 15 };

/* Security key for low resolution HDS */
static uint8 aHDSLOWSecTable[16] = {
   0,  2,  8, 7,  15, 14,  11,  6, 9,  1,  3,  4,  12, 5, 13, 10 };

/* Security key for MediaSaving */
static uint8 aMediaSavingSecTable[16] = {
   5,  1, 13, 9,   6,  2,  15, 11, 3, 14, 10,  4,   7, 8,  0, 12 };

#ifndef NO_TOOTHPICK_TABLE
static uint8 aToothpickTable[16] = {
   15,  1, 13, 9,   6,  2,  5, 12, 3, 10, 14,  4,   7, 8,  0, 11 };
#endif

/* Security key for subfeatures (UI Language, protected plugins) */
/* NB. Because this feature is being used by the subfeature_permission
 * its first number _must_ be 15. Regular features cannot have 15
 * as their first number.
 */
static uint8 aSubFeatureSecTable[16] = {
  15,  3, 12,  2,  7,  9,  5, 13,  0, 11,  1,  8,  6, 10, 14,  4 };

/* ditto */
static uint8 aHCPSecTable[16] = {
  15,  6, 12,  8,  3,  2, 10, 13,  7, 14, 11,  9,  1,  5,  4,  0 };

/* Security key for CHC-S845-2 */
static uint8 aChcS8452SecurityTable[16] = {
   8, 15,  0, 10,  9,  1,  6,  2, 14, 3, 12,  5,  4,  13, 11,  7 };

/* Security key for ICC / HIPP / Harlequin Color Pro */
static uint8 aICCSecurityTable[16] = {
   3, 10,  5,  8,  7,  4,  6,  1, 12, 15, 14,  9,  2, 11,  0, 13 };

/* Security key for HSCS (ne HCMS-Lite) */
static uint8 aHCMSLiteSecurityTable[16] = {
   6,  4,  2,  3,  9, 10,  7, 13,  8, 12,  5, 15,  1, 14, 11,  0 };

/* Security key for HCEP */
static uint8 aHCEPSecurityTable[16] = {
   2, 11,  0, 13, 12, 15, 14,  8,  7,  4,  6,  1,  9 , 3, 10,  5  };

/* Security key for Two-thread multi-processing */
static uint8 aTwoThreadSecTable[16] = {
  13, 10,  2,  3, 11, 15,  7, 12,  8,  1,  4, 14,  9,  6,  0,  5 };

/* Security key for PostScript when the dongle denies its use */
static uint8 aPostScriptSecTable[16] = {
   9,  3,  5, 11,  10, 12,  1,  4, 14, 8,  0, 15,  6,  7, 13,  2 };

/* Security key for PDF when the dongle denies its use */
static uint8 aPDFSecTable[16] = {
   8, 11,  0,  2,  1,  9,  6, 10, 14, 3,  7,  5,  4,  13, 15, 12 };

static uint8 aPDFOutSecTable[16] = {
   1, 13,  6, 14,  2, 10,  3, 15,  0,  5,  9,  8, 12,  7, 11,  4 };

/* Security key for de-restricting Morisawa outlines */
static uint8 aMPSOutlineSecTable[16] = {
   1,  8,  1,  8, 13,  0,  7, 14,  5,  0, 12, 14, 14, 11, 11,  1 };

/* Security key table for rev 6 upgrade */
static uint8 aRev6SecTable[16] = {
   3, 13,  2, 10, 15,  9,  6, 11, 14,  4,  5,  1,  0, 12,  8,  7 };

/* Security key table for Trap Pro */
static uint8 aTrapProSecTable[16] = {
   4,  6,  5, 10,  1,  9, 13,  2, 14, 15,  7, 12,  3, 11,  8,  0 };

/* Security key table for platform upgrade */
static uint8 aPlatformUpgradeSecTable[16] = {
  11,  4,  8,  0, 15, 14,  3,  2, 12,  7,  5, 10,  1,  6,  9, 13 };

/* Security key table for HPS when the dongle denies its use */
static uint8 aHpsSecTable[16] = {
   7,  0, 12, 15,  1,  5,  9, 14, 11, 13,  2,  8,  6,  3,  4, 10 };

/* Security key table for simple imposition */
static uint8 aSimpleImpositionSecTable[16] = {
   9,  2, 15,  1,  0,  7, 12, 14,  5,  8, 10,  3, 11,  4,  6, 13 };

/* Security key table for rev 7 upgrade */
static uint8 aRev7SecTable[16] = {
   2,  0, 14,  7,  5,  1, 13, 15, 11,  3,  4,  8, 10,  9,  6, 12 };

/* Security key table for HXM */
static uint8 aHxmSecTable[16] = {
   4,  3, 11,  5,  9,  1, 10,  7, 12, 15, 14, 13,  2,  6,  8,  0 };

/* Security key for XPS when the dongle denies its use */
static uint8 aXPSSecTable[16] = {
  13,  8,  4,  3,  9,  1, 14, 15,  6, 12,  0,  5,  2, 10, 11,  7 };

/* Security key to omit watermark when the dongle requires its use */
static uint8 aApplyWatermarkSecTable[16] = {
   7, 15,  6,  5, 12,  4,  3, 14, 13,  8, 11,  9, 10,  0,  1,  2 };

/* Security key for core modules
 * Used by subfeature_permission so first number = 15
 */
static uint8 aCoreModuleSecTable[16] = {
  15,  4, 10,  6,  3,  9,  5,  1, 13, 14, 12,  2,  8, 11,  7,  0 };

/* Security key table for rev 8 upgrade */
static uint8 aRev8SecTable[16] = {
  14,  9,  2,  6,  1, 15, 13,  7,  8, 10, 12,  0, 11,  4,  5,  3 };

/* Security key table for rev 9 upgrade */
static uint8 aRev9SecTable[16] = {
   3,  4,  5, 14, 13, 12,  1,  2, 15,  6,  7,  0,  8,  9, 11, 10 };

/* Security key table for TeamWork 2 upgrade */
static uint8 aRevTW2SecTable[16] = {
   1, 14,  7,  5,  6,  3, 11, 10, 12, 13,  2, 15,  4,  9,  8,  0 };

/* Security key table for multi-threaded compositing */
static uint8 aMTCSecTable[16] = {
   4,  5,  7, 14,  1, 13, 12,  2,  6, 15, 11,  0,  9, 10,  8,  3 };

/* Security key table for HXM light */
static uint8 aHxmLowSecTable[16] = {
   0,  3, 12,  6, 11, 10,  7, 14,  1, 15,  8,  9,  2, 13,  5,  4 };

/* Security key table for limit on max threads
 * Used by subfeature_permission so first number = 15
 */
static uint8 aMaxThreadsLimitSecTable[16] = {
  15,  4,  9,  5,  1, 10, 11,  7,  2, 14,  3, 13, 12, 6 ,  8,  0 };

/* Security key table for pipelining  */
static uint8 aPipeliningSecTable[16] = {
   8,  2,  6, 15, 14,  6,  9,  0, 13, 12, 11, 10,  1,  4,  5,  3 };

/* Security key table for 64-bit upgrade  */
static uint8 a64BitSecTable[16] = {
   2,  3,  7, 11, 13, 14, 10,  1,  6,  5, 12,  8,  4, 15,  9,  0 };

/* Security key table for MultiRIP 3 upgrade */
static uint8 aRevMR3SecTable[16] = {
   2,  4,  5,  8,  0, 12,  9,  1, 11, 10, 13,  3,  7, 14, 15,  6 };

/* Security key table for external Harlequin VariData (aka retained raster) */
static uint8 aHvdExternalSecTable[16] = {
   5,  7,  6, 10,  2, 14,  3, 13,  1,  9, 12, 15,  4, 11,  8,  0 };

/* Security key table for internal Harlequin VariData (aka retained raster) */
static uint8 aHvdInternalSecTable[16] = {
   6,  9,  1, 14,  5, 10,  7,  4,  0, 11, 15,  8, 12,  2, 13,  3 };

/* Security key table for 32-bit downgrade  */
static uint8 a32BitSecTable[16] = {
   9,  0,  1,  2,  6, 10, 14, 15,  8, 13,  7,  5, 12, 11,  4,  3 };

/* Security key table for 32-bit MultiRIP 4 upgrade */
static uint8 aRev32MR4SecTable[16] = {
  14, 13,  7,  2, 10, 11, 12,  1,  6, 15,  5,  3,  4,  9,  0,  8 };

/* Security key table for 64-bit MultiRIP 4 upgrade */
static uint8 aRev64MR4SecTable[16] = {
   6,  7,  8,  2,  3,  1,  4, 12, 11,  9,  0, 10, 14, 15, 13,  5 };

#ifdef __cplusplus
}
#endif


#endif /* protection for multiple inclusion */
