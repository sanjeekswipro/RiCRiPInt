/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:gofiles.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2008 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file defines the list of files to be run in the given order
 * to start up the rip. It effectively replaces bootstrap.ps
 *
 * To add a new file:
 * 1. insert an extern declaration as the name of the file (less suffix)
 * 2. put a new line in the right place in sequence for the item in
 *    the array of startup_file structures.
 * 3. include the appropriate definition in the Macintosh source macdev.c
 */

#include "core.h"
#include "objects.h"

#include "bitblts.h"
#include "display.h"
#include "matrix.h"
#include "graphics.h"
#include "dlstate.h"
#include "stacks.h"
#include "constant.h"
#include "swerrors.h"
#include "namedef_.h"
#include "hqmemcmp.h"
#include "gofiles.h"

/* add a new declaration here: */
extern uint8 sdict_ [];
extern uint8 alldicts [];
extern uint8 odict_ [];
extern uint8 miscpss [];
extern uint8 resource [];
extern uint8 pseudpss [];
extern uint8 errorpss [];
extern uint8 printpss [];
extern uint8 edpdpss [];
extern uint8 cieopts [];
extern uint8 srvrpss [];
extern uint8 idlepss [];
extern uint8 jobpss [];
extern uint8 execupss [];
extern uint8 comntpss [];
extern uint8 colcomnt [];
extern uint8 altsdpss [];
extern uint8 pagedev [];
extern uint8 gstatpss [];
extern uint8 idlompss [];
extern uint8 hcmspss [];

/* WARNING: If you add a new boot-pss string here, you MUST provide
 * a matching dummy XXXXpss string in SWcoreskin!macos:src:bootdev.c.
 */

typedef struct {
  uint8 * filename;
  uint8 * string;
} startup_file;

/* here's where you add new files or change their order: */
static startup_file startup_files [] = {

              /******************************/

              /* automatically generated */
              { (uint8 *)"../sdict_.psb",   sdict_   },
              { (uint8 *)"../alldicts.psb", alldicts },
              /* automatically generated */
              { (uint8 *)"../odict_.psb",   odict_   },
              { (uint8 *)"../miscpss.psb",  miscpss  },
              { (uint8 *)"../resource.psb", resource },
              { (uint8 *)"../pseudpss.psb", pseudpss },
              { (uint8 *)"../errorpss.psb", errorpss },
              { (uint8 *)"../printpss.psb", printpss },
              { (uint8 *)"../edpdpss.psb",  edpdpss  },
              { (uint8 *)"../cieopts.psb",  cieopts  },
              { (uint8 *)"../srvrpss.psb",  srvrpss  },
              { (uint8 *)"../idlepss.psb",  idlepss  },
              { (uint8 *)"../jobpss.psb",   jobpss   },
              { (uint8 *)"../execupss.psb", execupss },
              { (uint8 *)"../comntpss.psb", comntpss },
              { (uint8 *)"../colcomnt.psb", colcomnt },
              { (uint8 *)"../altsdpss.psb", altsdpss },
              { (uint8 *)"../pagedev.psb",  pagedev  },
              { (uint8 *)"../gstatpss.psb", gstatpss },
              { (uint8 *)"../idlompss.psb", idlompss },
              { (uint8 *)"../hcmspss.psb", hcmspss },

              /********************************/
};

/* ---------------------------------------------------------------------- */
void get_startup_file (int32 i, uint8 **filename, int32 *length)
{
  * filename = NULL;
  if (i < sizeof (startup_files) / sizeof (startup_file)) {
    * filename = (uint8 *) startup_files [i].filename;
    * length = strlen_int32 ((char *)* filename);
  }
}

/* ---------------------------------------------------------------------- */
void get_startup_string (uint8 *filename, int32 filelength, uint8 **string, int32 *stringlength)
{
  register int32 i;

  i = sizeof (startup_files) / sizeof (startup_file);
  while (--i >= 0) {
    if (HqMemCmp (startup_files [i].filename, strlen_int32 ((char *)startup_files [i].filename),
        filename, filelength) == 0)
    {
      /* the first two bytes contain the length */
      if ((* string = (uint8 *) startup_files [i].string) == NULL)
        return;
      * stringlength = (int32) (** string) << 8;
      (* string)++;
      * stringlength += ** string;
      (* string)++;
      return;
    }
  }

  *string = NULL;
}

/* Log stripped */
