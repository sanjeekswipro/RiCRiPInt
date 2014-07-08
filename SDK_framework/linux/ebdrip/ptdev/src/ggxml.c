/* Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWptdev!src:ggxml.c(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */
#include "ptincs.h"
#include "printticket.h"

#include "ggxml.h"

/**
 * @file
 * @brief GG XML interfaces
 */

/* Various page dimensions*/
double size[2];
double bleedbox[4];
double contentbox[4];


/** @brief A simple function to parse the page Size attribute value. */
static int32 parse_size(
  const char* attribute,
  double      nSize[2])
{
  char* value;
  char* rest;

  value = (char*)attribute;

  nSize[0] = xml_parse_double(value, &rest);
  if ( rest == value ) {
    return(FALSE);
  }
  value = rest;
  if ( *value != ',' ) {
    return(FALSE);
  }
  value++;
  nSize[1] = xml_parse_double(value, &rest);
  if ( rest == value ) {
    return(FALSE);
  }
  value = rest;
  /* No trailing characters! */
  if ( *value != '\0' ) {
    return(FALSE);
  }
  return(TRUE);
}

/** @brief A simple function to parse BleedBox and ContentBox attribute values. */
static int32 parse_box(
  const char* attribute,
  double box[4])
{
  int32 i;
  char* value;
  char* rest;

  value = (char*)attribute;

  for ( i = 0; i < 4; i ++ ) {
    if ( i > 0 ) {
      if ( *value != ',' ) {
        return(FALSE);
      }
      value++;
    }
    box[i] = xml_parse_double(value, &rest);
    if ( rest == value ) {
      return(FALSE);
    }
    value = rest;
  }
  /* No trailing characters! */
  if ( *value != '\0' ) {
    return(FALSE);
  }
  return(TRUE);
}

/** @brief Start callback for GGNS Page element - extracts page size and boxes */
static void gg_page_start(
  void*         data,
  const char**  attributes)
{
  int32 index;

  UNUSED_PARAM(void*, data);

  index = xml_find_attr((char**)attributes, (uint8*)"Size");
  if ( index >= 0 ) {
    if ( !parse_size(attributes[index + 1], size) ) {
      pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"Page Size attribute value not valid.");
      return;
    }
  }
  index = xml_find_attr((char**)attributes, (uint8*)"BleedBox");
  if ( index >= 0 ) {
    if ( !parse_box(attributes[index + 1], bleedbox) ) {
      pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"Page BleedBox attribute value not valid.");
      return;
    }
  }
  index = xml_find_attr((char**)attributes, (uint8*)"ContentBox");
  if ( index >= 0 ) {
    if ( !parse_box(attributes[index + 1], contentbox) ) {
      pt_xml_set_error(XPSPT_ERROR_SYNTAX, (uint8*)"Page ContentBox attribute value not valid.");
      return;
    }
  }
}

static const XML_ELEMENT_CALLBACKS gg_callbacks[] = {
  {STRING_AND_LENGTH("PageDetails"),
    NULL,
    NULL,
    NULL},
  {STRING_AND_LENGTH("Page"),
    gg_page_start,
    NULL,
    NULL},
  {NULL} /* Indicates end of element list */
};

const XML_NAMESPACE_ELEMENTS gg_elements = {
  {STRING_AND_LENGTH("http://schemas.globalgraphics.com/xps/2005/03/pagedetails")},
  gg_callbacks,
  NULL
};


DEVICE_FILEDESCRIPTOR gg_open(
  DEVICE_FILEDESCRIPTOR fd)
{
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, fd);

  return(pt_xml_start());
}

int32 gg_close(
  DEVICE_FILEDESCRIPTOR fd,
  int32 abort)
{
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, fd);
  UNUSED_PARAM(int32, abort);

  /* Terminate the XML parser and release namespace list */
  pt_xml_end();

  return(TRUE);
}

int32 gg_write(
  uint8*  buffer,
  int32*  len)
{
  return(xml_parse(buffer, len));
}

int32 gg_read(
  uint8*  buffer,
  int32*  len)
{
  UNUSED_PARAM(uint8*, buffer);
  UNUSED_PARAM(int32*, len);

  return(FALSE);
}

int32 gg_eof(
  DEVICE_FILEDESCRIPTOR fd)
{
  UNUSED_PARAM(DEVICE_FILEDESCRIPTOR, fd);

  return(FALSE);
}


/* EOF ggxml.c */
