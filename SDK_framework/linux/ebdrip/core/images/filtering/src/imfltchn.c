/** \file
 * \ingroup imgfilter
 *
 * $HopeName: COREimgfilter!src:imfltchn.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2003-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Manages an image filter chain
 */

#include "core.h"
#include "imfltchn.h"

#include "objnamer.h"
#include "imagfltr.h"
#include "datatrns.h"
#include "mm.h"
#include "psvm.h"
#include "objects.h"
#include "namedef_.h"
#include "stacks.h"
#include "dicthash.h"
#include "dictscan.h"
#include "miscops.h"
#include "stdarg.h"
#include "swcopyf.h"
#include "stackops.h"
#include "intrpltr.h"
#include "mskscalr.h"
#include "avrag_if.h"
#include "alphaDiv.h"
#include "decimate.h"
#include "smooth.h"
#include "swerrors.h"
#include "utils.h"

/* --Private macros-- */

#define IMAGEFILTERCHAIN_NAME "Imagefilter chain"

/* --Private datatypes-- */

/* Status of each link in the filter chain */
typedef enum LinkState_s {
  LinkDead = 1, /* The link does not have a valid image filter */
  LinkInvalid,  /* The link is not valid (the filter rejected the image) */
  LinkReady,    /* The link is ready to read data */
  LinkPending,  /* The link has data waiting for it */
  LinkLocked,   /* The link's output is in use */
  LinkStalled   /* The link's output is waiting for the target link to become
                ready */
}LinkState;

/* Type of link */
typedef enum LinkType_s {
  LinkWithTransaction = 1,
  LinkWithoutTransaction
}LinkType;

/* A single link in the filter chain */
typedef struct ChainLink_s {
  uint8* stalledData;
  uint32 stalledSize;
  uint32 type;
  uint32 top;    /* Two-deep stack for state */
  uint32 bottom; /* */
  DataTransaction *transaction;
  ImageFilter *filter;
  struct ChainLink_s *previous;
  struct ChainLink_s *next;
  OBJECT_NAME_MEMBER
}ChainLink;

/* Filter chain manager */
struct ImageFilterChain_s {
  uint32 filterCount;
  uint32 interpolatorCount;
  uint32 filterStartIndex;
  size_t chainSize;
  ChainLink **chain;
  ChainLink *firstActiveFilter;
  ChainLink *lastActiveFilter;
  OBJECT_NAME_MEMBER
};

/* --Private variables-- */

static ImageFilterChain *globalImageChain = NULL;
static ImageFilterChain *globalMaskChain = NULL;

/* --Private prototypes-- */

static void rescaleImageTransform(OMATRIX* transform,
                                  uint32 oldWidth,
                                  uint32 oldHeight,
                                  uint32 newWidth,
                                  uint32 newHeight);

/* --ChainLink prototypes-- */

static ChainLink* chainLinkNew(uint32 imageFilterName);
static ImageFilter* chainLinkGetFilterInterface(uint32 filterName);
static void chainLinkDelete(ChainLink* self);
static void chainLinkReset(ChainLink* self);
static Bool chainLinkSetType(ChainLink* self, uint32 newType);
static uint32 chainLinkState(ChainLink* self);
static void chainLinkSetState(ChainLink* self, uint32 newTop);
static void chainLinkPushState(ChainLink* self, uint32 newTop);
static void chainLinkPopState(ChainLink* self);
static void chainLinkNewData(ChainLink* self, uint32 dataLength, uint8* data);
static void chainLinkNewTransaction(ChainLink* self,
                                    DataTransaction* newTransaction);
static void chainLinkStalledData(ChainLink* self,
                                 uint32 dataLength,
                                 uint8* data);

/* --ImageFilterChain-- */

/* --Public functions-- */

/* Image filter chain accessor function.
 */
extern int32 imageFilterChainImage(ImageFilterChain** chainOut)
{
  if (globalImageChain == NULL) {
    /* The order of filters is significant. rescaleImageType12() in pdfimg.c
       assumes a possible sequence of interpolation, averaging, decimation and
       then alpha div. */
    uint32 interpolatorList[] = {
      NAME_InterpolateImageFilter,
      NAME_AverageImageFilter};
    uint32 imageFilterList[] = {
      NAME_DecimateImageFilter,
      NAME_AlphaDivideImageFilter,
      NAME_SmoothImageFilter};

    globalImageChain = imageFilterChainNew(NUM_ARRAY_ITEMS(interpolatorList),
                                           interpolatorList,
                                           NUM_ARRAY_ITEMS(imageFilterList),
                                           imageFilterList);
    if (globalImageChain == NULL)
      return FALSE;
  }
  *chainOut = globalImageChain;
  return TRUE;
}

