/** \file
 * \ingroup rleblt
 *
 * $HopeName: SWv20!src:rleColorantMapping.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Interfaces for RLE colorant mapping. Colorants on the display list need to
 * be mapped to the set of colorants output in the PGB (in color RLE we output
 * composite data).
 *
 * This file implements this behavior for both opaque and transparency jobs;
 * in the latter case the mapping must be updated as the current raster style
 * (i.e. current group) changes during rendering.
 */

#include "core.h"
#include "rleColorantMapping.h"

#include "gu_chan.h"
#include "hdlPrivate.h"
#include "group.h"
#include "groupPrivate.h"
#include "monitor.h"
#include "swerrors.h"
#include "mm.h"
#include "swrle.h"
#include "display.h"
#include "objnamer.h" /* OBJECT_NAME_MEMBER */
#include "hqmemset.h"


/** \brief Add a colorant to a list, if it is unique.

    \param list     The list to add the colorant to.
    \param colorant A colorant to add to the list.

    \retval TRUE  If the colorant already existed on the list, or was added
                  successfully.
    \retval FALSE If an error occurred.

    \note The colorant list stores a reference to the \a colorant parameter,
    it is the caller's responsibility to ensure that the colorant list is
    destroyed before the colorant reference goes out of scope or is
    deallocated. */
static Bool colorantListAdd(ColorantList* list,
                            const GUCR_COLORANT_INFO* colorant);

/* --Private macros-- */

#define COLORANTLIST_NAME "ColorantList"
#define RLECOLORANTMAP_NAME "RleColorantMap"


/* --Private types-- */

/** This structure is used to hold a single colorant info structure as part of a
linked list. */
struct ColorantEntry {
  const GUCR_COLORANT_INFO* colorant;
  struct ColorantEntry* next;
};

/** This structure holds the master list of all colorants known on a given page,
including colorants that are not output on the device.

The device colorants will be in a contigous block at the start of the list, the
remaining colorants being 'virtual' - i.e. not on the output device and only
used within transparency groups.
*/
struct ColorantList {
  mm_pool_t pool ;

  ColorantEntry *head;
  ColorantEntry **tail;

  /** The number of device colorants in the list; these will be before any
      virtual colorants in the list. This is initially set to -1, indicating
      that all colorants are device colorants. */
  int32 totalDeviceColorants;

  OBJECT_NAME_MEMBER
};

/** This structure defines a full set of mappings from the current set of DL
color colorant indices to the set of colorants listed in a PGB header.
*/
struct RleColorantMap {
  /* The pools from which memory for this object was allocated. */
  mm_pool_t *pools;

  /* The allocated size of the table. */
  uint32 tableSize;

  /* The mapping array; this is indexed by colorant indices. */
  uint16* mapping;

  /* This is the channel which unknown colorant indices map to. */
  uint16 unknownColorantChannel;

  OBJECT_NAME_MEMBER
};


/* --Private methods-- */

#ifdef DEBUG_BUILD

/** Dump the colorants in the passed list to the monitor.
*/
void debug_print_colorantlist(ColorantList* list)
{
  ColorantEntry* scan = list->head;
  int32 count = 0;

  monitorf((uint8*)"Colorants:\n");

  monitorf((uint8*)"  Device Colorants:\n");
  while (scan != NULL && (list->totalDeviceColorants == -1 ||
                          count < list->totalDeviceColorants)) {
    NAMECACHE* name = scan->colorant->name;

    monitorf((uint8*)"    %.*s\n", name->len, name->clist);

    scan = scan->next;
    count ++;
  }

  monitorf((uint8*)"  Virtual Colorants:\n");
  while (scan != NULL) {
    NAMECACHE* name = scan->colorant->name;

    monitorf((uint8*)"    %.*s\n", name->len, name->clist);

    scan = scan->next;
  }
}
#endif

/** Search the list starting with 'head' for a colorant with the passed name. If
it is present, the number of links traversed to reach it is returned - i.e. if
'head' contains the named colorant, zero is returned. Returns -1 if the entry is
not present in the list.
*/
static int32 getNamedColorant(ColorantList* list, NAMECACHE* name)
{
  ColorantEntry* scan = list->head;
  uint32 position = 0;

  while (scan != NULL) {
    /* NAMECACHE entries are unique - the same name is never duplicated in the
    cache; therefore two identical colorant names will share the same namecache
    entry, making pointer comparision a sufficient test for equality. */
    if (scan->colorant->name == name)
      return position;

    scan = scan->next;
    position ++;
  }

  return -1;
}

