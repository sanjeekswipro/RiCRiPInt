/** \file
 * \ingroup psfuncs
 *
 * $HopeName: COREfunctions!src:pscalc.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * A very basic PostScript 'compiler' which converts some types of simple
 * PostScript into an equivalent form than can then be run independently of
 * the main PS interpreter.
 *
 * Designed mainly for processing "type 4 functions" (PS Calculator functions),
 * it can also be used for other basic PS fragments that need to be run either
 * repeatedly or else at inconvenient times (like at render time when
 * multi-threading).
 *
 * Started by just supporting the operators listed as being valid in type 4
 * functions, but has been extened to include a few extra ones to allow
 * standard DeviceN CustomConversions callbacks to be run.
 *
 * Example customer CustomConversions callbacks are :-
 *   {0. 0. 0. 4 -1 roll 1 exch sub 0. 0. 0.}     % Gray: k - cmykogv
 *   {3 {1 exch sub 3 1 roll} repeat 0. 0. 0. 0.} % RGB: rgb - cmykogv
 *   {0. 0. 0.}                                   % CMYK: cmyk - cmykogv
 * While internally we have ones like :-
 *   {}                                           % Gray
 *   {.11 mul exch .59 mul add exch .3 mul add}   % RGB
 *   {1 exch sub 4 1 roll                         % CMYK
 *    1 exch sub .11 mul 3 index mul exch
 *    1 exch sub .59 mul 3 index mul add
 *    4 2 roll 1 exch sub .3 mul mul add}
 * which will all work fine.
 *
 * But we also use
 *   {3 {1 exch sub 3 1 roll} repeat [1 1 1] //hqn-max-ucr exec} % RGB
 *     where /hqn-max-ucr does an array "get", and
 *   { ... 4 -1 roll 0.2 0.8 //SplitComponent exec 5 1 roll ... } % RGB
 *     where /SplitComponent includes local definitions e.g. /end_light
 *
 * So we will either need to alter these last two bits of usage, or enhance
 * pscalc to include this extra functionality.
 */

#include "core.h"
#include "swerrors.h"
#include "pscalc.h"
#include "fileio.h"
#include "mm.h"
#include "fileops.h"
#include "control.h"
#include "stacks.h"
#include "dictscan.h"
#include "namedef_.h"
#include "functns.h"
#include "pathops.h"
#include "execops.h"
#include "hqmemcpy.h"
#include "constant.h"
#include "monitor.h"
#include "miscops.h"

#if defined(DEBUG_BUILD)
static int32 pscalc_debug = 0;
#endif

/**
 * PS-calculator object types.
 */
enum {
  PSCALC_INT,
  PSCALC_REAL,
  PSCALC_BOOL,
  PSCALC_OPERATOR,
  PSCALC_PROC,
};

/**
 * PS-calculator operator opcodes.
 */
enum {
  PSCALC_invalid = -1,

  /* Type 4 Arithmetic operators */
  PSCALC_abs = 0, PSCALC_add, PSCALC_atan, PSCALC_ceiling, PSCALC_cos,
  PSCALC_cvi, PSCALC_cvr, PSCALC_div, PSCALC_exp, PSCALC_floor, PSCALC_idiv,
  PSCALC_ln, PSCALC_log, PSCALC_mod, PSCALC_mul, PSCALC_neg, PSCALC_round,
  PSCALC_sin, PSCALC_sqrt, PSCALC_sub, PSCALC_truncate,

  /* Type 4 Relational operators */
  PSCALC_and, PSCALC_bitshift, PSCALC_eq, PSCALC_false, PSCALC_ge, PSCALC_gt,
  PSCALC_le, PSCALC_lt, PSCALC_ne, PSCALC_not, PSCALC_or, PSCALC_true,
  PSCALC_xor,

  /* Type 4 Conditional operators */
  PSCALC_if, PSCALC_ifelse,

  /* Type 4 Stack operators */
  PSCALC_copy, PSCALC_dup, PSCALC_exch, PSCALC_index, PSCALC_pop, PSCALC_roll,

  /* Extended non-type 4 function operators */
  PSCALC_repeat, PSCALC_exec, PSCALC_for,
};

/**
 * List of possible arguments to PS-Calculator operators.
 */
enum {
  PSCALC_ARG_ANY,   /**< Any object type is a valid argument */
  PSCALC_ARG_NUM,   /**< Argument must a number, i.e. Int or Real */
  PSCALC_ARG_REAL,  /**< Argument must a Real */
  PSCALC_ARG_INT,   /**< Argument must an Int */
  PSCALC_ARG_BOOL,  /**< Argument must a Bool */
  PSCALC_ARG_IOB,   /**< Argument must either an Int or a Bool */
  PSCALC_ARG_PROC,  /**< Argument must a Procedure */
};

/**
 * Table defining valid arguments for all of the PS-Calculator functions.
 * This is a minimum requirement, more strict rules may be enforced in the
 * code particular to each operator.
 *
 * Note : Only two arguments checked. The few operators that have more than
 * two fixed arguments do their own checking.
 */
