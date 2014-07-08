/*
 *    File: pspumacr.h    * PSio: PUblic definitions of MACRos
 *
 *  Author: Frederick A. Hallock (Fredo)
 *  
 *  This file creates all of the publicly accessable macro definitions.
 *  The file is "locked" by definition of XPSPUMACR so that multiple
 *  inclusions are not detrimental.
 *
 *  Herein, a macro naming convention is used. It classifies macro
 *  usage into five main categories:
 *
 *  1)  eXistance           defined or not, as in #ifdef XPS_?????
 *  2)  Constant            as in #define CPS_FOO 25
 *  3)  Expression          enclosed in () and used like C 'expression' syntax
 *                          can be used as "if(EPS_FOO)"; expressions CAN NOT
 *                          have any semicolons (;) in them but can be followed
 *                          by semicolon when the macro is invoked
 *  4)  Statement           enclosed in C 'do {} while(0)' statement so as to
 *							turn a multi-statement macro into a single statement
 *							which can be invoked like any single line statement in C.
 *  5)  Text Replacement    anything that is not used as above, such as
 *                          #define TPS_BEGIN_EXPR (
 *
 *  The five main categories are identified using a single letter to start
 *  the macro name, as in X, C, E, S, T, respectively. Following the type id
 *  letter is a subsystem id tuplet, in this case PS, to identify PSIO.
 *
 *$Id: export:pspumacr.h,v 1.29.26.1.1.1 2013/12/19 11:27:53 anon Exp $
 *
* Log stripped */

#ifndef   XPSPUMACR
#define   XPSPUMACR
    
/* Turn on all the extra debugging code, etc. */
#ifdef    DEBUG_BUILD
#ifndef	  XPS_DEBUG
#define	  XPS_DEBUG    /* leave this defined if debug build */
#endif /* XPS_DEBUG */
#endif /* DEBUG_BUILD */


#define CPS_MAX_STRING       4096  /* The max size of character string objs */
#define CPS_MAX_FILENAME      127  /* The max filename length */


/* These are defines for invalid handles.
 * Handles are pointers when XPS_DEBUG is not defined
 */

#ifdef    XPS_DEBUG

#define CPS_BAD_METH_HAND   0xfee1bad4 /* Not-pointers have L.O. 2 bits off */
#define CPS_BAD_INST_HAND   0xdeadb8
#define CPS_BAD_OBJ_HAND    0x01dfe11a

#else  /* XPS_DEBUG */

#define CPS_BAD_METH_HAND   0xadeadc0d /* pointers can't have L.O. 2 bits on */
#define CPS_BAD_INST_HAND   0xadeadee1
#define CPS_BAD_OBJ_HAND    0x8badf00d

#endif /* XPS_DEBUG */

/*  These are the values that PSIO control flags can have
*/
#define CPS_NO_OP          0    /* symbol for no EXTRA operation */
#define CPS_INSIDE_OP      1    /* true=operate inside obj, false=below obj */
#define CPS_KEY_OP         2    /* true=intending to operate on a key */
#define CPS_DELETE_ALL_OP  4    /* true=interlock deletion of complex obj */
#define CPS_IS_SAVED_OP    8    /* true=operate on saved obj, false=normal obj */
#define CPS_CURRENT_OP    16    /* true=operate on current obj. */
                                /* not inside or below */
#define CPS_NO_COMMENT_OP 32	/* true=only return non-comment object handles
								   below comments in syntax tree */

/*  These are the values that PSIO attributes can have
*/
#define CPS_TEMP           1    /* create a temp on write */
#define CPS_NO_TEMP        2    /* don't create a temp on write */

/*  The values of PSIO object types are:
*/

/* scalar objects */
#define CPS_INTEGER     0
#define CPS_REAL        1
#define CPS_BOOL        2
#define CPS_MARKER      3       /* placeholder for gen callback */
#define CPS_COMMENT     4
#define CPS_NAME        5
#define CPS_STRING      6

/* compound objects */
#define CPS_ARRAY       8
#define CPS_DICT        9
#define CPS_EXEC_ARRAY 10
#define CPS_ARRAY_END  11       /* only for callbacks */
#define CPS_DICT_END   12       /* only for callbacks */

#define CPS_MAX_TYPE    9  /* other types can't appear in obj tree */
#define CPS_BAD_TYPE    7  /* for a small amount of sanity? */
                            
