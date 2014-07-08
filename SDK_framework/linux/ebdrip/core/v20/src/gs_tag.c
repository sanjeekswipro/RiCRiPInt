/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:gs_tag.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS gstate tagging.
 */

#include "core.h"
#include "gs_tag.h"

#include "swerrors.h"
#include "objects.h"
#include "dictscan.h"
#include "namedef_.h"

#include "psvm.h"
#include "pscontext.h"
#include "stacks.h"
#include "typeops.h"
#include "dicthash.h"
#include "dictops.h"
#include "graphics.h"
#include "gstate.h"
#include "render.h"
#include "dlstate.h"
#include "dl_store.h"
#include "swrle.h"
#include "vndetect.h"
#include "display.h"
#include "jobmetrics.h"

/*
 *  Tag blocks are divided into categories so that the DL tag ones can be placed
 *  before the non-DL tag ones.  This speeds up attaching them to the DL objects.
 */

enum tag_block_categories {
  CAT_dl_tags = 0,
  CAT_non_dl_tags,
  CAT_num_categories
};

/*
 *  The counters are numbered rather than placed in a structure because several operations
 *  loop across all the counters doing the same to each (eg sum_total, so_far)
 */

enum tag_block_counters {
  COUNT_blocks = 0,
  COUNT_tags,
  COUNT_bits,
  COUNT_num_counters
};

/* This structure holds information about a particular tag_block_category. */

typedef struct catinfo {
  int32 counters[COUNT_num_counters];
  int32 numbers[COUNT_num_counters];
#if defined( ASSERT_BUILD )
  int32 limits[COUNT_num_counters];
#endif
} CATINFO ;

/* Passed as the third argument to walk_dictionary */

typedef struct tag_walker {
  GSTAGSTRUCTUREOBJECT *gts ; /* NULL on first pass */
  CATINFO category_info[CAT_num_categories] ;
  CATINFO *ci; /* Points into above array for current category */
  int32 category , width ;
  NAMECACHE *block_name ;
  int32 type_bit_mask ;
} TAG_WALKER ;


static NAMETYPEMATCH tag_dict[] = {
/* 0 */ { NAME_Start               , 1, { OINTEGER }},
/* 1 */ { NAME_Width               , 1, { OINTEGER }},
/* 2 */ { NAME_Type                , 1, { ONAME }},
        DUMMY_END_MATCH
} ;

/*  Called by walk_dictionary.  Examine an individual tag dictionary.
 */

