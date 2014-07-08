/** \file
 * \ingroup jbig
 *
 * $HopeName: COREjbig!src:jbig2i.c(EBDSDK_P.1) $
 *
 * Copyright (C) 2002-2011 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Core JBIG2 implementation.
 * Exposes an internal interface which is used by the API layer to provide
 * required JBIG2 functionality. In turn, it relies on the API layer to
 * provide implementations of the basic memory and I/O library services
 * required. This means this code can be kept platform and environment
 * independent.
 */
/** \todo BMJ 27-Sep-07 :  some more general cleanup would be useful to try
 * and make the code more understandable and reliable :-
 *  + get rid of many of the gotos
 *  + use some local Bool type
 *  + More error checking in the low-level functions
 *  + outer for(;;) loops in many functions are a confusing construct
 *  + main function too big, needs re-factoring
 */

#include "jbig2i.h"

#define schar signed char
#define uchar unsigned char

/*
 * Strings encoding the standard Huffman tables.
 */
static uchar jhtA[] = {
  0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01,
  0x10, 0x49, 0x23, 0x81, 0x80,
};


static uchar jhtB[] = {
  0x25, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x4b, 0x21, 0x06, 0x23, 0xb8, 0x6c,
};


static uchar jhtC[] = {
  0x37, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
  0x4b, 0x88, 0x10, 0x20, 0x30, 0x43, 0x56, 0x87,
  0x60,
};


static uchar jhtD[] = {
  0x24, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x4c, 0x21, 0x06, 0x23, 0xb8, 0x50,
};


static uchar jhtE[] = {
  0x34, 0xff, 0xff, 0xff, 0x01, 0x00, 0x00, 0x00,
  0x4c, 0xf0, 0x41, 0x03, 0x08, 0x75, 0xbe,
};


static uchar jhtF[] = {
  0x34, 0xff, 0xff, 0xf8, 0x00, 0x00, 0x00, 0x08,
  0x00, 0xb5, 0x26, 0x44, 0x7a, 0xd5, 0x62, 0xa7,
  0x6e, 0xe2, 0x4c, 0xad, 0x80,
};


static uchar jhtG[] = {
  0x34, 0xff, 0xff, 0xfc, 0x00, 0x00, 0x00, 0x08,
  0x00, 0x92, 0xe2, 0x3d, 0x6a, 0xb1, 0x62, 0xd5,
  0xad, 0x1d, 0xc3, 0x97, 0x56, 0x80,
};


static uchar jhtH[] = {
  0x37, 0xff, 0xff, 0xff, 0xf1, 0x00, 0x00, 0x06,
  0x86, 0x83, 0x91, 0x81, 0x90, 0x70, 0x40, 0x21,
  0x50, 0x60, 0x34, 0x61, 0x44, 0x45, 0x56, 0x57,
  0x67, 0x78, 0x6a, 0x99, 0x20,
};


static uchar jhtI[] = {
  0x37, 0xff, 0xff, 0xff, 0xe1, 0x00, 0x00, 0x0d,
  0x0b, 0x84, 0x92, 0x82, 0x91, 0x71, 0x41, 0x31,
  0x31, 0x51, 0x61, 0x35, 0x62, 0x45, 0x46, 0x57,
  0x58, 0x68, 0x79, 0x6b, 0x99, 0x20,
};


static uchar jhtJ[] = {
  0x37, 0xff, 0xff, 0xff, 0xeb, 0x00, 0x00, 0x10,
  0x46, 0x74, 0x80, 0x70, 0x50, 0x22, 0x50, 0x60,
  0x70, 0x80, 0x26, 0x55, 0x65, 0x66, 0x67, 0x68,
  0x69, 0x6a, 0x7b, 0x88, 0x20,
};


static uchar jhtK[] = {
  0x24, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x8d, 0x21, 0x18, 0x21, 0xa6, 0xac, 0xba, 0xef,
  0xcf, 0x7e, 0x1c,
};


static uchar jhtL[] = {
  0x26, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x49, 0x10, 0x40, 0xca, 0x85, 0x2c, 0x5c, 0x39,
  0x74, 0xed, 0xe4, 0x50, 0x80,
};


static uchar jhtM[] = {
  0x24, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
  0x8d, 0x21, 0x88, 0x28, 0x85, 0xbc, 0x72, 0xcf,
  0x4d, 0x7e, 0x1c,
};


static uchar jhtN[] = {
  0x02, 0xff, 0xff, 0xff, 0xfe, 0x00, 0x00, 0x00,
  0x03, 0xd9, 0x6c, 0x00,
};


static uchar jhtO[] = {
  0x24, 0xff, 0xff, 0xff, 0xe8, 0x00, 0x00, 0x00,
  0x19, 0xf3, 0x2a, 0x60, 0x60, 0x86, 0x20, 0xa7,
  0x2f, 0x3f,
};


static uchar *jhts[] = {
  jhtA, jhtB, jhtC, jhtD, jhtE, jhtF, jhtG, jhtH,
  jhtI, jhtJ, jhtK, jhtL, jhtM, jhtN, jhtO,
};

#define J2_NUMSTDHTS (sizeof(jhts) / sizeof(jhts[0]))

/*
 * IDs for the arithmetic integer decoding contexts.
 */
#define J2_IAAI 0
#define J2_IADH 1
#define J2_IADS 2
#define J2_IADT 3
#define J2_IADW 4
#define J2_IAEX 5
#define J2_IAFS 6
#define J2_IAIT 7
#define J2_IARDH 8
#define J2_IARDW 9
#define J2_IARDX 10
#define J2_IARDY 11
#define J2_IARI 12

#define J2_NUMIA 13

#define J2_IAID 13

#define MASK(x) (1 << (x))
#define MASK_ALL 0x3fff

#define J2_IA_CXBYTES 512

/*
 * The JBIG2 coding means that we will need to read a mixture of bits
 * and bytes from the input stream. So create a structure to unify both
 * access methods, so that bits/bytes don't get out of sync.
 * Also, JBIG2 coding includes a peek ahead at the input stream, and
 * may require up to two bytes to be pushed back into the input stream.
 * ungetc() will not work pushing two bytes back, if it happens across
 * a I/O buffer boundary. So need to maintain our own local pushback
 * information for these two bytes.
 */
typedef struct J2BITSTREAM
{
  J2STREAM *source; /* The abstract input stream */
  int bits;         /* #bits remaining in the current byte to be used */
  int putback;      /* how many bytes have been pushed back */
  uchar lastch;     /* the last byte read, if we have not used all its bits */
  uchar back[2];    /* up two two bytes pushed back */
} J2BITSTREAM;

typedef struct J2FSTATE
{
  unsigned int c, a;
  uchar  *str;
  int huff;
  J2BITSTREAM in;
} J2FSTATE;

#define J2_IA_ICX(cx) (cx >> 1)
#define J2_IA_MPSCX(cx) (cx & 1)
#define J2_IA_CX(icx, mpscx) ((icx << 1) + mpscx)

static int jatemplatebits[] = { 16, 13, 13, 10 };

typedef struct J2HUFFENT
{
  int off, max;
} J2HUFFENT;

typedef struct J2HUFFVAL
{
  int length, offset;
} J2HUFFVAL;

#define J2OOB -1 /* JBIG2 "Out Of Bounds" value */

typedef struct J2HTAB
{
  int oob;
  int low;
  int n;
  J2HUFFVAL *vals;
} J2HTAB;

typedef struct J2BITMAP
{
  int w, h;
  int left, stride;
  uchar *base;
  int free_it;
} J2BITMAP;

#define J2_ROP_OR 0
#define J2_ROP_AND 1
#define J2_ROP_XOR 2
#define J2_ROP_XNOR 3
#define J2_ROP_REPLACE 4

#define J2_SEGFLAGS_PAFS 0x40
#define J2_SEGFLAGS_TYPE 0x3f
#define J2_SEGFLAGS_PRIMARY(x) (((x) >> 4) & 3)
#define J2_PRIMARY_METADATA 3
#define J2_SEGFLAGS_SECONDARY(x) (((x) >> 2) & 3)
#define J2_SECONDARY_DICTIONARY 0
#define J2_SEGFLAGS_IMMEDIATE 2
#define J2_SEGFLAGS_LOSSLESS 1

typedef struct J2SEGMENT
{
  struct J2SEGMENT *next;
  int number;
  int pageno;
  int length, used;
  uchar flags;
} J2SEGMENT;

#define J2_SEGTYPE_SYMBOL_DICT 0

typedef struct J2SDSEGMENT
{
  J2SEGMENT segment;
  int sdflags;
  schar atflags[8];
  int nimported;
  int nexported;
  int nnew;
  int dontfree;
  J2BITMAP *symbols;
  J2BITMAP **symptrs;
  uchar *contexts;
  uchar *rcontexts;
} J2SDSEGMENT;

#define J2_SEGTYPE_PATTERN_DICT 16

typedef struct J2PDSEGMENT
{
  J2SEGMENT segment;
  int pdflags;
  int n;
  J2BITMAP patterns;
} J2PDSEGMENT;

#define J2_SEGTYPE_TABLE 53

typedef struct J2TABLESEGMENT
{
  J2SEGMENT segment;
  J2HTAB *ht;
} J2TABLESEGMENT;

#define J2_SEGTYPE_TEXT 4
#define J2_SEGTYPE_HTONE 20
#define J2_SEGTYPE_GENERIC 36
#define J2_SEGTYPE_REFINEMENT 40

typedef struct J2REGIONSEGMENT
{
  J2SEGMENT segment;
  J2BITMAP region;
  int bytes;
  int x, y;
  int combop;
} J2REGIONSEGMENT;

#define J2_SEGTYPE_PAGE_INFO 48
#define J2_SEGTYPE_END_OF_PAGE 49
#define J2_SEGTYPE_END_OF_STRIPE 50

#define J2_PAGEDEFPIXEL(x) (((x) >> 2) & 1)
#define J2_PAGECOMBOP(x) (((x) >> 3) & 3)

typedef struct J2PRIVATE
{
  J2STREAM *inputstream;
  J2STREAM *globals;
  int stage;
  J2SEGMENT segh;
  J2SEGMENT *segments;
  int nreferred;
  J2SEGMENT **referred;
  uchar *retain;
  int segind;
  uchar eod;
  uchar inpage;
  uchar endofstripeseen;
  uchar had_file_header;
  J2FSTATE fstate;
  uchar iacontexts[J2_NUMIA][J2_IA_CXBYTES];
  int iaidcodelen;
  uchar *iaidcontexts;
  J2HTAB *hufftables[J2_NUMIA];
  J2HTAB *stdhufftables[J2_NUMSTDHTS];
  int pageflags;
  int pagew, pageh, maxstripe, stripetop;
  J2BITMAP pagebuffer;
  J2REGIONSEGMENT currentregion;
  uchar *savebuf;
  int savebufsize;
  struct
  {
    uchar *ptr;
    int count;
  } extra;
} J2PRIVATE;

/*
 * Various tables for arithmetic decoding.
 *
 * We can encode both the nlps and switch transitions in a single table
 */
static unsigned int qetab[] = {
  0x56010000, 0x34010000, 0x18010000, 0x0ac10000,
  0x05210000, 0x02210000, 0x56010000, 0x54010000,
  0x48010000, 0x38010000, 0x30010000, 0x24010000,
  0x1c010000, 0x16010000, 0x56010000, 0x54010000,
  0x51010000, 0x48010000, 0x38010000, 0x34010000,
  0x30010000, 0x28010000, 0x24010000, 0x22010000,
  0x1c010000, 0x18010000, 0x16010000, 0x14010000,
  0x12010000, 0x11010000, 0x0ac10000, 0x09c10000,
  0x08a10000, 0x05210000, 0x04410000, 0x02a10000,
  0x02210000, 0x01410000, 0x01110000, 0x00850000,
  0x00490000, 0x00250000, 0x00150000, 0x00090000,
  0x00050000, 0x00010000, 0x56010000,
};

static char nmpstab2[] = {
  (1 << 1) | 0, (1 << 1) | 1, (2 << 1) | 0, (2 << 1) + 1,
  (3 << 1) | 0, (3 << 1) | 1, (4 << 1) | 0, (4 << 1) | 1,
  (5 << 1) | 0, (5 << 1) | 1, (38 << 1) | 0, (38 << 1) | 1,
  (7 << 1) | 0, (7 << 1) | 1, (8 << 1) | 0, (8 << 1) | 1,
  (9 << 1) | 0, (9 << 1) | 1, (10 << 1) | 0, (10 << 1) | 1,
  (11 << 1) | 0, (11 << 1) | 1, (12 << 1) | 0, (12 << 1) | 1,
  (13 << 1) | 0, (13 << 1) | 1, (29 << 1) | 0, (29 << 1) | 1,
  (15 << 1) | 0, (15 << 1) | 1, (16 << 1) | 0, (16 << 1) | 1,
  (17 << 1) | 0, (17 << 1) | 1, (18 << 1) | 0, (18 << 1) | 1,
  (19 << 1) | 0, (19 << 1) | 1, (20 << 1) | 0, (20 << 1) | 1,
  (21 << 1) | 0, (21 << 1) | 1, (22 << 1) | 0, (22 << 1) | 1,
  (23 << 1) | 0, (23 << 1) | 1, (24 << 1) | 0, (24 << 1) | 1,
  (25 << 1) | 0, (25 << 1) | 1, (26 << 1) | 0, (26 << 1) | 1,
  (27 << 1) | 0, (27 << 1) | 1, (28 << 1) | 0, (28 << 1) | 1,
  (29 << 1) | 0, (29 << 1) | 1, (30 << 1) | 0, (30 << 1) | 1,
  (31 << 1) | 0, (31 << 1) | 1, (32 << 1) | 0, (32 << 1) | 1,
  (33 << 1) | 0, (33 << 1) | 1, (34 << 1) | 0, (34 << 1) | 1,
  (35 << 1) | 0, (35 << 1) | 1, (36 << 1) | 0, (36 << 1) | 1,
  (37 << 1) | 0, (37 << 1) | 1, (38 << 1) | 0, (38 << 1) | 1,
  (39 << 1) | 0, (39 << 1) | 1, (40 << 1) | 0, (40 << 1) | 1,
  (41 << 1) | 0, (41 << 1) | 1, (42 << 1) | 0, (42 << 1) | 1,
  (43 << 1) | 0, (43 << 1) | 1, (44 << 1) | 0, (44 << 1) | 1,
  (45 << 1) | 0, (45 << 1) | 1, (45 << 1) | 0, (45 << 1) | 1,
  (46 << 1) | 0, (46 << 1) | 1,
};

static char nlpstab2[] = {
  (1 << 1) | 1, (1 << 1) | 0, (6 << 1) | 0, (6 << 1) | 1,
  (9 << 1) | 0, (9 << 1) | 1, (12 << 1) | 0, (12 << 1) | 1,
  (29 << 1) | 0, (29 << 1) | 1, (33 << 1) | 0, (33 << 1) | 1,
  (6 << 1) | 1, (6 << 1) | 0, (14 << 1) | 0, (14 << 1) | 1,
  (14 << 1) | 0, (14 << 1) | 1, (14 << 1) | 0, (14 << 1) | 1,
  (17 << 1) | 0, (17 << 1) | 1, (18 << 1) | 0, (18 << 1) | 1,
  (20 << 1) | 0, (20 << 1) | 1, (21 << 1) | 0, (21 << 1) | 1,
  (14 << 1) | 1, (14 << 1) | 0, (14 << 1) | 0, (14 << 1) | 1,
  (15 << 1) | 0, (15 << 1) | 1, (16 << 1) | 0, (16 << 1) | 1,
  (17 << 1) | 0, (17 << 1) | 1, (18 << 1) | 0, (18 << 1) | 1,
  (19 << 1) | 0, (19 << 1) | 1, (19 << 1) | 0, (19 << 1) | 1,
  (20 << 1) | 0, (20 << 1) | 1, (21 << 1) | 0, (21 << 1) | 1,
  (22 << 1) | 0, (22 << 1) | 1, (23 << 1) | 0, (23 << 1) | 1,
  (24 << 1) | 0, (24 << 1) | 1, (25 << 1) | 0, (25 << 1) | 1,
  (26 << 1) | 0, (26 << 1) | 1, (27 << 1) | 0, (27 << 1) | 1,
  (28 << 1) | 0, (28 << 1) | 1, (29 << 1) | 0, (29 << 1) | 1,
  (30 << 1) | 0, (30 << 1) | 1, (31 << 1) | 0, (31 << 1) | 1,
  (32 << 1) | 0, (32 << 1) | 1, (33 << 1) | 0, (33 << 1) | 1,
  (34 << 1) | 0, (34 << 1) | 1, (35 << 1) | 0, (35 << 1) | 1,
  (36 << 1) | 0, (36 << 1) | 1, (37 << 1) | 0, (37 << 1) | 1,
  (38 << 1) | 0, (38 << 1) | 1, (39 << 1) | 0, (39 << 1) | 1,
  (40 << 1) | 0, (40 << 1) | 1, (41 << 1) | 0, (41 << 1) | 1,
  (42 << 1) | 0, (42 << 1) | 1, (43 << 1) | 0, (43 << 1) | 1,
  (46 << 1) | 0, (46 << 1) | 1,
};

/*
 * There are just too many of these.
 */
typedef struct J2TEXTPARAMS
{
  int sbhuff;
  int sbrefine;
  int logsbstrips;
  int sbstrips;
  int sbnuminstances;
  int sbnumsyms;
  J2HTAB *sidht;
  int sbsymcodelen;
  J2BITMAP **symptrs;
  int transposed;
  int refcorner;
  int sbdsoffset;
  J2HTAB *sbhufffs;
  J2HTAB *sbhuffds;
  J2HTAB *sbhuffdt;
  J2HTAB *sbhuffrdw;
  J2HTAB *sbhuffrdh;
  J2HTAB *sbhuffrdx;
  J2HTAB *sbhuffrdy;
  J2HTAB *sbhuffrsize;
  int sbtemplate;
  schar sbrat[4];
  J2BITMAP *dest;
  int rop;
  uchar *rcontexts;
} J2TEXTPARAMS;

