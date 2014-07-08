#define DOWN2(a,b) ((((a)<<0)&128)|(((a)<<1)&64)|(((a)<<2)&32)|(((a)<<3)&16)| \
                    (((b)>>4)&8)|(((b)>>3)&4)|(((b)>>2)&2)|(((b)>>1)&1))

/*
 * Template function for doing image downsampling.
 *
 * Main code is parameterised by bits_per_comp, ncomps, nprocs etc.
 * These are all typically small integers in the range 0-4
 * Having loads of nested loops running from 0..1 or 0..3 makes the code
 * very slow. Really need to specialise the code for the common values of
 * these variables. But that means you end up with a huge number of copies
 * of the code, usually hand-optimised. Want to make the process automatic.
 * Have tried to do it via template function include. Its not ideal, but its
 * the best I can manage for now.
 */
int32 DOWN_FUNCTION(IMAGEARGS *imageargs, IM_BUFFER *imb,
                    int32 y0, int32 downbytes, Bool from_tmp)
{
  int32 downx = imageargs->downsample.x, ncomps = imageargs->ncomps;
  int32 bpc = imageargs->bits_per_comp, nprocs = imageargs->nprocs;
  int32 downy = imageargs->downsample.y;
  int32 method = imageargs->downsample.method;
  int32 i, j, k, y = (y0 % downy);
  int32 ysample = (downy > 1), xsample = 0 ;

  UNUSED_PARAM(int32, downx);
  UNUSED_PARAM(int32, downy);
  UNUSED_PARAM(int32, ncomps);
  UNUSED_PARAM(int32, bpc);
  UNUSED_PARAM(int32, nprocs);
  UNUSED_PARAM(int32, method);

  /* nprocs is 1 for all PCL, but not necessarily for PS/XPS */
  /* These asserts correspond to the code in setup_downsample() */
  HQASSERT((bpc == 8 || bpc == 1) && (ncomps == 1 || ncomps == 3 ||
            ncomps == 4 ) && (downy == 1 || downy == 2 || downy == 4) && (
            downx == 1 || downx == 2 || downx == 4) && (downx * downy > 1),
            "Bad downsample params");
  HQASSERT(bpc != 1 || (downx <= 2 && downy <= 2 && ncomps == 1),
           "Bad downsample params2");

  if (bpc == 1) {
    int32 Bayer = y0 % (downy*2) ;
    switch (Bayer) {
    case 0:
      for ( i = 0; i < DOWN_NPROCS; i++ ) {
        uint8 *dbuf = imb->dbuf[i];
        uint8 *ibuf = from_tmp ? imb->tbuf[i] : imb->ibuf[i];
        imb->obuf[i] = dbuf;

        for ( j = 0; j < downbytes/DOWN_NCOMPS; j++ ) {
          for ( k = 0; k < DOWN_NCOMPS; k++ ) {
            uint8 a = ibuf[j*DOWN_DOWNX], b = ibuf[j*DOWN_DOWNX+1] ;
            dbuf[j] = (a&128)|((a<<3)&32)|((b>>4)&8)|((b>>1)&2) ;
          }
        }
      }
      break ;

    case 1:
      for ( i = 0; i < DOWN_NPROCS; i++ ) {
        uint8 *dbuf = imb->dbuf[i];
        uint8 *ibuf = from_tmp ? imb->tbuf[i] : imb->ibuf[i];
        imb->obuf[i] = dbuf;

        for ( j = 0; j < downbytes/DOWN_NCOMPS; j++ ) {
          for ( k = 0; k < DOWN_NCOMPS; k++ ) {
            uint8 a = ibuf[j*DOWN_DOWNX], b = ibuf[j*DOWN_DOWNX+1] ;
            dbuf[j] |= ((a<<1)&64)|((a<<4)&16)|((b>>3)&4)|(b&1) ;
          }
        }
      }
      break ;

    case 2:
      for ( i = 0; i < DOWN_NPROCS; i++ ) {
        uint8 *dbuf = imb->dbuf[i];
        uint8 *ibuf = from_tmp ? imb->tbuf[i] : imb->ibuf[i];
        imb->obuf[i] = dbuf;

        for ( j = 0; j < downbytes/DOWN_NCOMPS; j++ ) {
          for ( k = 0; k < DOWN_NCOMPS; k++ ) {
            uint8 a = ibuf[j*DOWN_DOWNX], b = ibuf[j*DOWN_DOWNX+1] ;
            dbuf[j] = ((a<<1)&128)|((a<<2)&32)|((b>>3)&8)|((b>>2)&2) ;
          }
        }
      }
      break ;

    case 3:
      for ( i = 0; i < DOWN_NPROCS; i++ ) {
        uint8 *dbuf = imb->dbuf[i];
        uint8 *ibuf = from_tmp ? imb->tbuf[i] : imb->ibuf[i];
        imb->obuf[i] = dbuf;

        for ( j = 0; j < downbytes/DOWN_NCOMPS; j++ ) {
          for ( k = 0; k < DOWN_NCOMPS; k++ ) {
            uint8 a = ibuf[j*DOWN_DOWNX], b = ibuf[j*DOWN_DOWNX+1] ;
            dbuf[j] |= ((a<<2)&64)|((a<<3)&16)|((b>>2)&4)|((b>>1)&1) ;
          }
        }
      }
      break ;
    }

    return (y == 0) ? 1 : 0;
  }

  if ( DOWN_METHOD == 0 ) {
    if ( y != ysample )
      return (y == 0) ? 1 : 0 ;
  }

  for ( i = 0; i < DOWN_NPROCS; i++ ) {
    uint8 *dbuf = imb->dbuf[i];
    uint8 *ibuf = from_tmp ? imb->tbuf[i] : imb->ibuf[i];
    int32 nsrc = (DOWN_NCOMPS > DOWN_NPROCS)?DOWN_NCOMPS:1;
    imb->obuf[i] = dbuf;

    for ( j = 0; j < downbytes/nsrc; j++ ) {
      if ( DOWN_METHOD == 0 )
        xsample = j < (downbytes/nsrc - 1) ;
      for ( k = 0; k < nsrc; k++ ) {

        if ( DOWN_METHOD == 0 ) {
          /*if ( y == ysample ) {*/
            if (DOWN_DOWNX > 1)
              dbuf[nsrc*j+k] = ibuf[nsrc*(j+xsample)*DOWN_DOWNX+k];
            else
              dbuf[nsrc*j+k] = ibuf[nsrc*j*DOWN_DOWNX+k];
          /*}*/
        }
      }
    }
  }
  return (y == 0) ? 1 : 0;
}

#undef DOWN_FUNCTION
#undef DOWN_NPROCS
#undef DOWN_NCOMPS
#undef DOWN_DOWNX
#undef DOWN_METHOD
#undef DOWN2
