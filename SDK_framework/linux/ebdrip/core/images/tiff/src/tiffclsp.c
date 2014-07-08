/** \file
 * \ingroup tiff
 *
 * $HopeName: SWv20tiff!src:tiffclsp.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2014, 2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * TIFF execution operators.
 */

#include "core.h"
#include "graphics.h"           /* gstateptr->colorInfo */
#include "gscequiv.h"           /* GSC_EQUIV_LVL_NONEKNOWN */
#include "gschead.h"            /* gsc_setcolorspace() */
#include "gscsmpxform.h"        /* EQUIVCOLOR */
#include "gstate.h"             /* gstateptr */
#include "hqmemcmp.h"           /* HqMemCmp */
#include "mm.h"                 /* mm_alloc */
#include "namedef_.h"           /* NAME_... */
#include "objects.h"            /* OBJECT */
#include "swerrors.h"           /* VMERROR */

#include "monitori.h"           /* emonitorf */

#include "tiffclsp.h"

typedef struct cmykequiv_item_t {
  NAMECACHE     *nmActual ;
  COLORANTINDEX  ciActual ;
  int32          nequivlevel;
  EQUIVCOLOR     cmykequiv;
  struct cmykequiv_item_t *next;
} cmykequiv_item_t ;

struct cmyk_spot_data {
  cmykequiv_item_t *gdbhead;
  mm_pool_t mm_pool;
} ;


static void HSV2RGB(float h, float s, float v, float *r, float *g, float *b)
{
  int i;
  float f, p, q, t;

  if ( s == 0 ) {
    /* achromatic (grey) */
    *r = *g = *b = v;
    return;
  }

  h /= 60;                        /* sector 0 to 5 */
  i = (int)floor( h );
  HQASSERT(i >= 0 && i <= 5, "Hue sector is out of range") ;

  f = h - i;                      /* fractional part of h */
  p = v * ( 1 - s );
  q = v * ( 1 - s * f );
  t = v * ( 1 - s * ( 1 - f ) );

  switch ( i ) {
  case 0:
    *r = v;
    *g = t;
    *b = p;
    break;
  case 1:
    *r = q;
    *g = v;
    *b = p;
    break;
  case 2:
    *r = p;
    *g = v;
    *b = t;
    break;
  case 3:
    *r = p;
    *g = q;
    *b = v;
    break;
  case 4:
    *r = t;
    *g = p;
    *b = v;
    break;
  default:                /* case 5: */
    *r = v;
    *g = p;
    *b = q;
    break;
  }

}

static void removeall_cmyk_equivs( tiff_image_data_t *self)
{
  cmykequiv_item_t * dbcurr;
  cmykequiv_item_t * dbnext;
  HQASSERT(self,"NULL self in removeall_cmyk_equivs");

  if ( self->sp_data ) {
    for (dbcurr = self->sp_data->gdbhead ; dbcurr ; dbcurr = dbnext) {
      dbnext = dbcurr->next;
      mm_free(self->sp_data->mm_pool,
              dbcurr,
              sizeof(cmykequiv_item_t)) ;
    }
    self->sp_data->gdbhead = NULL;
  }
}

/* Convert Lab to RGB
   => L = 0..100, a,b = -128..127
   <= R,G,B = 0.0..1.0
 */