/*
 * Read a 4byte word from memory.
 */
static int j2word32(uchar *data)
{
  return (int)((data[0]<<24)|(data[1]<<16)|(data[2]<<8)|(data[3]));
}

/*
 * Get and return a single byte from the specificed bitstream.
 */
static int jb2get1byte(J2BITSTREAM *j2s)
{
  /* JB2ASSERT(j2s->bits == 0,"JBIG2 bitstream out of sync"); */

  if ( j2s->putback > 0 )
  {
    int ch = j2s->back[0];

    JB2ASSERT(j2s->putback <= 2,"JBIG2 putback more than 2 chars");
    if ( --(j2s->putback) > 0 )
      j2s->back[0] = j2s->back[1];
    return ch;
  }
  else
    return jb2getc(j2s->source);
}

/*
 * Get the specified number of bytes from the given JBIG2 stream,
 * and put the result in the memory referenced by 'ptr'.
 * Return -1 on EOF, else +1.
 * Note : if nbytes is negative, this indicates a word of the given size
 * should be read instead.
 */
static int j2getbytes(J2BITSTREAM *j2s, int nbytes, int *ptr)
{
  int i, ch;
  uchar c[4];

  switch(nbytes)
  {
    case -1:
      if ( (ch = jb2get1byte(j2s)) < 0 )
        return -1;
      *ptr = ch;
      break;
    case -2:
      for ( i = 0; i < 2; i++ )
      {
        if ( (ch = jb2get1byte(j2s)) < 0 )
          return -1;
        c[i] = (uchar)ch;
      }
      *ptr = ((c[0]<<8)|(c[1]));
      break;
    case -4:
      for ( i = 0; i < 4; i++ )
      {
        if ( (ch = jb2get1byte(j2s)) < 0 )
          return -1;
        c[i] = (uchar)ch;
      }
      *ptr =  j2word32(c);
      break;
    default:
    {
      uchar *cptr = (uchar *)ptr;

      for ( i = 0; i < nbytes; i++ )
      {
        if ( (ch = jb2get1byte(j2s)) < 0 )
          return -1;
        *cptr++ = (uchar)ch;
      }
      break;
    }
  }
  return 1;
}

/*
 * Read the requested number of bytes from the specified JBIG2 segment.
 */
static int j2segbytes(J2BITSTREAM *j2s, J2SEGMENT *seg, int nbytes, int *ptr)
{
  int n, err;

  if ( (n = nbytes ) < 0 )
    n = -n;

  /**
   * Check to see that we are not trying to get more bytes from the segment
   * than the header claims are present. However, cannot do this test if
   * the segment was marked as of unknown length (i.e. length < 0 ).
   */
  if ( seg->length >=0 && seg->used + n > seg->length )
    return -2;
  if ( (err = j2getbytes(j2s, nbytes, ptr)) < 0 )
    return err;
  seg->used += n;
  return err;
}

/*
 * Resets some or all of the arithmetic integer decoding
 * contexts, and clears the array of Huffman tables.
 */
static void j2inittables(J2PRIVATE *j2priv)
{
  int j;

  for (j = 0; j < J2_NUMIA; j++)
  {
    jb2zero((char *)j2priv->iacontexts[j], J2_IA_CXBYTES);
  }
  jb2zero((char *)j2priv->hufftables, J2_NUMIA * sizeof(J2HTAB *));
  if (j2priv->iaidcodelen)
    jb2zero((char *)j2priv->iaidcontexts, 1 << j2priv->iaidcodelen);
}

/*
 * Implement the BYTEIN logic as described in the documentation
 * section E.3.4, figure E.19
 */
static int j2bytein(J2FSTATE *fsp, unsigned int *c_val, int init)
{
  J2BITSTREAM *j2s = &(fsp->in);
  uchar ch, cp[2];
  int used = 0;
  unsigned int c;

  /*
   * NAFF ALERT - making the arithmetic coding
   * restartable is so much hassle that it really
   * is easier to call refill from the leaf routines.
   */
  if ( j2getbytes(j2s, 2, (int *)cp) < 0 )
    return (-2);

  ch = cp[0];
  if ( init )
    c = ch << 16;
  else
    c = *c_val;

  if (ch == 0xff)
  {
    if (cp[1] > 0x8f)
    {
      c += ch << 8;
      j2s->bits = 8;
    }
    else
    {
      ch = cp[1];
      used = 1;
      c += ch << 9;
      j2s->bits = 7;
    }
  }
  else
  {
    ch = cp[1];
    used = 1;
    c += ch << 8;
    j2s->bits = 8;
  }
  JB2ASSERT(j2s->putback == 0,"JBIG2 too many bytes putback\n");
  if ( used == 0 )
  {
    j2s->putback = 2;
    j2s->back[0] = cp[0];
    j2s->back[1] = cp[1];
  }
  else if ( used == 1 )
  {
    j2s->putback = 1;
    j2s->back[0] = cp[1];
  }
  *c_val = c;
  return 1;
}

/*
 * Loads the C and A registers at the start of an encoded bit stream.
 */
static int j2seedia(J2FSTATE *fsp)
{
  unsigned int c;

  if ( j2bytein(fsp, &c, 1) < 0 )
    return (-2);
  fsp->in.bits =  fsp->in.bits - 7;
  fsp->c = c << 7;
  fsp->a = 0x80000000;
  return (1);
}


/*
 * Initialise the state of the specified bitstream
 */
static void j2initbitstream(J2BITSTREAM *j2s, J2STREAM *source)
{
  j2s->bits = 0;
  j2s->source = source;
  j2s->lastch = 0;
  j2s->putback = 0;
  j2s->back[0] = j2s->back[1] = 0;
}

/**
 * Throw away data at the end of a bitstream.
 * Start by throwing away any unused bits in the byte.
 * Then if required, see if the terminating marker <FFAC> is present,
 * and if so throw it away too (see documentation, section 7.2.7 ).
 * \param[in,out]  fsp    state structure pointer
 * \param[in]      ffac   ?
 * \param[in]      lunk   Is the segment length unknown
 *
 * \todo BMJ 27-Sep-07 :  should be able to return errors from here
 */
static void j2skipbitstream(J2FSTATE *fsp, int ffac, int lunk)
{
  J2BITSTREAM *j2s = &(fsp->in);

  j2s->bits = 0;
  if ( ffac )
  {
    uchar cp[4];

    if ( j2getbytes(j2s, 2, (int *)cp) < 0 )
      return;
    if ( cp[0] == 0xff && cp[1] == 0xac )
    {
      /*
       * Throw away the EOD marker 0xFFAC.
       * If the segment length is unknown, need to throw away the
       * next four bytes (row count) as well
       */
      if ( lunk )
        (void)j2getbytes(j2s, 4, (int *)cp);
    }
    else
    {
      JB2ASSERT(j2s->putback == 0,"JBIG2 too many bytes putback\n");
      j2s->putback = 2;
      j2s->back[0] = cp[0];
      j2s->back[1] = cp[1];
    }
  }
}

/*
 * Initialise the J2FSTATE structure ready for reading a stream of bits.
 */
static int j2setfstate(J2FSTATE *fsp, int huff)
{
  J2BITSTREAM *j2s = &(fsp->in);

  fsp->huff = huff;
  JB2ASSERT(j2s->bits == 0,"JBIG2 bitstream out of sync\n");
  j2initbitstream(j2s, j2s->source);
  if (!huff)
    return (j2seedia(fsp));
  else
    return (1);
}

/**
 * Finished with the J2FSTATE structure.
 * \param[in,out]  fsp    state structure pointer
 * \param[in]      huff   ?
 * \param[in]      lunk   Is the segment length unknown
 */
static void j2unsetfstate(J2FSTATE *fsp, int huff, int lunk)
{
  j2skipbitstream(fsp, !huff, lunk);
}

/*
 * Decodes a single bit using arithmetic coding.
 */
static int j2iabit(J2FSTATE *fsp, uchar * contexts, int prev)
{
  unsigned int c, a, qe;
  int state;
  int d;

  state = contexts[prev];
  d = state & 1;
  qe = qetab[state >> 1];
  a = fsp->a - qe;
  c = fsp->c;
  if (c >= qe)
  {
    c -= qe;
    if (a & 0x80000000)
    {
      fsp->c = c;
      fsp->a = a;
      return (d);
    }
    if (a < qe)
    {
      d ^= 1;
      state = nlpstab2[state];
    }
    else
    {
      state = nmpstab2[state];
    }
  }
  else
  {
    if (a < qe)
    {
      state = nmpstab2[state];
    }
    else
    {
      d ^= 1;
      state = nlpstab2[state];
    }
    a = qe;
  }

  contexts[prev] = (uchar)state;
  do
  {
    if (fsp->in.bits == 0)
    {
      if ( j2bytein(fsp, &c, 0) < 0 )
      {
        /** \todo BMJ : 19/04/2007 : sort out return value
         * Calling code does not seem to take any notice of EOF
         * return by this routine, in fact it relies on the last
         * value being returned repeatedly !?
         * return (-2);
         */
         return d;
      }
    }
    a <<= 1;
    c <<= 1;
    fsp->in.bits--;
  } while ((a & 0x80000000) == 0);
  fsp->c = c;
  fsp->a = a;

  return (d);
}

/*
 * Decodes a single value using the specified arithmetic integer
 * decoding procedure.
 */
static int j2readia(J2FSTATE *fsp, uchar * contexts, int *rp)
{
  int prev;
  int d;
  int s, j, len, val;
  static int lengths[] = {
      2, 4, 6, 8, 12, 32,
  };
  static int offsets[] = {
      0, 4, 20, 84, 340, 4436,
  };

  s = j2iabit(fsp, contexts, 1);
  prev = 2 | s;

  for (j = 0; j < 5; j++)
  {
    d = j2iabit(fsp, contexts, prev);
    prev = (prev << 1) | d;
    if (!d)
      break;
  }

  val = 0;
  for (len = lengths[j]; len > 0; len--)
  {
    d = j2iabit(fsp, contexts, prev);
    prev = (prev << 1) | d;
    if (prev >= 512)
      prev = (prev & 511) | 256;
    val = (val << 1) | d;
  }
  val += offsets[j];
  if (s == 1 && val == 0)
      return (-1);
  *rp = s ? -val : val;
  return (1);
}

/*
 * Decodes a single value using the IAID decoding procedure.
 */
static int j2readiaid(J2FSTATE *fsp, uchar * contexts, int codelen, int *rp)
{
  int prev, j, d;

  prev = 1;

  for (j = 0; j < codelen; j++)
  {
    d = j2iabit(fsp, contexts, prev);
    prev = (prev << 1) | d;
  }
  *rp = prev & ~(1 << codelen);;
  return (1);
}

static int sltpcontexts[] = {
  0x9b25, 0x795, 0x0e5, 0x195,
};

#define JBITS(_base, _bitaddr, _shft) ((((_base)[(_bitaddr) >> 3] >> \
                                        (7 - ((_bitaddr) & 7))) & 1) << _shft)

/*
 * Decodes a bitmap using arithmetic coding.
 */
static int j2readregion(J2FSTATE *fsp, J2BITMAP *dest, int template,
                        int tpgdon, uchar * contexts, schar * atflags,
                        uchar * skip)
{
  uchar *dp;
  int d;
  int x, y, bitaddr, dbitaddr, atx, aty;
  int j;
  int ltp;
  int thisrow;
  int nominal = 0;
  int prev, prevmask = 0;
  static schar nom0[] = { 3, -1, -3, -1, 2, -2, -2, -2 };
  static int atshifts[] = { 4, 10, 11, 15 };
  uchar *base;
  int w, h, stride, bitstride;

  base = dest->base;
  w = dest->w;
  h = dest->h;
  stride = dest->stride;
  bitstride = 8 * stride;

  switch (template)
  {
    case 0:
      nominal = !jb2cmp((char *)atflags, (char *)nom0, sizeof(nom0));
      prevmask = nominal ? 0xf7ef : 0x63cf;
      break;

    case 1:
      nominal = (atflags[0] == 3 && atflags[1] == -1);
      prevmask = nominal ? 0x1df7 : 0x1de7;
      break;

    case 2:
      nominal = (atflags[0] == 2 && atflags[1] == -1);
      prevmask = nominal ? 0x37b : 0x373;
      break;

    case 3:
      nominal = (atflags[0] == 2 && atflags[1] == -1);
      prevmask = nominal ? 0x3ef : 0x3cf;
      break;
  }

  prev = 0;
  ltp = 0;

  y = dbitaddr = 0;
  dp = base;
  do
  {
    if (tpgdon)
    {
      ltp ^= j2iabit(fsp, contexts, sltpcontexts[template]);
      if (ltp)
      {
        if (y == 0)
          jb2zero((char *)dp, stride);
        else
          jb2copy((char *)dp - stride, (char *)dp, stride);
        dp += stride;
        dbitaddr += bitstride;
        y++;
        continue;
      }
    }
    if (y > 0)
    {
      switch (template)
      {
        case 0:
          if (nominal)
          {
            prev = dp[-stride] & 0xf0;
            if (y > 1)
              prev |= (dp[-2 * stride] << 6) & 0x3800;
          }
          else
          {
            prev = dp[-stride] & 0xe0;
            if (y > 1)
              prev |= (dp[-2 * stride] << 6) & 0x3000;
            for (j = 0; j < 4; j++)
            {
              atx = atflags[2 * j];
              aty = y + atflags[2 * j + 1];
              if ((unsigned)atx < (unsigned)w && (unsigned)aty < (unsigned)h)
              {
                bitaddr = aty * bitstride + atx;
                if (bitaddr < dbitaddr)
                  prev |= JBITS(base, bitaddr, atshifts[j]);
              }
            }
          }
          break;

        case 1:
          if (nominal)
          {
            prev = (dp[-stride] >> 1) & 0x78;
            if (y > 1)
              prev |= (dp[-2 * stride] << 4) & 0xe00;
          }
          else
          {
            prev = (dp[-stride] >> 1) & 0x70;
            if (y > 1)
              prev |= (dp[-2 * stride] << 4) & 0xe00;
            atx = atflags[0];
            aty = y + atflags[1];
            if ((unsigned)atx < (unsigned)w && (unsigned)aty < (unsigned)h)
            {
              bitaddr = aty * bitstride + atx;
              if (bitaddr < dbitaddr)
                prev |= JBITS(base, bitaddr, 3);
            }
          }
          break;

        case 2:
          if (nominal)
          {
            prev = (dp[-stride] >> 3) & 0x1c;
            if (y > 1)
              prev |= (dp[-2 * stride] << 1) & 0x180;
          }
          else
          {
            prev = (dp[-stride] >> 3) & 0x18;
            if (y > 1)
              prev |= (dp[-2 * stride] << 1) & 0x180;
            atx = atflags[0];
            aty = y + atflags[1];
            if ((unsigned)atx < (unsigned)w && (unsigned)aty < (unsigned)h)
            {
              bitaddr = aty * bitstride + atx;
              if (bitaddr < dbitaddr)
                prev |= JBITS(base, bitaddr, 2);
            }
          }
          break;

        case 3:
          if (nominal)
          {
            prev = (dp[-stride] >> 1) & 0x70;
          }
          else
          {
            prev = (dp[-stride] >> 1) & 0x60;
            atx = atflags[0];
            aty = y + atflags[1];
            if ((unsigned)atx < (unsigned)w && (unsigned)aty < (unsigned)h)
            {
              bitaddr = aty * bitstride + atx;
              if (bitaddr < dbitaddr)
                prev |= JBITS(base, bitaddr, 4);
            }
          }
          break;
      }
    }

    x = thisrow = 0;
    while (x < w - 1)
    {
      if (skip && (skip[stride * y + (x >> 3)] << (x & 7)) & 0x80)
        d = 0;
      else
        d = j2iabit(fsp, contexts, prev);
      thisrow = (thisrow << 1) | d;

      dbitaddr++;
      x++;
      if ((x & 7) == 0)
        *dp++ = (uchar)thisrow;

      prev = ((prev << 1) | d) & prevmask;
      if (!nominal)
        dp[0] = (uchar)(thisrow << (8 - (x & 7)));
      switch (template)
      {
        case 0:
          if (nominal)
          {
            if (y > 0)
            {
              if (x < w - 3)
              {
                bitaddr = dbitaddr + 3 - bitstride;
                prev |= JBITS(base, bitaddr, 4);
              }
              if (x > 2)
              {
                bitaddr = dbitaddr - 3 - bitstride;
                prev |= JBITS(base, bitaddr, 10);
              }
              if (y > 1)
              {
                if (x < w - 2)
                {
                  bitaddr = dbitaddr + 2 - 2 * bitstride;
                  prev |= JBITS(base, bitaddr, 11);
                }
                if (x > 1)
                {
                  bitaddr = dbitaddr - 2 - 2 * bitstride;
                  prev |= JBITS(base, bitaddr, 15);
                }
              }
            }
          }
          else
          {
            if (y > 0)
            {
              if (x < w - 2)
              {
                bitaddr = dbitaddr + 2 - bitstride;
                prev |= JBITS(base, bitaddr, 5);
              }
              if (y > 1)
              {
                if (x < w - 1)
                {
                  bitaddr = dbitaddr + 1 - 2 * bitstride;
                  prev |= JBITS(base, bitaddr, 12);
                }
              }
            }
            for (j = 0; j < 4; j++)
            {
              atx = x + atflags[2 * j];
              aty = y + atflags[2 * j + 1];
              if ((unsigned)atx < (unsigned)w && (unsigned)aty < (unsigned)h)
              {
                bitaddr = aty * bitstride + atx;
                if (bitaddr < dbitaddr)
                  prev |= JBITS(base, bitaddr, atshifts[j]);
              }
            }
          }
          break;

        case 1:
          if (nominal)
          {
            if (y > 0)
            {
              if (x < w - 3)
              {
                bitaddr = dbitaddr + 3 - bitstride;
                prev |= JBITS(base, bitaddr, 3);
              }
              if (y > 1 && x < w - 2)
              {
                bitaddr = dbitaddr + 2 - 2 * bitstride;
                prev |= JBITS(base, bitaddr, 9);
              }
            }
          }
          else
          {
            if (y > 0 && x < w - 2)
            {
              bitaddr = dbitaddr + 2 - bitstride;
              prev |= JBITS(base, bitaddr, 4);
            }
            if (y > 1)
            {
              bitaddr = dbitaddr + 2 - 2 * bitstride;
              prev |= JBITS(base, bitaddr, 9);
            }
            atx = x + atflags[0];
            aty = y + atflags[1];
            if ((unsigned)atx < (unsigned)w && (unsigned)aty < (unsigned)h)
            {
              bitaddr = aty * bitstride + atx;
              if (bitaddr < dbitaddr)
                prev |= JBITS(base, bitaddr, 3);
            }
          }
          break;

        case 2:
          if (nominal)
          {
            if (y > 0)
            {
              if (x < w - 2)
              {
                bitaddr = dbitaddr + 2 - bitstride;
                prev |= JBITS(base, bitaddr, 2);
              }
              if (y > 1 && x < w - 1)
              {
                bitaddr = dbitaddr + 1 - 2 * bitstride;
                prev |= JBITS(base, bitaddr, 7);
              }
            }
          }
          else
          {
            if (y > 0 && x < w - 1)
            {
              bitaddr = dbitaddr + 1 - bitstride;
              prev |= JBITS(base, bitaddr, 3);
              if (y > 1)
              {
                bitaddr = dbitaddr + 1 - 2 * bitstride;
                prev |= JBITS(base, bitaddr, 7);
              }
            }
            atx = x + atflags[0];
            aty = y + atflags[1];
            if ((unsigned)atx < (unsigned)w && (unsigned)aty < (unsigned)h)
            {
              bitaddr = aty * bitstride + atx;
              if (bitaddr < dbitaddr)
                prev |= JBITS(base, bitaddr, 2);
            }
          }
          break;

        case 3:
          if (nominal)
          {
            if (y > 0 && x < w - 2)
            {
              bitaddr = dbitaddr + 2 - bitstride;
              prev |= JBITS(base, bitaddr, 4);
            }
          }
          else
          {
            if (y > 0 && x < w - 1)
            {
              bitaddr = dbitaddr + 1 - bitstride;
              prev |= JBITS(base, bitaddr, 5);
            }
            atx = x + atflags[0];
            aty = y + atflags[1];
            if ((unsigned)atx < (unsigned)w && (unsigned)aty < (unsigned)h)
            {
              bitaddr = aty * bitstride + atx;
              if (bitaddr < dbitaddr)
                prev |= JBITS(base, bitaddr, 4);
            }
          }
          break;
      }
    }
    if (skip && (skip[stride * y + (x >> 3)] << (x & 7)) & 0x80)
      d = 0;
    else
      d = j2iabit(fsp, contexts, prev);
    thisrow = (thisrow << 1) | d;

    *dp++ = (uchar)(thisrow << (7 - ((w - 1) & 7)));
    dbitaddr = (dbitaddr + 8) & ~7;

    y++;
  } while (y < h);
  return (1);
}

