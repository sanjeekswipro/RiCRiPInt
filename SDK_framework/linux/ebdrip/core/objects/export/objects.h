/** \file
 * \ingroup objects
 *
 * $HopeName: COREobjects!export:objects.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1987-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Exposed definitions for core RIP object subsystem.
 *
 * Core (RIP) object subsystem. The object subsystem defines the base object
 * system for PostScript and PDF. It is in a separate compound because it
 * provides a generally useful structured data system, including hash table
 * functions, arrays, etc, and so that non-PostScript products do not to
 * include SWv20.
 *
 * If you do not need to access the internal definitions of the object
 * structure, include objecth.h instead.
 */

#ifndef __OBJECTS_H__
#define __OBJECTS_H__

#include "objectt.h"

#include "coretypes.h" /* Until core is included in everything */

/**
 * \defgroup objects Object subsystem.
 * \ingroup core
 */
/** \{ */

/* Tags for GC'ed structures referred to from OBJECT. */

typedef int8 TypeTag;  /**< \brief GC tag type definition. */

enum { /* avoid 0, for error detection */
  tag_NCACHE = 1, /**< Name cache entry GC tag. */
  tag_FILELIST,   /**< File structure GC tag. */
  tag_GSTATE,     /**< Gstate structure GC tag. */
  tag_LIMIT       /**< This is the limit of tag values, not a real tag. */
};

/* The tag is in the same place in all types, that's the point of it. */
struct generic_typed_object {
  TypeTag typetag;
  /* other stuff */
};

/* ----------------------------------------------------------------------------
   Define PostScript OBJECTs
---------------------------------------------------------------------------- */


/* CONSTANT DEFINITIONS */

/* NOTE: The values are important, since we are dealing with bit-values. */

enum { /* bit-mask values */
  CANEXEC    = 0x20,  /**< Object tag permissions minimum value for exec. */
  CANREAD    = 0x40,  /**< Object tag permissions minimum value for read. */
  CANWRITE   = 0x60   /**< Object tag permissions minimum value for write. */
} ;

enum { /* bit-mask values */
  ETYPEMASK  = 0x1F,  /**< Object tags type mask. */
  ACCEMASK   = 0x60,  /**< Object tags permissions mask. */
  EXECMASK   = 0x80   /**< Object tags executability mask. */
} ;

/* If you change the order or insert new object types, you MUST:
 *
 * 1) Check that the order and offset of the "type" names (e.g. integertype,
 * realtype) in SW20!src:names.nam is the same as the new object type order.
 *
 * 2) Check that the definition of isSRCompObj() below is correct, and
 * identifies all objects subject to save/restore at any level (including
 * the server loop restore).
 *
 * 3) Check that the definition of isPSCompObj() below is correct, and
 * includes all objects that have PSVM allocated memory associated with the
 * value of the object.
 *
 * 4) Check that the order and definition of the types in
 * SWcore!testsrc:swaddin.cpp is consistent with this list.
 *
 * 5) Check that the order and definition of the types, tag, and mark bits in
 * MMsrc!fmtpscon.h is consistent with this list.
 */
enum {
/* Used to mark an empty place in a dictionary + operator on execution stack */
  ONOTHING      = 0x00,   /**< Empty place in dict, internal op on exec stack. */

  OINTEGER      = 0x01,   /**< Integer type. */
  OREAL         = 0x02,   /**< Real number type. */
  OINFINITY     = 0x03,   /**< Out of range number type. */

  OOPERATOR     = 0x04,   /**< Operator type. */
  OBOOLEAN      = 0x05,   /**< Boolean type. */
  OMARK         = 0x06,   /**< Mark type, or stopped mark on exec stack. */
  ONULL         = 0x07,   /**< Null type, or pathforall on exec stack. */
  OFONTID       = 0x08,   /**< FID type. Should not appear on exec stack. */
  ONAME         = 0x09,   /**< Name type. */
  OINDIRECT     = 0x13,   /**< PDF indirect reference. */
  OINVALID      = 0x14,   /**< Invalid type, scribbled over freed objs. */
  OFILEOFFSET   = 0x15,   /**< A 48-bit integer with the top 16 bits saved
                               in the .len part of the object structure. */
  OCPOINTER     = 0x16,   /* A 32 or 64 bit C pointer depending on
                             platform. This object type should be used
                             only when absolutely necessary. It allows
                             the safe transfer of a C pointer through
                             an OBJECT. This should never be used from
                             PS, although is obviously used to pass C
                             pointers through PS objects. */

/* NOTE THAT THIS NUMBER REPRESENTS COMPOSITE OBJECTS AND ABOVE */
  OCOMPOSITE    = 0x17,   /* The first composite object type */

  OSAVE         = 0x17,   /**< Save type - marks a bgsave on execution stack. */
  OEXTENDED     = 0x18,   /**< Virtual type in len. See virtual types below. */
  ODICTIONARY   = 0x19,   /**< Dictionary type. */
  OSTRING       = 0x1A,   /**< String type. */
  OFILE         = 0x1B,   /**< File type. */

  OARRAY        = 0x1C,   /**< Array type. */
  OPACKEDARRAY  = 0x1D,   /**< Packed array type. Exec-beware possible clashes. */

/* The following objects should not appear on the execution stack. */
  OGSTATE       = 0x1E,   /**< Gstate object. */

  OLONGSTRING   = 0x1F,    /**< String len>65535, len + clist in LONGSTR
                           * struct in value field OBJECT. Most PS
                           * operators won't accept this type...
                           */

/* The following are virtual types, encoded by the len field of an OEXTENDED */
  OVIRTUAL      = 0x20,    /* The first virtual object type */

  OLONGARRAY    = 0x20,    /**< Array len>65535. Points to 2 OBJECT trampoline:
                                An OINTEGER (len=0) containing the length and
                                An OARRAY (len=0) pointing to the first entry */
  OLONGPACKEDARRAY = 0x21, /**< Packed version of above, probably not needed. */

  OBADTYPE      = 0x22     /* first illegal extended type number */
} ;

/** oXType(object) returns the extended (virtual) object type */
#define oXType(_obj) ((theTags(_obj)&ETYPEMASK) == OEXTENDED ? \
                      theLen(_obj) : (theTags(_obj)&ETYPEMASK))

