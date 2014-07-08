/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swpfinapi.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007-2011 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 *
 * \brief  This header file provides definitions for PFIN (the Pluggable
 * Font Interface) to the core RIP.
 *
 * PFIN is used to implement font renderer libraries that support new
 * font formats and/or replace internal font handling for certain font types.
 */


#ifndef __SWPFIN_H__
#define __SWPFIN_H__

/* -------------------------------------------------------------------------- */
/** \defgroup swpfinapi Pluggable Font API
 * \ingroup interface
 * \{
 */

#include <stddef.h>  /* size_t */
#include "ripcall.h"
#include "swapi.h" /* sw_api_version */
#include "swmemapi.h"  /* sw_memory_instance */
#include "swdataapi.h" /* sw_data_api */
#include "swblobfactory.h" /* sw_blob_factory */

struct OBJECT ;

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/** \brief Version numbers defined for the PFIN API. */
enum {
  SW_PFIN_API_VERSION_20071111, /**< Original version */
  SW_PFIN_API_VERSION_20071231, /**< miscop added */
  SW_PFIN_API_VERSION_20080808, /**< bitimage and options added */
  SW_PFIN_API_VERSION_20090305, /**< uid added */
  SW_PFIN_API_VERSION_20090401  /**< flush added - Current version */
  /* new versions go here */
#ifdef CORE_INTERFACE_PRIVATE
  , SW_PFIN_API_VERSION_NEXT /* Do not ever use this name. */
  /* SW_PFIN_API_VERSION is provided so that the Harlequin Core RIP can test
     compatibility with current versions, without revising the registration
     code for every interface change.

     Implementations of sw_pfin_api within the Harlequin Core RIP should NOT
     use this; they should have explicit version numbers. Using explicit
     version numbers will allow them to be backwards-compatible without
     modifying the code for every interface change.
  */
  , SW_PFIN_API_VERSION = SW_PFIN_API_VERSION_NEXT - 1
#endif
};

/* -------------------------------------------------------------------------- */
/** \brief Return values from sw_pfin_api and sw_pfin_callback_api
    functions. */
enum {
  /* Entries present in SW_PFIN_API_VERSION_20071111: */
  SW_PFIN_SUCCESS = 0,   /**< Normal return value */
  SW_PFIN_ERROR,         /**< Some unknown failure occurred */
  SW_PFIN_ERROR_MEMORY,  /**< A memory allocation failed */
  SW_PFIN_ERROR_INVALID, /**< The module believes the font to be invalid */
  SW_PFIN_ERROR_UNSUPPORTED, /**< The module is unable to handle the font construction */
  SW_PFIN_ERROR_UNKNOWN, /**< The font or glyph is not known */
  SW_PFIN_ERROR_VERSION, /**< An insufficiently new api has been supplied */
  SW_PFIN_ERROR_SYNTAX   /**< Programming error - illegal parameters */
  /* End of entries present in SW_PFIN_API_VERSION_20071111 */
};

/** \brief Type of return values from the sw_pfin_api functions. */
typedef int sw_pfin_result ;

/** \brief Definition context opaque type.

    This is an opaque type definition for contexts passed to the
    implementation in API calls that can define fonts. Contexts pointers of
    this type can never be stored and re-used; they are only valid for the
    duration of the sw_pfin_api call which originally passed them to the
    implementation.
*/
typedef struct sw_pfin_define_context sw_pfin_define_context ;

/** \brief Outline context opaque type.

    This is an opaque type definition for contexts passed to the
    implementation in API calls that can generate outlines. Context pointers
    of this type can never be stored and re-used; they are only valid for the
    duration of the \c sw_pfin_api call which originally passed them to the
    implementation.
*/
typedef struct sw_pfin_outline_context sw_pfin_outline_context ;

/** \brief Bitmap context opaque type.

    This is an opaque type definition for contexts passed to the
    implementation in API calls that can generate bitmaps. Context pointers
    of this type can never be stored and re-used; they are only valid for the
    duration of the \c sw_pfin_api call which originally passed them to the
    implementation.
*/
typedef struct sw_pfin_bitmap_context sw_pfin_bitmap_context ;

/** \brief  A structure containing callback functions for PFIN modules.

   \p define() and \p undefine() are for font control.

   \p move(), \p line() and \p curve() are for path creation and can only be
   used from within a call to sw_pfin_api::outline(). A SW_PFIN_ERROR_SYNTAX
   error will result otherwise. PFIN begins and ends the character
   automatically and the path is closed implicitly.

   Versioning of this structure is tied to the sw_pfin_api version. The RIP
   expects only callbacks present in the implementation version registered to
   be called.
 */
