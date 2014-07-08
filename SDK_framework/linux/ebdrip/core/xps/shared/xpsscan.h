/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!shared:xpsscan.h(EBDSDK_P.1) $
 * $Id: shared:xpsscan.h,v 1.54.10.1.1.1 2013/12/19 11:24:47 anon Exp $
 *
 * Copyright (C) 2004-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Extra type converters for XPS attribute strings, parsing them into
 * suitable types.
 */

#ifndef __XPSSCAN_H__
#define __XPSSCAN_H__

#include "xmlgtype.h" /* xmlGFilter */
#include "graphict.h" /* PATHINFO */
#include "matrix.h"   /* OMATRIX */
#include "xpsparts.h" /* xps_partname_t */

/**
 * \brief XPS StaticResource matching
 *
 * \param input Pointer to a UTF-8 string.
 *
 * Consumes the string "{StaticResource".
 */
Bool xps_match_static_resource(/*@in@*/ /*@notnull@*/ utf8_buffer* input) ;


/**
 * \brief Point type converter.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store a point
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_Point(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                          /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                          /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                          /*@notnull@*/ void *data /* SYSTEMVALUE[2] */);

/**
 * \brief Positive real number point type converter.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store a point
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_PointGE0(xmlGFilter *filter,
                             xmlGIStr *attrlocalname,
                             utf8_buffer* value,
                             void *data /* SYSTEMVALUE[2] */) ;

/**
 * \brief Matrix type converter.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store an OMATRIX
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_Matrix(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                           /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                           /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                           /*@notnull@*/ void *data /* OMATRIX* */) ;


typedef struct xps_matrix_designator_s {
  xmlGIStr* elementname;
  OMATRIX* matrix;
} xps_matrix_designator;

/**
 * \brief Syntax checking type converter for a matrix "matrix(...)" or
 * a resource reference to a matrix.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Pointer to a xps_matrix_designator.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_RscRefMatrix(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                                 /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                                 /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                                 /*@notnull@*/ void *data /* xps_matrix_designator* */) ;

/**
 * \brief Color type converter.
 *
 * For RGB, Alpha component is optional; if omitted, color[0] is set to
 * 1.0.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store an ARGB triple
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_Color(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                          /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                          /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                          /*@notnull@*/ void *data /* xps_color_designator* */) ;

typedef struct xps_imagesource_designator {
  xps_partname_t *image ;
  xps_partname_t *profile ;
} xps_imagesource_designator ;

/**
 * \brief ImageSource type converter.
 *
 * If the ImageSource is a straight part name URI, profile will be set
 * to NULL. In the event of an error, image and source will be set to
 * NULL.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store an ARGB triple
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_UriCtxBmp(xmlGFilter *filter,
                              xmlGIStr *attrlocalname,
                              utf8_buffer* value,
                              void *data /* xps_imagesource_designator* */) ;


enum { not_set, sRGB, scRGB, iccbased, dummy_max_colorspace } ;

typedef struct xps_color_designator {
  xmlGIStr* elementname ;
  Bool color_set ;
  USERVALUE alpha ;
  int32 n_colorants ;  /* excluding alpha component */
  USERVALUE color[8] ;
  int32 colorspace ;
  xps_partname_t *color_profile_partname ;
} xps_color_designator ;

/**
 * \brief Color type converter.
 *
 * For RGB, Alpha component is optional; if omitted, alpha is set to
 * 1.0.  Allows a color to be specified directly or via a reference.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to a xps_color_designator
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_RscRefColor(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                                /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                                /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                                /*@notnull@*/ void *data /* xps_color_designator* */) ;

typedef struct xps_fonturi_designator {
  /* font is the XPS part which represents the font. */
  xps_partname_t *font ;
  /* fontface_index is set to -1 if no fontface_index has been
     specified. If set, the range is between 0 and n-1, where n is the
     number of font faces contained in the TrueType Collection. */
  int32 fontface_index ;
} xps_fonturi_designator ;

