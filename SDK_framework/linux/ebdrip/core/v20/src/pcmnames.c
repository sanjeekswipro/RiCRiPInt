/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:pcmnames.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS Process Color Model (PCM) names
 */

#include "core.h"
#include "objects.h"
#include "namedef_.h"
#include "pcmnames.h"

NAMECACHE* pcmGyName[2] = {
  &system_names[NAME_Gray],
  NULL
};
NAMECACHE* pcmKName[2] = {
  &system_names[NAME_Black],
  NULL
};

NAMECACHE* pcmRGBNames[4] = {
  &system_names[NAME_Red],
  &system_names[NAME_Green],
  &system_names[NAME_Blue],
  NULL
};
NAMECACHE* pcmRGBKNames[5] = {
  &system_names[NAME_Red],
  &system_names[NAME_Green],
  &system_names[NAME_Blue],
  &system_names[NAME_Black],
  NULL
};
NAMECACHE* pcmRGBGyNames[5] = {
  &system_names[NAME_Red],
  &system_names[NAME_Green],
  &system_names[NAME_Blue],
  &system_names[NAME_Gray],
  NULL
};

NAMECACHE* pcmCMYNames[4] = {
  &system_names[NAME_Cyan],
  &system_names[NAME_Magenta],
  &system_names[NAME_Yellow],
  NULL
};
NAMECACHE* pcmCMYKNames[5] = {
  &system_names[NAME_Cyan],
  &system_names[NAME_Magenta],
  &system_names[NAME_Yellow],
  &system_names[NAME_Black],
  NULL
};
NAMECACHE* pcmCMYGyNames[5] = {
  &system_names[NAME_Cyan],
  &system_names[NAME_Magenta],
  &system_names[NAME_Yellow],
  &system_names[NAME_Gray],
  NULL
};

NAMECACHE* pcmAllNames[9] = {
  &system_names[NAME_Cyan],
  &system_names[NAME_Magenta],
  &system_names[NAME_Yellow],
  &system_names[NAME_Black],
  &system_names[NAME_Red],
  &system_names[NAME_Green],
  &system_names[NAME_Blue],
  &system_names[NAME_Gray],
  NULL
};


/* Log stripped */