typedef struct sw_pfin_callback_api {
  /* Entries present in SW_PFIN_API_VERSION_20071111: */

  /** \brief  Define a module-supplied Font or CIDFont.

      This can be called by the module during sw_pfin_api::start(),
      sw_pfin_api::configure() or sw_pfin_api::find().

      \param[in] context  The definition context passed by PFIN.

      \param[in] fontname  The name of the Font or CIDFont to define.

      The fontname must be a string or an array of strings. In the latter
      case all names are defined as the same font (with the same UniqueID),
      which is useful for aliasing common and generic fonts.

      \param[in] id  The module's reference to the font.

      This may simply be an integer or a string containing a filename. Font
      collections may require an array containing a string filename and an
      integer index. Multiple master fonts may require more complex arrays.

      All fonts defined by a module must have different ids. Aliases must be
      defined using an array fontname as above.

      \param[in] encoding  The encoding or character collection of the font.

      If a null datum, this is a Font with an Encoding of /StandardEncoding.

      If a string datum, a Font is defined with this named Encoding.

      Otherwise, it must be an array. If the array has three members, two
      strings and an integer, then the font is defined as a CIDFont with this
      Registry, Ordering and Supplement.

      If it is an array, but not a CID Registry, Ordering, and Supplement,
      all entries must be glyph name strings and the font is defined as a
      Font with this Encoding array.

      \return SW_PFIN_SUCCESS is returned if the Font or CIDFont was
      declared. SW_PFIN_ERROR_SYNTAX is returned if any parameters are
      illegal.
  */
  sw_pfin_result (RIPCALL *define)(/*@in@*/ /*@notnull@*/ sw_pfin_define_context *context,
                                   /*@in@*/ /*@notnull@*/ const sw_datum* fontname,
                                   /*@in@*/ /*@notnull@*/ const sw_datum* id,
                                   /*@in@*/ sw_datum* encoding);


  /** \brief  Undefine a previously-defined module-supplied Font or CIDFont.

      This can only sensibly be called from sw_pfin_api::configure().

      \param[in] context  The definition context passed by PFIN.

      \param[in] fontname  The name of the Font or CIDFont to undefine.

      The fontname must be a string or an array of strings. Only fonts
      belonging to this module can be undefined. If a font has subsequently
      been redefined it will not be undefined.

      \return SW_PFIN_SUCCESS is returned whether the Font or CIDFont was
      undefined correctly or was unknown. Returning an error in the latter
      case would serve little purpose. SW_PFIN_ERROR_SYNTAX is returned if
      the parameters are illegal.
  */
  sw_pfin_result (RIPCALL *undefine)(/*@in@*/ /*@notnull@*/ sw_pfin_define_context *context,
                                     /*@in@*/ /*@notnull@*/ sw_datum* fontname);


  /** \brief  Start a new contour, closing any previous contour.

      \param[in] context  The outline context passed by PFIN.

      \param[in] x,y  Coordinates of start point.

      \return  SW_PFIN_SUCCESS normally.
  */
  sw_pfin_result (RIPCALL *move)(/*@in@*/ /*@notnull@*/ sw_pfin_outline_context *context,
                                 double x, double y);


  /** \brief  Add a straight line to the current contour.

      \param[in] context  The outline context passed by PFIN.

      \param[in] x,y  Coordinates of end point.

      \return  SW_PFIN_SUCCESS normally.
  */
  sw_pfin_result (RIPCALL *line)(/*@in@*/ /*@notnull@*/ sw_pfin_outline_context *context,
                                 double x, double y);


  /** \brief  Add a bezier curve to the current contour.

      \param[in] context  The outline context passed by PFIN.

      \param[in] x0,y0  Coordinates of the first control point.

      \param[in] x1,y1  Coordinates of the second control point.

      \param[in] x2,y2  Coordinates of the end point.

      \return  SW_PFIN_SUCCESS normally.
  */
  sw_pfin_result (RIPCALL *curve)(/*@in@*/ /*@notnull@*/ sw_pfin_outline_context *context,
                                  double x0, double y0,
                                  double x1, double y1,
                                  double x2, double y2);

  /* End of entries present in SW_PFIN_API_VERSION_20071111 */

  /* Entries present in SW_PFIN_API_VERSION_20080808: */

  /** \brief  Create a bitmapped glyph.

      A module can respond to a request for a glyph by supplying a bitimage
      instead of a path.

      If the bitimage is already available in memory then it is delivered
      through this call. Alternatively, if no data buffer is specified then the
      module must implement the raster() method and will be called back to
      provide raster data in as many passes as are required.

      \param[in] context  The outline context passed by PFIN.

      \param[in] width,height  The dimensions of the bitimage. The width must
                               include any end-of-scanline padding bytes. The
                               raster data is supplied left to right and top to
                               bottom unless width or height are negative. If
                               width is negative then each scanline is supplied
                               right to left, and if height is negative then
                               lines are supplied bottom to top.

      \param[in] xoffset,yoffset  Offset to the cursor start position within the
                                  bitimage. In other words, the left sidebearing
                                  and baseline offsets. Note that the offsets
                                  are unaffected by the sign of width and height
                                  - they are the offsets from the bottom left of
                                  the bitimage, whichever raster direction has
                                  been chosen for the data. The cursor start
                                  position need not lie inside the bitimage, so
                                  these offsets are not bounded by the absolute
                                  width and height.

      \param[in] xres,yres  Resolution of the bitimage in dots per inch, or zero
                            for device resolution.

      \param[in] buffer  Pointer to the raster data or NULL if the module's
                         raster() callback is to be used instead.

      \param[in] length  Ignored if buffer is NULL. Otherwise, this is the
                         amount of data in the buffer. If too little data is
                         supplied, the raster() callback will be used to provide
                         additional data.


      \retval  SW_PFIN_SUCCESS normally, SW_PFIN_ERROR_SYNTAX if bitimage() is
               called more than once from within a sw_pfin_api::outline() call
               or if the bitimage data is not complete and the module has not
               implemented a raster() callback.
  */
  sw_pfin_result (RIPCALL *bitimage)(/*@in@*/ /*@notnull@*/ sw_pfin_outline_context *context,
                                     int width,   int height,
                                     int xoffset, int yoffset,
                                     double xres, double yres,
                                     /*@in@*/ unsigned char * buffer,
                                     size_t length);


  /** \brief Set, clear, toggle and read PFIN glyph creation options.

      \param[in] context  The outline context passed by PFIN.

      \param[in] mask  A bitmask of option bits to clear.

      \param[in] toggle  A bitmask of option bits to toggle.

      \retval  The options bits BEFORE being changed by the above.

      This single call is used to clear, set, toggle or read option bits. eg:

        To set:     options(context, bits, bits);
        To clear:   options(context, bits, 0);
        To toggle:  options(context, 0, bits);
        To read:    bits = options(context, 0, 0);

      It is possible to control multiple bits simultaneously, eg setting one
      while clearing another, toggling a third AND reading the previous state
      - which can then be easily restored:

        To restore: options(context, -1, bits);

      Logically, the operation is: new = (old & ~mask) ^ toggle
  */
  int (RIPCALL *options)(/*@in@*/ /*@notnull@*/ sw_pfin_outline_context *context,
                         int mask, int toggle);

  /* End of entries present in SW_PFIN_API_VERSION_20080808 */

  /* Entries present in SW_PFIN_API_VERSION_20090305: */

  /* \brief Read or write the unique ID to be used for a font definition.

     \param[in] context  The outline context passed by PFIN.

     \param[in] puid  A pointer to a 32bit signed integer.

     If the value pointed to is -1, it is overwritten with the UniqueID that
     will be allocated to the next font defined.

     If the value is not -1, it specifies the UniqueID to be used by the next
     font defined.

     \retval SW_PFIN_SUCCESS normally, SW_PFIN_ERROR_SYNTAX if called at an
     inappropriate time.

     Warning: Overriding the UniqueID must only be done with great care as
     misuse can result in font cache confusion and unpredictable output.

     Intended uses are forcing a PFIN-supplied standard font to have the
     UniqueID a job or environment expects, or allowing a module to warrant
     that a font has not changed since its previous definition to allow more
     efficient use of the font cache.

     Note that use of this call is optional and should be considered a possible
     performance optimisation or work-around for unwarranted job assumptions.
     If used, it must be issued before every call to define().
  */

  sw_pfin_result (RIPCALL *uid)(/*@in@*/ /*@notnull@*/ sw_pfin_define_context *context,
                                /*@in@*/ /*@notnull@*/ int *puid) ;

  /* End of entries present in SW_PFIN_API_VERSION_20090305: */

  /* Entries present in SW_PFIN_API_VERSION_20090401: */

  /* \brief Discard a glyph or a whole font from the fontcache.

     \param[in] uniqueid  The UniqueID of the font as read or written by the
                          uid() call above.

     \param[in] glyphname  An optional glyph identifier - string or integer
                           format only. If non-null, the specified glyph will
                           effectively be removed from the fontcache. If null,
                           the whole font is discarded.

     \retval  SW_PFIN_SUCCESS normally. The only possibility of error is due to
              lack of memory.

     This call allows a PFIN module to remove a previously-cached glyph, such
     that a subsequent attempt to plot it will cause the module to be called
     again, instead of fetching the glyph from the fontcache.

     The call can also be used, with a null glyphname, to discard a whole font
     from the cache, if the module knows it cannot be accessed again. This
     improves fontcache performance as useless glyphs can be discarded from
     memory early, rather than under low-memory conditions.
  */

  sw_pfin_result (RIPCALL *flush)(int32 uniqueid,
                                  /*@in@*/ const sw_datum *glyphname) ;

  /* End of entries present in SW_PFIN_API_VERSION_20090401: */

} sw_pfin_callback_api;