static void Lab2RGB(float L, float a, float b, float * R, float * G, float * B)
{
  double X, Y, Z, _r, _g, _b ;

  /* From Lab to XYZ... */
  Y = (L + 16)/116.0 ;
  X = a/500.0 + Y ;
  Z = Y - b/200.0 ;

  Y = (Y > 0.206893) ? Y*Y*Y : (Y - 16/116)/7.787 ;
  X = (X > 0.206893) ? X*X*X : (X - 16/116)/7.787 ;
  Z = (Z > 0.206893) ? Z*Z*Z : (Z - 16/116)/7.787 ;

  X *= 0.95047 ;    /* Observer= 2°, Illuminant= D65 */
  Z *= 1.08883 ;

  /* From XYZ to RGB... */
  _r = X* 3.2406 + Y*-1.5372 + Z*-0.4986 ;
  _g = X*-0.9689 + Y* 1.8758 + Z* 0.0415 ;
  _b = X* 0.0557 + Y*-0.2040 + Z* 1.0570 ;

  _r = (_r > 0.0031308) ? 1.055 * pow(_r, 1/2.4) - 0.055 : 12.92*_r ;
  _g = (_g > 0.0031308) ? 1.055 * pow(_g, 1/2.4) - 0.055 : 12.92*_g ;
  _b = (_b > 0.0031308) ? 1.055 * pow(_b, 1/2.4) - 0.055 : 12.92*_b ;

  /* Clip out-of-gamut components (as Photoshop does) */
  *R = (_r < 0.0) ? 0.0f : (_r > 1.0) ? 1.0f : (float) _r ;
  *G = (_g < 0.0) ? 0.0f : (_g > 1.0) ? 1.0f : (float) _g ;
  *B = (_b < 0.0) ? 0.0f : (_b > 1.0) ? 1.0f : (float) _b ;
}


static Bool add_cmyk_equivs(tiff_image_data_t *self,
                            NAMECACHE *   pColorantName,
                            COLORANTINDEX index,
                            PSDisplayInfo *inkdetails)
{
  cmykequiv_item_t * dbcurr;
  cmykequiv_item_t * dbprev;
  HQASSERT(self,"NULL self in add_cmyk_equivs");
  HQASSERT(pColorantName,"NULL pColorantName in add_cmyk_equivs");

  if (self->sp_data->gdbhead == NULL) {
    dbcurr = self->sp_data->gdbhead = mm_alloc( self->sp_data->mm_pool ,
                                                sizeof(cmykequiv_item_t),
                                                MM_ALLOC_CLASS_TIFF_COLOR ) ;
    if (dbcurr == NULL)
      return error_handler(VMERROR);
  } else {
    /* look and see if already in database */
    for (dbprev = NULL, dbcurr = self->sp_data->gdbhead;
         dbcurr != NULL;
         dbprev = dbcurr, dbcurr = dbcurr->next) {
      if (dbcurr->ciActual == index)
        return (dbcurr->nequivlevel != GSC_EQUIV_LVL_NONEKNOWN) ;
    }
    dbcurr = mm_alloc( self->sp_data->mm_pool ,
                       sizeof(cmykequiv_item_t),
                       MM_ALLOC_CLASS_TIFF_COLOR ) ;
    if (dbcurr == NULL)
      return error_handler(VMERROR);
    dbprev->next = dbcurr;
  }

  dbcurr->nequivlevel = GSC_EQUIV_LVL_NONEKNOWN;
  dbcurr->ciActual = index;
  dbcurr->nmActual = pColorantName;
  dbcurr->next = NULL;
  if (!gsc_rcbequiv_lookup(gstateptr->colorInfo, pColorantName,
                           dbcurr->cmykequiv, &dbcurr->nequivlevel))
    return FALSE;

  if (dbcurr->nequivlevel != GSC_EQUIV_LVL_NONEKNOWN)
    return TRUE;

  /* If it's not known then we can use the data from the job */
  if ( inkdetails != NULL ) {
    float a = inkdetails->color[0] ;
    float b = inkdetails->color[1] ;
    float c = inkdetails->color[2] ;

    switch ( inkdetails->colorSpace ) {
    case PhotoShop_RGB:
      a /= 65535.0f ;  /* R */
      b /= 65535.0f ;  /* G */
      c /= 65535.0f ;  /* B */
      break ;

    case PhotoShop_HSB:
      a = a/65536.0f * 360.0f ; /* Hue */
      b /= 65535.0f ;           /* Saturation */
      c /= 65535.0f ;           /* Brightness */
      HSV2RGB(a, b, c, &a, &b, &c) ;
      break ;

    case PhotoShop_CMYK: /* No longer generated by Photoshop, but supported */
      dbcurr->cmykequiv[0] = a / 65535.0f ;
      dbcurr->cmykequiv[1] = b / 65535.0f ;
      dbcurr->cmykequiv[2] = c / 65535.0f ;
      dbcurr->cmykequiv[3] = inkdetails->color[3] / 65535.0f ;
      return TRUE ;

    case PhotoShop_Lab:
      a /= 100.0f ;                                       /* L 0..100 */
      b = ((b >= 32768.0f) ? b - 65536.0f : b) / 100.0f ; /* a -128..127 */
      c = ((c >= 32768.0f) ? c - 65536.0f : c) / 100.0f ; /* b -128..127 */
      Lab2RGB(a, b, c, &a, &b, &c) ;
      break ;

    case PhotoShop_Gray:
      a = 1.0f - a / 10000.0f ;     /* This conversion from gray level to RGB */
      a = (float)pow(a, 1.0 / 2.1892) ; /* is empirical - highly nonlinear. */
      dbcurr->cmykequiv[0] = 0.0f ; /* See [66184] for details. */
      dbcurr->cmykequiv[1] = 0.0f ;
      dbcurr->cmykequiv[2] = 0.0f ;
      dbcurr->cmykequiv[3] = 1.0f - a ;
      return TRUE;

    default:
      /* Either there was no AlternateSpotColors table, or something has gone
         horribly wrong. We don't have the swatch tables so we can't use the
         textual token in the table. An Event would be useful, but if we were
         going to do that we'd be better doing it earlier so any definition
         could be overridden. */
      a = -1 ;
      break ;
    }
    if (a > -1) {
      /* If here, we have RGB in a,b,c - do naive cmyk conversion.
         We could do much better than this of course. */
      dbcurr->cmykequiv[0] = 1.0f - a ;
      dbcurr->cmykequiv[1] = 1.0f - b ;
      dbcurr->cmykequiv[2] = 1.0f - c ;
      dbcurr->cmykequiv[3] = 0.0f ;
      return TRUE;
    }
  }

  /* UVM("%%%%[ Warning: Unable to find color for TIFF channel %s ]%%%%\n") */
  emonitorf(CoreContext.page ? CoreContext.page->timeline : 0,
            MON_CHANNEL_MONITOR, MON_TYPE_TIFFBADCOLOR, (uint8*)
            "%%%%[ Warning: Unable to find color for TIFF channel %.*s ]%%%%\n",
            pColorantName->len, pColorantName->clist) ;
  /* use black as a last resort */
  return (gsc_rcbequiv_lookup(gstateptr->colorInfo,
                              &system_names[NAME_Black],
                              dbcurr->cmykequiv,
                              &dbcurr->nequivlevel) &&
          dbcurr->nequivlevel != GSC_EQUIV_LVL_NONEKNOWN) ;
}

