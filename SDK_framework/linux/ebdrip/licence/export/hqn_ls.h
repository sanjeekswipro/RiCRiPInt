/* $HopeName: ETmars-licence-c!export:hqn_ls.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * Harlequin Licensing System Version 3.1
 *
 * External definitions for the client side interface to the Harlequin network
 * licensing system.  The interface contains routines which communicate with
 * the hqn_lsd licence server daemon.
 *
 * The interface consists of three levels of interface routines, an LSdata
 * structure and (possibly) one or more signature arrays.
 *
 * See end of file for modification history.
 */

#ifndef LS_HQN_LS_H
#define LS_HQN_LS_H

#ifdef  __cplusplus
extern "C" {
#endif /* __cplusplus */


/* --- Basic type definitions ----------------------------------------*/

#define hls_int  int          /* 32 bit */
#define hls_uint unsigned int /* 32 bit */
#define hls_bool int

/* -------------------------------------------------------------------*/

#define LS_PUBLISHER_LEN 64
#define LS_PRODUCT_LEN   64
#define LS_VERSION_LEN   32
#define LS_MAX_CHAL_DATA 16
#define LS_MAX_SECRETS   4
#define LS_SIG_LEN       16

/* -------------------------------------------------------------------*/

/* Standard Status Codes
 * Returned by the lowest level interface routines.  The last status returned
 * can be obtained with ls_status_val().  Generally ls_status() is used to
 * check the last status and return a description string if an error occured.
 */

#define LS_SUCCESS                      0 /* completed successfuly */
#define LS_BADHANDLE           0xC8001001 /* handle not valid */
#define LS_INSUFFICIENTUNITS   0xC8001002 /* not enough resources for request */
#define LS_LICENCESYSNOTAVAILABLE 0xC8001003 /* no LS found to do operation */
#define LS_LICENSESYSNOTAVAILABLE 0xC8001003 /* no LS found to do operation */
#define LS_LICENCETERMINATED   0xC8001004 /* resources no longer granted */
#define LS_LICENSETERMINATED   0xC8001004 /* resources no longer granted */
#define LS_NOAUTHORIZATIONAVAILABLE 0xC8001005 /* no resources exist for
                                                * operation */
#define LS_NOLICENCESAVAILABLE 0xC8001006 /* resources exist but are not
                                           * available */
#define LS_NOLICENSESAVAILABLE 0xC8001006 /* resources exist but are not
                                           * available */
#define LS_NORESOURCES         0xC8001007 /* not enough resources (e.g memory)
                                           */
#define LS_NO_NETWORK          0xC8001008 /* network unavailable */
#define LS_NO_MSG_TEXT         0xC8001009 /* error while retrieving error
                                           * string */
#define LS_UNKNOWN_STATUS      0xC800100A /* no message exists for that code */
#define LS_BAD_INDEX           0xC800100B /* bad index to LSEnum... or
                                           * LSQuery... */
#define LS_NO_MORE_UNITS       0xC800100C /* no additional units available*/
#define LS_LICENCE_EXPIRED     0xC800100D /* maybe due to time restriction */
#define LS_LICENSE_EXPIRED     0xC800100D /* maybe due to time restriction */
#define LS_BUFFER_TOO_SMALL    0xC800100E /* error from LSQuery */

/* LSQueryLicence information types */

#define LS_INFO_NONE          0 /* Reserved */
#define LS_INFO_SYSTEM        1 /* Return server API version */
#define LS_INFO_DATA          2 /* Return licence data */
#define LS_INFO_UPDATE_PERIOD 3 /* Return interval between LSUpdate calls */
#define LS_LICENCE_CONTEXT    4 /* provide context in LS of handle */
#define LS_LICENSE_CONTEXT    4 /* provide context in LS of handle */

/* -------------------------------------------------------------------*/

/* Extended Status Codes
 * hqn_lsd actually returns more information than defined by the standard
 * status codes.  The extended status codes consist of the following values
 * or'd with the relevant standard status code.  The last extended status can
 * be obtained with ls_xstatus_val().
 */

#define LSX_SUCCESS          0
#define LSX_BADAPIVERSION    0x10000

/* LicenseSystem description string did not match that held by the server
 * contacted.  See LSdata structure. */

#define LSX_BADPRODUCT       0x20000

/* hqn_lsd contacted does not hold a permit for this product */

#define LSX_PERMITOUTOFDATE  0x30000

/* permit for this product has expired */

#define LSX_NOLICENCES       0x40000
#define LSX_NOLICENSES       0x40000

/* All the available licences for this product are being used */

#define LSX_BIGPHANDLE       0x50000
#define LSX_BIGLHANDLE       0x60000
#define LSX_BADHANDLE        0x70000

/* The licence context handle passed to hqn_lsd is invalid.  This may occur
 * because the handle has become corrupted or because the current licence has
 * been revoked for some reason.
 */

#define LSX_PERMITEXPIRED    0x80000

/* The current licence is no longer valid because the product permit expired or
 * timed out. */

#define LSX_DUPREQUEST       0x90000

/* You have requested a licence when you already hold one - may occur
 * automatically if there are long network delays.  Generally this is converted
 * to LS_SUCCESS but without a new licence being issued. */

#define LSX_TOOMANYLICENCES  0xa0000
#define LSX_TOOMANYLICENSES  0xa0000

/* hqn_lsd can be configured to allow only one instance of a product on a given
 * host.  This status is returned if an attempt is made to start another. */

#define LSX_LOSTLICENCE      0xb0000
#define LSX_LOSTLICENSE      0xb0000

/* The current licence context handle no longer belongs to you.  This can occur
 * in various ways depending on the policies set for the product on hqn_lsd. */

#define LSX_LOCKSNOFLOAT     0xc0000

/* Product is being run on a locked-node host but the only licences available
 * were floating and the LOCKSNOFLOAT policy was set. */

#define LSX_NOTVERIFIED      0xd0000

/* The server contacted has not been verified.  If multiple servers are
 * specified they must be able to contact each other before they will serve
 * licences. */

#define LSX_NEW_MASTER       0xe0000

/* The server contacted is about to become the acting domain master. */

#define LSX_LOSTCONTACT      0xf0000

/* The server previously contacted is no longer responding */

/* ----- Following are not or'd with the standard status --------- */

#define LSX_LOSTMASTER_TO_N  0x100000

#define LSX_TRY_SERVER_N     0x110000

/* The server was not the acting master but returns a suggestion for the next
 * server to try.  The index of the domain member to try is in the low byte of
 * the status. */

#define LSX_TRY_DOMAIN       0x120000

/* Current server does not serve licences for this product, try the
 * accompanying domain. */

#define LSX_ILLEGAL          0x130000

/* The server has detected an anomalous internal state */

#define LSX_NOMEMORY         0x140000

/* The server cannot allocate enough memory */

#define LSX_EXCLUDED         0x150000

/* Attempted to use a host which has been excluded from running this product. */

#define LSX_USERS_OFF        0x160000

/* hqn_lsd has been configured not to allow listing of current users of this
 * product. */

#define LSX_NOACCESS         0x170000

/* Users access level has been set to 0 */

/* --------------------------------------------------------------------*/

/* Value of licence context handle after unsuccessful request */

#define LSX_NOHANDLE         0xFFFFFFFF

/* Low level wild card values */

#define LS_DEFAULT_UNITS 0xFFFFFFFF /* units to consume up to LS */
#define LS_ANY           0          /* Use any licence service provider */
#define LS_USE_LAST      0x0800FFFF /* return message for last error */

/* Amount of data return by ls_get_context in *hls_int's*.
 * N.B. first element is number of bytes returned. */

#define LS_CONTEXT_SIZE 5

/* Licence context values */

#define LS_ACCESS_MASK 0x0f /* Mask for access level bits */
#define LS_LIC_LOCKED  0x10 /* Floating licence if not set */

/* Opaque handle to product licence context */

#define LS_Handle void *

/* Errors returned from ls_initialise */

#define LS_INIT_NO_MEM   -1
#define LS_INIT_BAD_ADDR -2
#define LS_INIT_SYSERROR -3

/* Symbol Obfuscation ------------------------------------------------*/

/* The api symbols in the hqn_ls library have names which have been obscured
 * for increased security.  The following defines translate the obfuscated
 * symbols to the standard api.  When debugging, break points can only be set
 * on the obscured symbols. */

#ifndef LS_NOHIDE

#define ls_initialise    bri_size
#define ls_get_licence   s_datut_err
#define ls_update_lic    ccumu_dat
#define ls_release_lic   c1_y_data
#define ls_status        dev_e1_1
#define ls_get_users     vanta_size

#define ls_do_request    ft_elem_1
#define ls_do_update     thu_sizht
#define ls_get_challenge u_siz2_t
#define ls_get_data      open_nc_err
#define ls_status_val    find_r_dev
#define ls_upstatus_val  bag_errl
#define ls_xstatus_val   si_datisk
#define ls_get_status    lsh_query
#define ls_get_context   fru_clas

#define LSInfo           not_lost_pag
#define LSRequest        devin_siz
#define LSRelease        c_1_empty
#define LSUpdate         c2_e1_dump
#define LSQueryLicence   cnt_min_tx

#endif /* not LS_NOHIDE */

#define LS_STATUS_CODE hls_uint /* result of low level function call */
#define LS_HANDLE      hls_uint /* identifies a "licence context" */

/* This is obsolete, but a lot of old code uses it to fill in the first field
 * of LSdata structures. */
#define HQNLS_VERSION (0)

/* The LSdata structure is used to pass values to and return values from the
 * interface routines.  ls_initialise() is passed a pointer to this structure
 * and fills in the Handle field.
 *
 * APIVersion:    Placeholder; not meaningful to user code.
 *
 * PublisherName: A unique string, generally your company name.  Maximum of
 *                LS_PUBLISHER_LEN characters.
 *
 * ProductName:   A unique string within the Publisher domain describing your
 *                product.  Max LS_PRODUCT_LEN characters.
 *
 * VersionString: A unique string within the Product domain.  Max
 *                LS_VERSION_LEN characters.
 *
 * Nchals:        Number of challenge values stored in Chalvals.
 *
 * Chalvals:      Pointer to an array of hls_uints used for testing the licence
 *                server.
 *
 * Ndata:         Number of data values expected from the licence server.
 *
 * Data: (out)    Pointer to array of hls_ints.  The product specific data
 *                contained in the product permit is returned in this array.
 *                N.B. The first value is the number of bytes of data returned,
 *                therefore the length should be Ndata + 1.
 *
 * UpPeriod:(out) The time in seconds within which the server expects to be
 *                contacted periodically.  If no contact is made within this
 *                time the server may reallocate the licence.
 *
 * Handle: (out)  Pointer to opaque data structure for this product context.
 *
 * Sig_Index:     Pointer to an array of pointers to the signature arrays.
 *                There is one signature array for each challenge value (only
 *                needs to be set for ls_get_licence()).
 */

typedef struct {
  hls_uint    APIVersion;
  char       *PublisherName;
  char       *ProductName;
  char       *VersionString;
  hls_int     Nchals;
  hls_uint   *Chalvals;
  hls_int     Ndata;
  hls_int    *Data;
  hls_uint    UpPeriod; /* in seconds */
  LS_Handle   Handle;
  hls_uint  **Sig_Index;
} LSdata;

/* Extension of LSdata, containing data for ls_get_licence_ex etc.
 *
 * nServerRetries: number of times to retry connecting to the license server, if
 *           the initial attempt fails with LS_LICENCESYSNOTAVAILABLE.
 *           Zero means try just once.
 *
 * nLicenceRetries: number of times to retry getting a license, if
 *           the initial attempt fails with LS_NOLICENCESAVAILABLE.
 *           Zero means try just once.
 *
 * retryMillis: amount of time to sleep between tries connecting to the
 *           license server. Only meaningful if number of retries is greater
 *           than zero.
 */
typedef struct {
  LSdata      data;
  hls_uint    nServerRetries;
  hls_uint    nLicenceRetries;
  hls_uint    retryMillis;
} LSdataExt;

/* Client side interface to users information.
 * If the USERS policy for this product is set on the licence server it is
 * possible to obtain a list of all current licence holders of this product.
 * ls_get_users() returns a pointer to a LSusers structure which contains a
 * list of pointers to LSlicence structures as follows:
 */

typedef struct {
  struct {
    hls_uint ls_res1;  /* reserved */
    hls_uint ls_iaddr; /* internet address */
  }         ls_host;
  hls_uint  ls_type;     /* type of licence (see context flags) */
  hls_int   ls_res2;     /* reserved  */
  char     *ls_username; /* user name */
  hls_uint  ls_res3;     /* reserved */
  hls_uint  ls_start;    /* time licence taken (output of time()) */
  hls_uint  ls_res4;     /* reserved */
} LSlicence;

typedef struct {
  hls_uint    ls_ulen;  /* len of ls_ulist = total available licences */
  LSlicence **ls_ulist; /* free licences have NULL entries */
} LSusers;

/* The LS_CHALLENGE structure holds the details of the challenge which is
 * passed to hqn_lsd.  The structure is returned with the response to the
 * challenge.  This is required only be the lowest level interface. */

typedef union {
  struct {
    hls_uint      reserved;               /* must be zero */
    hls_uint      secret;                 /* which secret (1-4) */
    hls_uint      size;                   /* 4-LS_MAX_CHAL_DATA */
    unsigned char data[LS_MAX_CHAL_DATA]; /* size is really previous field */
  } challenge;
  struct {
    hls_uint      size;             /* byte length of signature */
    unsigned char data[LS_SIG_LEN]; /* holds returned signature */
  } response;
} LS_CHALLENGE;

/*--------------------- Level One Interface ----------------------*/

/* hls_int ls_initialise(LSdata *data)
 * Input:  Pointer to LSdata structure
 * Output: Returns number of hqnservers found or a negative error
 *         condition.  Fills in Handle field if successful.
 *         Possible errors: LS_INIT_NO_MEM - can't allocate memory.
 *                          LS_INIT_BAD_ADDR - Bad Internet address of host..
 *                          LS_INIT_SYSERROR - OS/network error.
 *
 * ls_initialise must be called before any other licensing routines.  It checks
 * for hqnserver aliases.  It is not an error if none are found but only a
 * connection to a local host server will be tried in that case.  Otherwise
 * connection is attempted in order, starting with the local host, hqnserver1,
 * etc until a hqn_lsd daemon is found.  A unique handle is return in the
 * Handle field of the LSdata if successful.  ls_initialise() also reads the
 * environment variable HQNSERVERS; if it is set then the client library will
 * attempt to communicate with licence servers running on the hosts named in
 * this space-separated list instead of the default hqnserver1, hqnserver2,
 * etc.
 */
extern hls_int ls_initialise(LSdata *data);

/* hls_bool ls_get_licence(LSdata *data)
 * input:  pointer to a LSdata structure.
 * output: returns 1 for success else 0
 * An attempt is made to get a licence for the product specified in
 * the LSdata structure.  If successful the Data and UpPeriod fields
 * of the structure are filled in.  The return signature from the
 * challenge mechanism is also checked and if an illegal state is
 * detected the returned Data values will be bit-inverted (logically
 * 'notted') note that the the routine still returns success however.
 * If multiple licences are required then LS_Sig_Index must be set to
 * the correct signature list before calling ls_get_licence.
 *
 * N.B. - The first value in the Data field is the number of bytes returned.
 */
extern hls_bool ls_get_licence(LSdata *data);

/* Like ls_get_licence(), but respecting the additional fields in an
 * LSdataExt structure.
 */
extern hls_bool ls_get_licence_ex(LSdataExt* data);

/* for alternative spelling */
#define ls_get_license ls_get_licence
#define ls_get_license_ex ls_get_licence_ex

/* hls_bool ls_update_lic(LSdata *data)
 * Input: pointer to a LSdata structure.
 * Output: returns 1 for successful contact, else 0
 * This must be called at intervals no longer than the UpPeriod of
 * the LSdata structure.  It passes a challenge value to the server
 * and checks the returned signature, if an illegal state is
 * detected the Data values become bit-inverted.  Generally if the
 * update fails (e.g the current server has failed) ls_get_licence
 * should be called again.
 */
extern hls_bool ls_update_lic(LSdata *data);

/* hls_bool ls_release_lic(LSdata *data)
 * Input: pointer to a LSdata structure.
 * Output: returns 1 for successful release, else 0
 * Before termination the allocated licence should be released.
 * It is not essential to test the return value.
 */
extern hls_bool ls_release_lic(LSdata *data);

/* char *ls_status(LSdata *data)
 * Input:  pointer to a LSdata structure.
 * Output: Returns a pointer to an error string for the last status
 *         if it was not LS_SUCCESS, otherwise it returns a NULL
 *         pointer.
 */
extern char *ls_status(LSdata *data);

/* LSusers *ls_get_users(LSdata *data)
 * Input:  pointer to a LSdata structure.
 * Output: Returns a pointer to an LSusers structure if successful
 *         otherwise returns a NULL pointer.
 * If the server policy for the product allows it a list of current
 * licence holders can be obtained.  Note that the memory allocated
 * for the structures is freed on subsequent calls.
 */
extern LSusers *ls_get_users(LSdata *data);


/* Callback function
 * void *ls_get_orb(void)
 * Output: Returns a CORBA::ORB_ptr cast to a void *
 * Called by the licence server to obtain a reference to the orb when
 * using CORBA for inter-process communication
 */
extern void * ls_get_orb(void);

/*--------------------- Level Two Interface ----------------------*/

/* N.B. ls_initialise must be called before any of these routines */

/* hls_bool ls_do_request(LSdata *data)
 * Input  : pointer to a LSdata structure.
 * Output : TRUE for success, else FALSE
 * Requests a licence for the product specified in the LSdata
 * structure.  A challenge value is passed to the server and
 * the returned signature can be found in the LS_CHALLENGE
 * response structure.  No checking of the signature is done.
 */
extern hls_bool ls_do_request(LSdata *data);

/* hls_bool ls_get_data(LSdata *data)
 * Input:  pointer to a LSdata structure.
 * Output: TRUE for success, else FALSE
 * If successful fills in the Data and UpPeriod of the LSdata
 * structure.  No checking of the signature is done.
 */
extern hls_bool ls_get_data(LSdata *data);

/* LS_CHALLENGE *ls_get_challenge(hls_uint *secret, hls_uint *chalval,
 *     LSdata *data)
 * Input:  Pointers to storage space for the secret and challenge
 *         indices used in the last challenge.
 *         pointer to a LSdata structure.
 * Output: Returns a pointer to a LS_CHALLENGE structure and
 *         fills in the secret and chalval indices.
 * After an ls_do_request or ls_do_update the response field
 * of the LS_CHALLENGE holds the length and value of the returned
 * signature.  secret is the index of the secret used and chalval
 * is the index of challenge value used.  These are used to find
 * the correct signature array to check the returned signature
 * against.
 */
extern LS_CHALLENGE *ls_get_challenge(hls_uint *secret, hls_uint *chalval,
    LSdata *data);

/* hls_bool ls_get_context(hls_uint *buffer, LSdata *data)
 * Input:  ptr to array of hls_uints of size LS_CONTEXT_SIZE
 *         pointer to a LSdata structure.
 * Output: TRUE for success else FALSE
 * If successful the licence context is placed in the buffer:
 *
 * Index        Contents
 * 0    Total number of bytes returned in buffer
 * 1    Flags.  Lower four bits comprise the access level of the user; full
 *      access is 8, lowest level is 1.  The application can use this value if
 *      required to grant privileged functionality etc, in which case
 *      documentation should be provided for system administrators.
 *      LS_LIC_LOCKED can be logically and'd with the value to obtain the
 *      type of the licence (node-locked or floating).
 * 2    Current policy flags of the product permit.  See hqn_lsadmin.
 * 3    Start date of the permit from which the licence is obtained.  This
 *      is an integer of the form e.g. 19930421 for 21st April 1993.
 * 4    Expiry date of the permit in the same format as 3.
 */
extern hls_bool ls_get_context(hls_uint *buffer, LSdata *data);

/* hls_bool ls_do_update(LSdata *data)
 * Input: pointer to a LSdata structure.
 * Output: TRUE for success, else FALSE
 * As for ls_update_lic but does not check the returned signature.
 */
extern hls_bool ls_do_update(LSdata *data);

/* hls_uint ls_status_val(LSdata *data)
 * Input:  pointer to a LSdata structure.
 * Output: returns the last standard status value
 */
extern hls_uint ls_status_val(LSdata *data);

/* hls_uint ls_upstatus_val(LSdata *data)
 * Input:  pointer to a LSdata structure.
 * Output: returns the extended status value of the last update call.
 */
extern hls_uint ls_upstatus_val(LSdata *data);

/* hls_uint ls_xstatus_val(LSdata *data)
 * Input:  pointer to a LSdata structure.
 * Output: returns the last extended status value.
 */
extern hls_uint ls_xstatus_val(LSdata *data);

/* char *ls_get_status(LS_STATUS_CODE status, LSdata *data)
 * Input:  standard or extended status code.
 *         pointer to a LSdata structure.
 * Output: returns an error string describing the status value,
 *         or NULL if the current server could not be contacted.
 */
extern char *ls_get_status(LS_STATUS_CODE status, LSdata *data);

/*--------------------- Low Level Interface (3) ----------------------*/

/* Details of this interface are provided in the api documentation */

/* LSInfo points to the current LSdata structure.  ls_initialise sets this but
 * if multiple licences are required then it will need to be set explicitly
 * before each level 3 call. */

extern LSdata *LSInfo;

/* Request licensing resources needed, and associate them with a handle */

extern LS_STATUS_CODE LSRequest(unsigned char *, unsigned char *,
    unsigned char *, hls_uint *, unsigned char *,
    LS_CHALLENGE *, LS_HANDLE *);

/* Release licensing resources associated with the handle */

extern LS_STATUS_CODE LSRelease(LS_HANDLE, hls_uint, unsigned char *);

/* Confirm licence details / change units */

extern LS_STATUS_CODE LSUpdate(LS_HANDLE, hls_uint, hls_int *, unsigned char *,
    LS_CHALLENGE *);

/* Ask for details of licence */

extern LS_STATUS_CODE LSQueryLicence(LS_HANDLE, hls_uint, void *, hls_uint);

/* for alternative spelling */
#define LSQueryLicense LSQueryLicence

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* not LS_HQN_LS_H */

/* Modification history:
* Log stripped */