/** \brief  Enumeration of option bits affected by the option() call. */

enum {
  PFIN_OPTION_DYNAMIC = 1,    /*<< Glyph is dynamic and must not be cached */
  PFIN_OPTION_OPEN = 2,       /*<< Outlines should not be automatically closed */
  PFIN_OPTION_TRANSFORMED = 4, /*<< Bitimage has already been transformed */
  PFIN_OPTION_BITMAP_ORIENTATION_LANDSCAPE = 8,
  PFIN_OPTION_BITMAP_ORIENTATION_REVERSE = 16 /*<< Flags used to describe orientation of PCL bitmap glyphs */
} ;

/* -------------------------------------------------------------------------- */
/** \brief A structure containing a representation of a font.
 */
typedef struct sw_pfin_font {
  /* Entries present in SW_PFIN_API_VERSION_20071111: */
  sw_datum  font;    /**< The font directory. */
  double    dx, dy;  /**< Size of a device pixel in glyph coords. */
  /* End of entries present in SW_PFIN_API_VERSION_20071111 */
  /* Entries present in SW_PFIN_API_VERSION_20080808: */
  double    xdpi, ydpi; /**< The device resolution. Point size is derived like
                             this: ysize = 72*dy*ydpi; xsize = 72*dx*xdpi */
  int32     prefer; /**< Whether a bitimage is preferred, or disallowed */
  /* End of entries present in SW_PFIN_API_VERSION_20080808 */
  /* Entries present in SW_PFIN_API_VERSION_20090305 */
  double *  matrix;  /**< The font matrix from the font dictionary. */
  /* End of entries present in SW_PFIN_API_VERSION_20090305 */
} sw_pfin_font;

/** \brief An enumeration for the above sw_pfin_font to indicate whether an
    outline or bitimage glyph should be produced. Depending on font module
    capability and font format, the module may or may not be able to comply. */
