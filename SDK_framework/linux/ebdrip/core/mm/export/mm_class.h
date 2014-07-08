/** \file
 * \ingroup mm
 *
 * $HopeName: SWmm_common!export:mm_class.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2004-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * List of allocation class names.
 *
 * This list is used by defining MM_ALLOC_CLASS as a macro and
 * hash-including the file.  It is prudent to undefine the macro afterwards,
 * in case the list is needed again in the compilation.
 */



MM_ALLOC_CLASS(MM)              /* memory-management internals */
MM_ALLOC_CLASS(PSVM)            /* PostScript VM pages */
MM_ALLOC_CLASS(UNSPECIFIED)     /* macroized away */
MM_ALLOC_CLASS(PROMISE)         /* "promised" allocations */
MM_ALLOC_CLASS(EXTERN)          /* SwAlloc() allocations */
MM_ALLOC_CLASS(STATIC)          /* "static" allocations */
MM_ALLOC_CLASS(GENERAL)         /* if there is no sensible class */

MM_ALLOC_CLASS(CLIPRECORD)      /* system.c clip records */
MM_ALLOC_CLASS(CLIP_PATH)       /* graphics.h clip path */
MM_ALLOC_CLASS(CLIP_OBJECT)     /* macros.h clip objects */
MM_ALLOC_CLASS(CLIP_BOXES)      /* clippath.c bounding boxes */

MM_ALLOC_CLASS(MATRIX)          /* OMATRIX */
MM_ALLOC_CLASS(PATHLIST)        /* system.c path lists */
MM_ALLOC_CLASS(PATHINFO)        /* path info header */
MM_ALLOC_CLASS(LINELIST)        /* system.c line lists */
MM_ALLOC_CLASS(CHARPATHS)       /* system.c char paths */
MM_ALLOC_CLASS(ADOBE_DECODE)    /* adobe1.c font decoding */
MM_ALLOC_CLASS(TTFONT)          /* truetype font allocation */
MM_ALLOC_CLASS(BINARY_FILE)     /* binfile.c buffers */
MM_ALLOC_CLASS(BINARY_SEQ)      /* binscan.c buffers */
MM_ALLOC_CLASS(CLIST_OBJECT)    /* display.h clist objects */
MM_ALLOC_CLASS(NFILL_CACHE)     /* ndisplay.h nfill caches */
MM_ALLOC_CLASS(GSTAGS_CACHE)    /* gs_tag.c tag structure caches */
MM_ALLOC_CLASS(FONT_CACHE)      /* macros.h font caches */
MM_ALLOC_CLASS(MATRIX_CACHE)    /* macros.h matrix caches */
MM_ALLOC_CLASS(CHAR_CACHE)      /* macros.h char caches */
MM_ALLOC_CLASS(GSTATE)          /* graphics.h gstates */
MM_ALLOC_CLASS(PATHFORALL)      /* macros.h pathforalls */

MM_ALLOC_CLASS(STATE_OBJECT)    /* macros.h state objects */
MM_ALLOC_CLASS(LIST_OBJECT)     /* DL list objects */
MM_ALLOC_CLASS(DLREF)           /* DL container object */
MM_ALLOC_CLASS(STEM_BLOCK)      /* t1hint.c stem blocks */
MM_ALLOC_CLASS(SAVELIST)        /* swmemory.c savelists */

