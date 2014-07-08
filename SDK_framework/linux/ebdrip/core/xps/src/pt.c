/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!src:pt.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2005-2007, 2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * XPS Print Ticket part handling.
 */

#define OBJECT_SLOTS_ONLY

#include "core.h"
#include "hqmemcpy.h"
#include "swcopyf.h"
#include "printticket.h"
#include "devices.h"          /* device_first() */
#include "objects.h"
#include "fileio.h"
#include "params.h"
#include "miscops.h"
#include "execops.h"          /* setup_pending_exec() */
#include "namedef_.h"
#include "swerrors.h"

#include "xml.h"

#include "xpspt.h"

/** \brief Internal PT scope value to catch no starting scope being set. */
#define PT_SCOPE_UNDEFINED  (0)
/** \brief Constant for PrintTicket scope of Job. */
#define PT_SCOPE_JOB      (1)
/** \brief Constant for PrintTicket scope of Document. */
#define PT_SCOPE_DOCUMENT (2)
/** \brief Constant for PrintTicket scope of Page. */
#define PT_SCOPE_PAGE     (3)


/**
 * \brief Get a named parameter from the PT device.
 *
 * \param[in] pt
 * XPS PrintTicket handler
 * \param[in] name
 * Name of PT device parameter to retrieve
 * \param[in] len
 * Length of parameter name.
 * \param[out] param
 * Pointer to returned filled in \c DEVICEPARAM structure.
 *
 * \returns
 * \c TRUE if the named parameter is retrieved, else \c FALSE.
 */
static
Bool pt_get_param(
/*@in@*/ /*@notnull@*/
  XPS_PT*      pt,
/*@in@*/ /*@notnull@*/
  uint8*        name,
  int32         len,
/*@out@*/ /*@notnull@*/
  DEVICEPARAM*  param)
{
  HQASSERT((pt != NULL),
           "pt_get_param: pt handle NULL");
  HQASSERT((pt->device != NULL),
           "pt_get_param: pt device pointer NULL");
  HQASSERT((name != NULL),
           "pt_get_param: param name pointer NULL");
  HQASSERT((len > 0),
           "pt_get_param: invalid parameter name length");
  HQASSERT((param != NULL),
           "pt_get_param: pointer to returned param val NULL");

  theIDevParamName(param) = name;
  theIDevParamNameLen(param) = len;
  return ((*theIGetParam(pt->device))(pt->device, param) == ParamAccepted);

} /* pt_get_param */


/**
 * \brief Set a named parameter from the PT device.
 *
 * \param[in] pt
 * XPS PrintTicket handler
 * \param[out] param
 * Pointer to filled in \c DEVICEPARAM structure.
 *
 * \returns
 * \c TRUE if the named parameter is set successfully, else \c FALSE.
 */
static
Bool pt_set_param(
/*@in@*/ /*@notnull@*/
  XPS_PT*      pt,
/*@out@*/ /*@notnull@*/
  DEVICEPARAM*  param)
{
  HQASSERT((pt != NULL),
           "pt_set_param: pt handle NULL");
  HQASSERT((pt->device != NULL),
           "pt_set_param: pt device pointer NULL");
  HQASSERT((param != NULL),
           "pt_set_param: pointer to param val NULL");

  return ((*theISetParam(pt->device))(pt->device, param) == ParamAccepted);

} /* pt_set_param */


/**
 * \brief Set up PS error from PT device.
 *
 * The PT device parameters are queries to get the XML error details.
 *
 * \param[in] pt
 * XPS PrintTicket handler
 * \param[in] uri
 * PT file name
 * \param[in] uri_len
 * Length of PT file name
 *
 * \returns
 * \c FALSE.
 */