/** theXLen(object) returns the extended length */
#define theXLen(_obj) ((theTags(_obj)&ETYPEMASK) == OEXTENDED ? \
                       oInteger(*oArray(_obj)) : theLen(_obj))

/* There used to be some OTMP... types here and a OSHORTNAME type.
 * The idea of the OTMP... types was to help with GC by setting
 * initial types to OTMP... types and changing them to normal types
 * on operators like dup et al. An operator like the image operator
 * when given an OTMP... type could then free the memory. The idea
 * of the OSHORTNAME type was to represent a short name of less than
 * 4 bytes in the pointer field itself. Therefore to access the chars
 * of a name one would have to check the length field first.
 * Removed now since not used. Comment left in in case ever useful.
 */

enum { /* bit-mask values */
  NO_ACCESS     = 0x00, /**< Object tag permissions value for no access. */
  EXECUTE_ONLY  = 0x20, /**< Object tag permissions value for exec access. */
  READ_ONLY     = 0x40, /**< Object tag permissions value for read access. */
  UNLIMITED     = 0x60  /**< Object tag permissions value for all access. */
} ;

enum { /* bit-mask values */
  LITERAL       = 0x00,  /**< Literal flag value for object tags. */
  EXECUTABLE    = 0x80   /**< Executable flag value for object tags. */
} ;

#define ISAFONT       (0x80)  /**< Dictionary object tags flag for fonts. */

#define MAXPSSTRING     65535 /**< Maximum length of a PostScript string. */
#define MAXPSARRAY      65535 /**< Maximum length of a PostScript array. */
#define MAXPSDICT       65535 /**< Maximum length of a PostScript dictionary. */
#define MAXPSNAME       256  /**< Maximum length of a PostScript name. */

#define MAXINTERN       65535 /**< Maximum length of an interned string. */

/** The numeric value substitued for OINFINITY (i.e. that returned by
 * object_get_numeric() for an OINFINITY object). This is outside the
 * representable range of USERVALUE. */
#define OINFINITY_VALUE ((SYSTEMVALUE) 2.0E+100)

/** Sub NULL OBJECT types: */
enum {
  ISNORMALNULL = 0,   /**< As created by OBJECT_NOTVM_NULL, just an ONULL */
  /* The remainder will only be seen on the execution stack: */
  ISPATHFORALL,       /**< pathforall identifier. */
  ISINTERPRETEREXIT,  /**< exit identifier. (previously equal to zero) */
  ISPROCEDURE         /**< for SWaddin's benefit, points to the name of the proc being executed. */
} ;

#define GLOBMASK           (int8)0x01 /**< Mask to extract globalness from object mark. */
#define ISLOCAL             (int8)0x00 /**< Local allocation flag for object mark. */
#define ISGLOBAL            (int8)0x01 /**< Global allocation flag for object mark. */

#define SAVEMASK           (int8)0x3e /**< Mask to extract savelevel from object mark. */

#define VMMASK             (int8)0x40 /**< Mask to extract VM type from objects. */
#define ISNOTVM             (int8)0x00 /**< Flag to mark non-VM objects. */
#define ISPSVM              (int8)0x40 /**< Flag to mark PSVM objects. */

/* STRUCTURE DEFINITIONS */

/* operator class flags for the namecache entries. They are also defined in
 * tools:nametool.c.
 */
                            /* Can have up to 7 levels. */
enum { /* Bit mask values */
  LEVEL1OP           = 0x0001, /**< Operator is a level 1 one. */
  LEVEL2OP           = 0x0002, /**< Operator is a level 2 one. */
  LEVEL3OP           = 0x0003, /**< Operator is a level 3 one. */
  LEVEL4OP           = 0x0004, /**< Operator is a level 4 one...one day... */
  LEVELMASK          = 0x0007, /**< Mask for operator level. */

  COLOREXTOP         = 0x0008, /**< Operator is a colour extension operator. */
  SHADOWOP           = 0x0010, /**< Operator is shadowed, see miscops.c. */
  CANNOTSHADOWOP     = 0x0020, /**< Disallow operator to be shadowed. */
  SEPARATIONDETECTOP = 0x0040, /**< Operator used for seps detection. */
  FUNCTIONOP         = 0x0080, /**< Can be used as PDF+ encapsulated function. */
  RECOMBINEDETECTOP  = 0x0100, /**< Used for recombine color equivalents. */
  NAMEOVERRIDEOP   = 0x0200  /**< Name is part of a nameoverrides set. */
} ;

/* Along with SHADOWOP, def_() also acts on these flags: */
#define OTHERDEFOP (SEPARATIONDETECTOP | RECOMBINEDETECTOP | NAMEOVERRIDEOP)

enum {
  NCFLAG_BREAK = 1            /**< Break on this executable name */
} ;

/** Name cache entries. */
struct NAMECACHE {
  TypeTag typetag ;           /**< GC type tag. Must be first field. */
  uint8 sid ;                 /**< Save level. */
  uint16 len ;                /**< Length of name. */

  /*@null@*/ /*@dependent@*/
  struct NAMECACHE *next ;    /**< Next name cache entry. */

  /*@notnull@*/ /*@owned@*/
  uint8 *clist ;              /**< Name storage. */
  uint16 operatorclass ;      /** Operator class flags. */
  int16 namenumber ;          /**< \c system_names index for pre-defined names. */

  /*@null@*/ /*@dependent@*/
  struct OBJECT *dictobj ;    /**< Dictionary where uniquely defined. */

  /*@null@*/ /*@dependent@*/
  struct OBJECT *dictval ;    /**< Slot where uniquely defined. */

  /*@null@*/ /*@dependent@*/
  struct NAMECACHE *dictsid ; /**< Next in list of names created at this save. */

  /*@null@*/ /*@dependent@*/
  struct OBJECT *dictcpy ;    /**< Saved copy of dictobj. */

#if defined( NAMECACHE_STATS )
  /* Statistics gathering counters - useful when a piece of PS is running
   * slowly and a profile shows it's spending lots of time in fast_extract_hash.
   */
  uint32 hit_shallow ;
  uint32 hit_deep ;
  uint32 miss_shallow ;
  uint32 miss_deep ;
#endif
#if defined( DEBUG_BUILD ) || defined( ASSERT_BUILD )
  /* [51291] executable name breakpoint flag */
  uint32 flags ;              /**< Flags, including NCFLAG_BREAK. */
#endif
} ;

