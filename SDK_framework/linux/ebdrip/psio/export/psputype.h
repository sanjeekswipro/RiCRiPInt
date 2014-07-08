/*
 *    File: psputype.h    * PSio: PUblic definitions of TYPEdefs
 *
 *  Author: Frederick A. Hallock (Fredo)
 *
 *  PLEASE NOTE MACRO NAMING CONVENTION! SEE PSP?MACR.H
 *
 *  This file creates all of the publicly accessable type definitions.
 *  The file is "locked" by definition of PSPUTYPE so that multiple
 *  inclusions are not detrimental.
 *  
 *$Id: export:psputype.h,v 1.16.4.1.1.1 2013/12/19 11:27:54 anon Exp $
 *
* Log stripped */

#ifndef   XPSPUTYPE
#define   XPSPUTYPE

#include "pspumacr.h"

typedef uintptr_t PS_METH_HANDLE ;  /* pointer to method in non-debug mode */
typedef uintptr_t PS_INST_HANDLE ;  /* pointer to instance in non-debug mode */
typedef uintptr_t PS_OBJ_HANDLE  ;  /* pointer to object in non-debug mode */
typedef uint8  PS_CTL ;             /* control flags passed in calls to PSIO */

typedef int32 PS_error ;
typedef uint8 PS_TYPE_SIZE ;

typedef void *PS_CLIENT_DATA ;


/*
    This is the subsystem function pointer structure which allows tailored
    definition of the low level I/O and memory allocate/free environment.
*/
typedef struct ps_meth
{
    HqnFileDescriptor (*psys_open)(uint8 *filename, int32 mode,
                           PS_CLIENT_DATA client_data);
    int32             (*psys_close)(HqnFileDescriptor handle, PS_CLIENT_DATA client_data);
    int32             (*psys_read)(HqnFileDescriptor handle, uint8 *data, uint32 size,
                           PS_CLIENT_DATA client_data);
    int32             (*psys_write)(HqnFileDescriptor handle, uint8 *data, uint32 size,
                           PS_CLIENT_DATA client_data);
    int32             (*psys_rename)(uint8 *filename, uint8 *newfilename,
                           PS_CLIENT_DATA client_data);
    int32             (*psys_delete)(uint8 *filename, PS_CLIENT_DATA client_data);
    uint8 *           (*psys_alloc)(int32 size, PS_CLIENT_DATA client_data);
    void              (*psys_free)(uint8* address, PS_CLIENT_DATA client_data);
    uint8 *           psys_tmpdir;    /* path of tmp file writes */

    uint32            magic_cookie; /* initialized in PS_new_method */
#ifdef    XPS_DEBUG
    uint32            valid           ;
#endif /* XPS_DEBUG */
} PS_METH ;

/*
    This is the error structure which PSIO creates.
*/
typedef struct ps_err
{
    PS_error     err_val ;
    uint32       data    ; /* like file line numbers on read */
} PS_ERR ;


/*  This next structure is the PSIO object type union used by the PSIO_OBJ
    structure and some of the object library routines.
*/
typedef union ps_obj_types
{
    CPS_TYPE_INT     v_integer    ;
    CPS_TYPE_REAL    v_real       ;
    CPS_TYPE_BOOL    v_bool       ;
    uint8           *v_comment    ; /* must begin with "%" */
    uint8           *v_name       ; /* must begin with "/" */
    uint8           *v_string     ; /* must begin with "(" */
    struct ps_obj   *v_array      ; /* array list */
    struct ps_obj   *v_dict       ; /* key, value pair list */
    struct ps_obj   *v_list       ; /* any compound list */
} PS_OBJ_TYPES ;


/* Prototype for client callback function that gets called as each
   object is about to be created and added to the PSIO object tree.  The
   callback can optionally cause the object not to be created, or abort
   parsing the file.

   As well as being called before creating each object, the callback is
   invoked with type CPS_DICT_END or CPS_ARRAY_END to mark the close of
   dicts and arrays, and eventually the end of the file (the top-level
   objects are actually stored in a special root dict).

   Return:
      CPS_SUCCESS       add object normally
      CPS_NO_OBJECT     omit object, but continue scanning
      other             abort scanning with CPS_FAILURE
*/
typedef int32 (*PS_ADD_CALLBACK) (PS_CTL control,
                                  PS_TYPE_SIZE type,
                                  PS_OBJ_TYPES *value,
                                  PS_CLIENT_DATA data);

/* Prototype for client callback function that gets called when a marker
   object is about to be written; the callback is called repeatedly to
   generate objects to substitute in place of the marker.

   Return TRUE to splice in a new object of the specified type and
   value, FALSE to end sequence.  The callback will be invoked
   repeatedly until it returns FALSE.  Arrays and dicts can be
   generated, returning CPS_DICT_END/CPS_ARRAY_END to close them.
      
*/
typedef int32 (*PS_GEN_CALLBACK) (PS_CTL *control,
                                  PS_TYPE_SIZE *type,
                                  PS_OBJ_TYPES *value,
                                  PS_CLIENT_DATA data);


/******************************************************************************
******************************************************************************/
#endif /* XPSPUTYPE */