static NAMECACHE *get_cmyk_equiv_callback( GUCR_RASTERSTYLE* rasterStyle ,
                                           COLORANTINDEX ciPseudo ,
                                           EQUIVCOLOR **equiv,
                                           void *private_data)
{
  cmykequiv_item_t * dbcurr;
  tiff_image_data_t *self = private_data;

  UNUSED_PARAM( GUCR_RASTERSTYLE* , rasterStyle ) ;
  HQASSERT(self,"NULL self in get_cmyk_equiv_callback");

  for (dbcurr = self->sp_data->gdbhead ; dbcurr != NULL ; dbcurr = dbcurr->next) {
    if (dbcurr->ciActual == ciPseudo) {
      *equiv = &(dbcurr->cmykequiv);
      return dbcurr->nmActual ;
    }
  }
  return NULL;
}


static void destroy_cmyk_equivstuff( tiff_image_data_t *self)
{
  HQASSERT(self,"NULL self");

  removeall_cmyk_equivs(self);
  if (self->colindices) {
    mm_free(self->sp_data->mm_pool,
            self->colindices,
            sizeof(COLORANTINDEX) *  self->samples_per_pixel) ;
    self->colindices = NULL;
  }
}

static Bool do_cmyk_equiv(tiff_image_data_t *self,
                          NAMECACHE *   pColorantName,
                          COLORANTINDEX index,
                          PSDisplayInfo *inkdetails)
{
  HQASSERT(self,"NULL self in do_cmyk_equiv");
  HQASSERT(pColorantName,"NULL pColorantName in do_cmyk_equiv");

  self->colindices[index] = index;

  if ( !add_cmyk_equivs(self, pColorantName, self->colindices[index],
                        inkdetails) ) {
    destroy_cmyk_equivstuff(self);
    return FALSE;
  }

  return TRUE;
}

