#ifndef __CREXDATA_H__
#define __CREXDATA_H__

/* $HopeName: SWcoreskin!export:crexdata.h(EBDSDK_P.1) $
 *
 * RIP Configuration Extras.
 *
* Log stripped */

/* ----------------------- Includes ---------------------------------------- */

#include "coreskin.h"

/* coreskin */
#include "listutil.h"    /* listStruct  */

/* pic */
#include "gdevstio.h"    /* DICTSTRUCTION */

/* ggcore */
#include "fwstring.h"    /* FwTextByte */

/* ----------------------- Macros ------------------------------------------ */

/* Maximum possible number of extras features */
#define CRPEXTRAS_MAX_NUM_FEATURES      22

/* /HDS extra */
#define CRPEXTRAS_HDS                   0

/* /HDS low res extra */
#define CRPEXTRAS_HDSLOWRES             1

/* /HXM extra */
#define CRPEXTRAS_HXM                   5

/* /HXM low res extra */
#define CRPEXTRAS_HXMLOWRES             6

/* /RIPController extra */
#define CRPEXTRAS_RIPCONTROLLER         14

/* /MaxThreadsLimit extra */
#define CRPEXTRAS_MAX_THREADS_LIMIT     18

/* /Pipelining extra */
#define CRPEXTRAS_PIPELINING            19

/* dictstruction for extra dialog */
#define CREX_DS_PSIO_TOTAL              22

/* ----------------------- Types ------------------------------------------- */

/* ================= For single entry (or Edit dialog) ======================*/

/* some of the types have gui-like "Edit" in them. it comes from the old
 * they actually describe an entry in a table (and used by the config-rip
 * edit dialog).
 */
typedef struct ConfigureRIPEdit
{
  int32         password;
  int32         fTrial;
} ConfigureRIPEdit;

typedef struct ConfigureRIPPluginExtras
{
    listStruct Entry;
    FwID_Text  atbzFeatureName;
    FwID_Text  atbzOEMName;
    FwID_Text  atbzPluginName;
    FwID_Text  atbzDeviceName;
    int32      nPassword;
    int32      fTrial;
} ConfigureRIPPluginExtras;


/* ============== For multiple entries (or Extra dialog) ===================*/
/* Combined dialog state */
typedef struct ConfigureRIPExtras
{
  ConfigureRIPEdit      edit[ CRPEXTRAS_MAX_NUM_FEATURES ];
} ConfigureRIPExtras;

/* ----------------------- Data -------------------------------------------- */

extern FwTextByte   atbzConfigureRIPExtrasName[];
extern DICTSTRUCTION crexPSIODictStructions[];
extern struct DictPSIONode configureRIPExtrasPSIONode;
extern struct DictPSIONode optionalConfigureRIPExtrasPSIONode;
extern struct DictPSIONode configureRIPPluginExtrasPSIONode;

/* ----------------------- Functions --------------------------------------- */

extern int32 coreguiGetExtrasIndexFromFeature( int32 nFeature );

extern int32 coreguiGetExtrasFeatureFromIndex( int32 nIndex );


#endif /* __CREXDATA_H__ */

/* eof crexdata.h */