/* Mask filter chain accessor function.
 */
extern int32 imageFilterChainMask(ImageFilterChain** chainOut)
{
  if (globalMaskChain == NULL) {
    /* The interpolate and average image filters are present in the mask filter
       list in support of contone mask images (such as AlphaMasks), where the
       mask can be any bitdepth. This doesn't affect the behavior of regular
       image masks, because the image filters simply reject binary mask data
       (just as the mask filters reject any contone mask data). */

    /* The order of filters is significant. rescaleImageType12() in pdfimg.c
       assumes a possible sequence of interpolation, averaging, decimation and
       then alpha div. */
    uint32 interpolatorList[] = {
      NAME_ScaleMaskFilter,
      NAME_ScaleMaskFilter,
      NAME_ScaleMaskFilter,
      NAME_InterpolateImageFilter,
      NAME_AverageImageFilter};
    uint32 maskFilterList[] = {
      NAME_DecimateImageFilter,
      NAME_AlphaDivideMaskFilter};

    globalMaskChain = imageFilterChainNew(NUM_ARRAY_ITEMS(interpolatorList),
                                          interpolatorList,
                                          NUM_ARRAY_ITEMS(maskFilterList),
                                          maskFilterList);

    if (globalMaskChain == NULL)
      return FALSE;
  }
  *chainOut = globalMaskChain;
  return TRUE;
}

/* --Public methods-- */

/* Constructor. There are two stages to a filter chain - those filters which
always applied (as specified by the "filterNames" array), and those filters
which are activated only when the "interpolate" flag is true in an image
dictionary (as specified by "interpolatorNames" array).
*/
ImageFilterChain* imageFilterChainNew(uint32 interpolatorCount,
                                      uint32* interpolatorNames,
                                      uint32 filterCount,
                                      uint32* filterNames)

{
  ImageFilterChain *self;
  uint32 i, allocError = FALSE;

  self = (ImageFilterChain*) mm_alloc(mm_pool_temp, sizeof(ImageFilterChain),
                                      MM_ALLOC_CLASS_IMAGE_FILTER_CHAIN);
  if (self == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }

  /* NULL pointer members */
  self->filterCount = filterCount;
  self->interpolatorCount = interpolatorCount;
  self->filterStartIndex = 0;
  self->chainSize = sizeof(ChainLink*) * (self->interpolatorCount +
                                          self->filterCount);
  self->chain = NULL;
  self->firstActiveFilter = self->lastActiveFilter = NULL;
  NAME_OBJECT(self, IMAGEFILTERCHAIN_NAME);

  HQASSERT(self->chainSize > 0, "imageFilterChainNew - Chain has no filters");
  self->chain = mm_alloc(mm_pool_temp, self->chainSize,
                         MM_ALLOC_CLASS_IMAGE_FILTER_CHAIN);

  if ( self->chain == NULL ) {
    imageFilterChainDelete(self);
    (void)error_handler(VMERROR);
    return NULL;
  }

  /* Init each link to NULL incase alloc fails */
  for ( i = 0; i < (self->interpolatorCount + self->filterCount); ++i ) {
    self->chain[i] = NULL;
  }

  /* Alloc each link of the chain with the correct filter */
  for ( i = 0; i < self->interpolatorCount && !allocError; ++i ) {
    self->chain[i] = chainLinkNew(interpolatorNames[i]);

    if (self->chain[i] == NULL)
      allocError = TRUE;
  }

  /* Alloc a set of interpolator links */
  for ( i = 0; i < self->filterCount && !allocError ; ++i ) {
    self->chain[i + self->interpolatorCount] = chainLinkNew(filterNames[i]);

    if (self->chain[i + self->interpolatorCount] == NULL)
      allocError = TRUE;
  }

  if ( allocError ) {
    /* Unsuccessful initialisation */
    imageFilterChainDelete(self);
    return NULL;
  }

  return self;
}

/* Destructor
 */
