/** \file
 * \ingroup render
 *
 * $HopeName$
 *
 * Copyright (C) 2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Functions for rendering image masks into character device forms.
 */

struct render_blit_t ;
struct IMAGEDATA ;

/** Render an imagemask, represented by byte-aligned 1-bit data, into the
    outputform of a render state. */
void char_image_render(struct render_blit_t *rb, struct IMAGEDATA *imagedata,
                       const ibbox_t *imsbbox, uint8 *data, uint8 invert) ;

/* $Log$
 */