static
Bool pt_error_handler(
/*@in@*/ /*@notnull@*/
  XPS_PT*   pt,
/*@in@*/ /*@notnull@*/
  uint8*    uri,
  uint32    uri_len)
{
  int32   errorno;
  int32   line;
  int32   column;
  uint8*  detail;
  int32   detail_len;
  DEVICEPARAM param;

  HQASSERT((pt != NULL),
           "pt_error_handler: NULL pt context pointer");
  HQASSERT((uri != NULL),
           "pt_error_handler: NULL pt uri pointer");
  HQASSERT((uri_len > 0),
           "pt_error_handler: invalid pt uri length");

  /* Any error in sending the XML may have resulted in error_handler() being
   * called, so clear any error ready for a more detailed version.
   */
  error_clear();

  /* Get XML parse details from PT device */
  errorno = UNDEFINED;
  if ( pt_get_param(pt, STRING_AND_LENGTH("ErrorNo"), &param) ) {
    if ( param.type == ParamInteger ) {
      /* Translate pt device error to PS error */
      switch ( theDevParamInteger(param) ) {
      case XPSPT_ERROR_OUTOFMEM:
        errorno = VMERROR;
        break;
      case XPSPT_ERROR_SYNTAX:
        errorno = SYNTAXERROR;
        break;
      case XPSPT_ERROR_RANGECHECK:
        errorno = RANGECHECK;
        break;
      case XPSPT_ERROR_LIMITCHECK:
        errorno = LIMITCHECK;
        break;
      default:
        HQFAIL("pt_error_handler: unexpected PT device error number");
      }
    }
  }
  line = -1;
  if ( pt_get_param(pt, STRING_AND_LENGTH("ErrorLine"), &param) ) {
    if ( param.type == ParamInteger ) {
      line = theDevParamInteger(param);
    }
  }
  column = -1;
  if ( pt_get_param(pt, STRING_AND_LENGTH("ErrorColumn"), &param) ) {
    if ( param.type == ParamInteger ) {
      column = theDevParamInteger(param);
    }
  }
  detail = NULL;
  detail_len = 0;
  if ( pt_get_param(pt, STRING_AND_LENGTH("ErrorMessage"), &param) ) {
    if ( param.type == ParamString ) {
      detail = theDevParamString(param);
      detail_len = theDevParamStringLen(param);
      HQASSERT((detail_len >= 0),
               "pt_error_handler: Invalid error message length from PT device");
      HQASSERT(((detail != NULL) || (detail_len == 0)),
               "pt_error_handler: Invalid error message from PT device");
    }
  }

  /** \todo TODO: THIS IS A HACK AND NEEDS IMPROVING */
#define LONGFORM2 ("PartName: %.*s; Line: %d; Column: %d; XMLInfo: %.*s")
  return detailf_error_handler(errorno, LONGFORM2, uri_len, uri, line, column, detail_len, detail);

} /* pt_error_handler */


/* Initialise XPS PrintTicket state for the start of a new Job. */
void pt_init(
/*@out@*/ /*@notnull@*/
  XPS_PT*  pt)
{
  device_iterator_t dev_iter;

  HQASSERT((pt != NULL),
           "pt_init: state pointer NULL");

  /* Always start with job scope */
  pt->scope = PT_SCOPE_JOB;

  /* No start file associated with scope yet */
  pt->scope_file = onothing; /* Struct copy to set slot properties */

  /* Find first mounted and enable XPS PT device */
  for ( pt->device = device_first(&dev_iter, DEVICEENABLED|DEVICEWRITABLE|DEVICERELATIVE);
        pt->device != NULL;
        pt->device = device_next(&dev_iter) ) {
    /** \todo TODO Add PT device number as constant when can add swdevice.h */
    if ( theIDevTypeNumber(theIDevType(pt->device)) == XPSPT_DEVICE_TYPE ) {
#ifdef DEBUG_BUILD
      {
        /* Check no other PT devices are enabled! */
        DEVICELIST* dev = device_next(&dev_iter);
        while ( dev != NULL ) {
          HQTRACE(theIDevTypeNumber(theIDevType(dev)) == XPSPT_DEVICE_TYPE,
                  ("Warning: Found another enabled PT device: %s\n", theIDevName(dev)));
          dev = device_next(&dev_iter);
        }
      }
#endif /* DEBUG_BUILD */
      break;
    }
  }

} /* pt_init */


/**
 * \brief Copy the content of a PrintTicket part to the Print Ticket device
 * scope file.
 *
 * \param[in] flptr_part
 * Pointer to PrintTicket part filestream.
 * \param[in] flptr_dev
 * Pointer to PrintTicket device scope file filestream.
 *
 * \returns
 * \c TRUE if the content of the PrintTicket part has been copied to the Print
 * Ticket device scope file, else \c FALSE.
 */
static
Bool pt_copy(
/*@in@*/ /*@notnull@*/
  FILELIST* flptr_part,
/*@in@*/ /*@notnull@*/
  FILELIST* flptr_dev)
{
  uint8* pb_read;
  int32 cb_read;

  HQASSERT((flptr_part != NULL),
           "pt_copy: PT part file pointer NULL");
  HQASSERT((flptr_dev != NULL),
           "pt_copy: PT device file pointer NULL");

  /* Copy uri to device */
  while ( GetFileBuff(flptr_part, 16384, &pb_read, &cb_read) ) {
    if ( !file_write(flptr_dev, pb_read, cb_read) ) {
      return FALSE ;
    }
  }

  /* Check for error reading PT part */
  if ( isIIOError(flptr_part) ) {
    return FALSE ;
  }

  /* Flush any buffered PT XML to the PT device */
  if ( (theIMyFlushFile(flptr_dev))(flptr_dev) == EOF ) {
    return (*theIFileLastError(flptr_dev))(flptr_dev) ;
  }
  return TRUE ;

} /* pt_copy */