MM_ALLOC_CLASS(AES_BUFFER)      /* aes.c filter buffer */
MM_ALLOC_CLASS(AES_STATE)       /* aes.c filter state */
MM_ALLOC_CLASS(ASCII_85)        /* ascii85.c buffers */
MM_ALLOC_CLASS(ASCII_85_STATE)  /* ascii85.c state */
MM_ALLOC_CLASS(ASCII_HEX)       /* asciihex.c buffers */
MM_ALLOC_CLASS(CCITT_FAX)       /* ccittfax.c objects */
MM_ALLOC_CLASS(DCT_STATE)       /* dct.c DCT states */
MM_ALLOC_CLASS(DCT_BUFFER)      /* dct.c DCT buffers */
MM_ALLOC_CLASS(DCT_QUANT)       /* dct.c and gu_dct.c quant tables */
MM_ALLOC_CLASS(HUFF_TABLE)      /* dct.c and gu_dct.c Huffman tables */
MM_ALLOC_CLASS(LZW_BUFFER)      /* lzw.c buffers */
MM_ALLOC_CLASS(LZW_STATE)       /* lzw.c decode/encode state */
MM_ALLOC_CLASS(LZW_PREFIX)      /* lzw.c prefix table */
MM_ALLOC_CLASS(LZW_SUFFIX)      /* lzw.c suffix table */
MM_ALLOC_CLASS(LZW_STACK)       /* lzw.c stack */
MM_ALLOC_CLASS(LZW_HASH)        /* lzw.c hash table */
MM_ALLOC_CLASS(LZW_CODES)       /* lzw.c code table */
MM_ALLOC_CLASS(PNG_STATE)       /* pngfilter.c filter state */
MM_ALLOC_CLASS(PNG_LIBPNG)      /* pngfilter.c libpng */
MM_ALLOC_CLASS(PNG_BUFFER)      /* pngfilter.c filter buffer */
MM_ALLOC_CLASS(FLATE_BUFFER)    /* flate.c filter buffer */
MM_ALLOC_CLASS(FLATE_STATE)     /* flate.c filter state */
MM_ALLOC_CLASS(FLATE_HUFFTABLE) /* flate.c Huffman table parts */
MM_ALLOC_CLASS(FLATE_ZLIB)      /* flate.c zlib */
MM_ALLOC_CLASS(RSD_STATE)       /* rsd.c rsd filter state */
MM_ALLOC_CLASS(SUBFILE_BUFFER)  /* subfile.c filter buffer */
MM_ALLOC_CLASS(SUBFILE_STATE)   /* subfile.c decode state */
MM_ALLOC_CLASS(FILENAME_LIST)   /* filename.c SLIST structure */
MM_ALLOC_CLASS(FILENAME)        /* filename.c filename text */
MM_ALLOC_CLASS(FILENAME_OLIST)  /* filename.c object flist */
MM_ALLOC_CLASS(EEXEC_BUFFER)    /* eexec filter buffer */
MM_ALLOC_CLASS(NULL_BUFFER)     /* null filter buffer */
MM_ALLOC_CLASS(SWSTART_FILTER)  /* filters.c start filter struct */
MM_ALLOC_CLASS(FILTER_NAME)     /* filters.c filter name */
MM_ALLOC_CLASS(FILTER_BUFFER)   /* filters.c filter buffer */
MM_ALLOC_CLASS(STREAM_BUFFER)   /* stream.c filter buffer */
MM_ALLOC_CLASS(STREAM_STATE)    /* stream.c decode state */
MM_ALLOC_CLASS(DIFF_STATE)      /* diff.c en/decode state */
MM_ALLOC_CLASS(RC4_BUFFER)      /* rc4.c filter buffer */
MM_ALLOC_CLASS(RC4_STATE)       /* rc4.c filter state */

MM_ALLOC_CLASS(UPATH)           /* userpath */
MM_ALLOC_CLASS(UPATH_OPS)       /* userpath operators */
MM_ALLOC_CLASS(UPATH_ARGS)      /* userpath arguments */
MM_ALLOC_CLASS(UPATH_MATRIX)    /* userpath matrix cache entry */
MM_ALLOC_CLASS(UPATH_STROKE)    /* userpath fill cache entry */
MM_ALLOC_CLASS(UPATH_FILL)      /* userpath fill cache entry */

MM_ALLOC_CLASS(SHADOWOP)        /* shadowop function pointers */

MM_ALLOC_CLASS(IDLOM_IMAGELIST)   /* idlomim.c image list */
MM_ALLOC_CLASS(IDLOM_OCACHE)      /* idlomutl.c object cache */
MM_ALLOC_CLASS(IDLOM_OCACHE_LIST) /* idlomutl.c object node */
MM_ALLOC_CLASS(IDLOM_SCACHE)      /* idlomutl.c string cache */
MM_ALLOC_CLASS(IDLOM_SCACHE_LIST) /* idlomutl.c string node */
MM_ALLOC_CLASS(IDLOM_ARGS)        /* idlomapi.c argument struct */
MM_ALLOC_CLASS(IDLOM_PATHCACHE)   /* idlomapi.c path cache struct */
MM_ALLOC_CLASS(IDLOM_FONTCACHE)   /* idlomapi.c font cache struct */

MM_ALLOC_CLASS(IDLOM_UPCACHE)      /* idlomapi.c userpath cache struct */
MM_ALLOC_CLASS(IDLOM_UPATH_MATRIX) /* idlomapi.c matrix cache struct */
MM_ALLOC_CLASS(IDLOM_UPATH_FILL)   /* idlomapi.c fill cache struct */
MM_ALLOC_CLASS(IDLOM_UPATH_STROKE) /* idlomapi.c stroke cache struct */
MM_ALLOC_CLASS(IDLOM_IDCACHE)      /* idlomapi.c idcache struct */