/*
 * Decodes a generic refinement bitmap.
 *
 * In order to handle typical predction, we always used a 14 bit template
 * for refinement regions, and then just mask out the bits we don't want.
 */
static int j2refineregion(J2FSTATE *fsp, J2BITMAP *dest, int dx, int dy,
                          J2BITMAP *reference, int template, int tpgdon,
                          uchar * contexts, schar * atflags)
{
  uchar *dp;
  int prev;
  int d;
  int x, y, bitaddr, dbitaddr, atx, aty;
  int sw, sh, sstride, soffset;
  int j, k;
  int ltp, tpgrval;
  int thisrow;
  int nominal;
  int prevmask, templatemask;
  uchar *base, *sbase;
  int w, h, stride, bitstride;
  static schar nom0[] = { -1, -1, -1, -1 };

  if (fsp->huff)
  {
    if (j2seedia(fsp) < 0)
      return (-2);
  }

  base = dest->base;
  w = dest->w;
  h = dest->h;
  stride = dest->stride;
  bitstride = 8 * stride;

  sbase = reference->base;
  sw = reference->w;
  sh = reference->h;
  soffset = reference->left;
  sstride = 8 * reference->stride;

  if (template)
  {
    nominal = 1;
    prevmask = 0x1b6d;
    templatemask = 0x0bbf;
  }
  else
  {
    nominal = !jb2cmp((char *)atflags, (char *)nom0, sizeof(nom0));
    prevmask = nominal ? 0x1b6d : 0x0b65;
    templatemask = nominal ? 0x1fff : 0x2fff;
  }

  ltp = 0;

  y = dbitaddr = 0;
  dp = base;
  do
  {
    if (tpgdon)
      ltp ^= j2iabit(fsp, contexts, template ? 0x80 : 0x100);

    prev = (y > 0) ? ((dp[-stride] >> 5) & 6) : 0;
    if (!nominal)
    {
      atx = atflags[0];
      aty = y + atflags[1];
      if ((unsigned)atx < (unsigned)w && (unsigned)aty < (unsigned)h)
      {
        bitaddr = aty * bitstride + atx;
        if (bitaddr < dbitaddr)
          prev |= JBITS(base, bitaddr, 3);
      }
      atx = atflags[2] - dx;
      aty = y + atflags[3] - dy;
      if ((unsigned)atx < (unsigned)sw && (unsigned)aty < (unsigned)sh)
      {
        bitaddr = aty * sstride + atx + soffset;
        prev |= JBITS(sbase, bitaddr, 13);
      }
    }
    for (j = 0, k = 4; j < 9; j++)
    {
      atx = 1 - (j % 3) - dx;
      aty = y + 1 - (j / 3) - dy;
      if ((unsigned)atx < (unsigned)sw && (unsigned)aty < (unsigned)sh)
      {
        bitaddr = aty * sstride + atx + soffset;
        prev |= JBITS(sbase, bitaddr, k);
      }
      k++;
    }

    x = thisrow = 0;
    for (;;)
    {
      if (ltp)
      {
        tpgrval = (prev >> 4) & 0x1ff;
        if (tpgrval == 0 || tpgrval == 0x1ff)
          d = tpgrval & 1;
        else
          d = j2iabit(fsp, contexts, prev & templatemask);
      }
      else
      {
        d = j2iabit(fsp, contexts, prev & templatemask);
      }
      thisrow = (thisrow << 1) | d;
      x++;
      if (x == w)
        break;

      dbitaddr++;
      if ((x & 7) == 0)
        *dp++ = (uchar)thisrow;

      prev = ((prev << 1) | d) & prevmask;
      if (y > 0)
      {
        if (x < w - 1)
        {
          bitaddr = dbitaddr + 1 - bitstride;
          prev |= JBITS(base, bitaddr, 1);
        }
      }
      atx = x - dx + 1;
      aty = y - dy + 1;
      if ((unsigned)atx < (unsigned)sw && (unsigned)aty < (unsigned)sh)
      {
        bitaddr = aty * sstride + atx + soffset;
        prev |= JBITS(sbase, bitaddr, 4);
      }
      aty = y - dy;
      if ((unsigned)atx < (unsigned)sw && (unsigned)aty < (unsigned)sh)
      {
        bitaddr = aty * sstride + atx + soffset;
        prev |= JBITS(sbase, bitaddr, 7);
      }
      aty = y - dy - 1;
      if ((unsigned)atx < (unsigned)sw && (unsigned)aty < (unsigned)sh)
      {
        bitaddr = aty * sstride + atx + soffset;
        prev |= JBITS(sbase, bitaddr, 10);
      }
      if (!nominal)
      {
        atx = x + atflags[0];
        aty = y + atflags[1];
        if ((unsigned)atx < (unsigned)w && (unsigned)aty < (unsigned)h)
        {
          dp[0] = (uchar)(thisrow << (8 - (x & 7)));
          bitaddr = aty * bitstride + atx;
          if (bitaddr < dbitaddr)
            prev |= JBITS(base, bitaddr, 3);
        }
        atx = x - dx + atflags[2];
        aty = y - dy + atflags[3];
        if ((unsigned)atx < (unsigned)sw && (unsigned)aty < (unsigned)sh)
        {
          bitaddr = aty * sstride + atx + soffset;
          prev |= JBITS(sbase, bitaddr, 13);
        }
      }
    }
    *dp++ = (uchar)(thisrow << (7 - ((w - 1) & 7)));
    dbitaddr = (dbitaddr + 8) & ~7;

    y++;
  } while (y < h);

  if (fsp->huff)
    j2skipbitstream(fsp, 1, 0);

  return (1);
}

/*
 * Constructs a Huffman tree from an array of run length counts.
 */
static J2HTAB *j2calcht(int maxlen, int n, int htlow, int *starts,
                        int *counts, int *preflen, int *rangelow,
                        int *rangelen)
{
  J2HTAB *r;
  J2HUFFENT *ht;
  J2HUFFVAL *vals;
  int j, k, code;

  DEBUG_JB2(J2DBG_HUFF, jb2dbg("JBIG2 make huffman %d %d\n", n, htlow);)

  r = (J2HTAB *)jb2malloc( (unsigned int)sizeof(J2HTAB) + (maxlen + 2) *
                             sizeof(J2HUFFENT) + n * sizeof(J2HUFFVAL), 0);
  if (!r)
    return ((J2HTAB *) 0);

  r->oob = -1;
  r->low = -1;
  r->n = n;
  ht = (J2HUFFENT *) &r[1];
  vals = (J2HUFFVAL *) &ht[maxlen + 2];
  r->vals = vals;

  starts[0] = 0;
  counts[0] = 0;
  ht[0].off = 0;
  for (j = 1; j <= maxlen; j++)
    ht[j].off = starts[j] = starts[j - 1] + counts[j - 1];

  for (j = 0; j < n; j++)
  {
    code = preflen[j];
    if (code)
    {
      k = starts[code]++;
      vals[k].length = rangelen[j];
      vals[k].offset = rangelow[j];
      if (rangelow[j] == htlow - 1)
        r->low = k;
      if (rangelen[j] < 0)
        r->oob = k;
    }
  }

  code = 0;
  ht[0].max = -1;
  for (j = 1; j <= maxlen; j++)
  {
    ht[j].off -= code;
    code += counts[j];
    ht[j].max = ((code - 1) << 8) | 0xff;
    code <<= 1;
  }
  ht[j].off = -1;
  ht[j].max = 0x7fffffff;

  return (r);
}

/*
 * Decodes a string describing a Huffman table.  This routine
 * assumes that if the input is a table segment in a file, the table
 * segment has been read entirely into memory first.
 */
static J2HTAB *j2makeht(J2FSTATE *fsp)
{
  uchar *str;
  J2HTAB *r;
  uchar *str1;
  int htoob, htps, htrs, n_htps;
  int htlow, hthigh;
  int n;
  int j, rlow, plen, rlen;
  int sbit, sch;
  int *preflen, *rangelen, *rangelow, *counts, *starts;
  int maxlen;

  str = fsp->str;
  htoob = str[0] & 1;
  htps = ((str[0] >> 1) & 7) + 1;
  n_htps = (1 << htps);
  htrs = ((str[0] >> 4) & 7) + 1;
  htlow = j2word32(str + 1);
  hthigh = j2word32(str + 5);
  str += 9;

  n = 2 + htoob;
  rlow = htlow;
  sbit = 0;
  sch = 0;
  str1 = str;

  DEBUG_JB2(J2DBG_HUFF, jb2dbg("JBIG2 huffman decode %d %d\n", rlow, hthigh);)

  do
  {
    while (sbit < htps)
    {
      sch = (sch << 8) | *str1++;
      sbit += 8;
    }
    sbit -= htps;
    while (sbit < htrs)
    {
      sch = (sch << 8) | *str1++;
      sbit += 8;
    }
    sbit -= htrs;
    rlen = (sch >> sbit) & ((1 << htrs) - 1);
    rlow += 1 << rlen;
    n++;
  } while (rlow < hthigh);

  preflen = (int *)jb2malloc((unsigned int)(3 * n + 2 * n_htps) *
                                sizeof(int), 0);
  if (!preflen)
    return ((J2HTAB *) 0);
  rangelen = &preflen[n];
  rangelow = &rangelen[n];
  counts = &rangelow[n];
  starts = &counts[n_htps];
  jb2zero((char *)counts, n_htps * sizeof(int));

  j = 0;
  rlow = htlow;
  sbit = 0;
  str1 = str;
  maxlen = 0;
  do
  {
    while (sbit < htps)
    {
      sch = (sch << 8) | *str1++;
      sbit += 8;
    }
    sbit -= htps;
    preflen[j] = plen = (sch >> sbit) & ((1 << htps) - 1);
    counts[plen]++;
    if (plen > maxlen)
      maxlen = plen;
    while (sbit < htrs)
    {
      sch = (sch << 8) | *str1++;
      sbit += 8;
    }
    sbit -= htrs;
    rangelen[j] = rlen = (sch >> sbit) & ((1 << htrs) - 1);
    rangelow[j] = rlow;
    rlow += 1 << rlen;
    j++;
  } while (rlow < hthigh);
  while (sbit < htps)
  {
    sch = (sch << 8) | *str1++;
    sbit += 8;
  }
  sbit -= htps;
  preflen[j] = plen = (sch >> sbit) & ((1 << htps) - 1);
  counts[plen]++;
  if (plen > maxlen)
    maxlen = plen;
  rangelen[j] = 32;
  rangelow[j] = htlow - 1;
  j++;
  while (sbit < htps)
  {
    sch = (sch << 8) | *str1++;
    sbit += 8;
  }
  sbit -= htps;
  preflen[j] = plen = (sch >> sbit) & ((1 << htps) - 1);
  counts[plen]++;
  if (plen > maxlen)
    maxlen = plen;
  rangelen[j] = 32;
  rangelow[j] = hthigh;
  j++;
  if (htoob)
  {
    while (sbit < htps)
    {
      sch = (sch << 8) | *str1++;
      sbit += 8;
    }
    sbit -= htps;
    preflen[j] = plen = (sch >> sbit) & ((1 << htps) - 1);
    counts[plen]++;
    if (plen > maxlen)
      maxlen = plen;
    rangelen[j] = -1;
    rangelow[j] = 0;
  }

  r = j2calcht(maxlen, n, htlow, starts, counts, preflen,
                  rangelow, rangelen);
  jb2free((char *)preflen);
  return (r);
}

/*
 * Deal with a standard Huffman table
 */
static int j2stdht(J2PRIVATE *j2priv, J2HTAB **rp, int ind)
{
  J2FSTATE state;
  J2HTAB *r;

  r = j2priv->stdhufftables[ind];
  if (!r)
  {
    j2initbitstream(&(state.in), 0);
    state.str = jhts[ind];
    j2priv->stdhufftables[ind] = r = j2makeht(&state);
    if (!r)
      return (-1);
  }
  *rp = r;
  return (1);
}

/*
 * Find the requested Huffman table
 */
static int j2findtable(J2PRIVATE *j2priv, J2HTAB **rp,
                          int which, int userseg, int base)
{
  J2HTAB *ht = 0;
  int j;
  J2TABLESEGMENT *refseg;

  if (which != userseg)
  {
    /* It's a standard table */
    return (j2stdht(j2priv, rp, which + base));
  }
  else
  {
    /* It's a user table from the file */
    for (j = j2priv->segind; j < j2priv->nreferred; j++)
    {
      refseg = (J2TABLESEGMENT *)j2priv->referred[j];
      if ((refseg->segment.flags & J2_SEGFLAGS_TYPE) == J2_SEGTYPE_TABLE)
      {
        ht = refseg->ht;
        j2priv->segind = j + 1;
        break;
      }
    }
  }
  if (!ht)
    return (-1);
  *rp = ht;
  return (1);
}

/*
 * Just reads a bit field.
 */
static int j2readn(J2FSTATE *fsp, int length, int *rp)
{
  int bits;
  int code;
  uchar ch;

  bits = fsp->in.bits;
  ch   = fsp->in.lastch;
  if (bits != 0)
    code = (ch << (8 - bits)) & 0xff;
  else
    code = 0;

  code >>= (8 - bits);
  if (length <= bits)
  {
    bits -= length;
    code >>= bits;
  }
  else
  {
    while (length - bits >= 8)
    {
      if ( j2getbytes(&(fsp->in), 1, (int *)&ch) < 0 )
        return -2;
      code = (code << 8) | ch;
      length -= 8;
    }
    if (length > bits)
    {
      length -= bits;
      bits = 8 - length;
      if ( j2getbytes(&(fsp->in), 1, (int *)&ch) < 0 )
        return -2;
      code = (code << length) | (ch >> bits);
    }
    else if (length == bits)
      bits = 0;
  }
  fsp->in.bits   = bits;
  fsp->in.lastch = ch;
  *rp = code;

  return (1);
}

/*
 * Raed a single value from a Huffman table
 */
static int j2readhuff(J2FSTATE *fsp, J2HTAB *htp, int *rp)
{
  int bits;
  J2HUFFENT *ht;
  int code, ind;
  int j;
  uchar ch;

  ht = (J2HUFFENT *)&htp[1];
  bits = fsp->in.bits;
  ch   = fsp->in.lastch;
  if (bits != 0)
    code = (ch << (8 - bits)) & 0xff;
  else
    code = 0;
  do
  {
    if (bits == 0)
    {
      if ( j2getbytes(&(fsp->in), 1, (int *)&ch) < 0 )
        return -2;
      code |= ch;
      bits = 8;
    }
    code <<= 1;
    bits--;
    ht++;
  } while (code > ht->max);

  fsp->in.bits   = bits;
  fsp->in.lastch = ch;

  if (ht->max == 0x7fffffff)
    return (-2);

  ind = (code >> 8) + ht->off;

  if (ind == htp->oob)
    return (J2OOB);

  JB2ASSERT(ind >=0 && ind < htp->n,"JBIG2 huffman index out of range");
  j = j2readn(fsp, htp->vals[ind].length, &code);
  if (j < 0)
    return (j);

  if (ind == htp->low)
    code = -code;
  *rp = code + htp->vals[ind].offset;

  return (1);
}