static Bool fetch_photoshop_colors(tiff_image_data_t *self,
                                   COLORANTINDEX index)
{
  NAMECACHE *   pColorantName;
  uint8* pname;
  uint32 namelen;
  uint32 spots = 0;
  PSDisplayInfo *inkdetails;

  HQASSERT(self,"NULL self in fetch_photoshop_colors");
  HQASSERT(self->inknames != NULL && self->inkdetails != NULL,
           "Missing ink details in fetch_photoshop_colors");

  if (self->inknames == NULL || self->inkdetails == NULL)
    return TRUE ;

  pname = self->inknames ;
  inkdetails = self->inkdetails ;

  /* We must ignore masks in the future */
  while (spots < (self->PSDspots + self->PSDmasks)) {
    /* names are Pascal strings rather than null terminated */
    namelen = (uint32)*pname++;
    if (!namelen)
      break;

    if (inkdetails->type == PSD_spot || inkdetails->type == PSD_mask) {
      if ( (pColorantName = cachename( pname , namelen )) == NULL )
        return FALSE;
      if (!do_cmyk_equiv(self, pColorantName, index++, inkdetails))
        return FALSE;
      ++spots ;
    }
    pname += namelen ;
    ++inkdetails ;
  }

  return TRUE;
}

static Bool process_cmyk_equivalents( tiff_image_data_t *self )           /* I */
{
  uint32        j,k;
  COLORANTINDEX  index;
  uint8         *pname;
  GUCR_RASTERSTYLE *hr;
  NAMECACHE     *pColorantName;

  HQASSERT(self,"NULL self in process_cmyk_equivalents");

  hr = gsc_getRS(gstateptr->colorInfo);
  index = 0;

  switch (self->photoshopinks) {
  case e_photoshopinks_none:
    {
      uint32 namelen;

      for ( j = 0;
            (index < (COLORANTINDEX)self->samples_per_pixel) && (j < self->inknameslen);
            index++)
      {
        pname = self->inknames + j;
        /* skip to next null character */
        k = j;
        while ((self->inknames[j] != 0) && j < self->inknameslen )
          j++;

        namelen = j - k;
        j++;

        if ( (pColorantName = cachename( pname , namelen )) == NULL )
          return FALSE;
        if (!do_cmyk_equiv( self , pColorantName, index, NULL))
          return FALSE;
      }
    }
    break;

    /* names extrascted from TAG_Photoshop3ImageResource */
  case e_photoshopinks_gray:
    {
      /* first add the process color name */
      if (!do_cmyk_equiv(self, &system_names[NAME_Gray], index++, NULL))
        return FALSE;

      if (!fetch_photoshop_colors(self ,index))
        return FALSE;
    }
    break;
  case e_photoshopinks_rgb:
    {
      static NAMECACHE *process_cols[] = {
        &system_names[NAME_Red],
        &system_names[NAME_Green],
        &system_names[NAME_Blue]
      } ;

      /* first add the process color names */
      for (j = 0;j < 3;j++) {
        if (!do_cmyk_equiv(self, process_cols[j], index++, NULL))
          return FALSE;
      }
      if (!fetch_photoshop_colors(self  , index))
        return FALSE;
    }
    break;
  case e_photoshopinks_cmy:
  case e_photoshopinks_cmyk:
    {
      static NAMECACHE *process_cols[] = {
        &system_names[NAME_Cyan],
        &system_names[NAME_Magenta],
        &system_names[NAME_Yellow],
        &system_names[NAME_Black]
      } ;

      /* first add the CMY process color names */
      for (j = 0;j < 3;j++) {
        if (!do_cmyk_equiv(self, process_cols[j], index++, NULL))
          return FALSE;
      }

      if (self->photoshopinks == e_photoshopinks_cmyk) {
        /* Add the K process color name */
        if (!do_cmyk_equiv(self, process_cols[j], index++, NULL))
          return FALSE;
      }

      if (!fetch_photoshop_colors(self  , index))
        return FALSE;
    }
    break;
  default:
    return FALSE;
  }
  return TRUE;
}