MM_ALLOC_CLASS(IMAGE)           /* dl_image.c image object */
MM_ALLOC_CLASS(IMAGE_TABLES)    /* dl_image.c image grids) lookups */
MM_ALLOC_CLASS(IMAGE_EXPAND)    /* imexpand.c image expander structs */
MM_ALLOC_CLASS(IMAGE_EXPBUF)    /* imexpand.c image expander buffers */
MM_ALLOC_CLASS(IMAGE_CONVERT)   /* dl_image.c conversion buffers */
MM_ALLOC_CLASS(IMAGE_SCANLINE)  /* dl_image.c scanline buffers */
MM_ALLOC_CLASS(IMAGE_DATA)      /* dl_image.c xyvalues */
MM_ALLOC_CLASS(IMAGE_TILE)      /* dl_image.c fast image tile */
MM_ALLOC_CLASS(IMAGE_TILETABLE) /* dl_image.c fast image tile table */
MM_ALLOC_CLASS(IMAGE_LUT)       /* recomb.c lookup tables */
MM_ALLOC_CLASS(IMAGE_CONTEXT)   /* imagecontext.c image context interface */
MM_ALLOC_CLASS(IMAGE_DECODES)   /* imdecodes.c image decode arrays */
MM_ALLOC_CLASS(IMAGE_FILE)      /* imfile.c low mem purging */

MM_ALLOC_CLASS(FONT_INFO)       /* showops.c font info list */
MM_ALLOC_CLASS(FONT_SUBS)       /* fcache.c subs vector */
MM_ALLOC_CLASS(T32_IMAGESRC)    /* t32font.c: regenerated bitmap src */
MM_ALLOC_CLASS(T32_DATA)        /* t32font.c: info for other wmode */
MM_ALLOC_CLASS(CFF_DATA)        /* adobe2.c: info for translations */
MM_ALLOC_CLASS(CFF_INDEX)       /* t1c21.c: CFF index */
MM_ALLOC_CLASS(CFF_NAME)        /* t1c21.c: CFF names */
MM_ALLOC_CLASS(CFF_FDS)         /* t1c21.c: CFF file indexes */
MM_ALLOC_CLASS(CFF_STATE)       /* t1c21.c: CFF filter state */
MM_ALLOC_CLASS(CFF_BUFFER)      /* t1c21.c: CFF filter buffer */
MM_ALLOC_CLASS(CFF_STACK)       /* cff.c: CFF object stack */
MM_ALLOC_CLASS(CID_MAP)         /* cidfont.c: CID map cache */
MM_ALLOC_CLASS(CID_DATA)        /* cidfont.c: CID data cache */
MM_ALLOC_CLASS(PFIN)            /* pfin.c: PFIN data */

