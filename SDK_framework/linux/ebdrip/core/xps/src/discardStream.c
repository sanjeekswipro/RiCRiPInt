/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!src:discardStream.c(EBDSDK_P.1) $
 *
 * Copyright (c) 2007-2009 Global Graphics Software Ltd. All Rights Reserved.
 * Global Graphics Software Ltd. Confidential Information. *
 *
 * \brief
 * Implementation of the XPS DiscardControl part.
 */
#include "core.h"
#include "discardStream.h"

#include "objects.h"
#include "swerrors.h"
#include "namedef_.h"
#include "swctype.h"
#include "fileio.h"
#include "devices.h"
#include "hqmemcpy.h"
#include "hqnuri.h"
#include "xml.h"
#include "xps.h"
#include "xpspriv.h"
#include "xpsscan.h"
#include "hqmemcmp.h"
#include "zipdev.h"
#include "monitor.h"

typedef struct {
  dll_link_t listEntry;
  xps_partname_t* sentinelPage;
  xps_partname_t* target;

  /* This string should not be deallocated explicitly. */
  uint8* pageString;
  uint32 pageStringLength;
} DiscardedPart;

struct DiscardParser {
  /* True when more data is availble in the XML discard stream. */
  Bool moreData;

  xps_partname_t* discardPart;
  OBJECT file;
  DEVICELIST* device;
  xmlGFilterChain* filterChain;
  xml_chunk_parser_t* chunkParser;
  dll_list_t discardedParts;
};

static void printUri(char* message, hqn_uri_t* uri)
{
  uint8* name;
  uint32 nameLength;
  if (hqn_uri_get_field(uri, &name, &nameLength, HQN_URI_PATH))
    monitorf((uint8*)"%s %.*s\n", message, nameLength, name);
  else
    monitorf((uint8*)"Failed to get URI field.\n");
}

/**
 * Create a new discarded part and add it to the passed list.
 */
static DiscardedPart* discardedPartNew(dll_list_t* list)
{
  DiscardedPart* self = mm_alloc(mm_xml_pool, sizeof(DiscardedPart),
                                 MM_ALLOC_CLASS_XPS_DISCARDCONTROL);
  if (self != NULL) {
    DLL_RESET_LINK(self, listEntry);
    DLL_ADD_TAIL(list, self, listEntry);
    self->sentinelPage = NULL;
    self->target = NULL;
    self->pageString = NULL;
  }

  return self;
}

/**
 * Destructor.
 */
static void discardedPartDestroy(DiscardedPart* self)
{
  DLL_REMOVE(self, listEntry);

  if (self->sentinelPage != NULL)
    xps_partname_free(&self->sentinelPage);
  if (self->target != NULL)
    xps_partname_free(&self->target);

  /* Note that the pageString element is not ours to destroy. */

  mm_free(mm_xml_pool, self, sizeof(DiscardedPart));
}

/**
 * Start callback for a Discard element.
 */
static Bool discardElementStart(xmlGFilter* filter, const xmlGIStr* localname,
                                const xmlGIStr* prefix, const xmlGIStr* uri,
                                xmlGAttributes* attributes)
{
  static XML_ATTRIBUTE_MATCH match[] = {
    { XML_INTERN(SentinelPage), NULL, NULL, xps_convert_partname, NULL },
    { XML_INTERN(Target), NULL, NULL, xps_convert_partname, NULL },
    XML_ATTRIBUTE_MATCH_END
  };

  DiscardParser* self = xmlg_get_user_data(filter);
  DiscardedPart* discardedPart;

  UNUSED_PARAM(const xmlGIStr *, localname);
  UNUSED_PARAM(const xmlGIStr *, prefix);
  UNUSED_PARAM(const xmlGIStr *, uri);

  discardedPart = discardedPartNew(&self->discardedParts);
  match[0].data = &discardedPart->sentinelPage;
  match[1].data = &discardedPart->target;
  if (! xmlg_attributes_match(filter, localname, uri, attributes, match, FALSE))
    return FALSE;

  if (! hqn_uri_get_field(discardedPart->sentinelPage->uri,
                          &discardedPart->pageString,
                          &discardedPart->pageStringLength,
                          HQN_URI_PATH))
    return FALSE;

#ifdef INSTRUMENT_DISCARD_CONTROL
  printUri("New discard for sentinel page:", discardedPart->sentinelPage->uri);
  printUri("  Target:", discardedPart->target->uri);
#endif

  return TRUE;
}