static Bool walk_one_tag( OBJECT *thekey, OBJECT *theval, void *arg )
{
  TAG_WALKER *tw = (TAG_WALKER *) arg ;
  int32 start, width, is_string ;

  if ( oType(*theval) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;
  if ( !dictmatch( theval, tag_dict ) )
    return FALSE ;
  start = oInteger(*tag_dict[0].result) ;
  width = oInteger(*tag_dict[1].result) ;
  /* the Type tag must be either /integertype or /stringtype */
  if ( oName(*tag_dict[2].result) == system_names + NAME_integertype )
    is_string = FALSE ;
  else if ( oName(*tag_dict[2].result) == system_names + NAME_stringtype )
    is_string = TRUE ;
  else
    return error_handler( RANGECHECK ) ;
  /* The tags must cover a sensible range of the available width.
   * Note that we don't enforce either non-overlap or complete coverage - that's a feature,
   * not a bug.
   */
  if ( start < 0 || width < 1 || width > (is_string ? (65535 * 8) : 32) || start + width > tw->width )
    return error_handler( RANGECHECK ) ;
  if ( tw->gts != NULL ) {
     /* This branch is taken during the second pass */
    TAG_INFO *taginfo = & tw->gts->tags[tw->ci->counters[COUNT_tags]].tag ;

    taginfo->block_name = tw->block_name ;
    taginfo->tag_name = oName(*thekey) ;
    taginfo->bit_offset = tw->ci->counters[COUNT_bits] + start ;
    taginfo->bit_width = width + (is_string ? 32 : 0);
  }
  tw->ci->counters[COUNT_tags]++;
  return TRUE ;
}

static NAMETYPEMATCH tag_block_dict[] = {
  /* 0 */ { NAME_Width               , 1, { OINTEGER }},
  /* 1 */ { NAME_TagType | OOPTIONAL , 1, { OINTEGER }},
  /* 2 */ { NAME_Tags                , 1, { ODICTIONARY }},
    DUMMY_END_MATCH
} ;

/* Called by walk_dictionary.  Examines a tag block dictionary.
 */

static Bool walk_one_tag_block( OBJECT *thekey, OBJECT *theval, void *arg )
{
  TAG_WALKER *tw = (TAG_WALKER *) arg ;
  int32 width, word_width, tag_type ;

  if ( oType(*theval) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;
  if ( !dictmatch( theval, tag_block_dict ) )
    return FALSE ;
  tw->width = width = oInteger(*tag_block_dict[0].result) ;
  if ( width <= 0 )
    return error_handler( RANGECHECK ) ;
  word_width = (width + 31) >> 5 ;
  if ( tag_block_dict[1].result != NULL ) {
    /* It's a DL tag. */

    /* DL tag blocks must be a whole number of words wide. */
    if ( word_width >= (1 << RLE_LEN_INFO_COUNT) || width != (word_width << 5) )
      return error_handler( RANGECHECK ) ;
    tw->category = CAT_dl_tags ;
    tag_type = oInteger(*tag_block_dict[1].result) ;
    if ( tag_type < 0 || tag_type >= (1 << RLE_LEN_INFO_TYPE) )
      return error_handler( RANGECHECK ) ;
    if ( tw->type_bit_mask & (1 << tag_type) )
      return error_handler( RANGECHECK ) ; /* That type's already used */
    tw->type_bit_mask |= (1 << tag_type) ; /* Mark that type used */
  } else {
    tw->category = CAT_non_dl_tags ;
    tag_type = -1 ; /* Leaving this uninitialised might break cacheing */
  }

  /* Set ci so that we use the appropriate counters */
  tw->ci = & tw->category_info[tw->category] ;
  tw->block_name = oName(*thekey) ;

  if ( tw->gts != NULL ) {
     /* This branch is taken during the second pass */
    TAG_BLOCK_INFO *blockinfo = & tw->gts->tags[tw->ci->counters[COUNT_blocks]].block ;

    blockinfo->block_name = tw->block_name ;
    blockinfo->type_num = tag_type ;
    blockinfo->data_words = word_width ;
    blockinfo->data_woffset = (tw->ci->counters[COUNT_bits] >> 5) - 1;
      /*  - 1 because this is the offset into the data copied into the display list objects,
          which doesn't include the extra word at the beginning allocated for the
          allocation count, and included as 32 in COUNT_bits when initialised below */
  }
  /* OK, now verify/read all the actual tag dictionaries */
  if ( ! walk_dictionary( tag_block_dict[2].result, walk_one_tag, tw ) )
    return FALSE ;
  tw->ci->counters[COUNT_blocks]++;
  tw->ci->counters[COUNT_bits] += word_width << 5;
  return TRUE ;
}

static size_t gs_size(uint32 nblocks, uint32 ntags)
{
  GSTAGSTRUCTUREOBJECT *ptr = NULL;

  return (size_t)((uint8 *)(&(ptr->tags[nblocks+ntags])) - (uint8 *)(ptr));
}

/*
 * Allocate and fill a GSTAGSTRUCTUREOBJECT based on the contents of this gstate
 * tag dictionary.
 */

Bool make_gstagstructureobject(DL_STATE *page, OBJECT *gtd,
                               GSTAGSTRUCTUREOBJECT **pgts)
{
  TAG_WALKER tw ;
  int32 sum_totals[COUNT_num_counters], so_fars[COUNT_num_counters] ;
  int i, j ;

  if ( oType(*gtd) == ONULL ) {
    /* ONULL corresponds to a NULL GSTAGSTRUCTUREOBJECT. */
    *pgts = NULL ;
    return TRUE ;
  }
  if ( oType(*gtd) != ODICTIONARY )
    return error_handler( TYPECHECK ) ;
  /* Reset all counters */
  for ( i = 0; i < CAT_num_categories; i++ )
    for ( j = 0; j < COUNT_num_counters; j++ )
      tw.category_info[i].counters[j] = 0 ;
  /* First pass - verify correctness and find how big everything needs to be */
  tw.gts = NULL ;
  tw.type_bit_mask = 0;
  if ( ! walk_dictionary( gtd, walk_one_tag_block, &tw ) )
    return FALSE ;
  /* Preserve the counters, get the totals */
  for ( j = 0; j < COUNT_num_counters; j++ )  {
    sum_totals[j] = 0 ;
    for ( i = 0; i < CAT_num_categories; i++ )
      sum_totals[j] += tw.category_info[i].numbers[j] = tw.category_info[i].counters[j] ;
  }

  /* Allocate the structure object */
  tw.gts = (GSTAGSTRUCTUREOBJECT *)dl_alloc(page->dlpools,
    gs_size(sum_totals[COUNT_blocks], sum_totals[COUNT_tags]),
    MM_ALLOC_CLASS_GSTAGS_CACHE ) ;
  if ( tw.gts == NULL )
    return error_handler( VMERROR ) ;

  /* Now we know some totals we can fill them in */
  tw.gts->num_blocks = sum_totals[COUNT_blocks] ;
  tw.gts->num_dl_blocks =  tw.category_info[CAT_dl_tags].numbers[COUNT_blocks] ;
  tw.gts->num_tags = sum_totals[COUNT_tags] ;
  tw.gts->alloc_words = 1 + sum_totals[COUNT_bits] / 32 ;
  tw.gts->dl_words = tw.category_info[CAT_dl_tags].numbers[COUNT_bits] / 32 ;

  /* Alloc out the space among the categories.
   * "counters" plays a different role in each of the two passes.  In pass 1, the counters
   * are initialised to zero and used to count the total blocks etc. in a particular
   * category.  Before pass 2, tag descriptors, tag block descriptors, and space in the
   * data block are allocated out among the categories, and the counters mark where you're
   * up to in your allocated space.   */
  so_fars[COUNT_blocks] = 0;
  so_fars[COUNT_tags] = sum_totals[COUNT_blocks] ; /* write tags after blocks */
  so_fars[COUNT_bits] = 32 ; /* leave 32 bits for size of alloc */
  for ( i = 0; i < CAT_num_categories; i++ ) {
    for ( j = 0; j < COUNT_num_counters; j++ ) {
      tw.category_info[i].counters[j] = so_fars[j] ;
      so_fars[j] += tw.category_info[i].numbers[j] ;
#if defined( ASSERT_BUILD )
      tw.category_info[i].limits[j] = so_fars[j] ;
#endif
    }
  }

  /* Second pass: make use of the space, fill in the structure object */
  tw.type_bit_mask = 0 ; /* everything is checked again in the second pass */
  if ( ! walk_dictionary( gtd, walk_one_tag_block, &tw ) ) {
    HQFAIL( "Why didn't this fail first time around then?" ) ;
    return FALSE ;
  }
#if defined( ASSERT_BUILD )
  for ( i = 0; i < CAT_num_categories; i++ )
    for ( j = 0; j < COUNT_num_counters; j++ )
      HQASSERT( tw.category_info[i].counters[j] == tw.category_info[i].limits[j],
        "The space set aside wasn't exactly that used" ) ;
#endif

  *pgts = (GSTAGSTRUCTUREOBJECT*)dlSSInsert(page->stores.gstag,
                                            &tw.gts->storeEntry, FALSE);
  if ( *pgts == NULL )
    return FALSE ;
#ifdef METRICS_BUILD
  dl_metrics()->store.hdlCount++;
#endif

  if ( *pgts != tw.gts )
    dl_free(page->dlpools, tw.gts, gs_size(tw.gts->num_blocks,
            tw.gts->num_tags), MM_ALLOC_CLASS_GSTAGS_CACHE);

  return TRUE ;
}

/* Set the current gstate tagging state from the dictionary on the top of
 * the stack */

Bool setgstatetagdict_(ps_context_t *pscontext)
{
  DL_STATE *page = ps_core_context(pscontext)->page;
  OBJECT *gtd ;
  GSTAGSTRUCTUREOBJECT *gts ;
  uint32 *data ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  /* Must flush vignettes to ensure they have correct tags */
  if ( !flush_vignette(VD_Default) )
    return FALSE ;

  gtd = theTop( operandstack ) ;

  /* Mark the dictionary read only - not exactly secure, but should discourage
   * mucking about. */
  if ( !reduceOaccess( READ_ONLY , TRUE , gtd ) )
    return FALSE ;

  /* We're eager rather than lazy here because:
   * - we need to verify this dictionary is good now, and that's nearly as much
   *    work as setting it up
   * - many things become easier if the data is always present.
   */
  if ( !make_gstagstructureobject(page, gtd, &gts) )
    return FALSE ;
  gstateptr->theGSTAGinfo.dict = *gtd ;
  gstateptr->theGSTAGinfo.structure = gts ;
  data = gstateptr->theGSTAGinfo.data ;
  if ( data && (gts == NULL || data[0] != gts->alloc_words) ) {
    /* It's the wrong size - free it */
    mm_free( mm_pool_temp, data, data[0] * sizeof(int32) ) ;
    gstateptr->theGSTAGinfo.data = data = NULL ;
  }
  if ( gts ) {
    int32 i ;

    if ( data == NULL ) {
      /* Allocate it */
      data = mm_alloc( mm_pool_temp, gts->alloc_words * sizeof(int32), MM_ALLOC_CLASS_GSTATE ) ;
      if ( data == NULL )
        return error_handler( VMERROR ) ;
      data[0] = gts->alloc_words ;
      gstateptr->theGSTAGinfo.data = data ;
    }
    i = gts->alloc_words ;
    while (--i > 0) /* Don't zero out data[0] */
      data[i] = 0;
  }
  pop( & operandstack ) ;
  return TRUE ;
}

Bool currentgstatetagdict_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push( & gstateptr->theGSTAGinfo.dict , & operandstack ) ;
}

static Bool get_name( OBJECT *source, NAMECACHE **dest )
{
  if ( oType(*source) != ONAME )
    return error_handler( TYPECHECK ) ;
  *dest = oName(*source) ;
  return TRUE ;
}

static Bool get_tag_info(DL_STATE *page, NAMECACHE *block_name, NAMECACHE *tag_name,
                         TAG_INFO **ptag)
{
  GSTAGSTRUCTUREOBJECT *gts ;
  int i, lim ;

  gts = gstateptr->theGSTAGinfo.structure ;

  if (gts == NULL) {
    if ( !make_gstagstructureobject(page, &gstateptr->theGSTAGinfo.dict,
                                    &gstateptr->theGSTAGinfo.structure) ) {
      HQFAIL( "Nasty tricks can cause this but a bug is more likely" ) ;
      return FALSE ;
    }

    gts = gstateptr->theGSTAGinfo.structure ;

    if ( gts == NULL )
      return error_handler( UNDEFINED ) ;
  }

  lim = gts->num_blocks + gts->num_tags ;
  for ( i = gts->num_blocks; i < lim; i++ ) {
    if ( gts->tags[i].tag.tag_name == tag_name && gts->tags[i].tag.block_name == block_name ) {
      *ptag =  & gts->tags[i].tag ;
      return TRUE ;
    }
  }
  return error_handler( UNDEFINED ) ;
}


Bool setgstatetag_(ps_context_t *pscontext)
{
  DL_STATE *page = ps_core_context(pscontext)->page;
  NAMECACHE *block_name = NULL, *tag_name = NULL ;
  TAG_INFO *tag = NULL ;
  OBJECT *theo ;
  int32 bit_offset ;
  int32 bit_width  ;
  uint8 *pc ;
  uint8 sim_string[4] ;
  uint32 *data = gstateptr->theGSTAGinfo.data ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 2 )
    return error_handler( STACKUNDERFLOW ) ;

  if ( ! get_name(stackindex( 2 , & operandstack ), &block_name ) ||
       ! get_name(stackindex( 1 , & operandstack ), &tag_name ) ||
       ! get_tag_info(page, block_name, tag_name, &tag) )
    return FALSE ;

  /* Characters within a string passed to 'show' or other text showing operators
     are often held in a single display list object as an optimisation; this can
     cause any changes in gstatetags to be lost as they are held in the list
     object. Terminate any active composite character construction to ensure
     that any following characters will get their own display list object. */
  if ( !finishaddchardisplay(page, 1) )
    return FALSE ;

  /* Got the match */
  theo = theTop( operandstack ) ;

  bit_offset = tag->bit_offset ;
  bit_width = tag->bit_width ;
  if ( bit_width > 32 ) { /* It's a string */
    bit_width -= 32 ; /* Remove "string" marker to get real width */
    if ( oType(*theo) != OSTRING )
      return error_handler( TYPECHECK ) ;
    if ( theLen(*theo) != ( ( bit_width + 7 ) >> 3 ) )
      return error_handler( RANGECHECK ) ;
    pc = oString(*theo) ;
    /* Check that the last character fits */
    if ( (bit_width & 7) != 0 )
      if ( (pc[bit_width >> 3] & (0xff >> (bit_width & 7))) != 0 )
        return error_handler( RANGECHECK ) ;
  } else {
    uint32 ui = (uint32) oInteger(*theo) ;

    if ( oType(*theo) != OINTEGER )
      return error_handler( TYPECHECK ) ;
    if ( bit_width < 32 && (ui & (0xffffffff << bit_width)) != 0 )
      return error_handler( RANGECHECK ) ;
    ui <<= 32 - bit_width ; /* Shift into to high bits */
    sim_string[0] = (uint8)(ui >> 24) ;
    sim_string[1] = (uint8)(ui >> 16) ;
    sim_string[2] = (uint8)(ui >> 8) ;
    sim_string[3] = (uint8)ui ;
    pc = sim_string ;
  }

  while ( bit_width > 0 ) {
    uint32 *datap = & data[bit_offset >> 5] ;
    uint32 mask_width = bit_width >= 8 ? 8 : bit_width ;
    uint32 mask = ~(0xff >> mask_width) ;
    uint32 bitshift = (bit_offset & 31) ;

    HQASSERT( (*pc & ~mask) == 0, "Check that last word is the right length failed" ) ;
    datap[0] &= ~((uint32)(mask << 24) >> bitshift) ;
    datap[0] |= (uint32)(*pc << 24) >> bitshift ;
    if ( mask_width + bitshift > 32 ) { /* 32 - bitshift is num bits used */
      HQASSERT(24 + 32 - bitshift < 32, "Shifted too far") ;
      datap[1] &= ~(mask << (24 + 32 - bitshift)) ;
      datap[1] |= (*pc << (24 + 32 - bitshift)) ;
    }
    bit_offset += 8; bit_width -= 8; pc++ ;
  }
  npop( 3, & operandstack ) ;
  return TRUE ;
}