MM_ALLOC_CLASS(FILE_BUFFER)     /* fileio.c file buffer */
MM_ALLOC_CLASS(DASHLIST)        /* gstate dash array */
MM_ALLOC_CLASS(PATTERN_OBJECT)  /* DL pattern object */
MM_ALLOC_CLASS(PATTERN_SHAPE)   /* patternshape.c */
MM_ALLOC_CLASS(RECT_NUMBERS)    /* rectops.c sysvalue array */
MM_ALLOC_CLASS(RECTS)           /* rectangles */
MM_ALLOC_CLASS(BAND)            /* dynamic bands */
MM_ALLOC_CLASS(MASK_BAND)       /* clipping bands */
MM_ALLOC_CLASS(REL_STRING)      /* devices.c string parameter */
MM_ALLOC_CLASS(REL_STREAM)      /* devices.c stream header */
MM_ALLOC_CLASS(REL_PRIVATE)     /* devices.c private data */
MM_ALLOC_CLASS(REL_FILE)        /* devices.c relative file */
MM_ALLOC_CLASS(REL_PARAM)       /* devices.c relative parameter */
MM_ALLOC_CLASS(REL_ARRAY)       /* devices.c parameter array */
MM_ALLOC_CLASS(REL_DICT)        /* devices.c parameter dict */
MM_ALLOC_CLASS(DEVICELIST)      /* DEVICELIST header */
MM_ALLOC_CLASS(DEVICE_NAME)     /* DEVICELIST name */
MM_ALLOC_CLASS(DEVICE_PRIVATE)  /* DEVICE private data */
MM_ALLOC_CLASS(ASYNC)           /* asynchronous action memory */
MM_ALLOC_CLASS(TRANSFER_FN)     /* transfer functions */
MM_ALLOC_CLASS(HPS_FREQLIST)    /* gu_prscn.c frequency list */
MM_ALLOC_CLASS(HMS_CENTRE)      /* gu_hsl.c respi centres weights */
MM_ALLOC_CLASS(HMS_CORNER)      /* gu_hsl.c respi corner weights */
MM_ALLOC_CLASS(HALFTONE_VALUES) /* spot function results */
MM_ALLOC_CLASS(HPS_CELL)        /* HPS cell structure */
MM_ALLOC_CLASS(HPS_CELL_TABLE)  /* HPS cell table */
MM_ALLOC_CLASS(FORM)            /* swmemory.c forms */
MM_ALLOC_CLASS(FORMARRAY)       /* swmemory.c form arrays */
MM_ALLOC_CLASS(RLEFORM)         /* swmemory.c RLE forms */
MM_ALLOC_CLASS(STACK_FRAME)     /* stacks.c stack frames */
MM_ALLOC_CLASS(WALKDICT_TEMP)   /* scratch space for walk_dictionary_sorted */
MM_ALLOC_CLASS(CHALFTONE)       /* halftone cache structure */
MM_ALLOC_CLASS(LISTCHALFTONE)   /* halftone list cache structure */
MM_ALLOC_CLASS(HALFTONE_CACHE)  /* halftone cache table */
MM_ALLOC_CLASS(LEVELSRECORD)    /* halftone levels record structure */
MM_ALLOC_CLASS(FORMCLASS)       /* halftone form class structure */
MM_ALLOC_CLASS(HALFTONE_YS)     /* halftone Y convergence array */
MM_ALLOC_CLASS(HALFTONE_XY)     /* halftone XY coords */
MM_ALLOC_CLASS(HALFTONE_FORMCACHE) /* halftone form pointer list */
MM_ALLOC_CLASS(HALFTONE_FORM)   /* halftone form */
MM_ALLOC_CLASS(HALFTONE_LEVELS) /* halftone form usage pointers */
MM_ALLOC_CLASS(HALFTONE_PATH)   /* halftone cache path on disk */
MM_ALLOC_CLASS(TRANSFER_ARRAY)  /* transfer array for threshold ht */
MM_ALLOC_CLASS(HPS_PHASE)       /* HPS temporary phase array */
MM_ALLOC_CLASS(HPS_WEIGHTS)     /* HPS weight cache */
MM_ALLOC_CLASS(HPS_ORDER)       /* HPS order array */
MM_ALLOC_CLASS(TOKEN_STORE)     /* token storage area */
MM_ALLOC_CLASS(OBJECT_LIST)     /* object list */
MM_ALLOC_CLASS(PATTERN_RESERVE) /* Reserve memory for patterns */
MM_ALLOC_CLASS(RUNLEN_BUFFER)   /* Runlength filter buffer */
MM_ALLOC_CLASS(ROLLOVER)        /* Rollover states */
MM_ALLOC_CLASS(ROLLOVER_INFO)   /* Per-thread rollover rendering info */
MM_ALLOC_CLASS(SEPARATION)      /* gu_seps.c separation struct */
MM_ALLOC_CLASS(VIGNETTEARGS)    /* vndetect.c vignette workspace */
MM_ALLOC_CLASS(VIGNETTEOBJECT)  /* vndetect.c vignette dl object */
MM_ALLOC_CLASS(SPOTLIST)        /* spotlist.c spotno list */
MM_ALLOC_CLASS(PRECONVERT)      /* preconvert.c */
MM_ALLOC_CLASS(REGION)          /* region.c */

