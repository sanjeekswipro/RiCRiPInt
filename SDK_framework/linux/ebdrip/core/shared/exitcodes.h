/** \file
 * \ingroup core
 *
 * $HopeName: SWcore!shared:exitcodes.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Fatal RIP error codes.
 */

#ifndef __EXITCODES_H__
#define __EXITCODES_H__

struct SWSTART ; /* from COREinterface */

/* When modules are being initialised, they may choose to raise their
   own specific exit codes before returning FALSE from the init
   function. If they don't call dispatch_SwExit(), a catch all exit
   code will be raised which is somewhere inbetween
   CORE_BASE_MODULE_SWEXIT and CORE_BASE_SWEXIT. See rip_init() in
   swstart.c. We do this to ensure that dispatch_SwExit always gets
   called and we at least know which module failed. */
#define CORE_BASE_MODULE_SWEXIT 50

/* When the core makes an explicit call to dispatch_SwExit(), all exit codes are
   somewhere above CORE_BASE_SWEXIT. Looking at various exit codes on
   different platforms and shells, I've decided not to introduce any
   particular scheme but rather just name them uniquely. */
#define CORE_BASE_SWEXIT 200

enum {
  swexit_error_undecided = CORE_BASE_SWEXIT,

  swexit_error_pthreads_dll,

  swexit_error_devs_01,
  swexit_error_devs_02,

  swexit_error_meminit_01,
  swexit_error_meminit_02,
  swexit_error_meminit_03,

  swexit_error_startup_01,
  swexit_error_startup_02,
  swexit_error_startup_03,
  swexit_error_startup_04,

  swexit_error_mps_lib_assert,

  swexit_error_zipdev_write_file_root,
  swexit_error_zipdev_read_file_root,

  swexit_error_zipsw_init,
  swexit_error_zipsw_config_01,
  swexit_error_zipsw_config_02,
  swexit_error_zipsw_config_03,

  swexit_error_pcl5macrodev_root,

  swexit_error_MAX /* Never use this! */
} ;

/** Calls to dispatch_SwExit() should only be done under fatal error
    conditions. They ought to be rare. */
Bool dispatch_SwExit( int32 code, char* string) ;

/** \brief Post-boot RIP initialisation.

    This should be called when the interpreter is ready to run.

    \retval TRUE All of the phase's modules initialised successfully.
    \retval FALSE One of the phase's modules failed to initialise.
            Initialisation is stopped at the first failure, and
            dispatch_SwExit() is called.
 */
Bool rip_ready(void) ;

/** \brief Test to see if the rip has inited as is ready to run
 *
 * \retval TRUE is the RIP has done all initialisation, and is running, or has
 *              started shutting down.
 * \retval FALSE if the RIP has not yet completed initialisation
 */
Bool rip_is_inited(void);

/** \brief Test to see if the rip is shutting down
 *
 * \retval TRUE is the RIP is shutting down.
 * \retval FALSE if the RIP has not yet started shutting down.
 */
Bool rip_is_quitting(void);

/** \brief Flag indicating if the RIP should return when complete, or exit
    the process. */
extern Bool swstart_must_return ;

/* Log stripped */
#endif