/**
 * Start callback for the discard control document element.
 */
static Bool discardControlStart(xmlGFilter* filter, const xmlGIStr* localname,
                                const xmlGIStr* prefix, const xmlGIStr* uri,
                                xmlGAttributes* attributes)
{
  static XMLG_VALID_CHILDREN validChildren[] = {
    { XML_INTERN(Discard), XML_INTERN(ns_xps_2005_06_discard_control),
      XMLG_ZERO_OR_MORE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  };

  UNUSED_PARAM(const xmlGIStr *, prefix);
  UNUSED_PARAM(xmlGAttributes *, attributes);

  /* Setup the types of child elements allowed within this element. */
  return xmlg_valid_children(filter, localname, uri, validChildren);
}

/**
 * Create the discard control part filter and register element callbacks.
 */
static xmlGFilter* dParserCreateFilter(DiscardParser* self)
{
  xmlGFilter* filter = NULL;
  const xmlGIStr* ns = XML_INTERN(ns_xps_2005_06_discard_control);

  if (! xmlg_fc_new_filter(self->filterChain, &filter, 0, self, NULL))
    return NULL;

  /* Note that we don't try to destroy the filter if we error now; we assume
  the containing chain will destroy it in the error case. */

  if (! (xmlg_register_start_element_cb(filter, XML_INTERN(DiscardControl), ns,
                                        discardControlStart) &&
         xmlg_register_start_element_cb(filter, XML_INTERN(Discard), ns,
                                        discardElementStart))) {
    return NULL;
  }

  return filter;
}

/**
 * Error callback.
 */
static void parseError(xmlGParser* parser, hqn_uri_t* uri,
                       uint32 line, uint32 column,
                       uint8* detail, int32 detailLength)
{
  UNUSED_PARAM(xmlGParser*, parser);
  UNUSED_PARAM(hqn_uri_t*, uri);
  UNUSED_PARAM(uint32, line);
  UNUSED_PARAM(uint32, column);
  UNUSED_PARAM(uint8*, detail);
  UNUSED_PARAM(int32, detailLength);

  /* Errors in discard control are not fatal; do nothing. */
}

/**
 * Destructor. Note that it is safe to call this method on a partially
 * contructed parser.
 *
 * \return NULL.
 */
static DiscardParser* dParserDestroy(DiscardParser* self)
{
  /* Destroy discarded part list. */
  DiscardedPart* scan = DLL_GET_HEAD(&self->discardedParts, DiscardedPart, listEntry);
  while (scan != NULL) {
    DiscardedPart* next = DLL_GET_NEXT(scan, DiscardedPart, listEntry);
#ifdef INSTRUMENT_DISCARD_CONTROL
    printUri("Destroying non-action'd discard for part:", scan->target->uri);
#endif
    discardedPartDestroy(scan);
    scan = next;
  }

  if (self->chunkParser != NULL)
    xml_parse_chunk_finish(&self->chunkParser, TRUE);

  if (self->filterChain != NULL)
    xmlg_fc_destroy(&self->filterChain);

  /* Note that we ignore any error when closing the file; what else could we
  do? */
  if (oType(self->file) != ONULL)
    (void)xml_file_close(&self->file);

  xps_partname_free(&self->discardPart);
  mm_free(mm_xml_pool, self, sizeof(DiscardParser));
  return NULL;
}

/**
 * Constructor. The passed discard part will be opened for processing, but no
 * data will be read. This method assumes ownership of 'discardPart'.
 *
 * Note that failures when processing the discard part should not be fatal,
 * therefore no errors are setup if a failure occurs.
 */
static DiscardParser* dParserNew(xps_partname_t* discardPart)
{
  xmlGFilter* filter;
  DiscardParser* self;

  self = mm_alloc(mm_xml_pool, sizeof(DiscardParser),
                  MM_ALLOC_CLASS_XPS_DISCARDCONTROL);
  if (self == NULL) {
    xps_partname_free(&discardPart);
    return NULL;
  }

  self->discardPart = discardPart;
  self->moreData = TRUE;
  self->filterChain = NULL;
  self->file = onull; /* Struct copy to set slot properties */
  DLL_RESET_LIST(&self->discardedParts);

  /* Open the discard control part. */
  if (! open_file_from_psdev_uri(discardPart->uri, &self->file, FALSE))
    return dParserDestroy(self);

  /* Create the filter chain. */
  if (! xmlg_fc_new(core_xml_subsystem, &self->filterChain,
                    &xmlexec_memory_handlers, discardPart->uri,
                    discardPart->uri, NULL))
    return dParserDestroy(self);

  filter = dParserCreateFilter(self);
  if (filter == NULL)
    return dParserDestroy(self);

  self->device = get_device(oFile(self->file));

  /* Override the parse error callback to be XPS specific. */
  xmlg_fc_set_parse_error_cb(self->filterChain, parseError);

  if (! xml_parse_chunk_init(oFile(self->file), self->filterChain,
                             &self->chunkParser))
    return dParserDestroy(self);

  return self;
}

/**
 * Discard the passed part from the zip device.
 */
static Bool discardFromZipDevice(DEVICELIST* device, uint8* filename)
{
  if (theIIoctl(device)(device, 0, ZIP_IOCTL_DISCARD_ENTRY,
                        (intptr_t)filename) != 0)
    return FAILURE(FALSE);

  return TRUE;
}

/**
 * Discard the specified part from the specified device.
 */
static Bool discardPartFromDevice(DEVICELIST* device, hqn_uri_t* partUri)
{
  uint8* path;
  uint32 pathLength;

  if (hqn_uri_get_field(partUri, &path, &pathLength, HQN_URI_PATH) &&
      pathLength + 1 <= LONGESTFILENAME) {
    /* NULL-terminate the filename. */
    uint8 filename[LONGESTFILENAME];
    HqMemCpy(filename, path, pathLength);
    filename[pathLength] = 0;

#ifdef INSTRUMENT_DISCARD_CONTROL
    printUri("  Discarding part:", partUri);
#endif

    switch (device->devicetype->devicenumber) {
      default:
        return FAILURE(FALSE);

      case ZIP_DEVICE_TYPE:
        return discardFromZipDevice(device, filename);
    }
  }
  else {
    return FAILURE(FALSE);
  }
}

/**
 * Set 'ready' to true if there is more data cached for the discard part,
 * i.e. when streaming and the zip device does not require a new piece to be
 * read before data is available for the discard part.
 */
static Bool zipDeviceNextPieceReady(DiscardParser* self, Bool* ready)
{
  int32 result;
  FILELIST* file = oFile(self->file);

  result = self->device->devicetype->ioctl_call(self->device,
                                                theIDescriptor(file),
                                                ZIP_IOCTL_NEXT_PIECE_READY, 0);
  if (result == -1)
    return FAILURE(FALSE);

  *ready = (result == 1);
  return TRUE;
}

/**
 * Process the discards for the specified page URI.
 *
 * Note that we can simply compare the URI of the fixed page with those in the
 * discard list since both should have been normalised as they were scanned by
 * xps_convert_partname().
 */
static Bool dParserProcessDiscards(DiscardParser* self, hqn_uri_t* pageUri)
{
  uint8* pageString;
  uint32 pageStringLength;
  DiscardedPart* scan = DLL_GET_HEAD(&self->discardedParts, DiscardedPart, listEntry);
  Bool verbose = FALSE;

#ifdef INSTRUMENT_DISCARD_CONTROL
  verbose = TRUE;
#endif

  if (! hqn_uri_get_field(pageUri, &pageString, &pageStringLength, HQN_URI_PATH))
    return FAILURE(FALSE);

  if (verbose)
    printUri("Processing discards for page:", pageUri);

  while (scan != NULL) {
    DiscardedPart* next = DLL_GET_NEXT(scan, DiscardedPart, listEntry);

    if (HqMemCmp(pageString, pageStringLength, scan->pageString,
                 scan->pageStringLength) == 0) {
      if (! discardPartFromDevice(self->device, scan->target->uri)) {
        if (verbose)
          monitorf((uint8*)"  Discard failed.\n");

        return FAILURE(FALSE);
      }
      discardedPartDestroy(scan);
    }
    scan = next;
  }
  return TRUE;
}

/**
 * Create a new discard control part parser. Note that 'discardPart' will be
 * managed by the parser and should NOT be deallocated by the caller, even if
 * this method returns FALSE.
 * \return FALSE on error.
 */
Bool xps_open_discard_parser(xps_partname_t* discardPart,
                             DiscardParser** discardParser)
{
  *discardParser = dParserNew(discardPart);
  return *discardParser != NULL;
}

/**
 * Destroy the passed discard parser.
 */
void xps_close_discard_parser(DiscardParser** discardParser)
{
  dParserDestroy(*discardParser);
  *discardParser = NULL;
}

/**
 * Process any discards for the page being processed by the passed filter.
 *
 * \return FALSE on error.
 */
Bool xps_process_discards(xmlGFilter *fixedPageFilter,
                          DiscardParser* discardParser)
{
  Bool skipParse = FALSE;

  /* On the zip device, don't try to parse the discard control stream if no
  data is available; doing so will break streaming as the device reads ahead
  looking for the next piece. */
  if (discardParser->device->devicetype->devicenumber == ZIP_DEVICE_TYPE) {
    Bool ready = FALSE;
    if (! zipDeviceNextPieceReady(discardParser, &ready))
      return FAILURE(FALSE);
    skipParse = !ready;
  }

#ifdef INSTRUMENT_DISCARD_CONTROL
  monitorf((uint8*)"Skipping discard parse: %s\n", skipParse ? "Yes" : "No");
#endif

  if (! skipParse && discardParser->moreData) {
    Bool success = xml_parse_chunk(discardParser->chunkParser,
                                   &discardParser->moreData);

    if (! success || ! discardParser->moreData) {
      xml_parse_chunk_finish(&discardParser->chunkParser, success);
      if (! success)
        return FALSE;
    }
  }

  return dParserProcessDiscards(discardParser,
                                xmlg_fc_get_uri(xmlg_get_fc(fixedPageFilter)));
}

/**
 * Immediately discard the specified part.
 */
Bool xps_discard_part(xps_partname_t* part)
{
  uint8 *authority;
  uint32 authorityLength;

#ifdef INSTRUMENT_DISCARD_CONTROL
  printUri("Immediate discard for part:", part->uri);
#endif

  if (hqn_uri_get_field(part->uri, &authority, &authorityLength,
                        HQN_URI_AUTHORITY) &&
      authorityLength + 1 <= LONGESTFILENAME) {
    uint8 devicename[LONGESTFILENAME];
    DEVICELIST* device;

    /* NULL-terminate the device name. */
    HqMemCpy(devicename, authority, authorityLength);
    devicename[authorityLength] = 0;

    device = find_device(devicename);
    if (device == NULL)
      return error_handler(UNDEFINED);
    else
      return discardPartFromDevice(device, part->uri);
  }
  else
    return FALSE;
}

/* Log stripped */