void imageFilterChainDelete(ImageFilterChain* self)
{
  uint32 i;

  if (self == NULL)
    return;

  VERIFY_OBJECT(self, IMAGEFILTERCHAIN_NAME);
  UNNAME_OBJECT(self);

  if (self->chain != NULL) {
    for ( i = 0; i < self->interpolatorCount + self->filterCount; ++i ) {
      chainLinkDelete(self->chain[i]);
    }
    mm_free(mm_pool_temp, self->chain, self->chainSize);
  }

  mm_free(mm_pool_temp, self, sizeof(ImageFilterChain));
}

/* Are the any filters in this chain (not including interpolators)? */
uint32 imageFilterChainEmpty(ImageFilterChain* self)
{
  VERIFY_OBJECT(self, IMAGEFILTERCHAIN_NAME);

  if (self->filterCount == 0)
    return TRUE;
  else
    return FALSE;
}

/* Query method to quickly determin if the passed chain would filter the passed
image.
No filters are actually instantiated, and as soon as any filter accepts the
image 'filteringActive' is set to TRUE. If no filters accepted the image,
'filteringActive' will be FALSE.
Returns FALSE on error; although no allocation is directly performed by this
method, it is still possible for a filter present method to fail.
*/
Bool imageFilterChainCheckImage(ImageFilterChain* self,
                                FilteredImageArgs* args,
                                uint32 forceNoInterpolate,
                                Bool* filteringActive)
{
  uint32 i;
  Bool accepted;
  ChainLink *link;
  FilteredImageResult changes;
  FilteredImageResult previousImage;
  FilteredImageArgs current;

  VERIFY_OBJECT(self, IMAGEFILTERCHAIN_NAME);
  HQASSERT(args != NULL && filteringActive != NULL,
           "imageFilterChainCheckImage - parameters cannot be NULL");

  *filteringActive = FALSE;

  /* If the interpolate flag was present in the image specification, include
     the interpolate filters in the chain iteration. */
  if ( !forceNoInterpolate && args->interpolateFlag )
    self->filterStartIndex = 0;
  else
    self->filterStartIndex = self->interpolatorCount;

  current = *args;

  /* Present the image to each link in the image filtering chain until someone
     accepts the image. */
  filteredImageResultSet(previousImage, current.width, current.height,
                         current.bpc, current.interpolateFlag);
  for ( i = self->filterStartIndex;
        i < self->interpolatorCount + self->filterCount; ++i ) {
    link = self->chain[i];
    if (chainLinkState(link) != LinkDead) {
      accepted = FALSE;
      changes = previousImage;
      if (!imageFilterPresent(link->filter, &current, &accepted, &changes))
        /* Error in filter present - abort now */
        return FALSE;
      if (accepted) {
        /* As soon as a filter accepts the image, indicate that filtering would
        take place for this image, and return. */
        *filteringActive = TRUE;
        return TRUE;
      }
    }
  }

  return TRUE;
}

/* Present a new image to this filter chain. This will cause all filters that
 * accept the image to be created.
 * The 'totalFilters' parameter will be set the number of filters that accepted
 * the image; this can be zero if no filters accepted the image.
 * Returns FALSE on error.
 *
 * !!NOTE!!: Changes to this function may need to be reflected in
 * imageFilterChainCheckImage().
 */