static struct {
  int32 nargs; /**< Required number of fixed arguments */
  int32 arg1;  /**< Type restrictions on the first argument */
  int32 arg2;  /**< Type restrictions on the second argument */
} pscalc_opargs[] = {
  { 1, PSCALC_ARG_NUM, PSCALC_ARG_ANY }, /* PSCALC_abs */
  { 2, PSCALC_ARG_NUM, PSCALC_ARG_NUM }, /* PSCALC_add */
  { 2, PSCALC_ARG_NUM, PSCALC_ARG_NUM }, /* PSCALC_atan */
  { 1, PSCALC_ARG_NUM, PSCALC_ARG_ANY }, /* PSCALC_ceiling */
  { 1, PSCALC_ARG_NUM, PSCALC_ARG_ANY }, /* PSCALC_cos */
  { 1, PSCALC_ARG_NUM, PSCALC_ARG_ANY }, /* PSCALC_cvi */
  { 1, PSCALC_ARG_NUM, PSCALC_ARG_ANY }, /* PSCALC_cvr */
  { 2, PSCALC_ARG_NUM, PSCALC_ARG_NUM }, /* PSCALC_div */
  { 2, PSCALC_ARG_NUM, PSCALC_ARG_NUM }, /* PSCALC_exp */
  { 1, PSCALC_ARG_NUM, PSCALC_ARG_ANY }, /* PSCALC_floor */
  { 2, PSCALC_ARG_INT, PSCALC_ARG_INT }, /* PSCALC_idiv */
  { 1, PSCALC_ARG_NUM, PSCALC_ARG_ANY }, /* PSCALC_ln */
  { 1, PSCALC_ARG_NUM, PSCALC_ARG_ANY }, /* PSCALC_log */
  { 2, PSCALC_ARG_INT, PSCALC_ARG_INT }, /* PSCALC_mod */
  { 2, PSCALC_ARG_NUM, PSCALC_ARG_NUM }, /* PSCALC_mul */
  { 1, PSCALC_ARG_NUM, PSCALC_ARG_ANY }, /* PSCALC_neg */
  { 1, PSCALC_ARG_NUM, PSCALC_ARG_ANY }, /* PSCALC_round */
  { 1, PSCALC_ARG_NUM, PSCALC_ARG_ANY }, /* PSCALC_sin */
  { 1, PSCALC_ARG_NUM, PSCALC_ARG_ANY }, /* PSCALC_sqrt */
  { 2, PSCALC_ARG_NUM, PSCALC_ARG_NUM }, /* PSCALC_sub */
  { 1, PSCALC_ARG_NUM, PSCALC_ARG_ANY }, /* PSCALC_truncate */
  { 2, PSCALC_ARG_IOB, PSCALC_ARG_IOB }, /* PSCALC_and */
  { 2, PSCALC_ARG_INT, PSCALC_ARG_INT }, /* PSCALC_bitshift */
  { 2, PSCALC_ARG_ANY, PSCALC_ARG_ANY }, /* PSCALC_eq */
  { 0, PSCALC_ARG_ANY, PSCALC_ARG_ANY }, /* PSCALC_false */
  { 2, PSCALC_ARG_NUM, PSCALC_ARG_NUM }, /* PSCALC_ge */
  { 2, PSCALC_ARG_NUM, PSCALC_ARG_NUM }, /* PSCALC_gt */
  { 2, PSCALC_ARG_NUM, PSCALC_ARG_NUM }, /* PSCALC_le */
  { 2, PSCALC_ARG_NUM, PSCALC_ARG_NUM }, /* PSCALC_lt */
  { 2, PSCALC_ARG_IOB, PSCALC_ARG_IOB }, /* PSCALC_ne */
  { 1, PSCALC_ARG_IOB, PSCALC_ARG_ANY }, /* PSCALC_not */
  { 2, PSCALC_ARG_IOB, PSCALC_ARG_IOB }, /* PSCALC_or */
  { 0, PSCALC_ARG_ANY, PSCALC_ARG_ANY }, /* PSCALC_true */
  { 2, PSCALC_ARG_IOB, PSCALC_ARG_IOB }, /* PSCALC_xor */
  { 2, PSCALC_ARG_PROC, PSCALC_ARG_BOOL }, /* PSCALC_if */
  { 3, PSCALC_ARG_PROC, PSCALC_ARG_PROC }, /* PSCALC_ifelse */
  { 1, PSCALC_ARG_INT, PSCALC_ARG_ANY }, /* PSCALC_copy */
  { 1, PSCALC_ARG_ANY, PSCALC_ARG_ANY }, /* PSCALC_dup */
  { 2, PSCALC_ARG_ANY, PSCALC_ARG_ANY }, /* PSCALC_exch */
  { 1, PSCALC_ARG_INT, PSCALC_ARG_ANY }, /* PSCALC_index */
  { 1, PSCALC_ARG_ANY, PSCALC_ARG_ANY }, /* PSCALC_pop */
  { 2, PSCALC_ARG_INT, PSCALC_ARG_INT }, /* PSCALC_roll */
  { 2, PSCALC_ARG_PROC, PSCALC_ARG_INT}, /* PSCALC_repeat */
  { 1, PSCALC_ARG_PROC, PSCALC_ARG_ANY}, /* PSCALC_exec */
  { 4, PSCALC_ARG_PROC, PSCALC_ARG_NUM}, /* PSCALC_for */
};

/**
 * The storage class for REAL data inside a PS-Calculator object.
 *
 * Note REAL data is promoted to a SYSTEMVALUE and back again when any
 * internal calculations are carried out.
 */
typedef USERVALUE PSCALC_OBJREAL;

/**
 * A PS-Calculator object.
 */
