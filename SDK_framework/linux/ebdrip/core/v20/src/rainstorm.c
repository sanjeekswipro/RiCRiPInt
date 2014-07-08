/** \file
 * \ingroup dl
 *
 * $HopeName: SWv20!src:rainstorm.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2008-2012 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Rainstorm prototype code
 *
 * Rainstorm is a project to simplify and serialise the rip display list,
 * and transmit it in a compressed stream format to a printer which contains
 * a lightweight decoder.
 * Prototype code is needed to be able to iron out issues with the design
 * and implementation of the Rainstorm stream format and API.
 *
 * This prototype code has been checked into the main rip source control
 * system for ease of development and testing, but is disabled and not
 * compiled by default. [ Could be made into a variant if I had the time to
 * wrestle with the jam build system ].
 * It has been developed and tested for windows builds only.
 * In order to use the code, comment in the define of RS_CODE_ENABLED below.
 *
 * Again for ease of initial development, the stand-alone decoder code
 * is also included in this source file, and can be built independently via
 *
 * cl /Zi /DBUILD_RSREAD=1 /Fersread.exe /Fdrsread.pdb /Forsread.obj rainstorm.c
 *
 * When this is done, Rainstorm is enabled by the PS fragment
 *   << /Rainstorm 1 >> setsystemparams
 * Then submit a job as usual, and a Rainstorm stream is created in the
 * top level folder, called "rip%d.rs".
 * An ascii listing of the contents of this can be created with the stand-alone
 * decoder using the command line
 *   rsread.exe -l rip0.rs
 * or it can be rendered to a windows BMP format via
 *   rsread.exe -c rip0.rs rip0.bmp
 *
 * N.B. THIS IS PROTOTYPE CODE THAT WILL BE THROWN AWAY
 *      WHEN MAIN DEVELOPMENT STARTS !
 *
 * The code contains experimental algorithms not suitable for a final product,
 * very little error checking, and arbitrary fixed upper limits on some stages
 * of processing. At this stage, it is known to be able to deal with a limited
 * number of simple test cases.
 */


/* #define RS_CODE_ENABLED 1 */


#ifdef BUILD_RSREAD
#define RS_CODE_ENABLED 0
#endif

#if RS_CODE_ENABLED /* { */

#include "core.h"
#include "swerrors.h"
#include "hqmemcpy.h"
#include "mm.h"
#include "mmcompat.h"
#include "fonts.h"
#include "fontcache.h"
#include "namedef_.h"

#include "display.h"
#include "bitblts.h"
#include "dl_store.h"
#include "images.h"
#include "imageo.h"
#include "graphics.h"
#include "dlstate.h"
#include "params.h"
#include "plotops.h"
#include "dl_image.h"
#include "ndisplay.h"
#include "dl_bres.h"
#include "gstate.h"
#include "clipops.h"
#include "control.h"
#include "charblts.h"
#include "dl_bbox.h"
#include "dl_foral.h"
#include "imstore.h"
#include "routedev.h"
#include "fcache.h"
#include "pathops.h"
#include "idlom.h"
#include "toneblt.h"
#include "trap.h"
#include "dl_free.h"
#include "vndetect.h"
#include "dl_color.h"
#include "gs_cache.h"
#include "gschtone.h"
#include "gschcms.h"
#include "gscdevci.h"
#include "gschead.h"
#include "halftone.h"
#include "color.h"
#include "rcbcntrl.h"
#include "recomb.h"
#include "rcbcomp.h"
#include "renderom.h"
#include "gs_tag.h"
#include "hdlPrivate.h"
#include "groupPrivate.h"
#include "cce.h"
#include "patternshape.h"
#include "ripmulti.h"
#include "ripdebug.h"
#include "debugging.h"
#include "dl_shade.h"
#include "rainstorm.h"

#define _CRT_SECURE_NO_DEPRECATE 1
#include <stdio.h>

typedef struct RS_RGB
{
  unsigned char r, g, b;
} RS_RGB;

typedef struct RS_COORD
{
  int32 x,y;
} RS_COORD;

typedef struct RSS /* Rainstorm state */
{
  RS_RGB color[256];
  uint8 colori, freei;
  struct
  {
    Bool cached;
    CHARCACHE *cc;
    int32 rlesize;
    void *name;
  } ccache[256];
} RSS;

static void rs_bandinit(RSS *rss)
{
  rss->color[0].r = rss->color[0].g = rss->color[0].b = 0;
  rss->color[1].r = rss->color[1].g = rss->color[1].b = 0;
  rss->color[2].r = rss->color[2].g = rss->color[2].b = 255;
  rss->colori = 1;
  rss->freei  = 3;
}

statcic void rs_write_nib(RS_BAND_BLOCK *rs, int32 val)
{
  if ( rs->nib )
  {
    rs->cmds[rs->size++] |= (uint8)(val);
    rs->nib = FALSE;
  }
  else
  {
    rs->cmds[rs->size] = (uint8)(val<<4);
    rs->nib = TRUE;
  }
}

static void rs_write(RS_BAND_BLOCK *rs, int32 val, int32 enc)
{
  switch ( enc )
  {
    case RS_BYTE:
      rs->cmds[rs->size++] = (uint8)(val);
      break;

    case RS_OFF16:
      val += 0x1000; /* and fall thru ... */
    case RS_DBYTE:
      rs->cmds[rs->size++] = (uint8)(val%256);
      rs->cmds[rs->size++] = (uint8)(val/256);
      break;

    case RS_UNS8:
      if ( val < 255 )
        rs->cmds[rs->size++] = (uint8)val;
      else
      {
        rs->cmds[rs->size++] = 0xff;
        rs->cmds[rs->size++] = (uint8)((val-255)%256);
        rs->cmds[rs->size++] = (uint8)((val-255)/256);
      }
      break;

    case RS_SIGN8:
      if ( -127 <= val && val <= 127 )
        rs->cmds[rs->size++] = (uint8)(val+127);
      else
      {
        rs->cmds[rs->size++] = 0xff;
        rs->cmds[rs->size++] = (uint8)((val+32767)%256);
        rs->cmds[rs->size++] = (uint8)((val+32767)/256);
      }
      break;
    case RS_VAL4:
      if ( 1 <= val && val <= 15 )
        rs_write_nib(rs, val-1);
      else if ( 16 <= val && val <= 270 )
      {
        val -= 16;
        rs_write_nib(rs, 0xF);
        rs_write_nib(rs, (val>>4)&0xF);
        rs_write_nib(rs, (val)&0xF);
      }
      else
      {
        val -= 271;
        rs_write_nib(rs, 0XF);
        rs_write_nib(rs, 0XF);
        rs_write_nib(rs, 0XF);
        rs_write_nib(rs, (val>>12)&0xF);
        rs_write_nib(rs, (val>>8)&0xF);
        rs_write_nib(rs, (val>>4)&0xF);
        rs_write_nib(rs, (val)&0xF);
      }
      break;

    case RS_NIBBLE:
      rs_write_nib(rs, val);
      break;
  }
}