/**
 * \brief Open a config file with appriopiate flags.
 *
 * \param[in] pt
 * XPS PrintTicket handler
 * \param[in] file_type
 * Type of config file, either \c "S" or \c "E".
 * \param[in] open_flags
 * Flags to open file on device with.
 * \param[in] ps_flags
 * PS opten flags (should be equivalent of \c open_flags)
 * \param[out] ofile
 * Pointer to returned PS file object.
 *
 * \returns
 * \c TRUE if opened the config file, else \c FALSE.
 */
static
Bool pt_open_config_file(
/*@in@*/ /*@notnull@*/
  XPS_PT*  pt,
/*@in@*/ /*@notnull@*/
  uint8*    file_type,
  int32     open_flags,
  int32     ps_flags,
/*@out@*/ /*@notnull@*/
  OBJECT*   ofile)
{
  uint8         pt_filename[LONGESTDEVICENAME + 16];
  static uint8* pt_scope[] = {
    NULL,
    (uint8*)"J",
    (uint8*)"D",
    (uint8*)"P"
  };

  HQASSERT((pt != NULL),
           "pt_open_config_file: state pointer NULL");
  HQASSERT((file_type != NULL),
           "pt_open_config_file: file type NULL");
  HQASSERT((ofile != NULL),
           "pt_open_config_file: pointer to returned OBJECT NULL");

  /* Create PT device filename based on scope and start/end type */
  swcopyf(pt_filename, (uint8*)"%%%s%%%s%s", theIDevName(pt->device), pt_scope[pt->scope], file_type);
  oString(snewobj) = pt_filename;
  theLen(snewobj) = CAST_UNSIGNED_TO_UINT16(strlen((char*)pt_filename));

  return file_open(&snewobj, open_flags, ps_flags, FALSE, 0, ofile) ;

} /* pt_open_config_file */


/**
 * \brief Close the start scope file.
 *
 * Normally the interpreter will close the start scope file by reading to EOF,
 * but if there is an error it needs to be explicitly closed.
 *
 * The file object is set to ONOTHING so we can detect when the start scope file
 * has not been opened yet by the time we want to read the start config PS.
 *
 * \param[in] pt
 * XPS PrintTicket handler
 */
static
void pt_close_start_file(
/*@in@*/ /*@notnull@*/
  XPS_PT*          pt)
{
  HQASSERT((pt != NULL),
           "pt_close_start_file: state pointer NULL");
  HQASSERT((oType(pt->scope_file) == OFILE),
           "pt_close_start_file: scope file has not been created");

  if ( isIOpenFile(oFile(pt->scope_file)) ) {
    (void)file_close(&pt->scope_file);
  }
  theTags(pt->scope_file) = ONOTHING;

} /* pt_close_start_file */


/* Merge and validate the PrintTicket part according to the current scope. */
Bool pt_mandv(
/*@in@*/ /*@notnull@*/
  xmlGFilter*      filter,
/*@in@*/ /*@notnull@*/
  XPS_PT*          pt,
/*@in@*/ /*@notnull@*/
  xps_partname_t*  part)
{
  Bool    status;
  OBJECT  part_file = OBJECT_NOTVM_NOTHING;
  uint8*  pt_uri;
  uint32  pt_uri_len;
  xmlGIStr *pt_mimetype ;

  static XPS_CONTENT_TYPES pt_content_types[] = {
    { XML_INTERN(mimetype_printing_printticket) },
    XPS_CONTENT_TYPES_END
  } ;

  HQASSERT((pt != NULL),
           "pt_mandv: state pointer NULL");
  HQASSERT((pt->device == NULL ||
            (pt->scope >= PT_SCOPE_JOB && pt->scope <= PT_SCOPE_PAGE)),
           "pt_mandv: PT has invalid scope");

  /* Catch absence of any PT device */
  if ( pt->device == NULL ) {
    return TRUE ;
  }

  /* Open a PS file on the PrintTicket part for read. */
  if ( !xps_open_file_from_partname(filter, part, &part_file,
                                    XML_INTERN(rel_xps_2005_06_printticket),
                                    pt_content_types,
                                    &pt_mimetype, FALSE)) {

    return FALSE ;
  }

  status = FALSE;

  /* Open the scope start file and copy the print ticket part contents to it.
   * Note: the scope start file gets closed by reading back the PS config and
   * that this will happen before the next print ticket is seen.
   */
  if ( pt_open_config_file(pt, (uint8*)"S", SW_RDWR, RW_FLAG, &pt->scope_file) ) {
    status = pt_copy(oFile(part_file), oFile(pt->scope_file));

    if ( !status ) {
      pt_close_start_file(pt);
      if ( !hqn_uri_get_field(part->uri, &pt_uri, &pt_uri_len, HQN_URI_NAME)) {
        HQFAIL("Unable to get PT URI") ;
        pt_uri_len = 0;
      }

      (void)pt_error_handler(pt, pt_uri, pt_uri_len);
    }
  }

  /* Failure reading from the PT part will have already been caught so don't
   * really care what happens when closing here.
   */
  (void)xml_file_close(&part_file);

  return status ;

} /* pt_mandv */


