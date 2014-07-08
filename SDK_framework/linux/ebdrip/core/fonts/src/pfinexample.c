/** \file
 * \ingroup PFIN
 *
 * $HopeName: COREfonts!src:pfinexample.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 *
 * \brief Example PFIN module implementation
 *
 * This is an example PFIN module implementation. As such it is unnecessarily
 * verbose - it defines methods such as configure and find which it does not
 * really need, and does a lot of sanity checking of parameters, just for the
 * sake of it.
 *
 * The font it defines only contains three glyphs, /.notdef, /space and /A in
 * that order, and each is hard-wired. This is simply an example, a real font
 * module would process some kind of font data to generate its glyphs.
 */

#include "core.h" /** \todo Move this outside of core */
#include "swdataapi.h"
#include "swpfinapi.h"

/* ========================================================================== */

typedef struct pfin_example_instance {
  sw_pfin_instance super ; /**< Superclass must be the first field */
  int    id;           /* a unique value because we are being paranoid in this example */
  HqBool suspended;    /* TRUE if we have been suspended by PFIN - only used for sanity checking during PFIN development */
  HqBool deferred;     /* TRUE if we have not yet defined our deferred fonts */
} pfin_example_instance ;

enum { WS_ID = 0x1234 };

/* -------------------------------------------------------------------------- */
/* Start (or resume) the PFIN module */