static void rs_color(LISTOBJECT *lobj, RS_BAND_BLOCK *rs, RSS *rss)
{
  dl_color_t dlc;
  uint8 colori = 0;

  dlc_from_lobj_weak(lobj, &dlc);
  switch ( dlc_check_black_white(&dlc) )
  {
    case DLC_TINT_WHITE:
      colori = 2;
      break;
    case DLC_TINT_BLACK:
      colori = 1;
      break;
    case DLC_TINT_OTHER:
    {
      color_entry_t *pce;
      uint8 r, g, b;

      pce = &(dlc.ce);
      r = (uint8)(pce->pcv[0]/0x100);
      g = (uint8)(pce->pcv[1]/0x100);
      b = (uint8)(pce->pcv[2]/0x100);

      if ( r == 0 && g == 0 && b == 0 )
        colori = 1;
      else if ( r == 0xff && g == 0xff && b == 0xff )
        colori = 2;
      else
      {
        for ( colori = 3; colori < rss->freei; colori++ )
        {
          if ( rss->color[colori].r == r && rss->color[colori].g == g &&
               rss->color[colori].b == b )
            break;
        }
        if ( colori == rss->freei )
        {
          rss->color[rss->freei].r = r;
          rss->color[rss->freei].g = g;
          rss->color[rss->freei].b = b;
          if ( rss->freei != 255 )
            rss->freei++;

          rs->cmds[rs->size++] = 0x81;
          rs->cmds[rs->size++] = colori;
          rs->cmds[rs->size++] = r;
          rs->cmds[rs->size++] = g;
          rs->cmds[rs->size++] = b;
        }
      }
      break;
    }
  }
  if ( rss->colori != colori )
  {
    rs->cmds[rs->size++] = 0x82;
    rs->cmds[rs->size++] = colori;
    rss->colori = colori;
  }
}

typedef struct ATHREAD
{
  int32 ncoords;
  int32 minx, maxx;
  RS_COORD xy[1];
} ATHREAD;

typedef struct ALLTHREADS
{
  int32 bytes;
  int32 nthreads;
  ATHREAD *thread[1];
} ALLTHREADS;

static ALLTHREADS *extract_threads(NFILLOBJECT *nfill)
{
  static ALLTHREADS *allthreads = NULL;
  int32 th;

  if ( allthreads == NULL )
  {
    allthreads = (ALLTHREADS *)malloc(1024*1024);
    allthreads->bytes = 1024*1024;
  }

  if ( (allthreads->nthreads = nfill->nthreads) > 50 )
    return NULL;

  for ( th = 0; th < nfill->nthreads; th++ )
  {
    NBRESS *nbress = nfill->thread[th];
    ATHREAD *at;
    RS_COORD *xy;
    dcoord i, x, y;

    allthreads->thread[th] = (ATHREAD *)(((uint8 *)allthreads) +
        sizeof(ALLTHREADS) + allthreads->nthreads * sizeof(ATHREAD *) +
        th*allthreads->bytes/allthreads->nthreads);
    at = allthreads->thread[th];
    xy = at->xy;
    xy->x = nbress->nx1; xy->y = nbress->ny1; xy++;
    at->ncoords = 2;
    x = nbress->nx2; y = nbress->ny2;
    xy->x = x; xy->y = y; xy++;

    dxylist_reset(&(nbress->dxy));
    while ( dxylist_get(&(nbress->dxy),&x,&y) )
    {
      xy->x = x; xy->y = y; xy++;
      at->ncoords++;
    }
    if ( at->ncoords > 1000 )
      return NULL;
  }
  return allthreads;
}

static int32 merge_threads(ALLTHREADS *at)
{
  int32 th, dst;

  /* join any threads that we can */
  for ( th = 0; th < at->nthreads; th++ )
  {
    ATHREAD *at1 = at->thread[th];

    for ( dst = 0; dst < at->nthreads; dst++ )
    {
      ATHREAD *at2 = at->thread[dst];

      if ( dst != th && at1 != NULL && at2 != NULL )
      {
        /* can th(at1) be added to the end of dst(at2) ? */
        if ( at1->xy[0].x == at2->xy[at2->ncoords-1].x &&
             at1->xy[0].y == at2->xy[at2->ncoords-1].y )
        {
          int32 j;

          for ( j = 0; j < at1->ncoords; j++ )
            at2->xy[at2->ncoords+j-1] = at1->xy[j];
          at2->ncoords += at1->ncoords-1;
          at->thread[th] = NULL;
        }
      }
    }
  }
  /* re-count the threads */
  for ( th = 0, dst = 0; th < at->nthreads; th++ )
  {
    at->thread[dst] = at->thread[th];
    if ( at->thread[dst] != NULL )
      dst++;
  }
  at->nthreads -= (th-dst);

  /* Temp : assume no self-intersections, reverse sort by max x */
  for ( th = 0; th < at->nthreads; th++ )
  {
    ATHREAD *at1 = at->thread[th];
    int32 j, mymin, mymax;

    mymin = mymax = at1->xy[0].x;
    for ( j = 1; j < at1->ncoords; j++ )
    {
      if ( at1->xy[j].x > mymax )
        mymax = at1->xy[j].x;
      if ( at1->xy[j].x < mymin )
        mymin = at1->xy[j].x;
    }
    at1->minx = mymin;
    at1->maxx = mymax;
  }
  for ( th = 0; th < at->nthreads; th++ )
  {
    for ( dst = 0; dst < at->nthreads; dst++ )
    {
      if ( th != dst )
      {
        ATHREAD *at1 = at->thread[th];
        ATHREAD *at2 = at->thread[dst];

        if ( ( th < dst && at1->maxx > at2->maxx ) ||
             ( th > dst && at1->maxx < at2->maxx ) )
        {
          at->thread[th]  = at2;
          at->thread[dst] = at1;
        }
      }
    }
  }
  if ( at->nthreads >=2 && at->thread[0]->minx > at->thread[1]->minx )
  {
    ATHREAD *at1 = at->thread[0];
    ATHREAD *at2 = at->thread[1];

    at->thread[0] = at2;
    at->thread[1] = at1;
  }
  return at->nthreads;
}

static void makeyhull(RS_BAND_BLOCK *rs, ATHREAD *left, ATHREAD *right)
{
  RS_COORD *lxy = left->xy;
  RS_COORD *rxy = right->xy;

  do
  {
    int32 i, do_left, do_right, nvecs, x, y;
    int32 eleft = 0, eright = 0;
    int32 toptail = 0;

    if ( ( do_left = left->ncoords ) > 17 )
      do_left = 17;
    if ( ( do_right = right->ncoords ) > 17 )
      do_right = 17;

    if ( lxy[0].y < rxy[0].y )
      eright++, toptail += 1;
    else if ( lxy[0].y > rxy[0].y )
      eleft++, toptail += 2;

    if ( lxy[do_left-1].y < rxy[do_right-1].y )
      eleft++, toptail += 4;
    else if ( lxy[do_left-1].y > rxy[do_right-1].y )
      eright++, toptail += 8;

    if ( do_left > 17 - eleft )
      do_left = 17 - eleft;
    if ( do_right > 17 - eright )
      do_right = 17 - eright;

    nvecs = (do_left + eleft -2)*16+(do_right + eright - 2);

    /* write the hull header */
    rs_write(rs, 0x86, RS_BYTE);
    rs_write(rs, 0, RS_UNS8); /* ya */
    rs_write(rs, rs->y1-rs->y0+1, RS_UNS8); /* yb */
    rs_write(rs, nvecs, RS_BYTE); /* nvecs */

    /* write the hull left hand vectors */
    if ( toptail & 2 )
      { x = rxy[0].x; y = rxy[0].y; }
    else
      { x = lxy[0].x; y = lxy[0].y; }
    rs_write(rs, x, RS_OFF16);
    rs_write(rs, y, RS_OFF16);
    for ( i = (toptail & 2) ? 0 : 1; i < do_left; i++ )
    {
      rs_write(rs, lxy[i].x - x, RS_SIGN8);
      rs_write(rs, lxy[i].y - y, RS_UNS8);
      x = lxy[i].x;
      y = lxy[i].y;
    }
    if ( toptail & 4 )
    {
      rs_write(rs, rxy[do_right-1].x - x, RS_SIGN8);
      rs_write(rs, rxy[do_right-1].y - y, RS_UNS8);
    }

    /* write the hull right hand vectors */
    if ( toptail & 1 )
      { x = lxy[0].x; y = lxy[0].y; }
    else
      { x = rxy[0].x; y = rxy[0].y; }
    rs_write(rs, x, RS_OFF16);
    rs_write(rs, y, RS_OFF16);
    for ( i = (toptail & 1) ? 0 : 1; i < do_right; i++ )
    {
      rs_write(rs, rxy[i].x - x, RS_SIGN8);
      rs_write(rs, rxy[i].y - y, RS_UNS8);
      x = rxy[i].x;
      y = rxy[i].y;
    }
    if ( toptail & 8 )
    {
      rs_write(rs, lxy[do_left-1].x - x, RS_SIGN8);
      rs_write(rs, lxy[do_left-1].y - y, RS_UNS8);
    }

    lxy += do_left;
    rxy += do_right;
    left->ncoords -= do_left;
    right->ncoords -= do_right;

    if ( left->ncoords > 0 || right->ncoords > 0 )
    {
      left->ncoords++, lxy--;
      right->ncoords++, rxy--;
      if ( left->ncoords == 1 )
        lxy[1] = lxy[0], left->ncoords++;
      if ( right->ncoords == 1 )
        rxy[1] = rxy[0], right->ncoords++;
    }
  } while ( left->ncoords > 0 || right->ncoords > 0 );
}