/* Perform start of scope RIP configuration. */
Bool pt_config_start(
/*@in@*/ /*@notnull@*/
  XPS_PT*  pt)
{
  int32 status;

  HQASSERT((pt != NULL),
           "pt_config_start: pt pointer NULL");
  HQASSERT(((pt->scope >= PT_SCOPE_JOB) && (pt->scope <= PT_SCOPE_PAGE)),
           "pt_config_start: pt pointer NULL");

  if ( !SystemParams.XPS )
    return error_handler(INVALIDACCESS);

  if ( pt->scope == PT_SCOPE_JOB ) {
    /* Enable OverprintPreview to match behaviour when BackdropRender was
       required.  XPS uses non-overlapping scan conversion rules. */
    if ( !run_ps_string((uint8*)
                        "<</OverprintPreview true >> setinterceptcolorspace "
                        "<</VirtualDeviceSpace /DeviceRGB "
                        "  /ScanConversion /RenderTesselating >> setpagedevice") ) {
      return FALSE ;
    }

    /* This section sets the MS deviations from the TIFF6 standard */
    /* TBD need to decide upon default for TreatES0asES2 */
    if ( !run_ps_string((uint8*)"<</TreatES0asES2 true /NoUnitsSameAsInch true>>settiffparams") ) {
      return FALSE ;
    }
  }

  if ( pt->device != NULL ) {
    /* If the start file has not been opened yet (i.e. there is no PT for this
     * scope) then open it now and run the config PS from it.
     */
    if ( (oType(pt->scope_file) != OFILE) &&
         !pt_open_config_file(pt, (uint8*)"S", SW_RDWR, RW_FLAG, &pt->scope_file) ) {
      return FALSE ;
    }

    /* Just make the file object executable and pass to the interpreter to run.
     * It will read to the end and close the file for us.  We just have to tidy
     * up our end ready for the next start scope call.
     */
    theTags(pt->scope_file) |= EXECUTABLE;
    status = setup_pending_exec(&pt->scope_file, TRUE);
    pt_close_start_file(pt);
    if ( !status ) {
      return FALSE ;
    }

  } else {
    if ( pt->scope == PT_SCOPE_PAGE ) {
      /* Minimal PS to map to XPS userspace when no PT device - pagesize as requested, 96dpi, origin TL */
      uint8 buffer[512];
      swcopyf(buffer, (uint8*)"<< /Scaling [0.75 -0.75] /PageSize [%f %f]>>setpagedevice",
              pt->width, pt->height);
      if ( !run_ps_string(buffer) ) {
        return FALSE ;
      }
    }
  }

  /* Reduce scope of next PT */
  pt->scope++;

  return TRUE ;

} /* pt_config_start */


/* Perform end of scope RIP configuration. */
Bool pt_config_end(
/*@in@*/ /*@notnull@*/
  XPS_PT*  pt,
  Bool      abortjob)
{
  int32   status;
  OBJECT  end_file = OBJECT_NOTVM_NOTHING;
  static DEVICEPARAM param = {
    STRING_AND_LENGTH("AbortJob"),
    ParamBoolean,
    NULL
  };

  HQASSERT((pt != NULL),
           "pt_config_end: pt pointer NULL");

  if ( !SystemParams.XPS )
    return error_handler(INVALIDACCESS);

  /* Raise scope of print ticket */
  pt->scope--;
  HQASSERT(((pt->scope >= PT_SCOPE_JOB) && (pt->scope <= PT_SCOPE_PAGE)),
           "pt_config_end: invalid pt scope");

  /* Catch absence of any PT device */
  if ( pt->device == NULL ) {
    return TRUE ;
  }

  if ( !abortjob ) {
    /* Run end config PS */
    if ( !pt_open_config_file(pt, (uint8*)"E", SW_RDONLY, READ_FLAG, &end_file) ) {
      return FALSE ;
    }
    theTags(end_file) |= EXECUTABLE;
    status = setup_pending_exec(&end_file, TRUE);
    (void)file_close(&end_file);
    return status ;

  } else { /* Signal to PT device that job is being aborted */
    theDevParamBoolean(param) = TRUE;
    (void)pt_set_param(pt, &param);
  }

  return TRUE ;

} /* pt_config_end */