MM_ALLOC_CLASS(PKI)             /* PKI interface memory allocation */
MM_ALLOC_CLASS(PDF_CONTEXT)     /* PDF context (in|out) (mark|exec) */
MM_ALLOC_CLASS(PDF_PSOBJECT)    /* PS object from the PDF world */
MM_ALLOC_CLASS(PDF_RESOURCE)    /* PDF Resource structure */
MM_ALLOC_CLASS(PDF_SCANCACHE)   /* Character cache for PDF scanner */
MM_ALLOC_CLASS(PDF_FILELIST)    /* FILELIST belonging to PDF */
MM_ALLOC_CLASS(PDF_XREF)        /* PDF XREF cache infrastructure */
MM_ALLOC_CLASS(PDF_TEXTSTATE)   /* PDF text state */
MM_ALLOC_CLASS(PDF_CRYPT_FILTERS) /* PDF crypt filters */
MM_ALLOC_CLASS(PDF_CRYPTO_INFO) /* PDF crypto information */
MM_ALLOC_CLASS(PDF_OC)          /* PDF optional content groups and structures */
MM_ALLOC_CLASS(PDF_TEXTSTRING)  /* PDF text string filtered conversion to UTF-8 */
MM_ALLOC_CLASS(PDF_FILERESTORE)  /* PDF marking context file restore list */

MM_ALLOC_CLASS(PDF_RR_STATE)    /* Harlequin VariData (nee Retained Raster) */
MM_ALLOC_CLASS(PDF_RR_SCAN_NODE)
MM_ALLOC_CLASS(PDF_RR_GSTATE)
MM_ALLOC_CLASS(PDF_RR_HASHTREE)
MM_ALLOC_CLASS(PDF_RR_LINKS)
MM_ALLOC_CLASS(PDF_RR_ELEMENTS)
MM_ALLOC_CLASS(PDF_RR_PAGES_NODE)
MM_ALLOC_CLASS(PDF_RR_MARKS_NODE)
MM_ALLOC_CLASS(PDF_RR_MARKS)
MM_ALLOC_CLASS(PDF_RR_EVENT)

MM_ALLOC_CLASS(PDF_IRRC)
MM_ALLOC_CLASS(IRR)

MM_ALLOC_CLASS(PDFOUT)          /* SWpdf_out compound */
MM_ALLOC_CLASS(PDFOUT_COLOR)
MM_ALLOC_CLASS(PDFOUT_EXTGS)
MM_ALLOC_CLASS(PDFOUT_FONT)
MM_ALLOC_CLASS(PDFOUT_IMAGE)
MM_ALLOC_CLASS(PDFOUT_ODB)
MM_ALLOC_CLASS(PDFOUT_OUTLIST)

MM_ALLOC_CLASS(NCOLOR)          /* dl_color.c Ncolor data */
MM_ALLOC_CLASS(DCI_LUT)         /* gscdevci.c devicecode LUTs */
MM_ALLOC_CLASS(INDEX_LUT)       /* gscindex.c indexed color LUTs */
MM_ALLOC_CLASS(CIE_34)          /* gsctable.c N dim table data */
MM_ALLOC_CLASS(TRANSFERS)       /* gsctable.c transfer arrays */
MM_ALLOC_CLASS(COLOR_TABLE)     /* gsctable.c 3 or 4d color table */
MM_ALLOC_CLASS(CSPACE_CACHE)    /* simple transform cache */
MM_ALLOC_CLASS(COC)             /* color cache */
MM_ALLOC_CLASS(ICC_PROFILE_CACHE) /* ICC profiles */
MM_ALLOC_CLASS(CMM)             /* external CMM's */

MM_ALLOC_CLASS(SHADING)         /* shading.c shaded fills */
MM_ALLOC_CLASS(FUNCTIONS)       /* functns.c */
MM_ALLOC_CLASS(RSDSTORE)        /* rsdstore.c cache of rsd data */

MM_ALLOC_CLASS(RCB_SEPINFO)     /* rcbcntrl.c - separation info */
MM_ALLOC_CLASS(RCB_ADJUST)      /* rcbadjst.c - recombine color adjustment */
MM_ALLOC_CLASS(RCB_TRAP)        /* rcbtrap.c - recombine traps */


MM_ALLOC_CLASS(TRAP_PARAMS)     /* Params structures in trap.c */

MM_ALLOC_CLASS(GUC_COLORANTSET) /* The various objects in gu_chan.c controlling */
MM_ALLOC_CLASS(GUC_RASTERSTYLE) /* ... color formats */
MM_ALLOC_CLASS(GUC_SHEET)
MM_ALLOC_CLASS(GUC_CHANNEL)
MM_ALLOC_CLASS(GUC_COLORANT)