static void rs_nfill(LISTOBJECT *lobj, RS_BAND_BLOCK *rs, RSS *rss)
{
  ALLTHREADS *at = extract_threads(lobj->dldata.nfill);

  if ( at == NULL )
    return;
  rs_color(lobj, rs, rss);
  while ( merge_threads(at) > 1 )
  {
    ATHREAD *th0 = at->thread[0];
    ATHREAD *th1 = at->thread[1];
    RS_COORD th0_end, th1_end;

    th0_end = th0->xy[th0->ncoords-1];
    th1_end = th1->xy[th1->ncoords-1];
    if ( th0_end.y < th1_end.y )
      th0->xy[th0->ncoords++] = th1_end;
    else if ( th0_end.y > th1_end.y )
      th1->xy[th1->ncoords++] = th0_end;
    makeyhull(rs, th0, th1);
    if ( th0_end.y == th1_end.y )
      at->thread[0] = at->thread[1] = NULL;
    else if ( th0_end.y < th1_end.y )
    {
      th0->xy[0] = th0_end;
      th0->xy[1] = th1_end;
      th0->ncoords = 2;
      at->thread[1] = NULL;
    }
    else
    {
      th1->xy[0] = th1_end;
      th1->xy[1] = th0_end;
      th1->ncoords = 2;
      at->thread[0] = NULL;
    }
  }
}

static int32 decode_runs(uint8 *rle, int32 bytes, int32 nib, int32 *runs)
{
  int32 n[1000], nn, rr;
  int32 nvals, maxval;

  if ( nib == 1 )
  {
    nvals = 2*bytes; maxval = 0xF;
    for ( nn = 0; nn < nvals; nn += 2 )
    {
      n[nn]   = (rle[nn/2]>>4)&0xf;
      n[nn+1] = (rle[nn/2])&0xf;
    }
  }
  else
  {
    nvals = bytes; maxval = 0xFF;
    for ( nn = 0; nn < nvals; nn += 2 )
    {
      n[nn]   = rle[nn];
      n[nn+1] = rle[nn+1];
    }
  }
  for ( rr = 0, nn = 0; nn < nvals; )
  {
    if ( n[nn] == maxval - 1 )
    {
      nn++;
      runs[rr++] = (maxval + 1) + n[nn++];
    }
    else if ( n[nn] == maxval )
    {
      nn++;
      runs[rr] = (2*maxval + 1) + (maxval+1)*n[nn++];
      runs[rr++] += n[nn++];
    }
    else
    {
      runs[rr++] = 1 + n[nn++];
    }
  }
  if ( rr & 1 )
    rr--;
  return rr/2;
}

static void rs_ouputrlechar(RS_BAND_BLOCK *rs, int32 id, FORM *form)
{
  uint8 *rle = (uint8 *)(form->addr), *end = rle + form->size;
  int32 nib = form->type - FORMTYPE_CACHERLE1 + 1;
  int32 runs[1000], npairs, nn;
  uint8 *nnibbles;

  rs_write(rs, 0x8B, RS_BYTE);
  rs_write(rs, id, RS_BYTE);
  rs_write(rs, form->w, RS_UNS8);
  rs_write(rs, form->h, RS_UNS8);
  nnibbles = &(rs->cmds[rs->size]);
  rs_write(rs, 0, RS_DBYTE);

  if ( nib > 2 )
    return;

  while ( rle < end )
  {
    int32 nlines = *rle++;
    int32 size   = *rle++;

    npairs = decode_runs(rle, size, nib, runs);
    rle += size;

    if ( npairs == 0 )
      rs_write(rs, 15, RS_NIBBLE); /* 15 == blank line ? */
    else if ( npairs <= 11 )
      rs_write(rs, npairs, RS_NIBBLE);
    else
    {
      ; /* TEMP : NYI */
    }
    for ( nn = 0; nn < npairs*2; nn++ )
      rs_write(rs, runs[nn], RS_VAL4);
    if ( nlines > 1 )
    {
      rs_write(rs, 14, RS_NIBBLE);
      rs_write(rs, nlines-1, RS_VAL4);
    }
  }
  nn = (int32)((&(rs->cmds[rs->size]) - nnibbles - 2)*2);
  if ( rs->nib )
  {
    rs_write(rs, 0, RS_NIBBLE);
    nn++;
  }
  nnibbles[0] = (uint8)(nn&0xff);
  nnibbles[1] = (uint8)((nn>>8)&0xff);
}

static void rs_cachechar(RSS *rss, CHARCACHE *cc, RS_BAND_BLOCK *rs, FORM *form)
{
  int32 i;

  for ( i = 0; i < 256; i++ )
  {
    if ( rss->ccache[i].cached && cc == rss->ccache[i].cc &&
         cc->rlesize == rss->ccache[i].rlesize &&
         cc->glyphname._d1.vals.name == rss->ccache[i].name )
      return;
  }
  for ( i = 0; i < 256; i++ )
  {
    if ( !rss->ccache[i].cached )
    {
      rss->ccache[i].cached = TRUE;
      rss->ccache[i].cc = cc;
      rss->ccache[i].rlesize = cc->rlesize;
      rss->ccache[i].name = cc->glyphname._d1.vals.name;
      rs_ouputrlechar(rs, i, form);
      return;
    }
  }
}

static uint8 rs_cacheid(RSS *rss, CHARCACHE *cc)
{
  int32 i;

  for ( i = 0; i < 256; i++ )
  {
    if ( rss->ccache[i].cached && cc == rss->ccache[i].cc &&
         cc->rlesize == rss->ccache[i].rlesize &&
         cc->glyphname._d1.vals.name == rss->ccache[i].name )
      return (uint8)i;
  }
  return 255;
}

static void rs_page_defines(DLREF *dl, RS_BAND_BLOCK *rs, RSS *rss)
{
  int32 i;

  for ( i = 0; i < 256; i++ )
    rss->ccache[i].name = NULL, rss->ccache[i].cached = FALSE;

  for ( ; dl != NULL; dl = dlref_next(dl) )
  {
    LISTOBJECT *lobj = dlref_lobj(dl);

    if ( lobj->opcode  == RENDER_char )
    {
      DL_CHARS *dlchars = lobj->dldata.text;

      for ( i = 0; i < dlchars->nchars; i++ )
      {
        FORM *form = dlchars->ch[i].form;
        CHARCACHE *cc = NULL;

        if ( form->type == FORMTYPE_CHARCACHE )
        {
          cc = ((CHARCACHE *)form);
          form = cc->thebmapForm;
        }

        if ( form->type >= FORMTYPE_CACHERLE1 &&
             form->type <= FORMTYPE_CACHERLE8 )
          rs_cachechar(rss, cc, rs, form);
      }
    }
  }
}

