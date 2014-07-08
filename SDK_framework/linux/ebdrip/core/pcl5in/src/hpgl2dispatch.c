/** \file
 * \ingroup hpgl2
 *
 * $HopeName: COREpcl_pcl5!src:hpgl2dispatch.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Dispatch HPGL2 commands once scanned.
 */

#include "core.h"
#include "hpgl2dispatch.h"

#include "pcl5context_private.h"
#include "macros.h"

#include "objects.h"
#include "namedef_.h"

/* include HPGL2 operator headers */
#include "hpgl2config.h"
#include "hpgl2misc.h"
#include "hpgl2vector.h"
#include "hpgl2polygon.h"
#include "hpgl2fonts.h"
#include "hpgl2linefill.h"
#include "hpgl2technical.h"

/* ============================================================================
 * HPGL2 register function code.
 * ============================================================================
 */
typedef Bool (*HPGL2_OperatorCallback)(PCL5Context *pcl5_ctxt) ;

/* Under certain circumstances, HPGL2 operators are to be treated as
 * null-ops.  Following flags define circumstances where the real
 * operator is invoked.  Eg. polygon mode, lost mode of the HPGL
 * interpreter.  PATH_CONT indicates special processing to PD;
 * operator
 */
enum {
  HPGL2NONE = 0,
  POLYGON = 1,
  LOST = 2,
  PATH_CONT = 4, /* indicates operator defines a path continuation for PD; */
  PATH_IGNORE = 8 /* indicates op should have any effect on path continuation */
};

struct HPGL2RegisteredOp {
  uint8 op_name_ch1 ;
  uint8 op_name_ch2 ;
  HPGL2_OperatorCallback op ;
  uint8 modes;
} ;

#define HPGL2_LAST_ENTRY { '\0', '\0', NULL, HPGL2NONE}

/* op is tested for NULL to end the list. */
static struct HPGL2RegisteredOp hpgl2_ops[] = {
  /* HP-GL/2 Commands organized by Group (as listed within the PCL5 */
  /* Technical Reference). */

  /* Configuration Group (1 of 5) */
  /* (PG, RP is unsupported) */
  {'C', 'O', hpgl2op_CO, HPGL2NONE|POLYGON|LOST| PATH_IGNORE},  /* Comment */
  {'D', 'F', hpgl2op_DF, HPGL2NONE|POLYGON|LOST },  /* Default Values */
  {'I', 'N', hpgl2op_IN, HPGL2NONE|POLYGON|LOST },  /* Initialize */
  {'I', 'P', hpgl2op_IP, HPGL2NONE|LOST},  /* Input P1 and P2 */
  {'I', 'R', hpgl2op_IR, HPGL2NONE|LOST},  /* Input Relative P1 and P2 */
  {'I', 'W', hpgl2op_IW, HPGL2NONE|LOST},  /* Input Window */
  {'R', 'O', hpgl2op_RO, HPGL2NONE|LOST},  /* Rotate Coordinate System */
  {'S', 'C', hpgl2op_SC, HPGL2NONE|LOST},  /* Scale */
  /* 8 */

  /* Vector Group (2 of 5) */
  {'A', 'A', hpgl2op_AA, HPGL2NONE|POLYGON|PATH_CONT},/* Arc Absolute */
  {'A', 'R', hpgl2op_AR, HPGL2NONE|POLYGON|PATH_CONT},/* Arc Relative */
  {'A', 'T', hpgl2op_AT, HPGL2NONE|POLYGON|PATH_CONT},  /* Absolute Arc Three Point */
  {'B', 'R', hpgl2op_BR, HPGL2NONE|POLYGON|PATH_CONT},  /* Bezier Relative */
  {'B', 'Z', hpgl2op_BZ, HPGL2NONE|POLYGON|PATH_CONT},  /* Bezier Absolute */
  {'C', 'I', hpgl2op_CI, HPGL2NONE|POLYGON|PATH_CONT},  /* Circle */
  {'P', 'A', hpgl2op_PA, HPGL2NONE|POLYGON|LOST|PATH_CONT},  /* Plot Absolute */
  {'P', 'D', hpgl2op_PD, HPGL2NONE|POLYGON|LOST|PATH_CONT},  /* Pen Down */
  {'P', 'E', hpgl2op_PE, HPGL2NONE|POLYGON|PATH_CONT},  /* Polyline Encoded */
  {'P', 'R', hpgl2op_PR, HPGL2NONE|POLYGON|PATH_CONT},  /* Plot Relative */
  {'P', 'U', hpgl2op_PU, HPGL2NONE|POLYGON|LOST},  /* Pen Up */
  {'R', 'T', hpgl2op_RT, HPGL2NONE|POLYGON|PATH_CONT},  /* Relative Arc Three Point */
  /* 12 */