/*
 * Read a single value using either the Huffman table, or integer
 * contexts appropriate to the value being decoded.
 */
static int j2readint(J2PRIVATE *j2priv, int which, int *rp)
{
  if (j2priv->fstate.huff)
    return (j2readhuff(&j2priv->fstate, j2priv->hufftables[which], rp));
  else
    return (j2readia(&j2priv->fstate, j2priv->iacontexts[which], rp));
}

/*
 * Reads and decodes a symbol ID Huffman table.
 */
static int j2readsidht(J2FSTATE *fsp, int numsyms, J2HTAB **rp)
{
  int len, lastlen = 0;
  int j, n;
  int rclens[35], rccounts[16], rcstarts[16];
  int rcrangelow[35], rcrangelen[35];
  int *slens, scounts[32], sstarts[32], *srangelow, *srangelen;
  J2HTAB *ht;

  jb2zero((char *)rccounts, sizeof(rccounts));
  for (j = 0; j < 35; j++)
  {
    n = j2readn(fsp, 4, &len);
    if (n < 0)
      return (-2);
    rccounts[len]++;
    rclens[j] = len;
  }

  for (j = 0; j < 32; j++)
  {
    rcrangelow[j] = j;
    rcrangelen[j] = 0;
  }
  rcrangelow[32] = 103;
  rcrangelen[32] = 2;
  rcrangelow[33] = 203;
  rcrangelen[33] = 3;
  rcrangelow[34] = 211;
  rcrangelen[34] = 7;
  ht = j2calcht(15, 35, 0, rcstarts, rccounts, rclens, rcrangelow,
                   rcrangelen);
  if (!ht)
    return (-2);

  slens = (int *)jb2malloc((unsigned int) 3 * numsyms * sizeof(int), 0);
  if (!slens)
  {
    jb2free((char *)ht);
    return (-2);
  }
  jb2zero((char *)scounts, sizeof(scounts));
  srangelow = &slens[numsyms];
  srangelen = &srangelow[numsyms];
  for (j = 0; j < numsyms; j++)
  {
    srangelow[j] = j;
    srangelen[j] = 0;
  }

  for (j = 0; j < numsyms;)
  {
    n = j2readhuff(fsp, ht, &len);
    if (n < 0)
    {
      jb2free((char *)ht);
      jb2free((char *)slens);
      return (-2);
    }
    if (len > 200)
    {
      len -= 200;
      /* KAS 3/12/2002
       * This was previously j+=len; which left 'holes' in
       * slens, which caused problems in j2calcht().
       */
      while (len--)
      {
        scounts[0]++;
        slens[j++] = 0;
      }
    }
    else if (len > 100)
    {
      len -= 100;
      scounts[lastlen] += len;
      while (len--)
        slens[j++] = lastlen;
    }
    else
    {
      scounts[len]++;
      slens[j++] = len;
      lastlen = len;
    }
  }

  j2skipbitstream(fsp, 0, 0);

  jb2free((char *)ht);
  ht = j2calcht(31, numsyms, 0, sstarts, scounts, slens, srangelow,
                   srangelen);
  jb2free((char *)slens);
  if (!ht)
      return (-2);
  *rp = ht;
  return (1);
}

/*
 * Combines two region bitmaps.
 */
static void j2dorop(J2BITMAP *bms, J2BITMAP *dest, int dx, int dy, int rop)
{
  uchar *drow, *srow, *dp, *sp;
  int lmask, rmask, sbits, sbits1;
  int dstride, sstride;
  int sx, sy, w, h;
  int diff;
  int bw;
  int j, k, shift;

  sx = bms->left;
  sy = 0;
  w = bms->w;
  h = bms->h;
  if (dy < 0)
  {
    sy -= dy;
    h += dy;
    dy = 0;
  }
  diff = dy + h - dest->h;
  if (diff > 0)
    h -= diff;
  if (h <= 0)
    return;
  if (dx < 0)
  {
    sx -= dx;
    w += dx;
    dx = 0;
  }
  diff = dx + w - dest->w;
  if (diff > 0)
    w -= diff;
  if (w <= 0)
    return;
  dx += dest->left;

  dstride = dest->stride;
  sstride = bms->stride;

  drow = dest->base + dy * dstride + (dx >> 3);
  dx &= 7;
  srow = bms->base + sy * sstride + (sx >> 3);
  sx &= 7;
  shift = sx - dx;
  lmask = 0xff >> dx;
  rmask = 0xff << (7 - ((dx + w - 1) & 7));

  /*
   * The rasterop code is ugly because 1) we want to keep the size down,
   * 2) the rasters are mixed-endian on little-endian machines.  Since
   * we can't use all of the macros in rop.h, we use none of them.
   *
   * Note that this code can fetch off the end of the source bitmap.  This
   * doesn't matter as j2allocbitmap() has allocated an extra 4 bytes
   * safety guard at the end.
   */
  bw = (dx + w - 9) >> 3;
  if (bw < 0)
  {
    lmask &= rmask;
    rmask = 0;
  }
  for (k = 0; k < h; k++)
  {
    dp = drow;
    sp = srow;
    sbits1 = *sp++ << (8 + shift);
    if (shift > 0)
      sbits1 |= *sp++ << shift;
    sbits = sbits1 >> 8;
    switch (rop)
    {
      case J2_ROP_OR:
        *dp++ |= sbits & lmask;
        break;

      case J2_ROP_AND:
        *dp++ &= sbits | ~lmask;
        break;

      case J2_ROP_XOR:
        *dp++ ^= sbits & lmask;
        break;

      case J2_ROP_XNOR:
        dp[0] = (uchar)((dp[0] & ~lmask)|((~(dp[0] ^ sbits)) & lmask));
        dp++;
        break;

      case J2_ROP_REPLACE:
        dp[0] = (uchar)((dp[0] & ~lmask) | (sbits & lmask));
        dp++;
        break;
    }
    for (j = 0; j < bw; j++)
    {
      if (shift <= 0)
        sbits1 = (sbits1 << 8) | *sp++ << (8 + shift);
      else
        sbits1 = (sbits1 << 8) | *sp++ << shift;
      sbits = sbits1 >> 8;
      switch (rop)
      {
        case J2_ROP_OR:
          *dp++ |= sbits;
          break;

        case J2_ROP_AND:
          *dp++ &= sbits;
          break;

        case J2_ROP_XOR:
          *dp++ ^= sbits;
          break;

        case J2_ROP_XNOR:
          dp[0] = (uchar)(~(dp[0] ^ sbits));
          dp++;
          break;

        case J2_ROP_REPLACE:
          *dp++ = (uchar)sbits;
          break;
      }
    }
    if (rmask)
    {
      if (shift <= 0)
          sbits1 = (sbits1 << 8) | *sp++ << (8 + shift);
      else
          sbits1 = (sbits1 << 8) | *sp++ << shift;
      sbits = sbits1 >> 8;
      switch (rop)
      {
        case J2_ROP_OR:
          dp[0] |= sbits & rmask;
          break;

        case J2_ROP_AND:
          dp[0] &= sbits | ~rmask;
          break;

        case J2_ROP_XOR:
          dp[0] ^= sbits & rmask;
          break;

        case J2_ROP_XNOR:
          dp[0] = (uchar)((dp[0] & ~rmask) | ((~(dp[0] ^ sbits)) & rmask));
          break;

        case J2_ROP_REPLACE:
          dp[0] = (uchar)((dp[0] & ~rmask) | (sbits & rmask));
          break;
      }
    }
    drow += dstride;
    srow += sstride;
  }
}

/*
 * Allocates memory and fills in the appropriate structure to define
 * a JBIg2 bitmap.
 */
static int j2allocbitmap(J2BITMAP *bp, int w, int h)
{
  int bytes;

  bp->w = w;
  bp->h = h;
  bp->stride = (w + 7) >> 3;
  bp->left = 0;
  bytes = bp->h * bp->stride;
  /*
   * Note : j2dorop() code reads source before deciding if the data will
   * be needed. This means it may read a few extra bytes beyond the end of
   * the raster. To prevent a whole load of extra tests being needed in the
   * rop code, just add an extra 4 bytes to the malloc as a safety guard
   * so we can happily read beyond the end.
   */
  bp->base = (uchar *)jb2malloc(bytes+4, 1);
  bp->free_it = 1;
  return (bp->base ? 1 : -1);
}

#define J2_REF_RIGHT 2
#define J2_REF_TOP 1

/*
 * Called if there was an error during the decoing of the textregion.
 */
static int j2textregion_err(J2BITMAP *bm, J2PRIVATE *j2priv, char *errstr)
{
  if ( bm->base )
    jb2free((char *)bm->base);
  return jb2error(j2priv->inputstream, errstr, -2);
}

/*
 * Decode a JBI2 text region
 */
static int j2textregion(J2PRIVATE *j2priv, J2TEXTPARAMS *params)
{
  int j, k;
  int stript;
  int firsts, curs, curt, ninst;
  int dt, ds;
  int id;
  int refine;
  int rdw, rdh, rdx, rdy, rsize;
  int x, y;
  J2BITMAP *sym;
  J2BITMAP bm;

  bm.base = 0;

  DEBUG_JB2(J2DBG_TXT, jb2dbg("JBIG2 textregion\n");)

  if (j2readint(j2priv, J2_IADT, &stript) < 0)
    return j2textregion_err(&bm, j2priv, "Bad stript");

  DEBUG_JB2(J2DBG_TXT, jb2dbg("JBIG2 textregion stript %d\n", stript);)
  stript *= -params->sbstrips;

  for ( firsts = ninst = 0; ninst < params->sbnuminstances; )
  {
    DEBUG_JB2(J2DBG_TXT, jb2dbg("JBIG2 textregion inst %d/%d\n", ninst,
                         params->sbnuminstances);)

    /* decode delta t plus first symbols S co-ord DFS=ds */
    if ( j2readint(j2priv, J2_IADT, &dt) < 0 ||
         j2readint(j2priv, J2_IAFS, &ds) < 0 )
      return j2textregion_err(&bm, j2priv, "Bad dt/ds");
    DEBUG_JB2(J2DBG_TXT, jb2dbg("JBIG2 textregion dt/ds %d/%d\n", dt, ds);)
    dt *= params->sbstrips;
    stript += dt;
    firsts += ds;
    curs = firsts;
    for ( ; ; ) /* decode each symbol instance in the strip */
    {
      curt = stript;
      /* decode the symbol T co-ord */
      if (params->sbstrips > 1)
      {
        if (params->sbhuff)
          j = j2readn(&j2priv->fstate, params->logsbstrips, &k);
        else
          j = j2readia(&j2priv->fstate,
                           j2priv->iacontexts[J2_IAIT], &k);
        if (j < 0)
          return j2textregion_err(&bm, j2priv, "Bad T co-ord");
        curt += k;
      }

      /* decode the symbol ID */
      if (params->sbhuff)
      {
        if (params->sidht)
          j = j2readhuff(&j2priv->fstate, params->sidht, &id);
        else
          j = j2readn(&j2priv->fstate, params->sbsymcodelen, &id);
      }
      else
      {
        j = j2readiaid(&j2priv->fstate, j2priv->iaidcontexts,
                          params->sbsymcodelen, &id);
      }
      if (j < 0)
        return j2textregion_err(&bm, j2priv, "Bad symbol ID");

      DEBUG_JB2(J2DBG_TXT, jb2dbg("JBIG2 textregion id %d\n", id);)

      if ((unsigned)id >= (unsigned)params->sbnumsyms)
        return j2textregion_err(&bm, j2priv, "Out-of-range symbol ID");

      sym = params->symptrs[id];

      refine = 0;
      if (params->sbrefine)
      {
        if (params->sbhuff)
          j = j2readn(&j2priv->fstate, 1, &refine);
        else
          j = j2readia(&j2priv->fstate, j2priv->iacontexts[J2_IARI],
                          &refine);
        if (j < 0)
          return j2textregion_err(&bm, j2priv, "Bad refine");
      }

      if (refine)
      {
        if ( j2readint(j2priv, J2_IARDW, &rdw) < 0 ||
             j2readint(j2priv, J2_IARDH, &rdh) < 0 ||
             j2readint(j2priv, J2_IARDX, &rdx) < 0 ||
             j2readint(j2priv, J2_IARDY, &rdy) < 0 )
          return j2textregion_err(&bm, j2priv, "Bad refine");
        if (params->sbhuff)
        {
          j = j2readhuff(&j2priv->fstate, params->sbhuffrsize, &rsize);
          if (j < 0)
            return j2textregion_err(&bm, j2priv, "Bad refine");
        }
        if (sym->w + rdw < 0 || sym->h + rdh < 0)
          return j2textregion_err(&bm, j2priv, "Bad text region");
        if (j2allocbitmap(&bm, sym->w + rdw, sym->h + rdh) < 0)
          return j2textregion_err(&bm, j2priv, "Out of memory");
        rdx += rdw >> 1;
        rdy += rdh >> 1;
        j = j2refineregion(&j2priv->fstate, &bm, rdx, rdy, sym,
                              params->sbtemplate, 0, params->rcontexts,
                              params->sbrat);
        if (j < 0)
          return j2textregion_err(&bm, j2priv, "Bad refine call");
        sym = &bm;
      }

      /*
       * These differ from the spec. As you can see in the
       * worked example in the spec, a glyph with its bottom
       * at y=5, is actually rendered with its bottom at
       * y=6, because they apparently work in pixel numbers,
       * not co-ordinates.
       */
      if (params->transposed)
      {
        x = curt;
        if (params->refcorner & J2_REF_RIGHT)
          x -= sym->w;
        y = curs;
        curs += sym->h - 1;
      }
      else
      {
        x = curs;
        curs += sym->w - 1;
        y = curt;
        if (!(params->refcorner & J2_REF_TOP))
          y -= sym->h - 1;
      }
      j2dorop(sym, params->dest, x, y, params->rop);
      jb2free((char *)bm.base);
      bm.base = 0;

      ninst++;
      j = j2readint(j2priv, J2_IADS, &ds);
      if (j == J2OOB)
        break;
      if ( j < 0 || ninst >= params->sbnuminstances )
        return j2textregion_err(&bm, j2priv, "Missing OOB");
      curs += ds + params->sbdsoffset;
    }
  }
  return (1);
}

static int j2mmr(J2BITSTREAM *mmr, J2BITSTREAM *j2s, int w, int h, int eob)
{
  /*
   * Throughout the JBIG code, we cheat by using the
   * CCITTFaxDecode filter to read any G4-compressed data.
   */
  j2initbitstream(mmr, jb2openMMR(j2s->source, w, h, eob));
  if ( mmr->source == 0 )
    return -1;
  jb2countup(j2s->source);
  return 1;
}

/*
 * Each segment type (except for trivial ones) has a specialist routine,
 * which appear below.
 *
 * Because JBIG2Decode can only be used from PDF files, we know that these
 * special routines can never block while reading more input, so the buffer
 * refill code is simpler than it would otherwise be.
 */
/** \todo Make sure it's safe when used from PS. */

#define J2_SDHUFF 0x0001
#define J2_SDREFAGG 0x0002
#define J2_SDHUFFDH(x) (((x) >> 2) & 3)
#define J2_SDHUFFDW(x) (((x) >> 4) & 3)
#define J2_SDHUFFBMSIZE(x) (((x) >> 6) & 1)
#define J2_SDHUFFAGGINST(x) (((x) >> 7) & 1)
#define J2_SDUSED 0x0100
#define J2_SDRETAINED 0x0200
#define J2_SDTEMPLATE(x) (((x) >> 10) & 3)
#define J2_SDRTEMPLATE(x) (((x) >> 12) & 1)

#define J2SD_HEADER 0
#define J2SD_ATFLAGS 1
#define J2SD_NUMSYMS 2

/*
 * Reads a symbol dictionary definition segment.
 *
 * Documentation section 6.5 : Symbol dictionary decoding procedure.
 */