static sw_pfin_result RIPCALL pfin_example_start(sw_pfin_instance* instance,
                                       sw_pfin_define_context *context,
                                       sw_pfin_reason reason)
{
  pfin_example_instance *example;

  if ( (example = (pfin_example_instance *)instance) == NULL ) {
    HQFAIL("No context?") ;
    return SW_PFIN_ERROR ;
  }

  if (reason == SW_PFIN_REASON_START) {
    static sw_datum fontname = SW_DATUM_STRING("ExamplePFINFont");
    static sw_datum id = SW_DATUM_INTEGER(1);

    /* if the supplied structures or APIs are too old, we can't start */
    if ( instance->data_api->info.version < SW_DATA_API_VERSION_20071111 ||
         instance->mem->implementation->info.version < SW_MEMORY_API_VERSION_20071110 )
      return SW_PFIN_ERROR_VERSION;

    /* initialise workspace */
    example->id = WS_ID;
    example->suspended = FALSE;
    example->deferred = TRUE;

    /* define some fonts */
    if (instance->callbacks->define(context, &fontname, &id, 0) != SW_PFIN_SUCCESS)
      return SW_PFIN_ERROR;

  } else {
    /* for resume, there's nothing much to do */
    HQASSERT(example->suspended, "Resume when not suspended");
    example->suspended = FALSE;
  }

  return SW_PFIN_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* Quit (or suspend) the PFIN module */

static sw_pfin_result RIPCALL pfin_example_stop(sw_pfin_instance* instance,
                                      sw_pfin_define_context *context,
                                      sw_pfin_reason reason)
{
  pfin_example_instance *example ;

  UNUSED_PARAM(sw_pfin_define_context *, context) ;

  /* Downcast instance to example subclass*/
  if ( (example = (pfin_example_instance *)instance) == NULL ||
       example->id != WS_ID ) {
    HQFAIL("Module not initialised/resumed correctly");
    return SW_PFIN_ERROR;
  }

  /* do any tidying up, freeing caches etc (for suspend or quit) */


  /* remove our ID on quit, but not on suspend */
  switch (reason) {
  case SW_PFIN_REASON_STOP:
    example->id = 0;
    break ;
  case SW_PFIN_REASON_SUSPEND:
    example->suspended = TRUE;
    break ;
  default:
    HQFAIL("Invalid PFIN stop reason") ;
    return SW_PFIN_ERROR ;
  }

  return SW_PFIN_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* Configuration has changed.
 *
 * If a real PFIN module has no configuration options, then configure isn't
 * required.
 */

static sw_pfin_result RIPCALL pfin_example_configure(sw_pfin_instance* instance,
                                           sw_pfin_define_context *context,
                                           const sw_datum* config)
{
  pfin_example_instance *example ;

  UNUSED_PARAM(sw_pfin_define_context *, context) ;

  /* Downcast instance to example subclass*/
  if ( (example = (pfin_example_instance *)instance) == NULL ||
       example->id != WS_ID || example->suspended) {
    HQFAIL("Module not initialised/resumed correctly");
    return SW_PFIN_ERROR;
  }

  /* our configuration value must be a dictionary */
  if (config->type != SW_DATUM_TYPE_DICT)
    return SW_PFIN_ERROR_UNSUPPORTED;

  /* respond to configuration keys here */

  return SW_PFIN_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* findresource or resourceforall.
 *
 * If a real PFIN module defines all its fonts at initialise or config, then
 * find isn't required, but this example contains the following bare-bones
 * version for reference.
 */

static sw_pfin_result RIPCALL pfin_example_find(sw_pfin_instance* instance,
                                      sw_pfin_define_context *context,
                                      const sw_datum* findname)
{
  static sw_datum fontname = SW_DATUM_STRING("ExampleDeferredFont");
  static sw_datum id = SW_DATUM_INTEGER(2);
  HqBool define = FALSE;
  pfin_example_instance *example ;

  /* Downcast instance to example subclass*/
  if ( (example = (pfin_example_instance *)instance) == NULL ||
       example->id != WS_ID || example->suspended) {
    HQFAIL("Module not initialised/resumed correctly");
    return SW_PFIN_ERROR;
  }

  if (example->deferred) {
    if (findname->type == SW_DATUM_TYPE_NOTHING) {
      /* a resourceforall is about to happen */
      define = TRUE;
    } else {
      /* a findresource has failed to find the given fontname */
      define = instance->data_api->equal(&fontname, findname);
    }

    if (define) {
      example->deferred = FALSE;
      if (instance->callbacks->define(context, &fontname, &id, 0) != SW_PFIN_SUCCESS)
        return SW_PFIN_ERROR;
    }
  }
  /* return success whether we defined the font or not */
  return SW_PFIN_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* utility functions used by pfin_example_metrics() and pfin_example_outline() below */

static sw_pfin_result find_font(pfin_example_instance* pfin,
                                const sw_pfin_font* font)
{
  static sw_datum key = SW_DATUM_STRING("PFID");
  sw_datum value;
  int fid = -1;

  if ( pfin->super.data_api->get_keyed(&font->font, &key, &value) == SW_DATA_OK &&
       value.type == SW_DATUM_TYPE_INTEGER )
    fid = value.value.integer;

  if (fid < 1 || fid > 2)
    return -1;

  return fid;
}

/* -------------------------------------------------------------------------- */

typedef struct {
  unsigned int len;
  char*        name;
} glyphname ;

#define STRING(s) sizeof(""s"")-1,""s""

glyphname glyphlist[] = {
  {STRING(".notdef")},
  {STRING("space")},
  {STRING("A")},
  {0,0} /* end marker */
} ;

static sw_pfin_result find_glyph(pfin_example_instance* pfin,
                                 int fid, const sw_datum* glyph)
{
  int cid = -1;

  UNUSED_PARAM(pfin_example_instance * , pfin) ;

  if (fid >= 1 && fid <= 2) {
    switch (glyph->type) {
    case SW_DATUM_TYPE_INTEGER:
      cid = glyph->value.integer;
      if ( cid < 0 || cid >= sizeof(glyphlist)/sizeof(glyphname) )
        cid = -1;
      break;
    case SW_DATUM_TYPE_STRING:
      {
        glyphname* glyphs = glyphlist ;
        cid = 0 ;
        while (glyphs->len) {
          if (glyph->length == glyphs->len &&
              memcmp(glyph->value.string, glyphs->name, glyphs->len) == 0)
            return cid;
          glyphs++;
          cid++;
        }
      }
      cid = -1;
      break;
    }
  }

  return cid;
}

/* -------------------------------------------------------------------------- */
/* Return the metrics of a glyph */

static sw_pfin_result RIPCALL pfin_example_metrics(sw_pfin_instance* instance,
                                         const sw_pfin_font* font,
                                         const sw_datum* glyph, double metrics[2])
{
  int fid, cid;
  pfin_example_instance *example ;

  /* Downcast instance to example subclass*/
  if ( (example = (pfin_example_instance *)instance) == NULL ||
       example->id != WS_ID || example->suspended) {
    HQFAIL("Module not initialised/resumed correctly");
    return SW_PFIN_ERROR;
  }

  if ((fid = find_font(example, font)) == -1 ||
      (cid = find_glyph(example, fid, glyph)) == -1)
    return SW_PFIN_ERROR_UNKNOWN;

  /* all our fonts are monospaced! */
  metrics[0] = 1.0f;
  metrics[1] = 0;

  return SW_PFIN_SUCCESS;
}

/* -------------------------------------------------------------------------- */
/* Return the outline of a glyph */

static sw_pfin_result RIPCALL pfin_example_outline(sw_pfin_instance* instance,
                                         sw_pfin_outline_context *context,
                                         const sw_pfin_font* font,
                                         const sw_datum* glyph)
{
  int           cid, fid ;
  double        X = 0.1f, Y = 0.1f ;
  pfin_example_instance *example ;

  /* Downcast instance to example subclass*/
  if ( (example = (pfin_example_instance *)instance) == NULL ||
       example->id != WS_ID || example->suspended) {
    HQFAIL("Module not initialised/resumed correctly");
    return SW_PFIN_ERROR;
  }

  if ((fid = find_font(example, font)) == -1 ||
      (cid = find_glyph(example, fid, glyph)) == -1)
    return SW_PFIN_ERROR_UNKNOWN;

  /* weight adjustment */
  if (fid == 2) {
    X += 0.05f ;    /* our second font is bolder */
    Y += 0.05f ;
  }

  /* Simple hinting! Round stem widths to whole number of pixels */
  if (font->dx) {
    int i = (int) (X  / font->dx) ;
    X = font->dx * i ;
  }
  if (font->dy) {
    int i = (int) (Y / font->dy) ;
    Y = font->dy * i ;
  }

  switch (cid) {
  case 0: /* .notdef */
    if (instance->callbacks->move(context, 0, 0) ||
        instance->callbacks->line(context, 1.0f, 0) ||
        instance->callbacks->line(context, 1.0f, 1.0f) ||
        instance->callbacks->line(context, 0, 1.0f) ||
        instance->callbacks->move(context, X, Y) ||
        instance->callbacks->line(context, X, 1.0f-Y) ||
        instance->callbacks->line(context, 1.0f-X, 1.0f-Y) ||
        instance->callbacks->line(context, 1.0f-X, Y) )
      return SW_PFIN_ERROR;
    break;
  case 1: /* space */
    break ;
  case 2: /* A */
    if (instance->callbacks->move(context, 0.1f, 0) ||
        instance->callbacks->line(context, 0.1f+X, 0) ||
        instance->callbacks->line(context, 0.1f+X, 0.45f-Y/2) ||
        instance->callbacks->line(context, 0.9f-X, 0.45f-Y/2) ||
        instance->callbacks->line(context, 0.9f-X, 0) ||
        instance->callbacks->line(context, 0.9f, 0) ||
        instance->callbacks->line(context, 0.9f, 0.7f) ||
        instance->callbacks->curve(context, 0.9f,0.7f, 0.9f,0.9f, 0.7f,0.9f) ||
        instance->callbacks->line(context, 0.3f, 0.9f) ||
        instance->callbacks->curve(context, 0.3f,0.9f, 0.1f,0.9f, 0.1f,0.7f) ||
        instance->callbacks->move(context, 0.1f+X, 0.45f+Y/2) ||
        instance->callbacks->line(context, 0.1f+X, 0.7f) ||
        instance->callbacks->curve(context, 0.1f+X,0.7f, 0.1f+X,0.9f-Y, 0.3f,0.9f-Y) ||
        instance->callbacks->line(context, 0.7f, 0.9f-Y) ||
        instance->callbacks->curve(context, 0.7f,0.9f-Y, 0.9f-X,0.9f-Y, 0.9f-X,0.7f) ||
        instance->callbacks->line(context, 0.9f-X, 0.45f+Y/2) )
      return SW_PFIN_ERROR;
    break;
  }

  return SW_PFIN_SUCCESS;
}

/* ========================================================================== */
/* A demo PFIN module, which supplies one font and a single glyph */

const sw_pfin_api pfin_example_module = {
  {
    SW_PFIN_API_VERSION_20071111,
    (uint8*)"Demo",   /* The name by which the module will be configured */
    (uint8*)"An example PFIN font module\n"
            "0.01 (22-May-2007)\n"
            "\302\251 2007 Global Graphics Software Ltd.\n"
            "This example PFIN module can be used as a basis for writing "
            "real PFIN modules.",
    sizeof(pfin_example_instance) /* instance data size */
  },
  NULL, /* No init() */
  NULL, /* No finish() */
  pfin_example_start,
  pfin_example_stop,
  pfin_example_configure,
  pfin_example_find,
  pfin_example_metrics,
  pfin_example_outline
};

/* ========================================================================== */

/* Log stripped */