enum {
  PFIN_BITIMAGE_PREFERRED,  /**< Glyph is sufficiently small and not doing CHARPATH */
  PFIN_OUTLINE_PREFERRED,  /**< Glyph is large enough for outline representation to be more efficient */
  PFIN_OUTLINE_REQUIRED  /**< CHARPATH mandates an outline. a bitimaged font should return INVALIDFONT */
} ;

/* -------------------------------------------------------------------------- */
/** \brief Type declaration of the PFIN implementation API. */
typedef struct sw_pfin_api sw_pfin_api ;

/** \brief A structure containing the context of a PFIN module instance call.
 *
 * This is created by PFIN when a PFIN instance is constructed, and is
 * thereafter used for all subsequent PFIN calls to and from the module until
 * the instance is destroyed.
 *
 * The instance structure may be subclassed to hold private data by defining
 * a subclass structure containing this structure as its first member, and
 * using the size of that structure as the implementation's instance size.
 * Individual methods may then downcast their instance pointer parameters to
 * subclass pointers, and use the private data. e.g.,
 *
 * \code
 * typedef struct my_instance {
 *   sw_pfin_instance super ; // must be first entry
 *   struct my_data *dynamic ;
 *   int32 other_fields ;
 * } my_instance ;
 *
 * static sw_pfin_result my_stop(sw_pfin_instance *inst,
 *                               sw_pfin_define_context *context,
 *                               sw_pfin_reason reason)
 * {
 *   my_instance *myinst = (my_instance *)inst ; // downcast to subclass
 *   // free allocated data, if necessary:
 *   inst->mem->implementation->free(inst->mem, myinst->dynamic) ;
 * } ;
 *
 * const static sw_pfin_api my_impl = {
 *   {
 *     SW_PFIN_API_VERSION_20071111,
 *     (const uint8 *)"myname",
 *     (const uint8 *)("A long description of my module implementation"
 *                     "Copyright (C) 2007 Global Graphics Software Ltd."),
 *     sizeof(my_instance), // RIP will allocate this amount for instance
 *   },
 *   // ...more of sw_pfin_api definition...
 *   my_stop,
 *   // ...rest of sw_pfin_api definition...
 * } ;
 *
 * // Call SwRegisterPFIN(&my_impl) after SwInit() to register this module.
 * \endcode
 *
 * The RIP will not touch memory beyond the size of the instance structure
 * for the implementation version registered.
 */
typedef struct sw_pfin_instance {
  /* Entries present in SW_PFIN_API_VERSION_20071111: */
  /** \brief Pointer to the API implementation.

       API methods for a blob map should always be called by indirecting
       through the map's implementation field.
  */
  const sw_pfin_api *implementation;

  /** \brief PFIN callback API to the RIP. */
  const sw_pfin_callback_api *callbacks ;

  /** \brief A memory allocator instance.

       This object is supplied by the RIP so that the PFIN implementation can
       allocate memory using the RIP's memory allocator. PFIN implementations
       should use this in preference to malloc() and free(), so that the RIP
       can track memory allocation and respond to low memory states more
       effectively. This memory allocator should only be used for allocations
       local to the module instance. Allocations will be automatically
       destroyed when the sw_pfin_api::stop() method is called with
       SW_PFIN_REASON_STOP.
  */
  sw_memory_instance *mem;

  /** \brief A blob factory instance.

      This factory should be be used to create blob instances when the PFIN
      module wishes to access font resource data on file systems managed by
      the RIP.
  */
  const sw_blob_factory_instance *blob_factory;

  /** A data API implementation for accessing sw_datum objects for this PFIN
      instance. */
  const sw_data_api* data_api;

  /* End of entries present in SW_PFIN_API_VERSION_20071111 */
} sw_pfin_instance ;

/* -------------------------------------------------------------------------- */
/** \brief Reason codes for PFIN's sw_pfin_api::start() and
    sw_pfin_api::stop() methods. */
enum {
  /* start() reasons present in SW_PFIN_API_VERSION_20071111: */
  SW_PFIN_REASON_START = 0, /**< Started up with no retained data. */
  SW_PFIN_REASON_RESUME,    /**< Resuming from suspend with retained data. */
  /* start() reasons present in SW_PFIN_API_VERSION_20071111 */

  /* stop() reasons present in SW_PFIN_API_VERSION_20071111: */
  SW_PFIN_REASON_STOP = 0,  /**< Stopping module, so release all resources. */
  SW_PFIN_REASON_SUSPEND    /**< Suspending module, it may retain data. */
  /* stop() reasons present in SW_PFIN_API_VERSION_20071111 */
};

/** \brief Type for PFIN reason values. */
typedef int sw_pfin_reason ;

/* -------------------------------------------------------------------------- */
/** \brief Collection structure for initialisation parameters. */
typedef struct sw_pfin_init_params {
  /* Entries present in SW_PFIN_API_VERSION_20071111: */

  /** \brief Global memory allocator.

      The memory allocators provided in the instances should only be used for
      allocations local to that instance. This memory allocator can be used
      for allocations which must survive individual instance lifetimes. */
  sw_memory_instance *mem ;

  /* End of entries present in SW_PFIN_API_VERSION_20071111 */
} sw_pfin_init_params ;


/* -------------------------------------------------------------------------- */
/** \brief The definition of an implementation of the PFIN
 * interface.
 *
 * PFIN guarantees to call \p init() before any other calls, and \p finish()
 * after all other calls. It may be that PFIN starts and stops the instance
 * contextually, or the instance may be active for the lifetime of the RIP.
 * This is not defined.
 */
struct sw_pfin_api {
  sw_api_info info; /**< Version number, name, display name, instance size. */

