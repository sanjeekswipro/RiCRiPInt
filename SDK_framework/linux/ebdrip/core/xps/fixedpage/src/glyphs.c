/** \file
 * \ingroup fixedpage
 *
 * $HopeName: COREedoc!fixedpage:src:glyphs.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Implementation of XPS fixed payload glyph callbacks.
 *
 * See glyph_functions declaration at the end of this file for the element
 * callbacks this file implements.
 */

#include "core.h"

#include "swerrors.h"
#include "xml.h"
#include "tt_font.h"          /* tt_xps_lookup() */
#include "fontops.h"          /* gs_setfont() */
#include "charsel.h"          /* char_selector_t */
#include "routedev.h"         /* DEVICE_SETG */
#include "graphics.h"
#include "gu_cons.h"          /* gs_moveto() */
#include "gstate.h"
#include "tranState.h"
#include "hqunicode.h"        /* utf8_buffer */
#include "vndetect.h"         /* flush_vignette() */
#include "display.h"          /* finishaddchardisplay() */
#include "fcache.h"           /* plotchar() */
#include "gu_ctm.h"           /* gs_modifyctm() */
#include "gu_path.h"          /* path_*() */
#include "clipops.h"          /* gs_addclip() */
#include "system.h"           /* path_free_list() */
#include "swmemory.h"         /* gs_cleargstates() */
#include "showops.h"          /* [xy]width */
#include "objnamer.h"
#include "rectops.h"
#include "render.h"
#include "pattern.h"
#include "idlom.h"
#include "params.h"
#include "implicitgroup.h"
#include "pathcons.h"         /* gs_newpath() */
#include "plotops.h"
#include "lowmem.h" /* mm_memory_is_low */
#include "control.h" /* handleNormalAction */

#include "xmltypeconv.h"
#include "xpspriv.h"
#include "xpsfonts.h"         /* xps_getfont() */
#include "xpsscan.h"          /* xps_convert_utf8 */
#include "fixedpagepriv.h"

#include "namedef_.h"

#ifdef DEBUG_BUILD
#include "miscops.h"      /* run_ps_string */
#include "xmlg.h"         /* xmlg_line_and_column */
#include "swcopyf.h"      /* swncopyf */
#include "xpsdebug.h"
#endif

#define GLYPHS_STATE_NAME "XPS Glyphs"

/* 20 degree shear for ItalicSimulation */
#define ITALICSIMULATION_SHEAR 0.363970234266202

/** \brief Structure to contain Glyphs state. */
typedef struct xpsGlyphsState_s {
  xpsCallbackStateBase base;

  /** For the gsave/grestore around the Glyphs. */
  int32 gstate_id;

  Bool transform_invertible ; /**< RenderTransform is invertible; otherwise Path will be transparent. */
  OMATRIX transform ; /**< Transform matrix for Clip, Fill, Stroke. */
  OMATRIX savedctm ;  /**< Copy of CTM before modification. */

  /** Abbreviated paths are scanned in the commit after transform matrix is known. */
  utf8_buffer abbreviated_clip ;
  PATHINFO clip ;     /**< Clip path property. */
  int32 clip_rule;    /**< FillRule associated with clipping path. */

  int32 paint_flags; /**< Current paint style (one of XPS_PAINT_*). */

  OBJECT*       font;
  utf8_buffer   unicodestring;
  utf8_buffer   indices;
  SYSTEMVALUE   originx;
  SYSTEMVALUE   originy;
  double        fontrenderingemsize;
  int32         bidilevel;

  int32 gstate_mask_id; /**< gsave id for the opacity mask if saved. */
  Group* mask_group; /**< Group object for the opacity mask. */

  USERVALUE saved_opacity ; /** Manually save/restore opacity for efficiency. */

  Bool pattern_ok ; /**< Is it OK to accept pattern child elements? */
  Bool sideways;
  int32 style;

  OBJECT_NAME_MEMBER
} xpsGlyphsState ;

/** \brief Indices string parsing state. */
typedef struct GLYPH_PARSE {
  utf8_buffer   buffer;     /**< Indices string being parsed. */
} GLYPH_PARSE;

/** \brief Data for a single entry in the Glyphs Indices attribute. */
typedef struct GLYPH_SPECIFICATION {
  int32   index;        /**< Font glyph index. \c -1 if no index is given. */
  UTF32   codepoint;    /**< UnicodeString glyph codepoint. \c -1 if no codepoint available. */
  int32   c_codeunits;  /**< Number of codepoints in \c UnicodeString for cluster. */
  int32   c_glyphs;     /**< Number of glyph indexes in cluster. */
  Bool    advance_set;  /**< The glyph advance is specified. */
  double  advance;      /**< Advance width of next glyph relative to origin of this glyph. */
  double  uoffset;      /**< Offset relative to this glyph origin to move this glyph. */
  double  voffset;      /**< Offset relative to this glyph origin to move this glyph. */
} GLYPH_SPECIFICATION;

/** \brief Glyph selector state. */
typedef struct GLYPH_SELECTOR {
  int32         state;        /**< Selector state. */
  GLYPH_PARSE   parse;        /**< Indices parse state. */
  utf8_buffer   unicodestring_iter; /**< UnicodeString iterator. */
  UTF16         codeunits[2]; /**< UTF-16 codeunits for current UTF-8 codepoint. */
  utf16_buffer  utf16;        /**< UTF-16 codeunit iterator. */
  int32         c_glyphs;     /**< Number of glyphs in a mapping. */
  int32         c_codeunits;  /**< Number of codeunits in a mapping. */
  int32         c_char;       /**< Number of glyphs to plot. */

  GLYPH_SPECIFICATION glyph;  /**< Specification of current selected glyph. */
} GLYPH_SELECTOR;

/* States of the Glyph selector */
#define GS_STATE_INDICES  (1)
#define GS_STATE_MAPPING  (2)
#define GS_STATE_UNISTR   (3)

/** \brief Default Glyph specification. */
static GLYPH_SPECIFICATION glyph_spec_default = {
  -1, -1, 1, 1, FALSE, 0.0, 0.0, 0.0
};

/** Are we in glyphs, and if so is it OK to expand a pattern? */
Bool glyphs_pattern_valid(xpsCallbackState *state)
{
  xpsGlyphsState *glyphs_state = (xpsGlyphsState*)state ;
  HQASSERT(state != NULL, "No state") ;

  return (glyphs_state->base.type == XPS_STATE_GLYPHS &&
          glyphs_state->pattern_ok) ;
}

/**
 * \brief
 * Initialise parse state of a Glyphs Indices attribute.
 *
 * \param[out] p_parse
 * Pointer to Indices parse state to initialise.
 * \param[in] p_indices
 * Pointer to Indices string to be parsed.
 */
static
void parse_init(
/*@out@*/ /*@notnull@*/
  GLYPH_PARSE*        p_parse,
/*@in@*/ /*@notnull@*/
  utf8_buffer*        p_indices)
{
  HQASSERT((p_parse != NULL),
           "parse_init: NULL glyph parse pointer");
  HQASSERT((p_indices != NULL),
           "parse_init: NULL parse buffer pointer");

  p_parse->buffer = *p_indices;

} /* parse_init */


/**
 * \brief
 * Indicate if there is more Indices string data to parse.
 *
 * \param[in] p_parse
 * Pointer to Indices parse state.
 *
 * \return
 * \c TRUE if there is more Indices string data to parse, else \c FALSE.
 */
static
Bool parse_more(
/*@in@*/ /*@notnull@*/
  GLYPH_PARSE*    p_parse)
{
  HQASSERT((p_parse != NULL),
           "parse_more: NULL parse pointer");

  return(utf8_iterator_more(&p_parse->buffer));

} /* parse_more */


/**
 * \brief
 * Raise a PS error from parsing the Glyphs Indices and UnicodeString attributes
 * keeping the error message in the same format as the xmlg attributes match
 * mechanism.
 *
 * \param[in] filter
 * Pointer to XML filter.
 * \param[in] attr
 * Pointer to ttribute with match error.
 * \param[in] error
 * PS error to raise.
 *
 * \return
 * \c Always returns FALSE.
 */
static
Bool glyphs_parse_error(
/*@in@*/ /*@notnull@*/
  xmlGFilter* filter,
/*@in@*/ /*@notnull@*/
  xmlGIStr*   attr,
  int32       error)
{
  HQASSERT((filter != NULL),
           "parse_error: NULL filter pointer");
  HQASSERT((attr != NULL),
           "parse_error: NULL attribute name pointer");
  HQASSERT(((attr == XML_INTERN(Indices)) || (attr == XML_INTERN(UnicodeString))),
           "parse_error: invalid Glyphs attribute name");

  /* Usually the type converter invoked by the match call raises a PS error.
   * Hence, we need to do this first before invoking the match error. */
  (void)error_handler(error);
  (void)xmlg_attributes_invoke_match_error(filter, XML_INTERN(Glyphs), /* We do not track prefix */ NULL,
                                           NULL, attr);
  return(FALSE);

} /* glyphs_parse_error */


/** \brief Raise error for a Glyphs element Indices attribute. */
Bool indices_parse_error(
/*@in@*/ /*@notnull@*/
  xmlGFilter* filter,
  int32       error);
