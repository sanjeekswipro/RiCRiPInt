/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_pgb!swraster.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * HHR raster delivery control interface.
 */

#ifndef __SWRASTER_H__
#define __SWRASTER_H__


/** \brief Read/write structure used during the
   DeviceIOCtl_RasterRequirements and DeviceIOCtl_RenderingStart IOCTL
   calls. */

typedef struct _raster_requirements {
  int32  eraseno; /**< Page generation ID. */
  uint32 page_width; /**< Page width in pixels. */
  uint32 page_height; /**< Page height in pixels. */
  uint32 bit_depth; /**< \brief Bit depth. */
  uint32 components; /**< Number of color components overall. */
  uint32 components_per_band; /**< Number of color components present in each band. */
  uint32 nChannels; /**< Number of color channels. */
  uint32 minimum_bands; /**< The minimum number of raster destination bands
                             that the RIP requires. */
  uint32 band_width; /**< Width of each band. */
  uint32 band_height; /**< Height of each band in lines. */
  uint32 line_bytes; /**< Number of bytes that the RIP will write for each line
                          of a band. */
  int32 have_framebuffer; /**< Set to TRUE by \c RasterRequirements if it has
                               (or is able to promise by the time \c
                               DeviceIOCtl_GetBufferForRaster is called) a
                               complete framebuffer. */
  int32 interleavingStyle; /**< Interleaving style. */

  size_t band_size; /**< The size of the entire band buffer. */
  size_t scratch_size; /**< Set by \c RasterRequirements to the size of
                            a scratch buffer requested from the core. */
  void *scratch_band; /**< Scratch buffer handed by the core to \c RenderingStart. */

  int32 handled; /**< Flag which is set to FALSE by the caller, and set to TRUE
                      by the callee if it has completed. If not, the caller will
                      keep calling with the same data until either this flag is
                      set to TRUE or the callee returns an error. */
} RASTER_REQUIREMENTS;


/** \brief Structure whose values can be set by the raster consumer
    during a DeviceIOCtl_GetBufferForRaster IOCTL call to provide
    memory into which the RIP will render directly. */

typedef struct _raster_destination {
  int32 bandnum;                       /**< \brief Band index. */
  uint8 *memory_base;                  /**< \brief Pointer to the base
                                         memory location for the RIP
                                         to render into. */
  uint8 *memory_ceiling;               /**< \brief Limit on the memory
                                         range into which the RIP can
                                         render, i.e. pointer to the
                                         first memory location the RIP
                                         is not allowed to
                                         modify. Note that this value
                                         may be smaller than \c
                                         memory_base in the case of a
                                         negative line stride. */
  int32 handled;                      /**< \brief Flag which is set to
                                         FALSE by the caller, and set
                                         to TRUE by the callee if it
                                         has done everything it needs
                                         to do. If not, the caller
                                         will keep calling with the
                                         same data until either this
                                         flag is set to TRUE or the
                                         callee returns an error. */
} RASTER_DESTINATION;


/* N.B. The IOctls are not in the public API, but kept here for convenience. */
/** Opcode for \c DEVICELIST_IOCTL(). Arg is a pointer to \c
    RASTER_REQUIREMENTS. N.B. This ioctl is called from the interpreter, not
    serialized wrt. to the rest of the PGB calls. */
#define DeviceIOCtl_RasterRequirements 103
/** Opcode for \c DEVICELIST_IOCTL(). Arg is a pointer to \c RASTER_REQUIREMENTS. */
#define DeviceIOCtl_RenderingStart     104
/** Opcode for \c DEVICELIST_IOCTL(). Arg is a pointer to \c RASTER_DESTINATION. */
#define DeviceIOCtl_GetBufferForRaster 105
/** Opcode for \c DEVICELIST_IOCTL(). Arg is a pointer to \c uint32. N.B. This
    ioctl is called from the interpreter, not serialized wrt. to the rest of the
    PGB calls. */
#define DeviceIOCtl_RasterStride       106


#endif /* __SWRASTER_H__ */