Bool colorantListAddFromRasterStyle(ColorantList *list,
                                    GUCR_CHANNEL *channel,
                                    Bool sheet_only)
{
  HQASSERT(list != NULL, "No colorant list") ;
  HQASSERT(channel != NULL, "No channel") ;
  HQASSERT(gucr_framesStartOfSheet(channel, NULL, NULL),
           "Channel is not at the start of a sheet") ;

  /* Iterate over all colorants in the frames and channels in the raster style. */
  do {
    GUCR_COLORANT *colorant;

    for (colorant = gucr_colorantsStart(channel);
         gucr_colorantsMore(colorant, GUCR_INCLUDING_PIXEL_INTERLEAVED);
         gucr_colorantsNext(&colorant)) {
      const GUCR_COLORANT_INFO *info;

      /* Get the colorant info. This function will return FALSE if the
         colorant is not renderable, or unknown, in which case we don't add
         it to the list. */
      if (gucr_colorantDescription(colorant, &info)) {
        /* Only add the colorant if it is not already present. */
        if (! colorantListAdd(list, info) )
          return FALSE;
      }
    }

    gucr_framesNext(&channel) ;
  } while ( gucr_framesMore(channel) &&
            !(sheet_only && gucr_framesStartOfSheet(channel, NULL, NULL)) ) ;

  return TRUE;
}

/** Populate 'map', creating mappings for all of the valid and renderable
colorant indices in the passed raster style ('rs') to the matching colorants in
the passed 'list'.
*/
static Bool populateMapFromRasterStyle(RleColorantMap* map,
                                       GUCR_RASTERSTYLE* rs,
                                       ColorantList* list)
{
  GUCR_CHANNEL* channel;
  GUCR_COLORANT* colorant;
  const GUCR_COLORANT_INFO *info;

  /* Iterate over all colorants in the frames and channels in the raster style. */
  for (channel = gucr_framesStart(rs);
       gucr_framesMore(channel);
       gucr_framesNext(&channel)) {
    for (colorant = gucr_colorantsStart(channel);
         gucr_colorantsMore(colorant, GUCR_INCLUDING_PIXEL_INTERLEAVED);
         gucr_colorantsNext(&colorant)) {

      /* Get the colorant info. This function will return FALSE if the
         colorant is not renderable, or unknown, in which case we don't add it
         to the list. */
      if (gucr_colorantDescription(colorant, &info)) {
        int32 channelIndex = getNamedColorant(list, info->name);

        /* It is possible to have colorants that are not on the device but
           are in a raster style; this occurs when separating or omitting
           separations. If this occurs we just don't add any mapping for this
           channel; should the channel be encountered during rendering it
           will be given the special unknown colorant index. */
        if (channelIndex != -1) {
          COLORANTINDEX ci = info->colorantIndex;

          HQASSERT(ci < (int32)map->tableSize,
                   "populateMapFromRasterStyle - colorant index too large.");

          /** \todo ajcd 2012-11-17: We currently don't support duplicate
              mappings used for CT/LW. It's a bit late to find this out now,
              we should have detected it earlier, or we should fix the lookup
              to handle multiple mappings. */
          if ( map->mapping[ci] != map->unknownColorantChannel )
            return detail_error_handler(CONFIGURATIONERROR,
                                        "Duplicate color channels not supported in RLE output") ;

          map->mapping[ci] = (uint16)channelIndex;
        }
      }
    }
  }

  return TRUE ;
}

/** Return the number of entries in the passed list.
*/
static int32 colorantListSize(ColorantList* self)
{
  uint32 count = 0;
  ColorantEntry* scan;

  HQASSERT(self != NULL, "colorantListSize - 'self' cannot be NULL");
  scan = self->head;
  while (scan != NULL) {
    count ++;
    scan = scan->next;
  }
  return count;
}

/* --Public methods-- */