#define indices_parse_error(f, e) (glyphs_parse_error((f), XML_INTERN(Indices), (e)))
/** \brief Raise error for a Glyphs element UnicodeSting attribute. */
Bool ustring_parse_error(
/*@in@*/ /*@notnull@*/
  xmlGFilter* filter,
  int32       error);
#define ustring_parse_error(f, e) (glyphs_parse_error((f), XML_INTERN(UnicodeString), (e)))

/**
 * \brief
 * Parse next glyph cluster from Indices.
 *
 * From 0.90 spec.
 *
 *    <!-- Indices grammar for Glyphs.Indices -->
 *    <xs:simpleType name="ST_Indices">
 *        <xs:restriction base="xs:string">
 *            <xs:whiteSpace value="collapse" />
 *<!--
 *            <xs:pattern value="(\
 *                                ((\([pint](:[pint])?\))?[uint])?\
 *                                (,[prn]?(,[rn]?(,[rn])?)?)?\
 *                                )\
 *                                (;\
 *                                    ((\([pint](:[pint])?\))?[uint])?\
 *                                    (,[prn]?(,[rn]?(,[rn])?)?)?\
 *                                )*" />
 *-->
 *            <xs:pattern value="(((\(([1-9][0-9]*)(:([1-9][0-9]*))?\))?([0-9]+))?(,(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)?(,((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)?(,((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))?)?)?)(;((\(([1-9][0-9]*)(:([1-9][0-9]*))?\))?([0-9]+))?(,(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)?(,((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)?(,((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?))?)?)?)*" />
 *        </xs:restriction>
 *    </xs:simpleType>
 *
 * Glyph specification is of the form:
 * - [(CodeunitCount[:GlyphCount])][GlyphIndex[,Advance[,uOffset[,vOffset]]]]
 *
 * Other points to note:
 * - It would seem that no whitespace is allowed.
 * - The pattern comment in the 0.75 s0schema.xsd is different from the spec.
 *
 * \param[in] filter
 * Pointer to XML filter.
 * \param[in] p_parse
 * Pointer to Indices string parse state.
 * \param[out] p_glyph
 * Pointer to returned glyph data.
 *
 * \return
 * \c TRUE if glyph data is successfully parsed from the Indices string, else \c
 * FALSE.
 */
static
Bool parse_next_index(
/*@in@*/ /*@notnull@*/
  xmlGFilter *filter,
/*@in@*/ /*@notnull@*/
  GLYPH_PARSE*      p_parse,
/*@out@*/ /*@notnull@*/
  GLYPH_SPECIFICATION*  p_glyph)
{
  Bool  cluster_set;
  int32 error_type ;

  HQASSERT((filter != NULL),
           "parse_next_index: NULL filter pointer");
  HQASSERT((p_parse != NULL),
           "parse_next_index: NULL parse state pointer");
  HQASSERT((p_glyph != NULL),
           "parse_next_index: NULL glyph data pointer");

#define error_handler DO NO USE error_handler, use indices_parse_error instead!

  /* Initialise indices glyph structure with defaults */
  *p_glyph = glyph_spec_default;

  /* Parse optional glyph cluster mapping */
  cluster_set = FALSE;
  if ( xml_match_unicode(&p_parse->buffer, '(') ) {
    /* Get number of UnicodeString codeunits */
    if ( !xps_xml_to_int(&p_parse->buffer, &p_glyph->c_codeunits, xps_pint, &error_type) ) {
      return indices_parse_error(filter, error_type) ;
    }
    /* Parse optional glyph count */
    if ( xml_match_unicode(&p_parse->buffer, ':') &&
         !xps_xml_to_int(&p_parse->buffer, &p_glyph->c_glyphs, xps_pint, &error_type) ) {
      return indices_parse_error(filter, error_type) ;
    }
    if ( !xml_match_unicode(&p_parse->buffer, ')') ) {
      return indices_parse_error(filter, SYNTAXERROR) ;
    }
    cluster_set = TRUE;
  }

  /* Parse optional font glyph index */
  if ( xps_xml_to_int(&p_parse->buffer, &p_glyph->index, xps_uint, &error_type) ) {
    /* Must be +ve 16-bit index */
    if ( (p_glyph->index&(~0xffffu)) != 0 ) {
      return indices_parse_error(filter, RANGECHECK) ;
    }
  } else if ( cluster_set ) {
    /* If there is a cluster map then there must be glyph id */
    return(indices_parse_error(filter, SYNTAXERROR));
  }

  if ( xml_match_unicode(&p_parse->buffer, ',') ) {
    /* Get advance */
    if ( xps_xml_to_double(&p_parse->buffer, &p_glyph->advance, xps_prn, &error_type) ) {
      p_glyph->advance_set = TRUE;
    }

    if (xml_match_unicode(&p_parse->buffer, ',')) {
      /* Get origin u offset */
      (void)xps_xml_to_double(&p_parse->buffer, &p_glyph->uoffset, xps_rn, &error_type);

      if ( xml_match_unicode(&p_parse->buffer, ',') ) {
        /* Get origin v offset */
        (void)xps_xml_to_double(&p_parse->buffer, &p_glyph->voffset, xps_rn, &error_type);
      }
    }
  }

  /* Check valid end of specification */
  if ( (p_parse->buffer.unitlength > 0) &&
       !xml_match_unicode(&p_parse->buffer, ';') ) {
    return indices_parse_error(filter, SYNTAXERROR) ;
  }

#undef error_handler
  return(TRUE);
} /* parse_next_index */


/**
 * \brief
 * Check if there are more UFT-16 code units to be read from UnicodeString.
 *
 * \param[in] p_glyph_selector
 * Pointer to glyph selector containing current UTF-16 encoded codepoint and
 * UnicodeString iterator.
 *
 * \return
 * \c TRUE if there are more code units that can be read, else \c FALSE.
 */
static
Bool more_utf16_codepoint(
/*@in@*/ /*@notnull@*/
  GLYPH_SELECTOR* p_glyph_selector)
{
  HQASSERT((p_glyph_selector != NULL),
           "more_utf16_codepoint: NULL glyph selector pointer");

  return(utf16_iterator_more(&p_glyph_selector->utf16) ||
         utf8_iterator_more(&p_glyph_selector->unicodestring_iter));

} /* more_utf16_codepoint */


/**
 * \brief
 * Return the next UTF-16 code unit for the UnicodeString.
 *
 * The UnicodeString is held internally in UTF-8 encoding.  To not have to
 * allocate yet another buffer to transcode the UnicodeString to UTF-16, each
 * codepoint in the UTF-8 version is transcoded to a fixed size UTF-16 buffer,
 * and then that buffer is iterated over.  When the UTF-16 buffer containing the
 * code units for the last codepoint is empty, the next UnicodeString codepoint
 * is transcoded UTF-16 so the code units can be iterated over.
 *
 * \param[in] p_glyph_selector
 * Pointer to glyph selector containing current UTF-16 encoded codepoint and
 * UnicodeString iterator.
 * \param[out] p_codeunit
 * Pointer to returned UTF-16 code unit.
 *
 * \return
 * \c TRUE if counted UTF-16 code units without any UTF-8 to UTF-16 transcoding
 * errors, else \c FALSE.
 */
static
Bool next_utf16_codeunit(
/*@in@*/ /*@notnull@*/
  GLYPH_SELECTOR* p_glyph_selector,
/*@out@*/ /*@notnull@*/
  UTF16*          p_codeunit)
{
  HQASSERT((p_glyph_selector != NULL),
           "next_utf16_codeunit: NULL glyph selector pointer");
  HQASSERT((more_utf16_codepoint(p_glyph_selector)),
           "next_utf16_codeunit: no more codeunits to return");
  HQASSERT((p_codeunit != NULL),
           "next_utf16_codeunit: NULL pointer to returned codeunit");

  /* If no outstanding code units convert next UTF-8 codepoint */
  if ( !utf16_iterator_more(&p_glyph_selector->utf16) ) {
    utf16_iterator_init(&p_glyph_selector->utf16, p_glyph_selector->codeunits, 2);
    if ( unicode_to_utf16(utf8_iterator_get_next(&p_glyph_selector->unicodestring_iter),
                          &p_glyph_selector->utf16) != UTF_CONVERT_OK ) {
      /* Should never happen as the only errors possible are in the utf8 string
       * is empty or there is not enough space in the utf16 string. */
      return(FALSE);
    }
    p_glyph_selector->utf16.unitlength = 2 - p_glyph_selector->utf16.unitlength;
    p_glyph_selector->utf16.codeunits = p_glyph_selector->codeunits;
  }

  *p_codeunit = *p_glyph_selector->utf16.codeunits++;
  p_glyph_selector->utf16.unitlength--;
  return(TRUE);

} /* next_utf16_codeunit */


/**
 * \brief
 * Skip a number of UTF-16 code units in the UnicodeString.
 *
 * \param[in] p_glyph_selector
 * Pointer to glyph selector containing current UTF-16 encoded codepoint and
 * UnicodeString iterator.
 * \param[in] count
 * Number of UTF-16 code units to skip, must be 1 or more.
 *
 * \return
 * \c TRUE if counted UTF-16 code units without any UTF-8 to UTF-16 transcoding
 * errors, else \c FALSE.
 */
