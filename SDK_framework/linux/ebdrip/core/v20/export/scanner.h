/** \file
 * \ingroup psscan
 *
 * $HopeName: SWv20!export:scanner.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1994-2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PS scanner/tokeninser API
 */

#ifndef __SCANNER_H__
#define __SCANNER_H__

struct core_init_fns ; /* from SWcore */

/** \defgroup psscan PostScript scanner
    \ingroup ps */
/** \{ */

void ps_scanner_C_globals(struct core_init_fns *fns) ;

Bool f_scanner(register FILELIST *flptr, Bool scan_comments, Bool flag_binseq);
Bool s_scanner(uint8 *str, int32 len, int32 *pos, int32 *lineno,
               Bool scan_comments, Bool flag_binseq);

extern Bool scannedObject;
extern Bool scannedBinSequence;

/** \} */

#endif /* protection for multiple inclusion */

/* Log stripped */