  /* Entries present in SW_PFIN_API_VERSION_20071111: */

  /** \brief Initialise any implementation-specific data.

      This method is called when the RIP is booted up. It will be the first
      call that the implementation receives.

      This call is optional (indicated by NULL).

      \param[in] impl The registered PFIN implementation to be initialised.

      \param[in] params A structure containing callback APIs and parameters
      valid for the lifetime of the module. Any parameters that the
      implementation needs access to should be copied out of this structure
      into private storage for the registered implementation.

      \retval TRUE Success, indicating that the implementation is fully
      initialised.

      \retval FALSE Failure to initialise the implementation. If this is
      returned, the implementation will not be finalised, and the RIP will
      terminate.
   */

  HqBool (RIPCALL *init)(/*@in@*/ /*@notnull@*/ sw_pfin_api* impl,
                         /*@in@*/ /*@notnull@*/ const sw_pfin_init_params *params);


  /** \brief This method is called when the RIP is shutting down, after all
      calls to the implementation or its instances.

      The module instances should not access any data owned by the RIP after
      this call, nor should they call any implementation or RIP callback API
      methods after this call. This method is optional.

      \param[in] impl A registered PFIN implementation to finalise.
  */
  void (RIPCALL *finish)(/*@in@*/ /*@notnull@*/ sw_pfin_api* impl);


  /** \brief Start using an instance of the font module, and define fonts.

      This call is optional (indicated by a null) but is expected to be
      present.

      A module may choose not to define some or all of its fonts during \p
      start(); it may define fonts in response to an appropriate \p
      configure() call, or it may choose to defer definition until the last
      minute, via the \p find() call. This may be preferable in systems where
      RIP booting speed is more crucial than findfont performance, and would
      suit modules that provide very many fonts, or whose mechanism for font
      discovery has performance implications.

      \param[in,out] pfin An instance of the sw_pfin_instance structure to
      complete. The RIP will allocate a structure of the size presented in
      the implementation's sw_pfin_api::info.instance_size field, fill in the
      implementation and callback API pointers, and then pass it to this
      routine. If the module needs to allocate some workspace per-instance,
      it may sub-class the instance to allocate extra private workspace by
      initialising the implementation's sw_pfin_api::info.instance_size
      larger than the size of the sw_pfin_instance structure, then
      downcasting the instance pointer in method calls.

      The PFIN instance contains the API implementation pointer and selected
      additional APIs which the module should use to call RIP methods.

      \param[in] define_context A context allowing PFIN to call the
      sw_pfin_callback_api::define() method.

      \param[in] reason This will be either SW_PFIN_REASON_START or
      SW_PFIN_REASON_RESUME. If the reason is SW_PFIN_REASON_START, the
      module must initialise all data in the instance and its subclasses. If
      the reason is SW_PFIN_REASON_RESUME, the instance will have previously
      been suspended by calling \p stop() with reason code
      SW_PFIN_REASON_SUSPEND, and may have retained data.

      \return The module should return SW_PFIN_SUCCESS if it is to be active.
      A return of SW_PFIN_ERROR_MEMORY may cause PFIN to free resources and
      try again, or it may be fatal. Any other error is guaranteed to be
      fatal, and no other module entry points will be called, including \p
      stop().
   */
  sw_pfin_result (RIPCALL *start)(/*@in@*/ /*@notnull@*/ sw_pfin_instance *pfin,
                                  /*@in@*/ /*@notnull@*/ sw_pfin_define_context *define_context,
                                  sw_pfin_reason reason);


  /** \brief Close down the font module, close files and undefine fonts.

      This call is optional (indicated by a NULL) but is expected to be
      present if \p start() is. If not present, PFIN will attempt to free the
      module's resources - closing any files (blobs) the module opened,
      freeing memory, and undefining its fonts. Thus if this is all that is
      required, \p stop() may be omitted.

      \param[in] pfin  The PFIN instance.

      \param[in] define_context A context allowing PFIN to call the
      sw_pfin_callback_api::undefine() method.

      \param[in] reason This will be either SW_PFIN_REASON_STOP or
      SW_PFIN_REASON_SUSPEND. If the reason is SW_PFIN_REASON_QUIT, the
      module must make no further reference to the instance or any data
      allocated through the PFIN instance's memory allocator pointer. If the
      reason is SW_PFIN_REASON_SUSPEND, the implementation should free up
      resources, but may retain data in the instance subclass. After
      suspension, the only methods which will be called are either \p start()
      (with a reason code of SW_PFIN_REASON_RESUME) or \p stop() (with a
      reason code of SW_PFIN_REASON_STOP).

      \return  The module should normally return SW_PFIN_SUCCESS. Any error
      return is unlikely to make any difference to RIP shutdown, but may make
      reinitialisation within one RIP lifetime impossible.
  */
  sw_pfin_result (RIPCALL *stop)(/*@in@*/ /*@notnull@*/ sw_pfin_instance* pfin,
                                 /*@in@*/ /*@notnull@*/ sw_pfin_define_context *define_context,
                                 sw_pfin_reason reason);