static
Bool skip_utf16_codeunits(
/*@in@*/ /*@notnull@*/
  GLYPH_SELECTOR* p_glyph_selector,
  int32           count)
{
  UTF16 codeunit;

  HQASSERT((p_glyph_selector != NULL),
           "skip_utf16_codeunits: NULL glyph selector pointer");
  HQASSERT((count > 0),
           "skip_utf16_codeunits: invalid code unit skip count");

  /* Note: don't short circuit counting surrogate pairs since it is valid (if
   * stoopid) XPS to have a cluster map stop after the first of a surrogate
   * pair. */
  while ( (count > 0) && more_utf16_codepoint(p_glyph_selector) ) {
    count-- ;
    if ( !next_utf16_codeunit(p_glyph_selector, &codeunit) ) {
      return(FALSE);
    }
  }

  return(count == 0);
} /* skip_utf16_codeunits */


/**
 * \brief
 * Count the number UTF-16 code units for the remainder of the UnicodeString.
 *
 * \param[in] p_glyph_selector
 * Pointer to glyph selector containing current UTF-16 encoded codepoint and
 * UnicodeString iterator.
 * \param[out] p_count
 * Pointer to returned number of UTF-16 code units remaining.
 *
 * \return
 * \c TRUE if counted UTF-16 code units without any UTF-8 to UTF-16 transcoding
 * errors, else \c FALSE.
 */
static
Bool count_utf16_codeunits(
/*@in@*/ /*@notnull@*/
  GLYPH_SELECTOR* p_glyph_selector,
/*@out@*/ /*@notnull@*/
  int32*          p_count)
{
  UTF16 codeunit;
  int32 count;

  HQASSERT((p_glyph_selector != NULL),
           "count_utf16_codeunits: NULL glyph selector pointer");
  HQASSERT((p_count != NULL),
           "count_utf16_codeunits: NULL pointer to returned code unit count");

  count = 0;
  if ( more_utf16_codepoint(p_glyph_selector) ) {
    do {
      if ( !next_utf16_codeunit(p_glyph_selector, &codeunit) ) {
        return(FALSE);
      }
      /* Short circuit when surrogate pair generated */
      count += 1 + p_glyph_selector->utf16.unitlength;
      p_glyph_selector->utf16.unitlength = 0;
    } while ( utf8_iterator_more(&p_glyph_selector->unicodestring_iter) );
  }

  *p_count = count;
  return(TRUE);

} /* count_utf16_codeunits */


/**
 * \brief
 * Initialise Glyph UnicodeString/Indices glyph selector state, and also
 * validate Indices glyph specifications.
 *
 * There are a number of benefits to validating the Indices value at this point.
 * - Find the number of glyphs that will be added to the display list.
 * - Less error checking in the main glyph selector function making it simpler.
 *
 * \param[in] filter
 * Pointer to XML filter.
 * \param[in] p_glyph_selector
 * Pointer to glyph selector to initialise.
 * \param[in] p_state
 * Pointer to Glyphs state.
 *
 * \return
 * \c TRUE if the Indices value is validated ok else \c FALSE.
 */
static
Bool glyph_selector_init(
/*@in@*/ /*@notnull@*/
  xmlGFilter*     filter,
/*@out@*/ /*@notnull@*/
  GLYPH_SELECTOR* p_glyph_selector,
/*@in@*/ /*@notnull@*/
  xpsGlyphsState* p_state)
{
  int32         count;
  int32         c_codeunits = 0;
  int32         max_index;
  GLYPH_SPECIFICATION glyph;
  GLYPH_PARSE   parse;
  utf8_buffer*  p_unicodestring;
  utf8_buffer*  p_indices;
  Bool          unicodestring_set;

  HQASSERT((filter != NULL),
           "glyph_selector_init: NULL filter pointer");
  HQASSERT((p_glyph_selector != NULL),
           "glyph_selector_init: NULL selector pointer");
  HQASSERT((p_state != NULL),
           "glyph_selector_init: NULL state pointer");

  p_unicodestring = &p_state->unicodestring;
  p_indices = &p_state->indices;
  unicodestring_set = (p_unicodestring->codeunits != NULL) ;

  HQASSERT((p_unicodestring != NULL),
           "glyph_selector_init: NULL UnicodeString pointer");
  HQASSERT((p_indices != NULL),
           "glyph_selector_init: NULL UnicodeString pointer");

  /* Get the maximum glyph index */
  if ( !tt_xps_maxindex(p_state->font, &max_index) )
    return FALSE;

  /* UTF-16 code unit buffer is initially empty */
  utf16_iterator_init(&p_glyph_selector->utf16, p_glyph_selector->codeunits, 0);
  p_glyph_selector->unicodestring_iter = *p_unicodestring;

  p_glyph_selector->c_char = 0;

  /* Validate Indices string, and count how many glyphs will be plotted */
  parse_init(&parse, p_indices);
  for (;;) {
    if ( parse_more(&parse) ) {
      /* Parse next entry in Indices */
      if ( !parse_next_index(filter, &parse, &glyph) ) {
        return(FALSE);
      }

      /* Increase number of glyphs to be rendered */
      p_glyph_selector->c_char += glyph.c_glyphs;

      /* Check there is a UnicodeString code unit/point if no glyph index specified
       * and it is a 1:1 cluster map. */
      if ( ((glyph.c_codeunits == 1) && (glyph.c_glyphs == 1)) &&
           (glyph.index < 0) && ! more_utf16_codepoint(p_glyph_selector) ) {
        return(indices_parse_error(filter, SYNTAXERROR));
      }

      /* Check glyph index isn't outside range for font */
      if ( glyph.index > max_index )
        return(indices_parse_error(filter, RANGECHECK));

      /* Skip UnicodeString code units according to cluster map. */
      if ( unicodestring_set && !skip_utf16_codeunits(p_glyph_selector, glyph.c_codeunits) ) {
        return(ustring_parse_error(filter, RANGECHECK));
      }

      /* Skip additional glyphs according to cluster map. */
      count = glyph.c_glyphs;
      while ( --count > 0 ) {
        if ( !parse_next_index(filter, &parse, &glyph) ) {
          return(FALSE);
        }
        /* Check glyph index is given and that there is no cluster map. */
        if ( (glyph.index < 0 ) ||
             (glyph.c_codeunits > 1) || (glyph.c_glyphs > 1) ) {
          return(indices_parse_error(filter, SYNTAXERROR));
        }
      }

    } else { /* Add in remaining number of code units. */
      if ( unicodestring_set && !count_utf16_codeunits(p_glyph_selector, &c_codeunits) ) {
        return(ustring_parse_error(filter, RANGECHECK));
      }
      p_glyph_selector->c_char += c_codeunits;
      /* Finished validation */
      break;
    }
  }

  /* Set up iterators of UnicodeString and Indices */
  parse_init(&p_glyph_selector->parse, p_indices);
  p_glyph_selector->unicodestring_iter = *p_unicodestring;

  /* UTF-16 code unit buffer is initially empty */
  utf16_iterator_init(&p_glyph_selector->utf16, p_glyph_selector->codeunits, 0);

  /* Initial glyph selector state */
  p_glyph_selector->state = GS_STATE_INDICES;

  return(TRUE);

} /* glyph_selector_init */


static Bool xps_notdef(
  char_selector_t*  selector,
  int32             type,
  int32             charCount,
  FVECTOR           *advance,
  void*             data)
{
  char_selector_t selector_copy ;

  UNUSED_PARAM(void *, data) ;

  HQASSERT(selector, "No char selector for PS notdef character") ;
  /* Note: cid > 0 in this assertion, because we shouldn't be notdef mapping
     the notdef cid (value 0) */
  HQASSERT(selector->cid > 0, "XPS notdef char selector is not a defined CID") ;

  selector_copy = *selector ;

  /* No CMap lookup for notdef. Use CID 0 (notdef) in current font instead */
  selector_copy.cid = 0 ;

  return plotchar(&selector_copy, type | DOXPS, charCount, NULL, NULL, advance,
                  CHAR_NORMAL) ;
}

/**
 * \brief
 * Get character selector for next glyph in glyph run.
 *
 * \param[in] filter
 * Pointer to XML filter.
 * \param[in] p_glyph_selector
 * Pointer to Glyph selector state.
 * \param[out] p_char_selector
 * Pointer to returned character selector.
 * \param[out] p_eod
 * Pointer to returned end of glyph run flag.
 *
 * \return
 * \c TRUE if next character selected ok or end of glyph run reached, else \c
 * FALSE.
 */
