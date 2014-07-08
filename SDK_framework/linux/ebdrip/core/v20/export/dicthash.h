/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!export:dicthash.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS dictionary sizes and hashing API
 */

#ifndef __DICTHASH_H__
#define __DICTHASH_H__

#include "objecth.h"      /* OBJECT */


#define SYSDICT_SIZE       (( int32 ) 512 )
#define USRDICT_SIZE       (( int32 ) 256 )
#define GLODICT_SIZE       (( int32 ) 128 )
#define INTERNAL_SIZE      (( int32 ) 256 )
#define FONTPRIVATE_SIZE   (( int32 )  16 )

extern OBJECT userdict ;
extern OBJECT systemdict ;
extern OBJECT globaldict ;
extern OBJECT internaldict ;

Bool initDicts(void);
void finishDicts(void);

int32 getDictMaxLength(OBJECT *dict);

Bool insert_hash(register OBJECT *thed,register OBJECT *thekey,OBJECT *theo);
Bool insert_hash_even_if_readonly(register OBJECT *thed,register OBJECT *thekey,OBJECT *theo);
Bool fast_insert_hash(register OBJECT *thed,register OBJECT *thekey, OBJECT *theo);
Bool fast_insert_hash_name(register OBJECT *thed, int32 namenum, OBJECT *theo);

#define fast_sys_extract_hash(thekey) fast_extract_hash(&systemdict, thekey)
#define fast_user_extract_hash(thekey) fast_extract_hash(&userdict, thekey)
#define fast_global_extract_hash(thekey) fast_extract_hash(&globaldict, thekey)

Bool CopyDictionary(OBJECT *o1, OBJECT *o2,
                    Bool (*doInsert)(OBJECT *key, void *doData), void *doData);

extern void install_systemdict(OBJECT *sysd);

/* MACROS DEFINITIONS */

/* Warnings reduction: these are changed so that IF we execute
 * DO_RESULT(), a return TRUE immediately follows; thus we can avoid
 * return TRUE within the DO_RESULT macro and so avoid statement not
 * reached (the terminating while(0)) warnings.  [#10015]
 */

#define NAMECACHE_DICT_NOT_ANYWHERE( Akey , DO_ACTION , DO_RESULT ) \
if ( oType(*Akey) == ONAME ) { \
  NAMECACHE *Lnptr ; \
  Lnptr = oName(*Akey) ; \
  if ( Lnptr->dictobj == NULL && \
       Lnptr->dictval == NULL ) { \
    DO_ACTION() ; \
    DO_RESULT() ; \
    return TRUE ; \
  } \
}

#define NAMECACHE_DICT_EXTRACT_HASH( Akey , DO_TEST , DO_ACTION , DO_RESULT ) \
if ( oType(*Akey) == ONAME ) { \
  OBJECT    *Lther ; \
  OBJECT    *Lthed ; \
  NAMECACHE *Lnptr ; \
  Lnptr = oName(*Akey) ; \
  Lthed = Lnptr->dictobj ; \
  if ( DO_TEST( Lthed )) { \
    Lther = Lnptr->dictval ; \
    DO_ACTION( Lthed , Lther ) ; \
    if ( ! oCanRead(*Lthed) && !object_access_override(Lthed) ) \
      return error_handler( INVALIDACCESS ) ; \
    DO_RESULT( Lther ) ; \
    return TRUE ; \
  } \
}

#define NAMECACHE_DICT_INSERT_HASH(_context, Akey , Aval , DO_TEST , DO_ACTION , DO_RESULT ) \
if ( oType(*Akey) == ONAME ) { \
  OBJECT    *Lther ; \
  OBJECT    *Lthed ; \
  NAMECACHE *Lnptr ; \
  Lnptr = oName(*Akey) ; \
  Lthed = Lnptr->dictobj ; \
  if ( DO_TEST( Lthed )) { \
    Lther = Lnptr->dictval ; \
    DO_ACTION( Lthed , Lther ) ; \
    if ( DICT_ALLOC_LEN( Lthed ) == 0 ) { \
      --Lthed ; \
      HQASSERT(oType(*Lthed) != ONOTHING, "NAMECACHE_DICT_INSERT_HASH: bad link") ; \
      Lthed = oDict(*Lthed) ; \
    } \
    HQASSERT(Lther > Lthed && Lther <= Lthed + 1 + 2 * DICT_ALLOC_LEN(Lthed), \
             "NAMECACHE_DICT_INSERT_HASH: dictval out of range"); \
    if ( oGlobalValue(*Lthed)) \
      if ( illegalLocalIntoGlobal(Aval, (_context)) ) \
        return error_handler( INVALIDACCESS ) ; \
    if ( ! oCanWrite(*Lthed) && !object_access_override(Lthed) ) \
      return error_handler( INVALIDACCESS ) ; \
    if ( SLOTISNOTSAVED(Lthed, (_context)) )              \
      if ( ! check_dsave( Lthed , _context) ) \
        return FALSE ; \
    Copy( Lther , Aval ) ; \
    DO_RESULT() ; \
    return TRUE ; \
  } \
}

/* ACCESSOR MACRO DEFINITIONS */

#endif /* protection for multiple inclusion */


/* Log stripped */
