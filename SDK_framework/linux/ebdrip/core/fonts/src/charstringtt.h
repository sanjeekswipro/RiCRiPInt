/** \file
 * \ingroup fonts
 *
 * $HopeName: COREfonts!src:charstringtt.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * TrueType charstring methods. This is one of several definitions for
 * charstring_methods_t. This one defines the callbacks the TrueType
 * charstring interpreters use to get at data from a font definition.
 * Different TrueType fonts (CIDFontType 2, Type 42) will provide different
 * methods to get at the data.
 */

#ifndef __CHARSTRINGTT_H__
#define __CHARSTRINGTT_H__

struct charstring_methods_t {
  /* A private data pointer, passed to the methods below. Be careful with this;
     if the charstring methods are allocated statically, they will need to be
     saved and restored around recursive plotchar calls, or this pointer
     may be changed unintentionally. */
  void *data ;

  /* open_frame is used to get access to a block of information from the
     sfnts data. If it succeeds, close_frame MUST be called with the same
     frame parameter. Multiple frames may be open at once; I believe they
     will be opened and closed in strict stack order, but have not checked
     that the Bitstream interpreter obeys this convention. */
  uint8 *(*open_frame)(void *data, uint32 offset, uint32 length) ;
  void (*close_frame)(void *data, uint8 **frame) ;
} ;

/*
Log stripped */
#endif /* protection for multiple inclusion */