static int j2sd(J2BITSTREAM *j2s, J2PRIVATE *j2priv)
{
  J2SDSEGMENT *sdseg = 0, *refseg;
  J2BITMAP *symbols = 0;
  J2BITMAP **sptemp;
  J2BITMAP **symptrs = 0;
  uchar *bm = 0;
  J2BITSTREAM mmr;
  char *cp = 0;
  int n, bytes, rbytes;
  J2HTAB *bmsizeht = 0;
  int j, k, r;
  int height, width, dw, dh, totwidth, bmsize, stride;
  int classstart, classind;
  int exportind, exportflag, run;
  int sbcodelen = 0;
  int id, dx, dy;
  int stage = J2SD_HEADER;
  int sdhuff = 0;
  int nreferred;
  J2SEGMENT **referred;
  J2TEXTPARAMS params;
  int dontfreereferred = 0;
  J2SEGMENT *segh = &(j2priv->segh);

  /* Set params to 0s for huffrsize garbage collection, fix later */
  jb2zero((char *) &params, sizeof(params));
  j2initbitstream(&mmr, 0); /* Indicate mmr stream not used as yet */

  DEBUG_JB2(J2DBG_PARSE|J2DBG_SYM, jb2dbg("JBIG2 symbol dictionary\n");)

  for (;;)
  {
    switch (stage)
    {
      case J2SD_HEADER:
        sdseg = (J2SDSEGMENT *)jb2malloc((int)sizeof(J2SDSEGMENT), 1);
        if (!sdseg)
        {
          cp = "Out of memory";
          goto bad;
        }
        j2inittables(j2priv);
        jb2copy((char *)segh, (char *)&sdseg->segment, sizeof(J2SEGMENT));
        if ( j2segbytes(j2s, segh, -2, &(sdseg->sdflags)) < 0 )
          goto bad;
        sdhuff = sdseg->sdflags & J2_SDHUFF;

        DEBUG_JB2(J2DBG_SYM, jb2dbg("JBIG2 sd flags %d\n", sdseg->sdflags);)

        if (!sdhuff)
        {
          sdseg->contexts = (uchar *)jb2malloc((unsigned int)1 <<
                          jatemplatebits[J2_SDTEMPLATE(sdseg->sdflags)], 0);
          if (!sdseg->contexts)
          {
            cp = "Out of memory";
            goto bad;
          }
          if (sdseg->sdflags & J2_SDUSED)
          {
            /** \todo NYI. */
            DEBUG_JB2(J2DBG_WARN, jb2dbg("JBIG2 J2_SDUSED ?\n");)
          }
          else
          {
            jb2zero((char *)sdseg->contexts, 1 <<
                  jatemplatebits[J2_SDTEMPLATE(sdseg->sdflags)]);
          }
        }
        if (sdseg->sdflags & J2_SDREFAGG)
        {
          sdseg->rcontexts = (uchar *)jb2malloc((unsigned int) 1 << 14, 0);
          if (!sdseg->rcontexts)
          {
            cp = "Out of memory";
            goto bad;
          }
          if (sdseg->sdflags & J2_SDUSED)
          {
            /** \todo NYI. */
            DEBUG_JB2(J2DBG_WARN, jb2dbg("JBIG2 J2_SDUSED ?\n");)
          }
          else
          {
            jb2zero((char *)sdseg->rcontexts, 1 << 14);
          }
        }
        stage = J2SD_ATFLAGS;
        /* DROP THROUGH */

      case J2SD_ATFLAGS:
        bytes = sdhuff ? 0 : ((J2_SDTEMPLATE(sdseg->sdflags) == 0) ? 8 : 2);
        rbytes = ((sdseg->sdflags & J2_SDREFAGG) &&
                  J2_SDRTEMPLATE(sdseg->sdflags) == 0) ? 4 : 0;
        jb2zero((char *)sdseg->atflags, 8);
        if ( j2segbytes(j2s, segh, bytes, (int *)sdseg->atflags) < 0 ||
             j2segbytes(j2s, segh, rbytes, (int *)params.sbrat) < 0 )
          goto bad;
        stage = J2SD_NUMSYMS;
        /* DROP THROUGH */

      case J2SD_NUMSYMS:
        if ( j2segbytes(j2s, segh, -4, &(sdseg->nexported)) < 0 ||
             j2segbytes(j2s, segh, -4, &(sdseg->nnew)) < 0 )
          goto bad;

        DEBUG_JB2(J2DBG_SYM, jb2dbg("JBIG2 symbols %d %d\n",
              sdseg->nexported,sdseg->nnew);)

        symbols = (J2BITMAP *)jb2malloc(sdseg->nnew * sizeof(J2BITMAP) +
                              sdseg->nexported * sizeof(J2BITMAP *), 1);
        if (!symbols)
        {
          cp = "Out of memory";
          goto bad;
        }
        sdseg->symbols = symbols;
        sdseg->symptrs = (J2BITMAP **) &symbols[sdseg->nnew];

        nreferred = j2priv->nreferred;
        referred = j2priv->referred;
        for (j = sdseg->nimported = 0; j < nreferred; j++)
        {
          refseg = (J2SDSEGMENT *)referred[j];
          if ((refseg->segment.flags & J2_SEGFLAGS_TYPE) ==
                                                 J2_SEGTYPE_SYMBOL_DICT)
            sdseg->nimported += refseg->nexported;
        }
        n = sdseg->nimported + sdseg->nnew;
        if (sdseg->sdflags & J2_SDREFAGG)
        {
          for (j = n - 1, sbcodelen = 1; j >= 2; j >>= 1, sbcodelen++) ;
        }
        DEBUG_JB2(J2DBG_SYM, jb2dbg("JBIG2 %d symbol bitmaps\n", n);)
        symptrs = (J2BITMAP **)jb2malloc(n * sizeof(J2BITMAP *), 1);
        if (!symptrs)
        {
          cp = "Out of memory";
          goto bad;
        }
        params.symptrs = symptrs;
        sptemp = symptrs;
        for (j = 0; j < nreferred; j++)
        {
          refseg = (J2SDSEGMENT *)referred[j];
          if ((refseg->segment.flags & J2_SEGFLAGS_TYPE) ==
                                              J2_SEGTYPE_SYMBOL_DICT)
          {
            n = refseg->nexported;
            jb2copy((char *)refseg->symptrs, (char *)sptemp,
                                    n * sizeof(J2BITMAP *));
            sptemp += n;
          }
        }

        if (sdhuff)
        {
          j2priv->segind = 0;
          if ( j2findtable(j2priv, &j2priv->hufftables[J2_IADH],
                              J2_SDHUFFDH(sdseg->sdflags), 3, 3) < 0 ||
               j2findtable(j2priv, &j2priv->hufftables[J2_IADW],
                              J2_SDHUFFDW(sdseg->sdflags), 3, 1) < 0 ||
               j2findtable(j2priv, &bmsizeht,
                              J2_SDHUFFBMSIZE(sdseg->sdflags), 1, 0) < 0 ||
               j2findtable(j2priv, &j2priv->hufftables[J2_IAAI],
                              J2_SDHUFFAGGINST(sdseg->sdflags), 1, 0) < 0 ||
               j2stdht(j2priv, &j2priv->hufftables[J2_IAEX], 0) < 0 )
              goto bad;
          if (sdseg->sdflags & J2_SDREFAGG)
          {
            if ( j2stdht(j2priv, &params.sbhuffrsize, 0) < 0 ||
                 j2stdht(j2priv, &j2priv->hufftables[J2_IAFS], 5) < 0 ||
                 j2stdht(j2priv, &j2priv->hufftables[J2_IADS], 7) < 0 ||
                 j2stdht(j2priv, &j2priv->hufftables[J2_IADT],10) < 0 ||
                 j2stdht(j2priv, &j2priv->hufftables[J2_IARDW],14) < 0 )
              goto bad;
            j2priv->hufftables[J2_IARDH] = j2priv->hufftables[J2_IARDW];
            j2priv->hufftables[J2_IARDX] = j2priv->hufftables[J2_IARDW];
            j2priv->hufftables[J2_IARDY] = j2priv->hufftables[J2_IARDW];
          }
        }
        else
        {
          if (sdseg->sdflags & J2_SDREFAGG)
          {
            if (j2priv->iaidcodelen < sbcodelen)
            {
              jb2free((char *)j2priv->iaidcontexts);
              j2priv->iaidcontexts = (uchar *)jb2malloc(1 << sbcodelen, 1);
              if (!j2priv->iaidcontexts)
              {
                cp = "Out of memory";
                goto bad;
              }
              j2priv->iaidcodelen = sbcodelen;
            }
            else
            {
              if (j2priv->iaidcodelen)
                jb2zero((char *)j2priv->iaidcontexts, 1 << sbcodelen);
            }
          }
        }


        if (j2setfstate(&j2priv->fstate, sdhuff) < 0)
          goto bad;

        classstart = classind = 0;
        height = 0;
        if (sdseg->nnew == 0 )
          ; /* do nothing */
        else if ((sdseg->sdflags & (J2_SDHUFF | J2_SDREFAGG)) ==
                                                              J2_SDHUFF)
        {
          /*
           * Documentation section 6.5.9 : Height class collective bitmap
           * coding when SDHUFF == 1 and SDREFAGG == 0
           */
          DEBUG_JB2(J2DBG_SYM,jb2dbg("JBIG2 height class collective bmap\n"));
          do
          {
            classind = 0;
            j = j2readhuff(&j2priv->fstate,
                              j2priv->hufftables[J2_IADH], &dh);
            if (j < 0)
              goto badcode;

            height += dh;
            width = totwidth = 0;

            for (;;)
            {
              j = j2readhuff(&j2priv->fstate,
                                j2priv->hufftables[J2_IADW], &dw);
              if ( j == J2OOB )
                break;
              if (j < -1)
                goto badcode;
              if ( classstart + classind >= sdseg->nnew )
                goto badcode; /* Too many without finding OOB */
              symbols[classstart + classind].h = height;
              width += dw;
              symbols[classstart + classind].w = width;
              *sptemp++ = &symbols[classstart + classind];
              totwidth += width;
              classind++;
            }

            j = j2readhuff(&j2priv->fstate, bmsizeht, &bmsize);
            if (j < 0)
              goto badcode;

            j2unsetfstate(&j2priv->fstate, 1, 0);

            stride = (totwidth + 7) / 8;
            if ( bmsize != 0 ) /* Else bitmap is stored uncompressed */
            {
              if ( j2mmr(&mmr, j2s, totwidth, height, 0) < 0 )
                goto bad;
            }
            bmsize = stride * height;
            bm = (uchar *)jb2malloc((unsigned int)bmsize, 0);
            if (!bm)
            {
              cp = "Out of memory";
              goto bad;
            }
            for (j = classstart, k = 0; j < classstart + classind; j++)
            {
              JB2ASSERT(j < sdseg->nnew, "JBIG2 overran symbol array");
              symbols[j].base = bm;
              if ( j == classstart )
                symbols[j].free_it = 1;
              symbols[j].stride = stride;
              symbols[j].left = k;
              k += symbols[j].w;
            }

            if ((r = j2getbytes(mmr.source != 0 ? &mmr : j2s, bmsize,
                                                              (int*)bm)) < 0)
              goto bad;

            if ( mmr.source )
            {
              jb2closeMMR(mmr.source);
              mmr.source = 0;
            }

            /* Source pointer has now moved */
            if (j2setfstate(&j2priv->fstate, 1) < 0)
              goto bad;
            classstart += classind;
          } while (classstart < sdseg->nnew);
        }
        else
        {
          /*
           * Documentation section 6.5.5 : Height class delta height
           * coding when SDHUFF == 0 or SDREFAGG == 1
           */
          DEBUG_JB2(J2DBG_SYM, jb2dbg("JBIG2 height class different bmps\n"));
          do
          {
            if (j2readint(j2priv, J2_IADH, &dh) < 0)
              goto badcode;

            JB2ASSERT(dh >= 0,"JBIG2 negative height offset");
            height += dh;
            width = 0;

            for (;;)
            {
              j = j2readint(j2priv, J2_IADW, &dw);
              if (j < -1)
                goto badcode;
              if ( j == J2OOB ) /* End of this height class */
                break;
              if ( classind >= sdseg->nnew)
                goto badcode; /* Too many without finding OOB */

              width += dw;
              DEBUG_JB2(J2DBG_SYM, jb2dbg("JBIG2 read symbols %d : %d*%d\n",
                    classind, width, height);)
              JB2ASSERT(classind < sdseg->nnew, "JBIG2 overran symbol array");
              if (j2allocbitmap(&symbols[classind], width, height) < 0)
              {
                cp = "Out of memory";
                goto bad;
              }
              *sptemp++ = &symbols[classind];

              /*
               * Documentation section 6.5.8.2
               */
              if (sdseg->sdflags & J2_SDREFAGG)
              {
                if (j2readint(j2priv, J2_IAAI, &n) < 0)
                  goto badcode;
                if (n == 1 && jb2refagg1())
                {
                  if (sdhuff)
                    j = j2readn(&j2priv->fstate, sbcodelen, &id);
                  else
                    j = j2readiaid(&j2priv->fstate, j2priv->iaidcontexts,
                                      sbcodelen, &id);
                  if (j < 0)
                    goto badcode;
                  if ( j2readint(j2priv, J2_IARDX, &dx) < 0 ||
                       j2readint(j2priv, J2_IARDY, &dy) < 0 )
                    goto badcode;
                  if (sdhuff)
                  {
                    if ( j2readhuff(&j2priv->fstate, params.sbhuffrsize,
                                       &bmsize) < 0)
                      goto badcode;
                  }
                  if ( j2refineregion(&j2priv->fstate, &symbols[classind],
                                         dx, dy, symptrs[id],
                                         J2_SDRTEMPLATE(sdseg->sdflags), 0,
                                         sdseg->rcontexts, params.sbrat) < 0 )
                    goto badcode;
                }
                else
                {
                  /*
                   * CHECK - do I need to do anything about
                   * skipping unused bits with sdhuff?
                   */
                  DEBUG_JB2(J2DBG_WARN, jb2dbg("JBIG2 sdhuff bits ?\n");)
                  params.sbhuff = sdhuff;
                  params.sbrefine = 1;
                  params.logsbstrips = 0;
                  params.sbstrips = 1;
                  params.sbnuminstances = n;
                  params.sbnumsyms = sdseg->nimported + classind;
                  params.sidht = 0;
                  params.sbsymcodelen = sbcodelen;
                  params.transposed = 0;
                  params.refcorner = J2_REF_TOP;
                  params.sbdsoffset = 0;
                  params.sbtemplate = J2_SDRTEMPLATE(sdseg->sdflags);
                  params.rcontexts = sdseg->rcontexts;
                  params.dest = &symbols[classind];
                  params.rop = J2_ROP_OR;
                  if (j2textregion(j2priv, &params) < 0)
                    goto badcode;
                }
              }
              else
              {
                if ( j2readregion(&j2priv->fstate, &symbols[classind],
                                     J2_SDTEMPLATE(sdseg->sdflags), 0,
                                     sdseg->contexts, sdseg->atflags,
                                     (uchar *) 0) < 0 )
                  goto badcode;
              }
              classind++;
            }
          } while (classind < sdseg->nnew);

          if ((sdseg->sdflags & J2_SDRETAINED) == 0)
          {
            jb2free((char *)sdseg->contexts);
            sdseg->contexts = 0;
            jb2free((char *)sdseg->rcontexts);
            sdseg->rcontexts = 0;
          }
        }
        exportind = 0;
        exportflag = 0;
        n = sdseg->nimported + sdseg->nnew;
        sptemp = sdseg->symptrs;
        if ( n > 0 )
        {
          do
          {
            if (j2readint(j2priv, J2_IAEX, &run) < 0)
                goto badcode;
            if (exportflag)
            {
              /*
               * If we export symbols imported from other dictionaries,
               * then we can't free those dictionaries before us. The
               * simplest way of doing this is to not free them until
               * the end of the page.
               */
              if (exportind < sdseg->nimported)
                dontfreereferred = 1;
              jb2copy((char *)&symptrs[exportind], (char *)sptemp,
                    run * sizeof(J2BITMAP *));
              sptemp += run;
            }
            exportind += run;
            exportflag = !exportflag;
          } while (exportind < n);
        }

        if (dontfreereferred)
        {
          for (j = 0; j < nreferred; j++)
          {
            refseg = (J2SDSEGMENT *)referred[j];
            if ((refseg->segment.flags & J2_SEGFLAGS_TYPE) ==
                                                  J2_SEGTYPE_SYMBOL_DICT)
              refseg->dontfree = 1;
          }
        }

        sdseg->segment.next = j2priv->segments;
        j2priv->segments = &sdseg->segment;

        j2unsetfstate(&j2priv->fstate, sdhuff, 0);
        r = 1;
        goto done;
    }
  }

  /* NOTREACHED */

badcode:
  cp = (sdhuff) ? "Bad Huffman code" : "Bad arithmetic coding";
  goto bad;

bad:
  if (symbols)
  {
    for (j = 0; j < sdseg->nnew; j++)
    {
      if (symbols[j].free_it == 1)
        jb2free((char *)symbols[j].base);
    }
    jb2free((char *)symbols);
  }
  if (sdseg)
  {
    jb2free((char *)sdseg->contexts);
    jb2free((char *)sdseg->rcontexts);
  }
  jb2free((char *)sdseg);
  r = jb2error(j2priv->inputstream, cp, -2);

done:
  if ( mmr.source )
    jb2closeMMR(mmr.source);
  jb2free((char *)symptrs);
  return (r);
}

#define J2_PDMMR 0x0001
#define J2_PDTEMPLATE(x) (((x) >> 1) & 3)
/*
 * Reads a pattern dictionary definition segment.
 */
static int j2pd(J2BITSTREAM *j2s, J2PRIVATE *j2priv)
{
  J2PDSEGMENT *pdseg = 0;
  J2SEGMENT *segh = &(j2priv->segh);
  static schar atflags[] = { 0, 0, -3, -1, 2, -2, -2, -2 };
  J2BITSTREAM mmr;
  uchar *contexts = 0;
  char *cp = 0;
  int w, h, wn, bytes;
  int j, r;

  DEBUG_JB2(J2DBG_PARSE, jb2dbg("JBIG2 pattern dictionary\n");)

  j2initbitstream(&mmr, 0); /* Indicate mmr stream not used as yet */
  pdseg = (J2PDSEGMENT *)jb2malloc(sizeof(J2PDSEGMENT), 1);
  if (!pdseg)
  {
    cp = "Out of memory";
    goto bad;
  }
  jb2copy((char *)segh, (char *)&pdseg->segment, sizeof(J2SEGMENT));
  if ( j2segbytes(j2s, segh, -1, &(pdseg->pdflags)) < 0 ||
       j2segbytes(j2s, segh, -1, &w) < 0 ||
       j2segbytes(j2s, segh, -1, &h) < 0 ||
       j2segbytes(j2s, segh, -4, &(pdseg->n)) < 0 )
    goto bad;
  if (w < 0 || h < 0)
    goto badseg;
  pdseg->n += 1;
  wn = pdseg->n * w;
  if (j2allocbitmap(&pdseg->patterns, wn, h) < 0)
  {
    cp = "Out of memory";
    goto bad;
  }

  if (pdseg->pdflags & J2_PDMMR)
  {
    if ( j2mmr(&mmr, j2s, wn, h, 0) < 0 )
      goto bad;

    bytes = h * pdseg->patterns.stride;
    if ((r = j2getbytes(&mmr, bytes, (int *)pdseg->patterns.base)) <=0 )
      goto bad;

    jb2closeMMR(mmr.source);
    mmr.source = 0;
  }
  else
  {
    contexts = (uchar *)jb2malloc(1 << jatemplatebits[J2_PDTEMPLATE
                                         (pdseg->pdflags)], 1);
    if (!contexts)
    {
      cp = "Out of memory";
      goto bad;
    }
    atflags[0] = (uchar)(-w);
    if (j2setfstate (&j2priv->fstate, 0) < 0)
      goto bad;
    j = j2readregion(&j2priv->fstate, &pdseg->patterns,
                        J2_PDTEMPLATE(pdseg->pdflags), 0, contexts,
                        atflags, (uchar *) 0);
    if (j < 0)
      goto badcode;
    j2unsetfstate(&j2priv->fstate, 0, 0);
  }

  pdseg->patterns.w = w;

  pdseg->segment.next = j2priv->segments;
  j2priv->segments = &pdseg->segment;

  r = 1;
  goto done;

  /* NOTREACHED */

badseg:
  cp = "Bad pattern dictionary segment";
  goto bad;

badcode:
  cp = (pdseg->pdflags & J2_PDMMR) ?
                  "Bad Huffman code" : "Bad arithmetic coding";
  goto bad;

bad:
  if (pdseg)
    jb2free((char *)pdseg->patterns.base);
  jb2free((char *)pdseg);
  r = jb2error(j2priv->inputstream, cp, -2);

done:
  jb2free((char *)contexts);
  if ( mmr.source )
    jb2closeMMR(mmr.source);
  return (r);
}

