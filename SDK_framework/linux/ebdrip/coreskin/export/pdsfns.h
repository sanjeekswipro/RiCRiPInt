#ifndef __PDSFNS_H__
#define __PDSFNS_H__

/* $HopeName: SWcoreskin!export:pdsfns.h(EBDSDK_P.1) $
 *
 * pdsfns.h defines function prototypes that the product dependent skins to
 * the coreskin must define in order for the coreskin to link correctly.
 *
* Log stripped */


/* ----------------------------- Includes ---------------------------------- */

/* Include coreutil master include file first as built on top of it */
#include "coreskin.h"   /* Includes std.h, coreutil.h, etc */
#include "fwstring.h"   /* FwTextString... */
#include <stdarg.h>

/* ----------------------------- Types ------------------------------------- */

/* Forward Types */
struct DeviceType;

/*
 * Possible results from pds_*question*().
 * pds_noresponse is returned for GUI-less applications where the user
 * cannot be interactively questioned.
 *
 * Deliberately avoid using 0 or 1 so that if a developer assumes a boolean
 * response or specifically tests for one, the error is likely to be
 * detected.
 */

enum
{
 pds_no = 2,
 pds_yes,
 pds_noresponse
};

/* ----------------------------- Functions --------------------------------- */

extern void  pds_warning (FwTextString ptbzFormat, ...);
extern int32 pds_yesnoquestion (FwTextString ptbzQuestion);
extern int32 pds_yesnoquestionf (FwTextString ptbzFormat, ...);
extern int32 pds_vyesnoquestionf (FwTextString ptbzFormat, va_list vlist);
extern int32 pds_noyesquestion (FwTextString ptbzFormat);
extern int32 pds_noyesquestionf (FwTextString ptbzFormat, ...);
extern int32 pds_vnoyesquestionf (FwTextString ptbzFormat, va_list vlist);

extern void  pds_installperiodictasks( void );

extern FwTextString pds_DefaultPageSetupName PROTO((void));

extern void pds_LogMonitorToWindow(int32 devID, FwTextString smsg, int32 length );

/* Dial functions needed by PDS layer */

struct CSDial;
extern void pds_displaydial(struct CSDial * pDial);
extern void pds_updatedial(struct CSDial * pDial);
extern void pds_undisplaydial(struct CSDial * pDial);


#ifdef WIN32
/* This may subject to move once headless RIP  becomes avaiable in
 * all platforms. 
 */
void  pds_processCommandLine(char * cmdBuffer);
#endif

#endif /* protection for multiple inclusion */


/* eof pdsfns.h */