/**
 * \brief FontUri type converter.
 *
 * If the FontUri is a straight part name URI, fontface_index will be
 * set to -1. If the FontUri contains a font face index as the
 * fragment, it will be set appropriately. In the event of an error,
 * font will be set to NULL and fontface_index will be set to -1.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store font uri designator.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_FontUri(xmlGFilter *filter,
                         xmlGIStr *attrlocalname,
                         utf8_buffer* value,
                         void *data /* xps_fonturi_designator* */) ;

/**
 * \brief Interned value type converter. This is only used for Relationships
 * Type attributes, and is deprecated. A specific converter will be added for
 * those attributes.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store an interned string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_Type(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                      /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                      /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                      /*@notnull@*/ void *data /* xmlGIStr** */) ;

/**
 * \brief UTF8 string type converter.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store a non-interned string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_utf8(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                      /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                      /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                      /*@notnull@*/ void *data /* utf8_buffer* */) ;

/**
 * \brief Syntax checking type converter for a resource name.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Pointer to utf8_buffer to store the resource name.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_Key(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                     /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                     /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                     /*@notnull@*/ void *data /* utf8_buffer* */) ;

/**
 * \brief Syntax checking type converter for resource references.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Element name.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_RscRef(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                           /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                           /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                           /*@notnull@*/ void *data /* xmlGIStr* */) ;

/**
 * \brief Syntax checking type converter for path mini-language or a resource
 * reference to a verbose path.  Does not scan the buffer, but simply returns
 * it back for scanning later, but it does resolve any resource reference.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data xps_abbrevgeom_designator*
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
typedef struct xps_abbrevgeom_designator_s {
  xmlGIStr* elementname;
  utf8_buffer attributebuffer;
} xps_abbrevgeom_designator;

Bool xps_convert_ST_RscRefAbbrGeom(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                                   /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                                   /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                                   /*@notnull@*/ void *data /* xps_abbrevgeom_designator* */) ;

/**
 * \brief Syntax checking type converter for path mini-language;
 * excludes the option of fillrule and resource references.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data The PATHINFO to be filled-in.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
typedef struct xps_path_designator_s {
  xmlGIStr* elementname;
  PATHINFO* path;
  int32 fillrule ;
} xps_path_designator;

Bool xps_convert_ST_AbbrGeom(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                             /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                             /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                             /*@notnull@*/ void *data /* PATHINFO* */) ;

/**
 * \brief Syntax checking type converter for path mini-language;
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data xps_path_designator*
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_AbbrGeomF(xmlGFilter *filter,
                              xmlGIStr *attrlocalname,
                              utf8_buffer* value,
                              void *data /* xps_path_designator* */) ;

/**
 * \brief Type converter for contentbox.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location of sbbox_t to be filled in.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_ContentBox(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                               /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                               /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                               /*@notnull@*/ void *data /* sbbox_t* */) ;

/**
 * \brief Type converter for bleedbox.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location of sbbox_t to be filled in.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_BleedBox(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                             /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                             /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                             /*@notnull@*/ void *data /* sbbox_t* */) ;

/**
 * \brief Type converter for Viewbox.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location of sbbox_t to be filled in.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_ViewBox(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                            /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                            /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                            /*@notnull@*/ void *data /* RECTANGLE* */) ;

/**
 * \brief Type converter for content type stream part names. The part
 * name will be made absolute.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store part name.
 *
 * \note On success, will return a newly allocated part name. The part name 
 * will be fully normalised as per the XPS specification.
 *
 * \retval TRUE Success. The location of the xps_partname_t will be updated to
 *              point to a newly allocated part name. This partname will need
 *              to be freed by calling xps_partname_free()
 * \retval FALSE Failure. The location of the xps_partname_t will be set to
 *               NULL.
 */
Bool xps_convert_partname(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                          /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                          /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                          /*@notnull@*/ void *data /* xps_partname_t** */) ;

