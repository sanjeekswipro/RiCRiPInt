/** \file
 *
 * impl.h.fmtpscon: POSTSCRIPT OBJECT FORMAT CONSTANTS
 *
 * $Id: fmtpscon.h,v 1.15.1.2.1.1 2013/12/19 11:27:07 anon Exp $
 * $HopeName: MMsrc!fmtpscon.h(EBDSDK_P.1) $
 * Copyright (c) 2001 Ravenbrook Limited.
 * Copyright (C) 2002-2013 Global Graphics Software Ltd. All rights reserved.
xo * Global Graphics Software Ltd. Confidential Information.
 *
 * .readership: MPS developers, SW developers
 *
 * .purpose: This contains definitions for a simplified version of the
 * PS object format for internal MM tests.  There's no need to track SW
 * - in fact, it's a better test if it's not 100% the same, as long as
 * all useful code paths are covered.
 */

#ifndef __FMTPSCON_H__
#define __FMTPSCON_H__


#define MACRO_START       do {
#define MACRO_END         } while (0)


/* definitions copied from hqtypes.h */


typedef signed   char                   int8;
typedef signed   short                  int16;
typedef signed   int                    int32;

typedef unsigned char                   uint8;
typedef unsigned short                  uint16;
typedef unsigned int                    uint32;


/* definitions copied from platform.h */


#if defined(VXWORKS)

typedef long intptr_t ;
typedef unsigned long uintptr_t ;

#endif


#if defined(_MSC_VER) && _MSC_VER < 1310

/* WIN DDK defines these. See io.h and stdarg.h in WIN DDK
   2600.1106. If not using the WIN DDK, we need to define (u)intptr_t
   ourselves. */
#ifndef _INTPTR_T_DEFINED
typedef int intptr_t ;
#endif
#ifndef _UINTPTR_T_DEFINED
typedef unsigned int uintptr_t ;
#endif

#endif


#ifdef  MACINTOSH

#ifndef _MACHTYPES_H_

#ifndef _INTPTR_T
#define _INTPTR_T
typedef long intptr_t;
#endif

#ifndef _UINTPTR_T
#define _UINTPTR_T
typedef unsigned long uintptr_t;
#endif

#endif

#endif /* MACINTOSH */


#ifdef linux

#if __WORDSIZE == 64
#define PLATFORM_IS_64BIT 1
#undef PLATFORM_IS_32BIT
# ifndef __intptr_t_defined
typedef long int                intptr_t;
#  define __intptr_t_defined
# endif
typedef unsigned long int       uintptr_t;
#else
#define PLATFORM_IS_32BIT 1
#undef PLATFORM_IS_64BIT
# ifndef __intptr_t_defined
typedef int                     intptr_t;
#  define __intptr_t_defined
# endif
typedef unsigned int            uintptr_t;
#endif

#endif /* linux */


/* save level & alloc mode, faked out */

typedef struct {
  int8 savelevel;
  int32 glallocmode;
} corecontext_t;

extern corecontext_t Context;


/* definitions copied from mm_core.h */

#define MINSAVELEVEL   0
#define MAXSAVELEVELS 32
#define SAVELEVELINC 2
#define NUMBERSAVES(_sl) ((_sl)>>1)

#define MM_PS_TYPED_ALIGNMENT (8)

#define MM_PS_ALIGNMENT (sizeof(OBJECT))


/* definitions copied from mm.h */


#define PTR_ALIGN_DOWN(type, x, a) \
  ((type)((char*)(x) - (uintptr_t)(x) % (uintptr_t)(a)))
#define PTR_IS_ALIGNED_P2(type, x, a) \
  (((uintptr_t)(x) & ((uintptr_t)(a) - 1)) == 0)


/* definitions copied from swvalues.h */


typedef float    USERVALUE ;


/* definitions copied from gcscan.h */


#define ADDR_OFFSET(base, limit) ((size_t)((char *)(limit) - (char *)(base)))


/* PS_MARK_BLOCK -- mark a block of PS memory
 *
 * Marks a block of memory from scan_base (inclusive) for scan_len
 * bytes.
 *
 * We know MPS_RETAIN will not change _scan_ptr, because the block will
 * not be moved.  */