#define J2_REGIONHEADERBYTES 17

/*
 * All the segment types that define a region use j2regionheader() to
 * set up the region, and j2finishregion() to dispose of it.
 */
static int j2regionheader(J2BITSTREAM *j2s, J2PRIVATE *j2priv)
{
  J2SEGMENT *segh = &(j2priv->segh);
  int w, h;

  jb2copy((char *)segh, (char *)&j2priv->currentregion.segment,
                        sizeof(J2SEGMENT));

  if ( j2segbytes(j2s, segh, -4, &w) < 0 ||
       j2segbytes(j2s, segh, -4, &h) < 0 ||
       j2segbytes(j2s, segh, -4, &(j2priv->currentregion.x)) < 0 ||
       j2segbytes(j2s, segh, -4, &(j2priv->currentregion.y)) < 0 ||
       j2segbytes(j2s, segh, -1, &(j2priv->currentregion.combop)) < 0 )
    return jb2error(j2priv->inputstream, "EOF in middle of region header", -2);

  if (w < 0 || h < 0)
    return jb2error(j2priv->inputstream, "Bad region segment", -2);

  if (j2allocbitmap(&j2priv->currentregion.region, w, h) < 0)
    return jb2error(j2priv->inputstream, "Out of memory", -2);

  j2priv->currentregion.bytes = j2priv->currentregion.region.stride *
                                 j2priv->currentregion.region.h;

  return (1);
}

/*
 * Dispose of the specified region.
 */
static int j2finishregion(J2PRIVATE *j2priv)
{
  J2REGIONSEGMENT *rseg;

  if (j2priv->segh.flags & J2_SEGFLAGS_IMMEDIATE)
  {
    j2dorop(&j2priv->currentregion.region, &j2priv->pagebuffer,
               j2priv->currentregion.x,
               j2priv->currentregion.y - j2priv->stripetop,
               j2priv->currentregion.combop);

    jb2free((char *)j2priv->currentregion.region.base);
    j2priv->currentregion.region.base = 0;
    return (1);
  }

  rseg = (J2REGIONSEGMENT *)jb2malloc(sizeof(J2REGIONSEGMENT), 0);
  if (!rseg)
    return jb2error(j2priv->inputstream, "Out of memory", -2);
  jb2copy((char *)&j2priv->currentregion, (char*)rseg,
      sizeof(J2REGIONSEGMENT));

  rseg->segment.next = j2priv->segments;
  j2priv->segments = (J2SEGMENT *)rseg;

  return (1);
}

#define J2_SBHUFF 0x0001
#define J2_SBREFINE 0x0002
#define J2_LOGSBSTRIPS(x) (((x) >> 2) & 3)
#define J2_REFCORNER(x) (((x) >> 4) & 3)
#define J2_TRANSPOSED 0x0040
#define J2_SBCOMBOP(x) (((x) >> 7) & 3)
#define J2_SBDEFPIXEL(x) (((x) >> 9) & 1)
#define J2_SBTEMPLATE(x) (((x) >> 15) & 1)

#define J2_SBHUFFFS(x) (((x) >> 0) & 3)
#define J2_SBHUFFDS(x) (((x) >> 2) & 3)
#define J2_SBHUFFDT(x) (((x) >> 4) & 3)
#define J2_SBHUFFRDW(x) (((x) >> 6) & 3)
#define J2_SBHUFFRDH(x) (((x) >> 8) & 3)
#define J2_SBHUFFRDX(x) (((x) >> 10) & 3)
#define J2_SBHUFFRDY(x) (((x) >> 12) & 3)
#define J2_SBHUFFRSIZE(x) (((x) >> 14) & 1)

#define J2TEXT_HEADER 0
#define J2TEXT_HUFFFLAGS 1
#define J2TEXT_ATFLAGS 2
#define J2TEXT_NUMINSTANCES 3

/*
 * Return the symbol code length given the number of symbols.
 * Spec (6.5.2.3 Setting SBSYMCODES and SBSYMCODELEN) says
 * if huffman
 *   len = max(ceiling(log2(nsyms)),1)
 * else
 *   len = ceiling(log2(nsyms))
 */
int j2symlen(int nsyms, int huff)
{
  int len, pow2;

  if ( nsyms == 0 )
    return 0;

  for ( len = 0, pow2 = 1; pow2 < nsyms; pow2 *= 2 )
    len++;
  if (huff && len == 0 )
    len++;
  return len;
}

/*
 * Reads a text region segment.
 */
static int j2text(J2BITSTREAM *j2s, J2PRIVATE *j2priv)
{
  int segflags = 0, huffflags;
  char *cp = 0;
  int j, n, r = 0, bytes;
  J2SDSEGMENT *sdseg;
  J2BITMAP **sptemp;
  J2BITMAP **symptrs = 0;
  int stage = J2TEXT_HEADER;
  int nreferred;
  J2SEGMENT **referred;
  J2SEGMENT *segh = &(j2priv->segh);
  J2TEXTPARAMS params = {0};
  uchar *regionbase;

  DEBUG_JB2(J2DBG_PARSE|J2DBG_TXT, jb2dbg("JBIG2 text segment\n");)
  for (;;)
  {
    DEBUG_JB2(J2DBG_TXT, jb2dbg("JBIG2 text stage %d\n",stage);)
    switch (stage)
    {
      case J2TEXT_HEADER:
        jb2zero((char *) &params, sizeof(params));
        if (segh->pageno == 0 || !j2priv->inpage)
          goto badseg;

        j2inittables (j2priv);

        if ( j2segbytes(j2s, segh, -2, &segflags) < 0 )
          goto bad;

        params.sbhuff = (segflags & J2_SBHUFF);
        params.sbrefine = (segflags & J2_SBREFINE);
        params.sbtemplate = J2_SBTEMPLATE(segflags);
        DEBUG_JB2(J2DBG_TXT,
            jb2dbg("JBIG2 text sbhuff     %d\n",params.sbhuff);
            jb2dbg("JBIG2 text sbrefine   %d\n",params.sbrefine);
            jb2dbg("JBIG2 text sbtemplate %d\n",params.sbtemplate);)
        if (params.sbrefine)
        {
          params.rcontexts = (uchar *)jb2malloc(1 << 14, 1);
          if (!params.rcontexts)
          {
            cp = "Out of memory";
            goto bad;
          }
        }
        params.refcorner = J2_REFCORNER(segflags);
        params.transposed = (segflags & J2_TRANSPOSED);
        /*
         * Extract signed fields in two steps to play safe with
         * over-enthusiastic optimisers.
         */
        params.sbdsoffset = segflags << 17;
        params.sbdsoffset = params.sbdsoffset >> 27;
        if (!params.sbhuff)
        {
          stage = J2TEXT_ATFLAGS;
          continue;
        }
        stage = J2TEXT_HUFFFLAGS;
        /* DROP THROUGH */

      case J2TEXT_HUFFFLAGS:

        if ( j2segbytes(j2s, segh, -2, &huffflags) < 0 )
          goto bad;
        DEBUG_JB2(J2DBG_TXT, jb2dbg("JBIG2 text flags %d\n",huffflags);)

        j2priv->segind = 0;
        if ( j2findtable(j2priv, &j2priv->hufftables[J2_IAFS],
                            J2_SBHUFFFS(huffflags), 3, 5) < 0 ||
             j2findtable(j2priv, &j2priv->hufftables[J2_IADS],
                            J2_SBHUFFDS(huffflags), 3, 7) < 0 ||
             j2findtable(j2priv, &j2priv->hufftables[J2_IADT],
                            J2_SBHUFFDT(huffflags), 3, 10) < 0 ||
             j2findtable(j2priv, &j2priv->hufftables[J2_IARDW],
                            J2_SBHUFFRDW(huffflags), 3, 13) < 0 ||
             j2findtable(j2priv, &j2priv->hufftables[J2_IARDH],
                            J2_SBHUFFRDH(huffflags), 3, 13) < 0 ||
             j2findtable(j2priv, &j2priv->hufftables[J2_IARDX],
                            J2_SBHUFFRDX(huffflags), 3, 13) < 0 ||
             j2findtable(j2priv, &j2priv->hufftables[J2_IARDY],
                            J2_SBHUFFRDY(huffflags), 3, 13) < 0 ||
             j2findtable(j2priv, &params.sbhuffrsize,
                            J2_SBHUFFRSIZE(huffflags), 1, 0) < 0 )
            goto bad;
        stage = J2TEXT_ATFLAGS;
        /* DROP THROUGH */

      case J2TEXT_ATFLAGS:
        if (!params.sbrefine || params.sbtemplate != 0)
        {
          stage = J2TEXT_NUMINSTANCES;
          continue;
        }
        if ( j2segbytes(j2s, segh, 4, (int *)&(params.sbrat)) < 0 )
          goto bad;

        stage = J2TEXT_NUMINSTANCES;
        /* DROP THROUGH */

      case J2TEXT_NUMINSTANCES:
        if ( j2segbytes(j2s, segh, -4, &(params.sbnuminstances)) < 0 )
          goto bad;
        nreferred = j2priv->nreferred;
        referred = j2priv->referred;
        for (j = params.sbnumsyms = 0; j < nreferred; j++)
        {
          sdseg = (J2SDSEGMENT *)referred[j];
          if ((sdseg->segment.flags & J2_SEGFLAGS_TYPE) ==
                                                 J2_SEGTYPE_SYMBOL_DICT)
            params.sbnumsyms += sdseg->nexported;
        }
        DEBUG_JB2(J2DBG_TXT,
            jb2dbg("JBIG2 text sbnuminstances %d\n", params.sbnuminstances);
            jb2dbg("JBIG2 text nreferred      %d\n", nreferred);
            jb2dbg("JBIG2 text referred       %d\n", referred);
            jb2dbg("JBIG2 text sbnumsyms      %d\n", params.sbnumsyms);)

        params.sbsymcodelen = j2symlen(params.sbnumsyms, params.sbhuff);
        symptrs = (J2BITMAP **)jb2malloc(params.sbnumsyms*
                                           sizeof(J2BITMAP *), 1);
        if (!symptrs)
        {
          cp = "Out of memory";
          goto bad;
        }
        params.symptrs = symptrs;
        sptemp = symptrs;
        for (j = 0; j < nreferred; j++)
        {
          sdseg = (J2SDSEGMENT *)referred[j];
          if ((sdseg->segment.flags & J2_SEGFLAGS_TYPE) ==
                                                     J2_SEGTYPE_SYMBOL_DICT)
          {
            n = sdseg->nexported;
            jb2copy((char *)sdseg->symptrs, (char *)sptemp, n *
                                          sizeof(J2BITMAP *));
            sptemp += n;
          }
        }

        if (j2setfstate(&j2priv->fstate, params.sbhuff) < 0)
          goto bad;
        if (params.sbhuff)
        {
          if ( j2readsidht(&j2priv->fstate, params.sbnumsyms,
                              &params.sidht) < -1)
            goto badcode;
        }
        else
        {
          if (j2priv->iaidcodelen < params.sbsymcodelen)
          {
            jb2free((char *)j2priv->iaidcontexts);
            j2priv->iaidcontexts = (uchar *)jb2malloc(1 <<
                                                       params.sbsymcodelen, 1);
            if (!j2priv->iaidcontexts)
            {
              cp = "Out of memory";
                goto bad;
            }
            j2priv->iaidcodelen = params.sbsymcodelen;
          }
          else
          {
            if (j2priv->iaidcodelen)
              jb2zero((char *)j2priv->iaidcontexts, 1 << params.sbsymcodelen);
          }
        }

        params.dest = &j2priv->currentregion.region;
        params.rop = J2_SBCOMBOP(segflags);
        if (J2_SBDEFPIXEL(segflags))
        {
          bytes = j2priv->currentregion.bytes;
          regionbase = j2priv->currentregion.region.base;
          for (j = 0; j < bytes; j++)
            regionbase[j] = 0xff;
        }

        params.logsbstrips = J2_LOGSBSTRIPS(segflags);
        params.sbstrips = 1 << params.logsbstrips;

        DEBUG_JB2(J2DBG_TXT,
            jb2dbg("JBIG2 text rop         %d\n",params.rop);
            jb2dbg("JBIG2 text logsbstrips %d\n",params.logsbstrips);
            jb2dbg("JBIG2 text sbstrips    %d\n",params.sbstrips);)
        j = j2textregion(j2priv, &params);
        if (j < 0)
        {
          r = j;
          goto done;
        }

        j2unsetfstate(&j2priv->fstate, params.sbhuff, 0);

        r = 1;
        goto done;
    }
  }

  /* NOTREACHED */

badseg:
  cp = "Bad text region segment";
  goto bad;

badcode:
  cp = (segflags & J2_SBHUFF) ? "Bad Huffman code" : "Bad arithmetic coding";
  goto bad;

bad:
  r = jb2error(j2priv->inputstream, cp, -2);

done:
  jb2free((char *)params.sidht);
  jb2free((char *)params.rcontexts);
  jb2free((char *)symptrs);
  return (r);
}

#define J2_GBMMR 0x0001
#define J2_GBTEMPLATE(x) (((x) >> 1) & 3)
#define J2_GBTPGDON(x) (((x) >> 3) & 1)

#define J2GENERIC_HEADER 0
#define J2GENERIC_ATFLAGS 2
#define J2GENERIC_REGION 3

/*
 * Reads a generic region segment.
 */
static int j2generic(J2BITSTREAM *j2s, J2PRIVATE *j2priv)
{
  int segflags = 0;
  schar atflags[8];
  J2BITSTREAM mmr;
  uchar *contexts = 0;
  char *cp = 0;
  int bytes;
  int r;
  int stage = J2GENERIC_HEADER;
  J2SEGMENT *segh = &(j2priv->segh);
  int lunk = (segh->length < 0 ); /* Is the segment length unknown ? */

  DEBUG_JB2(J2DBG_PARSE, jb2dbg("JBIG2 generic segment\n");)
  j2initbitstream(&mmr, 0); /* Indicate mmr stream not used as yet */
  for (;;)
  {
    switch (stage)
    {
      case J2GENERIC_HEADER:
        if (segh->pageno == 0 || !j2priv->inpage)
          goto badseg;
        if ( j2segbytes(j2s, segh, -1, &segflags) < 0 )
          goto bad;

        if (segflags & J2_GBMMR)
        {
          stage = J2GENERIC_REGION;
          continue;
        }
        stage = J2TEXT_ATFLAGS;
        /* DROP THROUGH */

      case J2GENERIC_ATFLAGS:
        bytes = (J2_GBTEMPLATE(segflags) == 0) ? 8 : 2;
        jb2zero((char *)atflags, 8);
        if ( j2segbytes(j2s, segh, bytes, (int *)(atflags)) < 0 )
          goto bad;
        stage = J2GENERIC_REGION;
        /* DROP THROUGH */

      case J2GENERIC_REGION:
        if (segflags & J2_GBMMR)
        {
          /** \todo EOFB ``if data length unknown'' (is it ever?) */
          /** \todo unknown height regions */
          if ( j2mmr(&mmr, j2s, j2priv->currentregion.region.w,
                                j2priv->currentregion.region.h, 0) < 0 )
            goto bad;

          if ((r = j2getbytes(&mmr, j2priv->currentregion.bytes,
                          (int *)j2priv->currentregion.region.base)) == -2)
            goto bad;

          jb2closeMMR(mmr.source);
          mmr.source = 0;
          if ( lunk )
          {
            uchar eod[4];

            /**
             * If segment length was unknown, we need to scan for EOD
             * marker (0x00,0x00) and throw any extra padding away.
             */
            while ( j2getbytes(j2s, 2, (int *)eod) > 0 &&
                    (eod[0] != 0x00 || eod[1] != 0x00) )
              ; /* Do nothing */
            if ( eod[0] != 0x00 || eod[1] != 0x00 )
              return -1;
            if ( j2getbytes(j2s, 4, (int *)eod) < 0 )
            {
              /**
               * My only example job did not have the four extra row-count
               * bytes at the end, but was otherwise valid.
               * Perhaps I'm parsing it wrong ?
               * Just ignore any errors reading these last four bytes for
               * now until I get some more examples of what should be
               * happening.
               * \todo BMJ 27-Sep-07 : Is this code right ?
               *                       More test cases needed !
               */
              /* return -1; ? */
            }
          }
        }
        else
        {
          contexts = (uchar *)jb2malloc(1 << jatemplatebits[J2_GBTEMPLATE
                                               (segflags)], 1);
          if (!contexts)
          {
            cp = "Out of memory";
            goto bad;
          }
          if (j2setfstate(&j2priv->fstate, 0) < 0)
            goto bad;
          if ( j2readregion(&j2priv->fstate, &j2priv->currentregion.region,
                               J2_GBTEMPLATE(segflags),
                               J2_GBTPGDON(segflags), contexts, atflags,
                               (uchar *) 0) < 0)
            goto badcode;
          j2unsetfstate(&j2priv->fstate, 0, lunk);
        }
        r = 1;
        goto done;
    }
  }
  /* NOTREACHED */
badseg:
  cp = "Bad generic region segment";
  goto bad;

badcode:
  cp = (segflags & J2_GBMMR) ? "Bad Huffman code" : "Bad arithmetic coding";
  goto bad;

bad:
  r = jb2error(j2priv->inputstream, cp, -2);

done:
  if ( mmr.source )
    jb2closeMMR(mmr.source);
  jb2free((char *)contexts);
  return (r);
}