Bool imageFilterChainNewImage(ImageFilterChain* self,
                              FilteredImageArgs* args,
                              uint32 forceNoInterpolate,
                              uint32* totalFilters)
{
  uint32 i;
  uint32 j;
  uint32 activeCount;
  Bool accepted;
  ChainLink *link;
  ChainLink *firstValidLink = NULL;
  ChainLink *lastValidLink = NULL;
  FilteredImageResult changes;
  FilteredImageResult previousImage;
  FilteredImageArgs current;
  OMATRIX tempTransform;

  VERIFY_OBJECT(self, IMAGEFILTERCHAIN_NAME);
  HQASSERT(args != NULL && totalFilters != NULL,
           "imageFilterChainNewImage - parameters cannot be NULL");

  *totalFilters = 0;

  /* If the interpolate flag was present in the image specification, include
     the interpolate filters in the chain iteration. */
  if ( !forceNoInterpolate && args->interpolateFlag )
    self->filterStartIndex = 0;
  else
    self->filterStartIndex = self->interpolatorCount;

  current = *args;

  filteredImageResultSet(previousImage, current.width, current.height,
                         current.bpc, current.interpolateFlag);
  for ( i = self->filterStartIndex;
        i < self->interpolatorCount + self->filterCount; ++i ) {
    link = self->chain[i];
    if (chainLinkState(link) != LinkDead) {
      accepted = FALSE;
      changes = previousImage;
      if (!imageFilterPresent(link->filter, &current, &accepted, &changes))
        /* Error in filter present - abort now */
        return FALSE;
      if (accepted) {
        if (imageFilterCreate(link->filter, &current, &changes)) {
          if (! filteredImageResultEqual(changes, previousImage)) {
            /* If the filter makes any changes to the image, change the
            'current' argument set for the next filter in the chain. */
            current.width = changes.width;
            current.height = changes.height;

            HQASSERT(changes.bpc == 1 || changes.bpc == 8 || changes.bpc == 16,
                     "imageFilterChainNewImage - bits per component changed to "
                     "unacceptable value.");
            current.bpc = changes.bpc;
            current.interpolateFlag = changes.interpolate;
          }
          chainLinkSetState(link, LinkReady);
          /* Adjust the image to device transform to reflect any size changes.
          This must be changed for each acceptance because it can be used by
          each filter to determin details about the image on the page. */
          rescaleImageTransform(&current.imageToDevice,
                                previousImage.width, previousImage.height,
                                current.width, current.height);
          filteredImageResultSet(previousImage, current.width, current.height,
                                 current.bpc, current.interpolateFlag);
        }
        else
          /* Error in filter creation, abort now */
          return FALSE;
      }
    }
  }
  /* Scale the image transform to make the on-page size the same, although
     it's actual dimensions may have changed. */
  tempTransform = args->imageTransform;
  rescaleImageTransform(&tempTransform, current.width, current.height,
                        args->width, args->height);

  /* Change the input args to reflect the output of the filter chain */
  *args = current;
  args->imageTransform = tempTransform;

  /* Connect each valid link in the chain to the next and previous valid
    links */
  activeCount = 0;
  for ( i = self->filterStartIndex;
        i < self->interpolatorCount + self->filterCount; ++i ) {
    link = self->chain[i];

    if (chainLinkState(link) == LinkReady) {
      activeCount ++;

      if (lastValidLink == NULL) {
        /* chainLinkSetType() cannot fail for LinkWithoutTransaction */
        (void)chainLinkSetType(link, LinkWithoutTransaction);
        firstValidLink = link;
      }
      else {
        if ( !chainLinkSetType(link, LinkWithTransaction) ) {
          imageFilterChainEndImage(self);
          return TRUE; /* Abort */
        }
      }

      link->previous = lastValidLink;
      lastValidLink = link;

      /* Init the next to null, then try to find another valid link in the
         remainder of the list */
      link->next = NULL;
      for ( j = i + 1; j < self->interpolatorCount + self->filterCount; ++j ) {
        if (chainLinkState(self->chain[j]) == LinkReady) {
          link->next = self->chain[j];
          break;
        }
      }
    }
  }
  self->firstActiveFilter = firstValidLink;
  self->lastActiveFilter = lastValidLink;

  *totalFilters = activeCount;
  return TRUE;
}

/* Destroy all filters in this chain
 */
void imageFilterChainEndImage(ImageFilterChain* self)
{
  uint32 i;

  VERIFY_OBJECT(self, IMAGEFILTERCHAIN_NAME);

  for ( i = self->filterStartIndex;
        i < self->interpolatorCount + self->filterCount; ++i ) {
    imageFilterDestroy(self->chain[i]->filter);
    chainLinkReset(self->chain[i]);
  }
}

/* Set the source of data for the chain. Data will be consumed from the
 * passed transaction during calls to imageFilterChainPush()
 */
void imageFilterChainSetDataSource(ImageFilterChain* self,
                                   DataTransaction* data)
{
  VERIFY_OBJECT(self, IMAGEFILTERCHAIN_NAME);

  HQASSERT(data != NULL,
           "imageFilterChainSetDataSource - 'data' parameter is NULL");
  HQASSERT(self->firstActiveFilter != NULL,
           "imageFilterChainSetDataSource - the chain is empty");

  chainLinkNewTransaction(self->firstActiveFilter, data);
}