static
void glyph_selector_next(
/*@in@*/ /*@notnull@*/
  xmlGFilter*     filter,
/*@in@*/ /*@notnull@*/
  GLYPH_SELECTOR* p_glyph_selector,
/*@out@*/ /*@notnull@*/
  char_selector_t* p_char_selector,
/*@out@*/ /*@notnull@*/
  int32*          p_eod)
{
  UTF16 codeunit = 0;

  HQASSERT((filter != NULL),
           "glyph_selector_next: NULL filter pointer");
  HQASSERT((p_glyph_selector != NULL),
           "glyph_selector_next: NULL glyph selector pointer");
  HQASSERT((p_char_selector != NULL),
           "glyph_selector_next: NULL character selector pointer");
  HQASSERT((p_eod != NULL),
           "glyph_selector_next: NULL end of characters pointer");

  p_char_selector->name = NULL;

  /* Check if we have finished iterating glyphs. */
  *p_eod = FALSE;
  if ( p_glyph_selector->c_char-- == 0 ) {
    *p_eod = TRUE;
    return;
  }

  for (;;) {
    switch ( p_glyph_selector->state ) {
    case GS_STATE_INDICES:
      /* Parse next Indices/UnicodeString glyph to plot */
      if ( !parse_more(&p_glyph_selector->parse) ) {
        /* No more Indices to parse, get next cid from UnicodeString */
        p_glyph_selector->state = GS_STATE_UNISTR;
        continue;
      }
      (void)parse_next_index(filter, &p_glyph_selector->parse, &p_glyph_selector->glyph);
      p_char_selector->cid = p_glyph_selector->glyph.index;

      if ( (p_glyph_selector->glyph.c_glyphs == 1) &&
           (p_glyph_selector->glyph.c_codeunits == 1) ) {
        /* 1:1 cluster map - use the UnicodeString codeunit as a codepoint if no
         * glyph index specified */
        p_glyph_selector->glyph.codepoint = -1;
        if ( more_utf16_codepoint(p_glyph_selector) ) {
          (void)next_utf16_codeunit(p_glyph_selector, &codeunit);
          p_glyph_selector->glyph.codepoint = (UTF32)codeunit;
        }
        if ( p_char_selector->cid == -1 ) {
          p_char_selector->cid = p_glyph_selector->glyph.codepoint;
        }

      } else { /* m:n cluster map - skip over UnicodeString codeunits */
        (void)skip_utf16_codeunits(p_glyph_selector, p_glyph_selector->glyph.c_codeunits);
        /* Remember length of mapping glyph sequence */
        p_glyph_selector->c_glyphs = p_glyph_selector->glyph.c_glyphs;

        p_glyph_selector->state = GS_STATE_MAPPING;
      }
      return;

    case GS_STATE_MAPPING:
      if ( --p_glyph_selector->c_glyphs > 0 ) {
        /* Get next glyph specification in a character glyph mapping sequence */
        HQASSERT((parse_more(&p_glyph_selector->parse)),
                 "glyph_selector_next: cluster map glyph sequence ended early");
        (void)parse_next_index(filter, &p_glyph_selector->parse, &p_glyph_selector->glyph);
        p_char_selector->cid = p_glyph_selector->glyph.index;
        return;
      }
      p_glyph_selector->state = GS_STATE_INDICES;
      break;

    case GS_STATE_UNISTR:
      p_glyph_selector->glyph = glyph_spec_default;
      (void)next_utf16_codeunit(p_glyph_selector, &codeunit);
      p_glyph_selector->glyph.codepoint = (UTF32)codeunit;
      p_char_selector->cid = p_glyph_selector->glyph.codepoint;
      return;

    default:
      HQFAIL("glyph_selector_next: unknown state");
      return;
    }
  }

  /*neverreached*/

} /* glyph_selector_next */

/** Extract the xps context and/or XPS state, asserting that the right
   objects have been found. */
static inline Bool xps_glyphs_state(
  /*@notnull@*/ /*@in@*/           xmlGFilter *filter,
  /*@null@*/ /*@out@*/             xmlDocumentContext **xps_ptr,
  /*@notnull@*/ /*@out@*/          xpsGlyphsState **state_ptr)
{
  xmlDocumentContext *xps_ctxt;
  xpsGlyphsState *state ;

  HQASSERT(filter != NULL, "filter is NULL");
  xps_ctxt = xmlg_get_user_data(filter) ;
  HQASSERT(xps_ctxt != NULL, "xps_ctxt is NULL");

  *state_ptr = NULL; /* silence compiler warning */

  if ( xps_ptr != NULL )
    *xps_ptr = xps_ctxt ;

  state = (xpsGlyphsState*)xps_ctxt->callback_state;
  if ( state == NULL || state->base.type != XPS_STATE_GLYPHS )
    return FALSE ;

  VERIFY_OBJECT(state, GLYPHS_STATE_NAME) ;

  *state_ptr = state ;

  return TRUE ;
}

/** Add a gsave only if necessary. */
static Bool bracket_glyphs(xpsGlyphsState *state)
{
  VERIFY_OBJECT(state, GLYPHS_STATE_NAME) ;

  if ( state->gstate_id == GS_INVALID_GID ) {
    if (! gs_gpush(GST_GSAVE))
      return FALSE;

    state->gstate_id = gstackptr->gId ;
  }

  return TRUE ;
}

static Bool apply_render_transform(xpsGlyphsState *state)
{
  OMATRIX transform, newctm ;

  /* If there has been no modification of the CTM prior to this, and the
     new matrix won't do anything, we can quit early. */
  if ( MATRIX_EQ(&state->transform, &identity_matrix) &&
       MATRIX_EQ(&state->savedctm, &thegsPageCTM(*gstateptr)) )
    return TRUE ;

  /* We need to modify the CTM, even though we will transform the path
     explicitly, because we may be stroking and the linewidth is affected
     by the transform. Also, the context for Fill and Stroke brushes
     is affected by the RenderTransform. */
  matrix_mult(&state->transform, &state->savedctm, &newctm) ;

  /* The new local transformation matrix is the same as the old local
     transformation matrix, so there is nothing to do here. Move along. */
  if ( MATRIX_EQ(&newctm, &thegsPageCTM(*gstateptr)) )
    return TRUE ;

  /* Are the RenderTransform and CTM invertible?  If not, the Path must be
     treated as completely transparent. */
  if ( !matrix_inverse(&state->transform, &transform) ||
       !matrix_inverse(&thegsPageCTM(*gstateptr), &transform) ) {
    state->transform_invertible = FALSE ;
    return TRUE ;
  }

  /* We need to generate a transform that undoes the CTM, adds the
     RenderTransform to the original CTM, and then re-does the CTM.
     Invertible matrices should not cause error, but whether a degenerate
     path or no path is displayed is implementation-dependent. */
  matrix_mult(&transform, &newctm, &transform) ;

  /* Should always delay clip path creation until after transform matrix has been
     seen; Path code needs to convert to device space to handle cases like
     omitting a segment when the two points coincide. Calling path_transform
     after seeing a transform won't give the same results as delaying
     creating the path. */
  HQASSERT(!state->clip.firstpath, "Can't transform clip path after changing transform matrix") ;

  if ((state->paint_flags & XPS_PAINT_FILL) != 0) {
    /* Modify fill pattern space matrix by transform. */
    if ( !pattern_matrix_remake(GSC_FILL, &state->transform, FALSE) )
      return FALSE ;
  }

  return gs_setctm(&newctm, FALSE) ;
}

/*=============================================================================
 * XML start/end element callbacks
 *=============================================================================
 */


/**
 * \brief Commit callback for the Glyphs element.
 *
 * \param[in] filter
 * The current XML parse handle.
 * \param[in] localname
 * Er something.
 * \param[in] prefix
 * Er something else.
 * \param[in] uri
 * I know this one, a URI - but of what I don't know.
 * \param[in] attrs
 * Glyph element attributes present.
 *
 * \return  \c TRUE if the Glyphs commit processed ok, else \c FALSE.
 */