#define PS_MARK_BLOCK(scan_state, scan_base, scan_len) \
  MACRO_START \
    mps_addr_t _scan_ptr = (mps_addr_t)(scan_base); \
    mps_addr_t _scan_limit = ADDR_ADD( _scan_ptr, scan_len ); \
    \
    for ( ; _scan_ptr < _scan_limit; \
         _scan_ptr = ADDR_ADD( _scan_ptr, sizeof(OBJECT) )) \
      MPS_RETAIN( &_scan_ptr, TRUE ); \
  MACRO_END


/* definitions copied from objecth.h */


typedef struct OBJECT OBJECT ;
typedef struct NAMECACHE NAMECACHE ;


/* definitions copied from objects.h */


typedef int8 TypeTag;

enum {
  tag_NCACHE = 1, /* avoid 0, for error detection */
  tag_FILELIST,
  tag_GSTATE,
  tag_LIMIT /* this is the limit of tag values, not a real tag */
};

/* The tag is in the same place in all types, that's the point of it. */
struct generic_typed_object {
  TypeTag typetag;
  /* other stuff */
};


#define CANEXEC       (0x20)
#define CANREAD       (0x40)
#define CANWRITE      (0x60)

#define ETYPEMASK     (0x1F)
#define ACCEMASK      (0x60)
#define EXECMASK      (0x80)

/* Used to mark an empty place in a dictionary + operator on execution stack */
#define ONOTHING      (0x00)   /* Exec - internal operator */

#define OINTEGER      (0x01)   /* Exec */
#define OREAL         (0x02)   /* Exec */
#define OINFINITY     (0x03)   /* Exec */

#define OOPERATOR     (0x04)   /* Exec */
#define OBOOLEAN      (0x05)   /* Exec */
#define OMARK         (0x06)   /* Exec - stopped mark */
#define ONULL         (0x07)   /* Exec  - pathforall */
#define OFONTID       (0x08)

#define ONAME         (0x09)   /* Exec */

#define OINDIRECT     (0x13)

/* NOTE THAT THIS NUMBER REPRESENTS COMPOSITE OBJECTS AND ABOVE */
#define OSAVE         (0x17)   /* Exec - marks a bgsave on execution stack */
#define ODICTIONARY   (0x19)   /* Exec */
#define OSTRING       (0x1A)   /* Exec */
#define OFILE         (0x1B)   /* Exec */

#define OARRAY        (0x1C)   /* Exec */
#define OPACKEDARRAY  (0x1D)   /* Exec-beware possible clashes*/

#define OGSTATE       (0x1E)


#define OLONGSTRING   (0x1F)    /* string len>65535, len in first 4 bytes of
                                 * clist, not in len field of OBJECT.  Most
                                 * PS operators won't accept this type....
                                 */

#define NONE          (0x00)
#define EXECUTE_ONLY  (0x20)
#define READ_ONLY     (0x40)
#define UNLIMITED     (0x60)

#define LITERAL       (0x00)
#define EXECUTABLE    (0x80)

#define ISAFONT       (0x80)

#define MAXPSSTRING     65535
#define MAXPSARRAY      65535
#define MAXPSDICT       65535
#define MAXPSNAME       254

/* Sub NULL OBJECT types */
#define ISINTERPRETEREXIT  0x00
#define ISPATHFORALL       0x01

#define GLOBMASK           (int8)0x01
#define ISLOCAL             (int8)0x00
#define ISGLOBAL            (int8)0x01
#define SAVEMASK           (int8)0x3e
#define VMMASK             (int8)0x40
#define ISPSVM              (int8)0x40 /**< Flag to mark PSVM objects. */
#define ISNOTVM             (int8)0x00 /**< Flag to mark non-VM objects. */



/* STRUCTURE DEFINITIONS */


struct NAMECACHE {
  TypeTag typetag ; /* must be first field */
  uint8 sid ;
  uint16 len ;

  /*@null@*/ /*@dependent@*/
  struct NAMECACHE *next ;

  /*@notnull@*/ /*@owned@*/
  uint8 *clist ;
  uint16 operatorclass ;
  int16 namenumber ;

  /*@null@*/ /*@dependent@*/
  struct OBJECT *dictobj ;

