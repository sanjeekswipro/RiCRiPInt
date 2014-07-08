/** \file
 * \ingroup paths
 *
 * $HopeName: SWv20!export:upathenc.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * External declarations for upathops.c
 */

#ifndef __UPATHENC_H__
#define __UPATHENC_H__

struct PATHINFO ; /* from SWv20 */
struct OBJECT ; /* from COREobjects */
struct OMATRIX ; /* from SWv20 */

/** Count the number of operators and arguments required to encode a path
    as a binary userpath.

    \param path The path to be encoded.
    \param opCnt Storage for the number of operators.
    \param argCnt Storage for the number of arguments.
    \return TRUE on success, FALSE on failure. The routine should only fail
      when the path is malformed.
*/
Bool upathEncBinaryCount(/*@notnull@*/ /*@in@*/ struct PATHINFO *path,
                         /*@notnull@*/ /*@out@*/ int32 *opCnt,
                         /*@notnull@*/ /*@out@*/ int32 *argCnt) ;

/** Count the number of objects required to encode a path as a non-binary
    userpath.

    \param path The path to be encoded.
    \param objCnt Storage for the number of objects.
    \return TRUE on success, FALSE on failure. The routine should only fail
      when the path is malformed.
*/
Bool upathEncAsciiCount(/*@notnull@*/ /*@in@*/ struct PATHINFO *path,
                        /*@notnull@*/ /*@out@*/ int32 *objCnt) ;

/** Encode an HNA or extended HNA header for binary userpath arguments.

    \param argStr The argument storage in which to put the header.
    \param argCnt The number of arguments that will be encoded.
    \param isLong TRUE if an extended HNA (for longstring arguments) is being
      used.
*/
void upathEncBinaryHeader(/*@notnull@*/ /*@out@*/ USERVALUE *argStr,
                          int32 argCnt, Bool isLong) ;

/** Encode a path as a binary userpath.

    \param bbox The bounding box to encode in the userpath.
    \param path The path to be encoded.
    \param transform An optional transform applied to the points in the path
      (but not the bounding box).
    \param opStr Storage for the operator string.
    \param opCnt The number of operators expected.
    \param argStr Storage for the argument HNA or extended HNA.
    \param argCnt The number of arguments expected.
    \return TRUE on success, FALSE on failure. The routine should only fail
      when the path is malformed.
*/
Bool upathEncBinaryData(/*@notnull@*/ /*@in@*/ sbbox_t *bbox,
                        /*@notnull@*/ /*@in@*/ struct PATHINFO *path,
                        /*@null@*/ /*@in@*/ OMATRIX *transform,
                        /*@notnull@*/ /*@out@*/ uint8 *opStr, int32 opCnt,
                        /*@notnull@*/ /*@out@*/ USERVALUE *argStr, int32 argCnt) ;

/** Encode a path as a non-binary userpath.

    \param bbox The bounding box to encode in the userpath.
    \param path The path to be encoded.
    \param transform An optional transform applied to the points in the path
      (but not the bounding box).
    \param objList Storage for the objects.
    \param objCnt The number of objects expected.
    \return TRUE on success, FALSE on failure. The routine should only fail
      when the path is malformed.
*/
Bool upathEncAsciiData(/*@notnull@*/ /*@in@*/ sbbox_t *bbox,
                       /*@notnull@*/ /*@in@*/ struct PATHINFO *path,
                       /*@null@*/ /*@in@*/ OMATRIX *transform,
                       /*@notnull@*/ /*@out@*/ OBJECT *objList, int32 objCnt) ;

#endif /* protection for multiple inclusion */

/* Log stripped */
