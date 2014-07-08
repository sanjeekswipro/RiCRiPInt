/* Copyright (C) 2006-2007 Global Graphics Software Ltd. All rights reserved.
 *
 * $HopeName: SWptdev!src:prnttckt.h(EBDSDK_P.1) $
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 */
#ifndef __PRNTTCKT_H__
#define __PRNTTCKT_H__  (1)

#include "xmlcbcks.h"

/**
 * \file
 * \brief Print ticket handling.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PRINTTICKET PRINTTICKET;

/**
 * \brief Print ticket data is held in a type of DOM tree (Document
 * Object Model) - a tree of elements with pointers to parents,
 * siblings, and children.  There is a node per element type
 * (i.e. there are not separate start and end element nodes).
 *
 * For now PT elements only ever have zero or one attribute, and its value is
 * stored in the value union.  Value element CDATA is also stored in the value
 * union - the attribute for a Value element just defines type of CDATA data so
 * is effectively stored in value_type.
 */
typedef struct PT_PARAM {
  struct PT_PARAM*  p_sibling;  /**< Sibling parameter */
  struct PT_PARAM*  p_parent;   /**< Containing parameter */
  struct PT_PARAM*  p_children; /**< Contained parameters */
  struct PT_PARAM*  p_last_child; /**< Last contained parameter */

  int32             param_type; /**< Type of param */
  XML_QNAME*        name;       /**< Parameter name */

  int32             value_type; /**< Type of param value*/
  union {
    uint8*          string;
    int32           integer;
    double          decimal;
    XML_QNAME*      qname;
  } value;                      /**< Value of parameter */
} PT_PARAM;

/**
 * \brief Structure to hold information about active page areas.
 *
 * All values should be in PostScript points.
 * @note Setting fTrimToImageableArea to TRUE will change the output raster size
 * to match the imageable area (if set).
 */
typedef struct PT_PAGEAREAS {
  double arrFixedPageSize[2];  /**< Width, Height */
  double arrBleedBox[4];       /**< OriginX, OriginY, Width, Height */
  double arrContentBox[4];     /**< OriginX, OriginY, Width, Height */
  double arrImageableArea[4];  /**< OriginWidth, OriginHeight, ExtentWidth, ExtentHeight */
  int32 fTrimToImageableArea;  /**< Flag to enable trimming to the imageable
                                    area (if set). If /c FALSE then a clipping
                                    region is used instead. */
} PT_PAGEAREAS;

/** \brief Initialise new set of print ticket parameters */
int32 pt_initialise(
  PRINTTICKET** pt);


/** \brief Add a namespace to a PrintTicket */
int32 pt_add_ns(
  PRINTTICKET*  pt,
  XML_NS*       ns);


/* Types of PrintTicket elements */
#define PT_PARAM_UNDEFINED      (0)
#define PT_PARAM_FEATURE        (1)
#define PT_PARAM_OPTION         (2)
#define PT_PARAM_PARAMETERREF   (3)
#define PT_PARAM_PARAMETERINIT  (4)
#define PT_PARAM_SCOREDPROPERTY (5)
#define PT_PARAM_PROPERTY       (6)
#define PT_PARAM_VALUE          (7)

/* Constants used for PrintTicket validation */
#define PT_PARAM_CHECK_END      (-1)

#define PT_ONE_OF               (0)
#define PT_ONE_OR_MORE          (1)

/* Types of PrintTicket element values */
#define PT_VALUE_UNDEFINED      (0)
#define PT_VALUE_STRING         (1)
#define PT_VALUE_QNAME          (2)
#define PT_VALUE_INTEGER        (3)
#define PT_VALUE_DECIMAL        (4)


/**
 *  \brief Create the PS required to configure the RIP when starting to
 *  process a page.
 *
 *  Parse the specified Print Ticket at the page level, producing an equivalent
 *  PostScript representation which can then be passed to the /StartPage
 *  function in the Print Ticket procset.
 *
 *  \param  pPageAreas  Points to a structure defining various page areas (such
 *  as the size of the FixedPage, ContentBox area, etc). All values should be
 *  in PostScript points.
 *  \param  fXPSSignatureValid  Should be FALSE if signature is invalid
 *  \param  pTicket  The Print Ticket info
 *  \return a malloc'ed PS string (or NULL on error)
 *
 *  \see pt_createJobStartPS(), pt_createDocumentStartPS()
 */
uint8* pt_createPageStartPS (const PT_PAGEAREAS* pPageAreas,
                             int32 fXPSSignatureValid,
                             PRINTTICKET* pTicket);

/**
 *  \brief Create the PS required to configure the RIP when starting to
 *  process a document.
 *
 *  Parse the specified Print Ticket at the document level, producing an equivalent
 *  PostScript representation which can then be passed to the /StartDocument
 *  function in the Print Ticket procset.
 *
 *  \param  pTicket  The Print Ticket info
 *  \return a malloc'ed PS string (or NULL on error)
 *
 *  \see pt_createJobStartPS(), pt_createPageStartPS()
 */
