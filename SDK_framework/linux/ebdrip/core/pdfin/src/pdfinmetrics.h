/** \file
 * \ingroup pdf
 *
 * $HopeName: SWpdf!src:pdfinmetrics.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2009 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PDF input metrics structure
 */
#ifndef __PDFINMETRICS_H__
#define __PDFINMETRICS_H__

typedef struct pdfin_metrics_t {
  uint32 JPX;
  struct {
    uint32 luminosity;
    uint32 alpha;
    uint32 image;
  } softMaskCounts;
  struct {
    uint32 rgb;
    uint32 cmyk;
    uint32 gray;
    uint32 icc3Component;
    uint32 icc4Component;
    uint32 iccNComponent;
  } blendSpaceCounts ;

} pdfin_metrics_t ;

extern pdfin_metrics_t pdfin_metrics ;

/* Log stripped */
#endif /* !__PDFINMETRICS_H__ */