/**
 * \brief Type converter for part reference. The part name will be made absolute.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store part name.
 *
 * \note On success, will return a newly allocated part name. The part name 
 * will be fully normalised as per the XPS specification.
 *
 * \retval TRUE Success. The location of the xps_partname_t will be updated to
 *              point to a newly allocated part name. This partname will need
 *              to be freed by calling xps_partname_free()
 * \retval FALSE Failure. The location of the xps_partname_t will be set to
 *               NULL.
 */
Bool xps_convert_part_reference(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                                /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                                /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                                /*@notnull@*/ void *data /* xps_partname_t** */) ;

/**
 * \brief Type converter for part extensions.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to extension.
 *
 * \note On success, will return a newly allocated extension. The extension
 * will be fully normalised as per the XPS specification.
 *
 * \retval TRUE Success. The location of the xps_extension_t will be updated to
 *              point to a newly allocated extension. This extension will need
 *              to be freed by calling xps_extension_free()
 * \retval FALSE Failure. The location of the xps_extension_t will be set to
 *               NULL.
 */
Bool xps_convert_extension(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                           /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                           /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                           /*@notnull@*/ void *data /* xps_extension_t** */) ;

/**
 * \brief Type converter for mime types. The mime type will be lowercased.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store part name.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_mimetype(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                          /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                          /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                          void *data /* xmlGIStr** */) ;

/**
 * \brief Type converter for stroke dash array.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location of sbbox_t to be filled in.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_StrokeDashArray(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                                 /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                                 /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                                 /*@notnull@*/ void *data /* LINESTYLE* */) ;

/**
 * \brief Type converter for a prefix. A prefix is defined as a
 * sequence of "non whitespace" characters until we get a versioning
 * XSD from MS.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the work.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_prefix_no_colon(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                                 /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                                 /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                                 /*@notnull@*/ void *data /* utf8_buffer* */) ;

/**
 * \brief Type converter for a URI prefix. Returns the prefix without the
 * trailing :, but the colon is consumed and MUST exist.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the prefix.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_prefix(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                        /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                        /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                        /*@notnull@*/ void *data /* utf8_buffer* */) ;

/**
 * \brief Type converter for an XML qualified name local part.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the prefix.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_local_name(xmlGFilter *filter,
                            xmlGIStr *attrlocalname,
                            utf8_buffer* value,
                            void *data /* utf8_buffer* */) ;


/**
 * \brief Type converter XPS bidilevel type.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the number.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_bidilevel(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                           /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                           /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                           void *data /* int32* */) ;

/**
 * \brief Type converter XPS pint type.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the number.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_pint(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                      /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                      /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                      void *data /* int32* */) ;

/**
 * \brief Type converter XPS uint type.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the number.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_uint(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                      /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                      /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                      void *data /* int32* */) ;

/**
 * \brief Type converter XPS dec type.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the number.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_dec(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                     /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                     /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                     void *data /* double* */) ;

/**
 * \brief Type converter XPS rn type.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the number.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_dbl_rn(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                        /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                        /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                        void *data /* double* */) ;

/**
 * \brief Type converter XPS rn type, but return it as a float.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the number.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_fl_rn(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                       /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                       /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                       void *data /* float* */) ;

/**
 * \brief Type converter XPS prn type.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the number.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_dbl_prn(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                         /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                         /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                         void *data /* double* */) ;

/**
 * \brief Type converter XPS prn type, but return it as a float.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the number.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_fl_prn(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                        /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                        /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                        void *data /* double* */) ;

/**
 * \brief Type converter XPS geone type.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the number.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_dbl_ST_GEOne(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                              /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                              /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                              void *data /* double* */) ;

/**
 * \brief Type converter XPS geone type, but return as a float.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the number.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_fl_ST_GEOne(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                             /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                             /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                             void *data /* double* */) ;

/**
 * \brief Type converter XPS gezero type.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the number.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_dbl_ST_GEZero(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                               /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                               /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                               void *data /* double* */) ;

/**
 * \brief Type converter XPS gezero type, but return it as a float.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the number.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_fl_ST_GEZero(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                              /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                              /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                              void *data /* double* */) ;

/**
 * \brief Type converter XPS zeroone type.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the number.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_dbl_ST_ZeroOne(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                                /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                                /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                                void *data /* double* */) ;