/** The systems name table contains all pre-defined names. The indices into
    this array are the same regardless of RIP variant. */
extern NAMECACHE system_names[] ;

/* On NAME objects. */
#define theISaveLevel(val)      ((val)->sid)
#define theINLen(val)           ((val)->len)
#define theICList(val)          ((val)->clist)
#define theIOpClass(val)        ((val)->operatorclass)
#define theINameNumber(val)     ((val)->namenumber)

/** Operator function type. */
typedef Bool (* OPFUNCTION)(ps_context_t *context);

/** Operator function and name association. */
typedef struct OPERATOR {
  OPFUNCTION    opcall  ;    /**< Operator function pointer. */
  struct NAMECACHE *opname ; /**< Operator name cache entry. */
} OPERATOR ;

/** The operator table contains all operators. */
extern OPERATOR system_ops[] ;

/* Access macros on OPERATOR objects. */
#define theIOpCall(val)       ((val)->opcall)
#define theIOpName(val)       ((val)->opname)

/** Longstring can store more than 65535 bytes in a string; it's only used by
    a few PS operators. */
typedef struct LONGSTR {
  int32 len ;    /**< Long string length. */
  uint8 *clist ; /**< Long string storage. */
} LONGSTR ;

/** Object structure. Object fields are overlaid to fit objects in 8 bytes. */
struct OBJECT {
  union d0_s {
    struct bits_s {
      uint8 tags ;  /**< Object type, permissions, executability. */
      int8 mark ;   /**< Globalness and save level. Must be signed. */
      uint16 len ;  /**< String/array/dict length or PDF indirect generation. */
    } bits ;
    uint32 transfer ;            /* must be unsigned - PTH */
  } _d0 ;
  union d1_s {
    union vals_s {
      /*@null@*/
      void *              object_d1_initialiser ;
      /* NB: object_d1_initialiser MUST BE FIRST; ANSI permits static
       * initialisation of unions via the first element. Some compilers
       * insist that this be addressy flavoured if it is to be initialised to
       * something relocatable, so having a special field, of void *, just
       * for this purpose is cleanest. See task 8014. Don't ever use this arm
       * of the union, never let its name be said.
       */
      int32               integer  ; /**< Integer value. */
      USERVALUE           real     ; /**< Real value. */
      int32               boolean  ; /**< Boolean value. */
      int32               fid      ; /**< Font ID value. */
      /*@notnull@*/ /*@dependent@*/
      struct OPERATOR     *theop   ; /**< Operator value. */
      /*@notnull@*/ /*@dependent@*/
      uint8               *clist   ; /**< String value. */
      /*@notnull@*/ /*@dependent@*/
      struct OBJECT       *alist   ; /**< Array value. */
      /*@notnull@*/ /*@dependent@*/
      struct OBJECT       *dict    ; /**< Dictionary value. */
      /*@notnull@*/ /*@dependent@*/
      struct DPAIR        *plist   ; /**< Dictionary internal pair value. */
      /*@notnull@*/ /*@dependent@*/
      struct NAMECACHE    *name    ; /**< Name value. */
      /*@notnull@*/ /*@dependent@*/
      struct FILELIST     *files   ; /**< File value. */
      /*@notnull@*/ /*@dependent@*/
      struct SAVELIST     *save    ; /**< Save value. */
      /*@notnull@*/ /*@dependent@*/
      struct GSTATE       *pgstate ; /**< Gstate value. */
      /*@notnull@*/ /*@dependent@*/
      int32               *other   ; /**< Special purpose pointer value. */
      int32               xrefid   ; /**< PDF indirect value. */
      /*@notnull@*/ /*@dependent@*/
      struct stream       *stream  ; /**< PDF stream value. */
      /*@notnull@*/ /*@dependent@*/
      struct LONGSTR      *lstr    ; /**< Long string value. */
      uint32              fileoffset_low ; /**< lower 32-bit value of
                                                a file offset.  (top
                                                int16 in .len) */
      /*@notnull@*/ /*@dependent@*/
      void *              cptr    ;  /**< C pointer. Used for internal
                                        passing of C pointers via
                                        PS. */
    } vals ;
    uintptr_t transfer ;             /* must be unsigned - PTH */
  } _d1 ;
} ;

/** A bitmask with a bit set for each OBJECT type that has a 32bit value
    rather than a pointer in the above structure. */

#define OBJECT_32BIT (1<<OINTEGER | 1<<OREAL | 1<<OBOOLEAN | 1<<OFONTID | \
                      1<<OINDIRECT | 1<<OFILEOFFSET)

/** Dictionary pairs are used as the internal values for dictionary storage.
    The oDict() pointer from a top-level dictionary object points to the
    second object in an even-length array of objects. This object contains
    the length of the remaining objects in the array, and a pointer to the
    next object in the array. The object before this is an extension
    dictionary object; if it is an ODICTIONARY, it points into another array
    of objects, which may point to further arrays recursively. The remaining
    objects in the array are treated as key/value pairs. */
typedef struct DPAIR {
  OBJECT key ; /**< Dictionary key; ONOTHING for an empty slot. */
  OBJECT obj ; /**< Dictionary value; ONOTHING for an empty slot. */
} DPAIR ;

/** Save/restore info: namepurges is internal to COREobjects, but is saved.
    This structure captures the information saved by the object subsystem for
    each savelevel. */
struct OBJECTSAVE {
  int32 glallocmode ;
  NAMECACHE *namepurges ;
} ;

/****************************************************************************/

/* Now that we've defined the sizes of objects, include the files which
   manipulate them. This is done after the OBJECT definition now so that this
   file can be included in extern "C" blocks of C++ files. */
#include "objecth.h"

/****************************************************************************/

/* MACRO DEFINITIONS */

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

/* Checking OBJECTS */
#define tagsType(_tags)       ((_tags)&ETYPEMASK) /**< Object type from tags. */
#define tagsAccess(_tags)     ((_tags)&ACCEMASK)  /**< Object permissions from tags. */
#define tagsExec(_tags)       ((_tags)&EXECMASK)  /**< Object executability from tags. */

