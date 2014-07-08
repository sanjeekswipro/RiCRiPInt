/** \file
 * \ingroup corexml
 *
 * $HopeName: CORExml!src:xmlops.c(EBDSDK_P.1) $
 * $Id: src:xmlops.c,v 1.58.4.1.1.1 2013/12/19 11:25:09 anon Exp $
 *
 * Copyright (C) 2006-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS xmlexec operator and friends.
 */

#include "core.h"

#include "control.h"    /* ps_interpreter_level */
#include "dictscan.h"   /* dictmatch */
#include "fileio.h"     /* FILELIST */
#include "mmcompat.h"   /* mm_alloc_with_header etc.. */
#include "namedef_.h"   /* NAME_xmlexec, &c. */
#include "objectt.h"    /* OBJECT */
#include "params.h"     /* setparam, currentparam */
#include "stacks.h"     /* operandstack */
#include "swerrors.h"   /* UNDEFINED */

#include "xml.h"
#include "xmlcontext.h"
#include "xmlintern.h"
#include "psdevuri.h"
#include "xmlrecognitionpriv.h"

Bool xml_map_namespace(const xmlGIStr *from_uri, const xmlGIStr **to_uri)
{
  XMLExecContext *p_xmlexec_context;
  OBJECT *theo ;

  HQASSERT(from_uri != NULL, "from_uri is NULL") ;
  HQASSERT(to_uri != NULL, "to_uri is NULL") ;

  p_xmlexec_context = SLL_GET_HEAD(&xml_context.sls_contexts, XMLExecContext, sll);
  HQASSERT(p_xmlexec_context != NULL, "p_xmlexec_context is NULL") ;

  oName(nnewobj) = (NAMECACHE *)&(from_uri->name) ;

  if ( (theo = p_xmlexec_context->map_uri_data) == NULL ||
       (theo = extract_hash(theo, &nnewobj)) == NULL ) {
    *to_uri = from_uri ; /* No mapping possible or present. */
    return TRUE ;
  }

  switch ( oType(*theo) ) {
  case ONAME:
    *to_uri = (xmlGIStr *)oName(*theo) ;
    return TRUE ;
  case OSTRING:
    if ( intern_create(to_uri, oString(*theo), theLen(*theo)) )
      return TRUE ;
    /*@fallthrough@*/
  default:
    break ;
  }

  return error_handler(UNDEFINED) ;
}

static void parse_error_cb(
      xmlGParser *xml_parser,
      hqn_uri_t *uri,
      uint32 line,
      uint32 column,
      uint8 *detail,
      int32  detail_len)
{
  XMLExecContext *p_xmlexec_context ;
  uint8 *uri_name ;
  uint32 uri_name_len ;

  HQASSERT(xml_parser != NULL, "xml_parser is NULL") ;

  UNUSED_PARAM(xmlGParser*, xml_parser);

  p_xmlexec_context = SLL_GET_HEAD(&xml_context.sls_contexts, XMLExecContext, sll) ;

  /* Because we go recursive on parse instances, protect against
     raising a PS error more than once. */
  /** \todo if xmlexec goes recursive, we will raise a PS error more than
     once. */
  if (p_xmlexec_context->error_occured)
    return ;

  /* Although this should never happen, continue to report at least
     some error rather than no error at all. */
  if (! hqn_uri_get_field(uri, &uri_name, &uri_name_len, HQN_URI_ENTIRE)) {
    uri_name = (uint8 *)"Invalid URI while handling error condition." ;
    uri_name_len = strlen_uint32((const char *)uri_name) ;
  }

  /* We set the default parse error callback to a generic form so that
     we do not display a PDL specfic error message when we don't yet
     know the PDL type. */
#define SHORTFORM ("URI: %.*s; Line: %d; Column: %d.")
#define LONGFORM ("URI: %.*s; Line: %d; Column: %d; XMLInfo: %.*s.")

  /* libgenxml internal errors may as well come out as UNDEFINED PS
     errors */
  if (detail == NULL || detail_len == 0) {
    (void)detailf_error_handler(UNDEFINED, SHORTFORM, uri_name_len, uri_name, line,
                                column) ;
  } else {
    (void)detailf_error_handler(UNDEFINED, LONGFORM, uri_name_len, uri_name, line,
                                column, detail_len, detail) ;
  }
}