MM_ALLOC_CLASS(TIFF_CONTEXT)    /* tiffexec mem classes */
MM_ALLOC_CLASS(TIFF_BUFFER)
MM_ALLOC_CLASS(TIFF_READER)
MM_ALLOC_CLASS(TIFF_FILE)
MM_ALLOC_CLASS(TIFF_IFD)
MM_ALLOC_CLASS(TIFF_IFDLINK)
MM_ALLOC_CLASS(TIFF_IFDENTRIES)
MM_ALLOC_CLASS(TIFF_VALUE)
MM_ALLOC_CLASS(TIFF_QTABLE)
MM_ALLOC_CLASS(TIFF_PSOBJECT)
MM_ALLOC_CLASS(TIFF_DECODE)
MM_ALLOC_CLASS(TIFF_STRING)
MM_ALLOC_CLASS(TIFF_COLOR)
MM_ALLOC_CLASS(TIFF_STRIP)
MM_ALLOC_CLASS(TIFF_FILTER_STATE)

MM_ALLOC_CLASS(AVERAGE_IF)  /* Image filtering classes */
MM_ALLOC_CLASS(SMOOTH_IF)
MM_ALLOC_CLASS(DATA_PREPARER)
MM_ALLOC_CLASS(DATA_SOURCE)
MM_ALLOC_CLASS(DATA_TRANSACTION)
MM_ALLOC_CLASS(FILTERED_IMAGE)
MM_ALLOC_CLASS(IMAGE_FILTER)
MM_ALLOC_CLASS(IMAGE_FILTER_CHAIN)
MM_ALLOC_CLASS(IMAGE_FILTER_CHAIN_LINK)
MM_ALLOC_CLASS(INTERPOLATOR)
MM_ALLOC_CLASS(MASK_SCALER)
MM_ALLOC_CLASS(MATCH_PATTERN)
MM_ALLOC_CLASS(PIXEL_PACKER)

MM_ALLOC_CLASS(TRAPPING_BRUSH)
MM_ALLOC_CLASS(TRAPPING_CELL)
MM_ALLOC_CLASS(TRAPPING_CELL_COLORTYPE)
MM_ALLOC_CLASS(TRAPPING_CELL_COLOR)
MM_ALLOC_CLASS(TRAPPING_CELL_SPOT)
MM_ALLOC_CLASS(TRAPPING_CELL_FLAGS)
MM_ALLOC_CLASS(TRAPPING_CELL_FACTOR)
MM_ALLOC_CLASS(TRAPPING_CELL_TRACKER)
MM_ALLOC_CLASS(TRAPPING_CELL_TABLE)
MM_ALLOC_CLASS(TRAPPING_COLOR)
MM_ALLOC_CLASS(TRAPPING_COLORANT_INFO)
MM_ALLOC_CLASS(TRAPPING_COLOR_STORE)
MM_ALLOC_CLASS(TRAPPING_COLOR_WORKSPACE)
MM_ALLOC_CLASS(TRAPPING_MISC)
MM_ALLOC_CLASS(TRAPPING_OBJECT_LIST)
MM_ALLOC_CLASS(TRAPPING_PAIR)
MM_ALLOC_CLASS(TRAPPING_RLE_CONTEXT)
MM_ALLOC_CLASS(TRAPPING_RLE_STORE)
MM_ALLOC_CLASS(TRAPPING_RLE_WORKSPACE)
MM_ALLOC_CLASS(TRAPPING_SHAPE)
MM_ALLOC_CLASS(TRAPPING_TRACKERS)
MM_ALLOC_CLASS(TRAPPING_CONTEXT)
MM_ALLOC_CLASS(TRAPPING_TASKS)
MM_ALLOC_CLASS(TRAPPING_COMPOSITE_CAPTURE)

MM_ALLOC_CLASS(RBT_SIMPLE_TREE)
MM_ALLOC_CLASS(RBT_HUFF_TREE)
MM_ALLOC_CLASS(RBT_WORKSPACE)

MM_ALLOC_CLASS(DL_CELL)
MM_ALLOC_CLASS(DL_CELL_LUT)
MM_ALLOC_CLASS(DL_CELL_STREAM)

MM_ALLOC_CLASS(BACKDROP_STATE)
MM_ALLOC_CLASS(BACKDROP_DATA)

MM_ALLOC_CLASS(DLSTATESTORE)
MM_ALLOC_CLASS(BDATTRIB)
MM_ALLOC_CLASS(TRANATTRIB)
MM_ALLOC_CLASS(SOFTMASKATTRIB)

MM_ALLOC_CLASS(MATTE_COLOR)

MM_ALLOC_CLASS(HDL)
MM_ALLOC_CLASS(GROUP)

MM_ALLOC_CLASS(SMP_LOCK)