Bool currentgstatetag_(ps_context_t *pscontext)
{
  DL_STATE *page = ps_core_context(pscontext)->page;
  NAMECACHE *block_name = NULL, *tag_name = NULL ;
  TAG_INFO *tag = NULL ;
  int32 bit_offset ;
  int32 bit_width , len ;
  uint8 sim_string[4] ;
  uint8 *pc, *opc ;
  uint32 *data = gstateptr->theGSTAGinfo.data ;
  corecontext_t *corecontext = pscontext->corecontext ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  if ( ! get_name(stackindex( 1 , & operandstack ), &block_name ) ||
       ! get_name( theTop( operandstack ) , &tag_name ) ||
       ! get_tag_info(page, block_name, tag_name, &tag) )
    return FALSE ;

  bit_offset = tag->bit_offset ;
  bit_width = tag->bit_width ;
  if ( bit_width > 32 ) { /* It's a string */
    bit_width -= 32 ;  /* Remove "string" marker to get real width */
    len = (( bit_width + 7 ) >> 3 ) ;
    opc = get_smemory( len ) ;
    if ( opc == NULL )
      return error_handler( VMERROR ) ;
  } else {
    len = (( bit_width + 7 ) >> 3 ) ;
    opc = sim_string ;
    sim_string[0] = sim_string[1] = sim_string[2] = sim_string[3] = 0 ;
  }

  pc = opc ;
  while ( bit_width > 0 ) {
    uint32 *datap = & data[bit_offset >> 5] ;
    uint32 mask_width = bit_width >= 8 ? 8 : bit_width ;
    uint32 mask = ~(0xff >> mask_width) ;
    int32 bitshift = (bit_offset & 31) ;

    *pc = (uint8)(((uint32)(datap[0] << bitshift) >> 24) & mask) ;
    if ( mask_width + bitshift > 32 ) { /* 32 - bitshift is num bits used */
      *pc = (uint8)(*pc | (mask & (datap[1] >> (24 + 32 - bitshift)))) ;
    }
    bit_offset += 8; bit_width -= 8; pc++ ;
  }

  if ( opc != sim_string ) { /* It's a real string */
    OBJECT ostring = OBJECT_NOTVM_NOTHING ;

    theTags(ostring) = OSTRING|LITERAL|UNLIMITED ;
    theLen(ostring) = CAST_TO_UINT16(len) ;
    oString(ostring) = opc ;
    SETGLOBJECT(ostring, corecontext) ;
    Copy(theTop(operandstack), &ostring) ;
  } else {
    uint32 ui = ((sim_string[0] << 24 ) | (sim_string[1] << 16) |
                 (sim_string[2] << 8) | sim_string[3]) ;
    oInteger( inewobj ) = (int32)(ui >> (32 - tag->bit_width)) ;
    pop( & operandstack ) ;
    Copy(theTop(operandstack), &inewobj) ;
  }
  return TRUE ;
}


