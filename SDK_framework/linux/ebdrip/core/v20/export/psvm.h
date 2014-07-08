/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:psvm.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1991-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Define PostScript Virtual Memory (VM)
 */

#ifndef __PSVM_H__
#define __PSVM_H__

struct OBJECT ; /* from COREobjects */
struct FILELIST ; /* from COREfileio */

/* CONSTANT DEFINITIONS */

#define DEFAULTVMTHRESHOLD MAXINT32

/* STRUCTURE DEFINITIONS */

typedef struct PS_SAVEINFO {
  struct OBJECT *theomem ;
  int32 packing ;
  struct FILELIST *mystdin , *mystdout , *mystderr;
  uint16 stdin_filterid, stdout_filterid, stderr_filterid;
  int16 idiomchange;
  int32 gsid ;
  int32 objectformat ;
  uint8 langlevel ;
  uint8 colorext ;
  uint8 compfonts ;
  uint8 cmykdetect ;
  uint8 cmykdetected ;
} PS_SAVEINFO ;

/* EXTERNAL DEFINITIONS */
extern PS_SAVEINFO *workingsave ;

/* MACROS DEFINITIONS */

#define NOTINEXITSERVER(_context) ((NUMBERSAVES((_context)->savelevel)-1)>MINSAVELEVEL)
#define INNORMALSERVER(_context)  ((NUMBERSAVES((_context)->savelevel)-1)==(MINSAVELEVEL+1))

/* ACCESSOR MACRO DEFINITIONS */

#define theCMYKDetect(_v)            ((_v).cmykdetect)
#define theICMYKDetect(_v)           ((_v)->cmykdetect)
#define theCMYKDetected(_v)          ((_v).cmykdetected)
#define theICMYKDetected(_v)         ((_v)->cmykdetected)

#define theOMemory(_v)               ((_v).theomem)
#define theIOMemory(_v)              ((_v)->theomem)
#define thePacking(_v)               ((_v).packing)
#define theIPacking(_v)              ((_v)->packing)
#define theObjectFormat(_v)          ((_v).objectformat)
#define theIObjectFormat(_v)         ((_v)->objectformat)
#define theSaveLangLevel(_v)         ((_v).langlevel)
#define theISaveLangLevel(_v)        ((_v)->langlevel)
#define theSaveColorExt(_v)          ((_v).colorext)
#define theISaveColorExt(_v)         ((_v)->colorext)
#define theSaveCompFonts(_v)         ((_v).compfonts)
#define theISaveCompFonts(_v)        ((_v)->compfonts)
#define theStdin(_v)                 ((_v).mystdin)
#define theIStdin(_v)                ((_v)->mystdin)
#define theStdout(_v)                ((_v).mystdout)
#define theIStdout(_v)               ((_v)->mystdout)
#define theStderr(_v)                ((_v).mystderr)
#define theIStderr(_v)               ((_v)->mystderr)
#define theISaveStdinFilterId(_v)    ((_v)->stdin_filterid)
#define theISaveStdoutFilterId(_v)   ((_v)->stdout_filterid)
#define theISaveStderrFilterId(_v)   ((_v)->stderr_filterid)
#define theLastIdiomChange(_v)       ((_v).idiomchange)
#define theILastIdiomChange(_v)      ((_v)->idiomchange)
#define theGSid(_v)                  ((_v).gsid)
#define theIGSid(_v)                 ((_v)->gsid)

void ps_save_commit(PS_SAVEINFO *sptr) ;
Bool ps_restore_prepare(PS_SAVEINFO *sptr, int32 slevel);
void ps_restore_commit(PS_SAVEINFO *sptr) ;
Bool ps_restore(ps_context_t *pscontext, int32 slevel) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