typedef struct PSCALC_OBJ {
  int32 type; /**< Object type : controls which "val" union entry is used */
  union {
    int32 integer;        /**< Value for integer objects */
    PSCALC_OBJREAL real;  /**< Value for real objects */
    Bool  boolean;        /**< Value for boolean objects */
    int32 opcode;         /**< Value for operator objects */
    struct {
      int16 starti;         /**< Start index of procedure array */
      int16 endi;           /**< End index of procedure array */
    } range;              /**< Value for procedure objects */
  } val;                /**< Union of object value */
} PSCALC_OBJ;

/**
 * Maximum number of PS-Calculator objects that will be compiled in one
 * go into a single procedure.
 *
 * Note. There is also an implementation upper limit of 64k because start/end
 * indices in objects are 16bit values.
 */
#define PSCALC_MAXOBJS 1000

/**
 * Maximum PS-Calculator stack size.
 */
#define PSCALC_MAXSTACK 100

/**
 * The object stack used by the PS-Calculator.
 */
typedef struct PSCALC_STACK {
  int32 size; /**< Total number of elements in the stack */
  int32 top;  /**< Index of the next free space on the stack */
  PSCALC_OBJ obj[PSCALC_MAXSTACK+10]; /**< Stack plus some safety overhead */
} PSCALC_STACK;

/**
 * Table mapping PS operator names to PS-Calculator opcodes.
 */
static struct {
  int32 sysop;   /**< PS operator name */
  int32 calcop;  /**< PS-Calculator operator opcode */
} sys2calc[] = {
  { NAME_abs, PSCALC_abs },
  { NAME_add, PSCALC_add },
  { NAME_atan, PSCALC_atan },
  { NAME_ceiling, PSCALC_ceiling },
  { NAME_cos, PSCALC_cos },
  { NAME_cvi, PSCALC_cvi },
  { NAME_cvr, PSCALC_cvr },
  { NAME_div, PSCALC_div },
  { NAME_exp, PSCALC_exp },
  { NAME_floor, PSCALC_floor },
  { NAME_idiv, PSCALC_idiv },
  { NAME_ln, PSCALC_ln },
  { NAME_log, PSCALC_log },
  { NAME_mod, PSCALC_mod },
  { NAME_mul, PSCALC_mul },
  { NAME_neg, PSCALC_neg },
  { NAME_round, PSCALC_round },
  { NAME_sin, PSCALC_sin },
  { NAME_sqrt, PSCALC_sqrt },
  { NAME_sub, PSCALC_sub },
  { NAME_truncate, PSCALC_truncate },
  { NAME_and, PSCALC_and },
  { NAME_bitshift, PSCALC_bitshift },
  { NAME_eq, PSCALC_eq },
  { NAME_false, PSCALC_false },
  { NAME_ge, PSCALC_ge },
  { NAME_gt, PSCALC_gt },
  { NAME_le, PSCALC_le },
  { NAME_lt, PSCALC_lt },
  { NAME_ne, PSCALC_ne },
  { NAME_not, PSCALC_not },
  { NAME_or, PSCALC_or },
  { NAME_true, PSCALC_true },
  { NAME_xor, PSCALC_xor },
  { NAME_if, PSCALC_if },
  { NAME_ifelse, PSCALC_ifelse },
  { NAME_copy, PSCALC_copy },
  { NAME_dup, PSCALC_dup },
  { NAME_exch, PSCALC_exch },
  { NAME_index, PSCALC_index },
  { NAME_pop, PSCALC_pop },
  { NAME_roll, PSCALC_roll },
  { NAME_repeat, PSCALC_repeat },
  { NAME_exec, PSCALC_exec },
  { NAME_for, PSCALC_for },
};

/**
 * See if the name is in our table of operators which we handle, if so
 * return the PS-Calculator opcode.
 */
static int32 pscalc_op(OBJECT *obj)
{
  int32 i;

  for ( i = 0; i < sizeof(sys2calc)/sizeof(sys2calc[0]); i++ ) {
    if ( oOp(*obj) == &system_ops[sys2calc[i].sysop] )
      return sys2calc[i].calcop;
  }
  return PSCALC_invalid;
}

/**
 * Get the value of a PC calculator number (real or integer).
 */
static Bool pscalc_getnum(PSCALC_OBJ *obj, SYSTEMVALUE *val)
{
  if ( obj->type == PSCALC_REAL )
    *val = (SYSTEMVALUE)obj->val.real;
  else if ( obj->type == PSCALC_INT )
    *val = (SYSTEMVALUE)obj->val.integer;
  else {
    *val = 0.0;
    return FALSE;
  }
  return TRUE;
}

/**
 * Report and raise the given PS calculator error condition.
 */
static int32 pscalc_err(int32 err, int32 opcode)
{
#if defined(DEBUG_BUILD)
  if ( pscalc_debug > 0 ) {
    monitorf((uint8 *)"PSCALC Error %d %d\n", err, opcode);
  }
#else
  UNUSED_PARAM(int32, opcode);
#endif
  return err;
}

/**
 * Check that the given argument means the restrcitions in type we have
 * placed upon it. Also if it happens to be a number (int or real), return
 * its value converted to a format suitable for use in internal calculations.
 */