  /*@null@*/ /*@dependent@*/
  struct OBJECT *dictval ;

  /*@null@*/ /*@dependent@*/
  struct NAMECACHE *dictsid ;

  /*@null@*/ /*@dependent@*/
  struct OBJECT *dictcpy ;      /* Saved copy of dictobj */

#if defined( NAMECACHE_STATS )
  /* Statistics gathering counters - useful when a piece of PS is running
   * slowly and a profile shows it's spending lots of time in fast_extract_hash.
   */
  uint32 hit_shallow ;
  uint32 hit_deep ;
  uint32 miss_shallow ;
  uint32 miss_deep ;
#endif
} ;


/* On NAME objects. */
#define theISaveLevel(val)      ((val)->sid)
#define theNLen(val)            ((val).len)
#define theINLen(val)           ((val)->len)
#define theICList(val)          ((val)->clist)
#define theOpClass(val)         ((val).operatorclass)
#define theIOpClass(val)        ((val)->operatorclass)
#define theINameNumber(val)     ((val)->namenumber)


struct OBJECT {
  union d0_s {
    struct bits_s {
      uint8 tags ;
      int8 mark ;   /* Must be signed - AC */
      uint16 len ;
    } bits ;
    uint32 transfer ;              /* must be unsigned - PTH */
  } _d0 ;
  union d1_s {
    union vals_s {
      void *              object_d1_initialiser ;
      /* NB: object_d1_initialiser MUST BE FIRST; ANSI permits static
       * initialisation of unions via the first element. Some compilers
       * insist that this be addressy flavoured if it is to be initialised to
       * something relocatable, so having a special field, of void *, just
       * for this purpose is cleanest. See task 8014. Don't ever use this arm
       * of the union, never let its name be said.
       */
      int32               integer  ;
      USERVALUE           real     ;
      int32               bool     ;
      int32               fid      ;
      /*@notnull@*/ /*@dependent@*/
      struct OPERATOR     *theop   ;
      /*@notnull@*/ /*@dependent@*/
      uint8               *clist   ;
      /*@notnull@*/ /*@dependent@*/
      struct OBJECT       *alist   ;
      /*@notnull@*/ /*@dependent@*/
      struct OBJECT       *dict    ;
      /*@notnull@*/ /*@dependent@*/
      struct DPAIR        *plist   ;
      /*@notnull@*/ /*@dependent@*/
      struct NAMECACHE       *name    ;
      /*@notnull@*/ /*@dependent@*/
      struct FILELIST     *files   ;
      /*@notnull@*/ /*@dependent@*/
      struct SAVELIST     *save    ;
      /*@notnull@*/ /*@dependent@*/
      struct GSTATE       *pgstate ;
      /*@notnull@*/ /*@dependent@*/
      int32               *other   ;
      int32               xrefid   ;
      /*@notnull@*/ /*@dependent@*/
      struct stream       *stream  ;
      /*@notnull@*/ /*@dependent@*/
      struct LONGSTR      *lstr    ;
    } vals ;
    uintptr_t transfer ;             /* must be unsigned - PTH */
  } _d1 ;
} ;


/* EXTERNAL DEFINITIONS */

/* MACROS DEFINITIONS */

/* Macros for moving an object about. */
#define OBJECT_SET_D0(_obj,_val) (((_obj)._d0).transfer = (_val))
#define OBJECT_GET_D0(_obj) (((_obj)._d0).transfer)
#define OBJECT_GET_D1(_obj) (( (OBJECT_32BIT & (1 << oType(_obj)) ) != 0) ? \
                             ((_obj)._d1).transfer & MAXUINT32 :        \
                             ((_obj)._d1).transfer)
#define OBJECT_SET_D1(_obj,_val) (((_obj)._d1).transfer = (_val))

#if defined(ASSERT_BUILD) || defined(DEBUG_BUILD)
#if defined(PLATFORM_IS_64BIT)
#define OBJECT_SCRIBBLE_D1(_obj) (((_obj)._d1).transfer = 0xdeadbeefdeadbeefull)
#else
#define OBJECT_SCRIBBLE_D1(_obj) (((_obj)._d1).transfer = 0xdeadbeefu)
#endif
#else
#define OBJECT_SCRIBBLE_D1(_obj) EMPTY_STATEMENT()
#endif