#define isSRCompObj(_o)      (oType(_o)==ONAME || isPSCompObj(_o))  /**< Is object subject to save/restore? */
#define isPSCompObj(_o)      (oType(_o)>=OSAVE)  /**< Is object composite? */

#define oGlobalValue(_o)     (theMark(_o)&GLOBMASK) /**< Is composite object's value in global memory? */

/** Reset only the globalness flag of an object. */
#define SETGLOBJECTTO(_o,_gl) MACRO_START \
  HQASSERT(BOOL_IS_VALID(_gl), "Global flag is not boolean" ) ; \
  theMark(_o) = (int8)((theMark(_o) & ~GLOBMASK) | (_gl)) ; \
MACRO_END

/** Set object to global/local and unsaved by current allocation mode. */
#define SETGLOBJECT(_o, _context) SETGLOBJECTTO(_o, (_context)->glallocmode)

/** Test if object slot requires saving. */
#define SLOTISNOTSAVED(_o, _context) ((theMark(*(_o))&SAVEMASK)<(_context)->savelevel)

/** Test if object isn't in PS VM. */
#define NOTVMOBJECT(_o)       ((theMark(_o)&VMMASK)==ISNOTVM)

/** Value with which to mark non-VM dictionaries. */
#define ISNOTVMDICTMARK(_savelevel) (int8)(ISLOCAL | ISNOTVM | (_savelevel))

/** Return save level appropriate for a specified global allocation mode. */
#define GLMODE_SAVELEVEL(_gl, _context)                                 \
  (((_gl) && (_context)->savelevel > SAVELEVELINC) ? SAVEMASK : (_context)->savelevel)

/** Mark an object slot as saved, using an explicit allocation mode from the
    slot's container. This should only be used by the save/restore
    machinery. */
#define SETSLOTSAVED(_o, _gl, _context) MACRO_START              \
  HQASSERT(BOOL_IS_VALID(_gl), "Global flag is not boolean" ) ;  \
  HQASSERT(!NOTVMOBJECT(_o), "Saving non-VM object" ) ;          \
  theMark(_o) = (int8)((theMark(_o) & ~SAVEMASK) | GLMODE_SAVELEVEL(_gl, (_context))) ; \
MACRO_END

/** Test if gstate requires saving. */
#define GSTATEISNOTSAVED(_gs, _context) \
  ((_gs)->slevel < (_context)->savelevel && (_gs)->saved)

/** Mark gstate as saved with explicit global allocation mode. */
#define SETGSTATESAVED(_gs, _gl, _context) MACRO_START             \
  HQASSERT(BOOL_IS_VALID(_gl), "Global flag is not boolean" ) ;  \
  (_gs)->slevel = GLMODE_SAVELEVEL(_gl, (_context)) ;            \
  (_gs)->saved = TRUE ;                                          \
MACRO_END

/** Mark gstate as saved with current global allocation mode. */
#define NEWGSTATESAVED(_gs, _context) \
  SETGSTATESAVED((_gs), (_context)->glallocmode, (_context))

/** Test if it would be illegal to put a local object into this object. */
#define illegalLocalIntoGlobal(_o, _context)                            \
  (isPSCompObj(*(_o)) && oGlobalValue(*(_o)) == ISLOCAL && \
   (_context)->savelevel != 0)

/** Test if two objects are identical, except for the slot properties. */
#define OBJECTS_IDENTICAL(o1_, o2_) \
  (((OBJECT_GET_D0(o1_) ^ OBJECT_GET_D0(o2_)) & ~(VMMASK|SAVEMASK)) == 0 && \
   OBJECT_GET_D1(o1_) == OBJECT_GET_D1(o2_))

/****************************************************************************/

/* ACCESSOR MACRO DEFINITIONS */

#define theTags(_obj)    (((_obj)._d0).bits.tags) /**< Extract object tags. */
#define theITags(_obj)   (((_obj)->_d0).bits.tags) /**< \deprecated Extract object tags. */
#define theLen(_obj)     (((_obj)._d0).bits.len)  /**< Extract object length. */
#define theILen(_obj)    (((_obj)->_d0).bits.len) /**< \deprecated Extract object length. */
#define theMark(_obj)    (((_obj)._d0).bits.mark) /**< Extract object mark. */

/* For an xref object the length contains the generation number. */
#define theGen(_obj)     (((_obj)._d0).bits.len) /**< Get indirect generation. */
#define theIGen(_obj)    (((_obj)->_d0).bits.len) /**< \deprecated Get indirect generation. */

/* Direct object accessors; straight from the object, no indirect versions. */
#define oType(_obj)       (theTags(_obj)&ETYPEMASK) /**< Get object type. */
#define oAccess(_obj)     (theTags(_obj)&ACCEMASK)  /**< Get object permissions. */
#define oExec(_obj)       (theTags(_obj)&EXECMASK)  /**< Get object executability. */

#define oCanWrite(_obj)   ((theTags(_obj)&ACCEMASK)>=CANWRITE) /**< Do object permissions allow writing? */
#define oCanRead(_obj)    ((theTags(_obj)&ACCEMASK)>=CANREAD) /**< Do object permissions allow reading? */
#define oCanExec(_obj)    ((theTags(_obj)&ACCEMASK)>=CANEXEC) /**< Do object permissions allow execution? */

#define oExecutable(_obj)   (theTags(_obj)&EXECMASK)  /**< Is this an executable object? */

#define oInteger(_obj)      ((_obj)._d1.vals.integer) /**< Integer object value. */
#define oReal(_obj)         ((_obj)._d1.vals.real)    /**< Real object value. */
#define oBool(_obj)         ((_obj)._d1.vals.boolean) /**< Boolean object value. */
#define oFid(_obj)          ((_obj)._d1.vals.fid)     /**< Font ID object value. */
#define oOp(_obj)           ((_obj)._d1.vals.theop)   /**< Operator object pointer. */
#define oString(_obj)       ((_obj)._d1.vals.clist)   /**< String object value. */
#define oArray(_obj)        ((_obj)._d1.vals.alist)   /**< Array object value. */
#define oDict(_obj)         ((_obj)._d1.vals.dict)    /**< Dictionary object value. */
#define oName(_obj)         ((_obj)._d1.vals.name)    /**< Name object pointer. */
#define oFile(_obj)         ((_obj)._d1.vals.files)   /**< File object pointer. */
#define oSave(_obj)         ((_obj)._d1.vals.save)    /**< Save object value. */
#define oGState(_obj)       ((_obj)._d1.vals.pgstate) /**< Gstate object pointer. */
#define oOther(_obj)        ((_obj)._d1.vals.other)   /**< Special object pointer. */
#define oXRefID(_obj)       ((_obj)._d1.vals.xrefid)  /**< Indirect object value. */
#define oStream(_obj)       ((_obj)._d1.vals.stream)  /**< Stream object value. */
#define oLongStr(_obj)      ((_obj)._d1.vals.lstr)    /**< Longstring object pointer. */
#define oCPointer(_obj)     ((_obj)._d1.vals.cptr)    /**< C structure pointer. */