static
Bool xps_Glyphs_Commit(
  xmlGFilter*    filter,
  const xmlGIStr* localname,
  const xmlGIStr* prefix,
  const xmlGIStr* uri,
  xmlGAttributes* attrs)
{
  corecontext_t *context = get_core_context_interp();
  DL_STATE *page = context->page ;
  xpsGlyphsState *state ;
  OMATRIX sm;
  char_selector_t char_selector = {0} ;
  Bool eod;
  GLYPH_SELECTOR glyph_data = {0};
  int32 count = 1 ;
  SYSTEMVALUE dx;
  SYSTEMVALUE dy;
  SYSTEMVALUE rtol_dx;
  SYSTEMVALUE rtol_dy;
  SYSTEMVALUE glyph_x;
  SYSTEMVALUE glyph_y;
  SYSTEMVALUE bold_x = 0;
  SYSTEMVALUE bold_y = 0;
  SYSTEMVALUE em100s;
  Bool success = FALSE ;
  HDLTinfo savedHDLT ;
  Group* group = NULL ;
  int32 gid = GS_INVALID_GID ;
  STROKE_PARAMS stroke ;
  void* cmap = 0;

  UNUSED_PARAM(const xmlGIStr*, localname);
  UNUSED_PARAM(const xmlGIStr*, prefix);
  UNUSED_PARAM(const xmlGIStr*, uri);
  UNUSED_PARAM( xmlGAttributes *, attrs ) ;

  if ( !context->systemparams->XPS )
    return error_handler(INVALIDACCESS);

  if ( !xps_glyphs_state(filter, NULL, &state) )
    return error_handler(UNREGISTERED) ;

  /* Should always delay path creation until after transform matrix has been
     seen; Path code needs to convert to device space to handle cases like
     omitting a segment when the two points coincide. Calling path_transform
     after seeing a transform won't give the same results as delaying
     creating the path.  The attribute string is guaranteed to be around
     until after the commit. */
  if ( state->abbreviated_clip.unitlength > 0 ) {
    static PATHINFO clip = PATHINFO_STATIC(NULL,NULL,NULL) ;
    static xps_path_designator clip_designator = {
      XML_INTERN(Path_Clip), &clip, EOFILL_TYPE
    } ;
    path_init(clip_designator.path);
    clip_designator.fillrule = EOFILL_TYPE ;
    if ( !xps_convert_ST_AbbrGeomF(filter, NULL, &state->abbreviated_clip, &clip_designator) )
      return xmlg_attributes_invoke_match_error(filter, localname, /* We do not track prefix */ NULL,
                                                NULL, XML_INTERN(Clip)) ;
    state->clip = *clip_designator.path;
    state->clip_rule = clip_designator.fillrule;
  }

  if ( !flush_vignette(VD_Default) ) {
    return(FALSE);
  }

  /* XPS 0.8 section 5.1.5 states that an empty UnicodeString and
     either an empty or no Indices causes an error. Note that our
     unicode string converter consumes {} as an empty string. */
  if ( (state->unicodestring.unitlength == 0) &&
       (state->indices.unitlength == 0) ) {
    return detail_error_handler(SYNTAXERROR, "Glyphs Indices and UnicodeString properties cannot both be empty.") ;
  }

  /* If we're not painting (probably because of a non-invertible matrix
     in a brush), we can skip the text. */
  if ( (state->paint_flags & XPS_PAINT_FILL) == 0 )
    return TRUE ;

  /* Metro 0.6d (10.1) states that a value of zero shows no visible text.  */
  if ( state->fontrenderingemsize == 0.0 )
    return TRUE ;

  if ( thePath(state->clip) != NULL ) {
    if ( !bracket_glyphs(state) ||
         !gs_addclip(state->clip_rule, &state->clip, FALSE) )
      return FALSE ;
  }

  /* Font size rendering matrix - -gated due to xps user space reversed in y,
   * doing: 0 EmSize neg matrix scale.
   * If simulating italic, skew by 20 degrees. */
  if ( state->sideways ) {
    sm.matrix[0][0] = 0;
    sm.matrix[1][0] = -state->fontrenderingemsize;
    sm.matrix[0][1] = -state->fontrenderingemsize;
    sm.matrix[1][1] = 0;
    sm.matrix[2][0] = 0;
    sm.matrix[2][1] = 0;
    if ( SIMULATE_ITALIC(state->style) ) {
      sm.matrix[0][0] = state->fontrenderingemsize * ITALICSIMULATION_SHEAR;
    }
    MATRIX_SET_OPT_BOTH( &sm );
  } else {
    MATRIX_COPY(&sm, &identity_matrix);
    sm.matrix[0][0] = state->fontrenderingemsize;
    sm.matrix[1][1] = -state->fontrenderingemsize;
    if ( SIMULATE_ITALIC(state->style) ) {
      sm.matrix[1][0] = state->fontrenderingemsize * ITALICSIMULATION_SHEAR;
      MATRIX_SET_OPT_BOTH( &sm );
    }
  }
  gs_setfontctm(&sm);

  /* Find number of chars in glyph run - also does validation of Indices
   * attribute value */
  if ( !glyph_selector_init(filter, &glyph_data, state) )
    return FALSE;

  if ( !finishaddchardisplay(page, glyph_data.c_char) )
    return FALSE;

  savedHDLT = gstateptr->theHDLTinfo ;
  gstateptr->theHDLTinfo.next = &savedHDLT ;
  if ( !IDLOM_BEGINTEXT(NAME_Glyphs) ) {
    gstateptr->theHDLTinfo = savedHDLT ;
    return FALSE ;
  }

  textContextEnter();

#define return DO_NOT_RETURN_goto_cleanup_INSTEAD!

  state->pattern_ok = TRUE ; /* We can accept child pattern callbacks */
  if ( !DEVICE_SETG(page, GSC_FILL, DEVICE_SETG_NORMAL) ) {
    state->pattern_ok = FALSE ;
    goto cleanup ;
  }

  state->pattern_ok = FALSE ;

  /* Map glyph origin to device space */
  MATRIX_TRANSFORM_XY(state->originx, state->originy, glyph_x, glyph_y,
                      &thegsPageCTM(*gstateptr));

  em100s = state->fontrenderingemsize * 0.01;

  /* If there's more than one glyph or we're simulating bold, and transparency applies,
     then an implicit group is required to avoid compositing between glyphs on any
     overlapping areas (this is equivalent to text KO mode in PDF). */
  if ( glyph_data.c_char > 1 || SIMULATE_BOLD(state->style) ) {
    /* Testing for a transparent pattern needs to be done after the first setg. */
    Bool transparent_pattern = ( page->currentdlstate->patternstate &&
                                 page->currentdlstate->patternstate->backdrop ) ;
    if ( !openImplicitGroup(page, &group, &gid, IMPLICIT_GROUP_TEXT_KO,
                            transparent_pattern) )
      goto cleanup ;

    if ( group ) {
      /* Opening the group invalidates the previous setg, and so we do it again. */
      state->pattern_ok = TRUE ; /* We can accept child pattern callbacks */
      if ( !DEVICE_SETG(page, GSC_FILL, DEVICE_SETG_NORMAL) ) {
        state->pattern_ok = FALSE ;
        goto cleanup ;
      }
      state->pattern_ok = FALSE ;
    }
  }

  /* If simulating bold, we increase each advance width by 2% of an em and offset the origin
   * by 1% em horizontally AND vertically, and initialise the stroke parameters. */
  if ( SIMULATE_BOLD(state->style) ) {
    if (!flush_vignette(VD_Default))
      goto cleanup ;

    dx = ( (state->bidilevel&1) == 1 )? -em100s : em100s;
    MATRIX_TRANSFORM_DXY(dx, -em100s, dx, dy, &thegsPageCTM(*gstateptr));
    glyph_x += dx;
    glyph_y += dy;
    MATRIX_TRANSFORM_DXY(2*em100s, 0.0, bold_x, bold_y, &thegsPageCTM(*gstateptr));

    set_gstate_stroke(&stroke, &thePathInfo(*gstateptr), NULL, FALSE) ;
    theDashPattern(stroke.linestyle) = onull ; /* Struct copy to set slot properties */
    theDashListLen(stroke.linestyle) = 0 ;
    theMiterLimit(stroke.linestyle) = 10.0f ;
    theLineWidth(stroke.linestyle) = (USERVALUE) (2*em100s) ;
    theFlatness(stroke.linestyle) = fcache_flatness(page) / 2 ;
    /* Use miter/bevel join as opposed to XPS miter/clip join.
       The latter produces ugly spikes on some glyphs with lines
       joining at acute angles. */
    theLineJoin(stroke.linestyle) = MITER_JOIN ;
    gs_newpath();
  }

  for (;;) {
    FVECTOR advance ;

    if ( mm_memory_is_low || dosomeaction )
      if ( !handleNormalAction() )
        goto cleanup;

    /* Get next glyph */
    glyph_selector_next(filter, &glyph_data, &char_selector, &eod);
    if ( eod ) {
#ifdef DEBUG_BUILD
      if ( (debug_xps & DEBUG_XPS_MARK_TEXT) != 0) {
        unsigned int line, column;
        /* Debug stuff */
        if (!error_signalled_context(context->error) &&
            xmlg_line_and_column(filter, &line, &column)) {
          uint8 buff[128];
          swncopyf(buff, 128, (uint8*)"[{/Courier 0.5 selectfont (<-%d) show}stopped cleartomark", line);
          (void)run_ps_string(buff);
        }
      }
#endif
      success = TRUE ;
      goto cleanup ;
    }

    /* When no index is specified, lookup index for UnicodeString codeunit/point */
    if ( glyph_data.glyph.index == -1 ) {
      HQASSERT((glyph_data.glyph.codepoint >= 0),
               "xps_Glyphs_Commit: no glyph index or codepoint");
      if ( !tt_xps_lookup(&char_selector.cid, state->font, glyph_data.glyph.codepoint, &cmap) )
        goto cleanup ;
    }

    /* If doing right to left have to move origin back for glyph */
    rtol_dx = rtol_dy = 0.0;
    if ( (state->bidilevel&1) == 1 ) {
      /** \todo TODO Best approach? Need to turn HDLT off? */
      if ( !plotchar(&char_selector, DOSTRINGWIDTH | DOXPS, count, xps_notdef, NULL,
                     &advance, CHAR_NORMAL) )
        goto cleanup ;
      rtol_dx = advance.x + bold_x;
      rtol_dy = advance.y + bold_y;
    }

    /* Offset glyph origin if needed */
    dx = glyph_data.glyph.uoffset;
    dy = -glyph_data.glyph.voffset;
    if ( (dx != 0.) || (dy != 0.) ) {
      dx *= em100s;
      if ( (state->bidilevel&1) == 1 ) {
        dx = -dx;
      }
      dy *= em100s;
      MATRIX_TRANSFORM_DXY(dx, dy, dx, dy, &thegsPageCTM(*gstateptr));
    }

    /* Set origin for glyph */
    if ( !path_moveto((glyph_x - rtol_dx + dx), (glyph_y - rtol_dy + dy), MOVETO, &thePathInfo(*gstateptr)) )
      goto cleanup ;

    /* Plot the glyph */
    if ( !plotchar(&char_selector, DOSHOW | DOXPS, count++, xps_notdef, NULL,
                   &advance, CHAR_NORMAL) )
      goto cleanup ;

    /* Simulate bold by stroking the character outline */
    if ( SIMULATE_BOLD(state->style) ) {
      state->pattern_ok = TRUE ;
      if ( !init_charpath(TRUE) ||
           !plotchar(&char_selector, DOCHARPATH | DOXPS, count-1, xps_notdef,
                     NULL, &advance, CHAR_NORMAL) ||
           !dostroke(&stroke, GSC_FILL, STROKE_NOT_VIGNETTE) ||
           !end_charpath(TRUE) ||
           !gs_newpath() )
        goto cleanup ;
      state->pattern_ok = FALSE ;
      advance.x += bold_x;
      advance.y += bold_y;
    }

    /* Update glyph origin using either specified advance or natural glyph advance */
    if ( glyph_data.glyph.advance_set ) {
      dx = glyph_data.glyph.advance*em100s;
      if ( (state->bidilevel&1) == 1 ) {
        dx = -dx;
      }
      MATRIX_TRANSFORM_DXY(dx, 0., dx, dy, &thegsPageCTM(*gstateptr));

      glyph_x += dx;
      glyph_y += dy;

    } else if ( (state->bidilevel&1) == 0 ) {
      /* LToR - normal advance */
      glyph_x += advance.x;
      glyph_y += advance.y;

    } else {
      /* RToL - reverse normal advance */
      glyph_x -= advance.x;
      glyph_y -= advance.y;
    }
  }

  /* success = TRUE is done inside the for loop on EOD */
 cleanup:

  if ( !closeImplicitGroup(&group, gid, success) )
    success = FALSE ;

  textContextExit();

  if ( !IDLOM_ENDTEXT(NAME_Glyphs, success) )
    success = FALSE ;

  gstateptr->theHDLTinfo = savedHDLT ;

  if ( success && !finishaddchardisplay(page, 1) )
    success = FALSE;

#undef return
  return success;
} /* xps_Glyphs_Commit */