  /** \brief This call allows the module to respond to changes in
      configuration.

      This call is optional and may be NULL if the module has no configuration
      options.

      Whenever setpfinparams is invoked, all active PFIN modules with a \p
      configure() entry will be called with the value of their configuration
      key. A module called "SynthFonts" will be called with the value of the
      /SynthFonts key in the dictionary supplied to setpfinparams. If this
      key is not present, the \p configure() method is not called.

      The module may respond to its configuration by defining or undefining
      fonts through the sw_pfin_callback_api::define() and
      sw_pfin_callback_api::undefine() PFIN callback api calls.

      \param[in] pfin  The PFIN instance.

      \param[in] define_context A context allowing the module to call the
      sw_pfin_callback_api::define() and sw_pfin_callback_api::undefine()
      methods.

      \param[in] config The configuration datum. This is expected to be a
      dictionary, but modules should rigorously check the type of the datum
      since this is a user-configurable parameter. Attempting to match or
      extract values from non-dictionary parameters using the sw_data_api
      methods will return appropriate errors, so the module can rely on
      checking the error return values from those calls.

      \return Any errors in configuration can be rejected by returning
      SW_PFIN_ERROR_INVALID, otherwise SW_PFIN_SUCCESS denotes a lack of
      error. Modules are encouraged to be as forgiving and adaptive as
      possible with user-generated configuration. Nonsensical configuration
      should be ignored or defaulted, with a warning if necessary, rather
      than automatically generating an error.

      For example, if a configuration file contains "/SynthFonts mark" then a
      datum of type SW_DATUM_TYPE_INVALID will be delivered to the SynthFonts
      module, and it should act as it sees fit.
  */
  sw_pfin_result (RIPCALL *configure)(/*@in@*/ /*@notnull@*/ sw_pfin_instance * pfin,
                                      /*@in@*/ /*@notnull@*/ sw_pfin_define_context *define_context,
                                      /*@in@*/ const sw_datum* config);


  /** \brief  A last-minute chance for the module to define required fonts.

      This call is optional and should be NULL if the module defines all its
      fonts during \p start() or \p configure() calls.

      If a /Font or /CIDFont findresource call was unable to find the
      required resource (or if resourceforall is about to be executed), all
      PFIN modules are called with the name of the missing resource (or a
      null datum) at which point a module may choose to define the missing
      font (or all fonts it can supply) by calling \p define().

      In the case of findresource, if any module does call \p define() then
      the findresource will be tried again. If that still fails, the
      appropriate error is generated.

      \param[in] pfin  The PFIN context.

      \param[in] define_context A context allowing PFIN to call the
      sw_pfin_callback_api::define() method.

      \param[in] fontname If this is a null datum then a resourceforall is
      about to occur, so the module should define all fonts it had not
      already defined. Otherwise fontname is a string datum containing a font
      (or CIDFont) name, and the module should try to define just that one
      font, if it can.

      \return The module should return SW_PFIN_SUCCESS whether it can supply
      the missing font or not. PFIN detects the module's ability to supply
      the missing font by the fact the module called define. Returning
      SW_PFIN_ERROR_MEMORY may allow PFIN to free memory and try again, but
      will not have any other adverse effect. Returning any other error will
      cause the findresource or resourceforall to fail, and may cause the
      module to be shut down.
  */
  sw_pfin_result (RIPCALL *find)(/*@in@*/ /*@notnull@*/ sw_pfin_instance* pfin,
                                 /*@in@*/ /*@notnull@*/ sw_pfin_define_context *define_context,
                                 /*@in@*/ const sw_datum* fontname);


  /** \brief  Return the metrics for a given character and font.

      This call is mandatory.

      \param[in] pfin  The PFIN instance.

      \param[in] font  The PFIN font. This contains the font dictionary as
      a datum, and the size of a device pixel in glyph coordinates.

      \param[in] glyph  The required glyph. Depending on font type this will
      be delivered as an integer gid or a glyph name string.

      \param[in] metrics The array into which to put the escapement vector of
      the glyph.

      \return The module should return SW_PFIN_ERROR_UNKNOWN if it is unable
      to find the glyph in the font. This may result in a notdef glyph being
      used. If the module believes the font to be broken in some way it must
      return SW_PFIN_ERROR_INVALID. This may result in PFIN passing the font
      to another module, or may result in an INVALIDFONT error. However, if
      the module is unable to handle the font due to support limitations, due
      to an unusual but not illegal font structure or unrecognised data
      representation, it should return SW_PFIN_ERROR_UNSUPPORTED. This will
      probably result in the font being passed to another module. Returning
      SW_PFIN_ERROR_MEMORY may allow PFIN to free memory and try again. If
      the metrics were succesfully found, SW_PFIN_SUCCESS must be returned.

      It may be advantageous to cache glyph information at this point as a call
      to metrics will often be followed by a call to outline or raster.
   */
  sw_pfin_result (RIPCALL *metrics)(/*@in@*/ /*@notnull@*/ sw_pfin_instance* pfin,
                                    /*@in@*/ /*@notnull@*/ const sw_pfin_font* font,
                                    /*@in@*/ /*@notnull@*/ const sw_datum* glyph,
                                    /*@in@*/ /*@notnull@*/ double metrics[2]);