static void rs_char(LISTOBJECT *lobj, RS_BAND_BLOCK *rs, RSS *rss)
{
  DL_CHARS *dlchars = lobj->dldata.text;
  int32 i;

  rs_color(lobj, rs, rss);

  for ( i = 0; i < dlchars->nchars; i++ )
  {
    FORM *form = dlchars->ch[i].form;
    dcoord x   = dlchars->ch[i].x;
    dcoord y   = dlchars->ch[i].y;
    CHARCACHE *cc = NULL;

    if ( form->type == FORMTYPE_CHARCACHE )
    {
      cc = ((CHARCACHE *)form);
      form = cc->thebmapForm;
    }

    if ( form->type >= FORMTYPE_CACHERLE1 &&
         form->type <= FORMTYPE_CACHERLE8 )
    {
      uint8 id = rs_cacheid(rss, cc);

      rs_write(rs, 0x8C, RS_BYTE);
      rs_write(rs, id, RS_BYTE);
      rs_write(rs, x, RS_OFF16);
      rs_write(rs, y-rs->y0, RS_SIGN8);
    }
  }
}

static void rs_moveto(RS_BAND_BLOCK *rs, int32 x, int32 y)
{
  rs_write(rs, 0x80, RS_BYTE);
  rs_write(rs, x, RS_DBYTE);
  rs_write(rs, y, RS_UNS8);
}

static void rs_image(LISTOBJECT *lobj, RS_BAND_BLOCK *rs)
{
  IMAGEOBJECT *im = lobj->dldata.image;
  int32 xx = im->bbox.x1;
  int32 yy = im->bbox.y1;
  int32 w = im->bbox.x2 - im->bbox.x1;
  int32 h = im->bbox.y2 - im->bbox.y1;
  int32 x, y, colorant;
  uint8 *compsize, *compdata;

  if ( w <= 0 || h <= 0 )
    return; /* safety valve */

  if ( im->ims == NULL || im_storebpp(im->ims) != 8 )
    return;

  rs_moveto(rs, xx, yy);
  rs_write(rs, 0x87, RS_BYTE);
  rs_write(rs, im->rw, RS_UNS8);
  rs_write(rs, im->rh, RS_UNS8);
  rs_write(rs, 0, RS_UNS8);
  rs_write(rs, 0, RS_UNS8);
  rs_write(rs, im->rw, RS_UNS8);
  rs_write(rs, im->rh, RS_UNS8);
  rs_write(rs, w, RS_UNS8);
  rs_write(rs, h, RS_UNS8);
  rs_write(rs, 0, RS_BYTE); /* uncompress 8 bit RGB */
  compsize = &(rs->cmds[rs->size]);
  rs_write(rs, 0, RS_DBYTE); /* space for compressed size */
  rs_write(rs, 0, RS_DBYTE); /* space for compressed size */
  compdata = &(rs->cmds[rs->size]);

  for ( y = 0; y < im->rh; y++ )
  {
    for ( colorant = 0; colorant < 3; colorant++ )
    {
      for ( x = 0; x < im->rw; )
      {
        int32 impixels;
        uint8 *imdata;

        im_storeread(im->ims, x, y, colorant, &imdata, &impixels);
        if ( x + impixels > im->rw )
          impixels = im->rw - x;
        HqMemCpy(compdata, imdata, impixels);
        rs->size += impixels;
        compdata += impixels;
        x += impixels;
      }
    }
  }
  compsize[0] = (uint8)(((int32)(compdata - compsize - 4))&0xff);
  compsize[1] = (uint8)(((int32)((compdata - compsize - 4)>>8))&0xff);
  compsize[2] = (uint8)(((int32)((compdata - compsize - 4)>>16))&0xff);
  compsize[3] = (uint8)(((int32)((compdata - compsize - 4)>>24))&0xff);
}

static void rs_rect(LISTOBJECT *lobj, RS_BAND_BLOCK *rs, RSS *rss)
{
  int32 x = lobj->bbox.x1;
  int32 y = lobj->bbox.y1;
  int32 w = lobj->bbox.x2 - x;
  int32 h = lobj->bbox.y2 - y;

  if ( y < (int32)rs->y0 )
    { h -= rs->y0-y; y = rs->y0; }
  if ( y + h - 1 > (int32)rs->y1 )
    { h = (int32)rs->y1 - y + 1; }

  if ( h > 0 )
  {
    y -= (int32)rs->y0;
    rs_color(lobj, rs, rss);
    rs_moveto(rs, x, y);

    rs_write(rs, 0x85, RS_BYTE);
    rs_write(rs, w, RS_UNS8);
    rs_write(rs, h, RS_UNS8);
  }
}