  /* Polygon Group (3 of 5) */
  {'E', 'A', hpgl2op_EA, HPGL2NONE|PATH_CONT},  /* Edge Rectangle Absolute */
  {'E', 'P', hpgl2op_EP, HPGL2NONE|PATH_CONT},  /* Edge Polygon */
  {'E', 'R', hpgl2op_ER, HPGL2NONE|PATH_CONT},  /* Edge Rectangle Relative */
  {'E', 'W', hpgl2op_EW, HPGL2NONE|PATH_CONT},  /* Edge Wedge */
  {'F', 'P', hpgl2op_FP, HPGL2NONE|PATH_CONT},  /* Fill Polygon */
  {'P', 'M', hpgl2op_PM, HPGL2NONE|POLYGON|LOST|PATH_CONT },  /* Polygon Mode */
  {'R', 'A', hpgl2op_RA, HPGL2NONE|PATH_CONT},  /* Fill Rectangle Absolute */
  {'R', 'R', hpgl2op_RR, HPGL2NONE|PATH_CONT},  /* Fill Rectangle Relative */
  {'W', 'G', hpgl2op_WG, HPGL2NONE|PATH_CONT},  /* Fill Wedge */
  {'R', 'Q', hpgl2op_RQ, HPGL2NONE|PATH_CONT},  /* Rectangle Quick */
  /* 10 */

  /* Character Group (4 of 5) */
  {'A', 'D', hpgl2op_AD, HPGL2NONE|LOST},  /* Alternate Font Definition */
  {'C', 'F', hpgl2op_CF, HPGL2NONE|LOST},  /* Character Fill Mode */
  {'C', 'P', hpgl2op_CP, HPGL2NONE},  /* Character Plot */
  {'D', 'I', hpgl2op_DI, HPGL2NONE|LOST},  /* Absolute Direction */
  {'D', 'R', hpgl2op_DR, HPGL2NONE|LOST},  /* Relative Direction */
  {'D', 'T', hpgl2op_DT, HPGL2NONE|LOST},  /* Define Label Terminator */
  {'D', 'V', hpgl2op_DV, HPGL2NONE|LOST},  /* Define Variable Text Path */
  {'E', 'S', hpgl2op_ES, HPGL2NONE|LOST},  /* Extra Space */
  {'F', 'I', hpgl2op_FI, HPGL2NONE},  /* Select Primary Font */
  {'F', 'N', hpgl2op_FN, HPGL2NONE},  /* Select Secondary Font */
  {'L', 'B', hpgl2op_LB, HPGL2NONE},  /* Label */
  {'L', 'O', hpgl2op_LO, HPGL2NONE},  /* Label Origin */
  {'S', 'A', hpgl2op_SA, HPGL2NONE|LOST},  /* Select Alternate Font */
  {'S', 'B', hpgl2op_SB, HPGL2NONE|LOST},  /* Scalable or Bitmap Fonts */
  {'S', 'D', hpgl2op_SD, HPGL2NONE|LOST},  /* Standard Font Definition */
  {'S', 'I', hpgl2op_SI, HPGL2NONE|LOST},  /* Absolute Character Size */
  {'S', 'L', hpgl2op_SL, HPGL2NONE|LOST},  /* Character Slant */
  {'S', 'R', hpgl2op_SR, HPGL2NONE|LOST},  /* Relative Character Size */
  {'S', 'S', hpgl2op_SS, HPGL2NONE|LOST},  /* Select Standard font */
  {'T', 'D', hpgl2op_TD, HPGL2NONE|LOST},  /* Transparent Data */
  {'L', 'M', hpgl2op_LM, HPGL2NONE|LOST},  /* Label Mode */
  /* 21 */

  /* Line and fill attributes Group (5 of 5) */
  {'A', 'C', hpgl2op_AC, HPGL2NONE|LOST},  /* Anchor Corner */
  {'F', 'T', hpgl2op_FT, HPGL2NONE|LOST|PATH_CONT},  /* Fill Type */
  {'L', 'A', hpgl2op_LA, HPGL2NONE|LOST},  /* Line Attributes */
  {'L', 'T', hpgl2op_LT, HPGL2NONE|LOST},  /* Line Type */
  {'P', 'W', hpgl2op_PW, HPGL2NONE|LOST},  /* Pen Width */
  {'R', 'F', hpgl2op_RF, HPGL2NONE|LOST},  /* Raster Fill Definition */
  {'S', 'M', hpgl2op_SM, HPGL2NONE|LOST},  /* Symbol Mode */
  {'S', 'P', hpgl2op_SP, HPGL2NONE|LOST|PATH_IGNORE},  /* Select Pen */
  {'S', 'V', hpgl2op_SV, HPGL2NONE|LOST},  /* Screened Vectors */
  {'T', 'R', hpgl2op_TR, HPGL2NONE|LOST},  /* Transparency Mode */
  {'U', 'L', hpgl2op_UL, HPGL2NONE|LOST},  /* User-defined Line Type */
  {'W', 'U', hpgl2op_WU, HPGL2NONE|LOST},  /* Pen Width Unit Selection */
  /* 12 */