/**
 * \brief Callback for the Glyphs start element.
 *
 * \param[in] filter
 * The current XML parse handle.
 * \param[in] localname
 * Er something.
 * \param[in] prefix
 * Er something else.
 * \param[in] uri
 * I know this one, a URI - but of what I don't know.
 * \param[in] attrs
 * Glyph element attributes present.
 *
 * \return  \c TRUE if the Glyphs element processed ok, else \c FALSE.
 */
static Bool xps_Glyphs_Start(
/*@in@*/ /*@notnull@*/
  xmlGFilter*  filter,
/*@in@*/ /*@notnull@*/
  const xmlGIStr* localname,
/*@in@*/ /*@notnull@*/
  const xmlGIStr* prefix,
/*@in@*/ /*@notnull@*/
  const xmlGIStr* uri,
/*@in@*/ /*@tnull@*/
  xmlGAttributes* attrs)
{
  OBJECT* fontdict;
  xmlDocumentContext *xps;
  xpsGlyphsState *state ;
  Bool success = FALSE ;

  static Bool opacity_set, bidilevel_set, style_set, dummy, name_set, lang_set ;
  static Bool indices_set, unicodestring_set, sideways_set, sideways ;
  static USERVALUE opacity;
  static int32 bidilevel, style;
  static double fontrenderingemsize, originx, originy;
  static xmlGIStr *lang;
  static xps_fonturi_designator fonturi_designator ;
  static utf8_buffer unicodestring, devicefontname, name;
  static utf8_buffer indices;
  static xps_abbrevgeom_designator abbreviated_clip = { XML_INTERN(Glyphs_Clip) } ;
  static OMATRIX matrix;
  static xps_matrix_designator matrix_designator = { XML_INTERN(Glyphs_RenderTransform), &matrix };
  static xps_color_designator fill_designator = { XML_INTERN(Glyphs_Fill), FALSE };

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(Glyphs_RenderTransform), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Glyphs_Clip), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Glyphs_OpacityMask), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Glyphs_Fill), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    { XML_INTERN(Visual), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  static XML_ATTRIBUTE_MATCH match[] = {
    /* Required attributes */
    { XML_INTERN(FontRenderingEmSize), NULL, NULL, xps_convert_dbl_ST_GEZero, &fontrenderingemsize },
    { XML_INTERN(FontUri), NULL, NULL, xps_convert_FontUri, &fonturi_designator },
    { XML_INTERN(OriginX), NULL, NULL, xps_convert_dbl_ST_Double, &originx },
    { XML_INTERN(OriginY), NULL, NULL, xps_convert_dbl_ST_Double, &originy },
    /* Optional attributes */
    { XML_INTERN(Opacity), NULL, &opacity_set, xps_convert_fl_ST_ZeroOne, &opacity },
    { XML_INTERN(BidiLevel), NULL, &bidilevel_set, xps_convert_bidilevel, &bidilevel },
    { XML_INTERN(StyleSimulations), NULL, &style_set, xps_convert_ST_StyleSimulations, &style },
    { XML_INTERN(Indices), NULL, &indices_set, xps_convert_utf8, &indices },
    { XML_INTERN(IsSideways), NULL, &sideways_set, xps_convert_ST_Boolean, &sideways },
    { XML_INTERN(UnicodeString), NULL, &unicodestring_set, xps_convert_ST_UnicodeString, &unicodestring },
    { XML_INTERN(CaretStops), NULL, &dummy, xps_convert_ST_CaretStops, NULL },
    { XML_INTERN(DeviceFontName), NULL, &dummy, xps_convert_ST_UnicodeString, &devicefontname },
    { XML_INTERN(Name), NULL, &name_set, xps_convert_ST_Name, &name },
    { XML_INTERN(FixedPage_NavigateUri), NULL, &dummy, xps_convert_navigate_uri, NULL },
    { XML_INTERN(lang), XML_INTERN(ns_w3_xml_namespace), &lang_set, xml_convert_lang, &lang },
    /* References; ordered for least work */
    { XML_INTERN(Clip), NULL, &dummy, xps_convert_ST_RscRefAbbrGeom, &abbreviated_clip },
    { XML_INTERN(OpacityMask), NULL, &dummy, xps_convert_ST_RscRef, XML_INTERN(Glyphs_OpacityMask) },
    { XML_INTERN(Fill), NULL, &dummy, xps_convert_ST_RscRefColor, &fill_designator },
    { XML_INTERN(RenderTransform), NULL, &dummy, xps_convert_ST_RscRefMatrix, &matrix_designator },
    XML_ATTRIBUTE_MATCH_END
  };

  static XPS_COMPLEXPROPERTYMATCH complex_properties[] = {
    { XML_INTERN(Glyphs_RenderTransform), XML_INTERN(ns_xps_2005_06), XML_INTERN(RenderTransform), NULL, TRUE },
    { XML_INTERN(Glyphs_Clip), XML_INTERN(ns_xps_2005_06), XML_INTERN(Clip), NULL, TRUE },
    { XML_INTERN(Glyphs_OpacityMask), XML_INTERN(ns_xps_2005_06), XML_INTERN(OpacityMask), NULL, TRUE },
    /* Visual MUST appear immediately after Path_OpacityMask */
    { XML_INTERN(Visual), XML_INTERN(ns_xps_2005_06), NULL, NULL, TRUE },
    { XML_INTERN(Glyphs_Fill), XML_INTERN(ns_xps_2005_06), XML_INTERN(Fill), NULL, TRUE },
    XPS_COMPLEXPROPERTYMATCH_END
  };

  UNUSED_PARAM(const xmlGIStr*, prefix);

  HQASSERT(filter != NULL, "filter is NULL");
  xps = xmlg_get_user_data(filter) ;
  HQASSERT(xps != NULL, "xps_ctxt is NULL");

  MATRIX_COPY(matrix_designator.matrix, &identity_matrix);
  abbreviated_clip.attributebuffer.codeunits = NULL ;
  abbreviated_clip.attributebuffer.unitlength = 0 ;
  fill_designator.color_set = FALSE;
  fill_designator.color_profile_partname = NULL;

  if (! xps_commit_register(filter, localname, uri, attrs, complex_properties,
                            xps_Glyphs_Commit))
    return FALSE ;

  /* Must clean up minipath properly if used. */
#define return DO_NOT_RETURN_GO_TO_early_cleanup_INSTEAD!

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE)) {
    (void) error_handler(UNDEFINED) ;
    goto early_cleanup ;
  }

  HQASSERT(fonturi_designator.font != NULL, "Font URI reference is NULL") ;

  if (! xmlg_valid_children(filter, localname, uri, valid_children)) {
    (void) error_handler(UNDEFINED) ;
    goto early_cleanup ;
  }

  if ( !sideways_set )
    sideways = FALSE;

  /* The XPS Specification (section 5.1.6.2) does not allow Glyphs
     elements to be specified with both IsSideways true and BidiLevel
     right-to-left (i.e. Not odd). */
  if (sideways) {
    if (bidilevel_set && ((bidilevel & 1) == 1)) {
       (void)detail_error_handler(UNDEFINED,
                                           "Glyphs element does not support sideways right-to-left text.") ;

      goto early_cleanup ;
    }
  }

  if ( !xps_font_define(fonturi_designator.font, fonturi_designator.fontface_index,
                        filter, &fontdict, sideways) ) {
    (void) error_handler(UNDEFINED) ;
    goto early_cleanup ;
  }

  xps_partname_free(&fonturi_designator.font) ;
  HQASSERT(fonturi_designator.font == NULL, "Font URI reference exists after freed") ;

  /* Set the font */
  if ( !gs_setfont(fontdict) )
    goto early_cleanup ;

  /* Make a new path state. */
  state = mm_alloc(mm_xml_pool, sizeof(xpsGlyphsState),
                   MM_ALLOC_CLASS_XPS_CALLBACK_STATE) ;
  if (state == NULL) {
    (void) error_handler(VMERROR) ;
    goto early_cleanup ;
  }