/* Extended values - no type checking (these are not 'X' macros) */
/** The length of a LongArray object */
#define oLongArrayLen(_obj) (((_obj)._d1.vals.alist)->_d1.vals.integer)
/** The array pointer of a LongArray object */
#define oLongArray(_obj)    (((_obj)._d1.vals.alist+1)->_d1.vals.alist)

/* Big Integer currently implemented as an Hq32x2 */
#define FileOffsetToHq32x2(_val,_obj)  ( (_val).low = (_obj)._d1.vals.fileoffset_low, (_val).high = ((int32)theLen(_obj)) )
#define Hq32x2ToFileOffset(_obj,_val)  ( (_obj)._d1.vals.fileoffset_low = (_val).low, theLen(_obj) = ((uint16)(_val).high))

/* Common sub-object accessors. */

/** Get \c system_names index, or -1 if not pre-defined. */
#define oNameNumber(_obj)   theINameNumber(oName(_obj))

/* On DICTIONARY-PAIR objects. */
#define theIKey(val)    ((val)->key) /**< Get dictionary pair key. */
#define theIObject(val) ((val)->obj) /**< Get dictionary pair value. */

/* For longstring support */
#define theLSLen(val)      ((val).len)   /**< Get long string length. */
#define theILSLen(val)     ((val)->len)  /**< \deprecated Get long string length */
#define theLSCList(val)    ((val).clist) /**< Get long string storage. */
#define theILSCList(val)   ((val)->clist)  /**< \deprecated Get long string storage */


/* copying objects */

/** Copy objects directly. Either this macro or Copy() should be used for
    all copying between different types of memory (e.g. between PSVM and non
    PSVM), and also between PSVM slots which may have different savelevels
    (i.e., all PSVM). The slot's VM type and savelevel are preserved, the
    global mode comes from the source object. This macro is coded to only
    evaluate the source object once. */
#define OCopy(dest_, src_) MACRO_START                            \
  int8 _mark_ = (int8)(theMark(dest_) & ~GLOBMASK) ;              \
  (dest_) = (src_) ;                                              \
  theMark(dest_) = (int8)(_mark_ | (theMark(dest_) & GLOBMASK)) ; \
MACRO_END

/** Copy objects through pointers. Either this macro or OCopy() should be
    used for all copying between different types of memory (e.g. between PSVM
    and non PSVM), and also between PSVM slots which may have different
    savelevels (i.e., all PSVM). The slot's VM type and savelevel are
    preserved, the global mode comes from the source object. This macro is
    coded to only evaluate the arguments once, so object_slot_notvm() can
    be used in the source argument. */
#define Copy(dest_, src_) MACRO_START                             \
  register OBJECT *_dest_ = (dest_) ;                             \
  register const OBJECT *_src_ = (src_) ;                         \
  int8 _mark_ = (int8)(theMark(*_dest_) & ~GLOBMASK) ;            \
  *_dest_ = *_src_ ;                                              \
  theMark(*_dest_) = (int8)(_mark_ | (theMark(*_dest_) & GLOBMASK)) ; \
MACRO_END

/****************************************************************************/

/** Static or auto initialiser for a nothing non-PSVM object. Use this to
    initialise C stack or static objects which will then be completed through
    object_store_* routines. */
#define OBJECT_NOTVM_NOTHING \
  {{{ONOTHING|LITERAL|NO_ACCESS, ISNOTVM|ISLOCAL|SAVEMASK, 0}}, {{NULL}}}

/** Static or auto initialiser for a null non-PSVM object. */
#define OBJECT_NOTVM_NULL \
  {{{ONULL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0}}, {{NULL}}}

/** Static or auto initialiser for a mark non-PSVM object. */
#define OBJECT_NOTVM_MARK \
  {{{OMARK|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0}}, {{NULL}}}

/** Static or auto initialiser for an out of range non-PSVM number object. */
#define OBJECT_NOTVM_INFINITY \
  {{{OINFINITY|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0}}, {{NULL}}}

/** Static or auto initialiser for a boolean non-PSVM object. */
#define OBJECT_NOTVM_BOOLEAN(b_)  \
  {{{OBOOLEAN|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0}}, {{(void*)((intptr_t)!!((Bool)(b_)))}}}

/** Static or auto initialiser for an integer non-PSVM object. */
#define OBJECT_NOTVM_INTEGER(i_) \
  {{{OINTEGER|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0}}, {{(void*)((intptr_t)((int32)(i_))&MAXUINT32)}}}

/** Static or auto initialiser for a string non-PSVM object. The value MUST
    be a string literal. */
#define OBJECT_NOTVM_STRING(s_) \
  {{{OSTRING|LITERAL|READ_ONLY, ISNOTVM|ISLOCAL|SAVEMASK, (uint16)sizeof("" s_ "") - 1}}, {{(void*)((uint8 *)("" s_ ""))}}}

/** Static or auto initialiser for a name non-PSVM object. The value MUST
    be an object name index (NAME_*). */
#define OBJECT_NOTVM_NAME(n_, litexec_) \
  {{{ONAME|(litexec_), ISNOTVM|ISLOCAL|SAVEMASK, 0}}, {{(void*)&system_names[n_]}}}

/** Static or auto initialiser for a system operator non-PSVM object. The value
    MUST be an object name index (NAME_*). */
#define OBJECT_NOTVM_SYSTEMOP(n_) \
  {{{OOPERATOR|EXECUTABLE, ISNOTVM|ISLOCAL|SAVEMASK, 0}}, {{(void*)&system_ops[n_]}}}

