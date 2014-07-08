/* Copyright (C) 2012 Global Graphics Software Ltd. All rights reserved. */
/* Global Graphics Software Ltd. Confidential Information. */
#ifndef __PRODUCT_H__
#define __PRODUCT_H__

/* $HopeName: SWprod_hqnrip!ebdrip:export:product.h(EBDSDK_P.1) $
 *
 * Each EP product should provide a product.h to act as a master include file
 * for that product. It should in turn include the minimal set necessary to
 * ensure that nested includes can be used, and pick up compilation switches.
 *
 * This means each compound that requires product specific
 * configuration can just include product.h first without having to
 * worry about which product it is being used in.
 */

/*
* Log stripped */

/* --------------------- Includes ------------------------------------------ */

#include "std.h"

/* v20iface */
#include "swvalues.h" /* As pic files need USERVALUE etc, but cant include */

/* Dongle selection */
#ifdef WIN32
#define SENTINEL
#endif

#ifdef	UNIX

#ifdef linux
/* Only have Sentinel SuperPro support for Linux */
#define SENTINEL
#else
/* And only MICROPHAR for all other UNIX platforms */
#define MICROPHAR
#endif

#endif


#ifdef	MACINTOSH
#define SENTINEL 1
#endif

/**
 * \cond FULL_SOURCE_DISTRIB
 * \file
 */