#undef return
#define return DO_NOT_RETURN_GO_TO_state_cleanup_INSTEAD!

  state->base.type = XPS_STATE_GLYPHS;
  state->base.next = xps->callback_state;
  state->gstate_id = GS_INVALID_GID;
  state->transform_invertible = TRUE ;
  MATRIX_COPY(&state->transform, matrix_designator.matrix) ;
  MATRIX_COPY(&state->savedctm, &thegsPageCTM(*gstateptr)) ;
  state->abbreviated_clip = abbreviated_clip.attributebuffer ;
  path_init(&state->clip);
  state->clip_rule = EOFILL_TYPE ;
  state->paint_flags = XPS_PAINT_NONE;
  state->gstate_mask_id = GS_INVALID_GID;
  state->mask_group = NULL;
  state->saved_opacity = tsConstantAlpha(gsTranState(gstateptr), FALSE) ;
  HQASSERT(state->saved_opacity == tsConstantAlpha(gsTranState(gstateptr), TRUE),
           "Stroking and non-stroking opacity should be the same in XPS") ;
  state->pattern_ok = FALSE;
  NAME_OBJECT(state, GLYPHS_STATE_NAME) ;

  if ( opacity_set ) {
    tsSetConstantAlpha(gsTranState(gstateptr), FALSE, opacity, gstateptr->colorInfo);
    tsSetConstantAlpha(gsTranState(gstateptr), TRUE, opacity, gstateptr->colorInfo);
  }

  /* Empty string is treated as not being present. */
  if ( unicodestring_set && unicodestring.unitlength > 0) {
    state->unicodestring = unicodestring;
  } else {
    utf8_iterator_init(&state->unicodestring, NULL, 0);
  }
  if ( indices_set ) {
    state->indices = indices;
  } else {
    utf8_iterator_init(&state->indices, NULL, 0);
  }
  state->font = fontdict;
  state->originx = originx;
  state->originy = originy;
  state->fontrenderingemsize = fontrenderingemsize;
  state->sideways = sideways;
  if ( bidilevel_set ) {
    state->bidilevel = bidilevel;
  } else {
    state->bidilevel = 0;
  }
  if ( style_set ) {
    state->style = style;
  } else {
    state->style = STYLE_NONE;
  }

  /* Render transform may have been supplied as a matrix(...) attribute. */
  if (! apply_render_transform(state))
    goto state_cleanup ;

  /* Fill attribute may be present, specifying color directly. */
  if ( fill_designator.color_set && state->transform_invertible ) {
    state->paint_flags |= XPS_PAINT_FILL;
    if ( !xps_setcolor(xps, GSC_FILL, &fill_designator) )
      goto state_cleanup ;
  }

  /* Good completion; link the new glyph state into the context */
  xps->callback_state = (xpsCallbackState*)state ;

  success = TRUE;

 state_cleanup:
  if (! success) {
    VERIFY_OBJECT(state, GLYPHS_STATE_NAME) ;
    if (state->gstate_id != GS_INVALID_GID)
      (void)gs_cleargstates(state->gstate_id, GST_GSAVE, NULL) ;
    (void)gs_setctm(&state->savedctm, FALSE) ;
    UNNAME_OBJECT(state);
    mm_free(mm_xml_pool, state, sizeof(xpsGlyphsState));
  }

 early_cleanup:
  if ( fonturi_designator.font != NULL )
    xps_partname_free(&fonturi_designator.font) ;
  if (fill_designator.color_profile_partname != NULL)
    xps_partname_free(&fill_designator.color_profile_partname) ;

#undef return
  return success ;
} /* xps_Glyphs_Start */


static Bool xps_Glyphs_End(
  xmlGFilter*    filter,
  const xmlGIStr* localname,
  const xmlGIStr* prefix,
  const xmlGIStr* uri,
  Bool            success)
{
  xmlDocumentContext *xps;
  xpsGlyphsState *state ;

  UNUSED_PARAM(const xmlGIStr*, localname);
  UNUSED_PARAM(const xmlGIStr*, prefix);
  UNUSED_PARAM(const xmlGIStr*, uri);

  /* The rest of the cleanups rely on the state being present. */
  if ( !xps_glyphs_state(filter, &xps, &state) )
    return success && error_handler(UNREGISTERED) ;

  if ( state->gstate_id != GS_INVALID_GID )
    if ( !gs_cleargstates(state->gstate_id, GST_GSAVE, NULL) )
      success = FALSE ;

  /* For efficiency, stroke and fill opacity values are restored manually.
     Opacity may be set before a bracket_glyphs, so this must be done after the
     gs_cleargstates.  (May as well just set the values as it is as cheap as
     getting the values from the gstate and then testing on if they have
     changed.) */
  tsSetConstantAlpha(gsTranState(gstateptr), TRUE, state->saved_opacity, gstateptr->colorInfo);
  tsSetConstantAlpha(gsTranState(gstateptr), FALSE, state->saved_opacity, gstateptr->colorInfo);

  /* Changing just the transformation matrix doesn't require a gsave, restore
     it here in case it was changed. */
  if ( !gs_setctm(&state->savedctm, FALSE) )
    success = FALSE ;

  path_free_list(thePath(state->clip), mm_pool_temp) ;
  xps->callback_state = state->base.next ;
  UNNAME_OBJECT(state);
  mm_free(mm_xml_pool, state, sizeof(xpsGlyphsState)) ;

  return success ;
} /* xps_Glyphs_End */


static Bool xps_Glyphs_Clip_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps_ctxt ;
  xpsGlyphsState *state ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(PathGeometry), XML_INTERN(ns_xps_2005_06), XMLG_G_SEQUENCED, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. Also, no compatibility attributes are allowed on
     properties. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  if (! xps_glyphs_state(filter, &xps_ctxt, &state) )
    return error_handler(UNREGISTERED) ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  HQASSERT(xps_ctxt->ignore_isstroked, "ignore_isstroked should be true");
  HQASSERT(!xps_ctxt->use_pathfill, "use_pathfill should be false");
  HQASSERT(thePath(xps_ctxt->path) == NULL, "path should be empty");
  HQASSERT(thePath(xps_ctxt->pathfill) == NULL, "pathfill should be empty");

  return TRUE; /* keep on parsing */
}

static Bool xps_Glyphs_Clip_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsGlyphsState *state ;

  UNUSED_PARAM( xmlGFilter* , filter ) ;
  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* The rest of the cleanups rely on the state being present. */
  if ( !xps_glyphs_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  /* Steal constructed path from xps context and put into glyphs state. This will
     later be transformed by the RenderTransform and applied as a clip. */
  path_free_list(thePath(state->clip), mm_pool_temp) ;
  if ( xps_ctxt->use_pathfill ) {
    state->clip = xps_ctxt->pathfill ;
    path_init(&xps_ctxt->pathfill) ;
    xps_ctxt->use_pathfill = FALSE ;
    path_free_list(thePath(xps_ctxt->path), mm_pool_temp) ;
    path_init(&xps_ctxt->path) ;
  } else {
    state->clip = xps_ctxt->path ;
    path_init(&xps_ctxt->path) ;
  }

  HQASSERT(!xps_ctxt->use_pathfill, "use_pathfill should still be false");
  HQASSERT(thePath(xps_ctxt->path) == NULL, "path should be empty");
  HQASSERT(thePath(xps_ctxt->pathfill) == NULL, "pathfill should be empty");

  state->clip_rule = xps_ctxt->fill_rule ;

  return success;
}

static Bool xps_Glyphs_RenderTransform_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  xmlDocumentContext *xps;
  xpsGlyphsState *state ;

  /* XMLG_ONE, XMLG_NO_GROUP for release build Matrix transform */
  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(MatrixTransform), XML_INTERN(ns_xps_2005_06), XMLG_ONE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. Also, no compatibility attributes are allowed on
     properties. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  if (! xps_glyphs_state(filter, &xps, &state))
    return error_handler(UNREGISTERED) ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  xps->transform = &state->transform ;

  return TRUE; /* keep on parsing */
}