/**
 * \brief Type converter XPS zeroone type, but return it as a float.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the number.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_fl_ST_ZeroOne(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                               /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                               /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                               void *data /* double* */) ;

/**
 * \brief Type converter for XPS double. Places result into a floating
 * point type.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the number.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_fl_ST_Double(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                              /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                              /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                              void *data /* float* */) ;

/**
 * \brief Type converter XPS double type. Places result into a double type.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the number.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_dbl_ST_Double(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                               /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                               /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                               void *data /* double* */) ;

/* The XPS 0.75 spec defines the following types of number patterns:

    <!--DEFINE [pint]       "([1-9][0-9]*)" -->
    <!--DEFINE [uint]       "([0-9]+)" -->
    <!--DEFINE [dec]        "(\-?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+)))" -->
    <!--DEFINE [rn]         "((\-|\+)?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)" -->
    <!--DEFINE [prn]        "(\+?(([0-9]+(\.[0-9]+)?)|(\.[0-9]+))((e|E)(\-|\+)?[0-9]+)?)" -->

  The XPS 0.75 spec also defines the following simple types:

    ST_GEOne
    ST_GEZero
    ST_ZeroOne

  Which are all based on XML Schema ST_Double (which is an XML schema double
  with no INF, -INF and NaN) and have further value restrictions.

  xps_xml_to_double implements:
    dec, rn, prn, and ST_Double (which is equivalent to rn)

  xps_xml_to_int implements:
    int, pint and uint
*/
enum { xps_dec = 1, xps_rn, xps_prn, xps_uint, xps_pint, xps_int,
       xps_ST_Double = xps_rn } ;

Bool xps_xml_to_int(/*@in@*/ /*@notnull@*/ utf8_buffer* input,
                    /*@in@*/ /*@notnull@*/ int32* p_int,
                    int32 type,
                    /*@in@*/ /*@notnull@*/ int32 *error_result) ;

/** \brief Type converter for XML double type.
 *
 * Converts an XML string to a double number.
 *
 * \param input Pointer to UTF-8 string to scan.
 * \param p_double Pointer to returned double value.
 * \param type Type of double syntax to scan.
 * \param error_result Reason for scan error.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_xml_to_double(/*@in@*/ /*@notnull@*/ utf8_buffer* input,
                       /*@in@*/ /*@notnull@*/ double* p_double,
                       int32 type,
                      /*@in@*/ /*@notnull@*/ int32 *error_result) ;

/**
 * \brief Type converter for a Boolean as per the 0.75 s0schema.xsd
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the Boolean.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_Boolean(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                            /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                            /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                            /*@notnull@*/ void *data /* Bool* */) ;

/**
 * \brief Syntax checking type converter for a unicode string.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the unicode string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_UnicodeString(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                                  /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                                  /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                                  /*@notnull@*/ void *data /* utf8_buffer* */) ;

/**
 * \brief Scan dublin type.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the unicode string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_dublin_type(xmlGFilter *filter,
                             xmlGIStr *attrlocalname,
                             utf8_buffer* value,
                             void *data /* utf8_buffer* */) ;

/**
 * \brief Scan dublin value.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store the unicode string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_dublin_value(xmlGFilter *filter,
                              xmlGIStr *attrlocalname,
                              utf8_buffer* value,
                              void *data /* utf8_buffer* */) ;

/**
 * \brief Syntax checking type converter for ST_Name.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Pointer to utf8_buffer to store the ST_Name.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_Name(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                         /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                         /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                         /*@notnull@*/ void *data /* utf8_buffer* */) ;

/**
 * \brief Converter for TargetMode. Defined in relationships.xsd
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store an interned string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_TargetMode(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                               /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                               /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                               /*@notnull@*/ void *data /* xmlGIStr** */) ;

/**
 * \brief Converter for FixedPage.NavigateUri.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store an interned string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_navigate_uri(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                              /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                              /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                              /*@notnull@*/ void *data /* NULL */) ;