uint8* pt_createDocumentStartPS (PRINTTICKET* pTicket);

/**
 *  \brief Produce the PS required to configure the RIP when starting to
 *  process a job.
 *
 *  Parse the specified Print Ticket at the job level, producing an equivalent
 *  PostScript representation which can then be passed to the /StartJob
 *  function in the Print Ticket procset.
 *
 *  \param  pTicket  The Print Ticket info
 *  \return a malloc'ed PS string (or NULL on error)
 *
 *  \see pt_createDocumentStartPS(), pt_createPageStartPS()
 */
uint8* pt_createJobStartPS (PRINTTICKET* pTicket);

/** \brief Add new print ticket parameter to tree and make it the
 * current one being updated */
int32 pt_add_new_param(
  PRINTTICKET*  pt,
  int32         param_type,
  XML_QNAME*    name);

/** \brief Set value of new print ticket element */
int32 pt_set_value(
  PRINTTICKET*  pt,
  int32         value_type,
  void*         value);

/** \brief Seen end tag of element, make the parent element the current one to add to */
void pt_end_param(
  PRINTTICKET*  pt);


/** \brief Check if the print ticket is complete.  Simple check to see
    if we are back at the root of the print ticket. */
int32 pt_complete(
  PRINTTICKET*  pt);


/** \brief Transfer print ticket to another pointer.  (Return another reference to the PrintTicket.) */
PRINTTICKET* pt_copy(
  PRINTTICKET*  pt);


/** \brief Release a PrintTicket and all of its content. */
void pt_release(
  PRINTTICKET** p_pt);


/* Constants representing PrintTicket scope level */
#define PT_LEVEL_UNKNOWN   (-1)
#define PT_LEVEL_JOB       (0)
#define PT_LEVEL_DOCUMENT  (1)
#define PT_LEVEL_PAGE      (2)

/** \brief Merge and validate a new PrintTicket with existing validated PrintTicket for
 * a specific level */
int32 pt_merge_and_validate(
  PRINTTICKET** p_pt,
  PRINTTICKET*  pt_valid,
  PRINTTICKET*  pt_new,
  int32         level);

/** \brief Find feature parameter in PrintTicket */
PT_PARAM* pt_find_feature(
  PRINTTICKET*  pt,
  uint8*        name);

/** \brief Find parameter init parameter in PrintTicket */
PT_PARAM* pt_find_paraminit(
  PRINTTICKET* pt,
  uint8*       name);

/** \brief Find sub parameter element of given type and name (can be null) */
PT_PARAM* pt_find_sub_param(
  PT_PARAM* param,
  int32     type,
  uint8*    name);

/** \brief Get type of value stored for parameter element */
int32 pt_param_value_type(
  PT_PARAM* param);

/** \brief Get value of parameter element */
void pt_param_value(
  PT_PARAM* param,
  void*     value);

/**
 * \brief Get the feature level of the specified parameter.
 *
 * I.e. 'JobNUp' would return \c PT_LEVEL_JOB.
 *
 * \param  param
 * \return A PT_LEVEL_xxx value.
 */
int32 pt_param_level(PT_PARAM* param);

/**
 * \brief Check each of the current param's children to ensure
 * they have the correct type.
 *
 * \param pt
 * \param pnValidChildren  A list of valid param types, terminated with
 * \c PT_PARAM_CHECK_END.
 * \return \c TRUE on success, \c FALSE otherwise.
 */
int32 pt_param_has_valid_children (
  PRINTTICKET* pt,
  int32*       pnValidChildren);

/**
 * \brief Check the current param has the correct quantity of specific
 * child elements.
 *
 * Specifying \c PT_ONE_OF means the current param must have <em>exactly one</em>
 * child with one of the specified param types.  \c PT_ONE_OR_MORE means
 * it must have <em>at least one</em> of the specified param types.
 *
 * \param pt
 * \param nCondition  One of \c PT_ONE_OF or \c PT_ONE_OR_MORE.
 * \param pnValidChildren  A list of valid param types, terminated with
 * \c PT_PARAM_CHECK_END.
 * \return \c TRUE on success, \c FALSE otherwise.
 */
int32 pt_param_has_required_children (
  PRINTTICKET* pt,
  int32        nCondition,
  int32*       pnValidChildren);

/**
 * \brief Determine whether the current param has a child object.
 *
 * \param pt
 * \return \c TRUE if it has at least one child, \c FALSE otherwise.
 */
int32 pt_param_has_children (
  PRINTTICKET* pt);

#ifdef __cplusplus
}
#endif

#endif /* !__PRNTTCKT_H__ */


/* EOF prnttckt.h */
