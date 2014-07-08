#ifndef __PSIOUTIL_H__
#define __PSIOUTIL_H__

/* $HopeName: SWpsio!export:psioutil.h(EBDSDK_P.1) $
 * Copyright (c) 1995-2011 Global Graphics Software Ltd.
 *
 * Macros/protos &c. for the convenience functions.
 *
* Log stripped */

/* ----------------------------- Includes ---------------------------------- */

#include "std.h"

/* psio */
#include "psputype.h"
#include "pspuprot.h"

/* ggcore */

#include "fwstring.h"   /* FwTextString */


#ifdef __cplusplus
extern "C" {
#endif


/* ------------ Support for heirarchies of PSIO dictionaries --------------- */

/* Often we want a PSIO file to contain a heirarchy of dictionaries, with
 * different modules responsible for different dictionaries in the heirarchy.
 * These definitions allow modules to recursively declare a static structure
 * describing this heirarchy in terms of those defined by other modules. This
 * structure contains the minimal amount needed for PSIO actions to be
 * automated in the simple case of an array of DICTSTRUCTIONs and an area of
 * memory to work on, whilst allowing sufficient hooks for modules to add
 * their own custom PSIO code. The hierarchy is traversed depth first, with the
 * action being invoked on each node, followed by its children in the order it
 * lists them in pChildren.
 */

/* Use a different error type as extended to include client specific errors */
typedef PS_error        dictResult;

/* The set of possible actions on a dictionary heirarchy */
enum
{
  Dict_CreatePSIOTree   = 0,    /* Fill in empty PSIO tree from data */
  Dict_LoadFromPSIOTree = 1,    /* Update data from PSIO tree */
  Dict_SaveToPSIOTree   = 2     /* Update existing PSIO tree from data */
};

typedef int32 DictPSIOAction;

/* Forward declaration of a node in the heirarchy */
struct DictPSIONode;

/* The context passed when an action is invoked on the heirarchy */
typedef struct DictPSIOContext
{
  struct DictPSIOContext *      pParentContext; /* parent context NULL<=>root*/
  struct FwErrorState *         pErrorState;    /* for passing back errors */
  DictPSIOAction                action;         /* action to do */
  struct DictPSIONode *         pTree;          /* node in dict heirarchy */
  PS_OBJ_HANDLE                 dict;           /* PSIO dict object */
  uint8 *                       pData;          /* dict data to work on */
  uint8 *                       pPrivate;       /* module specific context */
} DictPSIOContext;

/* The function type for an action */
typedef dictResult DICT_PSIO_FN( DictPSIOContext * pContext );

/* A node in the heirarchy */
typedef struct DictPSIONode
{
  char *                        dictionaryKey;
  struct dictstruction *        pDictStructions;

  /* hook to allow override non-recursive action on self */
  DICT_PSIO_FN *                pDoNode;        /* &dictDefaultNodeAction */

  uint32                        nChildren;
  struct DictPSIOChild *        pChildren;      /* NULL <=> nChildren == 0 */
  /* only used for a node which contains an array of dicts,
   * arrayDefaultTreeAction uses it.
   */
  uint32                        nBytesInDSData;
} DictPSIONode;

/* Each non leaf node has an array of these (pChildren) to describe children.
 * Actions do a depth first tree scan where for each node the action is done
 * on the node itself, before doing the children in pChildren array order.
 */
typedef struct DictPSIOChild
{
  DictPSIONode *        pTree;          /* child dict heirarchy */
  uint32                dataOffset;     /* offset of child data */
  uint32                privateOffset;  /* offset of child module */
  /* hook to allow override recursive action on child */
  DICT_PSIO_FN *        pDoTree;        /* &dictDefaultTreeAction */
} DictPSIOChild;

/* Read in a PSIO file in as a tree of PSIO objects, convert to client format
 * using dictTreeWalk, then discard PSIO tree.
 * Return TRUE <=> success.
 */
extern int32 dictReadFile
 (
   struct FwErrorState *pErrorState,    /* for passing back errors */
   struct DictPSIONode *pTree,          /* node in dict heirarchy */
   PS_METH_HANDLE       psioMethod,     /* PSIO method */
   FwTextString         ptbzFilename,   /* File to read */
   DICT_PSIO_FN *       treeAction,     /* procedure to start off the tree */
   uint8 *              pData,          /* dict data to work on */
   uint8 *              pPrivate,       /* module specific context */
   PS_CLIENT_DATA       client_data,    /* client data for PSIO */
   int32                psio_meth_mode  /* mode to open file */
 );

/* Convert client data to tree of PSIO objects using dictTreeWalk,
 * write this out to a file, then discard PSIO tree.
 */
extern int32 dictWriteFile
 (
   struct FwErrorState *pErrorState,    /* for passing back errors */
   DictPSIONode *       pTree,          /* node in dict heirarchy */
   PS_METH_HANDLE       psioMethod,     /* PSIO method */
   FwTextString         ptbzFilename,   /* File to write */
   DICT_PSIO_FN *       treeAction,     /* procedure to start off the tree */
   uint8 *              pData,          /* dict data to work on */
   uint8 *              pPrivate,       /* module specific context */
   PS_CLIENT_DATA       client_data,    /* client data for PSIO */
   int32                psio_meth_mode  /* mode to open file */
 );

/* Use this to invoke an action, creates a dummy root DictPSIOContext.
 * Note parentDict isnt the PSIO dict corresponding to pTree, but its parent.
 * pErrorState is optional at present (can pass NULL), but may eventually
 * become compulsory.
 */
extern dictResult dictTreeWalk
 (
   struct FwErrorState *pErrorState,/* for passing back errors */
   DictPSIOAction       action,     /* action to do */
   struct DictPSIONode *pTree,      /* node in dict heirarchy */
   PS_OBJ_HANDLE        parentDict, /* parent of one matching pTree */
   DICT_PSIO_FN *       treeAction, /* procedure to start off the tree */
   uint8 *              pData,      /* dict data to work on */
   uint8 *              pPrivate    /* module specific context */
 );

#define dictDoAction( action, pTree, parentDict, treeAction, pData, pPrivate )\
 dictTreeWalk( NULL, action, pTree, parentDict, treeAction, pData, pPrivate )

/* Default function to operate on a single node in the dict heirarchy,
 * without operating on children.
 */
extern DICT_PSIO_FN     dictDefaultNodeAction;

/* Default function to operate on the dict heirarchy recursively.
 * The caller should fill in the elements of the pContext parameter except
 * for dict (the handle of the PSIO dict). This function, or its replacement is
 * responsible for setting dict up, probably using the parent dictionary
 * handle in the parent context, and the function dictSetupPSIODict() below.
 */
extern DICT_PSIO_FN     dictDefaultTreeAction;

/* Default function to work on a tree which is an array of the same dicts. 
 * In Dict_LoadFromPSIOTree mode, memory is allocated for the node action
 * to use. The structure that is used here _must_ have its next pointer
 * as the first entry, because this code will link these structures together.
 * Children of the node refer to tree heirarchies within _each_ dictionary.
 */
extern DICT_PSIO_FN     arrayDefaultTreeAction;



/* This is used by dictDefaultTreeAction to create or lookup the PSIO dict,
 * and fills in the dict field in the context.
 * It can be used by modules overriding dictDefaultTreeAction.
 */
extern dictResult dictSetupPSIODict PROTO
 ((PS_OBJ_HANDLE parentDict, DictPSIOContext * pContext, PS_CTL compoundType));

/* ---array scanning --- */

/* simply increase if not enough */
#define ARRAY_SCAN_MAX_DEPTH    4

typedef dictResult ARRAY_SCAN_FN PROTO
((
  DictPSIOContext *     pContext,
  int32                 depth,
  int32 *               pIndices,       /* 0, 1, ... (depth-1) */
  int32 *               pSizes,         /* 0, 1, ... (depth-1) */
  PS_TYPE_SIZE *        pType,
  PS_OBJ_TYPES *        pValue
));

/* This takes a (ARRAY_SCAN_FN *) as pDoNode */
extern DICT_PSIO_FN     arrayScanTreeAction;

/* --------------------- Support for regular arrays ------------------------ */

int32 PS_get_regular(PS_OBJ_HANDLE  psioObj,
                     int32          depth,
                     PS_TYPE_SIZE   type,
                     uint8          *key,
                     void           *value,
                     int32          fTraceErrors);
int32 PS_open_regular(uint8          * Psiofile,
                      PS_METH_HANDLE   meth_handle,
                      PS_INST_HANDLE * pInstance,
                      PS_OBJ_HANDLE  * pObject,
                      PS_CLIENT_DATA   client_data,    /* client data for PSIO */
                      int32            psio_meth_mode  /* mode to open file */
                     );
int32 PS_destroy_regular(PS_INST_HANDLE Instance);

struct __PSArray
{
    PS_TYPE_SIZE type;
    int32 length;
    union
    {
      struct __PSArray *v_array;
      int32            *v_integer;
      float            *v_real;
      char             **v_string;
    } u;
};
typedef struct __PSArray PSArray_t, *pPSArray_t;

/* general operations */
#define PSA_Length(d)                 ((d).length)
#define PSA_Type(d)                   ((d).type)

void PS_free_PSArray(PSArray_t *pArray);

/* Type-specific but "unsafe" access macros */

#define PSA_UnsafeLen2(d,i)           ((d).u.array[i].length)
#define PSA_UnsafeLen3(d,i,j)         ((d).u.array[i].u.array[j].length)

#define PSA_UnsafeType2(d,i)          ((d).u.array[i].type)
#define PSA_UnsafeType3(d,i,j)        ((d).u.array[i].u.array[j].type)

#define PSA_UnsafeInt1(d,i)           ((d).u.v_integer[i])
#define PSA_UnsafeFloat1(d,i)         ((d).u.v_real[i])
#define PSA_UnsafeString1(d,i)        ((d).u.v_string[i])
#define PSA_UnsafeName1(d,i)          ((d).u.v_string[i])
#define PSA_UnsafeArray1(d,i)         ((d).u.v_array[i])

#define PSA_UnsafeInt2(d,i,j)         ((d).u.v_array[i].u.v_integer[j])
#define PSA_UnsafeFloat2(d,i,j)       ((d).u.v_array[i].u.v_real[j])
#define PSA_UnsafeString2(d,i,j)      ((d).u.v_array[i].u.v_string[j])
#define PSA_UnsafeName2(d,i,j)        ((d).u.v_array[i].u.v_string[j])

#define PSA_UnsafeInt3(d,i,j,k)       ((d).u.v_array[i].u.v_array[j].u.v_integer[k])
#define PSA_UnsafeFloat3(d,i,j,k)     ((d).u.v_array[i].u.v_array[j].u.v_real[k])
#define PSA_UnsafeString3(d,i,j,k)    ((d).u.v_array[i].u.v_array[j].u.v_string[k])
#define PSA_UnsafeName3(d,i,j,k)      ((d).u.v_array[i].u.v_array[j].u.v_string[k])

/*
 * Type-checking and range-checking versions of above.
 */

#define PSA_Internal(d,typeid,uval,pType,i) \
  ((PSA_Type(d) == (typeid)) ? \
   ((0 <= (i) && (i) < PSA_Length(d)) ? \
    (d).u.uval : \
    (pType)(HQFAIL("PSArray_t: range check"),0)) : \
   (pType)(HQFAIL("PSArray_t: type check"),0))[i]

/* Added definitions to bypass recursive expand choked on by Symantec C compiler        */

#define PSA_Internal1(d,typeid,uval,pType,i) \
  ((PSA_Type(d) == (typeid)) ? \
   ((0 <= (i) && (i) < PSA_Length(d)) ? \
    (d).u.uval : \
    (pType)(HQFAIL("PSArray_t: range check"),0)) : \
   (pType)(HQFAIL("PSArray_t: type check"),0))[i]

#define PSA_Internal2(d,typeid,uval,pType,i) \
  ((PSA_Type(d) == (typeid)) ? \
   ((0 <= (i) && (i) < PSA_Length(d)) ? \
    (d).u.uval : \
    (pType)(HQFAIL("PSArray_t: range check"),0)) : \
   (pType)(HQFAIL("PSArray_t: type check"),0))[i]

#define PSA_SafeLen2(d,i)      PSA_Internal(d,CPS_ARRAY,array,PSArray_t*,i).length
#define PSA_SafeLen3(d,i,j) \
  PSA_Internal(PSA_Internal1(d,CPS_ARRAY,v_array,PSArray_t*,i), \
               CPS_ARRAY,array,PSArray_t*,j).length

#define PSA_SafeType2(d,i)     PSA_Internal(d,CPS_ARRAY,array,PSArray_t*,i).type
#define PSA_SafeType3(d,i,j) \
  PSA_Internal(PSA_Internal1(d,CPS_ARRAY,array,PSArray_t*,i), \
               CPS_ARRAY,array,PSArray_t*,j).type

#define PSA_SafeInt1(d,i)      PSA_Internal(d,CPS_INTEGER,v_integer,int32*,i)
#define PSA_SafeFloat1(d,i)    PSA_Internal(d,CPS_REAL,v_real,float*,i)
#define PSA_SafeString1(d,i)   PSA_Internal(d,CPS_STRING,v_string,char**,i)
#define PSA_SafeName1(d,i)     PSA_Internal(d,CPS_NAME,v_string,char**,i)
#define PSA_SafeArray1(d,i)    PSA_Internal(d,CPS_ARRAY,v_array,PSArray_t*,i)

#define PSA_SafeInt2(d,i,j) \
  PSA_Internal(PSA_Internal1(d,CPS_ARRAY,v_array,PSArray_t*,i), \
               CPS_INTEGER,v_integer,int32*,j)
#define PSA_SafeFloat2(d,i,j) \
  PSA_Internal(PSA_Internal1(d,CPS_ARRAY,v_array,PSArray_t*,i), \
               CPS_REAL,v_real,float*,j)
#define PSA_SafeString2(d,i,j) \
  PSA_Internal(PSA_Internal1(d,CPS_ARRAY,v_array,PSArray_t*,i), \
               CPS_STRING,v_string,char**,j)
#define PSA_SafeName2(d,i,j) \
  PSA_Internal(PSA_Internal1(d,CPS_ARRAY,v_array,PSArray_t*,i), \
               CPS_NAME,v_string,char**,j)

#define PSA_SafeInt3(d,i,j,k) \
  PSA_Internal(PSA_Internal1(PSA_Internal2(d,CPS_ARRAY,v_array,PSArray_t*,i),\
                            CPS_ARRAY,v_array,PSArray_t*,j), \
               CPS_INTEGER,v_integer,int32*,k)
#define PSA_SafeFloat3(d,i,j,k) \
  PSA_Internal(PSA_Internal1(PSA_Internal2(d,CPS_ARRAY,v_array,PSArray_t*,i),\
                            CPS_ARRAY,v_array,PSArray_t*,j), \
               CPS_REAL,v_real,float*,k)
#define PSA_SafeString3(d,i,j,k) \
  PSA_Internal(PSA_Internal1(PSA_Internal2(d,CPS_ARRAY,v_array,PSArray_t*,i),\
                            CPS_ARRAY,v_array,PSArray_t*,j), \
               CPS_STRING,v_string,char**,k)
#define PSA_SafeName3(d,i,j,k) \
  PSA_Internal(PSA_Internal1(PSA_Internal2(d,CPS_ARRAY,v_array,PSArray_t*,i),\
                            CPS_ARRAY,v_array,PSArray_t*,j), \
               CPS_NAME,v_string,char**,k)


/* use safe macros unless building release version */
#if defined( ASSERT_BUILD ) /* { */

#define PSA_Length2(d,i)        PSA_SafeLen2(d,i)
#define PSA_Length3(d,i,j)      PSA_SafeLen3(d,i,j)

#define PSA_Type2(d,i)          PSA_SafeType2(d,i)
#define PSA_Type3(d,i,j)        PSA_SafeType3(d,i,j)

#define PSA_Integer(d,i)        PSA_SafeInt1(d,i)
#define PSA_Float(d,i)          PSA_SafeFloat1(d,i)
#define PSA_String(d,i)         PSA_SafeString1(d,i)
#define PSA_Name(d,i)           PSA_SafeName1(d,i)

#define PSA_Array(d,i)          PSA_SafeArray1(d,i)

#define PSA_Integer2(d,i,j)     PSA_SafeInt2(d,i,j)
#define PSA_Float2(d,i,j)       PSA_SafeFloat2(d,i,j)
#define PSA_String2(d,i,j)      PSA_SafeString2(d,i,j)
#define PSA_Name2(d,i,j)        PSA_SafeName2(d,i,j)

#define PSA_Integer3(d,i,j,k)   PSA_SafeInt3(d,i,j,k)
#define PSA_Float3(d,i,j,k)     PSA_SafeFloat3(d,i,j,k)
#define PSA_String3(d,i,j,k)    PSA_SafeString3(d,i,j,k)
#define PSA_Name3(d,i,j)        PSA_SafeName3(d,i,j)

#else /* defined( ASSERT_BUILD ) } { */

#define PSA_Length2(d,i)        PSA_UnsafeLen2(d,i)
#define PSA_Length3(d,i,j)      PSA_UnsafeLen3(d,i,j)

#define PSA_Type2(d,i)          PSA_UnsafeType2(d,i)
#define PSA_Type3(d,i,j)        PSA_UnsafeType3(d,i,j)

#define PSA_Integer(d,i)        PSA_UnsafeInt1(d,i)
#define PSA_Float(d,i)          PSA_UnsafeFloat1(d,i)
#define PSA_String(d,i)         PSA_UnsafeString1(d,i)
#define PSA_Name(d,i)           PSA_UnsafeName1(d,i)

#define PSA_Array(d,i)          PSA_UnsafeArray1(d,i)

#define PSA_Integer2(d,i,j)     PSA_UnsafeInt2(d,i,j)
#define PSA_Float2(d,i,j)       PSA_UnsafeFloat2(d,i,j)
#define PSA_String2(d,i,j)      PSA_UnsafeString2(d,i,j)
#define PSA_Name2(d,i,j)        PSA_UnsafeName2(d,i,j)

#define PSA_Integer3(d,i,j,k)   PSA_UnsafeInt3(d,i,j,k)
#define PSA_Float3(d,i,j,k)     PSA_UnsafeFloat3(d,i,j,k)
#define PSA_String3(d,i,j,k)    PSA_UnsafeString3(d,i,j,k)
#define PSA_Name3(d,i,j)        PSA_UnsafeName3(d,i,j)

#endif /* defined( ASSERT_BUILD ) } */

/* Macros for writing key-value pairs, including some where the value
   is an array.
*/

#define psio_plain_add_val(_type1_,_undertype1_,_undercast1_,_value1_,_handle1_,_control1_) \
  MACRO_START \
  { \
    PS_OBJ_TYPES holder; \
    holder._undertype1_ = (_undercast1_)(_value1_); \
 \
    if (PS_write_val((_handle1_), (_control1_), \
                     (_type1_), \
                     &holder, \
                     &(_handle1_)) != CPS_SUCCESS) \
      { \
        failed = TRUE; goto done; \
      } \
  } \
  MACRO_END

#define psio_add_value(_type2_,_undertype2_,_undercast2_,_value2_,_handle2_,_control2_) \
  MACRO_START \
    psio_plain_add_val(_type2_,_undertype2_,_undercast2_,_value2_,_handle2_,_control2_); \
 \
    (_control2_) = CPS_NO_OP; \
  MACRO_END

#define psio_add_key_value(_key_,_type_,_undertype_,_undercast_,_value_,_handle_,_control_) \
  MACRO_START \
    psio_plain_add_val(CPS_NAME, v_name, uint8*, _key_, _handle_, (PS_CTL)(_control_ | CPS_KEY_OP)); \
 \
    (_control_) = CPS_NO_OP; \
 \
    psio_plain_add_val(_type_, _undertype_, _undercast_, _value_, _handle_, _control_); \
  MACRO_END

#define psio_unknown_value(_unkey_) \
  MACRO_START \
    psio_add_key_value(_unkey_, CPS_STRING, v_string, uint8*, "Unknown", psioObj, psioControl); \
  MACRO_END

#define psio_any_value(_anyvname_) \
  MACRO_START \
   psio_add_key_value((_anyvname_), CPS_NAME, v_name, uint8*, PRO_ANY_KEY, psioObj, psioControl); \
  MACRO_END

#define psio_add_comment_to(_comment_, _whatObj_, _whatControl_) \
  MACRO_START \
    if (PS_write_val(_whatObj_, _whatControl_, \
                     CPS_COMMENT, (PS_OBJ_TYPES *)(void *)&(_comment_), \
                     &(_whatObj_)) != CPS_SUCCESS) \
      { \
        failed = TRUE; goto done; \
      } \
    (_whatControl_) = CPS_NO_OP; \
  MACRO_END

#define psio_add_comment() \
  MACRO_START \
  { \
    PS_CTL comm_control = CPS_NO_OP; \
    psio_add_comment_to(pBuf, psioObj, comm_control); \
  } \
  MACRO_END

#define psio_add_string_array(_arrayname_,_countfield_,_arrayfield_) \
  MACRO_START \
  if (pProfile->_arrayfield_) \
    { \
      int32 array_index; \
      int32 number_of_array_elements = pProfile->_countfield_; \
      uint8** array_elements = pProfile->_arrayfield_; \
 \
      PS_OBJ_HANDLE elements_Obj = CPS_BAD_OBJ_HAND; \
       \
      PS_CTL elements_Ctl = CPS_INSIDE_OP; \
 \
      psio_add_key_value((_arrayname_), CPS_ARRAY, v_array, struct ps_obj*, NULL, psioObj, psioControl); \
 \
      elements_Obj = psioObj; \
 \
      for (array_index = 0; \
           array_index < number_of_array_elements; \
           array_index++) \
        { \
          psio_add_value(CPS_STRING, v_string, uint8*, array_elements[array_index], elements_Obj, elements_Ctl); \
        } \
    } else { \
      psio_any_value(_arrayname_); \
    } \
  MACRO_END

#define psio_add_string_or_unknown(_name_,_field_) \
  MACRO_START \
  if (pProfile->_field_) \
    { \
      psio_add_key_value(_name_, CPS_STRING, v_string, uint8*, pProfile->_field_, psioObj, psioControl); \
    } else { \
      psio_unknown_value(_name_); \
    } \
  MACRO_END

/* ---------------------------------------------------------------------- */

/* Client data for PSIO "SubFileDecode" filter method, that layers on
 * top of another method.
 */

typedef struct PSIO_subfile_data
{
  PS_METH_HANDLE    base_meth_handle;
  PS_CLIENT_DATA    base_data;
  /* Use the PS filter interpretation of string and count, except that
   * an empty string with 0 count is treated like a 0-length file,
   * rather than returning the whole file.
   */
  char            * eod_string;
  uint32            eod_count;

  /* remaining fields are private - they do not need to be initialized */
  uint32            eod_string_len;
  uint32            matched_len;
} PSIO_subfile_data;

/* Basic method information required to instantiate a subfile filter.
 * The client is responsible for providing the alloc/free routines.
 */
extern PS_METH PSIO_subfile_filter_method;


#ifdef __cplusplus
}
#endif


#endif /* ! __PSIOUTIL_H__ */

/* eof psioutil.h */