  /** \brief  Return a representation of a given glyph.

      This call is mandatory.

      From version 20080808 the module can choose to return a bitimaged or
      outline version of the glyph. Prior versions only support outlined
      glyphs, and this call's name still reflects that.

      The module finds the glyph definition, scales and hints the outline
      appropriately, then delivers it through the sw_pfin_callback_api using
      the outline context supplied.

      \param[in] pfin  The PFIN instance.

      \param[in] context  A context allowing PFIN to call the
      sw_pfin_callback_api::move(), sw_pfin_callback_api::line(), and
      sw_pfin_callback_api::curve() methods or, from version 20080808, the
      sw_pfin_callback_api::bitimage() method.

      \param[in] font  The PFIN font. This contains the font dictionary as
      a datum, and the size of a device pixel in glyph coordinates. From version
      20080808 this also contains the device resolution and a hint as to the
      preference for an outline or bitimage.

      \param[in] glyph  The required glyph. Depending on font type this will
      be delivered as an integer gid or a glyph name string.

      \return SW_PFIN_SUCCESS if the outline was delivered;
      SW_PFIN_ERROR_UNKNOWN if the glyph cannot be found;
      SW_PFIN_ERROR_MEMORY if there is a lack of memory;
      SW_PFIN_ERROR_UNSUPPORTED if the module cannot handle the font;
      SW_PFIN_ERROR_INVALID if the module believes the font to be broken.

      If context->prefer == PFIN_OUTLINE_REQUIRED, then only move(), line() and
      curve() methods may be used, otherwise the module can opt instead for a
      single call to bitimage(). Modules are encouraged to pay attention to
      context->prefer
  */
  sw_pfin_result (RIPCALL *outline)(/*@in@*/ /*@notnull@*/ sw_pfin_instance* pfin,
                                    /*@in@*/ /*@notnull@*/ sw_pfin_outline_context *context,
                                    /*@in@*/ /*@notnull@*/ const sw_pfin_font* font,
                                    /*@in@*/ /*@notnull@*/ const sw_datum* glyph);


  /* End of entries present in SW_PFIN_API_VERSION_20071111 */

  /* Entries present in SW_PFIN_API_VERSION_20071231: */

  /** \brief This call allows the module to perform implementation-specific
      operations, including communication with the PostScript environment.

      This call is optional.

      This can be invoked through the PostScript operator pfinop, with the
      syntax:

\code
        /ModuleName <single parameter> pfinop -> <optional result>
\endcode

      It is expected that the parameter will usually be an array or dictionary.
      The result optionally returned by the module can be of any form, though
      booleans and arrays will be useful.

      The module may respond to the operation by defining or undefining
      fonts through the sw_pfin_callback_api::define() and
      sw_pfin_callback_api::undefine() PFIN callback api calls, though this
      isn't its primary purpose.

      \param[in] pfin  The PFIN instance.

      \param[in] define_context A context allowing the module to call the
      sw_pfin_callback_api::define() and sw_pfin_callback_api::undefine()
      methods.

      \param[in,out] param A pointer to the pfinop parameter. This pointer
      should be updated by the module if it wants to return a result - leaving
      it unchanged is equivalent to setting it to null, resulting in pfinop
      leaving nothing on the stack.

      Note that any sw_datum pointed to by params must continue to exist, so do
      not use a local sw_datum on the C stack - a sw_datum in the module's
      workspace or a static is necessary. Complex results such as arrays, if not
      static, will have to be allocated as required and must be discarded or
      reused at the next miscop().

      \return If the module does not understand or cannot process the request,
      an error of SW_PFIN_ERROR_UNSUPPORTED or SW_PFIN_ERROR_SYNTAX should be
      returned as appropriate, otherwise SW_PFIN_SUCCESS denotes success, and
      the result param will be passed back to the caller.
  */
  sw_pfin_result (RIPCALL *miscop)(/*@in@*/ /*@notnull@*/ sw_pfin_instance * pfin,
                                   /*@in@*/ /*@notnull@*/ sw_pfin_define_context *define_context,
                                   /*@notnull@*/ sw_datum** param);

  /* End of entries present in SW_PFIN_API_VERSION_20071231 */

  /* Entries present in SW_PFIN_API_VERSION_20080808: */

  /** \brief  Return raster data for a bitimage glyph.

      This call is optional.

      This is called if the module uses the bitimage() method in reponse to a
      request for a glyph (instead of building an outline) AND the bitimage
      isn't delivered whole at the time of that call.

      Under those circumstances, PFIN will call back into the module from within
      the bitimage() call to retrieve raster data. It will be called repeatedly
      until it has returned enough data, or fails to return any (at which point
      the rest of the bitimage will be blank).

      \param[in] pfin  The PFIN instance. Note that the module can use this to
                       pass information from the context of the outline() call.

      \param[in] data  A sw_datum string to be updated by the module. The
                       string will usually be zero length and have a null
                       buffer pointer. The module should update the length and
                       pointer fields in this case. However, PFIN may allocate
                       a buffer into which the module may rasterise - see below.

      \return  SW_PFIN_SUCCESS normally. Anything else will stop further raster
               callbacks occuring.

      This callback is used by PFIN from within a bitimage() call if no buffer
      or insufficient data was supplied to that call.

      It will usually be called with a zero-length data string, and the module
      must allocate a buffer and update the string datum accordingly. Note that
      any amount of data can be returned by this callback - it will be issued
      repeatedly until enough raster data has been returned, or the callback
      returns an empty string. The allocated buffer can be reused or discarded
      the next time control returns to the module - either at the next raster()
      call or on return from the surrounding bitimage() call. It is the module's
      responsibility to free such a buffer.

      Under certain circumstances PFIN may preallocate a buffer, and the data
      string will then have a valid string pointer and a length. The module is
      encouraged to rasterise directly into this buffer. However, the module can
      just take the above approach in this case too, and override the string
      datum with its own buffer. If the module does this, and does not supply
      all the raster data immediately, further calls will occur as above (each
      with a null data string). Note that the module must never try to discard
      a buffer supplied by PFIN - to do so would generate an error.

      Returning with an empty data string will blank the rest of the bitimage
      and cause no further raster() callbacks to occur for this bitimage.
  */
  sw_pfin_result (RIPCALL *raster)(/*@in@*/ /*@notnull@*/ sw_pfin_instance* pfin,
                                   /*@in@*/ /*@notnull@*/ sw_datum * data);