/* Send FixedPage media information to the PT device. */
Bool pt_page_details(
/*@in@*/ /*@notnull@*/
  XPS_PT*     pt,
  double      width,
  double      height,
/*@in@*/ /*@notnull@*/
  RECTANGLE*  bleedbox,
/*@in@*/ /*@notnull@*/
  RECTANGLE*  contentbox)
{
  Bool    success;
  OBJECT  dev_file = OBJECT_NOTVM_NOTHING;
  uint8   pt_filename[LONGESTDEVICENAME + 16];
  uint8   buffer[16384];

  HQASSERT((pt != NULL),
           "pt_config_end: pt pointer NULL");
  HQASSERT((bleedbox != NULL),
           "pt_config_end: bleedbox pointer NULL");
  HQASSERT((contentbox != NULL),
           "pt_config_end: contentbox pointer NULL");
  HQASSERT((pt->scope == PT_SCOPE_PAGE),
           "pt_config_end: sending media details at wrong scope level");

  /* Catch absence of any PT device */
  if ( pt->device == NULL ) {
    /* Cache the page size for minimal page config PS */
    pt->width = width;
    pt->height = height;
    return TRUE ;
  }

  /* Create XML of fixedpage details */
  swcopyf(buffer,
          (uint8*)"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                  "<PageDetails xmlns=\"http://schemas.globalgraphics.com/xps/2005/03/pagedetails\">"
                  "  <Page Size=\"%f,%f\" BleedBox=\"%f,%f,%f,%f\" ContentBox=\"%f,%f,%f,%f\"/>"
                  "</PageDetails>",
          width, height,
          bleedbox->x, bleedbox->y, bleedbox->w, bleedbox->h,
          contentbox->x, contentbox->y, contentbox->w, contentbox->h);

  /* Generate name of fixedpage details file on PT device */
  swcopyf(pt_filename, (uint8*)"%%%s%%PD", theIDevName(pt->device));
  oString(snewobj) = pt_filename;
  theLen(snewobj) = CAST_UNSIGNED_TO_UINT16(strlen((char*)pt_filename));
  if ( !file_open(&snewobj, SW_WRONLY, WRITE_FLAG, FALSE, 0, &dev_file) ) {
    return FALSE ;
  }

  /* Copy XML in buffer to PT device */
  success = file_write(oFile(dev_file), buffer, strlen_int32((char*)buffer));
  success = file_close(&dev_file) && success;

  if ( !success ) {
    /* The XML error handler needs a file name, but there is no externally
     * visible file for the page details XML.  So the following is a made up
     * name to uniquely locate the source of the XML that caused the error.
     */
#define PAGE_DETAILS_NAME "Internal:PageDetails.xml"
    (void)pt_error_handler(pt, STRING_AND_LENGTH(PAGE_DETAILS_NAME));
  }

  return success ;

} /* pt_page_details */


/* Get the index of the next page in the current FixedDocument to interpret. */
Bool pt_next_page(
/*@in@*/ /*@notnull@*/
  XPS_PT*  pt,
/*@in@*/ /*@notnull@*/
  int32*    p_next_page)
{
  DEVICEPARAM param;

  HQASSERT((pt != NULL),
           "pt_next_page: pt pointer NULL");
  HQASSERT((pt->scope > PT_SCOPE_JOB),
           "pt_next_page: document has not yet been started");

  /* If there is no device then all pages are rendered */
  if ( pt->device == NULL ) {
    *p_next_page = XPSPT_PAGES_ALL;
    return TRUE ;
  }

  /* Get next page index from the PT device */
  if ( !pt_get_param(pt, STRING_AND_LENGTH("NextPage"), &param) ) {
    return FALSE ;
  }
  *p_next_page = theDevParamInteger(param);
  HQASSERT((*p_next_page >= XPSPT_COUNT_PAGES),
           "pt_next_page: invalid next page value");

  return TRUE ;

} /* pt_next_page */

/* Log stripped */