/**
 * \brief Converter for FillRule. Defined in s0schema.xsd
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store an interned string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_FillRule(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                             /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                             /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                             /*@notnull@*/ void *data /* xmlGIStr** */) ;

/**
 * \brief Converter for CaretStops.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store an interned string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_CaretStops(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                               /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                               /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                               /*@notnull@*/ void *data /* NULL */) ;

/**
 * \brief Converter for MappingMode.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store an interned string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_MappingMode(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                                /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                                /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                                /*@notnull@*/ void *data /* xmlGIStr** */) ;

/**
 * \brief Converter for SpreadMethod.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store an interned string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_SpreadMethod(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                                 /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                                 /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                                 /*@notnull@*/ void *data /* xmlGIStr** */) ;

/**
 * \brief Converter for ColorInterpolationMode.
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store an interned string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_ClrIntMode(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                               /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                               /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                               /*@notnull@*/ void *data /* xmlGIStr** */) ;

/**
 * \brief Converter for StyleSimulations. Defined in s0schema.xsd
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store a style value.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_StyleSimulations(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                                     /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                                     /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                                     /*@notnull@*/ void *data /* int32* */) ;
#define STYLE_BOLD_FLAG 1
#define STYLE_ITALIC_FLAG 2
#define STYLE_NONE 0
#define STYLE_BOLD STYLE_BOLD_FLAG
#define STYLE_ITALIC STYLE_ITALIC_FLAG
#define STYLE_BOLDITALIC STYLE_BOLD_FLAG|STYLE_ITALIC_FLAG
#define SIMULATE_BOLD(s) (((s) & STYLE_BOLD_FLAG) != 0)
#define SIMULATE_ITALIC(s) (((s) & STYLE_ITALIC_FLAG) != 0)

/**
 * \brief Converter for StrokeLineJoin. Defined in s0schema.xsd
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store an interned string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_LineJoin(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                             /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                             /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                             /*@notnull@*/ void *data /* xmlGIStr** */) ;

/**
 * \brief Converter for StrokeDashCap. Defined in s0schema.xsd
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store an interned string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_DashCap(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                            /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                            /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                            /*@notnull@*/ void *data /* xmlGIStr** */) ;

/**
 * \brief Converter for SweepDirection. Defined in s0schema.xsd
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store an interned string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_SweepDirection(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                                   /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                                   /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                                   /*@notnull@*/ void *data /* xmlGIStr** */) ;

/**
 * \brief Converter for TileMode. Defined in s0schema.xsd
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store an interned string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_TileMode(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                             /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                             /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                             /*@notnull@*/ void *data /* xmlGIStr** */) ;

/**
 * \brief Converter for ViewPortUnits. Defined in s0schema.xsd
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store an interned string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_ViewUnits(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                              /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                              /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                              /*@notnull@*/ void *data /* xmlGIStr** */) ;

/**
 * \brief Converter for EdgeMode. Defined in s0schema.xsd
 *
 * \param filter Pointer to XML filter.
 * \param attrlocalname The attribute name being converted.
 * \param value Pointer to a UTF-8 value string.
 * \param data Location to store an interned string.
 *
 * \retval TRUE Success. The location and length of the UTF-8 string are updated
 *              to a point after the parsed type syntax.
 * \retval FALSE Failure. The location and length of the UTF-8 string are not
 *               updated. The type-specific output may not be updated.
 */
Bool xps_convert_ST_EdgeMode(/*@in@*/ /*@notnull@*/ xmlGFilter *filter,
                             /*@in@*/ /*@notnull@*/ xmlGIStr *attrlocalname,
                             /*@in@*/ /*@notnull@*/ utf8_buffer* value,
                             /*@notnull@*/ void *data /* xmlGIStr** */) ;

/* ============================================================================
 * GGS XPS extensions.
 * ============================================================================
 */
Bool ggs_xps_convert_userlabel(xmlGFilter *filter,
                               xmlGIStr *attrlocalname,
                               utf8_buffer* value,
                               void *data /* Bool* */) ;

/* ============================================================================
* Log stripped */
#endif