#define J2_HMMR 0x0001
#define J2_HTEMPLATE(x) (((x) >> 1) & 3)
#define J2_HSKIP 0x0008
#define J2_HCOMBOP(x) (((x) >> 4) & 7)
#define J2_HDEFPIXEL(x) (((x) >> 7) & 1)

/*
 * Reads a halftone region segment.
 */
static int j2halftone(J2BITSTREAM *j2s, J2PRIVATE *j2priv)
{
  int segflags;
  static schar atflags[] = { 0, -1, -3, -1, 2, -2, -2, -2 };
  int gx, gy, gw, gh, gdx, gdy, gstride;
  char *cp = 0;
  uchar *contexts = 0;
  J2BITSTREAM mmr;
  uchar *skip = 0;
  int bpp = 0;
  int n, bytes, gbytes;
  int j, k, l, r, x, y;
  int ind, val;
  J2PDSEGMENT *pdseg = 0;
  J2BITMAP tempregion;
  J2BITMAP *gregions = 0;
  int noskip;
  uchar *regionbase;
  J2SEGMENT *segh = &(j2priv->segh);

  DEBUG_JB2(J2DBG_PARSE,jb2dbg("JBIG2 halftone segment\n");)
  j2initbitstream(&mmr, 0); /* Indicate mmr stream not used as yet */
  for (;;)
  {
    if (segh->pageno == 0 || !j2priv->inpage)
      goto badseg;
    if ( j2segbytes(j2s, segh, -1, &segflags) < 0 ||
         j2segbytes(j2s, segh, -4, &gw) < 0 ||
         j2segbytes(j2s, segh, -4, &gh) < 0 ||
         j2segbytes(j2s, segh, -4, &gx) < 0 ||
         j2segbytes(j2s, segh, -4, &gy) < 0 ||
         j2segbytes(j2s, segh, -2, &gdx) < 0 ||
         j2segbytes(j2s, segh, -2, &gdy) < 0 )
      goto bad;
    if (gw < 0 || gh < 0)
      goto badseg;

    for (j = 0; j < j2priv->nreferred; j++)
    {
      pdseg = (J2PDSEGMENT *)j2priv->referred[j];
      if ((pdseg->segment.flags & J2_SEGFLAGS_TYPE) == J2_SEGTYPE_PATTERN_DICT)
        break;
    }
    if (j == j2priv->nreferred)
      goto badseg;

    if (!(segflags & J2_HMMR))
    {
      atflags[0] = (J2_HTEMPLATE(segflags) >= 2) ? 2 : 3;
      if (j2setfstate(&j2priv->fstate, 0) < 0)
        goto bad;
      contexts = (uchar *)jb2malloc(1 << jatemplatebits[J2_HTEMPLATE
                                                          (segflags)], 1);
      if (!contexts)
      {
        cp = "Out of memory";
        goto bad;
      }
    }

    for (n = pdseg->n - 1, bpp = 1; n >= 2; n >>= 1, bpp++) ;

    gregions = (J2BITMAP *)jb2malloc(bpp*sizeof(J2BITMAP), 1);
    if (!gregions)
      goto badmem;

    gstride = (gw + 7) / 8;
    gbytes = gh * gstride;

    if (segflags & J2_HSKIP)
    {
      skip = (uchar *)jb2malloc(gbytes, 1);
      noskip = 1;
      for (k = 0; k < gh; k++)
      {
        for (j = 0; j < gw; j++)
        {
          x = (gx + k * gdy + j * gdx) >> 8;
          y = (gy + k * gdx - j * gdy) >> 8;
          if ( x <= -pdseg->patterns.w ||
               x >= j2priv->currentregion.region.w ||
               y <= -pdseg->patterns.h ||
               y >= j2priv->currentregion.region.h )
          {
            skip[k * gstride + (j >> 3)] |= 0x80 >> (j & 7);
            noskip = 0;
          }
        }
      }
      if (noskip)
      {
        jb2free((char *)skip);
        skip = 0;
      }
    }

    for (k = bpp - 1; k >= 0; k--)
    {
      if (j2allocbitmap(&gregions[k], gw, gh) < 0)
        goto badmem;

      if (segflags & J2_HMMR)
      {
        if ( j2mmr(&mmr, j2s, gw, 0, 1) < 0 )
          goto bad;

        if ((r = j2getbytes(&mmr, gbytes, (int *)gregions[k].base)) <=0 )
          goto bad;

        jb2closeMMR(mmr.source);
        mmr.source = 0;
      }
      else
      {
        j = j2readregion(&j2priv->fstate, &gregions[k],
                            J2_HTEMPLATE(segflags), 0, contexts, atflags,
                            skip);
        if (j < 0)
          goto badcode;
      }
      if (k == bpp - 1)
        continue;
      j2dorop(&gregions[k + 1], &gregions[k], 0, 0, J2_ROP_XOR);
    }

    if (J2_HDEFPIXEL (segflags))
    {
      bytes = j2priv->currentregion.bytes;
      regionbase = j2priv->currentregion.region.base;
      for (j = 0; j < bytes; j++)
        regionbase[j] = 0xff;
    }
    jb2copy((char *)&pdseg->patterns, (char *)&tempregion,
          sizeof(J2BITMAP));

    for (k = 0; k < gh; k++)
    {
      for (j = 0; j < gw; j++)
      {
        x = (gx + k * gdy + j * gdx) >> 8;
        y = (gy + k * gdx - j * gdy) >> 8;
        if (x <= -pdseg->patterns.w || x >= j2priv->currentregion.region.w ||
            y <= -pdseg->patterns.h || y >= j2priv->currentregion.region.h)
          continue;
        ind = k * gstride + (j >> 3);
        for (l = bpp - 1, val = 0; l >= 0; l--)
          val = (val << 1) | ((gregions[l].base[ind] >> (7 - (j & 7))) & 1);
        tempregion.left = val * tempregion.w;
        j2dorop(&tempregion, &j2priv->currentregion.region, x, y,
                   J2_HCOMBOP(segflags));
      }
    }

    if (!(segflags & J2_HMMR))
      j2unsetfstate(&j2priv->fstate, 0, 0);

    r = 1;
    goto done;
  }

  /* NOTREACHED */

badmem:
  cp = "Out of memory";
  goto bad;

badseg:
  cp = "Bad halftone region segment";
  goto bad;

badcode:
  cp = "Bad arithmetic coding";
  goto bad;

bad:
  r = jb2error(j2priv->inputstream, cp, -2);

done:
  if ( mmr.source )
    jb2closeMMR(mmr.source);
  jb2free((char *)contexts);
  if (gregions)
  {
    for (j = 0; j < bpp; j++)
      jb2free((char *)gregions[j].base);
    jb2free((char *)gregions);
  }
  jb2free((char *)skip);
  return (r);
}

#define J2_GRTEMPLATE(x) ((x) & 1)
#define J2_GRTPGRON(x) (((x) >> 1) & 1)

#define J2REFINEMENT_HEADER 0
#define J2REFINEMENT_ATFLAGS 2
#define J2REFINEMENT_REGION 3

/*
 * Reads a refinment region segment.
 */
static int j2refinement(J2BITSTREAM *j2s, J2PRIVATE *j2priv)
{
  int segflags = 0;
  schar atflags[4];
  int flags;
  char *cp = 0;
  int j, r;
  J2REGIONSEGMENT *rseg;
  J2BITMAP reference;
  uchar *rcontexts = 0;
  int stage = J2REFINEMENT_HEADER;
  J2SEGMENT *segh = &(j2priv->segh);

  DEBUG_JB2(J2DBG_PARSE,jb2dbg("JBIG2 refinement segment\n");)
  for (;;)
  {
    switch (stage)
    {
      case J2REFINEMENT_HEADER:
        if (segh->pageno == 0 || !j2priv->inpage)
          goto badseg;
        if ( j2segbytes(j2s, segh, -1, &segflags) < 0 )
          goto bad;

        if (J2_GRTEMPLATE(segflags) != 0)
        {
          stage = J2REFINEMENT_REGION;
          continue;
        }
        stage = J2TEXT_ATFLAGS;
        /* DROP THROUGH */

      case J2REFINEMENT_ATFLAGS:
        if ( j2segbytes(j2s, segh, 4, (int *)(atflags)) < 0 )
          goto bad;
        stage = J2REFINEMENT_REGION;
        /* DROP THROUGH */

      case J2REFINEMENT_REGION:
        for (j = 0; j < j2priv->nreferred; j++)
        {
          rseg = (J2REGIONSEGMENT *)j2priv->referred[j];
          flags = rseg->segment.flags;
          if (J2_SEGFLAGS_PRIMARY(flags) != J2_PRIMARY_METADATA &&
              J2_SEGFLAGS_SECONDARY(flags) != J2_SECONDARY_DICTIONARY)
          {
            jb2copy((char *)&rseg->region, (char *)&reference,
                   sizeof(J2BITMAP));
            break;
          }
        }
        if (j == j2priv->nreferred)
        {
          reference.w = j2priv->currentregion.region.w;
          reference.h = j2priv->currentregion.region.h;
          reference.left = j2priv->currentregion.x;
          reference.stride = j2priv->pagebuffer.stride;
          reference.base = j2priv->pagebuffer.base + reference.stride *
                           (j2priv->currentregion.y - j2priv->stripetop);
        }

        rcontexts = (uchar *)jb2malloc(1 << 14, 1);
        if (!rcontexts)
        {
          cp = "Out of memory";
          goto bad;
        }
        if (j2setfstate(&j2priv->fstate, 0) < 0)
          goto bad;

        if (j2refineregion(&j2priv->fstate, &j2priv->currentregion.region,
                              0, 0, &reference, J2_GRTEMPLATE(segflags),
                              J2_GRTPGRON(segflags), rcontexts, atflags) < 0)
        {
          cp = "Bad arithmetic coding";
          goto bad;
        }

        j2unsetfstate(&j2priv->fstate, 0, 0);

        r = 1;
        goto done;
    }
  }

  /* NOTREACHED */

badseg:
  cp = "Bad refinement region segment";
  /* DROP THROUGH */

bad:
  r = jb2error(j2priv->inputstream, cp, -2);

done:
  jb2free((char *)rcontexts);
  return (r);
}

/*
 * Reads a Huffman table definition segment.
 */
static int j2table(J2BITSTREAM *j2s, J2PRIVATE *j2priv)
{
  J2TABLESEGMENT *tableseg = 0;
  J2SEGMENT *segh = &(j2priv->segh);
  int bytes;
  uchar *sptr = 0;
  char *cp = 0;
  J2HTAB *ht = 0;
  J2FSTATE state;
  int r;

  DEBUG_JB2(J2DBG_PARSE, jb2dbg("JBIG2 huffman table segment\n");)
  bytes = segh->length - segh->used;
  sptr = (uchar *)jb2malloc((unsigned)bytes, 0);
  if (!sptr)
  {
    cp = "Out of memory";
    goto bad;
  }
  if ((r = j2getbytes(j2s, bytes, (int *)sptr)) < 0)
    goto bad;

  j2initbitstream(&(state.in), 0);
  state.str = sptr;

  ht = j2makeht(&state);
  if (!ht)
  {
    cp = "Bad table segment";
    goto bad;
  }

  tableseg = (J2TABLESEGMENT *)jb2malloc((unsigned)sizeof(J2TABLESEGMENT), 0);
  if (!tableseg)
  {
    cp = "Out of memory";
    goto bad;
  }
  jb2copy((char *)segh, (char *)&tableseg->segment, sizeof(J2SEGMENT));
  tableseg->ht = ht;

  tableseg->segment.next = j2priv->segments;
  j2priv->segments = &tableseg->segment;

  r = 1;
  goto done;

bad:
  if (ht)
    jb2free((char *)ht);
  r = jb2error(j2priv->inputstream, cp, -2);

done:
  if (sptr)
    jb2free((char *)sptr);
  return (r);
}

/*
 * Dipose of the memory asscoiated with the given segment.
 */
static void jb2freesegment(J2PRIVATE *j2priv,
                             J2SEGMENT *sp)
{
  J2SEGMENT *sp1;
  J2SDSEGMENT *sd;
  J2BITMAP *symbols;
  int j, n;

  if (sp == j2priv->segments)
  {
    j2priv->segments = sp->next;
  }
  else
  {
    for (sp1 = j2priv->segments; sp1->next != sp; sp1 = sp1->next) ;
    if (sp1)
      sp1->next = sp->next;
  }

  switch (sp->flags & J2_SEGFLAGS_TYPE)
  {
    case J2_SEGTYPE_SYMBOL_DICT:
      sd = (J2SDSEGMENT *)sp;
      jb2free((char *)sd->contexts);
      jb2free((char *)sd->rcontexts);
      symbols = sd->symbols;
      n = sd->nnew;
      for (j = 0; j < n; j++)
      {
        if (symbols[j].left == 0)
          jb2free((char *)symbols[j].base);
      }
      jb2free((char *)symbols);
      break;

    case J2_SEGTYPE_PATTERN_DICT:
      jb2free((char *) ((J2PDSEGMENT *)sp)->patterns.base);
      break;

    case J2_SEGTYPE_TABLE:
      jb2free((char *) ((J2TABLESEGMENT *)sp)->ht);
      break;

    case J2_SEGTYPE_TEXT:
    case J2_SEGTYPE_TEXT + J2_SEGFLAGS_IMMEDIATE:
    case J2_SEGTYPE_TEXT + J2_SEGFLAGS_LOSSLESS:
    case J2_SEGTYPE_TEXT + J2_SEGFLAGS_IMMEDIATE +
                              J2_SEGFLAGS_LOSSLESS:
    case J2_SEGTYPE_GENERIC:
    case J2_SEGTYPE_GENERIC + J2_SEGFLAGS_IMMEDIATE:
    case J2_SEGTYPE_GENERIC + J2_SEGFLAGS_LOSSLESS:
    case J2_SEGTYPE_GENERIC + J2_SEGFLAGS_IMMEDIATE +
                                 J2_SEGFLAGS_LOSSLESS:
    case J2_SEGTYPE_HTONE:
    case J2_SEGTYPE_HTONE + J2_SEGFLAGS_IMMEDIATE:
    case J2_SEGTYPE_HTONE + J2_SEGFLAGS_LOSSLESS:
    case J2_SEGTYPE_HTONE + J2_SEGFLAGS_IMMEDIATE +
                               J2_SEGFLAGS_LOSSLESS:
      jb2free((char *) ((J2REGIONSEGMENT *)sp)->region.base);
      break;
  }

  jb2free((char *)sp);
}

/*
 * If we have and decoded data squirreled away, then use that before
 * actually decoding some more.
 */
static int j2use_extra(J2PRIVATE *j2priv)
{
  int m = 0, n;

  if ( ( n = j2priv->extra.count ) != 0 )
  {
    m = jb2write(j2priv->inputstream, (char *)(j2priv->extra.ptr), n);

    JB2ASSERT(m <= n, "JBIG2 written too many bytes\n");

    j2priv->extra.count -= m;
    if ( j2priv->extra.count != 0 )
      j2priv->extra.ptr += m;
    else
      j2priv->extra.ptr = 0;
  }
  return m;
}

/*
 * We have decoded more than will fit in the filter output buffer.
 * So save it away in the "extra" structure, and return it a block
 * at a time.
 */
static void j2use_outbuffer(J2PRIVATE *j2priv, uchar *pagebuffer, int n)
{
  j2priv->extra.ptr   = pagebuffer;
  j2priv->extra.count = n;
  (void)j2use_extra(j2priv);
}

#define J2_FIRST_SEGHEADER 0
#define J2_NEXT_SEGHEADER 1
#define J2_MORE_4_REFERRED 2
#define J2_MORE_4_REFERRED_1 3
#define J2_REFERRED_NUMBERS 4
#define J2_PAGE_ASSOC 5
#define J2_SEG_LENGTH 6

#define J2_SEGTYPE_SWITCH 100

#if defined(DEBUG_BUILD)
static char *j2_segtypes[64] =
{
  /*  0 */ "Symbol dictionary",
  /*  1 */ "?", /*  2 */ "?", /*  3 */ "?",
  /*  4 */ "Intermediate text region",
  /*  5 */ "?",
  /*  6 */ "Immediate text region",
  /*  7 */ "Immediate lossless text region",
  /*  8 */ "?", /*  9 */ "?", /* 10 */ "?", /* 11 */ "?",
  /* 12 */ "?", /* 13 */ "?", /* 14 */ "?", /* 15 */ "?",
  /* 16 */ "Pattern dictionary",
  /* 17 */ "?", /* 18 */ "?", /* 19 */ "?",
  /* 20 */ "Intermediate halftone region",
  /* 21 */ "?",
  /* 22 */ "Immediate halftone region",
  /* 23 */ "Immediate lossless halftone region",
  /* 24 */ "?", /* 25 */ "?", /* 26 */ "?", /* 27 */ "?",
  /* 28 */ "?", /* 29 */ "?", /* 30 */ "?", /* 31 */ "?",
  /* 32 */ "?", /* 33 */ "?", /* 34 */ "?", /* 35 */ "?",
  /* 36 */ "Intermediate generic region",
  /* 37 */ "?",
  /* 38 */ "Immediate generic region",
  /* 39 */ "Immediate lossless generic region",
  /* 40 */ "Intermediate generic refinement region",
  /* 41 */ "?",
  /* 42 */ "Immediate generic refinement region",
  /* 43 */ "Immediate lossless generic refinement region",
  /* 44 */ "?", /* 45 */ "?", /* 46 */ "?", /* 47 */ "?",
  /* 48 */ "Page information",
  /* 49 */ "End of page",
  /* 50 */ "End of stripe",
  /* 51 */ "End of file",
  /* 52 */ "Profiles",
  /* 53 */ "Tables",
  /* 54 */ "?", /* 55 */ "?", /* 56 */ "?", /* 57 */ "?",
  /* 58 */ "?", /* 59 */ "?", /* 60 */ "?", /* 61 */ "?",
  /* 62 */ "Extension",
  /* 63 */ "?"
};
#endif

