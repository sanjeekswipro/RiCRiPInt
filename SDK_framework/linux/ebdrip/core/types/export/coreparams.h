/** \file
 * \ingroup types
 *
 * $HopeName: COREtypes!export:coreparams.h(EBDSDK_P.1) $
 * $Id: export:coreparams.h,v 1.9.5.1.1.1 2013/12/19 11:25:08 anon Exp $
 *
 * Copyright (C) 2002-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Core generic parameter setting/getting. The generic parameter mechanism
 * does not belong to any particular compound, nor is it a glue function (it
 * is invariant across different Core product configurations).
 */

#ifndef __COREPARAMS_H__
#define __COREPARAMS_H__

#include "objecth.h"            /* OBJECT */
#include "dictscan.h"           /* NAMETYPEMATCH */

typedef struct module_params_t {
  /*@notnull@*/ /*@dependent@*/
  NAMETYPEMATCH *match ; /* array pointer */
  /* Parameter setter function should return TRUE for successfully set and also
     for read-only parameters, ignoring the attempt to set the read-only
     parameter. It should only return FALSE when a set of a writable parameter
     fails. */
  /*@notnull@*/
  Bool (*set_param)(corecontext_t *context, uint16 name, OBJECT *value) ;
  /* Parameter getter function should ignore names it does not understand or
     does not wish to allow returned, and leave the object pointer as it is.
     It should only return FALSE if an error occurs returning the value of a
     readable parameter. */
  /*@notnull@*/
  Bool (*get_param)(corecontext_t *context, uint16 name, OBJECT *value) ;

  /*@null@*/ /*@dependent@*/
  struct module_params_t *next ;
} module_params_t ;

#endif

/*
Log stripped */
