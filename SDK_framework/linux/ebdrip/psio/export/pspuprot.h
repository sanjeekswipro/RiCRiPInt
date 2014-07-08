/*
 *    File: pspuprot.h    * PSio: PUblic definitions of routine PROTotypes
 *
 *  Author: Frederick A. Hallock (Fredo)
 *  
 *  PLEASE NOTE MACRO NAMING CONVENTION! SEE PSP?MACR.H
 *
 *  This file creates all of the publicly accessable routine prototypes.
 *  The file is "locked" by definition of XPSPUPROT so that multiple
 *  inclusions are not detrimental.
 *
 *$Id: export:pspuprot.h,v 1.17.31.1.1.1 2013/12/19 11:27:54 anon Exp $
 *
* Log stripped */

#ifndef   XPSPUPROT
#define   XPSPUPROT

#include "gdevstio.h"
#include "pspumacr.h"
#include "psputype.h"

#ifdef __cplusplus
extern "C"
{
#endif

/******************************************************************************
Remember: Only public routines or global structures should be declared here.
******************************************************************************/

/* PSSYSINI */

PS_error PS_new_method PROTO((PS_METH *fnc_ptrs,
                              PS_METH_HANDLE *meth_handle));

PS_error PS_init PROTO((void));


/* PSFILEIO */
/* Provide original entry point for compatibility. */
#define PS_open_read_file(filename, mode, meth_handle, client_data,     \
                          pinst_handle, pobj_handle)                    \
  (PS_open_read_file_ex((filename), (mode),                             \
                        (meth_handle), (client_data),                   \
                        (pinst_handle), (pobj_handle), NULL, NULL))

PS_error PS_open_read_file_ex PROTO((uint8 *filename, int32 mode,
                                     PS_METH_HANDLE meth_handle,
                                     PS_CLIENT_DATA client_data,
                                     PS_INST_HANDLE *inst_handle,
                                     PS_OBJ_HANDLE *obj_handle,
                                     PS_ADD_CALLBACK add_callback,
                                     PS_CLIENT_DATA add_data));

/* Provide original entry point for compatibility. */
#define PS_open_write_file(filename, mode, findent, inst_handle)        \
  (PS_open_write_file_ex((filename), (mode), (findent),                 \
                         (inst_handle), NULL, NULL))

PS_error PS_open_write_file_ex PROTO((uint8 *filename, int32 mode,
                                      int32 fIndent,
                                      PS_INST_HANDLE inst_handle,
                                      PS_GEN_CALLBACK gen_callback,
                                      PS_CLIENT_DATA gen_data));


/* PSADMLIB */

PS_error PS_create_inst PROTO((PS_METH_HANDLE meth_handle,
                               PS_CLIENT_DATA client_data,
                               PS_INST_HANDLE *inst_handle,
                               PS_OBJ_HANDLE *obj_handle));

PS_error PS_cleanup PROTO((PS_INST_HANDLE inst_handle));

PS_error PS_get_error PROTO((PS_INST_HANDLE inst_handle,
                             PS_ERR *next_err));

PS_error PS_get_write_size(PS_INST_HANDLE inst_handle, int32 fIndent, int32 *write_size);

PS_error PS_set_attribute(PS_INST_HANDLE inst_handle, PS_CTL control);

PS_error PS_set_client_data(PS_INST_HANDLE inst_handle, PS_CLIENT_DATA raw_data);
    

/* PSOBJLIB */

PS_error PS_get_obj PROTO((PS_OBJ_HANDLE obj_handle,
                           PS_CTL control,
                           PS_TYPE_SIZE *type,
                           PS_OBJ_HANDLE *retobj_handle));

PS_error PS_obj_lookup PROTO((PS_OBJ_HANDLE obj_handle,
                              PS_CTL control,
                              PS_TYPE_SIZE type,
                              PS_OBJ_TYPES *value,
                              PS_OBJ_HANDLE *retobj_handle));

PS_error PS_read_val_etc PROTO((PS_OBJ_HANDLE obj_handle,
                                PS_CTL control,
                            	PS_TYPE_SIZE *type,
                                PS_OBJ_TYPES *value,
    	                        PS_OBJ_HANDLE *retobj_handle));

PS_error PS_read_val PROTO((PS_OBJ_HANDLE obj_handle,
                            PS_CTL control,
                            PS_TYPE_SIZE *type,
                            PS_OBJ_TYPES *value));

PS_error PS_write_val PROTO((PS_OBJ_HANDLE obj_handle,
                             PS_CTL control,
                             PS_TYPE_SIZE type,
                             PS_OBJ_TYPES *value,
                             PS_OBJ_HANDLE *retobj_handle));

PS_error PS_modify_obj_etc PROTO((PS_OBJ_HANDLE obj_handle,
                              	  PS_CTL control,
                              	  PS_TYPE_SIZE type,
                              	  PS_OBJ_TYPES *value,
                             	  PS_OBJ_HANDLE *retobj_handle));

PS_error PS_modify_obj PROTO((PS_OBJ_HANDLE obj_handle,
                              PS_CTL control,
                              PS_TYPE_SIZE type,
                              PS_OBJ_TYPES *value));

/* PSTRANSL */
PS_error PS_read_DS PROTO((PS_OBJ_HANDLE obj_handle,
                           PS_CTL control,
                           DICTSTRUCTION *dicttemplate,
                           uint8 *data_address,
                           PS_OBJ_HANDLE *retobj_handle));

PS_error PS_write_DS PROTO((PS_OBJ_HANDLE obj_handle,
                            PS_CTL control,
                            DICTSTRUCTION *dicttemplate,
                            uint8 *data_address,
                            PS_OBJ_HANDLE *retobj_handle));

PS_error PS_modify_DS PROTO((PS_OBJ_HANDLE obj_handle,
                             PS_CTL control,
                             DICTSTRUCTION *dicttemplate,
                             uint8 *data_address,
                             PS_OBJ_HANDLE *retobj_handle));

/******************************************************************************
******************************************************************************/

#ifdef __cplusplus
}
#endif

#endif /* XPSPUPROT */