/* Push some data at the filter chain
 */
void imageFilterChainPush(ImageFilterChain* self)
{
  uint8 *readyData;
  uint32 dataLength;
  ChainLink *link;

  VERIFY_OBJECT(self, IMAGEFILTERCHAIN_NAME);

  link = self->firstActiveFilter;

  HQASSERT(self->firstActiveFilter != NULL,
           "imageFilterChainPush - no filters in chain; caller will wait for "
           "ever");

  /* Write data into the head of the chain if possible */
  if ((chainLinkState(link) == LinkReady) &&
      (dataTransactionAvailable(self->firstActiveFilter->transaction) > 0)) {
    chainLinkSetState(link, LinkPending);
  }

  /* Process each link */
  do {
    if (chainLinkState(link) != LinkInvalid) {
      /* If self link is stalled, retry the target setup */
      if ((chainLinkState(link) == LinkStalled) &&
          (chainLinkState(link->next) == LinkReady)) {
        /* Lock ourselves, and set the data for the next link */
        chainLinkPopState(link);
        chainLinkPushState(link, LinkLocked);
        chainLinkNewData(link->next, link->stalledSize, link->stalledData);
        chainLinkSetState(link->next, LinkPending);
      }

      if (chainLinkState(link) == LinkReady) {
        /* Push the filter without any data - this allows it to do work in
           it's push method */
        imageFilterPush(link->filter, link->transaction);
      }
      else {
        /* If this link has data pending, try to push it at it */
        if (chainLinkState(link) == LinkPending) {
          imageFilterPush(link->filter, link->transaction);

          if (dataTransactionAvailable(link->transaction) == 0) {
            /* If all data consumed - this link is ready to take some more,
               and the previous can be unlocked */
            chainLinkSetState(link, LinkReady);
            if (link->previous != NULL) {
              HQASSERT(chainLinkState(link->previous) == LinkLocked,
                       "imageFilterChainPush - this link was pending, but "
                       "previous link was not locked");

              /* A lock is only ever set with a push - it allows the link to
                 have another state below the lock (it may have been in a pending
                 state when it was locked) */
              chainLinkPopState(link->previous);
            }
          }
        }
      }

      /* Only try to pull data if this is not the last link in the chain, and
         it's not locked or stalled */
      if ((link->next != NULL) && (chainLinkState(link) != LinkLocked) &&
        (chainLinkState(link) != LinkStalled)) {

        /* Pull from this link */
        dataLength = imageFilterPull(link->filter, &readyData);
        if (dataLength > 0) {
          if (chainLinkState(link->next) == LinkReady) {
            /* The target link is ready, so lock ourself, and set them to
               pending */
            chainLinkPushState(link, LinkLocked);
            chainLinkNewData(link->next, dataLength, readyData);
            chainLinkSetState(link->next, LinkPending);
          }
          else {
            /* The target link is not ready - enter stalled state and retry
               next cycle */
            chainLinkPushState(link, LinkStalled);
            chainLinkStalledData(link, dataLength, readyData);
          }
        }
      }
    }
    link = link->next;
  }while (link != NULL);
}

/* Try to pull some data from the filter chain.
 * NOTE: The buffer of data returned should be considered read-only, and the
 * caller must consume all returned data before pushing more data at the chain.
 */
uint32 imageFilterChainPull(ImageFilterChain* self, uint8** result)
{
  uint32 dataLength;

  VERIFY_OBJECT(self, IMAGEFILTERCHAIN_NAME);
  HQASSERT(self->lastActiveFilter != NULL,
           "imageFilterChainPull - last active filter is NULL");

  dataLength = imageFilterPull(self->lastActiveFilter->filter, result);
  return dataLength;
}

/* --Private methods-- */

/* Given old and new dimensions, adjust the passed image transform to account
 * for the change
 */
static void rescaleImageTransform(OMATRIX* transform,
                                  uint32 oldWidth,
                                  uint32 oldHeight,
                                  uint32 newWidth,
                                  uint32 newHeight)
{
  SYSTEMVALUE dw, dh;
  OMATRIX scaleMatrix, resultMatrix;

  HQASSERT(transform != NULL,
           "rescaleImageTransform - 'transform' parameter is NULL");

  dw = oldWidth / (SYSTEMVALUE)newWidth;
  dh = oldHeight / (SYSTEMVALUE)newHeight;

  scaleMatrix = identity_matrix;
  scaleMatrix.opt = MATRIX_OPT_0011;
  scaleMatrix.matrix[0][0] = dw;
  scaleMatrix.matrix[1][1] = dh;

  matrix_mult(transform, &scaleMatrix, &resultMatrix);
  transform[0] = resultMatrix;
}

