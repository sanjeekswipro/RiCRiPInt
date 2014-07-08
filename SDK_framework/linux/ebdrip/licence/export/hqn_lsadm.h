/* $HopeName: ETmars-licence-c!export:hqn_lsadm.h(EBDSDK_P.1) $
 * Client admin interface
 *
 * Copyright (C) 2003-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * See end of file for modification history.
 */

#ifndef LS_HQN_LSADM_H
#define LS_HQN_LSADM_H

#ifdef  __cplusplus
extern "C" {
#endif /* __cplusplus */

#include "hqn_ls.h"

struct FwStrRecord;

/* Symbol Obfuscation ------------------------------------------------*/

#ifndef LS_NOHIDE

#define ls_local_ls      bro_size
#define ls_local_ls_id   bro_len
#define ls_reinit_local  try_size
#define ls_permit_dir    try_len

#endif /* not LS_NOHIDE */


typedef struct {
  hls_uint    ls_server_version;   /* The version number of the server */
  hls_uint    ls_api_version;      /* The API version of the server */
} LSserver;

#define HLS_VERSION_VALUE( _major_, _minor_, _revision_ ) ((_major_ * 10000) + (_minor_ * 100) + _revision_ )

#define HLS_EXTRACT_MAJOR_VERSION( _version_ ) ( _version_ / 10000 )
#define HLS_EXTRACT_MINOR_VERSION( _version_ ) (( _version_ % 10000 ) / 100 )
#define HLS_EXTRACT_REVISION( _version_ ) ( _version_ % 100 )

/*--------------------- External Interface ---------------------------*/

/* LSserver *ls_get_server_info(LSdata *data)
 * Input:  pointer to a LSdata structure.
 * Output: Returns a pointer to an LSserver structure if successful
 *         otherwise returns a NULL pointer.
 * Gets information about the server serving this license.
 */
extern LSserver * ls_get_server_info( LSdata * lsd );

/* hls_int ls_local_ls( void )
 * Output: TRUE <=> licence server running on local host
 */
extern hls_int ls_local_ls( void );


/* hls_int ls_local_ls_id( uint32 * pid )
 * Output: TRUE <=> success
 *
 * Obtains the unique ID of the local licence server, if any.
 */
extern hls_int ls_local_ls_id( hls_uint * pid );


/* hls_int ls_local_add_permit( char * pfile, hls_uint fAllowOverwrite )
 * Output: TRUE <=> success
 *
 * Adds a permit to the local license server
 * Set fAllowOverwrite to TRUE if it is OK for an existing
 * permit to be overwritten
 */
extern hls_int ls_local_add_permit( char * pfile, hls_uint fAllowOverwrite );


/* hls_int ls_reinit_local( void )
 * Output: TRUE <=> success
 *
 * ls_reinit_local triggers the local licence server, if any,
 * to re-read its permits.
 */
extern hls_int ls_reinit_local( void );


/* const char * ls_permit_dir( void )
 * Output: path to default permit directory on local host
 */
extern const char * ls_permit_dir( void );


/* hls_uint ls_server_date( void )
 * Input:  pointer to a LSdata structure.
 * Output: Returns TRUE <=> success and *pDate is set to
 * current date of server for lsd in form yyyymmdd
 */
extern hls_bool ls_server_date( LSdata * lsd, hls_uint * pDate );


/* hls_uint ls_get_date( void )
 * Output: today's date in form yyyymmdd
 */
extern hls_uint ls_get_date( void );


/* hls_uint ls_days_until( void )
 * Output: no of days from now to later
 *   result is negative if now is after later
 */
extern hls_int ls_days_from( hls_uint now, hls_uint later );


/* Attempt to start the local licence server. Return non-zero if successful or
 * zero if unsuccessful. Platform-specific implementations are expected.
 *
 * If ppResultMessage is not NULL/0, the pointer will be set to point to
 * a static text string explaining the result.
 *
 * If no true implementation available on a particular platform, provide a
 * trivial implementation that always returns false (and result message).
 */
extern hls_bool ls_start_local_ls_ext(const char** ppResultMessage);

/* Equivalent to ls_start_local_ls_ext(NULL). */
extern hls_bool ls_start_local_ls( void );


/* Attempt to locate the local licence manager. Return non-zero if successful or
 * zero if unsuccessful.  If successful writes the path to pPath.
 * Platform-specific implementations are expected.
 * If no true implementation available on a particular platform, provide a
 * trivial implementation that always returns false.
 */
extern hls_bool ls_locate_license_manager( struct FwStrRecord * pPath );


/* Attempt to locate the local licence server. Return non-zero if successful or
 * zero if unsuccessful.  If successful writes the path to pPath.
 * Platform-specific implementations are expected.
 * If no true implementation available on a particular platform, provide a
 * trivial implementation that always returns false.
 */
extern hls_bool ls_locate_license_server( struct FwStrRecord * pPath );


/* hls_uint ls_check_sig(LSdata *data)
 * Input:  pointer to a LSdata structure.
 * Output: result of signature check, e.g. SIGNATURE_VALID
 * Checks the signature of the server serving this license.
 */
extern hls_uint ls_check_sig( LSdata * lsd );


/* hls_bool ls_license_expiring(LSdata *data)
 * Input:  pointer to a LSdata structure.
 * Output: Returns TRUE if license expiry was determined successfully, if which case
 *         *pfExpiring is set to TRUE or FALSE to indicate if the license is expiring
 *         and *pDaysLeft is set to the number of days remaining of license validity
 *         (1 = expires today, 0 = expired / not valid yet).
 * Determines if this license is expiring.
 */
extern hls_bool ls_license_expiring( LSdata * lsd, hls_bool * pfExpiring, hls_uint * pDaysLeft );

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* not LS_HQN_LSADM_H */

/* Modification history:
* Log stripped */