/* --GSTAGSTRUCTUREOBJECT store methods-- */


/* Destructor.
*/
void gsTagDelete(DlSSEntry* entry, mm_pool_t *pools)
{
  GSTAGSTRUCTUREOBJECT* self = (GSTAGSTRUCTUREOBJECT*)entry;

  if (self != NULL)
    dl_free(pools, self, gs_size(self->num_blocks, self->num_tags),
            MM_ALLOC_CLASS_GSTAGS_CACHE);
}


/* Hash function.

Note that we use a desperately simple hash for the GSTAGSTRUCTUREOBJECT: it's
just the size in words of the data attached to the GSTATE.  In practice we
anticipate that jobs that use more than one GSTAGSTRUCTUREOBJECT will be rare,
and where they exist one is likely to be larger than the other. If this turns
out to be wrong we'll have to change it - paulc
*/
uintptr_t gsTagHash(DlSSEntry* entry)
{
  GSTAGSTRUCTUREOBJECT* self = (GSTAGSTRUCTUREOBJECT*)entry;

  HQASSERT(self != NULL, "gsTagHash - 'self' cannot be NULL");

  return self->alloc_words;
}


/* Are the passed GSTAGSTRUCTUREOBJECTs identical?
*/
Bool gsTagSame(DlSSEntry* entryA, DlSSEntry* entryB)
{
  GSTAGSTRUCTUREOBJECT* t1 = (GSTAGSTRUCTUREOBJECT*)entryA;
  GSTAGSTRUCTUREOBJECT* t2 = (GSTAGSTRUCTUREOBJECT*)entryB;
  int32 i, lim ;

  HQASSERT((t1 != NULL) && (t2 != NULL),
           "gsTagSame - parameters cannot be NULL");

  if ( t1->num_blocks != t2->num_blocks ||
       t1->num_dl_blocks != t2->num_dl_blocks ||
       t1->num_tags != t2->num_tags ||
       t1->dl_words != t2->dl_words ||
       t1->alloc_words != t2->alloc_words)
    return FALSE ;

#define FIELDS_DIFFER(field) (t1->tags[i].field != t2->tags[i].field)
  lim = t1->num_blocks ;
  for ( i = 0; i < lim; i++ ) {
    if ( FIELDS_DIFFER( block.block_name ) || FIELDS_DIFFER( block.type_num) ||
        FIELDS_DIFFER( block.data_words ) ||
        FIELDS_DIFFER( block.data_woffset ) )
      return FALSE ;
  }
  lim += t1->num_tags ;
  for ( ; i < lim ; i++ ) {
    if ( FIELDS_DIFFER( tag.block_name ) || FIELDS_DIFFER( tag.tag_name ) ||
        FIELDS_DIFFER( tag.bit_offset ) || FIELDS_DIFFER( tag.bit_width ) )
      return FALSE ;
  }
#undef FIELDS_DIFFER

  return TRUE ;
}

/* Log stripped */