/* --ChainLink-- */

/* --Private Macros-- */

#define FILTERCHAINLINK_NAME "Imagefilter chain link"

/* --Private methods-- */

/* Constructor
 */
static ChainLink* chainLinkNew(uint32 imageFilterName)
{
  ChainLink *self;

  self = (ChainLink*) mm_alloc(mm_pool_temp, sizeof(ChainLink),
                               MM_ALLOC_CLASS_IMAGE_FILTER_CHAIN_LINK);
  if (self == NULL) {
    (void)error_handler(VMERROR);
    return NULL;
  }
  NAME_OBJECT(self, FILTERCHAINLINK_NAME);

  /* NULL pointer members */
  self->stalledData = NULL;
  self->transaction = NULL;
  self->filter = NULL;
  self->previous = NULL;
  self->next = NULL;

  self->transaction = dataTransactionNewWithoutBuffer();
  if (self->transaction != NULL) { /* Was allocation successful? */

    self->type = LinkWithTransaction;
    self->filter = chainLinkGetFilterInterface(imageFilterName);
    if (self->filter != NULL) { /* Was allocation successful? */
      self->top = self->bottom = LinkInvalid;

      if (imageFilterDead(self->filter)) {
        self->top = LinkDead;
      }

      return self; /* Successful initialisation */
    }
  }

  /* Unsuccessful initialisation */
  chainLinkDelete(self);
  return NULL;
}

/* Obtain the interface structure for the named filter.
*/
static ImageFilter* chainLinkGetFilterInterface(uint32 filterName)
{
  ImageFilter* filter = NULL;

  switch (filterName) {

    case NAME_InterpolateImageFilter:
      filter = interpolatorExposeImageFilter();
      break;

    case NAME_AverageImageFilter:
      filter = averageIFExposeImageFilter();
      break;

    case NAME_ScaleMaskFilter:
      filter = maskScalerExposeImageFilter();
      break;

    case NAME_AlphaDivideImageFilter:
      filter = alphaDivExposeImageFilter();
      break;

    case NAME_AlphaDivideMaskFilter:
      filter = alphaDivExposeMaskFilter();
      break;

    case NAME_DecimateImageFilter:
      filter = decimateIFExposeImageFilter();
      break;

    case NAME_SmoothImageFilter:
      filter = smoothIFExposeImageFilter();
      break;

    default:
      HQFAIL("chainLinkGetFilterInterface - unknown filter name.");
      break;
  }

  return filter;
}

/* Destructor
 */
static void chainLinkDelete(ChainLink* self)
{
  if (self == NULL) {
    return;
  }
  VERIFY_OBJECT(self, FILTERCHAINLINK_NAME);
  UNNAME_OBJECT(self);

  if (self->type == LinkWithTransaction) {
    dataTransactionDelete(self->transaction);
  }

  imageFilterDelete(self->filter);

  mm_free(mm_pool_temp, self, sizeof(ChainLink));
}

/* Set the type - creating or deleting our DataTransaction as necessary.
 * Returns TRUE on success
 */
static Bool chainLinkSetType(ChainLink* self, uint32 newType)
{
  VERIFY_OBJECT(self, FILTERCHAINLINK_NAME);

  /* Can't reuse an old transaction because the fields may be invalid. */

  if ( self->type == LinkWithTransaction ) {
    dataTransactionDelete(self->transaction);
    self->transaction = NULL;
    self->type = LinkWithoutTransaction;
  }

  if ( newType == LinkWithTransaction ) {
    self->transaction = dataTransactionNewWithoutBuffer();
    if (self->transaction == NULL)
      return FALSE;
    self->type = newType;
  }

  return TRUE;
}

/* Reset this link to an initial state
 */
