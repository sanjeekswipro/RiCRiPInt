/** \file
 * \ingroup xps
 *
 * $HopeName: COREedoc!shared:xpsiccbased.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2006-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * xps iccbased colorspace cache interface and sRGB and scRGB
 * ICCBased colorspaces
 */

#ifndef __XPSICCBASED_H__
#define __XPSICCBASED_H__ (1)

#include "objecth.h"
#include "xpsparts.h"

struct SWSTART ;

Bool xps_colorspace_swstart(struct SWSTART *params) ;
void xps_colorspace_finish(void) ;

/* Default icc profile names and name lengths */
#define DEF_SRGB_PROFILE_STR  (uint8*) "iccprofiles/sRGB_IEC61966-2-1_withBPC.icc"
#define DEF_SCRGB_PROFILE_STR (uint8*) "iccprofiles/scRGB.icc"
#define DEF_CMYK_PROFILE_STR  (uint8*) "iccprofiles/ECI_USWebCoatedSWOP.icc"

#define DEF_SRGB_PROF_STRLEN  41
#define DEF_SCRGB_PROF_STRLEN 21
#define DEF_CMYK_PROF_STRLEN  35


/**
 * \brief
 * Set the sRGB space to a null object.
 */
extern
void xps_sRGB_reset( void );

/**
 * \brief
 * Get the sRGB space.
 *
 * \returns Pointer to sRGB space object.
 */
extern
OBJECT* get_xps_sRGB( void );

/**
 * \brief
 * Create a default sRGB iccbased colorspace and setcolorspace for the
 * colortype.
 *
 * \param[in] colortype
 * The colortype e.g. GSC_FILL etc.
 *
 * \return
 * \c TRUE if colorspace is found or created and set, else \c FALSE.
 */
extern
Bool set_xps_sRGB( int32 colortype );

/**
 * \brief
 * Create a default sRGB iccbased colorspace and make it the BlendRGB
 * intercept.
 *
 * \return
 * \c TRUE if colorspace found or created and used, else \c FALSE.
 */
extern
Bool set_xps_blendRGB( void );

/**
 * \brief
 * Compare md5s to see if a profile is the default sRGB profile.
 *
 * \param[in] iccbasedspace
 * The profile to compare with sRGB.
 * \param[out] match
 * \c TRUE if there was a match, else \c FALSE.
 *
 * \return
 * \c TRUE if no problems encountered, else \c FALSE.
 */
extern
Bool xps_profile_is_sRGB( OBJECT *iccbasedspace, Bool *match );

/**
 * \brief
 * Set the scRGB space to a null object.
 */
extern
void xps_scRGB_reset( void );

/**
 * \brief
 * Create a default scRGB iccbased colorspace and setcolorspace for the
 * colortype.
 *
 * \param[in] colortype
 * The colortype e.g. GSC_FILL etc.
 *
 * \return
 * \c TRUE if colorspace is found or created and set, else \c FALSE.
 */
extern
Bool set_xps_scRGB( int32 colortype );

/**
 * \brief
 * Compare md5s to see if a profile is the default scRGB profile.
 *
 * \param[in] iccbasedspace
 * The profile to compare with scRGB.
 * \param[out] match
 * \c TRUE if there was a match, else \c FALSE.
 *
 * \return
 * \c TRUE if no problems encountered, else \c FALSE.
 */
extern
Bool xps_profile_is_scRGB( OBJECT *iccbasedspace, Bool *match );


/**
 * \brief
 * Find an ICCBased colorspace object in the cache or add one.
 *
 * The URI from the color profile partname is first made an absolute
 * URI if it is in relative form.
 *
 * A cache is kept of all ICCBased colorspaces used on a FixedPage,
 * indexed on the absolute URI.
 *
 * \param[in] color_profile_partname
 * Pointer to profile partname on which to base the colorspace object.
 * \param[in] n_colorants
 * Number of colorants for entry in RSD filter dict.
 * \param[out] colorspace
 * Pointer to PS ICCBased colorspace array object found or made.
 *
 * \return
 * \c TRUE if colorspace is found or created and added, else \c FALSE.
 */
extern
Bool xps_icc_cache_define(/*@in@*/ /*@notnull@*/
                          xps_partname_t* color_profile_partname,
                          int32 n_colorants,
                          /*@in@*/ /*@notnull@*/
                          OBJECT **colorspace);

#endif

/* Log stripped */