MM_ALLOC_CLASS(BITGRID)

MM_ALLOC_CLASS(SEP_OMIT)

MM_ALLOC_CLASS(XML_CONTEXT)
MM_ALLOC_CLASS(XML_NAMESPACE)
MM_ALLOC_CLASS(XML_CACHE)
MM_ALLOC_CLASS(XML_DEBUG)
MM_ALLOC_CLASS(XML_URI)
MM_ALLOC_CLASS(XML_PARSE_CONTEXT)
MM_ALLOC_CLASS(XML_SUBSYSTEM)
MM_ALLOC_CLASS(XML_PARSER)
MM_ALLOC_CLASS(XML_STREAM)

MM_ALLOC_CLASS(ZIP_FILE)
MM_ALLOC_CLASS(ZIP_FILE_NAME)
MM_ALLOC_CLASS(ZIP_FILE_PIECE)
MM_ALLOC_CLASS(ZIP_FILE_STREAM)
MM_ALLOC_CLASS(ZIP_FILE_BUFFER)
MM_ALLOC_CLASS(ZIP_PARAM)
MM_ALLOC_CLASS(ZIP_ITERATOR)
MM_ALLOC_CLASS(ZIP_ITEM)
MM_ALLOC_CLASS(ZIP_READER)
MM_ALLOC_CLASS(ZIP_READER_BUFFER)
MM_ALLOC_CLASS(ZIP_ZLIB)
MM_ALLOC_CLASS(WO_ZIP)

MM_ALLOC_CLASS(XPS_ALTERNATE)
MM_ALLOC_CLASS(XPS_CALLBACK_STATE)
MM_ALLOC_CLASS(XPS_COMPLEX)
MM_ALLOC_CLASS(XPS_COMPAT)
MM_ALLOC_CLASS(XPS_CONTENTTYPE)
MM_ALLOC_CLASS(XPS_CONTEXT)
MM_ALLOC_CLASS(XPS_DRAWNAME)
MM_ALLOC_CLASS(XPS_EXTENSION)
MM_ALLOC_CLASS(XPS_GRADIENTSTOP)
MM_ALLOC_CLASS(XPS_FIXED_DOCUMENT)
MM_ALLOC_CLASS(XPS_FONT)
MM_ALLOC_CLASS(XPS_ICC_CACHE)
MM_ALLOC_CLASS(XPS_RESOURCE)
MM_ALLOC_CLASS(XPS_RELATIONSHIP)
MM_ALLOC_CLASS(XPS_RELS)
MM_ALLOC_CLASS(XPS_SELECTION)
MM_ALLOC_CLASS(XPS_SUPPORTED_URI)
MM_ALLOC_CLASS(XPS_PARTNAME)
MM_ALLOC_CLASS(XPS_PROCESSED_PARTNAME)
MM_ALLOC_CLASS(XPS_PT_PARAM)
MM_ALLOC_CLASS(XPS_PT_UTF8)
MM_ALLOC_CLASS(XPS_VER_CONTEXT)
MM_ALLOC_CLASS(XPS_DISCARDCONTROL)

MM_ALLOC_CLASS(UNICODE_FILTER_BUFFER)
MM_ALLOC_CLASS(URI)
MM_ALLOC_CLASS(JPEG2000_FILTER_STATE)
MM_ALLOC_CLASS(KAKADU)
MM_ALLOC_CLASS(JBIG2)

MM_ALLOC_CLASS(RLESTATE)        /* Runlength output states */
MM_ALLOC_CLASS(RLE_COLORANT_MAP)
MM_ALLOC_CLASS(RLE_COLORANT_LIST)
MM_ALLOC_CLASS(RLE_SURFACE_INSTANCE)
MM_ALLOC_CLASS(RLE_SURFACE_PAGE)
MM_ALLOC_CLASS(RLE_SURFACE_SHEET)

MM_ALLOC_CLASS(CCE)

MM_ALLOC_CLASS(HTM_SCRNMOD)
MM_ALLOC_CLASS(HTM_LISTMOD)
MM_ALLOC_CLASS(HTM_LISTHTINST)
MM_ALLOC_CLASS(HTM_DICTPARAMS)

MM_ALLOC_CLASS(SW_DATUM)
MM_ALLOC_CLASS(SW_BLOB)
MM_ALLOC_CLASS(SW_BLOB_MAP)

MM_ALLOC_CLASS(BLOB_CACHE)
MM_ALLOC_CLASS(BLOB_DATA)