static Bool pscalc_checkarg(PSCALC_OBJ *obj, int32 allowed, SYSTEMVALUE *val)
{
  (void)pscalc_getnum(obj, val);

  if ( allowed == PSCALC_ARG_ANY )
    return TRUE;
  if ( allowed == PSCALC_ARG_REAL && obj->type != PSCALC_REAL )
    return FALSE;
  if ( allowed == PSCALC_ARG_INT && obj->type != PSCALC_INT )
    return FALSE;
  if ( allowed == PSCALC_ARG_BOOL && obj->type != PSCALC_BOOL )
    return FALSE;
  if ( allowed == PSCALC_ARG_PROC && obj->type != PSCALC_PROC )
    return FALSE;
  if ( allowed == PSCALC_ARG_NUM ) {
    if ( !(obj->type == PSCALC_INT || obj->type == PSCALC_REAL) )
      return FALSE;
  }
  if ( allowed == PSCALC_ARG_IOB ) {
    if ( !(obj->type == PSCALC_INT || obj->type == PSCALC_BOOL) )
      return FALSE;
  }
  return TRUE;
}

/**
 * Are two PS-Calculator objects equal. Note ints can be equal to reals if they
 * have the same value.
 */
static Bool pscalc_equalobjs(PSCALC_OBJ *obj1, PSCALC_OBJ *obj2)
{
  switch( obj1->type ) {
    case PSCALC_INT:
      if ( obj2->type == PSCALC_INT )
        return ( obj1->val.integer == obj2->val.integer );
      if ( obj2->type == PSCALC_REAL )
        return ( (PSCALC_OBJREAL)obj1->val.integer == obj2->val.real );
      else
        return FALSE;
    case PSCALC_REAL:
      if ( obj2->type == PSCALC_REAL )
        return ( obj1->val.real == obj2->val.real );
      if ( obj2->type == PSCALC_INT )
        return ( (PSCALC_OBJREAL)obj2->val.integer == obj1->val.real );
      else
        return FALSE;
    case PSCALC_BOOL:
      return ( obj2->type == PSCALC_BOOL &&
               obj1->val.boolean == obj2->val.boolean );
    case PSCALC_OPERATOR:
      return ( obj2->type == PSCALC_OPERATOR &&
               obj1->val.opcode == obj2->val.opcode );
  }
  return FALSE;
}

static int32 pscalc_do_op(int32 opcode, PSCALC_OBJ *pso, PSCALC_STACK *stack);

/**
 * Run the given PS-Calculator procedure.
 */
static int32 pscalc_run(PSCALC_OBJ *pso, PSCALC_STACK *stack, PSCALC_OBJ *proc)
{
  int32 i, err, starti = proc->val.range.starti, endi = proc->val.range.endi;

  HQASSERT(proc->type == PSCALC_PROC &&
           starti > 0 && starti < PSCALC_MAXOBJS && endi > 0 &&
           endi < PSCALC_MAXOBJS && starti < endi,
           "Corrupt pscalc procedure\n");

  for ( i = starti; i < endi; i++ ) {
    PSCALC_OBJ *obj = pso+i;

    if ( stack->top >= stack->size )
      return pscalc_err(PSCALC_stackoverflow, PSCALC_invalid);

    switch ( obj->type ) {
      case PSCALC_INT:
      case PSCALC_REAL:
      case PSCALC_BOOL:
      case PSCALC_PROC:
        /* Executing any of these just means putting them on the stack */
        stack->obj[stack->top++] = *obj;
        /* Just put the header of a procedure on the stack, skip the body.
         * The body will be used when we come to execute the procedure via an
         * "if" or "ifelse".
         */
        if ( obj->type == PSCALC_PROC )
          i = obj->val.range.endi - 1;
        break;
      case PSCALC_OPERATOR:
        /* Lookup the operator and do what it says */
        if ((err = pscalc_do_op(obj->val.opcode, pso, stack)) != PSCALC_noerr)
          return err;
        break;
    }
  }
  return PSCALC_noerr;
}

/**
 * Execute a single PS-Calculator operator object.
 */