/*
 ******************************************************************************
 *
 * Internal interface exposed by the core JBIG2 code and wrapped by the
 * layer of code above to provide the external API.
 * Provides an open/read/close abstraction for JBIG2 streams.
 *
 ******************************************************************************
 */

/*
 * Read and decode data from the specified JBIG2 stream
 */
int jbig2read(J2STREAM *inputstream)
{
  J2BITSTREAM *j2s;
  J2PRIVATE *j2priv;
  J2SEGMENT *segptr, *segh;
  char *cp = 0;
  uchar *pagebuffer;
  int j, n, n1, ind, h;
  int val;
  int stage;
  int bytes;
  int result = 0;
  uchar dummy[8];

  j2priv = (J2PRIVATE *)jb2get_private(inputstream);
  j2priv->inputstream = inputstream;
  segh   = &(j2priv->segh);
  j2s    = &(j2priv->fstate.in);

  if ( (n = j2use_extra(j2priv)) != 0 )
    return n;

  jb2chars_in_outbuf(inputstream);

  if (j2priv->eod == 1)
    return jb2error(inputstream, 0, -1);

  /*
   * We read the global dictionaries the first time that the file
   * is read.
   */
  if (j2priv->globals)
  {
    DEBUG_JB2(J2DBG_PARSE, jb2dbg("JBIG2 using JBIG2Globals\n");)
    j2initbitstream(j2s, j2priv->globals);
  }
  else
    j2initbitstream(j2s, jb2infile(inputstream));
  stage = j2priv->stage;

  DEBUG_JB2(J2DBG_PARSE, jb2dbg("JBIG2 start parsing...\n");)
  for (;;)
  {
    switch (stage)
    {
      case J2_FIRST_SEGHEADER:
      case J2_NEXT_SEGHEADER:
        /*
         * Start by garbage collecting any segments that can be freed
         * after the last previous segment.
         */
        DEBUG_JB2(J2DBG_PARSE, jb2dbg("JBIG2 segment header\n");)
        n = j2priv->nreferred;
        if (n)
        {
          for (j = 0; j < n; j++)
          {
            if (j2priv->retain[j])
                continue;
            segptr = j2priv->referred[j];
            if ((segptr->flags & J2_SEGFLAGS_TYPE) ==
                                                  J2_SEGTYPE_SYMBOL_DICT)
            {

              /* KAS 15/01/2003
               * This *ought* to be OK< but it seems to be causing problems,
               * because we garbage collect a segment, and then want to refer
               * to it. Strange....  by simply not freeing symbol dictionaries,
               * the problem goes away. This does not cause a memory leak,
               * though it does increase the memory required.
               */
              /*
              if (((J2SDSEGMENT *)segptr)->dontfree)
              */
                continue;
            }
            jb2freesegment(j2priv, segptr);
          }
          jb2free((char *)j2priv->referred);
          j2priv->referred = 0;
          j2priv->nreferred = 0;
        }

        if (j2priv->endofstripeseen)
        {
          /* We have output a stripe, now clear the next stripe */
          val = (J2_PAGEDEFPIXEL(j2priv->pageflags)) ? 0xff : 0;
          bytes = j2priv->pagebuffer.stride * j2priv->pagebuffer.h;
          pagebuffer = j2priv->pagebuffer.base;
          for (j = 0; j < bytes; j++)
            pagebuffer[j] = (uchar)val;
          j2priv->endofstripeseen = 0;
        }

        /* Now parse the next segment header */
        jb2zero((char *)segh, sizeof(J2SEGMENT));
        /*
         * First read of segment header is a special case.
         * May need to either switch from globals to normal source,
         * or make-up an end-of-page segment if one was not present.
         */
        if ( j2getbytes(j2s, -4, &(segh->number)) < 0 )
        {
          if (j2priv->globals)
          {
            DEBUG_JB2(J2DBG_PARSE, jb2dbg("JBIG2 done JBIG2Globals\n");)
            jb2countdown(j2priv->globals);
            j2priv->globals = 0;
            j2initbitstream(j2s, jb2infile(inputstream));
            continue;
          }
          else if ( j2priv->inpage )
          {
            stage = J2_SEGTYPE_SWITCH + J2_SEGTYPE_END_OF_PAGE;
            continue;
          }
          else
            goto bad;
        }
        if ( stage == J2_FIRST_SEGHEADER )
        {
          static int j2head[2] = { 0x974A4232, 0x0D0A1A0A };
          int   dum1;
          uchar dum2;

          if ( segh->number == j2head[0] )
          {
            /* Its JBIG2 that has come through a PS stream and still
             * has its full JBIG2 header on. Parse and throw away the header
             * until we get to the first segment.
             */
            if ( j2getbytes(j2s, -4, &dum1) < 0 || dum1 != j2head[1] )
              goto bad;
            if ( j2getbytes(j2s, 1, (int *)&dum2) < 0 ) /* flag */
              goto bad;
            if ( (dum2 & 1) == 0 )
            {
              /*
               * Can't deal with random-access organisation.
               * But this is what the old PS test files used to provide.
               * So have a test and an assert to cacthe this.
               */
              JB2ASSERT( ((dum2 & 1) != 0 ),
                  "JBIG2 PS with random-access organisation unsupported");
              goto bad;
            }
            if ( (dum2 & 2) == 0 )
              if ( j2getbytes(j2s, -4, &dum1) < 0 ) /* npages */
                goto bad;
            stage = J2_NEXT_SEGHEADER;
            j2priv->had_file_header = 1;
            continue; /* go round again */
          }
        }
        DEBUG_JB2(J2DBG_PARSE, jb2dbg("JBIG2 segment# %d\n", segh->number);)

        if ( j2getbytes(j2s, 1, (int *)&(segh->flags)) < 0 )
          goto bad;
        if ( j2getbytes(j2s, -1, &n1) < 0 )
          goto bad;

        /* Sort out the referred-to segment numbers and retain bits */
        n = n1 >> 5;
        if (n == 7)
        {
          stage = J2_MORE_4_REFERRED;
          continue;
        }
        j2priv->nreferred = n;
        if (n > 0)
        {
          j2priv->referred = (J2SEGMENT **)jb2malloc(n *
                                         sizeof(J2SEGMENT *) + 1, 1);
          if (!j2priv->referred)
          {
            cp = "Out of memory";
            goto bad;
          }
          j2priv->retain = (uchar *) & j2priv->referred[n];
          j2priv->retain[0] = (uchar)n1;
        }
        stage = J2_REFERRED_NUMBERS;
        continue;

      case J2_MORE_4_REFERRED:
        if ( j2getbytes(j2s, -4, &n) < 0 )
          goto bad;
        n = n & 0x1fffffff;
        j2priv->nreferred = n;
        j2priv->referred = (J2SEGMENT **)jb2malloc(n *
                                 sizeof(J2SEGMENT *) + (n + 8) / 8, 1);
        if (!j2priv->referred)
        {
          cp = "Out of memory";
          goto bad;
        }
        j2priv->retain = (uchar *) & j2priv->referred[n];
        stage = J2_MORE_4_REFERRED_1;
        /* DROP THROUGH */

      case J2_MORE_4_REFERRED_1:
        bytes = (j2priv->nreferred + 8) / 8;
        if ( j2getbytes(j2s, bytes, (int *)(j2priv->retain)) < 0 )
          goto bad;
        stage = J2_REFERRED_NUMBERS;
        /* DROP THROUGH */

      case J2_REFERRED_NUMBERS:
        n = j2priv->nreferred;
        if (segh->number <= 256)
          bytes = -1;
        else if (segh->number <= 65536)
          bytes = -2;
        else
          bytes = -4;
        for (j = 0; j < n; j++)
        {
            if ( j2getbytes(j2s, bytes, &ind) < 0 )
              goto bad;
            for (segptr = j2priv->segments; segptr; segptr = segptr->next)
            {
              if (segptr->number == ind)
                break;
            }
            if (!segptr)
            {
              cp = "Bad segment reference";
              goto bad;
            }
            j2priv->referred[j] = segptr;
        }
        stage = J2_PAGE_ASSOC;
        /* DROP THROUGH */

      case J2_PAGE_ASSOC:
        bytes = (segh->flags & J2_SEGFLAGS_PAFS) ? -4 : -1;
        if ( j2getbytes(j2s, bytes, &(segh->pageno)) < 0 )
          goto bad;
        stage = J2_SEG_LENGTH;
        /* DROP THROUGH */

      case J2_SEG_LENGTH:
        segh->used = 0;
        if ( j2getbytes(j2s, -4, &(segh->length)) < 0 )
          goto bad;

        /* We now have a segment header, decide what to do with it */
        stage = (segh->flags & J2_SEGFLAGS_TYPE);
        DEBUG_JB2(J2DBG_PARSE, jb2dbg("JBIG2 stage %d = '%s'\n",
                    stage, j2_segtypes[stage]);)

        if ( segh->length < 0 )
        {
          if ( segh->length == -1 && stage ==  (J2_SEGTYPE_GENERIC +
                                                J2_SEGFLAGS_IMMEDIATE) )
          {
            /* Section 7.2.7. Immediate generic region may have a
             * segment length of -1 indicating data size is unknown
             * and we need to scan for an EOD marker
             */
            DEBUG_JB2(J2DBG_PARSE, jb2dbg("JBIG2 segment length unknown\n");)
            /*
             * Fall through, rest of code should be able to cope with
             * length == -1. Because of this test we know it can only come
             * through in valid circumstances now, i.e. for an Immediate
             * Generic Region.
             */
          }
          else
            goto bad;
        }
        stage += J2_SEGTYPE_SWITCH;
        continue;

      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_TEXT:
      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_TEXT + J2_SEGFLAGS_IMMEDIATE:
      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_TEXT + J2_SEGFLAGS_LOSSLESS:
      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_TEXT +
           J2_SEGFLAGS_IMMEDIATE + J2_SEGFLAGS_LOSSLESS:
      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_GENERIC:
      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_GENERIC +
           J2_SEGFLAGS_IMMEDIATE:
      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_GENERIC +
           J2_SEGFLAGS_LOSSLESS:
      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_GENERIC +
           J2_SEGFLAGS_IMMEDIATE + J2_SEGFLAGS_LOSSLESS:
      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_HTONE:
      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_HTONE +
           J2_SEGFLAGS_IMMEDIATE:
      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_HTONE +
           J2_SEGFLAGS_LOSSLESS:
      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_HTONE +
           J2_SEGFLAGS_IMMEDIATE + J2_SEGFLAGS_LOSSLESS:
      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_REFINEMENT:
      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_REFINEMENT +
           J2_SEGFLAGS_IMMEDIATE:
      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_REFINEMENT +
           J2_SEGFLAGS_LOSSLESS:
      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_REFINEMENT +
           J2_SEGFLAGS_IMMEDIATE + J2_SEGFLAGS_LOSSLESS:
        result = j2regionheader(j2s, j2priv);
        if (result < 0)
          return (result);
        switch (stage & ~(J2_SEGFLAGS_IMMEDIATE | J2_SEGFLAGS_LOSSLESS))
        {
          case J2_SEGTYPE_SWITCH + J2_SEGTYPE_TEXT:
            result = j2text(j2s, j2priv);
            break;

          case J2_SEGTYPE_SWITCH + J2_SEGTYPE_GENERIC:
            result = j2generic(j2s, j2priv);
            break;

          case J2_SEGTYPE_SWITCH + J2_SEGTYPE_HTONE:
            result = j2halftone(j2s, j2priv);
            break;

          case J2_SEGTYPE_SWITCH + J2_SEGTYPE_REFINEMENT:
            result = j2refinement(j2s, j2priv);
            break;
        }
        if (result < 0)
          return (result);
        result = j2finishregion(j2priv);
        if (result < 0)
          return (result);
        stage = J2_NEXT_SEGHEADER;
        continue;

      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_SYMBOL_DICT:
        result = j2sd(j2s, j2priv);
        if (result < 0)
          return (result);
        stage = J2_NEXT_SEGHEADER;
        continue;

      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_PATTERN_DICT:
        result = j2pd(j2s, j2priv);
        if (result < 0)
          return (result);
        stage = J2_NEXT_SEGHEADER;
        continue;

      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_PAGE_INFO:
        /* Page information segment */
        if ( j2priv->inpage )
        {
          cp = "Bad page information segment";
          goto bad;
        }
        if ( j2segbytes(j2s, segh, -4, &(j2priv->pagew)) < 0 ||
             j2segbytes(j2s, segh, -4, &(j2priv->pageh)) < 0 ||
             j2segbytes(j2s, segh, 8, (int *)dummy) < 0 ||
             j2segbytes(j2s, segh, -1, &(j2priv->pageflags)) < 0 ||
             j2segbytes(j2s, segh, -2, &(j2priv->maxstripe)) < 0 )
          goto bad;
        j2priv->stripetop = 0;
        if (j2priv->maxstripe & 0x8000)
        {
          j2priv->maxstripe &= 0x7fff;
        }
        else if (j2priv->pageh == -1)
        {
          cp = "Bad page information segment";
          goto bad;
        }
        else
        {
          j2priv->maxstripe = j2priv->pageh;
        }
        j2priv->inpage = 1;
        j2priv->endofstripeseen = 0;
        if ( j2allocbitmap(&j2priv->pagebuffer, j2priv->pagew,
                             j2priv->maxstripe) < 0 )
        {
          cp = "Out of memory";
          goto bad;
        }
        if (J2_PAGEDEFPIXEL(j2priv->pageflags))
        {
          bytes = j2priv->pagebuffer.stride * j2priv->pagebuffer.h;
          pagebuffer = j2priv->pagebuffer.base;
          for (j = 0; j < bytes; j++)
            pagebuffer[j] = 0xff;
        }

        stage = J2_NEXT_SEGHEADER;
        continue;

      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_END_OF_PAGE:
        if (j2priv->endofstripeseen)
        {
          /* Nothing to do. */
          j2priv->inpage = 0;
          j2priv->eod = 1;
          stage = J2_NEXT_SEGHEADER;
          continue;
        }
        j2priv->inpage = 0;
        j2priv->eod = 1;
        if ( j2priv->had_file_header )
        {
          /*
           * Data included a file header, so probably came from PS.
           * So it may also include PS file trailer.
           * So throw away data until we get to EOF.
           */
          while ( j2getbytes(j2s, 1, (int *)dummy) > 0 )
            ;
        }
        /* DROP THROUGH */

      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_END_OF_STRIPE:
        if (stage == J2_SEGTYPE_SWITCH + J2_SEGTYPE_END_OF_STRIPE)
        {
          if ( j2getbytes(j2s, -4, &h) < 0 )
            goto bad;
          h = h + 1 - j2priv->stripetop;
          j2priv->stripetop += h;
        }
        else
        {
          h = j2priv->pagebuffer.h;
        }
        j2priv->endofstripeseen = 1;
        pagebuffer = j2priv->pagebuffer.base;
        n = h * j2priv->pagebuffer.stride;
        /*
         * Of course, JBIG2 and PDF have different ideas about what
         * is black and what is white.
         */
        for (j = 0; j < n; j++)
          pagebuffer[j] = ~pagebuffer[j];
        stage = J2_NEXT_SEGHEADER;
        j2use_outbuffer(j2priv, pagebuffer, n);
        result = 1;
        break;

      case J2_SEGTYPE_SWITCH + J2_SEGTYPE_TABLE:
        result = j2table(j2s, j2priv);
        if (result < 0)
          return (result);
        stage = J2_NEXT_SEGHEADER;
        continue;

      default:
        /* Skip profile, extension, and any unkown segment types. */
        while ( segh->used < segh->length )
        {
          if ( j2segbytes(j2s, segh, 1, (int *)dummy) < 0 )
            break; /* should this be an error ? */
        }

        stage = J2_NEXT_SEGHEADER;
        continue;
    }

    break;
  }

  j2priv->stage = stage;
  return (result);

bad:
  return jb2error(inputstream, cp, -2);
}

/*
 * Create an initialise the requisite internal data structure to represent a
 * JBIG2 stream. return this to the caller and it will be installed as the
 * private data field in the JBIG2 stream.
 * 'globals' is a stream representing the JBIG2Globals resource, if present.
 */
void *jbig2open(J2STREAM *globals)
{
  J2PRIVATE *j2priv;

  if ( (j2priv = (J2PRIVATE *)jb2malloc(sizeof(J2PRIVATE), 1)) == 0 )
    return 0;
  j2priv->stage = J2_FIRST_SEGHEADER;
  j2priv->globals = globals;
  return (void *)j2priv;
}

/*
 * Finished with the given JBIG2 stream, so dispose of all asscoiated
 * private data.
 */
int jbig2close(J2STREAM *inputstream)
{
  J2PRIVATE *j2priv = (J2PRIVATE *)jb2get_private(inputstream);

  if ( j2priv )
  {
    int j;

    while (j2priv->segments)
      jb2freesegment(j2priv, j2priv->segments);

    for (j = 0; j < J2_NUMSTDHTS; j++)
      jb2free((char *)j2priv->stdhufftables[j]);
    jb2free((char *)j2priv->iaidcontexts);
    if (j2priv->globals)
      jb2countdown(j2priv->globals);
    if (j2priv->referred)
      jb2free((char *)j2priv->referred);
    jb2free((char *)j2priv->pagebuffer.base);

    jb2free((char *)j2priv);
    return (1);
  }
  else
    return (0);
}

/*
 * Restarted log with new port of code from Jaws.
 *
* Log stripped */

/* end of jbig2i.c */