#define CPS_CHAR_LIST   4  /* CPS_COMMENT | CPS_NAME | CPS_STRING */
#define CPS_COMPOUND    8  /* CPS_ARRAY | CPS_DICT | CPS_EXEC_ARRAY */


/* These are PSIO's definition of types within DICTSTRUCTIONs. (DS)
   NOTE: The usage of the DS type field within PSIO has been redefined
   relative to structio. The DS type field is not confined by a four
   bit subfield as in structio. PSIO uses the full int32 as the type.
   Types 0-3 and 8 are predefined by gdevstio.h for plugins. All other
   types are defined here by PSIO.
*/

/* These are primary defs. which can be moved to gdevstio.h in the future. */

/* These define the C type of the STIO types, and their sizes */
#define STIO_TYPE_INT       int32
#define STIO_SIZEOF_INT     4
#define STIO_TYPE_REAL      float
#define STIO_SIZEOF_REAL    4
#define STIO_TYPE_BOOL      uint32
#define STIO_SIZEOF_BOOL    4

#define STIO_INT_IARRAY     4  /* inline array of integers */
#define STIO_BOOL_IARRAY    5  /* inline array of bools */
#define STIO_REAL_IARRAY    6  /* inline array of reals */

/* Support flags within the struction_type field of a DICTSTRUCTION */
/* which can be or-ed with the type. (to be moved to SWpic in SW5.0) */
#define STIO_FLAG_OPTIONAL      (1<<30) /* optional when loading */

#define STIO_INT_OPT            (STIO_FLAG_OPTIONAL|STIO_INT)
#define STIO_FLOAT_OPT          (STIO_FLAG_OPTIONAL|STIO_FLOAT)
#define STIO_BOOL_OPT           (STIO_FLAG_OPTIONAL|STIO_BOOL)
#define STIO_INT_IARRAY_OPT     (STIO_FLAG_OPTIONAL|STIO_INT_IARRAY)
#define STIO_BOOL_IARRAY_OPT    (STIO_FLAG_OPTIONAL|STIO_BOOL_IARRAY)
#define STIO_REAL_IARRAY_OPT    (STIO_FLAG_OPTIONAL|STIO_REAL_IARRAY)
#define STIO_INLINE_STRING_OPT  (STIO_FLAG_OPTIONAL|STIO_INLINE_STRING)

#define STIO_TYPE_MASK          0xFFFF
#define STIO_TYPE(type)         ((type) & STIO_TYPE_MASK) /* extract type */

/* These are PSIO defs., created for consistency within PSIO. */
#define CPS_TYPE_INT    STIO_TYPE_INT
#define CPS_SIZEOF_INT  STIO_SIZEOF_INT
#define CPS_TYPE_REAL   STIO_TYPE_REAL
#define CPS_SIZEOF_REAL STIO_SIZEOF_REAL
#define CPS_TYPE_BOOL   STIO_TYPE_BOOL
#define CPS_SIZEOF_BOOL STIO_SIZEOF_BOOL

#define CPS_DS_END              STIO_END            /* end of DS array */
#define CPS_DS_INT              STIO_INT            /* int for DS array */
#define CPS_DS_FLOAT            STIO_FLOAT          /* float for DS array */
#define CPS_DS_BOOL             STIO_BOOL           /* bool for DS array */
#define CPS_DS_INT_IARRAY       STIO_INT_IARRAY     /* inline array of ints */
#define CPS_DS_BOOL_IARRAY      STIO_BOOL_IARRAY    /* inline array of bools */
#define CPS_DS_REAL_IARRAY      STIO_REAL_IARRAY    /* inline array of reals */
#define CPS_DS_STRING_IARRAY    STIO_INLINE_STRING  /* inline DS string */

#define CPS_DS_FLAG_OPTIONAL    STIO_FLAG_OPTIONAL  /* optional when loading */
#define CPS_DS_TYPE(type)       STIO_TYPE(type)

/*
    PSIO ERRORS: Values and meanings
*/
#define CPS_SUCCESS             01  /* NOT AN ERROR!! */

