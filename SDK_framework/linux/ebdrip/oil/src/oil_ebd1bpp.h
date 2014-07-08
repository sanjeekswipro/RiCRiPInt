/* Copyright (C) 2007-2009 Global Graphics Software Ltd. All rights reserved.
 *
 * This example is provided on an "as is" basis and without
 * warranty of any kind. Global Graphics Software Ltd. does not
 * warrant or make any representations regarding the use or results
 * of use of this example.
 *
 * $HopeName: SWebd_OIL_example_gg!src:oil_ebd1bpp.h(EBDSDK_P.1) $
 * 
 */
/*! \file
 *  \ingroup OIL
 *  Header file for the screening device.
 *
 */

#ifndef _OIL_SCRN_H_
#define _OIL_SCRN_H_

/*
 * Routine for opening an embedded screen table.
 */
DEVICE_FILEDESCRIPTOR RIPCALL ebd_scrn_open (unsigned char *filename);

/*
 * Routine for closing an embedded screen table.
 */
int RIPCALL ebd_scrn_close (int nScreen);

/*
 * Routine for reading an embedded screen table.
 */
int RIPCALL ebd_scrn_read (int nScreen,
                           unsigned char *buff ,
                           int len);

/*
 * Routine for seeking an embedded screen table.
 */
int RIPCALL ebd_scrn_seek  (int nScreen,
                            Hq32x2 *destination ,
                            int whence);

/*
 * Routine to initialize embedded screen tables.
 */
void ebd_scrn_init();

/*
 * Routine to get active screen's table width.
 */
int Get1bppScreenWidth(char *ActiveScreen);

/*
 * Routine to get active screen's table height.
 */
int Get1bppScreenHeight(char *ActiveScreen);

/*
 * Routine to get the active screen's PostScript language filename.
 */
void Get1bppScreenFileName(char *ScreenFile, char *ActiveScreen);

#endif /* _OIL_SCRN_H_ */