#define NOTVMOBJECT(_o)       ((theMark(_o)&VMMASK)==ISNOTVM)

#define GLMODE_SAVELEVEL(_gl, _context)                                 \
  (((_gl) && (_context)->savelevel > SAVELEVELINC) ? SAVEMASK : (_context)->savelevel)

#define SETSLOTSAVED(_o, _gl, _context) MACRO_START              \
  HQASSERT(!NOTVMOBJECT(_o), "Saving non-VM object" ) ;          \
  theMark(_o) = (int8)((theMark(_o) & ~SAVEMASK) | GLMODE_SAVELEVEL(_gl, (_context))) ; \
MACRO_END

/* ACCESSOR MACRO DEFINITIONS */

#define theTags(_obj)    (((_obj)._d0).bits.tags) /**< Extract object tags. */
#define theLen(_obj)     (((_obj)._d0).bits.len)  /**< Extract object length. */
#define theILen(_obj)    (((_obj)->_d0).bits.len)
#define theMark(_obj)    (((_obj)._d0).bits.mark) /**< Extract object mark. */

/* Direct object accessors; straight from the object, no indirect versions. */
#define oType(_obj)       (theTags(_obj)&ETYPEMASK) /**< Get object type. */
#define oAccess(_obj)     (theTags(_obj)&ACCEMASK)  /**< Get object permissions. */
#define oExec(_obj)       (theTags(_obj)&EXECMASK)  /**< Get object executability. */

#define oInteger(_obj)      ((_obj)._d1.vals.integer) /**< Integer object value. */
#define oReal(_obj)         ((_obj)._d1.vals.real)    /**< Real object value. */
#define oBool(_obj)         ((_obj)._d1.vals.boolean) /**< Boolean object value. */
#define oFid(_obj)          ((_obj)._d1.vals.fid)     /**< Font ID object value. */
#define oOp(_obj)           ((_obj)._d1.vals.theop)   /**< Operator object pointer. */
#define oString(_obj)       ((_obj)._d1.vals.clist)   /**< String object value. */
#define oArray(_obj)        ((_obj)._d1.vals.alist)   /**< Array object value. */
#define oDict(_obj)         ((_obj)._d1.vals.dict)    /**< Dictionary object value. */
#define oDPList(_obj)       ((_obj)._d1.vals.plist)
#define oName(_obj)         ((_obj)._d1.vals.name)    /**< Name object pointer. */
#define oFile(_obj)         ((_obj)._d1.vals.files)   /**< File object pointer. */
#define oSave(_obj)         ((_obj)._d1.vals.save)    /**< Save object value. */
#define oGState(_obj)       ((_obj)._d1.vals.pgstate) /**< Gstate object pointer. */
#define oOther(_obj)        ((_obj)._d1.vals.other)   /**< Special object pointer. */
#define oXRefID(_obj)       ((_obj)._d1.vals.xrefid)  /**< Indirect object value. */
#define oStream(_obj)       ((_obj)._d1.vals.stream)  /**< Stream object value. */
#define oLongStr(_obj)      ((_obj)._d1.vals.lstr)    /**< Longstring object pointer. */


#define OCopy(dest_, src_) MACRO_START                            \
  int8 _mark_ = (int8)(theMark(dest_) & ~GLOBMASK) ;              \
  (dest_) = (src_) ;                                              \
  theMark(dest_) = (int8)(_mark_ | (theMark(dest_) & GLOBMASK)) ; \
MACRO_END

#define Copy(dest_, src_) MACRO_START                             \
  register OBJECT *_dest_ = (dest_) ;                             \
  register const OBJECT *_src_ = (src_) ;                         \
  int8 _mark_ = (int8)(theMark(*_dest_) & ~GLOBMASK) ;            \
  *_dest_ = *_src_ ;                                              \
  theMark(*_dest_) = (int8)(_mark_ | (theMark(*_dest_) & GLOBMASK)) ; \
MACRO_END


#define OBJECT_NOTVM_NULL \
  {{{ONULL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0}}, {{NULL}}}


extern OBJECT onull ;    /**< Static null object. */


#endif /* protection for multiple inclusion */