#define PBE                      0  /* All errors must be less than this */
#define CPS_BASE_ERROR      PBE
#define CPS_FAILURE         PBE- 0  /* not success */
#define CPS_NO_ERROR        PBE- 1  /* no extra error blocks */
#define CPS_NO_MEMORY       PBE- 2  /* allocation failure */
#define CPS_NO_OBJECT       PBE- 3  /* op inside empty compound obj */
#define CPS_SAVED           PBE- 4  /* unsaved op on saved obj */
#define CPS_SIMPLE          PBE- 5  /* compound op on simple obj */
#define CPS_NOT_SAVED       PBE- 6  /* saved op on unsaved obj */
#define CPS_NOT_SIMPLE      PBE- 7  /* simple op on compound obj  */
#define CPS_NOT_DICT        PBE- 8  /* dict op in non-dict obj */
#define CPS_NOT_TYPE        PBE- 9  /* unknown or incorrect type */
#define CPS_NOT_KEY         PBE-10  /* not a valid key type obj or has no key */
#define CPS_KEY_KEY         PBE-11  /* can't write key after key */
#define CPS_NOT_BOOL        PBE-12  /* bool value not 0 or 1 */
#define CPS_READ_LESS_DATA  PBE-13  /* not enough PSIO data to fill DS size */
#define CPS_READ_MORE_DATA  PBE-14  /* more PSIO data than DS requested size */
#define CPS_KEY_NOT_FOUND   PBE-15  /* PS_obj_lookup failed to find given key */

#define CPS_GOT_VALUE       PBE-17  /* key already has a value */
#define CPS_ONE_PAIR        PBE-18  /* one key allowed in top level dict */

#define CPS_METHODS_OFLOW   PBE-21  /* out of fixed space for methods */
#define CPS_MIXED_METHODS   PBE-23  /* PS_add_obj() objs not same method */
#define CPS_SAVED_CHILD     PBE-24  /* obj is part of another saved obj */
#define CPS_MIXED_SAVE_CTL  PBE-25  /* save op on unsaved obj or vise-versa */
#define CPS_NOT_TOP         PBE-26  /* op not allowed on top dict */
#define CPS_INTERNAL_ERR    PBE-27  /* BAD ERROR! NOT SUPPOSED TO HAPPEN */
#define CPS_NO_INTERLOCK    PBE-28  /* can't del compound w/o "delete all" */
#define CPS_UNKNOWN_EN      PBE-29  /* parse: unknown executable name */
#define CPS_UNBALANCED      PBE-30  /* parse: unbalanced delimiter like { */
#define CPS_LIMIT_CHECK     PBE-31  /* lex: token got too big */
#define CPS_NO_EXEC_ARRAY   PBE-32  /* parse: unsupported compound obj */
#define CPS_NO_VALUE        PBE-33  /* parse: incomplete key/value pair */
#define CPS_BROKE_IO        PBE-34  /* low level IO error, pushed syserr */
#define CPS_SYNTAX_ERR      PBE-35  /* syntax error while reading file */

#define CPS_NOT_INITED      PBE-81  /* init not done */
#define CPS_BAD_METH        PBE-82  /* invalid method handle supplied */
#define CPS_BAD_INST        PBE-83  /* invalid instance */
#define CPS_BAD_OBJ         PBE-84  /* invalid object */
#define CPS_BEEN_INITED     PBE-85  /* init already done */

#define CPS_BAD_ERROR       PBE-86  /* for PS_get_error() unit test */
#define CPS_BAD_DATA        PBE-87  /* for PS_get_error() unit test */
#define CPS_NOT_SAME_TYPE   PBE-88  /* for PS_read/write() unit test */
#define CPS_NOT_SAME_DATA   PBE-89  /* for PS_read/write() unit test */
#define CPS_BAD_PTR_ARG     PBE-90  /* for PS_?????_DS() */
#define CPS_BAD_CONTROL     PBE-91  /* for PS_set_attributes() unit test */


/******************************************************************************
Used to begin statement macros.
******************************************************************************/
#define TPS_BEGIN_STMNT MACRO_START

/******************************************************************************
Used to end statement macros.
******************************************************************************/
#define TPS_END_STMNT MACRO_END

/******************************************************************************
Used to begin expression macros.
******************************************************************************/
#define TPS_BEGIN_EXPR (

/******************************************************************************
Used to end expression macros.
******************************************************************************/
#define TPS_END_EXPR )


/******************************************************************************
Used to compute offsets into a structure.
******************************************************************************/
#define EPS_OFFSET(_type_, _elt_)                                   \
    TPS_BEGIN_EXPR                                                  \
    Stio_Offset(_type_, _elt_)                                      \
    TPS_END_EXPR

/******************************************************************************
******************************************************************************/


#endif /* XPSPUMACR - put at eof !*/

/* eof pspumacr.h */