static Bool xps_Glyphs_RenderTransform_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps;
  xpsGlyphsState *state ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  /* MatrixTransform should already have captured the matrix */
  if ( !xps_glyphs_state(filter, &xps, &state) )
    return success && error_handler(UNREGISTERED) ;

  if ( xps->transform != NULL )
    success = (success && detail_error_handler(SYNTAXERROR,
      "Required MatrixTransform element is missing.")) ;

  xps->transform = NULL ;

  if ( success )
    success = apply_render_transform(state) ;

  return success;
}

static Bool xps_Glyphs_OpacityMask_Start(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      xmlGAttributes *attrs)
{
  DL_STATE *page = get_core_context_interp()->page ;
  xmlDocumentContext *xps_ctxt;
  xpsGlyphsState *state ;

  OBJECT colorSpace = OBJECT_NOTVM_NOTHING;
  COLORSPACE_ID dummyspace_id;
  int32 name_id;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(SolidColorBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(ImageBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(VisualBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(LinearGradientBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(RadialGradientBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(Visual), XML_INTERN(ns_xps_2005_06), XMLG_ZERO_OR_MORE, XMLG_NO_GROUP},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. Also, no compatibility attributes are allowed on
     properties. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM( const xmlGIStr* , prefix ) ;

  HQASSERT(filter != NULL, "filter is NULL");

  if (! xps_glyphs_state(filter, &xps_ctxt, &state))
    return error_handler(UNREGISTERED) ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  /* Will need to undo the setting of the mask at the end of Glyphs. */
  if (! bracket_glyphs(state) )
    return FALSE;

  /* The opacity mask colorspace is the same as the VirtualDeviceSpace */
  dlVirtualDeviceSpace(page, &name_id, &dummyspace_id);

  object_store_name(&colorSpace, name_id, LITERAL) ;


  /* An additional gsave is required around opacity mask creation in case
     Glyphs.Fill and Glyphs.Data have already been done. */
  if (gs_gpush(GST_GROUP)) {
    int32 gid = gstackptr->gId ;

    if ( groupOpen(page, colorSpace, TRUE /* isolated */, FALSE /* knockout */,
                   TRUE /* banded */, NULL /* bgcolor */, NULL /* xferfn */,
                   NULL /* patternTA */, GroupAlphaSoftMask, &state->mask_group) ) {
      if (gs_gpush(GST_GSAVE)) {
        xps_ctxt->colortype = GSC_FILL;
        state->gstate_mask_id = gid ;
        return TRUE ;
      }
      (void)groupClose(&state->mask_group, FALSE) ;
    }
    (void)gs_cleargstates(gid, GST_GROUP, NULL);
  }

  return FALSE ;
}

static Bool xps_Glyphs_OpacityMask_End(
      xmlGFilter *filter,
      const xmlGIStr *localname,
      const xmlGIStr *prefix,
      const xmlGIStr *uri,
      Bool success)
{
  xmlDocumentContext *xps_ctxt;
  xpsGlyphsState *state ;

  UNUSED_PARAM( const xmlGIStr* , localname ) ;
  UNUSED_PARAM( const xmlGIStr* , prefix ) ;
  UNUSED_PARAM( const xmlGIStr* , uri ) ;

  if ( !xps_glyphs_state(filter, &xps_ctxt, &state) )
    return success && error_handler(UNREGISTERED) ;

  state->pattern_ok = TRUE ; /* Can now accept child pattern callbacks */

  /* Clear this before making the soft mask in case the opacity mask has
     paths, glyphs or canvases with an opacity attribute. */
  xps_ctxt->colortype = GSC_UNDEFINED;

  if (success) {
    /* The brush has already been setup; now paint the current clipping
       rectangle with the brush to make an object to put into the soft mask
       group.  Note that the whole rectangle does not require compositing,
       only those regions covered by the objects that use the soft mask. */
    sbbox_t brushbox;
    OMATRIX inverse_ctm;

    brushbox = thegsPageClip(*gstateptr).rbounds ;

    /* If can't invert the ctm, treat as transparent by having an empty group. */
    if ( matrix_inverse(&thegsPageCTM(*gstateptr), &inverse_ctm) ) {
      RECTANGLE rect;

      bbox_transform(&brushbox, &brushbox, &inverse_ctm) ;

      bbox_to_rectangle(&brushbox, &rect) ;

      if (! dorectfill(1, &rect, GSC_FILL, RECT_NORMAL))
        success = FALSE;
    }
  }

  if (! groupClose(&state->mask_group, success))
    success = FALSE;

  state->pattern_ok = FALSE ;

  HQASSERT(state->gstate_mask_id != GS_INVALID_GID,
           "Must have a gstate id for the opacity mask");
  if ( !gs_cleargstates(state->gstate_mask_id, GST_GROUP, NULL) )
    success = FALSE ;
  state->gstate_mask_id = GS_INVALID_GID;

  if (success) {
    success = tsSetSoftMask(gsTranState(gstateptr),
                            AlphaSoftMask,
                            groupId(state->mask_group),
                            gstateptr->colorInfo) ;
  }

  return success;
}

static Bool xps_Glyphs_Fill_Start(
/*@in@*/ /*@notnull@*/
  xmlGFilter*    filter,
/*@in@*/ /*@notnull@*/
  const xmlGIStr* localname,
/*@in@*/ /*@notnull@*/
  const xmlGIStr* prefix,
/*@in@*/ /*@notnull@*/
  const xmlGIStr* uri,
/*@in@*/ /*@null@*/
  xmlGAttributes* attrs)
{
  xmlDocumentContext *xps;
  xpsGlyphsState *state ;

  static XMLG_VALID_CHILDREN valid_children[] = {
    { XML_INTERN(SolidColorBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(ImageBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(VisualBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(LinearGradientBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    { XML_INTERN(RadialGradientBrush), XML_INTERN(ns_xps_2005_06), XMLG_GROUP_ONE_OF, 1},
    XMLG_VALID_CHILDREN_END
  } ;

  /* No attributes allowed. Also, no compatibility attributes are allowed on
     properties. */
  static XML_ATTRIBUTE_MATCH match[] = {
    XML_ATTRIBUTE_MATCH_END
  } ;

  UNUSED_PARAM(const xmlGIStr*, prefix);

  if (! xps_glyphs_state(filter, &xps, &state))
    return error_handler(UNREGISTERED) ;

  if (! xmlg_attributes_match(filter, localname, uri, attrs, match, TRUE) )
    return error_handler(UNDEFINED) ;
  if (! xmlg_valid_children(filter, localname, uri, valid_children))
    return error_handler(UNDEFINED);

  /* Set up the current color type, so that opacity et. al. apply to the right place. */
  xps->colortype = GSC_FILL;

  /* Keep on parsing */
  return TRUE;
} /* xps_Glyphs_Fill_Start */

static Bool xps_Glyphs_Fill_End(
/*@in@*/ /*@notnull@*/
  xmlGFilter*    filter,
/*@in@*/ /*@notnull@*/
  const xmlGIStr* localname,
/*@in@*/ /*@notnull@*/
  const xmlGIStr* prefix,
/*@in@*/ /*@notnull@*/
  const xmlGIStr* uri,
  Bool            success)
{
  xmlDocumentContext *xps;
  xpsGlyphsState *state ;

  UNUSED_PARAM(const xmlGIStr*, localname);
  UNUSED_PARAM(const xmlGIStr*, prefix);
  UNUSED_PARAM(const xmlGIStr*, uri);

  if ( !xps_glyphs_state(filter, &xps, &state) )
    return success && error_handler(UNREGISTERED) ;

  HQASSERT(xps->colortype == GSC_UNDEFINED ||
           xps->colortype == GSC_FILL,
           "Color type is not correct at Glyphs.Fill end") ;

  if ( xps->colortype == GSC_FILL && state->transform_invertible ) {
    /* We're going to fill the subsequent path. */
    state->paint_flags |= XPS_PAINT_FILL;
  }

  /* Reset the current color type */
  xps->colortype = GSC_UNDEFINED;

  return(success);

} /* xps_Glyphs_Fill_End */

/*=============================================================================
 * Register functions
 *=============================================================================
 */

xpsElementFuncts glyph_functions[] =
{
  { XML_INTERN(Glyphs),
    xps_Glyphs_Start,
    xps_Glyphs_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(Glyphs_Clip),
    xps_Glyphs_Clip_Start,
    xps_Glyphs_Clip_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(Glyphs_RenderTransform),
    xps_Glyphs_RenderTransform_Start,
    xps_Glyphs_RenderTransform_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(Glyphs_OpacityMask),
    xps_Glyphs_OpacityMask_Start,
    xps_Glyphs_OpacityMask_End,
    NULL /* No characters callback. */
  },
  { XML_INTERN(Glyphs_Fill),
    xps_Glyphs_Fill_Start,
    xps_Glyphs_Fill_End,
    NULL /* No characters callback. */
  },
  XPS_ELEMENTFUNCTS_END
};

/* ============================================================================
* Log stripped */
