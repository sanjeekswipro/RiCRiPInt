
/* Copyright (C) 2008-2011 Monotype Imaging Inc. All rights reserved. */

/* Confidential Information of Monotype Imaging Inc. */

/* adfimplicit.h */

/**
 *-----------------------------------------------------------------------------------
 *    Filename: ADFImplicit.h
 *-----------------------------------------------------------------------------------
 *-----------------------------------------------------------------------------------
 *    Copyright 2004-2007 Mitsubishi Electric Research Laboratories (MERL)
 *    An API for processing (e.g., generating and rendering) implicit ADFs
 *    Sarah Frisken and Ronald Perry
 *-----------------------------------------------------------------------------------
 */


/**
 *-----------------------------------------------------------------------------------
 *    To avoid multiple inclusion of header files
 *-----------------------------------------------------------------------------------
 */
#ifndef _ADF_IMPLICIT_
#define _ADF_IMPLICIT_


/**
 *-----------------------------------------------------------------------------------
 *    Required include files for this header file (not its implementation)
 *-----------------------------------------------------------------------------------
 */
#include "adftypesystem.h"
#include "adfgenerate.h"


/**
 *-----------------------------------------------------------------------------------
 *    To make functions accessible from C++ code
 *-----------------------------------------------------------------------------------
 */
#ifdef __cplusplus
extern "C" {
#endif

    
/**
 *-----------------------------------------------------------------------------------
 *    IMPLICIT ADF GENERATION
 *-----------------------------------------------------------------------------------
 *-----------------------------------------------------------------------------------
 *    Generate an implicit ADF representing the glyph described by the specified 
 *    ADFPath. A pointer to the implicit ADF is returned if the implicit ADF is
 *    generated successfully; a NULL is returned if the request cannot be satisfied.
 *-----------------------------------------------------------------------------------
 */
MEM_HANDLE ADFGenerateADFImplicit (void *libInst, MEM_HANDLE hpath, ADFGenAttrs
*genAttrs);


/**
 *-----------------------------------------------------------------------------------
 *    IMPLICIT ADF RENDERING
 *-----------------------------------------------------------------------------------
 *-----------------------------------------------------------------------------------
 *    Render the given implicit ADF glyph into the specified density image using the    
 *    rendering data ADFRenderGlyphData determined by ADFRenderSetup(). If the request
 *    cannot be satisfied, the specified density image is cleared (i.e., each pixel or
 *    pixel component of the density image is set to zero).
 *-----------------------------------------------------------------------------------
 */
ADF_Void ADFRenderGlyphImplicit (void *libInst, MEM_HANDLE hadf, 
                                 ADFRenderGlyphData *renderGlyphData, MEM_HANDLE himage);


/**
 *-----------------------------------------------------------------------------------
 *    End of C++ wrapper
 *-----------------------------------------------------------------------------------
 */
#ifdef __cplusplus
}
#endif


/**
 *-----------------------------------------------------------------------------------
 *    End of _ADF_IMPLICIT_
 *-----------------------------------------------------------------------------------
 */
#endif