MM_ALLOC_CLASS(FONT_DATA)

MM_ALLOC_CLASS(CRYPT_FILTER)

MM_ALLOC_CLASS(PCL_CONTEXT)
MM_ALLOC_CLASS(PCL_PATTERNS)
MM_ALLOC_CLASS(PCL_ATTRIB_PALETTE)
MM_ALLOC_CLASS(PCL_ATTRIB_PATTERN)
MM_ALLOC_CLASS(PCL_ATTRIB_MATRIX)
MM_ALLOC_CLASS(PCL_ATTRIB_ITER)
MM_ALLOC_CLASS(PCL5_FONTHDR)
MM_ALLOC_CLASS(PCL5_CHARDATA)
MM_ALLOC_CLASS(PCL5_SYMSET)

MM_ALLOC_CLASS(GROWABLE_ARRAY)

MM_ALLOC_CLASS(FASTRGB)

MM_ALLOC_CLASS(PCLXL_CONTEXT)
MM_ALLOC_CLASS(PCLXL_PARSER_CONTEXT)
MM_ALLOC_CLASS(PCLXL_GRAPHICS_STATE)
MM_ALLOC_CLASS(PCLXL_ATTRIBUTE_SET)
MM_ALLOC_CLASS(PCLXL_ATTRIBUTE)
MM_ALLOC_CLASS(PCLXL_DATA_TYPE_ARRAY)
MM_ALLOC_CLASS(PCLXL_ERROR_INFO)
MM_ALLOC_CLASS(PCLXL_STREAM)
MM_ALLOC_CLASS(PCLXL_FONT_HEADER)
MM_ALLOC_CLASS(PCLXL_FONT_CHAR)
MM_ALLOC_CLASS(PCLXL_CHAR_DATA)
MM_ALLOC_CLASS(PCLXL_PATTERN)
MM_ALLOC_CLASS(PCLXL_SCAN_LINE)
MM_ALLOC_CLASS(PCLXL_COLOR)
MM_ALLOC_CLASS(PCLXL_PASSTHROUGH_STATE)

MM_ALLOC_CLASS(INTERLEAVE_STATE)
MM_ALLOC_CLASS(INTERLEAVE_BUFFER)

MM_ALLOC_CLASS(RETAINEDRASTER_INFRA)
MM_ALLOC_CLASS(RETAINEDRASTER_BANDS)

MM_ALLOC_CLASS(NFILL)
MM_ALLOC_CLASS(GOURAUD)
MM_ALLOC_CLASS(CHAR_OBJECT)

MM_ALLOC_CLASS(BLIT_COLORMAP)
MM_ALLOC_CLASS(BLIT_COLOR)

MM_ALLOC_CLASS(ID_HASH_TABLE)

MM_ALLOC_CLASS(RDR)
MM_ALLOC_CLASS(RDR_ITER)

MM_ALLOC_CLASS(METRICS)

MM_ALLOC_CLASS(TASK)
MM_ALLOC_CLASS(TASK_GROUP)
MM_ALLOC_CLASS(TASK_LINK)
MM_ALLOC_CLASS(TASK_VECTOR)

MM_ALLOC_CLASS(DEFERRED_THREAD)

MM_ALLOC_CLASS(BAND_DATA)
MM_ALLOC_CLASS(FRAME_DATA)
MM_ALLOC_CLASS(SHEET_DATA)
MM_ALLOC_CLASS(PASS_DATA)
MM_ALLOC_CLASS(PAGE_DATA)

MM_ALLOC_CLASS(HTM_RENDER_INFO)
MM_ALLOC_CLASS(ERASE_ARGS)
MM_ALLOC_CLASS(JOIN_ARGS)

MM_ALLOC_CLASS(JOB)

MM_ALLOC_CLASS(MUTEX)
MM_ALLOC_CLASS(CONDITION)

MM_ALLOC_CLASS(RESOURCE_REQUIREMENT)
MM_ALLOC_CLASS(REQUIREMENT_NODE)
MM_ALLOC_CLASS(RESOURCE_POOL)
MM_ALLOC_CLASS(RESOURCE_SOURCE)
MM_ALLOC_CLASS(RESOURCE_LOOKUP)
MM_ALLOC_CLASS(RESOURCE_ENTRY)

MM_ALLOC_CLASS(WMPHOTO_DATA)

MM_ALLOC_CLASS(UNDECIDED) /* the class is not yet decided */

/* Log stripped */