Bool tiff_get_noncmyk_colorspace(tiff_image_data_t *self,
                                 OBJECT *cspace)
{
  COLORANTINDEX *colindices;
  OBJECT *dn_space;

  HQASSERT(self,"NULL self");
  HQASSERT(cspace,"NULL cspace");

  colindices = mm_alloc( self->sp_data->mm_pool ,
                         sizeof(COLORANTINDEX) *  self->samples_per_pixel,
                         MM_ALLOC_CLASS_TIFF_COLOR ) ;
  if (colindices == NULL) {
    return error_handler(VMERROR);
  }
  self->colindices = colindices;

  if ( !process_cmyk_equivalents(self) ) {
    destroy_cmyk_equivstuff(self);
    return FALSE;
  }
  dn_space = gsc_spacecache_getcolorspace(gstateptr->colorInfo,
                                          NULL , /* raster style not req */
                                          (int32)self->samples_per_pixel ,
                                          colindices ,
                                          get_cmyk_equiv_callback,
                                          (void *)self) ;
  if (dn_space == NULL) {
    destroy_cmyk_equivstuff(self);
    return FALSE ;
  }

  Copy(cspace, dn_space) ;

  return TRUE;
}

Bool tiff_set_noncmyk_colorspace( tiff_image_data_t *self)
{
  OBJECT cspace = OBJECT_NOTVM_NOTHING ;
  HQASSERT(self,"NULL self");

  if ( !tiff_get_noncmyk_colorspace(self, &cspace))
    return FALSE;
  if ( !gsc_setcustomcolorspacedirect( gstateptr->colorInfo ,
                                       GSC_FILL ,
                                       &cspace ,
                                       FALSE /* fCompositing */)) {
    tiff_destroy_noncmyk_colorspace(self);
    return FALSE ;
  }
  return TRUE;
}


Bool tiff_init_noncmyk_colorspace(tiff_image_data_t *self, mm_pool_t mm_pool)
{
  HQASSERT(self,"NULL self in tiff_init_noncmyk_colorspace");

  self->sp_data = mm_alloc(mm_pool, sizeof(cmyk_spot_data), MM_ALLOC_CLASS_TIFF_COLOR);
  if (self->sp_data == NULL)
    return error_handler(VMERROR);
  self->sp_data->gdbhead = NULL;
  self->sp_data->mm_pool = mm_pool;
  return TRUE;
}

void tiff_destroy_noncmyk_colorspace(tiff_image_data_t *self)
{
  HQASSERT(self,"Attempt to destroy NULL tiff_noncmyk_colorspace");
  if (self) {
    destroy_cmyk_equivstuff(self);
    if (self->sp_data) {
      mm_free(self->sp_data->mm_pool, self->sp_data, sizeof(cmyk_spot_data));
      self->sp_data = NULL;
    }
  }
}