  /* Technical Graphics Extension */
  /* (BP, CT, DL, EC, FR, MG, MT, NR, OE, OH, OI, OP, OS, PS, QL, ST, VS are unsupported) */
  {'M', 'C', hpgl2op_MC, HPGL2NONE|LOST},  /* Merge control */
  {'P', 'P', hpgl2op_PP, HPGL2NONE|LOST},  /* Pixel Placement */
  /* 2 */

  /* Miscellaneous */
  /* (TL, XT and YT are unsupported) */

  /* Palette Extension */
  {'C', 'R', hpgl2op_CR, HPGL2NONE},  /* Color range */
  {'N', 'P', hpgl2op_NP, HPGL2NONE},  /* Number of pens */
  {'P', 'C', hpgl2op_PC, HPGL2NONE},  /* Pen color assignment */
  /* 3 */

  HPGL2_LAST_ENTRY /* This MUST be last. */
} ;

/* ============================================================================
 * HPGL2 function lookup hash table
 * ============================================================================
 */

/* Take 5 low bits from first and second char. 2^5 x 2^5 = 1024 */
#define FUNCTTABLE_HASH_SIZE 1024

struct HPGL2FunctEntry {
  HPGL2_OperatorCallback op ;    /* Function */
  uint8 modes;
} ;

struct HPGL2FunctTable {
  /* Number of entries in hash table */
  unsigned int num_entries ;
  /* The hash table */
  struct HPGL2FunctEntry **table ;
} ;

static
struct HPGL2FunctEntry *hpgl2_find_funct_entry(struct HPGL2FunctTable *table,
                                               uint8 op_name_ch1,
                                               uint8 op_name_ch2,
                                               unsigned int *hval)
{
  HQASSERT(table != NULL, "table is NULL") ;
  HQASSERT(hval != NULL, "hval is NULL") ;

  /* Create hash between 0 and 1023 using 10 bits. Top 3 bits for both
     characters MUST be equal to 010. */

  /* (ch1 & 11100000) == 01000000 */
  /* (ch2 & 11100000) == 01000000 */
  HQASSERT((op_name_ch1 & 0xE0) == 0x40, "HPGL2 first op char invalid?") ;
  HQASSERT((op_name_ch2 & 0xE0) == 0x40, "HPGL2 second op char invalid?") ;

  *hval = (op_name_ch1 & 0x1F) << 5 ; /* (ch1 & 00011111) << 5 */
  *hval |= (op_name_ch2 & 0x1F) ; /* (ch2 & 00011111) Fill lower 5 bits. */

  HQASSERT(*hval < FUNCTTABLE_HASH_SIZE,
           "Something has gone wrong with the hash function.") ;
  return table->table[*hval] ;
}

Bool hpgl2_funct_table_create(struct HPGL2FunctTable **table)
{
  unsigned int i;
  HQASSERT(table != NULL, "table pointer is NULL");

  *table = mm_alloc(mm_pcl_pool, sizeof(struct HPGL2FunctTable),
                    MM_ALLOC_CLASS_PCL_CONTEXT) ;

  if (*table == NULL)
    return FALSE;

  (*table)->table = mm_alloc(mm_pcl_pool, sizeof(struct HPGL2FunctEntry*) * FUNCTTABLE_HASH_SIZE,
                             MM_ALLOC_CLASS_PCL_CONTEXT) ;

  if ((*table)->table == NULL) {
    mm_free(mm_pcl_pool, *table, sizeof(struct HPGL2FunctTable)) ;
    *table = NULL;
    return FALSE;
  }

  /* Initialize the table structure. */
  (*table)->num_entries = 0;
  for (i=0; i<FUNCTTABLE_HASH_SIZE; i++) {
    (*table)->table[i] = NULL;
  }

  return TRUE;
}

void hpgl2_funct_table_destroy(struct HPGL2FunctTable **table)
{
  unsigned int i ;

  HQASSERT(table != NULL, "table is NULL") ;
  HQASSERT(*table != NULL, "table pointer is NULL") ;

  for (i=0; i<FUNCTTABLE_HASH_SIZE; i++) {
    if ((*table)->table[i] != NULL) {
      mm_free(mm_pcl_pool, (*table)->table[i], sizeof(struct HPGL2FunctEntry)) ;
      (*table)->num_entries-- ;
    }
  }
  HQASSERT((*table)->num_entries == 0, "num_entries is not zero.") ;

  mm_free(mm_pcl_pool, (*table)->table, sizeof(struct HPGL2FunctEntry*) * FUNCTTABLE_HASH_SIZE) ;
  mm_free(mm_pcl_pool, (*table), sizeof(struct HPGL2FunctTable)) ;
  (*table) = NULL ;
  return ;
}