static int32 pscalc_do_op(int32 opcode, PSCALC_OBJ *pso, PSCALC_STACK *stack)
{
  PSCALC_OBJ *op1 = stack->top < 1 ? NULL : &(stack->obj[stack->top-1]);
  PSCALC_OBJ *op2 = stack->top < 2 ? NULL : &(stack->obj[stack->top-2]);
  SYSTEMVALUE v1 = 0.0, v2 = 0.0;
  PSCALC_OBJ tmp;
  int32 i, n, m, err;

  /* Check it has the correct number of arguments */
  if ( pscalc_opargs[opcode].nargs > stack->top )
    return pscalc_err(PSCALC_stackunderflow, opcode);

  /* If it has at least one or two args ensure they are of the correct type */
  if ( pscalc_opargs[opcode].nargs >= 1 ) {
    if ( !pscalc_checkarg(op1, pscalc_opargs[opcode].arg1, &v1) )
      return pscalc_err(PSCALC_typecheck, opcode);
  }
  if ( pscalc_opargs[opcode].nargs >= 2 ) {
    if ( !pscalc_checkarg(op2, pscalc_opargs[opcode].arg2, &v2) )
      return pscalc_err(PSCALC_typecheck, opcode);

    if ( pscalc_opargs[opcode].arg1 == PSCALC_ARG_IOB &&
         pscalc_opargs[opcode].arg2 == PSCALC_ARG_IOB )
      if ( op1->type != op2->type )
        return pscalc_err(PSCALC_typecheck, opcode);
  }

  /*
   * All of the operators accessed via a big switch statement. Makes the
   * function quite big. But there does not seem to be much to gain by breaking
   * them all out into their own subroutines. Each one is typically only a few
   * lines of code in length.
   * Could have done operators via a table of function pointers rather than an
   * opcode. But an opcode is much simpler and makes it easier to commonise
   * argument checking etc.
   *
   */
  /** \todo BMJ 25-Aug-11 : Not dealing with e.g. add(int, int) overflowing. */
  switch ( opcode ) {
    /* All the arithmetic operators */
    case PSCALC_abs:
      if ( op1->type == PSCALC_INT ) {
        if ( op1->val.integer < 0 )
          op1->val.integer = -op1->val.integer;
      }
      else /* real */ {
        if ( op1->val.real < 0.0 )
          op1->val.real = -op1->val.real;
      }
      break;
    case PSCALC_add:
      if ( op1->type == PSCALC_INT && op2->type == PSCALC_INT ) {
        op2->val.integer += op1->val.integer;
      } else {
        op2->type = PSCALC_REAL;
        op2->val.real = (PSCALC_OBJREAL)(v1 + v2);
      }
      stack->top--;
      break;
    case PSCALC_atan:
      if ( v1 == 0.0 && v2 == 0.0 )
        return pscalc_err(PSCALC_undefinedresult, opcode);
      op2->type = PSCALC_REAL;
      op2->val.real = (PSCALC_OBJREAL)(RAD_TO_DEG * atan2(v2, v1));
      if ( op2->val.real < 0.0 )
        op2->val.real += 360.0;
      stack->top--;
      break;
    case PSCALC_ceiling:
      if ( op1->type == PSCALC_REAL ) {
        n = (int32)op1->val.real;
        if ( n >= 0 && (PSCALC_OBJREAL)n - op1->val.real != 0.0 )
          n++;
        op1->val.real = (PSCALC_OBJREAL)n;
      }
      break;
    case PSCALC_cos:
      op1->type = PSCALC_REAL;
      op1->val.real = (PSCALC_OBJREAL)cos(v1*DEG_TO_RAD);
      break;
    case PSCALC_cvi:
      if ( op1->type == PSCALC_REAL ) {
        op1->type = PSCALC_INT;
        op1->val.integer = (int32)op1->val.real;
      }
      break;
    case PSCALC_cvr:
      if ( op1->type == PSCALC_INT ) {
        op1->type = PSCALC_REAL;
        op1->val.real = (PSCALC_OBJREAL)op1->val.integer;
      }
      break;
    case PSCALC_div:
      if ( v1 == 0.0 )
        return pscalc_err(PSCALC_undefinedresult, opcode);
      op2->type = PSCALC_REAL;
      op2->val.real = (PSCALC_OBJREAL)(v2/v1);
      stack->top--;
      break;
    case PSCALC_exp:
      op2->type = PSCALC_REAL;
      if ( v2 < 0.0 && v1 != (int32)v1 )
        return pscalc_err(PSCALC_undefinedresult, opcode);
      op2->val.real = (PSCALC_OBJREAL)pow(v2, v1);
      stack->top--;
      break;
    case PSCALC_floor:
      if ( op1->type == PSCALC_REAL ) {
        n = (int32)op1->val.real;
        if ( n <= 0 && (PSCALC_OBJREAL)n - op1->val.real != 0.0 )
          n--;
        op1->val.real = (PSCALC_OBJREAL)n;
      }
      break;
    case PSCALC_idiv:
      if ( op1->val.integer == 0 )
        return pscalc_err(PSCALC_undefinedresult, opcode);
      op2->val.integer = op2->val.integer/op1->val.integer;
      stack->top--;
      break;
    case PSCALC_ln:
      if ( v1 <= 0.0 )
        return pscalc_err(PSCALC_undefinedresult, opcode);
      op1->type = PSCALC_REAL;
      op1->val.real = (PSCALC_OBJREAL)log(v1);
      break;
    case PSCALC_log:
      if ( v1 <= 0.0 )
        return pscalc_err(PSCALC_undefinedresult, opcode);
      op1->type = PSCALC_REAL;
      op1->val.real = (PSCALC_OBJREAL)log10(v1);
      break;
    case PSCALC_mod:
      if ( op1->val.integer == 0 )
        return pscalc_err(PSCALC_undefinedresult, opcode);
      op2->val.integer = op2->val.integer % op1->val.integer;
      stack->top--;
      break;
    case PSCALC_mul:
      if ( op1->type == PSCALC_INT && op2->type == PSCALC_INT ) {
        op2->val.integer *= op1->val.integer;
      } else {
        op2->type = PSCALC_REAL;
        op2->val.real = (PSCALC_OBJREAL)(v1 * v2);
      }
      stack->top--;
      break;
    case PSCALC_neg:
      if ( op1->type == PSCALC_INT ) {
        op1->val.integer = -op1->val.integer;
      }
      else if ( op1->type == PSCALC_REAL ) {
        op1->val.real = -op1->val.real;
      }
      break;
    case PSCALC_round:
      if ( op1->type == PSCALC_REAL ) {
        n = (int32)(op1->val.real + 0.5);
        if ( n <= 0 && (PSCALC_OBJREAL)n - op1->val.real != 0.5 )
          n--;
        op1->val.real = (PSCALC_OBJREAL)n;
      }
      break;
    case PSCALC_sin:
      op1->type = PSCALC_REAL;
      op1->val.real = (PSCALC_OBJREAL)sin(v1*DEG_TO_RAD);
      break;
    case PSCALC_sqrt:
      if ( v1 < 0.0 )
        return pscalc_err(PSCALC_undefinedresult, opcode);
      op1->type = PSCALC_REAL;
      op1->val.real = (PSCALC_OBJREAL)sqrt(v1);
      break;
    case PSCALC_sub:
      if ( op1->type == PSCALC_INT && op2->type == PSCALC_INT ) {
        op2->val.integer -= op1->val.integer;
      } else {
        op2->type = PSCALC_REAL;
        op2->val.real = (PSCALC_OBJREAL)(v2 - v1);
      }
      stack->top--;
      break;
    case PSCALC_truncate:
      if ( op1->type == PSCALC_REAL ) {
        n = (int32)op1->val.real;
        op1->val.real = (PSCALC_OBJREAL)n;
      }
      break;

    /* Relational */
    case PSCALC_and:
      if ( op1->type == PSCALC_INT )
        op2->val.integer = (op1->val.integer & op2->val.integer);
      else
        op2->val.boolean = (op1->val.boolean & op2->val.boolean);
      stack->top--;
      break;
    case PSCALC_bitshift:
      n = op1->val.integer;
      if ( n < 0 )
        op2->val.integer = (op2->val.integer >> (-n));
      else if ( n > 0 )
        op2->val.integer = (op2->val.integer << n);
      stack->top--;
      break;
    case PSCALC_eq:
      op2->val.boolean = pscalc_equalobjs(op1, op2);
      op2->type = PSCALC_BOOL;
      stack->top--;
      break;
    case PSCALC_false:
      if ( stack->top >= stack->size )
        return pscalc_err(PSCALC_stackoverflow, opcode);
      op1 = &(stack->obj[stack->top]);
      op1->type = PSCALC_BOOL;
      op1->val.boolean = FALSE;
      stack->top++;
      break;
    case PSCALC_ge:
      op2->val.boolean = (Bool)(v2 >= v1);
      op2->type = PSCALC_BOOL;
      stack->top--;
      break;
    case PSCALC_gt:
      op2->val.boolean = (Bool)(v2 > v1);
      op2->type = PSCALC_BOOL;
      stack->top--;
      break;
    case PSCALC_le:
      op2->val.boolean = (Bool)(v2 <= v1);
      op2->type = PSCALC_BOOL;
      stack->top--;
      break;
    case PSCALC_lt:
      op2->val.boolean = (Bool)(v2 < v1);
      op2->type = PSCALC_BOOL;
      stack->top--;
      break;
    case PSCALC_ne:
      op2->val.boolean = !pscalc_equalobjs(op1, op2);
      op2->type = PSCALC_BOOL;
      stack->top--;
      break;
    case PSCALC_not:
      if ( op1->type == PSCALC_INT )
        op1->val.integer = ~op1->val.integer;
      else
        op1->val.boolean = !op1->val.boolean;
      break;
    case PSCALC_or:
      if ( op1->type == PSCALC_INT )
        op2->val.integer = (op1->val.integer | op2->val.integer);
      else
        op2->val.boolean = (op1->val.boolean | op2->val.boolean);
      stack->top--;
      break;
    case PSCALC_true:
      if ( stack->top >= stack->size )
        return pscalc_err(PSCALC_stackoverflow, opcode);
      op1 = &(stack->obj[stack->top]);
      op1->type = PSCALC_BOOL;
      op1->val.boolean = TRUE;
      stack->top++;
      break;
    case PSCALC_xor:
      if ( op1->type == PSCALC_INT )
        op2->val.integer = (op1->val.integer ^ op2->val.integer);
      else
        op2->val.boolean = (op1->val.boolean ^ op2->val.boolean);
      stack->top--;
      break;

    /* Conditional */
    case PSCALC_if:
      stack->top -= 2;
      if ( op2->val.boolean ) {
        if ((err = pscalc_run(pso, stack, op1)) != PSCALC_noerr)
          return err;
      }
      break;
    case PSCALC_ifelse:
      /* Only get 2 params for free, pick up the third by hand */
      if ( stack->obj[stack->top-3].type != PSCALC_BOOL )
        return pscalc_err(PSCALC_typecheck, opcode);
      stack->top -= 3;
      if ( stack->obj[stack->top].val.boolean ) {
        if ((err = pscalc_run(pso, stack, op2)) != PSCALC_noerr)
          return err;
      } else {
        if ((err = pscalc_run(pso, stack, op1)) != PSCALC_noerr)
          return err;
      }
      break;
    case PSCALC_repeat:
      stack->top -= 2;
      n = op2->val.integer;
      tmp = *op1;
      while ( n-- > 0 ) {
        if ((err = pscalc_run(pso, stack, &tmp)) != PSCALC_noerr)
          return err;
      }
      break;
    case PSCALC_exec:
      stack->top--;
      if ((err = pscalc_run(pso, stack, op1)) != PSCALC_noerr)
        return err;
      break;

    case PSCALC_for: {
      SYSTEMVALUE ff, f1, f2, f3;
      Bool use_ints = FALSE;

      if ( !pscalc_getnum(&stack->obj[stack->top-4], &f1) ||
           !pscalc_getnum(&stack->obj[stack->top-3], &f2) ||
           !pscalc_getnum(&stack->obj[stack->top-2], &f3) )
        return pscalc_err(PSCALC_typecheck, opcode);

      if ( stack->obj[stack->top-4].type == PSCALC_INT &&
           stack->obj[stack->top-3].type == PSCALC_INT &&
           stack->obj[stack->top-2].type == PSCALC_INT )
        use_ints = TRUE;

      tmp = *op1;
      stack->top -= 4;

      for ( ff = f1; (f3 > 0.0) ? (ff <= f2) : (ff >= f2); ff += f3 ) {
        if ( stack->top + 1 >= stack->size )
          return pscalc_err(PSCALC_stackoverflow, opcode);
        if ( use_ints ) {
          stack->obj[stack->top].type = PSCALC_INT;
          stack->obj[stack->top].val.integer = (int32)(ff + 0.5);
        } else {
          stack->obj[stack->top].type = PSCALC_REAL;
          stack->obj[stack->top].val.real = (PSCALC_OBJREAL)ff;
        }
        stack->top++;
        if ((err = pscalc_run(pso, stack, &tmp)) != PSCALC_noerr)
          return err;
      }
      break;
    }

    /* Stack */
    case PSCALC_copy:
      if ( (n = op1->val.integer) < 0 )
        return pscalc_err(PSCALC_rangecheck, opcode);
      stack->top--;
      if ( stack->top < n )
        return pscalc_err(PSCALC_stackunderflow, opcode);
      if ( stack->top + n >= stack->size )
        return pscalc_err(PSCALC_stackoverflow, opcode);
      for ( i = 0; i < n; i++ ) {
        op1 = &(stack->obj[stack->top]);
        op1[0] = op1[-n];
        stack->top++;
      }
      break;
    case PSCALC_dup:
      if ( stack->top >= stack->size )
        return pscalc_err(PSCALC_stackoverflow, opcode);
      op1[1] = op1[0];
      stack->top++;
      break;
    case PSCALC_exch:
      tmp = *op1;
      *op1 = *op2;
      *op2 = tmp;
      break;
    case PSCALC_index:
      if ( (n = op1->val.integer) < 0 )
        return pscalc_err(PSCALC_rangecheck, opcode);
      if ( n >= stack->top  - 1 )
        return pscalc_err(PSCALC_stackunderflow, opcode);
      op1[0] = op1[-n-1];
      break;
    case PSCALC_pop:
      stack->top--;
      break;
    case PSCALC_roll:
      m = op1->val.integer;
      if ( (n = op2->val.integer)  < 0 )
        return pscalc_err(PSCALC_rangecheck, opcode);

      stack->top -= 2;
      if ( stack->top < n )
        return pscalc_err(PSCALC_stackunderflow, opcode);
      while ( m != 0 ) {
        if ( m > 0 ) {
          tmp = stack->obj[stack->top - 1];
          for ( i = stack->top - 1; i > stack->top - n; i-- ) {
            stack->obj[i] = stack->obj[i-1];
          }
          stack->obj[stack->top - n] = tmp;
          m--;
        } else {
          tmp = stack->obj[stack->top - n];
          for ( i = stack->top - n; i < stack->top -1; i++ ) {
            stack->obj[i] = stack->obj[i+1];
          }
          stack->obj[stack->top - 1] = tmp;
          m++;
        }
      }
      break;
  }
  return PSCALC_noerr;
}