static void chainLinkReset(ChainLink* self)
{
  VERIFY_OBJECT(self, FILTERCHAINLINK_NAME);

  if (self->top != LinkDead) {
    self->bottom = self->top = LinkInvalid;
  }

  /* Kill the transaction in case it refers to an external buffer which has just
     been freed.  An external transaction (the LinkWithoutTransaction case)
     should be nulled too. */
  if ( self->type == LinkWithTransaction )
    dataTransactionDelete(self->transaction);
  self->transaction = NULL;
  self->type = LinkWithoutTransaction;
}

/* Get the state (top of the state stack) of this link
 */
static uint32 chainLinkState(ChainLink* self)
{
  VERIFY_OBJECT(self, FILTERCHAINLINK_NAME);

  return self->top;
}

/* Set the state (top of the state stack) of this link
 */
static void chainLinkSetState(ChainLink* self, uint32 newTop)
{
  VERIFY_OBJECT(self, FILTERCHAINLINK_NAME);

  self->top = newTop;
}

/* Push the state stack
 */
static void chainLinkPushState(ChainLink* self, uint32 newTop)
{
  VERIFY_OBJECT(self, FILTERCHAINLINK_NAME);
  HQASSERT(self->top != LinkInvalid, "chainLinkPushState - no state to push");
  HQASSERT(self->bottom == LinkInvalid, "chainLinkPushState - stack overflow");

  self->bottom = self->top;
  self->top = newTop;
}

/* Pop the state stack
 */
static void chainLinkPopState(ChainLink* self)
{
  VERIFY_OBJECT(self, FILTERCHAINLINK_NAME);
  HQASSERT(self->top != LinkInvalid, "chainLinkPopState - stack underflow");

  self->top = self->bottom;
  self->bottom = LinkInvalid;
}

/* Setup our transaction to work with new incomming data
 */
static void chainLinkNewData(ChainLink* self, uint32 dataLength, uint8* data)
{
  VERIFY_OBJECT(self, FILTERCHAINLINK_NAME);
  HQASSERT(self->top == LinkReady,
           "chainLinkNewData - trying to pass data to a link that is not "
           "ready");

  dataTransactionNewBuffer(self->transaction, data, dataLength, dataLength);
}

/* Set our transaction to the one passed
 */
static void chainLinkNewTransaction(ChainLink* self,
                                    DataTransaction* newTransaction)
{
  VERIFY_OBJECT(self, FILTERCHAINLINK_NAME);
  HQASSERT(self->type == LinkWithoutTransaction,
           "chainLinkNewTransaction - unable to accept new transactions");
  HQASSERT(self->top == LinkReady,
           "chainLinkNewTransaction - link is not in 'Ready' state");

  self->transaction = newTransaction;
}

/* Setup stalled data and length members, allowing a transaction to be setup
 * once the target link becomes available
 */
static void chainLinkStalledData(ChainLink* self,
                                 uint32 dataLength,
                                 uint8* data)
{
  VERIFY_OBJECT(self, FILTERCHAINLINK_NAME);

  self->stalledSize = dataLength;
  self->stalledData = data;
}

/* FilterParameters */

/* --Private prototypes-- */

static Bool filterParamsFill(OBJECT* params, FilteredImageArgs* args);
static Bool addInt(OBJECT* dict, int32 name, uint32 value);

/* --Public methods-- */

/* Run the PS procedure (specified by the passed "procName") and put
 * any results back into the passed "args". We don't really care if
 * the controller gets run or not - the filter just uses the passed 'args'
 * wether they get changed or not.
 */
