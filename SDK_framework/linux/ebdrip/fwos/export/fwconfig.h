/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __FWCONFIG_H__
#define __FWCONFIG_H__

/*
 * $HopeName: HQNframework_os!export:fwconfig.h(EBDSDK_P.1) $
 * FrameWork External Application Configuration module
 */

/* ----------------------- Includes ---------------------------------------- */

#include "fwcommon.h"   /* Common */
#include "fwerror.h"
#include "fwstring.h"
#include "fwvector.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* ----------------------- Functions --------------------------------------- */

/* FwConfigGetConfigDir      Get the location of the config dir
 *
 * Appends the directory path to pConfigDir.
 * Returns the number of bytes appended.
 */
extern uint32 FwConfigGetConfigDir( FwStrRecord * pConfigDir );

/* FwConfigReadFile          Read a config file's contents into a FwVector
 *
 * Opens ptbzFilename, reads line by line to EOF adding each line to
 * pVector (skipping blank lines, comments), closes file.
 * Returns TRUE <=> successfully read to EOF
 */
extern int32 FwConfigReadFile
 ( FwErrorState * pErrState, FwTextString ptbzFilename, FwVector * pVector );

/* FwConfigReadConfigFile    Read a config file's contents into a FwVector
 *
 * As FwConfigReadFile() but reads ptbzLeafname in FwConfigGetDir()
 */
extern int32 FwConfigReadConfigFile
 ( FwErrorState * pErrState, FwTextString ptbzLeafname, FwVector * pVector );

/* FwConfigWriteFile         Write a config file's contents from a FwVector
 *
 * Opens ptbzFilename, writes line by lineadding each line from
 * pVector, closes file.
 * Returns TRUE <=> successfully written
 */
extern int32 FwConfigWriteFile
 ( FwErrorState * pErrState, FwTextString ptbzFilename, FwVector * pVector );

/* FwConfigWriteConfigFile   Write a config file's contents from a FwVector
 *
 * As FwConfigWriteFile() but writes ptbzLeafname in FwConfigGetDir()
 */
extern int32 FwConfigWriteConfigFile
 ( FwErrorState * pErrState, FwTextString ptbzLeafname, FwVector * pVector );


#ifdef __cplusplus
}
#endif /* __cplusplus */

/*
* Log stripped */
#endif /* ! __FWCONFIG_H__ */