/**
 * \mainpage Harlequin RIP Full Source Code Distribution
 *
 * \section full_source_intro Introduction
 *
 * Welcome to the documentation pages for the full source distribution of the
 * Harlequin RIP.
 *
 * This page provides an overview of the Harlequin RIP architecture, along with
 * some diagrams that will help you navigate to places of specific interest.
 * We are continuing to improve both the quality and the quantity of
 * documentation for the source code. This is an ongoing process, and we
 * are always happy to hear feedback on areas that are in need of further
 * work.
 *
 * \section full_source_overview Overview
 *
 * When approaching the RIP source code for the first time, it is
 * important to understand the distinction between the
 * <em>core RIP</em> and the <em>skin</em>. The core RIP is the main
 * processing engine, responsible for all aspects of PDL interpretation
 * and rendering. However, it performs these functions in complete
 * isolation from its host environment: the core RIP knows nothing
 * about the disk files or streams that might provide its input, nor
 * about any output device to which it may be connected. Everything that
 * relates to connecting the core RIP with its host environment is the
 * responsibility of the skin, which is a separate layer of service
 * code. The skin layer would normally be implemented by an OEM
 * systems integrator. While there is only one core RIP, there could
 * be any number of different skins, depending on the host platform
 * or device.
 *
 * This distribution contains complete source code for the core RIP. It also
 * contains example source code for the skin layer. These skin examples
 * demonstrate some typical requirements of both host-based and
 * embedded RIP control. They are not intended for direct use in
 * products, but they can be used as templates for crafting a skin
 * layer suited to a specific environment.
 *
 * \section core_architecture Core Architecture
 *
 * The Core RIP employs a modular architecture, centered around a pipeline
 * dataflow model. At the front end of the pipeline are the scanners and parsers
 * for the various Page Description Languages (PDLs) that the RIP supports. These
 * are the modules that deal directly with input from the host application
 * or driver. From here, the input data undergoes successive transformations
 * within the pipeline. The internal representation takes the form of
 * two data models: the Graphics Model, which captures the current state
 * for marking the page, and the Display List Model, which describes the
 * collection of objects already on the page. At the back end of the pipeline,
 * the display list is ultimately rasterized. The raster data is then handled
 * through a buffering interface, where it can be received by the host application
 * or driver for downstream processing.
 *
 * The Core RIP is by no means a black box. Many key areas of functionality
 * are exposed via Application Programming Interfaces (APIs), allowing
 * developers to substitute their own, self-contained modules to perform
 * these functions. APIs are provided for the following functions:
 *
 * - Color Management
 * - Font Rendering
 * - Image Filtering
 * - Screening (Halftoning)
 * - XML Element Filtering (for XML-based PDLs)
 * - Output Data Management
 * 
 * The diagram below shows a schematic representation of the core RIP. Click
 * on the boxes to jump straight to the documentation for the corresponding
 * module.
 *
 * \htmlonly
 * <IMG SRC="./CoreArchitecture.JPG" USEMAP="#CoreArchMap">
 * <MAP NAME="CoreArchMap">
 *     <AREA SHAPE=CIRC COORDS="52, 106, 34"         HREF="group__ps.html">
 *     <AREA SHAPE=CIRC COORDS="52, 211,34"          HREF="group__pdf.html">
 *     <AREA SHAPE=CIRC COORDS="52, 315,34"          HREF="group__xps.html">
 *     <AREA SHAPE=CIRC COORDS="52, 523,34"          HREF="group__images.html">
 *     <AREA SHAPE=RECT COORDS="169, 46, 349, 81"    HREF="group__hdlt.html">
 *     <AREA SHAPE=RECT COORDS="169, 101, 349, 138"  HREF="group__imgfilter.html">
 *     <AREA SHAPE=RECT COORDS="169, 156, 349, 192"  HREF="group__fonts.html">
 *     <AREA SHAPE=RECT COORDS="169, 267, 349, 302"  HREF="group__dl.html">
 *     <AREA SHAPE=RECT COORDS="169, 323, 349, 357"  HREF="group__recombine.html">
 *     <AREA SHAPE=RECT COORDS="169, 376, 349, 412"  HREF="group__trapping.html">
 *     <AREA SHAPE=RECT COORDS="169, 433, 349, 467"  HREF="group__backdrop.html">
 *     <AREA SHAPE=RECT COORDS="169, 487, 349, 522"  HREF="group__color.html">
 *     <AREA SHAPE=RECT COORDS="169, 542, 349, 577"  HREF="group__scanconvert.html">
 *     <AREA SHAPE=RECT COORDS="169, 597, 349, 632"  HREF="structDeviceType.html">
 *     <AREA SHAPE=RECT COORDS="405, 83, 524, 99"    HREF="group__gstate.html">
 *     <AREA SHAPE=RECT COORDS="445, 130, 480, 147"  HREF="group__color.html">
 *     <AREA SHAPE=RECT COORDS="440, 160, 484, 177"  HREF="group__paths.html">
 *     <AREA SHAPE=RECT COORDS="397, 190, 529, 207"  HREF="group__backdrop.html">
 *     <AREA SHAPE=RECT COORDS="441, 220, 482, 232"  HREF="group__fonts.html">
 *     <AREA SHAPE=RECT COORDS="390, 343, 534, 560"  HREF="group__dl.html">
 *     <AREA SHAPE=RECT COORDS="571, 115, 661, 161"  HREF="group__swcmm.html">
 *     <AREA SHAPE=RECT COORDS="571, 207, 661, 254"  HREF="group__fonts.html">
 *     <AREA SHAPE=RECT COORDS="571, 298, 661, 345"  HREF="group__swflt.html">
 *     <AREA SHAPE=RECT COORDS="571, 393, 661, 439"  HREF="group__swhtm.html">
 *     <AREA SHAPE=RECT COORDS="571, 484, 661, 530"  HREF="group__libgenxml.html">
 *     <AREA SHAPE=RECT COORDS="571, 578, 661, 623"  HREF="structDeviceType.html">
 *     <AREA SHAPE=RECT COORDS="700, 115, 789, 161"  HREF="structDeviceType.html">
 *     <AREA SHAPE=RECT COORDS="700, 207, 789, 231"  HREF="group__fileio.html">
 *     <AREA SHAPE=RECT COORDS="700, 232, 789, 254"  HREF="group__filters.html">
 *     <AREA SHAPE=RECT COORDS="700, 298, 789, 345"  HREF="group__mm.html">
 *     <AREA SHAPE=RECT COORDS="700, 393, 789, 439"  HREF="group__zipdev.html">
 *     <AREA SHAPE=RECT COORDS="700, 484, 789, 530"  HREF="group__hqx.html">
 *     <AREA SHAPE=RECT COORDS="700, 578, 789, 623"  HREF="group__zipdev.html">
 * </MAP>
 * \endhtmlonly
 *
 * \section skin_architecture Skin Architecture
 *
 * The \ref skinkit "example skin" demonstrates how the core RIP can be integrated with
 * a host application or printer driver. One of the key abstractions that the
 * RIP employs to isolate itself from such details is the
 * \ref DeviceType "device interface". (In this context, the term "device" does
 * not refer to a printing device in the traditional sense. It is a more
 * abstract concept, representing a software module that provides
 * a specific set of I/O services for the RIP.) Devices are the essential
 * building blocks of the skin layer. The device interface is simple and
 * flexible, meaning that the skin can implement devices for a wide
 * range of services. Access to disk files is one obvious application, but
 * devices can also be implemented for access to sockets, external streaming
 * protocols, even just blocks of memory.
 *
 * The basis of the example skin is a collection of example devices, including
 * devices for files and sockets. There are also example implementations
 * of devices that are given special status by the RIP, such as
 * the config and pagebuffer devices.
 *
 * The skin also demonstrates some techniques for outputting raster data.
 * These output modules, along with a simple control API, allow the skin
 * to be built into a fully-functional command line application ("clrip").
 * Recent developments have built on this still further, adding a
 * \ref refimpl "minimal API for processing XPS documents", and a
 * functioning example of how the RIP can be integrated with
 * \ref xpsdrv "Microsoft's XPSDrv print driver architecture".
 *
 * The diagram below shows a schematic representation of the example RIP skin.
 * Click on the boxes to jump straight to the documentation for the corresponding
 * module.
 *
 * \htmlonly
 * <IMG SRC="./SkinArchitecture.JPG" USEMAP="#SkinArchMap">
 * <MAP NAME="SkinArchMap">
 *     <AREA SHAPE=RECT COORDS="354, 216, 471, 234"  HREF="group__skinkit.html">
 *     <AREA SHAPE=RECT COORDS="350, 176, 476, 195"  HREF="group__skintest.html">
 *     <AREA SHAPE=RECT COORDS="1, 69, 108, 115"     HREF="group__xpsdrv.html">
 *     <AREA SHAPE=RECT COORDS="1, 122, 108, 169"    HREF="skintest_8h.html">
 *     <AREA SHAPE=RECT COORDS="141, 532, 826, 565"  HREF="structDeviceType.html">
 *     <AREA SHAPE=RECT COORDS="154, 247, 409, 344"  HREF="skinkit_8h.html">
 *     <AREA SHAPE=RECT COORDS="154, 43, 409, 143"   HREF="group__refimpl.html">
 *     <AREA SHAPE=RECT COORDS="464, 68, 569, 115"   HREF="skintest_8h.html">
 *     <AREA SHAPE=RECT COORDS="649, 21, 699, 55"    HREF="tiffrast_8h.html">
 *     <AREA SHAPE=RECT COORDS="649, 62, 699, 97"    HREF="rawrast_8h.html">
 *     <AREA SHAPE=RECT COORDS="649, 103, 699, 136"  HREF="xpsrast_8h.html">
 *     <AREA SHAPE=RECT COORDS="649, 144, 699, 177"  HREF="oemraster_8h.html">
 *     <AREA SHAPE=RECT COORDS="187, 376, 248, 422"  HREF="config_8c.html">
 *     <AREA SHAPE=RECT COORDS="311, 376, 376, 422"  HREF="monitor_8c.html">
 *     <AREA SHAPE=RECT COORDS="425, 376, 511, 422"  HREF="pgbdev_8c.html">
 *     <AREA SHAPE=RECT COORDS="549, 376, 609, 422"  HREF="filedev_8c.html">
 *     <AREA SHAPE=RECT COORDS="645, 376, 707, 422"  HREF="sockdev_8c.html">
 *     <AREA SHAPE=RECT COORDS="740, 376, 801, 422"  HREF="ramdev_8c.html">
 * </MAP>
 * \endhtmlonly
 * 
 * \endcond
 */

/**
 * \brief Need a documented entity to prompt the import of some images to HTML output.
 *
 * \image html gg_logo.gif
 * \image html spacer.gif
 * \image html CoreArchitecture.jpg
 * \image html SkinArchitecture.jpg
 */
typedef struct { int a; } DOXYGEN_UTIL_DEF;

/**
 * \defgroup leskin_group Example RIP Skin
 */

/**
 * \defgroup core_api_example_group Core API Examples
 */

#endif /* ! __PRODUCT_H__ */

/* eof product.h */