/**
 * Execute the given bit of 'compiled' PS-Calculator code.
 *
 * Initialise the stack with the given set of values, and check exactly the
 * required number of values are returned.
 */
int32 pscalc_exec(PSCALC_OBJ *func, int32 n_in, int32 n_out,
                  USERVALUE *in, USERVALUE *out)
{
  PSCALC_STACK stack;
  int32 i, err;

  stack.size = PSCALC_MAXSTACK;
  stack.top  = 0;

  for ( i = 0; i < n_in; i++ ) {
    stack.obj[stack.top].type = PSCALC_REAL;
    stack.obj[stack.top].val.real = (PSCALC_OBJREAL)in[i];
    stack.top++;
  }

  if ( (err = pscalc_run(func, &stack, func)) != PSCALC_noerr )
    return err;

  if ( stack.top != n_out )
    return pscalc_err(PSCALC_rangecheck, PSCALC_invalid);
  for ( i = 0; i < n_out; i++ ) {
    PSCALC_OBJ *obj = &(stack.obj[--stack.top]);
    SYSTEMVALUE val;

    if ( obj->type == PSCALC_REAL )
      val = (SYSTEMVALUE)obj->val.real;
    else if ( obj->type == PSCALC_INT )
      val = (SYSTEMVALUE)obj->val.integer;
    else
      return pscalc_err(PSCALC_typecheck, PSCALC_invalid);
    out[n_out-1-i] = (USERVALUE)val;
  }
  return PSCALC_noerr;
}