Bool runFilterController(FilteredImageArgs* args,
                         uint32 procName,
                         Bool* disableFilter,
                         FilteredImageResult* changes,
                         Bool* changesMade)
{
  uint8 psBuf[320];
  ps_context_t *pscontext = get_core_context_interp()->pscontext ;

  HQASSERT(args != NULL && changes != NULL && disableFilter != NULL &&
           changesMade != NULL,
           "runFilterController - parameters cannot be NULL");

  /* Setup defaults. */
  *disableFilter = FALSE;
  *changesMade = FALSE;

  /* Mark the stack so we can cleartomark later */
  if (! mark_(pscontext))
    return FALSE;

  /* Get hold of the image filtering procset, and from that the image filtering
  parameter dictionary. Remove any result keys that may be left over from
  previous runs, and leave the parameter dictionary on top of the stack, with
  the procset below it. */
  if (! run_ps_string((uint8*)"/ImageFiltering /ProcSet findresource dup "
                      "/ProcParams get "
                      "[/DisableFilter /TargetWidth /TargetHeight] "
                      "{ dup 2 index exch known { 1 index exch undef } { pop } ifelse } "
                      "forall"))
    return FALSE;
  else {
    OBJECT paramDict = OBJECT_NOTVM_NOTHING ;

    OCopy(paramDict, *theTop(operandstack));

    /* Pop the parameter dict off the stack now we have a reference to it. */
    pop(&operandstack);

    if (! filterParamsFill(&paramDict, args))
      return FALSE;
    else {
      swcopyf(psBuf, (uint8*)"/%s exch /RunImageFilterController get exec",
              theICList(&system_names[procName]));

      if (! run_ps_string(psBuf))
        return FALSE;
      else {
        NAMETYPEMATCH results[] = {
          {NAME_DisableFilter | OOPTIONAL, 1, {OBOOLEAN}},
          {NAME_TargetWidth | OOPTIONAL, 2, {OINTEGER, OREAL}},
          {NAME_TargetHeight | OOPTIONAL, 2, {OINTEGER, OREAL}},
          {NAME_Interpolate, 1, {OBOOLEAN}},
          DUMMY_END_MATCH};

        /* We ran the proc successfully, extract results. */
        if (! dictmatch(&paramDict, results))
          return error_handler(TYPECHECK);

        /* DisableFilter */
        if (results[0].result != NULL)
          *disableFilter = oBool(*results[0].result);

        /* TargetWidth */
        if (results[1].result != NULL) {
          SYSTEMVALUE fwidth = object_numeric_value(results[1].result) ;
          if (! intrange(fwidth) )
            return error_handler(RANGECHECK);
          changes->width = (int32)fwidth ;
          *changesMade = TRUE;
        }

        /* TargetHeight */
        if (results[2].result != NULL) {
          SYSTEMVALUE fheight = object_numeric_value(results[2].result) ;
          if (! intrange(fheight) )
            return error_handler(RANGECHECK);
          changes->height = (int32)fheight ;
          *changesMade = TRUE;
        }

        /* Interpolate */
        if (oBool(*results[3].result) != args->interpolateFlag) {
          changes->interpolate = oBool(*results[3].result);
          *changesMade = TRUE;
        }
      }
    }
  }

  /* Clean up the stack - prevents untidy user filter controllers from killing
     everyone */
  return cleartomark_(pscontext);
}

/* --Private methods-- */

/* Fill the parameter dictionary using the passed args.
 * Returns FALSE on error.
 */
static Bool filterParamsFill(OBJECT* params, FilteredImageArgs* args)
{
  Bool success = TRUE;

  HQASSERT(params != NULL && args != NULL,
           "filterParamsFill - arguments cannot be NULL");

  /* Add the relevant values to the dictionary */
  success = success && addInt(params, NAME_Width, args->width);
  success = success && addInt(params, NAME_Height, args->height);
  success = success && addInt(params, NAME_WidthOnDevice, args->widthOnDevice);
  success = success && addInt(params, NAME_HeightOnDevice,
                              args->heightOnDevice);
  success = success && addInt(params, NAME_OriginalBitsPerComponent,
                              args->originalBpc);
  success = success && addInt(params, NAME_BitsPerComponent, args->bpc);
  success = success && addInt(params, NAME_ColorantCount, args->colorantCount);

  {
    OBJECT flag = OBJECT_NOTVM_NOTHING ;
    object_store_bool(&flag, args->interpolateFlag);
    success = success && fast_insert_hash_name(params, NAME_Interpolate, &flag);
  }

  return success;
}

/* Helper function to add an integer to the passed dictionary.
 * Returns FALSE on error.
 */
static Bool addInt(OBJECT* dict, int32 name, uint32 value)
{
  OBJECT valueObject = OBJECT_NOTVM_NOTHING;

  HQASSERT((dict != NULL), "addInt - Bad parameters");

  if (value > MAXINT32) {
    HQFAIL("addInt - number to large.");
    return error_handler(RANGECHECK);
  }
  object_store_integer(&valueObject, value);
  return fast_insert_hash_name(dict, name, &valueObject);
}

void init_C_globals_imfltchn(void)
{
  globalImageChain = NULL;
  globalMaskChain = NULL;
}

/* Log stripped */
