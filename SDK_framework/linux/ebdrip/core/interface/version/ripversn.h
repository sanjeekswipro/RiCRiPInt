/** \file
 * \ingroup interface
 *
 * Copyright (C) 1995-2007 Global Graphics Software Ltd. All rights reserved.
 * This source code contains the confidential and trade secret information of
 * Global Graphics Software Ltd. It may not be used, copied or distributed for any
 * reason except as set forth in the applicable Global Graphics license agreement.
 *
 * $HopeName: COREinterface_version!ripversn.h(EBDSDK_P.1) $
 */
#ifndef __RIPVERSN_H__
#define __RIPVERSN_H__

#ifdef never
/* WARNING!!  THIS FILE IS RUN THROUGH CPP ON THE MAC TO MAKE POSTSCRIPT.
 * ALL COMMENTS MUST BE #IFED OUT -- except when on cpp lines, it seems.
 */

#endif

#ifdef never
/* Want to canonicalise Doxygen header, but there seem to be some problem with
 * comments unless they are #IFED out. So do nothing until I can investigate
 * further.
 */
#endif

#ifdef never
/******************************************************************************
* To change the version number use the following section as a template.       *
* Because of limitations in the preprocessors (eg they can add whitespace) we *
* cant use them to combine the different components of the version number.    *
* You should copy the template and replace the sections in angle brackets as  *
* described below. Please ensure you do not introduce any extra whitespace    *
* at the end of lines, or between the #defines and the macro names.           *
*                                                                             *
* The fields that need replacing are:                                         *
*                                                                             *
* <Major>       The GUI product major version number.                         *
*                                                                             *
* <RIPMajor>    The RIP major version number.                                 *
*               This is different from the above for historical reasons.      *
*                                                                             *
* <Minor>       The GUI product minor version number.                         *
*                                                                             *
* <Revision>    The GUI product revision number.                              *
*                                                                             *
* <RevLetter>   The GUI product revision letter (empty for first release).    *
*                                                                             *
* <RelDef> <RelSuffix>                                                        *
*               Two different versions of the GUI product release type.       *
*               You should choose the appropriate line in the table below and *
*               set each to the corresponding string (without quotes). The    *
*               leading spaces are significant.                               *
*                                                                             *
*               RelDef        RelSuffix                                       *
*                                                                             *
*               "DEVELOPMENT" " development"                                  *
*               "ALPHA"       " alpha"                                        *
*               "BETA"        " beta"                                         *
*               "PRE"         " pre-release"                                  *
*               "FINAL"       ""                                              *
*                                                                             *
* <PIMajor>     The plugin interface major version number                     *
*                                                                             *
* <PIMinor>     The plugin interface minor version number                     *
*                                                                             *
******************************************************************************/

/* ----- Cut below this line for start of template ----- */
#define PRODUCT_VERSION_MAJOR <Major>
#define PRODUCT_VERSION_MINOR <Minor>
#define PRODUCT_VERSION_REVISION <Revision>
#define PRODUCT_<RelDef>_RELEASE

#define PRODUCT_VERSION <Major>.<Minor> Revision <Revision><RevLetter><RelSuffix>
#define PRODUCT_VERSION_QUOTED "<Major>.<Minor> Revision <Revision><RevLetter><RelSuffix>"
#define SHORT_PRODUCT_VERSION_QUOTED "<Major>.<Minor>r<Revision><RevLetter><RelSuffix>"
#define RIP_VERSION <RIPMajor>.<Minor> Revision <Revision><RevLetter><RelSuffix>

#define PLUGIN_INTERFACE_MAJOR <PIMajor>
#define PLUGIN_INTERFACE_MINOR <PIMinor>
/* ----- Cut above this line for end of template ----- */

#endif


#ifdef never
/* Use of template starts here */
#endif
#define PRODUCT_VERSION_MAJOR 40
#define PRODUCT_VERSION_MINOR 0
#define PRODUCT_VERSION_REVISION 1
#define PRODUCT_FINAL_RELEASE

#define RIP_VERSION 40.0 Revision 1a

#ifdef HHR_PRODUCT
/* Harlequin Host Renderer */
#define PRODUCT_VERSION 4.0 Revision 1a
#define PRODUCT_VERSION_QUOTED "4.0 Revision 1a"
#define SHORT_PRODUCT_VERSION_QUOTED "4.0r1a"

#else

#ifdef EBD_PRODUCT
/* Harlequin Embedded */
#define PRODUCT_VERSION 4.0 Revision 1a Development
#define PRODUCT_VERSION_QUOTED "4.0 Revision 1a Development"
#define SHORT_PRODUCT_VERSION_QUOTED "4.0r1a Development"

#else
/* Harlequin MultiRip */
#define PRODUCT_VERSION 10.0 Revision 1a
#define PRODUCT_VERSION_QUOTED "10.0 Revision 1a"
#define SHORT_PRODUCT_VERSION_QUOTED "10.0r1a"

#endif
#endif


#define PLUGIN_INTERFACE_MAJOR 20
#define PLUGIN_INTERFACE_MINOR 1
#ifdef never
/* Use of template ends here */
#endif


#ifdef never
/* The following define the latest versions of the core module interfaces */
#endif

#ifdef never
/* leave ALL COMMENTS #ifed out - thanks */
#endif


#endif /* ! __RIPVERSN_H__ */
