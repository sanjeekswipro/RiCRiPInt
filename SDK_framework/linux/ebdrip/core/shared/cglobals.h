/** \file
 * \ingroup core
 *
 * $HopeName: SWcore!shared:cglobals.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Utilities for declaring external init_C_globals functions.
 */

#ifndef __CGLOBALS_H__
#define __CGLOBALS_H__

/* Because the init_C_globals_<filename> functions are sprinkled all
   over the core code the files which implement the
   init_C_runtime_<module> functions would need to include a rats nest
   of header files. I did not want to extend the header list in these
   files purely to get to the init_C_globals_<filename> functions so
   decided to declare them in the files in which they are used. This
   macro helps us keep a well known function naming
   convention. Ultimately its hoped that all init_C_globals_<filename>
   functions would be purged from the code, but thats unlikely for a
   very long time. */

#define IMPORT_INIT_C_GLOBALS( _name ) \
  extern void init_C_globals_ ## _name(void) ;

/* Log stripped */
#endif
