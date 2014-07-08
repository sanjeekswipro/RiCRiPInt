/** \file
 * \ingroup objects
 *
 * $HopeName: COREobjects!export:dictbld.h(EBDSDK_P.1) $
 * $Id: export:dictbld.h,v 1.10.10.1.1.1 2013/12/19 11:25:00 anon Exp $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Build dictionary from name, object array.
 */

#ifndef __DICTBLD_H__
#define __DICTBLD_H__

#include "objects.h"
#include "dictscan.h"

typedef struct nametypebuild {
  uint16 name ;           /* 32767 means end of array. */
  OBJECT value ;          /* The value to insert is placed here. */
} NAMETYPEBUILD ;

#define DUMMY_END_BUILD {0x7fff}
#define DUMMY_END_NAME 0x7fff

/* note: alloc_func pointers are memory allocation callbacks that must be
  supplied by the caller. This way the dictionaries can be maintained in
  either PS or PDF VM */

int32 dictbuild( OBJECT *dict , 
                 NAMETYPEBUILD build_objects[],
                 OBJECT * (*alloc_func)(int32 size, void * params),
                 void * alloc_params );

int32 dictmatchbuild( OBJECT *dict ,
                      NAMETYPEMATCH build_objects[] , 
                      OBJECT * (*alloc_func)(int32 size, void * params),
                      void * alloc_params ) ;

/*
Log stripped */

#endif /* protection for multiple inclusion */