/* The PS operator. */
Bool xmlexec_(ps_context_t *pscontext)
{
  int32 num_args = 1;
  int32 stack_size;
  int32 status;
  OBJECT *ofile;
  OBJECT *odict = NULL, *aliases = NULL ;
  FILELIST *flptr ;
  XMLExecContext *p_xmlexec_context;
  hqn_uri_t *base_uri, *uri ;
  xmlGFilterChain *filter_chain ;
  xmlGFilter *recognition_filter ;
  int32 save_ps_interpreter_level = ps_interpreter_level ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  /* need some arguments... */
  if ( isEmpty(operandstack) )
    return error_handler(STACKUNDERFLOW) ;

  /* assume just file on stack */
  stack_size = theStackSize(operandstack) ;
  ofile = TopStack(operandstack, stack_size) ;

  if ( oType(*ofile) == ODICTIONARY ) {
    /* got parameter dict - look for file next */
    odict = ofile ;
    num_args++ ;

    /* should be a file under the dict */
    if ( stack_size < 1 )
      return error_handler(STACKUNDERFLOW) ;

    /* TBD */
    ofile = (&odict[-1]) ;
    if ( ! fastStackAccess(operandstack) )
      ofile = stackindex(1, &operandstack) ;
  }

  /* check we have a readable input file */
  if ( oType(*ofile) != OFILE )
    return error_handler(TYPECHECK) ;

  flptr = oFile(*ofile) ;
  if ( ! isIOpenFileFilter(ofile, flptr) || !isIInputFile(flptr) || isIEof(flptr) )
    return error_handler(IOERROR) ;

  /** \todo TODO - at this point we have a live input stream and optionally a
   * parameters dictionary.  Next update global parameters from the dict
   * (if present).
   */
  if ( odict != NULL ) {
    enum {
      match_Aliases,
      match_dummy
    } ;
    static NAMETYPEMATCH match[match_dummy + 1] = {
      { NAME_NamespaceAliases | OOPTIONAL, 1, { ODICTIONARY }},
      DUMMY_END_MATCH
    } ;

    if ( !dictmatch(odict, match) )
      return FALSE ;

    /* Use the dictionary directly as an alias mapping table. The dictionary
       is left on the operand stack for the duration of the xmlexec call, and
       the context is destroyed before returning, so it doesn't need GC
       scanning from the XML code. */
    aliases = match[match_Aliases].result ;
  }

  /* Create the xml exec context */
  if (! xmlexec_context_create(&p_xmlexec_context) )
    return FALSE ;

  if (! psdev_base_uri_from_open_file(flptr, &base_uri)) {
    xmlexec_context_destroy(&p_xmlexec_context) ;
    return error_handler(UNDEFINED) ;
  }

  if (! psdev_uri_from_open_file(flptr, &uri)) {
    hqn_uri_free(&base_uri) ;
    xmlexec_context_destroy(&p_xmlexec_context) ;
    return FALSE;
  }

  p_xmlexec_context->xml_flptr = flptr;
  p_xmlexec_context->xmlexec_params = odict;
  if ( aliases != NULL ) {
    p_xmlexec_context->map_uri = xml_map_namespace ;
    p_xmlexec_context->map_uri_data = aliases ;
  }

  /* User data is set to NULL as we don't know what the PDL is yet. */
  if (! xmlg_fc_new(core_xml_subsystem, &filter_chain,
                    &xmlexec_memory_handlers,
                    uri, base_uri, NULL /* user data */ )) {

    hqn_uri_free(&uri) ;
    hqn_uri_free(&base_uri) ;
    xmlexec_context_destroy(&p_xmlexec_context) ;
    return error_handler(UNDEFINED) ;
  }

  if (! recognition_xml_filter_init(filter_chain, 1, &recognition_filter)) {
    xmlg_fc_destroy(&filter_chain) ;
    hqn_uri_free(&uri) ;
    hqn_uri_free(&base_uri) ;
    xmlexec_context_destroy(&p_xmlexec_context) ;
    return FALSE;
  }

  /* Set the parse error callback to be XML generic as we do not know
     which PDL it is yet. */
  xmlg_fc_set_parse_error_cb(filter_chain, parse_error_cb) ;

  /* We do not know what the document elements can be at this
     stage. */

  /** \todo When we have a more robust namespace recognition
   * environment, the allowable document elements can be set
   * correctly. Example, if we discover that the xmlexec is being
   * executed on a content type stream for XPS, then we can set the
   * document element checking appropriately.
   */
  status = xml_parse_stream(flptr, filter_chain) ;

  xmlg_fc_destroy(&filter_chain) ;
  hqn_uri_free(&uri) ;
  hqn_uri_free(&base_uri) ;
  xmlexec_context_destroy(&p_xmlexec_context);

  /* pop arguments from stack only if xml parsed without error */
  if (status) {
    npop(num_args, &operandstack) ;
    HQASSERT(ps_interpreter_level == save_ps_interpreter_level,
             "ps interpreter level has become corrupt") ;
  } else {
    /* Under error condition we do not know if the PS interpreter
       level has been decremented correctly, so reset it to what it
       was when xmlexec started. */

    HQASSERT(ps_interpreter_level >= save_ps_interpreter_level,
             "ps interpreter level has become corrupt") ;
    ps_interpreter_level = save_ps_interpreter_level ;
  }

  return status ;
}

/* setxmlparams PS operator - sets current xml parameters. */
Bool setxmlparams_(ps_context_t *pscontext)
{
  corecontext_t *context = ps_core_context(pscontext) ;
  OBJECT* odict ;

  /* Check there is a readable dict on top of operand stack */
  if ( isEmpty(operandstack) ) {
    return error_handler(STACKUNDERFLOW) ;
  }

  odict = theTop(operandstack) ;

  if ( oType(*odict) != ODICTIONARY ) {
    return error_handler(TYPECHECK) ;
  }

  if ( !oCanRead(*oDict(*odict)) ) {
    if ( !object_access_override(odict) ) {
      return error_handler(INVALIDACCESS) ;
    }
  }

  /** \todo TODO - validate and merge new global xml parameters */

  /* And add any other parameters registered with xmlparamlist */
  if ( !setparams(context, odict, context->xmlparamlist) )
    return FALSE ;

  /* remove dict from stack */
  pop(&operandstack) ;

  return TRUE ;
}


/* currentxmlparams PS operator - returns dict with current xml
 * parameters.
 */
Bool currentxmlparams_(ps_context_t *pscontext)
{
  return currentparams(pscontext, ps_core_context(pscontext)->xmlparamlist) ;
}

/* ============================================================================
* Log stripped */