static void rs_hdl(DLREF *dl, RS_BAND_BLOCK *rs, RSS *rss)
{
  for ( ; dl != NULL; dl = dlref_next(dl) )
  {
    LISTOBJECT *lobj = dlref_lobj(dl);

    switch ( lobj->opcode )
    {
      case RENDER_rect:
        rs_rect(lobj, rs, rss);
        break;

      case RENDER_quad:
        /** \todo BMJ 09-Jun-08 :  : rs_quad(lobj, rs, rss); */
        break;

      case RENDER_fill:
        rs_nfill(lobj, rs, rss);
        break;

      case RENDER_char:
        rs_char(lobj, rs, rss);
        break;

      case RENDER_image:
        rs_image(lobj, rs);
        break;
      case RENDER_hdl:
        rs_hdl(hdlOrderList(lobj->dldata.hdl, rs, rss);
        break;
      case RENDER_void:
      case RENDER_erase:
      case RENDER_mask:
      case RENDER_vignette:
      case RENDER_gouraud:
      case RENDER_shfill:
      case RENDER_shfill_patch:
      case RENDER_group:
      case RENDER_cell:
      case RENDER_backdrop:
      default:
        break;
    }
  }
}

static void rs_band(DLREF *dl, RS_BAND_BLOCK *rs, RSS *rss)
{
  static uint8 *rsbuf = NULL;

  if ( rsbuf == NULL )
    rsbuf = malloc(64*1024*1024);
  rs->size = 0;
  rs->nib  = FALSE;
  rs->cmds = rsbuf;

  if ( rs->y0 == 0 && rs->y1 == 0 )
  {
    rs_page_defines(dl, rs, rss);
    return;
  }
  rs_bandinit(rss);
  rs_hdl(dl, rs, rss);
}

static FILE *rs_open()
{
  FILE *f;
  int32 page;
  char name[32];

  for ( page = 0; page < 10000; page++ )
  {
    sprintf(name,"rip%d.rs",page);
    if ( ( f = fopen(name,"r")) == NULL )
    {
      f = fopen(name,"wb");
      return f;
    }
    fclose(f);
  }
  return NULL;
}

void rainstorm(const DL_STATE* self)
{
  FILE *f = rs_open();
  RS_ID_BLOCK rs_id_block;
  RS_JOB_BLOCK rs_job_block;
  RS_PAGE_BLOCK rs_page_block;
  RS_BAND_BLOCK rs_band_block;
  RSS rss;
  uint32 meta;
  int32 bandi;

  rs_id_block.magic[0] = RS_MAGIC0;
  rs_id_block.magic[1] = RS_MAGIC1;
  rs_id_block.version[0] = 0;
  rs_id_block.version[1] = 0;
  fwrite(&rs_id_block, 1, sizeof(rs_id_block), f);

  rs_job_block.id = self->job_number;
  rs_job_block.meta_size = 4;
  meta = 0x12345678;
  fwrite(&rs_job_block, 1, sizeof(rs_job_block), f);
  fwrite(&meta, 1, rs_job_block.meta_size, f);

  rs_page_block.id = self->page_number;
  rs_page_block.rasterStyle = 0;
  rs_page_block.pageWidth = self->page_w;
  rs_page_block.pageHeight = self->page_h;
  rs_page_block.colors = 3;
  rs_page_block.bits   = 8;
  rs_page_block.spare1 = rs_page_block.spare2 = 0;
  rs_page_block.bg[0] = rs_page_block.bg[1] = rs_page_block.bg[2] = 0xff;
  rs_page_block.bg[3] = 0x0;
  rs_page_block.meta_size = 4;
  fwrite(&rs_page_block, 1, sizeof(rs_page_block), f);
  meta = 0x23456789;
  fwrite(&meta, 1, rs_page_block.meta_size, f);

  rs_band_block.y0 = rs_band_block.y1 = 0;
  rs_band(get_dl_orderlist(self), &rs_band_block, &rss);
  {
    fwrite(&(rs_band_block.size), 1, sizeof(rs_band_block.size), f);
    fwrite(rs_band_block.cmds, 1, rs_band_block.size, f);
  }
  for ( bandi = 0; bandi < self->sizedisplaylist; bandi++ )
  {
    rs_band_block.y0 = bandi*self->band_lines;
    rs_band_block.y1 = (bandi+1)*self->band_lines-1;
    if ( rs_band_block.y1 > (uint32)(self->page_h) )
        rs_band_block.y1 = (uint32)(self->page_h);

    rs_band(get_dl_head(self, bandi), &rs_band_block, &rss);
    if ( rs_band_block.size > 0 )
    {
      fwrite(&rs_band_block, 1, 12, f);
      fwrite(rs_band_block.cmds, 1, rs_band_block.size, f);
    }
  }
  meta = 0xffffffff;
  fwrite(&meta, 1, sizeof(uint32), f);
  fwrite(&meta, 1, sizeof(uint32), f);
  fclose(f);
}

#else  /* !RS_CODE_ENABLED */

#ifndef BUILD_RSREAD

#include "core.h"
#include "displayt.h"

/**
 * Dummy stub for when rainstorm prototype code is not
 * compiled in.
 */
void rainstorm(const DL_STATE *self)
{
  UNUSED_PARAM(const DL_STATE *, self);
}

#endif /* BUILD_RSREAD */

#endif /* RS_CODE_ENABLED } */

/*
 * ============================================================================
 */

#if BUILD_RSREAD /* { */

/*
 * Code for stand alone tool rsread.exe
 *
 * Read the contents of a Rainstorm graphics stream file and either
 *   a) Create an ascii listing of the contents or
 *   b) Convert it to a simple ".bmp" raster file
 */

#define _CRT_SECURE_NO_DEPRECATE 1
#include <stdio.h>
#include <malloc.h>
#include <signal.h>
typedef short          int16;
typedef unsigned short uint16;
typedef unsigned long  uint32;
typedef long           int32;
typedef unsigned char  uint8;
typedef int32          Bool;
#define TRUE 1
#define FALSE 0
#include "rainstorm.h"

void sigfunc(int sig)
{
  printf("Got signal %d... exiting\n",sig);
  exit(1);
}

void init_catch()
{
  signal(SIGINT, sigfunc);
  signal(SIGABRT, sigfunc);
  signal(SIGFPE, sigfunc);
  signal(SIGILL, sigfunc);
  signal(SIGSEGV, sigfunc);
  signal(SIGTERM, sigfunc);
}

void error(char *message)
{
  fprintf(stderr,"%s\n", message);
  exit(1);
}

typedef struct RS_RGB
{
  unsigned char r, g, b;
} RS_RGB;

typedef struct RS_COORD
{
  int32 x,y;
} RS_COORD;

typedef struct RSS /* Rainstorm State */
{
  FILE *src, *dst;
  RS_ID_BLOCK rs_id_block;
  RS_JOB_BLOCK  *rs_job_block;
  RS_PAGE_BLOCK *rs_page_block;
  struct
  {
    uint32 size;
    uint8 *ptr;
  } page_defines;
  RS_BAND_BLOCK band;
  uint32 rasterWidth, rasterHeight;
  uint8 *pageRaster;
  /* state info */
  int32 x,y;
  uint8 rlemode;
  RS_RGB color[256];
  uint8 colori;
  struct
  {
    int xx;
  } rcache[256];
} RSS;

typedef struct BMP_HEADER
{
  int32 filesize;
  int16 dum1, dum2;
  int32 offset;

  int32 size, width, height;
  int16 planes, bits;
  int32 compression, imagesize, xres, yres;
  int32 ncolors, impcolors;
} BMP_HEADER;

int bmp_setup(RSS *rss)
{
  BMP_HEADER bmp;
  int32 w = rss->rs_page_block->pageWidth;
  int32 h = rss->rs_page_block->pageHeight;
  int16 magic = 0x4D42;
  int32 x, y;
  int32 rw = ((w*3+3)/4)*4;
  uint8 *bg = rss->rs_page_block->bg;

  rss->rasterWidth  = rw;
  rss->rasterHeight = h;

  if ( ( rss->pageRaster = (uint8 *)malloc(rw*h)) == NULL )
    return -1;

  bmp.filesize = sizeof(bmp)+rw*h + sizeof(magic);
  bmp.dum1 = bmp.dum2 = 0;
  bmp.offset = sizeof(bmp) + sizeof(magic);

  bmp.size        = 0x28;
  bmp.width       = w;
  bmp.height      = -h; /* invert raster sense */
  bmp.planes      = 1;
  bmp.bits        = 0x18;
  bmp.compression = 0x0;
  bmp.imagesize   = rw*h;
  bmp.xres        = 2835; /* 72dpi */
  bmp.yres        = 2835; /* 72dpi */
  bmp.ncolors     = 0;
  bmp.impcolors   = 0;

  fwrite(&magic, 1, sizeof(magic), rss->dst);
  fwrite(&bmp, 1, sizeof(bmp), rss->dst);

  for ( y = 0 ; y < h; y++ )
  {
    for ( x = 0 ; x < w; x++ )
    {
      rss->pageRaster[3*(y*w+x)+0] = bg[0];
      rss->pageRaster[3*(y*w+x)+1] = bg[1];
      rss->pageRaster[3*(y*w+x)+2] = bg[2];
    }
  }
  return 1;
}

typedef struct NIBREAD
{
  Bool nib;
  uint8 *ptr;
} NIBREAD;

int32 rs_read(NIBREAD *nr, int32 encoding)
{
  int32 val;
  uint8 *ptr = nr->ptr;

  switch (encoding)
  {
    case RS_NIBBLE:
      if ( nr->nib )
      {
        val = ptr[0]&0xF;
        nr->nib = FALSE;
        nr->ptr++;
      }
      else
      {
        val = (ptr[0]>>4)&0xF;
        nr->nib = TRUE;
      }
      break;

    case RS_VAL4:
      if ( nr->nib )
      {
        val = (*ptr++ & 0xF) + 1;
        nr->nib = FALSE;
        nr->ptr += 1;
        if ( val == 16 )
        {
          val = 16 + *ptr++;
          nr->ptr += 1;
          if ( val == 256+16 )
          {
            val = 271 + ptr[0] + 256*ptr[1];
            nr->ptr += 2;
          }
        }
      }
      else
      {
        val = ((ptr[0]>>4)&0xF) + 1;
        nr->nib = TRUE;
        if ( val == 16 )
        {
          val = 16 + ((ptr[0]&0xF)<<4) + ((ptr[1]>>4)&0xF);
          nr->ptr += 1; ptr++;
          if ( val == 256+16 )
          {
            val = 271 + ((ptr[0]&0xF)<<12) + (((ptr[1]>>4)&0xF)<<8)
                      + ((ptr[1]&0xF)<<4) + ((ptr[2]>>4)&0xF);
            nr->ptr += 2;
          }
        }
      }
      break;
    case RS_UNS8:
      val = *ptr++;
      nr->ptr += 1;
      if ( val == 255 )
      {
        val = ptr[0] + 256*ptr[1] + 255;
        nr->ptr += 2;
      }
      break;
    case RS_SIGN8:
      val = *ptr++ - 127;
      nr->ptr += 1;
      if ( val == 128 )
      {
        val = ptr[0] + 256*ptr[1] - 32767;
        nr->ptr += 2;
      }
      break;
    case RS_OFF16:
        val = ptr[0] + 256*ptr[1] - 0x1000;
        nr->ptr += 2;
        break;
    case RS_SIZE16:
        val = ptr[0] + 256*ptr[1];
        nr->ptr += 2;
        if ( val == 0xffff )
        {
          ptr += 2;
          val = ptr[0] + 256*ptr[1] + 256*256*ptr[2] +
                256*256*256*ptr[3] + 65536;
          nr->ptr += 4;
        }
        break;
    case RS_BYTE:
      val = *ptr++;
      nr->ptr += 1;
      break;
    case RS_DBYTE:
      val = ptr[0] + 256*ptr[1];
      nr->ptr += 2;
      break;
    case RS_DELTA4:
      val = (*ptr++ & 0xF) - 8;
      nr->ptr += 1;
      break;
    case RS_DELTA2p2:
      break;
    default:
      error("Bad encoding style");
      break;
  }
  return val;
}

uint8 *base_addr(RSS *rss, int32 x, int32 y)
{
  y += rss->band.y0;

  if ( y < 0 || y >= rss->rasterHeight )
    return NULL;
  return rss->pageRaster + rss->rasterWidth * y + x*3;
}

void paintRun(RSS *rss, int32 color, int32 x, int32 y, int32 run)
{
  uint8 *ras = base_addr(rss, x, y);
  RS_RGB   *rgb = &rss->color[color];
  int32 i;

  if ( ras && color > 0 )
  {
    for ( i = 0; i < run; i++ )
    {
      *ras++ = rgb->b;
      *ras++ = rgb->g;
      *ras++ = rgb->r;
    }
  }
}

void paintRectangle(RSS *rss, int32 w, int32 h)
{
  int32 y;

  for ( y = 0; y < h; y++ )
    paintRun(rss, rss->colori, rss->x, rss->y+y, w);
}

int32 find_x(int32 y, RS_COORD *coord, int32 ncoord)
{
  int32 i;

  if ( y < coord[0].y )
    return -1001;

  for ( i = 0; i < ncoord-1; i++ )
  {
    if ( y <= coord[i+1].y )
    {
      double f = ((double)(y - coord[i].y))/(coord[i+1].y - coord[i].y);
      double x = coord[i].x + f*(coord[i+1].x - coord[i].x);
      return (int32)(x+0.5);
    }
  }
  return -1001;
}

void paintHull(RSS *rss, int32 ya, int32 yb, int32 nleft, int32 nright,
               RS_COORD *left, RS_COORD *right)
{
  int32 y, xl, xr;

  for ( y = ya; y <= yb; y++ )
  {
    xl = find_x(y+rss->band.y0, left, nleft);
    xr = find_x(y+rss->band.y0, right, nright);
    if ( xl > -1000 && xr > -1000 )
      paintRun(rss, rss->colori, xl, y, xr-xl);
  }
}

void paintContone(RSS *rss, int32 sw, int32 sh, int32 tx, int32 ty,
                  int32 tw, int32 th, int32 dw, int32 dh,
                  int32 colorspace, int32 compress, int32 bytes,uint8 *ptr)
{
  int32 sx, sy, repx, repy;

  if ( colorspace != 0 || compress != 0 )
    return;
  if ( tx != 0 || ty != 0 || sw != tw || sh != th )
    return;

  repx = dw/sw; if ( repx == 0 ) repx = 1;
  repy = dh/sh; if ( repy == 0 ) repy = 1;

  for ( sy = 0; sy < sh; sy++ )
  {
    int32 y = sy*dh/sh;

    for ( sx = 0; sx < sw; sx++ )
    {
      int32 x = sx*dw/sw;
      int32 i, j;

      uint8 r = ptr[sy*sw*3+sw*0+sx];
      uint8 g = ptr[sy*sw*3+sw*1+sx];
      uint8 b = ptr[sy*sw*3+sw*2+sx];

      for ( j = 0; j < repy; j++ )
      {
        uint8 *dst = rss->pageRaster + rss->rasterWidth * (rss->y + y+j) +
                                                          (rss->x + x)*3;

        for ( i = 0; i < repx; i++ )
          *dst++ = b, *dst++ = g, *dst++ = r;
      }
    }
  }
}

uint8 *find_glyph(RSS *rss, int32 find_id, int32 *nnibbles, int32 *w, int32 *h)
{
  NIBREAD nr;
  uint8 *end;

  nr.nib = FALSE;
  nr.ptr = rss->page_defines.ptr;
  end = nr.ptr + rss->page_defines.size;

  while ( nr.ptr < end )
  {
    if ( rs_read(&nr, RS_BYTE) == 0x8B )
    {
      int32 id, n;

      id = rs_read(&nr, RS_BYTE);
      *w  = rs_read(&nr, RS_UNS8);
      *h  = rs_read(&nr, RS_UNS8);
      n  = rs_read(&nr, RS_DBYTE);

      if ( id == find_id )
      {
        *nnibbles = n;
        return nr.ptr;
      }
      else
        nr.ptr += (n+1)/2;
    }
    else
      return NULL;
  }
  return NULL;
}

void paintGlyph(RSS *rss, int32 id, int32 x, int32 y)
{
  NIBREAD nr;
  int32 nn, w, h;
  uint8 *rle;

  rle = find_glyph(rss, id, &nn, &w, &h);
  nr.ptr = rle;
  nr.nib = FALSE;

  if ( rle && nn > 0 )
  {
    int32 val, npairs, repeat = 0;
    int32 runs[100], x0 = x;

    while ( (nr.ptr - rle)*2 + (nr.nib ? 1 : 0 ) < nn )
    {
      val = rs_read(&nr, RS_NIBBLE);

      switch ( val )
      {
        case 0:
          break;
        case 1: case 2: case 3: case 4: case 5: case 6:
        case 7: case 8: case 9: case 10: case 11:
          npairs = val;
          for ( val = 0; val < npairs*2; val++ )
          {
            runs[val] = rs_read(&nr, RS_VAL4);
            if ( val & 1 )
              paintRun(rss, rss->colori, x, y, runs[val]);
            x += runs[val];
          }
          x = x0;
          y++;
          break;
        case 12:
          break;
        case 13:
          break;
        case 14:
          repeat = rs_read(&nr, RS_VAL4);
          while ( repeat-- > 0 )
          {
            for ( val = 0; val < npairs*2; val++ )
            {
              if ( val & 1 )
                paintRun(rss, rss->colori, x, y, runs[val]);
              x += runs[val];
            }
            x = x0;
            y++;
          }
          break;
        case 15:
          npairs = 0;
          x = x0;
          y++;
          break;
      }
    }
  }
}

void rs_bandinit(RSS *rss)
{
  uint8 *bg = rss->rs_page_block->bg;
  int32 i;

  rss->rlemode = RS_ABUTTING;
  rss->x = rss->y = 0;
  for ( i=0; i<256; i++ )
  {
    rss->color[i].r = bg[0];
    rss->color[i].g = bg[1];
    rss->color[i].b = bg[2];
  }
  rss->color[0].r = rss->color[0].g = rss->color[0].b = 0;
  rss->color[1].r = rss->color[1].g = rss->color[1].b = 0;
  rss->color[2].r = rss->color[2].g = rss->color[2].b = 255;
  rss->colori = 1;
}

int rs_do_band(RSS *rss)
{
  RS_BAND_BLOCK *band = &(rss->band);
  NIBREAD nr;

  nr.nib = FALSE;
  rs_bandinit(rss);

  if ( rss->dst == NULL )
  {
    printf("  band %d -> %d: %d bytes of band cmds ...\n", band->y0, band->y1,
        band->size);
  }
  for ( nr.ptr = band->cmds ; nr.ptr < band->cmds + band->size; )
  {
    uint8 ch = *nr.ptr++;

    if ( ch & 0x80 ) /* escape command */
    {
      switch ( ch & 0x7F )
      {
        case 0: /* set (x,y) position */
          rss->x = rs_read(&nr, RS_DBYTE);
          rss->y = rs_read(&nr, RS_UNS8);
          if ( rss->dst == NULL )
            printf("    moveto (%d,%d)\n", rss->x, rss->y);
          break;
        case 1: /* Define a color */
        {
          uint8 colori, red, green, blue;

          colori = rs_read(&nr, RS_BYTE);
          red    = rs_read(&nr, RS_BYTE);
          green  = rs_read(&nr, RS_BYTE);
          blue   = rs_read(&nr, RS_BYTE);

          rss->color[colori].r = red;
          rss->color[colori].g = green;
          rss->color[colori].b = blue;

          if ( rss->dst == NULL )
            printf("    define color[%d] = (%x,%x,%x)\n",
                   colori, red, green, blue);
          break;
        }
        case 2: /* Set current color */
        {
          uint8 colori = rs_read(&nr, RS_BYTE);

          rss->colori = colori;
          if ( rss->dst == NULL )
            printf("    current color = %d\n", colori);
          break;
        }
        case 3: /* Set run length mode to abutting */
          rss->rlemode = RS_ABUTTING;
          if ( rss->dst == NULL )
            printf("    rlemode = ABUTTING\n");
          break;
        case 4: /* Set run length mode to vertical */
          rss->rlemode = RS_VERTICAL;
          if ( rss->dst == NULL )
            printf("    rlemode = VERTICAL\n");
          break;
        case 5: /* Render a rectangle */
        {
          int32 w = rs_read(&nr, RS_UNS8);
          int32 h = rs_read(&nr, RS_UNS8);

          if ( rss->dst == NULL )
            printf("    rectangle %d*%d\n", w, h);
          else
            paintRectangle(rss, w, h);
          break;
        }
        case 6: /* Render a y hull */
        {
          int32 ya = rs_read(&nr, RS_UNS8);
          int32 yb = rs_read(&nr, RS_UNS8);
          int32 i, nleft, nright;
          RS_COORD left[17], right[17];

          nleft = nright = rs_read(&nr, RS_BYTE);
          nleft >>= 4; nright &= 0x0F;
          nleft += 2; nright += 2;
          left[0].x = rs_read(&nr, RS_OFF16);
          left[0].y = rs_read(&nr, RS_OFF16);
          for ( i = 1; i < nleft; i++ )
          {
            left[i].x = left[i-1].x + rs_read(&nr, RS_SIGN8);
            left[i].y = left[i-1].y + rs_read(&nr, RS_UNS8);
          }
          right[0].x = rs_read(&nr, RS_OFF16);
          right[0].y = rs_read(&nr, RS_OFF16);
          for ( i = 1; i < nright; i++ )
          {
            right[i].x = right[i-1].x + rs_read(&nr, RS_SIGN8);
            right[i].y = right[i-1].y + rs_read(&nr, RS_UNS8);
          }

          if ( rss->dst == NULL )
          {
            printf("    y-hull %d->%d", ya, yb);
            printf(" %d:",nleft);
            for ( i = 0; i < nleft; i++ )
              printf("(%d,%d)", left[i].x, left[i].y);
            printf(" %d:",nright);
            for ( i = 0; i < nright; i++ )
              printf("(%d,%d)", right[i].x, right[i].y);
            printf("\n");
          }
          else
            paintHull(rss, ya, yb, nleft, nright, left, right);

          break;
        }
        case 7: /* Render a rectangle of raster data */
        {
          int32 sw = rs_read(&nr, RS_UNS8);
          int32 sh = rs_read(&nr, RS_UNS8);
          int32 tx = rs_read(&nr, RS_UNS8);
          int32 ty = rs_read(&nr, RS_UNS8);
          int32 tw = rs_read(&nr, RS_UNS8);
          int32 th = rs_read(&nr, RS_UNS8);
          int32 dw = rs_read(&nr, RS_UNS8);
          int32 dh = rs_read(&nr, RS_UNS8);
          int32 colorspace, compress, i, bytes;

          colorspace = compress = rs_read(&nr, RS_UNS8);
          colorspace >>= 4; compress &= 0x0F;
          bytes = rs_read(&nr, RS_DBYTE);
          bytes = bytes + 256*256*rs_read(&nr, RS_DBYTE);

          if ( rss->dst == NULL )
          {
            uint8 im;
            int32 i, maxi;

            printf("    contone rectangle %d*%d -> %d*%d %d*%d@(%d,%d) : %d/%d",
                sw,sh,dw,dh,tw,th,tx,ty,colorspace,compress);
            printf(" %d bytes of compressed image data : <", bytes);
            if ( (maxi = bytes) > 50 ) maxi = 50;
            for ( i = 0; i < maxi; i++ )
              printf("%02x", nr.ptr[i]);
            printf("...>\n");
          }
          else
            paintContone(rss,sw,sh,tx,ty,tw,th,dw,dh,colorspace,compress,
                         bytes,nr.ptr);

          nr.ptr += bytes;
          break;
        }
        case 8: /* Render a parallelogram strip of raster data */
          break;
        case 9: /* Render a complete smooth shade triangle */
          break;
        case 10: /* Render runs within a smooth shade triangle */
          break;
        case 11: /* Define a cached raster mask */
          break;
        case 12: /* Render a cached raster mask */
        {
          uint8 id = rs_read(&nr, RS_BYTE);
          int32 x  = rs_read(&nr, RS_OFF16);
          int32 y  = rs_read(&nr, RS_SIGN8);

          if ( rss->dst == NULL )
            printf("    glyph[%d] @ (%d,%d)\n", id, x, y);
          else
            paintGlyph(rss, id, x, y);

          break;
        }
        case 13: /* Override default object type */
          break;
        case 14: /* set overprint */
          break;
        case 127: /* Debug message */
        {
          char message[257];
          int32 i, len = rs_read(&nr, RS_BYTE);

          for ( i = 0; i < len; i++ )
            message[i] = *nr.ptr++;
          message[len] = '\0';

          if ( rss->dst == NULL )
            printf("    message '%s'\n", message);
        }
        break;
        default:
          if ( rss->dst == NULL )
            printf("    unknown cmd '%d'\n", ch);
          return -1;
          break;
      }
    }
    else /* color and a run */
    {
      uint8 color = (ch & 0x70)>>4;
      int32 run = 1 + (ch & 0xF);

      nr.ptr--;
      if ( rss->rlemode == RS_VERTICAL && color == 0 )
        run = rs_read(&nr, RS_DELTA4);
      else
        run = rs_read(&nr, RS_VAL4);

      if ( rss->dst == NULL )
        printf("    color %d : run %d\n", color, run);
      else
        paintRun(rss, color, rss->x, rss->y, run);

      if ( rss->rlemode == RS_VERTICAL )
        rss->y++;
      else
        rss->x += run;
    }
  }
  return 1;
}

void show_page_defines(RSS *rss)
{
  int32 id, nn, w, h;
  NIBREAD nr;
  uint8 *rle;

  for ( id = 0; id < 256; id++ )
  {
    if ( ( rle = find_glyph(rss, id, &nn, &w, &h) ) != NULL && nn > 0 )
    {
      int32 val, npairs, repeat = 0;
      int32 runs[100];

      printf("    glyph[%d] %d*%d %d nibbles : ", id, w, h, nn);
      nr.ptr = rle;
      nr.nib = FALSE;

      while ( (nr.ptr - rle)*2 + (nr.nib ? 1 : 0 ) < nn )
      {
        val = rs_read(&nr, RS_NIBBLE);

        switch ( val )
        {
          case 0:
            printf("VAL4 ");
            break;
          case 1: case 2: case 3: case 4: case 5: case 6:
          case 7: case 8: case 9: case 10: case 11:
            npairs = val;
            printf("%d(",npairs);
            for ( val = 0; val < npairs*2; val++ )
            {
              runs[val] = rs_read(&nr, RS_VAL4);
              if ( val != 0 )
                printf(",");
              printf("%d",runs[val]);
            }
            printf(") ");
            break;
          case 12:
            printf("DELTA4 ");
            break;
          case 13:
            printf("DELTA2p2 ");
            break;
          case 14:
            repeat = rs_read(&nr, RS_VAL4);
            printf("REPEAT(%d) ", repeat);
            break;
          case 15:
            printf("0() ");
            break;
        }
      }
      printf("\n");
    }
  }
}

int rsread(RSS *rss)
{
  int32 size, err = 0;
  RS_JOB_BLOCK  rs_job_block;
  RS_PAGE_BLOCK rs_page_block;

  size = sizeof(rss->rs_id_block);
  if ( fread(&(rss->rs_id_block), 1, size, rss->src) != size )
    return -1;
  if ( rss->rs_id_block.magic[0] != RS_MAGIC0 ||
       rss->rs_id_block.magic[1] != RS_MAGIC1 )
    return -1;
  if ( rss->rs_id_block.version[0] != 0 ||
       rss->rs_id_block.version[1] != 0 )
    return -1;

  size = sizeof(rs_job_block);
  if ( fread(&(rs_job_block), 1, size, rss->src) != size )
    return -1;
  size = rs_job_block.meta_size;
  rss->rs_job_block = (RS_JOB_BLOCK *)malloc(sizeof(RS_JOB_BLOCK) + size);
  if ( rss->rs_job_block == NULL )
    return -1;
  *(rss->rs_job_block) = rs_job_block;
  if ( fread(rss->rs_job_block + 1, 1, size, rss->src) != size )
    return -1;

  size = sizeof(rs_page_block);
  if ( fread(&(rs_page_block), 1, size, rss->src) != size )
    return -1;
  size = rs_page_block.meta_size;
  rss->rs_page_block = (RS_PAGE_BLOCK *)malloc(sizeof(RS_PAGE_BLOCK) + size);
  if ( rss->rs_page_block == NULL )
    return -1;
  *(rss->rs_page_block) = rs_page_block;
  if ( fread(rss->rs_page_block + 1, 1, size, rss->src) != size )
    return -1;

  size = sizeof(rss->page_defines.size);
  if ( fread(&(rss->page_defines.size), 1, size, rss->src) != size )
    return -1;
  size = rss->page_defines.size;
  if ( ( rss->page_defines.ptr = (uint8 *)malloc(size) ) == NULL )
    return -1;
  if ( fread(rss->page_defines.ptr, 1, size, rss->src) != size )
    return -1;

  if ( rss->dst )
  {
    if ( bmp_setup(rss) < 0 )
      return -1;
  }
  else
  {
    printf("Rainstorm stream :\n");
    printf("================\n");
    printf("  magic numbers %x %x\n", rss->rs_id_block.magic[0],
                                      rss->rs_id_block.magic[1]);
    printf("  versions %d %d\n", rss->rs_id_block.version[0],
                                 rss->rs_id_block.version[1]);
    printf("  jobid %d\n", rss->rs_job_block->id);
    printf("  %d bytes of job meta data\n", rss->rs_job_block->meta_size);
    printf("  pageid %d\n", rss->rs_page_block->id);
    printf("  rasterStyle %d\n", rss->rs_page_block->rasterStyle);
    printf("  page dims %d*%d\n", rss->rs_page_block->pageWidth,
                                  rss->rs_page_block->pageHeight);
    printf("  %d colors %d bits\n", rss->rs_page_block->colors,
                                    rss->rs_page_block->bits);
    printf("  background (%x,%x,%x)\n", rss->rs_page_block->bg[0],
                                  rss->rs_page_block->bg[1],
                                  rss->rs_page_block->bg[2]);
    printf("  %d bytes of page meta data\n", rss->rs_page_block->meta_size);
    printf("  %d bytes of page defines\n", rss->page_defines.size);
    show_page_defines(rss);
    printf("  ....\n");
  }

  while ( fread(&size, 1, sizeof(int32), rss->src) == sizeof(int32) &&
          size != -1 )
  {
    rss->band.y0 = size;
    fread(&(rss->band.y1), 1, sizeof(int32), rss->src);
    fread(&(rss->band.size), 1, sizeof(int32), rss->src);

    if ( (size = rss->band.size) > 0 )
    {
      rss->band.cmds = malloc(size);
      if ( fread(rss->band.cmds, 1, size, rss->src) != size )
        return -1;

      if ( rs_do_band(rss) < 0 )
      {
        err = -1;
        break;
      }
      free(rss->band.cmds);
    }
  }
  if ( rss->dst == NULL )
    printf("  End of bands\n");
  else
    fwrite(rss->pageRaster, 1, rss->rasterWidth*rss->rasterHeight, rss->dst);

  if ( err < 0 )
    return err;

  if ( fread(&size, 1, sizeof(int32), rss->src) != sizeof(int32) || size != -1 )
  {
    printf("  More than 1 page ?\n");
    return -1;
  }
  if ( rss->dst == NULL )
    printf("  End of page\n");
  return 1;
}

void main(int ac, char *av[])
{
  int err = 0;
  RSS rss;

  init_catch();

  rss.src = rss.dst = NULL;

  if ( ac == 3 && strcmp(av[1],"-l") == 0 &&
       (rss.src = fopen(av[2], "rb")) != NULL )
  {
    err = rsread(&rss);
  }
  else if ( ac == 4 && strcmp(av[1],"-c") == 0 &&
       (rss.src = fopen(av[2], "rb")) != NULL &&
       (rss.dst = fopen(av[3], "wb")) != NULL )
  {
    err = rsread(&rss);
  }
  else
  {
    fprintf(stderr, "List contents of a Rainstorm file\n");
    fprintf(stderr, "    usage : rsread -l rs-filename\n");
    fprintf(stderr, "Convert Rainstorm file to .bmp format\n");
    fprintf(stderr, "    usage : rsread -c rs-filename bmp-filename\n");
  }
  if ( rss.src )
    fclose(rss.src);
  if ( rss.dst )
    fclose(rss.dst);
  if ( err < 0 )
    fprintf(stderr, "!! Rainstorm read failed\n");
}

#endif /* BUILD_RSREAD } */

/* ==========================================================================
 *
* Log stripped */
