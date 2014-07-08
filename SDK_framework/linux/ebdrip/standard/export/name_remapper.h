/** \file
 * \ingroup cstandard
 *
 * $HopeName: HQNc-standard!export:name_remapper.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This header file can be used to rename functions that clash with other libraries.
 */

#ifndef _NAME_REMAPPER_H_
#define _NAME_REMAPPER_H_


/* fnt.h - Note: There maybe other functions that do not require renaming */
#define fnt_RoundToGrid HQN_fnt_RoundToGrid
#define fnt_Super45Round HQN_fnt_Super45Round
#define fnt_Execute HQN_fnt_Execute
#define fnt_RoundToHalfGrid HQN_fnt_RoundToHalfGrid
#define fnt_RoundUpToGrid HQN_fnt_RoundUpToGrid
#define fnt_RoundDownToGrid HQN_fnt_RoundDownToGrid
#define fnt_RoundOff HQN_fnt_RoundOff
#define fnt_RoundToDoubleGrid HQN_fnt_RoundToDoubleGrid
#define fnt_SuperRound HQN_fnt_SuperRound

/* mapstring.h - Note: There maybe other functions that do not require renaming */
#define MapString6_16 HQN_MapString6_16
#define MapString2 HQN_MapString2
#define MapString4_16 HQN_MapString4_16

/* fsglue.h - Note: There maybe other functions that do not require renaming */
#define fsg_PrivateFontSpaceSize HQN_fsg_PrivateFontSpaceSize
#define fsg_SetUpElement HQN_fsg_SetUpElement
#define fsg_FixXYMul HQN_fsg_FixXYMul
#define fsg_KeySize HQN_fsg_KeySize
#define fsg_WorkSpaceSetOffsets HQN_fsg_WorkSpaceSetOffsets
#define fsg_ReduceMatrix HQN_fsg_ReduceMatrix
#define fsg_InitInterpreterTrans HQN_fsg_InitInterpreterTrans
#define fsg_GridFit HQN_fsg_GridFit
#define fsg_MxScaleAB HQN_fsg_MxScaleAB
#define fsg_SetDefaults HQN_fsg_SetDefaults
#define fsg_IncrementElement HQN_fsg_IncrementElement
#define fsg_InnerGridFit HQN_fsg_InnerGridFit
#define fsg_MxConcat2x2 HQN_fsg_MxConcat2x2
#define fsg_RunFontProgram HQN_fsg_RunFontProgram

/* fontscal.h - Note: There maybe other functions that do not require renaming */
#define fs_Initialize HQN_fs_Initialize
#define fs_OpenFonts HQN_fs_OpenFonts
#define fs_GetAdvanceWidth HQN_fs_GetAdvanceWidth
#define fs_NewSfnt HQN_fs_NewSfnt
#define fs_NewTransformation HQN_fs_NewTransformation
#define fs_NewGlyph HQN_fs_NewGlyph

/* privsfnt.h - Note: There maybe other functions that do not require renaming */
#define sfnt_DoOffsetTableMap HQN_sfnt_DoOffsetTableMap
#define sfnt_ComputeMapping HQN_sfnt_ComputeMapping
#define sfnt_GetTablePtr HQN_sfnt_GetTablePtr
#define sfnt_GetOffsetAndLength HQN_sfnt_GetOffsetAndLength
#define sfnt_ComputeIndex2 HQN_sfnt_ComputeIndex2
#define sfnt_ComputeIndex6 HQN_sfnt_ComputeIndex6
#define sfnt_ComputeIndex4 HQN_sfnt_ComputeIndex4
#define sfnt_ReadSFNT HQN_sfnt_ReadSFNT
#define sfnt_ReadSFNTMetrics HQN_sfnt_ReadSFNTMetrics

#endif /* _NAME_REMAPPER_H_ */

/* Log stripped */