/** Static or auto initialiser for a general operator non-PSVM object.
    The value must be a structure beginning with an OPERATOR structure. */
#define OBJECT_NOTVM_OPERATOR(op_) \
  {{{OOPERATOR|EXECUTABLE, ISNOTVM|ISLOCAL|SAVEMASK, 0}}, {{(void*)(op_)}}}

/** Static or auto initialiser for an array object. Takes the number of
    elements and the address of the first OBJECT. */
#define OBJECT_NOTVM_ARRAY(obj_, n_) \
  {{{OARRAY|LITERAL|READ_ONLY, ISNOTVM|ISLOCAL|SAVEMASK, (n_)}}, {{(void*)(obj_)}}}

/** Static or auto initialiser for an executable array. Takes the number of
    elements and the address of the first OBJECT. */
#define OBJECT_NOTVM_PROC(obj_, n_) \
  {{{OARRAY|EXECUTABLE|READ_ONLY, ISNOTVM|ISLOCAL|SAVEMASK, (n_)}}, {{(void*)(obj_)}}}

/** Static or auto initialiser for a float non-PSVM object. NOTE This can
    only take a predefined constant from the list below. Do NOT use a float
    parameter! e.g. one half:
    \code OBJECT_NOTVM_REAL(OBJECT_0_5F) \endcode */
#define OBJECT_NOTVM_REAL(r_) \
  {{{OREAL|LITERAL, ISNOTVM|ISLOCAL|SAVEMASK, 0}}, {{(void*)((intptr_t)((int32)(r_))&MAXUINT32)}}}

#define OBJECT_1_0F        0x3f800000
#define OBJECT_0_75F       0x3f400000
#define OBJECT_0_5F        0x3f000000
#define OBJECT_0_25F       0x3e800000
#define OBJECT_0_0F        0x0
#define OBJECT_PI_F        0x40490fdb

/****************************************************************************/

/* Dictionary access macros */

/** \brief Dictionary load factor defines how full dicts are allowed to get */
#define DICT_LOAD_FACTOR (0.75)

/** \brief Number of slots in a dictionary holding \c _n entries,
    allowing for load factor.

    Small dictionaries are assumed to have exactly the right size.
 */
#define DICT_ADJUSTED_SLOTS(_n) \
  ((_n) < 20 ? (_n) : (size_t)((_n) / DICT_LOAD_FACTOR))

/** \brief Allocation size (in objects) required to create a dictionary
    capable of holding \c _n entries. */
#define NDICTOBJECTS(_n) (DICT_ADJUSTED_SLOTS(_n) * 2 + 3)

/** \brief Macro to get the current length of a dictionary. */
#define getDictLength(_len, _dict) MACRO_START \
    const OBJECT *_d_; \
    (_len) = 0; \
    _d_ = (_dict); \
    do { \
      _d_ = oDict(*_d_); \
      (_len) += theILen(_d_); \
      --_d_; \
    } while ( oType(*_d_) == ODICTIONARY ) ; \
MACRO_END

/** \brief Macro to get the allocated length of a dictionary. */
#define DICT_ALLOC_LEN(_dictarr) theLen((_dictarr)[-2])

#define DICT_IN_ARRAY(ptr, arr) \
  ((ptr) > (arr) && (ptr) <= (arr) + 1 + 2 * DICT_ALLOC_LEN(arr))

/** \brief Macro to set the access level for a dictionary. */
#define SET_DICT_ACCESS(_pdict, _access) MACRO_START                         \
  register OBJECT * _d_ = (_pdict) ;                                         \
  HQASSERT(((_access) & ~ACCEMASK) == 0, "Access mode is invalid") ;         \
  theTags(*_d_) = CAST_TO_UINT8((theTags(*_d_) & ~ACCEMASK) | (_access)) ;   \
  do {                                                                       \
    _d_ = oDict(*_d_) ;                                                      \
    theTags(*_d_) = CAST_TO_UINT8((theTags(*_d_) & ~ACCEMASK) | (_access)) ; \
    --_d_ ;                                                                  \
  } while ( oType(*_d_) == ODICTIONARY ) ;                                   \
MACRO_END

/*------------------------- STATIC OBJECTS ----------------------------*/

/* Static objects, used to temporarily store new created objects until they
   are stacked, extracted or inserted into dictionaries. */
extern OBJECT tnewobj ;  /**< Static TRUE boolean. */
extern OBJECT fnewobj ;  /**< Static FALSE boolean. */
extern OBJECT inewobj ;  /**< Static integer object. */
extern OBJECT fonewobj ; /**< Static Fileoffset object. */
extern OBJECT rnewobj ;  /**< Static real object. */
extern OBJECT ifnewobj ; /**< Static infinity object. */
extern OBJECT nnewobj ;  /**< Static literal name object */
extern OBJECT nnewobje ; /**< Static executable name object. */
extern OBJECT snewobj ;  /**< Static string object. */
extern OBJECT onothing ; /**< Static nothing object. */
extern OBJECT onull ;    /**< Static null object. */


/****************************************************************************/