ColorantList *colorantListNew(mm_pool_t pool)
{
  ColorantList* self;

  HQASSERT(pool != NULL, "No MM pool for colorant list") ;

  /* Allocate the list structure. */
  self = mm_alloc(pool, sizeof(ColorantList), MM_ALLOC_CLASS_RLE_COLORANT_MAP);
  if (self == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  /* Clear the structure (NULL any pointers, zero integer values, etc). */
  HqMemZero(self, sizeof(ColorantList));
  self->pool = pool ;
  self->tail = &self->head ;
  /* Set the device colorant count to -1 to indicate that all colorants are
     device colorants. */
  self->totalDeviceColorants = -1;
  NAME_OBJECT(self, COLORANTLIST_NAME);

  return self;
}

static Bool colorantListAdd(ColorantList* list,
                            const GUCR_COLORANT_INFO* colorant)
{
  ColorantEntry* newEntry;

  VERIFY_OBJECT(list, COLORANTLIST_NAME);
  HQASSERT(list->tail != NULL || *(list->tail) == NULL,
           "colorantListAdd - tail cannot contain a valid next entry.");
  HQASSERT(colorant != NULL, "Colorant cannot be NULL.");
  HQASSERT(colorant->name != NULL, "colorantListAdd - 'colorant' has no name.");

  if (getNamedColorant(list, colorant->name) != -1) {
    /* The colorant is already present - ignore it. */
    return TRUE;
  }

  newEntry = mm_alloc(list->pool, sizeof(ColorantEntry),
                      MM_ALLOC_CLASS_RLE_COLORANT_MAP);
  if (newEntry == NULL)
    return error_handler(VMERROR);

  newEntry->colorant = colorant;
  newEntry->next = NULL;

  *(list->tail) = newEntry ;
  list->tail = &newEntry->next ;

  return TRUE;
}

void colorantListTerminateDeviceColorants(ColorantList *colorants)
{
  VERIFY_OBJECT(colorants, COLORANTLIST_NAME);

  /* Mark the existing colorants as being the device colorants. */
  HQASSERT(colorants->totalDeviceColorants < 0,
           "Device colorant list already terminated") ;
  colorants->totalDeviceColorants = colorantListSize(colorants);
}

const GUCR_COLORANT_INFO* colorantListGetFirst(ColorantList* self,
                                               ColorantListIterator* iterator,
                                               Bool* groupColorant)
{
  VERIFY_OBJECT(self, COLORANTLIST_NAME);
  HQASSERT(iterator != NULL, "No colorant list iterator") ;

  iterator->list = self;
  iterator->next = &self->head;
  iterator->index = 0;

  return colorantListGetNext(iterator, groupColorant) ;
}

const GUCR_COLORANT_INFO* colorantListGetNext(ColorantListIterator* iterator,
                                              Bool* groupColorant)
{
  ColorantEntry *entry ;

  HQASSERT(iterator != NULL, "No colorant list iterator.");
  HQASSERT(iterator->list != NULL, "No colorant list in iterator.");

  if ( (entry = *iterator->next) == NULL )
    return NULL ;

  /* Will the returned colorant be a group or device colorant? */
  if (groupColorant != NULL) {
    if (iterator->list->totalDeviceColorants != -1 &&
        iterator->index >= iterator->list->totalDeviceColorants)
      *groupColorant = TRUE;
    else
      *groupColorant = FALSE;
  }

  iterator->next = &entry->next ;
  iterator->index++;

  return entry->colorant;
}

void colorantListDestroy(ColorantList** selfPointer)
{
  ColorantList* self = *selfPointer;
  ColorantEntry* scan;

  VERIFY_OBJECT(self, COLORANTLIST_NAME);

  scan = self->head;
  while (scan != NULL) {
    scan = scan->next;
    mm_free(self->pool, self->head, sizeof(ColorantEntry));
    self->head = scan;
  }

  mm_free(self->pool, self, sizeof(ColorantList));
  *selfPointer = NULL;
}

RleColorantMap* rleColorantMapNew(mm_pool_t *pools,
                                  GUCR_RASTERSTYLE* rs,
                                  ColorantList* colorants)
{
  RleColorantMap* self;
  uint32 tableSize;
  uint32 i;

  HQASSERT(rs != NULL && colorants != NULL, "parameters cannot be NULL.");

  /* The table must be large enough to be indexed by the highest colorant
  index. */
  tableSize = guc_getHighestColorantIndexInRasterStyle(rs, FALSE) + 1;

  self = dl_alloc(pools, sizeof(RleColorantMap) + (sizeof(int16) * tableSize),
                  MM_ALLOC_CLASS_RLE_COLORANT_MAP);
  if (self == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  /* Clear the structure (NULL any pointers, zero integer values, etc). */
  HqMemZero(self, sizeof(RleColorantMap));
  NAME_OBJECT(self, RLECOLORANTMAP_NAME);

  self->pools = pools;
  self->mapping = (uint16*)(self + 1);
  self->tableSize = tableSize;

  /* Colorant indices from colorants not on the raster style map to 1 + the
  number of colorants in the PGB. */
  self->unknownColorantChannel = (uint16)colorantListSize(colorants);

  /* Initialise the mappings to the unknown colorant. */
  for (i = 0; i < tableSize; i ++)
    self->mapping[i] = self->unknownColorantChannel;

  /* Populate the table. */
  if ( !populateMapFromRasterStyle(self, rs, colorants) )
    rleColorantMapDestroy(&self) ;

  return self;
}

void rleColorantMapDestroy(RleColorantMap** selfPointer)
{
  RleColorantMap* self = *selfPointer;

  VERIFY_OBJECT(self, RLECOLORANTMAP_NAME);
  UNNAME_OBJECT(self);

  dl_free(self->pools, self, sizeof(RleColorantMap) +
          (sizeof(uint16) * self->tableSize), MM_ALLOC_CLASS_RLE_COLORANT_MAP);
  *selfPointer = NULL;
}

Bool rleColorantMapGet(RleColorantMap* self, COLORANTINDEX ci, uint32* result)
{

  VERIFY_OBJECT(self, RLECOLORANTMAP_NAME);
  HQASSERT(result != NULL, "rleColorantMapGet - 'result' cannot be NULL.");

  /* If the colorant index is a special one, return the unknown colorant
     channel. */
  if (ci < 0 || ci >= (int32)self->tableSize) {
    *result = self->unknownColorantChannel;
    return FALSE;
  }

  *result = self->mapping[ci];
  return (self->mapping[ci] != self->unknownColorantChannel);
}

/* Log stripped */

