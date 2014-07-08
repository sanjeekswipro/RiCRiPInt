/* Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: GGEpms_gg_example!src:pms_engine_simulator.h(EBDSDK_P.1) $
 *
 */

/*! \file
 *  \ingroup PMS
 *  \brief Header file for Engine Simulator.
 *
 */

#ifndef _PMS_ENGINE_SIMULATOR_H_
#define _PMS_ENGINE_SIMULATOR_H_

extern PMS_TyJob g_tJob;

/*! \brief Frequency in milliseconds for checking if new page has been checked in by the RIP.*/
#define PAGECHECK_FREQUENCY     (100)         /* typicaly 100 milliseconds*/
/*! \brief To simulate Engine Warm-up delay in milliseconds.*/
#define ENGINE_WARMUP_DELAY     (1000L * 0)   /* milliseconds */
/*! \brief Engine Speed for processing Color Plain Paper job measured in pages per minute(ppm).*/
#define COLOR_PLAIN_PAPER_PPM   (20)          /* color plain ppm */
/*! \brief Engine Speed for processing Monochrome Plain Paper job measured in pages per minute(ppm).*/
#define MONO_PLAIN_PAPER_PPM    (40)          /* mono plain ppm */
/*! \brief To simulate time taken by input tray to pickup the paper - measured in milliseconds.*/
#define INPUT_TRAY1_DELAY       (100)         /* milliseconds */
/*! \brief To simulate time taken by output tray to throw-out the paper - measured in milliseconds.*/
#define OUTPUT_TRAY1_DELAY      (200)         /* milliseconds */
/*! \brief To simulate time taken by the paper to travel through the paperpath - measured in milliseconds.*/
#define PAPERPATH_DELAY         (1000L * 6)  /* milliseconds */
/*! \brief To simulate time allowed for the page to pass through before next page is picked up - measured in milliseconds.*/
#define INTERPAGE_GAP(x)        (60000L/x)    /* convert ppm into milliseconds */

void PMSOutput(void * dummy);
int EngineGetTravelTime(PMS_TyPage *pstPMSPage);
int EngineGetInterpageGap(PMS_TyPage *pstThisPage, PMS_TyPage *pstLastPage);
int EngineGetTrayInfo(void);
int EngineGetOutputInfo(void);
PMS_TyJob * EngineGetJobSettings(void);
void EngineSetJobDefaults(void);

#endif /* _PMS_ENGINE_SIMULATOR_H_ */