Bool register_hpgl2_ops(struct HPGL2FunctTable *table)
{
  struct HPGL2RegisteredOp *reg_curr;
  struct HPGL2FunctEntry *curr;
  unsigned int hval;

  for (reg_curr = hpgl2_ops; reg_curr->op != NULL; reg_curr++) {

    curr = hpgl2_find_funct_entry(table, reg_curr->op_name_ch1,
                                  reg_curr->op_name_ch2, &hval) ;

    HQASSERT(curr == NULL,
             "Seems we have a hash clash! This should not be possible.") ;

    curr  = mm_alloc(mm_pcl_pool, sizeof(struct HPGL2FunctEntry),
                     MM_ALLOC_CLASS_PCL_CONTEXT) ;

    if (curr == NULL)
      return FALSE ;

    curr->op = reg_curr->op ;
    curr->modes = reg_curr->modes;

    table->table[hval] = curr ;
    table->num_entries++ ;
  }
  return TRUE ;
}

HPGL2FunctEntry* hpgl2_get_op(
  PCL5Context*  pcl5_ctxt,
  uint8 op_name_ch1, uint8 op_name_ch2)
{
  HPGL2FunctEntry* entry;
  unsigned int hval;

  entry = hpgl2_find_funct_entry(pcl5_ctxt->ops_table, op_name_ch1,
                                 op_name_ch2, &hval);

  return(entry);
}

/* PCL COMPATIBILITY
 * On the HP reference printer the function of some operators can depend on
 * the subsequent operators (not just the side effects made to the HPGL state).
 * A particular example is the handling of PD; operation which is documented
 * as drawing a dot on the page but in the implementation depends on whether
 * subsequent operators can be considered as extending the path implied by
 * the dot. Thus, PD;PU; draw a dot, but PD;PD10,10 does not.
 * Support for this is not easily added to the HPGL interpeter operators so
 * we have to do a limited form of it around dispatch.
 *
 * process_path_continuation is used to handle the implementation of PD;
 * operations. PD; on its own sets a flag indicating the current point is
 * a candidate for being a dot. If the next operator to be invoked is one
 * that logically continues the path, then this flag is reset. If the
 * operator does not continue the path, then a "dot" may be drawn.
 */
/** \todo
 * The better solution would be to ensure that drawing operators can build up
 * paths with multiple subpaths and flush the drawing path as an when required
 * ( which could be indicated by a flag on the operator structure ).
 */
static void process_path_continuation(PCL5Context *pcl5_ctxt,
                                      Bool path_cont_op)
{
  HPGL2PrintState *print_state = get_hpgl2_print_state(pcl5_ctxt) ;

  if (print_state->dot_candidate) {
    if (!path_cont_op) {
      /*  */
      (void)draw_PD_dot(pcl5_ctxt);
    }
    print_state->dot_candidate = FALSE;
  }
}

/* ============================================================================
 * HPGL2 dispatch function code.
 * ============================================================================
 */

Bool hpgl2_dispatch_op(PCL5Context *pcl5_ctxt, HPGL2FunctEntry* op)
{
  HPGL2_OperatorCallback f = NULL;
  uint8 op_mode = 0;
  int32 path_action = 0;

  HQASSERT(pcl5_ctxt != NULL, "pcl5_ctxt is NULL") ;

  if (! pcl5_recording_a_macro) {

    /** \todo experimentation with HP4250 reference print indicates polygon
     * mode state is local to MPE, but suspect LOST mode indication is not.
     * So, need separate APIs to get appropriate context for operator dispatch.
     */
    if ( hpgl2_in_polygon_mode(pcl5_ctxt) )
      op_mode |= POLYGON;

    if ( hpgl2_in_lost_mode(pcl5_ctxt) )
      op_mode |= LOST;

    if (op != NULL) {
      /* treat operator as null op if HPGL mode disallows the operator. */
      if ( ((op->modes & op_mode) == op_mode) ) {
        path_action = op->modes & (PATH_CONT | PATH_IGNORE) ;
        f = op->op ;
      } else {
        path_action = PATH_CONT ;
        f = &hpgl2op_nullop;
      }
    }

    if ((path_action & PATH_IGNORE) != PATH_IGNORE)
      process_path_continuation(pcl5_ctxt,
                                (path_action & PATH_CONT) == PATH_CONT);

    if (f != NULL)
      return f(pcl5_ctxt) ;
  }

  return TRUE ;
}

/* ============================================================================
* Log stripped */