/**
 * Add a PS procedure to our array of 'compiled' PS-Calculator objects.
 *
 * A PS-Calculator function is just a linear array of objects. Sub-procedures
 * are flattened and put in line as well with a single object header,
 * there is no tree structure. This makes memory management much more
 * straight-forward. Each procedure header includes a start and end index
 * of its object contents. Executing a procedure just puts the header on the
 * stack. When it is then invoked by an "if" or "ifelse" the indicies can be
 * used to access the body of the procedure.
 */
static int16 pscalc_add2array(OBJECT *proc, PSCALC_OBJ *pso, int16 pso_i)
{
  int32 i, len, proc_i;

  len = theLen(*proc);
  pso[pso_i].type = PSCALC_PROC;
  proc_i = pso_i; /* remember where the proc starts */
  pso[pso_i].val.range.starti = pso_i+1;
  pso[pso_i].val.range.endi = 0; /* Not known yet */
  pso_i++;

  if ( pso_i + len >= PSCALC_MAXOBJS )
    return -1;

  for ( i = 0; i < len; i++ ) {
    OBJECT *obj = oArray(*proc) + i;

    /* PS function may or may not have been bound, so we need to deal with
     * both operator objects and name objects that resolve to operators.
     */
    switch ( oType(*obj) ) {
      case OINTEGER:
        pso[pso_i].type = PSCALC_INT;
        pso[pso_i].val.integer = oInteger(*obj);
        pso_i++;
        break;
      case OREAL:
        pso[pso_i].type = PSCALC_REAL;
        pso[pso_i].val.real = oReal(*obj);
        pso_i++;
        break;
      case OBOOLEAN:
        pso[pso_i].type = PSCALC_BOOL;
        pso[pso_i].val.boolean = (Bool)oBool(*obj);
        pso_i++;
        break;
      case ONAME:
        /* Name needs to refer to a built-in operator.
         * But true/false are exceptions as they are not operators
         * but names that refer to the boolean values. So have to deal
         * with theses two as special cases.
         */
        if ( oName(*obj)->namenumber == NAME_true ) {
          pso[pso_i].type = PSCALC_BOOL;
          pso[pso_i].val.boolean = TRUE;
          pso_i++;
          break;
        } else if ( oName(*obj)->namenumber == NAME_false ) {
          pso[pso_i].type = PSCALC_BOOL;
          pso[pso_i].val.boolean = FALSE;
          pso_i++;
          break;
        } else if ( (obj = name_is_operator(obj)) == NULL ) {
          return -1;
        }
        /* else drop through... */
      case OOPERATOR:
        pso[pso_i].type = PSCALC_OPERATOR;
        if ( (pso[pso_i].val.opcode = pscalc_op(obj)) == PSCALC_invalid )
          return -1;
        pso_i++;
        break;
      case OARRAY:
      case OPACKEDARRAY:
        if ( !oExecutable(*obj) )
          return -1;
        /* Could in theory recurse into array inside itself and go round in
         * circles. But limit on max size of pscalc array created will stop
         * things eventually. This is much easier than having to try and mark
         * procs as we go.
         */
        pso_i = pscalc_add2array(obj, pso, pso_i);
        if ( pso_i < 0 )
          return -1;
        break;
      case OMARK:
      case ONULL:
      case ODICTIONARY:
      case OSTRING:
      default:
        return -1;
    }
  }
  pso[proc_i].val.range.endi = pso_i;
  return pso_i;
}

