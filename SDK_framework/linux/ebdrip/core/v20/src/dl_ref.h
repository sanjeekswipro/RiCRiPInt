/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:dl_ref.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Private definition of the DL Reference object.
 */

#ifndef __DL_REF_H__
#define __DL_REF_H__

#include "display.h"


/**
 * A container holding one or more Display List objects.
 * These may be in memory or on disk.
 */
struct DLREF
{
  uint16 inMemory;     /**< Bool - is the DL object in memory or on disk? */
  uint16 nobjs;        /**< If on disk, number of objects. */
  union
  {
    LISTOBJECT *lobj;  /**< If inMemory, the single DL Object. */
    size_t diskoffset; /**< If on disk, the offset to the DL objects. */
  } dl;
  struct DLREF *next;  /**< Containers held as a singly-linked chain. */
};

#endif /* protection for multiple inclusion */

/*
* Log stripped */