  /* End of entries present in SW_PFIN_API_VERSION_20080808 */
} ;

/* -------------------------------------------------------------------------- */
/** \brief  Enumeration of miscop array reason calls
 *
 * Although miscops are directed to a named module, it may help implementational
 * flexibility if common parametric structures are used. These reason codes
 * may be used as the sole parameter, or as the first element of an array, or
 * as the value of a /Reason key in a dictionary, so allowing certain operations
 * to be implemented by multiple modules. The module chosen to perform such an
 * operation must be selected by some configuration.
 *
 * These are base numbers - each has a range of 65536 subreasons which will be
 * enumerated elsewhere, eg:
 *
 * \code
 * enum {
 *   PCL_FONT_SPECIFY = SW_PFIN_MISCOP_PCL,
 *   PCL_FONT_ID, PCL_DEFINE_FONT, PCL_DEFINE_CHARACTER
 * ...
 * \endcode
 */

enum {
  SW_PFIN_MISCOP_PCL = 1 << 16        /* base number for PCL operations */
};

/* -------------------------------------------------------------------------- */
/** \brief This routine makes a font module implementation known to the rip.
 *
 * It can be called any number of times with different implementations of the
 * PFIN API. It is likely that each implementation registered will have
 * only one instance constructed at a time.
 *
 * The implementation name is limited to 127 characters. This name will be
 * used to identify the font module and its fonts, and will be used as the
 * pfinparams configuration key for this module. As such, the names "PFIN",
 * "FontType" and "Exceptions" are illegal. The name "SW" is also reserved.
 *
 * \param[in] implementation The API implementation to register. This pointer
 * will be returned through the sw_pfin_api::init() and sw_pfin_api::finish()
 * calls, and also will be in the implementation member field of every
 * instance created, so the pointer can be in dynamically allocated memory.
 * Implementations may be subclassed to hold class-specific private data by
 * defining a subclass structure containing the sw_pfin_api structure as its
 * first member. Individual methods may then downcast their implementation
 * pointers to subclass pointers, and use those to get at the class data.
 * e.g.,
 *
 * \code
 * typedef struct my_implementation {
 *   sw_pfin_api super ; // must be first entry
 *   sw_memory_instance *mem ;
 *   struct my_class_data *dynamic ;
 * } my_implementation ;
 *
 * static HqBool RIPCALL my_init(sw_pfin_api *impl, const sw_pfin_init_params *params)
 * {
 *   my_implementation *myimpl = (my_implementation *)impl ; // downcast to subclass
 *   // save global memory allocator:
 *   myimpl->mem = params->mem ;
 *   myimpl->dynamic = NULL ;
 *   return TRUE ;
 * } ;
 *
 * static sw_pfin_result (RIPCALL *find)(sw_pfin_instance* pfin,
 *                                       sw_pfin_define_context *define_context,
 *                                       const sw_datum* fontname)
 * {
 *   my_implementation *myimpl = (my_implementation *)pfin->implementation ; // downcast to subclass
 *   myimpl->dynamic = myimpl->mem->implementation->alloc(myimpl->mem, 1024) ;
 * } ;
 *
 * const static my_implementation module = {
 *   { // sw_pfin_api initialisation
 *   },
 *   NULL, // global memory allocator
 *   NULL  // global allocations
 * } ;
 *
 * // Call SwRegisterPFIN(&module.super) after SwInit() to register this module.
 * \endcode
 */
sw_api_result RIPCALL SwRegisterPFIN(/*@in@*/ /*@notnull@*/ sw_pfin_api* implementation);

typedef sw_api_result (RIPCALL *SwRegisterPFIN_fp_t)(sw_pfin_api* implementation);

/* -------------------------------------------------------------------------- */
/* PFIN miscop exposure low-level calls require an opaque sw_pfin */

typedef struct sw_pfin sw_pfin ;

/** \brief Find a PFIN module by name.

    \param[in] theo A Name, String or Array containing a name or string as its
    first element.

    \return An opaque pointer to the PFIN module, to pass to calls such as
    pfin_miscop(), or NULL if the module is not recognised.
*/

sw_pfin* pfin_findpfin(struct OBJECT * theo) ;

/** \brief Send a miscop call to a specific PFIN module.

    This call allows a PFIN module to reply or respond to an enquiry or
    signal. As it is sent directly to a named module, the arguments and
    protocol are module-specific.

    \param[in] pfin Pointer to a PFIN module as returned by pfin_findpfin().

    \param[in,out] pparam A pointer to a pointer to a sw_datum parameter.
    The double indirection allows the PFIN module to return a different
    sw_datum as a response, or NULL.

    It is expected that parameters will normally be represented as an
    array with a reason code as the first member, but the interface is
    fully polymorphic and module-specific. See individual PFIN module
    documentation for further details.
*/

sw_pfin_result pfin_miscop(sw_pfin * pfin, sw_datum ** pparam) ;

/** \brief  Convert a PFIN return code into a PS error code.

    \param[in] error  PFIN return code.

    \return  Error code to be passed to errorhandler().
*/

int32 pfin_error(sw_pfin_result error) ;

#ifdef __cplusplus
}
#endif

/** \} */ /* end Doxygen grouping */


#endif /* __SWPFINAPI_H__ */