/**
 * Attempt to create a PS-Calculator representation of the PS procedure.
 *
 * May 'fail' for a number of number of different reasons. The PS may be
 * too big or complex, or include operators we do not support, or the
 * allocation of the PS-Calculator procedure block may fail. In all of
 * these cases just return NULL and do not raise an error, as any client
 * is required to have some fallback alternative.
 */
PSCALC_OBJ *pscalc_create(OBJECT *proc)
{
  int32 nobjs;
  PSCALC_OBJ pso[PSCALC_MAXOBJS];
  PSCALC_OBJ *func;

  if ( oType(*proc) != OARRAY && oType(*proc) != OPACKEDARRAY )
    return NULL;

  nobjs = pscalc_add2array(proc, pso, 0);
  if ( nobjs < 0 )
    return NULL;

  func = (PSCALC_OBJ *)mm_alloc(mm_pool_temp, sizeof(PSCALC_OBJ)*nobjs,
                                MM_ALLOC_CLASS_FUNCTIONS);
  if ( func == NULL )
    return NULL;

  HqMemCpy(func, pso, nobjs * sizeof(PSCALC_OBJ));

#if defined(DEBUG_BUILD)
  /* Simple code testing : Execute the pscalc func now with fixed args */
  if ( pscalc_debug > 0 ) {
    USERVALUE in = 0.5, out = 0.0;
    int32 err;

    err = pscalc_exec(func, 1, 1, &in, &out);
    if ( err == PSCALC_noerr )
      monitorf((uint8 *)"PSCALC(%d) %f -> %f\n", err, in, out);
    else
      monitorf((uint8 *)"PSCALC(%d)\n", err);
  }
#endif /* DEBUG_BUILD */

  return func;
}

/**
 * Free the memory associated with the given PS-Calculator function.
 */
void pscalc_destroy(PSCALC_OBJ *func)
{
  int32 nobjs;

  if ( func == NULL )
    return;

  HQASSERT(func->type == PSCALC_PROC && func->val.range.starti == 1 &&
           func->val.range.endi > 0 && func->val.range.endi < PSCALC_MAXOBJS,
           "Corrupt pscalc func\n");

  nobjs = func->val.range.endi;
  mm_free(mm_pool_temp, func, sizeof(PSCALC_OBJ)*nobjs);
}

/* Log stripped */