Bool tiff_set_DeviceN_colorspace(tiff_color_space_t* p_color_space,
                                 tiff_image_data_t*  p_image_data)
{
  uint32 tintsz;
  OBJECT  ocspace = OBJECT_NOTVM_NOTHING ;
  OBJECT  ocolorants = OBJECT_NOTVM_NOTHING;
  OBJECT  ottransform = OBJECT_NOTVM_NOTHING;
  uint32  count_extra;
  uint32  i;
  uint32  ott_index = 0 ;
  Bool    result;

#define DoOp(_name) MACRO_START \
  object_store_operator(&oArray(ottransform)[ott_index++], (_name)) ; \
MACRO_END

#define DoInt(_i) MACRO_START \
  object_store_integer(&oArray(ottransform)[ott_index++], (_i)) ; \
MACRO_END

  HQASSERT(p_color_space,"NULL p_color_space in tiff_set_DeviceN_colorspace");
  HQASSERT(p_image_data,"NULL p_image_data in tiff_set_DeviceN_colorspace");

  /* Wah - need to set up a DeviceN color space - how many surplus samples */
  count_extra = p_image_data->samples_per_pixel - p_color_space->count_channels;

  /* Set PS arrays to be empty - makes tidy up easier */
  theTags(ocspace) = OARRAY|LITERAL|UNLIMITED;
  oArray(ocspace) = NULL;
  theLen(ocspace) = CAST_TO_UINT16(0);
  OCopy(ocolorants, ocspace);
  OCopy(ottransform, ocspace);

#define NUM_EXTRACOMMANDS 18u /* extra entries required for clut lookup procedure */

  tintsz = ( p_image_data->colormap != NULL)? NUM_EXTRACOMMANDS:0u;

  /* Create the arrays for setcolorspace, list of colorants ,and the tint transform */
  /** \todo ajcd 2007-12-26: Why in PS memory? */
  result = ps_array(&ocspace, 4) &&
           ps_array(&ocolorants, (int32)p_image_data->samples_per_pixel) &&
           ps_array(&ottransform, (int32)(count_extra + tintsz));

  if ( result ) {
    /* Defining a DeviceN colorspace */
    object_store_name(&oArray(ocspace)[0], NAME_DeviceN, LITERAL) ;

    /* Add base color space channel names */
    for ( i = 0; i < p_color_space->count_channels; i++ ) {
      theTags(oArray(ocolorants)[i]) = ONAME|LITERAL;
      oName(oArray(ocolorants)[i]) = p_color_space->channel_name[i];
    }
    /* Add /None for extra channels */
    for ( ; i < p_image_data->samples_per_pixel; i++ ) {
      object_store_name(&oArray(ocolorants)[i], NAME_None, LITERAL) ;
    }
    OCopy(oArray(ocspace)[1], ocolorants) ;

    /* Insert base color space as the alternative colorspace */
    theTags(oArray(ocspace)[2]) = ONAME|LITERAL;
    oName(oArray(ocspace)[2]) = p_color_space->base_space;

    /* Set up tint transform - enough pops to reduce samples to number expected */
    theTags(ottransform) |= EXECUTABLE;
    for ( i = 0; i < count_extra; ++i ) {
      DoOp(NAME_pop);
    }

    /* if palettised then do table lookup manually in the tint transform
       now we are rid of the extra channels */
    if (p_image_data->colormap != NULL) {
      OBJECT oclut = OBJECT_NOTVM_NOTHING;

      object_store_name(&oArray(ocspace)[2], NAME_DeviceRGB, LITERAL);

      theTags(oclut) = OSTRING | LITERAL | READ_ONLY ;
      theLen(oclut) = (uint16)(3 << p_image_data->bits_per_sample) ;
      oString(oclut) = p_image_data->colormap ;

      /* do an RGB color lookup (8-bit table arranged RRR..GGG..BBB..) */
#define NUMVALSPERCOMPONENT 256
      DoOp(NAME_cvi);
      DoOp(NAME_dup);
      oArray(ottransform)[ott_index++] = oclut ; /* Slot flags will be same */
      DoOp(NAME_exch);
      DoOp(NAME_get);
      DoOp(NAME_exch);
      DoInt(NUMVALSPERCOMPONENT) ;
      DoOp(NAME_add);
      DoOp(NAME_dup);
      oArray(ottransform)[ott_index++] = oclut ; /* Slot flags will be same */
      DoOp(NAME_exch);
      DoOp(NAME_get);
      DoOp(NAME_exch);
      DoInt(NUMVALSPERCOMPONENT) ;
      DoOp(NAME_add);
      oArray(ottransform)[ott_index++] = oclut ; /* Slot flags will be same */
      DoOp(NAME_exch);
      DoOp(NAME_get);

      HQASSERT(ott_index == (tintsz + 1),
               "wrong size of array for tint transform");
    }

    OCopy(oArray(ocspace)[3], ottransform);

    /* Setup the DeviceN color space */
    result = push(&ocspace, &operandstack) &&
             gsc_setcolorspace(gstateptr->colorInfo, &operandstack, GSC_FILL);
  }

  return result ;
}

/* Log stripped */