/** \page objects Notes on the object subsystem

\section slots Object slots and object values

The definition of the OBJECT structure contains two sets of information,
packed into the same structure. The save level and VM type bits are
properties of the memory in which an OBJECT is stored (the "slot"). The rest
of the bits are properties of the object value itself.

The slot properties are initialised when object memory is allocated, and
should not be changed afterwards, except by the save/restore system. The
easiest ways to keep this rule are:

 * Always use Copy() or OCopy() to copy objects.
 * Always initialise OBJECT variables on the C stack with OBJECT_NOTVM_*.
 * Use the object_store_*() routines to construct simple objects in initialised
   slots.
 * Never modify theMark(object) directly.

\subsection vmmask VM type

The VM type of an object location is indicated by a single bit in the
object's mark field, masked by VMMASK, taking either the value ISPSVM or
ISNOTVM. The bit value ISPSVM indicates that the memory location is in a
PostScript memory pool, and is subject to save and restore. ISNOTVM indicates
that the memory is from another dynamic pool, or from the C stack, or
statically allocated from the data segment, and the location is not subject
to save and restore.

The object location's VM type and save level should be set before storing
object values into it. The memory alloction functions for PostScript memory
(get_omemory(), get_gomemory() and get_lomemory()) take care of setting the
ISPSVM bit value. The allocators for other pools from which objects are
taken (e.g. PDF and TIFF memory) take care of setting the ISNOTVM bit value.

When copying object values between different VM types, or between the C
stack or static variables and dynamic allocated memory, the Copy() or OCopy()
macros *must* be used. These macros take care to preserve the slot
properties, and only modifying the value of the object. It is *not* safe to use
direct assignment of OBJECT structures in these cases. If you are unsure
whether it is safe to use direct assignment, use Copy() or OCopy(), they are
always safe to use.

Objects allocated statically or on the C stack need their VM type setting
before values are stored in them. This can be achieved as part of
initialisation by using the OBJECT_NOTVM_* macros, some of which can also
initialise a value for the object as well:

\code
  {
    OBJECT later = OBJECT_NOTVM_NOTHING ; // store value later
    OBJECT again = OBJECT_NOTVM_NULL ; // start with ONULL, modify later
    OBJECT mark = OBJECT_NOTVM_MARK ; // a mark
    OBJECT infinite = OBJECT_NOTVM_INFINITY ; // Way too big
    OBJECT fifteen = OBJECT_NOTVM_INTEGER(15) ; // an integer, with value 15
    OBJECT black = OBJECT_NOTVM_NAME(NAME_Black, LITERAL) ; // /Black
    OBJECT str = OBJECT_NOTVM_STRING("A Literal string") ;
    OBJECT op = OBJECT_NOTVM_OPERATOR(NAME_defineresource) ; // execute this
  }
\endcode

The VM type bit values are arranged so that the normal zero initialisation of
static structures will set the VM type to ISNOTVM, and the object type to
ONOTHING.

Objects which are too cumbersome to use auto initialisers, or are part of a
dynamically allocated structure can use the function object_slot_notvm() to
set the mark correctly, but only before any value is assigned to the object.

\code
  my_struct = mm_alloc(mm_pool_temp, sizeof(my_struct_t), MM_ALLOC_CLASS_MINE) ;
  if ( my_struct == NULL )
    return error_handler(VMERROR) ;

  // initialise with a name
  object_store_name(object_slot_notvm(&my_struct->name_object), NAME_K, LITERAL) ;
  // initialise with a copy of another object
  Copy(object_slot_notvm(&my_struct->other_object), theo) ;
\endcode

The object_slot_notvm() routine returns the address of the object slot passed
to it, so it can be chained with the object_store_* routines to completely
initialise object slots and their values, as shown above.

The VM type is also called the "slot type", to indicate it is a property of
the object's memory slot.

\subsection savelevel Save levels

Save levels are a property of the memory in which an object is stored, and
not the object itself or its value. This property indicates the save level at
which the memory was either last saved, or was originally allocated if it has
not been saved yet. The save level needs to be considered when storing into
the slots of an array OARRAY, or inserting or removing elements from a
dictionary (ODICTIONARY).

The dictionary case is handled by the insert_hash() and remove_hash() variant
functions, and needs no special handling by the caller.

The array case needs special attention by all developers. Before modifying
any values in an array of OBJECTS which might be in PostScript VM, the function
check_asave() *must* be called to save the affected locations.

Object memory allocated through the PostScript get_omemory() routines are
automatically tagged with the appropriate savelevel, according to the global
allocation mode.

Non-VM objects usually have the maximum save level set (SAVEMASK), as a
backstop against leaking into PostScript memory. The PDF object allocator
marks PDF objects with the save level of the PDF execution context in which
the object were allocated. The savelevel is actually irrelevant for non-VM
objects, it is just a convenience to set them to the maximum savelevel.

\subsection init Setting object values

The object_store_* routines will set the object value in an initialised slot
to one of the simple types.

There are a set of static non-PSVM convenience objects provided to help with
creation of objects. These include ONOTHING and ONULL static objects. These
objects can be used with direct assignment to set the slot properties and value
of dynamically allocated OBJECT fields, or OBJECT variables on the C stack:

\code
  my_struct->object = onull ;  // Struct copy to set slot properties
\endcode

When these objects are used, there should always be an accompanying comment
about setting the slot properties, to indicate this is deliberate.

The tags and slot properties of the static objects should *never* be changed.
The values field of the \c inewobj (OINTEGER), \c fonewobj (OFILEOFFSET),
\c rnewobj (OREAL), \c nnewobj (literal ONAME), \c nnewobje (executable ONAME),
and \c snewobj (OSTRING) objects may be changed. The value fields of the other
objects *must not* be changed.


\subsection dictinit Initialising non-VM dictionaries
PostScript dictionaries should be allocated and initialised through the
ps_dictionary() function. Non-VM dictionaries require a little more care,
because the memory in which the dictionary contents are stored is allocated
separately from the dictionary object, and also because the
init_dictionary() call used to do this has special semantics with respect to
the object slot properties.

Non-VM dictionaries are initialised using the function init_dictionary():
\code
  OBJECT dictmem[NDICTOBJECTS(length)] ;
  OBJECT dictobj = OBJECT_NOTVM_NOTHING ;

  init_dictionary(&dictobj, NDICTOBJECTS(length), UNLIMITED, dictmem,
                  ISNOTVMDICTMARK(SAVEMASK)) ;
\endcode

The dictionary object \c dictobj must have its slot properties initialised,
however the slot properties of the dictionary content storage \c dictmem are
initialised by init_dictionary(). The slot properties are bundled into a mark
value by the macro:

\code
  ISNOTVMDICTMARK( save_level ) ;
\endcode

These slot properties *must* match the type of the underlying memory for the
dictionary contents. The value of \c ISNOTVMDICTMARK(SAVEMASK) as shown
above is appropriate for most non-VM dictionaries.

\section glocal Global/local memory and associated macros

\subsection background PostScript object memory

Save/restore only  needs to  be  considered for  OBJECTs that  consume
PSVM-OBJECT memory, i.e. for OBJECTs of type:

 ODICTIONARY
 OARRAY
 OPACKEDARRAY

Global/local memory only needs to be  considered  for all OBJECTs that
consume local/global memory, i.e. (presently) for OBJECTs of type:

 ODICTIONARY     OSTRING         OFILE           OARRAY
 OPACKEDARRAY    OGSTATE         OLONGSTRING

Note: ONAME OBJECTs (currently) always  go  in global memory, and  are
subject to global save/restore.

Note: In a quirk of the PostScript language, the contents of strings are
not subject to save/restore, though the string object itself is. The memory
associated with a PostScript string will be reclaimed when the save level at
which the string was created is restored.

Save/restore for PS OBJECTs works in a lazy style. Each OBJECT that sits in
VM has in its a slot properties a number which indicates the level that the
OBJECT has last been saved at (the OBJECT's savelevel). If a write is made to
an OBJECT, and the savelevel is less than the current active save level
(stored in a C global variable), then we know that a copy of this OBJECT
needs to be taken so that on the matching restore the OBJECT can be
reset. All new PostScript OBJECTs start off with the current active save
level.

Global memory is treated slightly differently from local memory, since
the general save/restore mechanism is only  exercised for save level 0
(our outer-most save level used for rebooting  the RIP) and save level
1 (a job's encapsulation). The special handling of global objects prevents
their values from being saved or restored when the save level is above this
level.

OBJECTs live in either local or global  memory, depending on the allocation
mode at the time the object's memory was allocated. The local/global state
is represented by one bit, masked by GLOBMASK, and taking the values ISGLOBAL
and ISLOCAL. These bit values are arranged so that ISGLOBAL is also the same
value as TRUE, ISLOCAL is FALSE.

The global bit actually indicates if the object's value is local or global,
and not whether the object's memory slot is itself in global memory.
This is partly because we may have an array  in  global  (or local)
memory that is of zero length, and so doesn't take up any VM (so there
is no room to store anything there!).

\subsection newobj Creating new PostScript VM OBJECTs

When creating a new OBJECT, its savelevel and VM type are set by the memory
allocator or initialiser. The local/global attribute is initialised to ISLOCAL
for all of the object allocators and auto initialisers, and only needs
resetting if the object's value is in global memory. The composite object
creation functions below take care of this automatically:
\verbatim
  ps_array()
  ps_string()
  ps_longstring()
  ps_string_or_longstring()
  ps_dictionary()
\endverbatim

In the rare cases where the globalness needs setting, the macros SETGLOBJECT()
or SETGLOBJECTTO() can be used. SETGLOBJECT() sets the global attribute to
the current savelevel, and is appropriate to accompany get_omemory():

\code
  oArray(newobject) = get_omemory(50) ;
  SETGLOBJECT(newobject) ;
\endcode

The macro SETGLOBJECTTO() is used to explicitly set a particular global
attribute:

\code
  oArray(lobject) = get_lomemory(50) ;
  SETGLOBJECTTO(lobject, FALSE) ; // local object
  oArray(gobject) = get_gomemory(50) ;
  SETGLOBJECTTO(gobject, TRUE) ; // global object
\endcode

For a dictionary (for convenience in loads of places),  the local/global
attribute is also replicated in the header OBJECT of the dictionary.

\subsection checkobj Checking OBJECTs

To check if the value of an OBJECT is in local or global memory, use the macro:

\code
  oGlobalValue( *theo ) // A true or ISGLOBAL value implies global
\endcode

To check whether an OBJECT is in VM, use the macro:

\code
  NOTVMOBJECT( *theo ) // TRUE implies it is not a VM OBJECT
\endcode

\subsection changeobj Modifying OBJECTs

When about to modify an OBJECT that requires saving (an array or dict),
one needs to  determine if  the OBJECT needs  saving  for  the  save/restore
mechanism.   For dictionaries, (virtually)  all  insertions go through
the functions insert_hash() or fast_insert_hash(), which handles saving the
dictionary slots, so the  technique is not explained here (if necessary,
look in dicthash.c).

The  first thing  that is  needed when  writing  into an array is to
check if the array slots need saving.  A function exists
to do all of this; not only  does it check  for save requirements, but
it then carries out  the  save/restore  mechanism.  Note  that  non-VM
OBJECTs are never saved. The function used for this is:

\code
Bool check_asave( olist , size , glmode , corecontext )
OBJECT *olist ;   // Pointer to the contents of the array (i.e. oArray(obj))
int32 size ;      // Length of array (in OBJECTs) to check for save/restore
Bool glmode ;     // Was the array allocated in global memory?
corecontext_t *corecontext // corecontext or NULL
\endcode

The global flag passed to check_asave() is the oGlobalValue() of the container
in which the object is being stored. Sub-sections of arrays may be checked by
reducing the size to check, and increasing the base address appropriately.
Typical usage of check_asave() might be:

\code
  if ( !check_asave(&oArray(obj)[n], theLen(obj) - n, oGlobalValue(obj), 0) )
    return FALSE ;
  // modify oArray(obj)[n...]
\endcode

Once the save/restore mechanism has been informed of  pending  changes
to an OBJECT, the OBJECT  can then  be modified.

\section misc Storing objects in containers

Care must be taken when storing OBJECTs into other OBJECTs. Putting local
OBJECTs into global OBJECTs is illegal and must be tested for by comparing
the global/local flag of the two OBJECTs in question. A macro exists to test
this, which should be used when a global container is having objects stored
in it:

\code
  if ( oGlobalValue(container) )
    if ( illegalLocalIntoGlobal(newentry) )
      return error_handler(INVALIDACCESS) ;
\endcode

The insert_hash() and fast_insert_hash() functions will automatically perform
this check for ODICTIONARY objects.

\section oaccess Object access permissions

When storing objects into arrays and dictionaries, the access permissions of
the array or dictionary should be checked. There are macros to check
permissions. When storing objects into a dictionary, the permissions of the
dictionary object's value should be examined, rather than the dictionary
header object. If access fails, then the object should be checked with
object_access_override() to see if access should be permitted anyway:

\code
  if ( !oCanRead(oarray) && !object_access_override(&oarray) )
    return error_handler(INVALIDACCESS) ;
  if ( !oCanWrite(*oDict(*theo)) && !object_access_override(oDict(*theo)) )
    return error_handler(INVALIDACCESS) ;
  if ( !oCanExec(*operator) && !object_access_override(operator) )
    return error_handler(INVALIDACCESS) ;
\endcode

The insert_hash() function automatically performs the writability check. Note
that the fast_insert_hash() functions do NOT perform this check, and so should
not be used if the check is required.

*/

/** \} */

/****************************************************************************/

/*
Log stripped */
#endif /* Protection from multiple inclusion */
