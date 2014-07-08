/** \file
 * \ingroup tiffcore
 *
 * $HopeName: tiffcore!src:ifdreadr.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1999-2007 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * TIFF IFD file reader.
 */

#include "core.h"
#include "swerrors.h"   /* IOERROR */
#include "swcopyf.h"    /* swcopyf() */
#include "monitor.h"

#include "lists.h"      /* DLL_ */

#include "tiffmem.h"    /* tiff_alloc_psdict() */
#include "tifffile.h"
#include "ifdreadr.h"
#include "hqmemcpy.h"   /* HqMemCpy */

#if defined( ASSERT_BUILD )
Bool ifd_debug = FALSE;
#endif

/*
 * Just how many of a smaller type you can pack into a LONG - for reading
 * the value field of an IFD entry.
 */
#define NUM_ASCII_PER_LONG  (ENTRY_SIZE_TYPE_LONG/ENTRY_SIZE_TYPE_ASCII)
#define NUM_BYTE_PER_LONG   (ENTRY_SIZE_TYPE_LONG/ENTRY_SIZE_TYPE_BYTE)
#define NUM_SBYTE_PER_LONG  (ENTRY_SIZE_TYPE_LONG/ENTRY_SIZE_TYPE_SBYTE)
#define NUM_SHORT_PER_LONG  (ENTRY_SIZE_TYPE_LONG/ENTRY_SIZE_TYPE_SHORT)
#define NUM_SSHORT_PER_LONG (ENTRY_SIZE_TYPE_LONG/ENTRY_SIZE_TYPE_SSHORT)

/** ifd reader context
 */
struct ifd_reader_s {
  mm_pool_t         mm_pool;            /* MM pool to do all allocations in */
  uint32            current_ifd_index;  /* Index of current IFD */
  ifd_ifd_t*        p_current_ifd;      /* Pointer to current IFD */
  uint32            count_ifds;         /* Count of IFDs in the file */
  dll_list_t        dls_ifds;           /* List of all IFDs from the file */
  tiff_long_t       first_ifd_offset;   /* Cached offset to first IFD */
  tiff_file_t*      p_file;             /* The TIFF file */
  ifd_ifdentry_data_t* known_tags;      /* the tags we care about, can be different for instance between tiff & wmphoto */
  Bool              f_abort_on_unknown;
  Bool              f_strict;
  Bool              f_verbose;
};


/** Lookup table of byte sizes of IFD entry types as they appear
 * in the file.
 */
static size_t file_entry_type_size[ENTRY_TYPE_NUMBER] = {
  ENTRY_SIZE_TYPE_UNSET,
  ENTRY_SIZE_TYPE_ASCII,
  ENTRY_SIZE_TYPE_BYTE,
  ENTRY_SIZE_TYPE_SHORT,
  ENTRY_SIZE_TYPE_LONG,
  ENTRY_SIZE_TYPE_RATIONAL,
  ENTRY_SIZE_TYPE_SBYTE,
  ENTRY_SIZE_TYPE_UNDEFINED,
  ENTRY_SIZE_TYPE_SSHORT,
  ENTRY_SIZE_TYPE_SLONG,
  ENTRY_SIZE_TYPE_SRATIONAL,
  ENTRY_SIZE_TYPE_FLOAT,
  ENTRY_SIZE_TYPE_DOUBLE,
  ENTRY_SIZE_TYPE_IFD13
};


/** Lookup table of byte sizes of IFD entry types as stored internally.
 */
static size_t internal_tag_type_size[ENTRY_TYPE_NUMBER] = {
  0,                         /* ENTRY_TYPE_UNSET */
  sizeof(tiff_ascii_t),      /* ENTRY_TYPE_ASCII */
  sizeof(tiff_byte_t),       /* ENTRY_TYPE_BYTE */
  sizeof(tiff_short_t),      /* ENTRY_TYPE_SHORT */
  sizeof(tiff_long_t),       /* ENTRY_TYPE_LONG */
  sizeof(tiff_rational_t),   /* ENTRY_TYPE_RATIONAL */
  sizeof(tiff_sbyte_t),      /* ENTRY_TYPE_SBYTE */
  sizeof(tiff_undefined_t),  /* ENTRY_TYPE_UNDEFINED */
  sizeof(tiff_sshort_t),     /* ENTRY_TYPE_SSHORT */
  sizeof(tiff_slong_t),      /* ENTRY_TYPE_SLONG */
  sizeof(tiff_srational_t),  /* ENTRY_TYPE_SRATIONAL */
  sizeof(tiff_float_t),      /* ENTRY_TYPE_FLOAT */
  sizeof(tiff_double_t),     /* ENTRY_TYPE_DOUBLE */
  sizeof(tiff_long_t)        /* ENTRY_TYPE_IFD13 */
};


static uint8* type_string[] = {
  UNKNOWN_T,
  (uint8 *)("BYTE"),
  (uint8 *)("ASCII"),
  (uint8 *)("SHORT"),
  (uint8 *)("LONG"),
  (uint8 *)("RATIONAL"),
  (uint8 *)("SBYTE"),
  (uint8 *)("UNDEFINED")
};


ifd_ifd_t* current_ifd(ifd_reader_t*  p_reader)
{
  return p_reader->p_current_ifd;
}

uint32  current_ifd_index(ifd_reader_t*  p_reader)
{
  return p_reader->current_ifd_index;
}

uint32 count_ifds(ifd_reader_t*  p_reader)
{
  return p_reader->count_ifds;
}

dll_list_t* dls_ifds(ifd_reader_t*  p_reader)
{
  return &(p_reader->dls_ifds);
}

void set_ifd_iterator(ifd_reader_t*  p_reader, ifd_ifd_t* p_ifd, uint32 ifd_index)
{
  p_reader->p_current_ifd = p_ifd;
  p_reader->current_ifd_index = ifd_index;
}

uint32 first_ifd_offset(ifd_reader_t*  p_reader)
{
  return p_reader->first_ifd_offset;
}

void set_first_ifd_offset(ifd_reader_t* p_reader, tiff_long_t f_off)
{
    p_reader->first_ifd_offset = f_off;
}

/** ifd_free_ifd() releases an IFD context, along with its array of
 * IFD entries, and any indirect entry values loaded as well.
 */
void ifd_free_ifd(
  mm_pool_t       mm_pool,    /* I */
  ifd_ifd_t**    pp_ifd)     /* I */
{
  ifd_ifd_t*       p_ifd;
  ifd_ifdentry_t*  p_entry;
  int32             i;
  size_t            size;

  HQASSERT((pp_ifd != NULL),
           "ifd_free_ifd: NULL pointer to IFD pointer");
  HQASSERT((*pp_ifd != NULL),
           "ifd_free_ifd: NULL IFD pointer");

  p_ifd = *pp_ifd;

  if ( p_ifd->p_entries != NULL ) {
    /* We have an array of IFD entries - free them off */
    p_entry = p_ifd->p_entries;

    /* Free off any entries with extended storage which are in memory (not pending) */
    for ( i = 0; i < (int32)(p_ifd->num_entries); i++ ) {

      if ( ((p_entry->flags)&ENTRY_INDIRECT_PENDING) == ENTRY_INDIRECT ) {
        size = (p_entry->count)*internal_tag_type_size[p_entry->type];
        mm_free(mm_pool, p_entry->p_data, size);
      }

      p_entry++;
    }

    /* Free off array of IFD entries */
    mm_free(mm_pool, p_ifd->p_entries, ((p_ifd->num_entries)*sizeof(ifd_ifdentry_t)));
  }

  /* Free off the IFD itself */
  mm_free(mm_pool, p_ifd, sizeof(ifd_ifd_t));
  *pp_ifd = NULL;

} /* Function ifd_free_ifd */


/** ifd_new_ifdlink() creates a new IFD list link, returning TRUE
 * if it succeeds, else returning FALSE and setting VMERROR.
 */
static Bool ifd_new_ifdlink(
  ifd_reader_t*    p_reader,       /* I */
  ifd_ifdlink_t**  pp_new_ifdlink) /* O */
{
  ifd_ifdlink_t*   p_ifdlink;

  HQASSERT((p_reader != NULL),
           "ifd_new_ifdlink: NULL reader pointer");
  HQASSERT((pp_new_ifdlink != NULL),
           "ifd_new_ifdlink: NULL pointer to returned IFDlink pointer");

  p_ifdlink = mm_alloc(p_reader->mm_pool, sizeof(ifd_ifdlink_t),
                       MM_ALLOC_CLASS_TIFF_IFDLINK);
  if ( p_ifdlink != NULL ) {
    /* Got a IFD link - initialise it */
    DLL_RESET_LINK(p_ifdlink, dll);
    p_ifdlink->p_ifd = NULL;
  } else
    return error_handler(VMERROR);

  *pp_new_ifdlink = p_ifdlink;
  return TRUE;
} /* Function ifd_new_ifdlink */


/** ifd_free_ifdlink() releases a IFD list link and its IFD if
 * it has one.
 */
static void ifd_free_ifdlink(
  ifd_reader_t*    p_reader,     /* I */
  ifd_ifdlink_t**  pp_ifdlink)   /* I */
{
 ifd_ifdlink_t*   p_ifdlink;

  HQASSERT((p_reader != NULL),
           "ifd_free_ifdlink: NULL reader pointer");
  HQASSERT((pp_ifdlink != NULL),
           "ifd_free_ifdlink: NULL pointer to IFDlink pointer");
  HQASSERT((*pp_ifdlink != NULL),
           "ifd_free_ifdlink: NULL IFDlink pointer");

  p_ifdlink = *pp_ifdlink;

  /* If we have an IFD then free it */
  if ( p_ifdlink->p_ifd != NULL ) {
    ifd_free_ifd(p_reader->mm_pool, &(p_ifdlink->p_ifd));
  }

  mm_free(p_reader->mm_pool, p_ifdlink, sizeof(ifd_ifdlink_t));
  *pp_ifdlink = NULL;

} /* Function ifd_free_ifdlink */


/** ifd_new_reader() creates a new ifd reader context with the given
 * buffer context.  It returns TRUE if the reader is created ok, else
 * FALSE and sets VMERROR.
 */
Bool ifd_new_reader(
  tiff_file_t*    p_file,         /* I */
  mm_pool_t       mm_pool,        /* I */
  ifd_ifdentry_data_t* known_tags,
  Bool          f_abort_on_unknown,
  Bool          f_strict,
  Bool          f_verbose,
  ifd_reader_t**  pp_new_reader)  /* O */
{
  ifd_reader_t*  p_reader;

  HQASSERT((p_file != NULL),
           "ifd_new_reader: NULL file pointer");
  HQASSERT((pp_new_reader != NULL),
           "ifd_new_reader: NULL pointer to returned pointer");

  p_reader = mm_alloc(mm_pool, sizeof(ifd_reader_t),
                      MM_ALLOC_CLASS_TIFF_READER);
  if ( p_reader == NULL ) {
    return error_handler(VMERROR);
  }

  /* Initialise the reader */
  p_reader->mm_pool           = mm_pool;
  p_reader->current_ifd_index = 0;
  p_reader->p_current_ifd     = NULL;
  p_reader->count_ifds        = 0;
  DLL_RESET_LIST(&(p_reader->dls_ifds));
  p_reader->first_ifd_offset  = 0;
  p_reader->p_file            = p_file;
  p_reader->known_tags        = known_tags;
  p_reader->f_abort_on_unknown= f_abort_on_unknown;
  p_reader->f_strict          = f_strict;
  p_reader->f_verbose         = f_verbose;

  /* Return pointer and success flag */
  *pp_new_reader = p_reader;
  return TRUE ;

} /* Function ifd_new_reader */


/** ifd_ifdentry_compare() compare function for qsort. Used to resort the
 * tags if they are found to be out of order when being read in.
 */
static int CRT_API ifd_ifdentry_compare(
  const void* arg1,     /* I */
  const void* arg2)     /* I */
{
   ifd_ifdentry_t *p_entry1 = (ifd_ifdentry_t*)arg1;
   ifd_ifdentry_t *p_entry2 = (ifd_ifdentry_t*)arg2;

   return((int)p_entry1->tag - (int)p_entry2->tag);

} /* Function ifd_ifdentry_compare */


/** ifd_ifd_offsetnext() returns the offset of the next
 * IFD after the given IFD.
 */
uint32 ifd_ifd_offsetnext(
  ifd_ifd_t*     p_ifd)          /* I */
{
  HQASSERT((p_ifd != NULL),
           "ifd_ifd_offsetnext: NULL IFD pointer");

  return(p_ifd->offset_next);

} /* Function ifd_ifd_offsetnext */


/** ifd_get_entry_indirect() loads offset IFD entry values from the
 * given buffer (repositioned in the file if needed) for the given
 * entry.  The entry must be marked as having a pending value load.
 * If the values are successfully loaded then it returns TRUE, else
 * FALSE and one of IOERROR, VMERROR, or UNDEFINED set.
 */
Bool ifd_get_entry_indirect(
  tiff_file_t*      p_file,         /* I */
  mm_pool_t         mm_pool,        /* I */
  ifd_ifdentry_t*  p_entry)        /* I/O */
{
  uint32            size;
  uint32            count;
  void*             p_data;
  union {
    tiff_byte_t* byte;
    tiff_ascii_t * ascii;
    tiff_undefined_t * undef;
    tiff_sbyte_t * sbyte;
    tiff_short_t * ushort;
    tiff_sshort_t * sshort;
    tiff_long_t * ulong;
    tiff_slong_t * slong;
    tiff_rational_t * rational;
    tiff_srational_t * srational;
    tiff_float_t * ffloat;
    tiff_double_t * ddouble;
  } pval;


  HQASSERT((p_entry != NULL),
           "ifd_get_tag_indirect: NULL entry pointer");
  HQASSERT((((p_entry->flags)&ENTRY_INDIRECT_PENDING) == ENTRY_INDIRECT_PENDING),
           "ifd_get_tag_indirect: no pending indirect load for entry");

  /* Locate tag data in the buffer */
  if ( !tiff_file_seek(p_file, p_entry->u.tiff_long) ) {
    return(FALSE);
  }

  /* Extract useful number */
  count = p_entry->count;

  /* Allocate context signalling VMERROR as required */
  size  = count * CAST_SIZET_TO_UINT32(internal_tag_type_size[p_entry->type]);
  p_data = mm_alloc(mm_pool, size, MM_ALLOC_CLASS_TIFF_VALUE);
  if ( p_data == NULL ) {
    return error_handler(VMERROR);
  }

  /* Read the appropriate number of values */
  switch ( p_entry->type ) {
  case ENTRY_TYPE_ASCII:
    HQASSERT((count > 4),
             "ifd_get_tag_indirect: value count seems small for ASCIIs");
    pval.ascii = p_entry->p_data = p_data;
    if ( !tiff_get_ascii(p_file, count, pval.ascii) ) {
      return(FALSE);
    }
    break;

  case ENTRY_TYPE_BYTE:
  case ENTRY_TYPE_UNDEFINED:
    HQASSERT((count > 4),
             "ifd_get_tag_indirect: value count seems small for BYTEs");
    pval.byte = p_entry->p_data = p_data;
    if ( !tiff_get_byte(p_file, count, pval.byte) ) {
      return(FALSE);
    }
    break;

  case ENTRY_TYPE_SBYTE:
    HQASSERT((count > 4),
             "ifd_get_tag_indirect: value count seems small for SBYTEs");
    pval.sbyte = p_entry->p_data = p_data;
    if ( !tiff_get_sbyte(p_file, count, pval.sbyte) ) {
      return(FALSE);
    }
    break;


  case ENTRY_TYPE_SHORT:
    HQASSERT((count > 2),
             "ifd_get_tag_indirect: value count seems small for SHORTs");
    pval.ushort = p_entry->p_data = p_data;
    if ( !tiff_get_short(p_file, count, pval.ushort) ) {
      return(FALSE);
    }
    break;

  case ENTRY_TYPE_IFD13:
  case ENTRY_TYPE_LONG:
    HQASSERT((count > 1),
             "ifd_get_tag_indirect: value count seems small for LONGs");
    pval.ulong = p_entry->p_data = p_data;
    if ( !tiff_get_long(p_file, count, pval.ulong) ) {
      return(FALSE);
    }
    break;

  case ENTRY_TYPE_RATIONAL:
    pval.rational = p_entry->p_data = p_data;
    if ( !tiff_get_rational(p_file, count, *pval.rational) ) {
      return(FALSE);
    }
    break;

  case ENTRY_TYPE_SSHORT:
    HQASSERT((count > 2),
             "ifd_get_tag_indirect: value count seems small for SSHORTs");
    pval.sshort = p_entry->p_data = p_data;
    if ( !tiff_get_sshort(p_file, count, pval.sshort) ) {
      return(FALSE);
    }
    break;

  case ENTRY_TYPE_SLONG:
    HQASSERT((count > 1),
             "ifd_get_tag_indirect: value count seems small for SLONGs");
    pval.slong = p_entry->p_data = p_data;
    if ( !tiff_get_slong(p_file, count, pval.slong) ) {
      return(FALSE);
    }
    break;

  case ENTRY_TYPE_SRATIONAL:
    pval.srational = p_entry->p_data = p_data;
    if ( !tiff_get_srational(p_file, count, *pval.srational) ) {
      return(FALSE);
    }
    break;

  case ENTRY_TYPE_FLOAT:
    pval.ffloat = p_entry->p_data = p_data;
    if ( !tiff_get_float(p_file, count, pval.ffloat) ) {
      return(FALSE);
    }
    break;

  case ENTRY_TYPE_DOUBLE:
    pval.ddouble = p_entry->p_data = p_data;
    if ( !tiff_get_double(p_file, count, pval.ddouble) ) {
      return(FALSE);
    }
    break;

  default:
    HQFAIL("ifd_get_tag_indirect: should have already caught unexpected type");
    return(FALSE);
  }

  /* Indirect load no longer pending */
  p_entry->flags &= (~ENTRY_PENDING);

  return(TRUE);

} /* Function ifd_get_tag_indirect */

/** ifd_tag_name() returns a pointer to the name for the given tag, or
 * the string "UnknownTag" if the tag is not recognised.
 */
uint8* ifd_tag_name(
  tiff_short_t      tag,
  ifd_reader_t*  p_reader )          /* I */
{
  ifd_ifdentry_data_t* p_tag;

  /* Look for matching tag and return pointer to its name */
  p_tag = p_reader->known_tags;
  do {
    if ( p_tag ->tag == tag ) {
      break;
    }
    p_tag++;
  } while ( p_tag->tag != TAG_Unknown );

  return(p_tag->name);

} /* Function ifd_tag_name */



/** ifd_new_ifd() creates a new IFD context.  It returns TRUE if the
 * buffer is created ok, else FALSE and sets VMERROR.
 */
Bool ifd_new_ifd(
  mm_pool_t     mm_pool,      /* I */
  ifd_ifd_t**   pp_new_ifd)   /* O */
{
  ifd_ifd_t* p_ifd;

  HQASSERT((pp_new_ifd != NULL),
           "ifd_new_ifd: NULL pointer to returned IFD pointer");

  p_ifd = mm_alloc(mm_pool, sizeof(ifd_ifd_t), MM_ALLOC_CLASS_TIFF_IFD);
  if ( p_ifd != NULL ) {
    /* Got an IFD - initialise it */
    p_ifd->offset      = p_ifd->offset_next = 0;
    p_ifd->num_entries = 0;
    p_ifd->p_entries   = NULL;
  } else
    return error_handler(VMERROR);

  *pp_new_ifd = p_ifd;
  return TRUE;
} /* Function ifd_new_ifd */


/** ifd_ifd_read() reads and IFD and its entries from the given buffer starting
 * at the given file offset.  If f_read_indirect is TRUE then any entries whose
 * values are stored elsewhere in the file will be loaded as well, otherwise they
 * are not and should be loaded later with a call to ifd_get_entry_indirect()
 * error_action controls how various IFD reading errors are handled.
 */
Bool ifd_ifd_read(
  ifd_reader_t*  p_reader,
  uint32        offset,         /* I */
  ifd_ifd_t*    p_ifd)          /* O */
{
  tiff_short_t  tag;
  tiff_short_t  type;
  tiff_short_t  entry_count;
  tiff_short_t  tag_prev;
  int32         i;
  Bool          f_seen_unknown_type = FALSE;
  Bool          f_needs_sort = FALSE;
  Bool          f_abort_on_unknown;
  Bool          f_strict;
  Bool          f_verbose;
  uint32        size;
  tiff_long_t   count;
  ifd_ifdentry_t*  p_entry;
  ifd_ifdentry_data_t* p_known_tag;
  tiff_file_t*    p_file;



  HQASSERT((p_ifd != NULL),
           "ifd_ifd_read: NULL IFD pointer");

  f_abort_on_unknown = p_reader->f_abort_on_unknown;
  f_verbose = p_reader->f_verbose;
  f_strict = p_reader->f_strict;

  /* Extract constant values */
  p_file = p_reader->p_file;

  if ( offset < 8 ) {
    return detail_error_handler(RANGECHECK, "Invalid offset to IFD.");
  }
  if ( (offset&1) != 0 ) {
    if ( f_strict ) {
      return detail_error_handler(UNDEFINED, "IFD offset is not word aligned.");

    } else if ( f_verbose ) {
      monitorf(UVS("%%%%[ Warning: IFD offset is not word aligned. ]%%%%\n"));
    }
  }

  /* Reposition file for start of IFD */
  if ( !tiff_file_seek(p_file, offset) ) {
    return detail_error_handler(IOERROR, "Unable to set file position to start of IFD.");
  }

  /* Find number of tags in IFD */
  if ( !tiff_get_short(p_file, 1, &entry_count) ) {
    return detail_error_handler(IOERROR, "Unable to read number of entries in IFD.");
  }
  if ( (int32)entry_count == 0 ) {
    return detail_error_handler(UNDEFINED, "IFD has no entries.");
  }

  p_ifd->offset      = offset;
  p_ifd->num_entries = (uint32)entry_count;

  /* Allocate buffer context */
  p_entry = mm_alloc(p_reader->mm_pool, ((p_ifd->num_entries)*sizeof(ifd_ifdentry_t)),
                     MM_ALLOC_CLASS_TIFF_IFDENTRIES);
  if ( p_entry == NULL ) {
    return error_handler(VMERROR);
  }

  /* Add array of entries to IFD */
  p_ifd->p_entries = p_entry;

  tag_prev = 0;

  for ( i = 0; i < (int32)entry_count; i++ ) {

    /* Read tag id, type, and count */
    if ( !tiff_get_short(p_file, 1, &tag) ||
         !tiff_get_short(p_file, 1, &type) ||
         !tiff_get_long(p_file, 1, &count) ) {
      return detail_error_handler(IOERROR, "Unable to read IFD entry details.");
    }

    if ( (type >= ENTRY_TYPE_FIRST) && (type <= ENTRY_TYPE_LAST) ) {
      /* Got a tag we know about - yippee! */

      /* Whinge about out of sequence tags */
      if ( tag < tag_prev ) {
        if ( f_strict ) {
          return detail_error_handler(UNDEFINED, "IFD entries are out of sequence.");
        }
        f_needs_sort = TRUE;
        if ( f_verbose ) {
          monitorf(UVM("%%%%[ Warning: IFD entry Tag %s(%u) out of sequence ]%%%%\n"),
                   ifd_tag_name(tag, p_reader), (uint32)tag);
        }
      }
      tag_prev = tag;

      /* Start setting up tag structure */
      p_entry->tag   = tag;
      p_entry->type  = type;
      p_entry->count = count;
      p_entry->flags = (count > 1) ? ENTRY_ARRAY : 0;

      /* Check if value fits in Offset field (which is a LONG) */
      size = count * CAST_SIZET_TO_UINT32(file_entry_type_size[type]);
      if ( size <= ENTRY_SIZE_TYPE_LONG ) {
        /* It does - extract value according to type */

        switch ( type ) {
        case ENTRY_TYPE_ASCII:
          /* All these can be read as unsigned bytes */
          if ( !tiff_get_ascii(p_file, NUM_ASCII_PER_LONG, &(p_entry->u.tiff_ascii[0])) ) {
            return detail_error_handler(IOERROR, "Unable to read values as ASCIIs.");
          }
          break;

        case ENTRY_TYPE_BYTE:
        case ENTRY_TYPE_UNDEFINED:
          /* All these can be read as unsigned bytes */
          if ( !tiff_get_byte(p_file, NUM_BYTE_PER_LONG, &(p_entry->u.tiff_byte[0])) ) {
            return detail_error_handler(IOERROR, "Unable to read values as BYTEs.");
          }
          break;

        case ENTRY_TYPE_SBYTE:
          /* All these can be read as unsigned bytes */
          if ( !tiff_get_sbyte(p_file, NUM_SBYTE_PER_LONG, &(p_entry->u.tiff_sbyte[0])) ) {
            return detail_error_handler(IOERROR, "Unable to read values as SBYTEs.");
          }
          break;

        case ENTRY_TYPE_SHORT:
          if ( !tiff_get_short(p_file, NUM_SHORT_PER_LONG, &(p_entry->u.tiff_short[0])) ) {
            return detail_error_handler(IOERROR, "Unable to read values as SHORTs.");
          }
          break;

        case ENTRY_TYPE_SSHORT:
          if ( !tiff_get_sshort(p_file, NUM_SSHORT_PER_LONG, &(p_entry->u.tiff_sshort[0])) ) {
            return detail_error_handler(IOERROR, "Unable to read values as SSHORTs.");
          }
          break;

        case ENTRY_TYPE_SLONG:
          if ( !tiff_get_slong(p_file, 1, &(p_entry->u.tiff_slong)) ) {
            return detail_error_handler(IOERROR, "Unable to read SLONG value.");
          }
          break;

        case ENTRY_TYPE_FLOAT:
          if ( !tiff_get_float(p_file, 1, &(p_entry->u.tiff_float)) ) {
            return detail_error_handler(IOERROR, "Unable to read FLOAT value.");
          }
          break;

        default:
          HQFAIL("tiff_ifd_read: reading unknown type - assuming long");
          /* FALLTHROUGH */

        case ENTRY_TYPE_IFD13:
        case ENTRY_TYPE_LONG:
          if ( !tiff_get_long(p_file, 1, &(p_entry->u.tiff_long)) ) {
            return detail_error_handler(IOERROR, "Unable to read LONG value.");
          }
          break;

        }

      } else { /* Value too big for offset - read as Offset */

        if ( !tiff_get_long(p_file, 1, &(p_entry->u.tiff_long)) ) {
          return detail_error_handler(IOERROR, "Unable to read entry value offset.");
        }

        if ( ((p_entry->u.tiff_long)&1) != 0 ) {
          /* All value offsets should be word aligned */
          if ( f_strict ) {
            return detail_error_handler(UNDEFINED, "IFD entry value offset is not word aligned.");

          } else if ( f_verbose ) {
            monitorf(UVM("%%%%[ Warning: IFD entry value offset for Tag %s(%u) is not word aligned ]%%%%\n"),
                     ifd_tag_name(tag, p_reader), (uint32)tag);
          }
        }

        /* Mark that we havent read the value in yet */
        p_entry->flags |= ENTRY_INDIRECT_PENDING;
      }

    } else { /* Unknown entry type - TIFF6 spec says entry should be skipped */

      if ( f_verbose ) {
        /* We report unknown types ONCE */
        if ( !f_seen_unknown_type ) {
          monitorf(UVM("%%%%[ Warning: Unknown IFD entry Type (%u) for Tag %s(%u) ]%%%%\n"),
                   (uint32)type, ifd_tag_name(tag, p_reader), (uint32)tag);
        }
        f_seen_unknown_type = TRUE;
      }

      if ( f_abort_on_unknown ) {
        /* We hates unknown types we does */
        return detail_error_handler(TYPECHECK, "Unknown IFD entry Type.");
      }

      /* Swallow offset LONG field */
      if ( !tiff_get_long(p_file, 1, &offset) ) {
        return detail_error_handler(IOERROR, "Unable to read LONG value.");
      }
    }

    /* Move onto next entry to fill in */
    p_entry++;
  }

  /* Get offset to next IFD - 0 implies no more IFDs */
  if ( !tiff_get_long(p_file, 1, &(p_ifd->offset_next)) ) {
    return detail_error_handler(IOERROR, "Unable to read offset to next IFD.");
  }

  /* Sort tags if they were not already */
  if ( f_needs_sort ) {
    qsort((void*)p_ifd->p_entries, p_ifd->num_entries, sizeof(ifd_ifdentry_t), ifd_ifdentry_compare);
  }

  /* Scan IFD entries for any indirect reads
   * We do this now to prevent thrashing of the buffer when reading the main IFD.
   */
  p_known_tag = p_reader->known_tags;
  p_entry = p_ifd->p_entries;
  for ( i = 0; i < (int32)entry_count; i++ ) {

    /* Find matching tag in the known tags table */
    while ( p_known_tag->tag < p_entry->tag ) {
      p_known_tag++;
    }

    if ( p_known_tag->tag == p_entry->tag ) {
      /* We know this tag - do any indirect loading required */

      if ( p_known_tag->f_read_immediate && (((p_entry->flags)&ENTRY_PENDING) != 0) ) {
        /*  We allow indirect reading for this tag - do it */

        if ( !ifd_get_entry_indirect(p_file, p_reader->mm_pool, p_entry) ) {
          /* Failed indirect read - whinge time */
          return detail_error_handler(IOERROR, "Unable to read entry offset value(s).");
        }
      }

    } else { /* Entry tag is unknown - do as requested */

      if ( f_verbose ) {
        monitorf(UVM("%%%%[ Warning: IFD entry Tag (%u) unknown ]%%%%\n"),
                 (uint32)(p_entry->tag));
      }

      if ( f_abort_on_unknown ) {
        return detail_error_handler(UNDEFINED, "Unknown IFD entry tag.");
      }
    }

    /* Move onto next IFD entry */
    p_entry++;
  }

  /* Whoppee! */
  return(TRUE);

} /* Function ifd_ifd_read */


/** ifd_free_reader() releases a ifd reader context and any IFDs (and
 * their entries, and so forth) that have been read in.
 */
void ifd_free_reader(
  ifd_reader_t** pp_reader)      /* I */
{
  ifd_reader_t*  p_reader;
  ifd_ifdlink_t* p_ifdlink;

  HQASSERT((pp_reader != NULL),
           "ifd_free_reader: NULL pointer to context pointer");
  HQASSERT((*pp_reader != NULL),
           "ifd_free_reader: NULL context pointer");

  p_reader = *pp_reader;

  /* Free off the file */
  tiff_free_file(&(p_reader->p_file));

  /* Free all IFDs */
  while ( !DLL_LIST_IS_EMPTY(&(p_reader->dls_ifds)) ) {
    p_ifdlink = DLL_GET_HEAD(&(p_reader->dls_ifds), ifd_ifdlink_t, dll);
    DLL_REMOVE(p_ifdlink, dll);
    ifd_free_ifdlink(p_reader, &p_ifdlink);
  }

  /* Free of memory and NULL original pointer */
  mm_free(p_reader->mm_pool, p_reader, sizeof(ifd_reader_t));
  *pp_reader = NULL;

} /* Function ifd_free_reader */


/** ifd_read_ifds() reads all the IFDs in a ifd file, and returns the number
 * of IFDs successfully read.  If all the IFDs are read ok the function returns
 * TRUE, else it returns FALSE and sets VMERROR, IOERROR, UNDEFINED, or
 * TYPECHECK.  This function should only be called once for a ifd file.
 */
Bool ifd_read_ifds(
  ifd_reader_t*   p_reader,       /* I */
  uint32*         p_number_ifds)  /* ) */
{
  tiff_file_t*    p_file;
  ifd_ifd_t*      p_ifd = NULL;
  uint32          offset;
  uint32          count_ifds;
  ifd_ifdlink_t*  p_ifdlink = NULL;
  int32           result;

  HQASSERT((p_reader != NULL),
           "ifd_read_ifds: NULL reader pointer");
  HQASSERT((DLL_LIST_IS_EMPTY(&(p_reader->dls_ifds))),
           "ifd_read_ifds: ifd list is not empty");
  HQASSERT((p_number_ifds != NULL),
           "ifd_read_ifds: NULL pointer to returned number of IFDs");

  /* Extract constant values */
  p_file = p_reader->p_file;
  offset = p_reader->first_ifd_offset;

  /* Read all IFDs until error or no more (offset is zero) */
  count_ifds = 0;
  do {
    /* Create link for main IFD */
    if ( !ifd_new_ifdlink(p_reader, &p_ifdlink) ) {
      return FALSE ;
    }

    /* Add links to end of list - main file IFD will be at head */
    DLL_ADD_TAIL(&(p_reader->dls_ifds), p_ifdlink, dll);

    /* Create IFD */
    if ( !ifd_new_ifd(p_reader->mm_pool, &p_ifd) ) {
      return FALSE ;
    }
    p_ifdlink->p_ifd = p_ifd;

    /* Read next IFD and get offset to subsequent one */
    result = ifd_ifd_read(p_reader, offset, p_ifd);
    if ( result ) {
      offset = ifd_ifd_offsetnext(p_ifd);
      count_ifds++;
    }

  } while ( result && (offset != 0) );

  /* Return how many IFDs we successfully read */
  *p_number_ifds = p_reader->count_ifds = count_ifds;

  HQTRACE(ifd_debug,
          ("ifd_read_ifds: successfully read %d IFDs", count_ifds));

  return result ;

} /* Function ifd_read_ifds */



/*
 * ifd_type_string()
 */
uint8* ifd_type_string(
  uint32        type)           /* I */
{
  if ( (type >= ENTRY_TYPE_FIRST) && (type <= ENTRY_TYPE_LAST) ) {
    return(type_string[type]);
  }

  return(UNKNOWN_T);

} /* Function ifd_type_string */




/** ifd_verify_ifd() checks the given IFD against an array of given entry
 * data.  It returns TRUE if the IFD satisfies the check data, else it
 * returns FALSE and sets UNDEFINED, TYPECHECK, or RANGECHECK.  Note, the
 * array of check data must be in ascending tag order, and all IFD entries
 * with offset values must have been loaded.
 */
Bool ifd_verify_ifd(
  ifd_reader_t*           p_reader,         /* I */
  ifd_ifd_t*              p_ifd,            /* I */
  ifd_ifdentry_check_t*   p_ifd_defaults,   /* I */
  uint32                  num_defaults)     /* I */
{
  uint32                  i;
  uint32                  j;
  uint32                  value;
  uint32                  entries_left;
  Bool                    f_verbose;
  Bool                    f_strict;
  ifd_ifdentry_check_t*  p_ifdentry_check;
  ifd_ifdentry_t*        p_entry;

  HQASSERT((p_ifd != NULL),
           "ifd_verify_ifd: NULL IFD pointer");
  HQASSERT((p_ifd_defaults != NULL),
           "ifd_verify_ifd: NULL defaults pointer");
  HQASSERT((num_defaults > 0),
           "ifd_verify_ifd: no defaults to check IFD against");

  /* Extract useful vars */
  entries_left = p_ifd->num_entries;
  p_entry      = p_ifd->p_entries;

  f_verbose = p_reader->f_verbose;
  f_strict = p_reader->f_strict;

  /* Loop over list of check entries */
  p_ifdentry_check = p_ifd_defaults;
  for ( i = 0; i < num_defaults; i++ ) {

    /* Skip any IFD entries that do not need checking */
    while ( (p_entry->tag < p_ifdentry_check->tag) &&
            (--entries_left > 0) ) {
      p_entry++;
    }

    /*
     * If we have run out of IFD entries then all that is left to
     * check is that there are no more mandatory entries.
     */
    if ( entries_left == 0 ) {
      do {
        if ( ((p_ifdentry_check->flags)&HF_MANDATORY) != 0 ) {
          if ( f_verbose ) {
            monitorf(UVM("%%%%[ Error: IFD entry %s(%u) missing ]%%%%\n"),
                     ifd_tag_name(p_ifdentry_check->tag, p_reader), (uint32)(p_ifdentry_check->tag));
          }
          return detail_error_handler(UNDEFINED, "Mandatory IFD entry missing from IFD file.");
        }
        p_ifdentry_check++;
      } while ( ++i < num_defaults );
      return(TRUE);
    }

    if ( p_entry->tag > p_ifdentry_check->tag ) {
      /* Check entry not in ifd - see if check entry is mandatory */

      if ( ((p_ifdentry_check->flags)&HF_MANDATORY) != 0 ) {
        if ( f_verbose ) {
          monitorf(UVM("%%%%[ Error: IFD entry %s(%u) missing ]%%%%\n"),
                   ifd_tag_name(p_ifdentry_check->tag, p_reader), (uint32)(p_ifdentry_check->tag));
        }
        return detail_error_handler(UNDEFINED, "Mandatory IFD entry missing from IFD file.");
      }

    } else { /* Matched entry tags - do detailed checks */
      HQASSERT((p_entry->tag == p_ifdentry_check->tag),
               "ifd_verify_ifd: tag mismatch");

      /* 1. Type check */
      if ( (BIT(p_entry->type)&(p_ifdentry_check->strict_types)) == 0 ) {

        /* Whinge if strict mode or not compatible type */
        if ( f_strict || ((BIT(p_entry->type)&(p_ifdentry_check->compatible_types)) == 0) ) {
          if ( f_verbose ) {
            monitorf(UVM("%%%%[ Error: IFD entry %s(%u) has wrong type - %s(%u) ]%%%%\n"),
                     ifd_tag_name(p_ifdentry_check->tag, p_reader), (uint32)(p_ifdentry_check->tag),
                     ifd_type_string(p_entry->type), (uint32)p_entry->type);
          }
          return detail_error_handler(TYPECHECK, "IFD entry of wrong type.");

        } else if ( f_verbose ) {
          /* Blather on if requested to */
          monitorf(UVM("%%%%[ Warning: IFD entry %s(%u) has incorrent type - %s(%u) ]%%%%\n"),
                   ifd_tag_name(p_ifdentry_check->tag, p_reader), (uint32)(p_ifdentry_check->tag),
                   ifd_type_string(p_entry->type), (uint32)p_entry->type);
        }
      }

      if ( ((p_entry->flags)&ENTRY_PENDING) != 0 ) {
        return detail_error_handler(UNDEFINED, "IFD entry not loaded - cannot validate.");
      }

      /* 2. Validate invariant */
      if ( ((p_ifdentry_check->flags)&HF_INVARIANT) != 0) {

        if ( (BIT(p_entry->type)&TB_INTEGER) != 0 ) {
          /* er, but only integer values - sheesh */

          if ( ((p_ifdentry_check->flags)&HF_ARRAY) != 0 ) {
            /* Checking an integer array */

            /* Check array sizes match */
            if ( p_ifdentry_check->count != p_entry->count ) {
              if ( f_verbose ) {
                monitorf(UVM("%%%%[ Error: IFD entry %s(%u) has wrong array size (%u) ]%%%%\n"),
                         ifd_tag_name(p_ifdentry_check->tag, p_reader), (uint32)(p_ifdentry_check->tag),
                         (uint32)p_entry->count);
              }
              return detail_error_handler(RANGECHECK, "IFD array entry has wrong size.");
            }

            /* Check array contents match */
            for ( j = 0; j < p_ifdentry_check->count; j++ ) {
              value = ifd_entry_array_index(p_entry, j);
              if ( value != p_ifdentry_check->p_value[j] ) {
                if ( f_verbose ) {
                  monitorf(UVM("%%%%[ Error: IFD entry %s(%u) array value (%u) invalid ]%%%%\n"),
                           ifd_tag_name(p_ifdentry_check->tag, p_reader), (uint32)(p_ifdentry_check->tag),
                           value);
                }
                return detail_error_handler(RANGECHECK, "IFD entry array has invalid value.");
              }
            }

          } else { /* Check entry wants scalar value */

            if ( p_entry->count != 1 ) {
              /* Check entry says scalar, the IFD entry says array - wah waaaahh! */
              if ( f_verbose ) {
                monitorf(UVM("%%%%[ Error: IFD entry %s(%u) value count is %u when expecting 1 ]%%%%\n"),
                         ifd_tag_name(p_ifdentry_check->tag, p_reader), (uint32)(p_ifdentry_check->tag),
                         (uint32)(p_entry->count));
              }
              return detail_error_handler(TYPECHECK, "IFD entry value count is greater than 1.");
            }

            switch ( p_entry->type ) {
            case ENTRY_TYPE_ASCII:
            case ENTRY_TYPE_BYTE:
            case ENTRY_TYPE_SHORT:
            case ENTRY_TYPE_IFD13:
            case ENTRY_TYPE_LONG:
              value = ifd_entry_unsigned_int(p_entry);
              break;

            default:
              /* This should have been caught earlier, but you never know */
              return detail_error_handler(TYPECHECK, "IFD entry has invalid type.");
            }

            switch ( p_ifdentry_check->flags&(HF_MINMAX|HF_OPTIONS) ) {
            case 0:
              /* Check value matches */
              if ( value != p_ifdentry_check->value ) {
                if ( f_verbose ) {
                  monitorf(UVM("%%%%[ Error: IFD entry %s(%u) has invalid value - %u ]%%%%\n"),
                           ifd_tag_name(p_ifdentry_check->tag, p_reader), (uint32)(p_ifdentry_check->tag),
                           value);
                }
                return detail_error_handler(RANGECHECK, "IFD entry value is invalid.");
              }
              break;

            case HF_MINMAX:
              /* Check value is in given min/max range */
              if ( (value < p_ifdentry_check->p_value[0]) ||
                   (value > p_ifdentry_check->p_value[1]) ) {
                if ( f_verbose ) {
                  monitorf(UVM("%%%%[ Error: IFD entry %s(%u) value (%u) is out of range ]%%%%\n"),
                           ifd_tag_name(p_ifdentry_check->tag, p_reader), (uint32)(p_ifdentry_check->tag),
                           value);
                }
                return detail_error_handler(RANGECHECK, "IFD entry value is out of range.");
              }
              break;

            case HF_OPTIONS:
              /* Check value exists in array */
              for ( j = 0; j < p_ifdentry_check->count; j++ ) {
                if ( value == p_ifdentry_check->p_value[j] ) {
                  break;
                }
              }
              if ( j == p_ifdentry_check->count ) {
                if ( f_verbose ) {
                  monitorf(UVM("%%%%[ Error: IFD entry %s(%u) has invalid value (%u) ]%%%%\n"),
                           ifd_tag_name(p_ifdentry_check->tag, p_reader), (uint32)(p_ifdentry_check->tag),
                           value);
                }
                return detail_error_handler(RANGECHECK, "IFD entry has invalid value.");
              }
              break;
            }
          }

        } else { /* Whinge about lack of non-integer invariant checking */

          if ( f_verbose ) {
            monitorf(UVM("%%%%[ Warning: non-integer IFD entry %s(%u) not being verified ]%%%%\n"),
                     ifd_tag_name(p_ifdentry_check->tag, p_reader), (uint32)(p_ifdentry_check->tag));
          }
        }
      }
    }

    /* Move onto next check entry */
    p_ifdentry_check++;
  }

  return(TRUE);

} /* Function ifd_verify_ifd */



/** ifd_find_entry() returns an IFD entry that matches the given entry tag,
 * else it returns NULL.
 */
ifd_ifdentry_t* ifd_find_entry(
  ifd_ifd_t*   p_ifd,            /* I */
  tiff_short_t  entry_tag)        /* I */
{
  uint32             num_entries;
  ifd_ifdentry_t*  p_entry;

  HQASSERT((p_ifd != NULL),
           "ifd_find_entry: NULL IFD pointer");
  HQASSERT((p_ifd->num_entries > 0),
           "ifd_find_entry: IFD is empty");

  /* Loop until found entry or no more */
  p_entry = p_ifd->p_entries;
  for ( num_entries = p_ifd->num_entries;
        num_entries > 0;
        num_entries--) {
    if ( p_entry->tag > entry_tag ) {
      break;

    } else if ( p_entry->tag == entry_tag ) {
      return(p_entry);
    }
    p_entry++;
  }

  return(NULL);

} /* Function ifd_find_entry */


/** ifd_entry_unsigned_int() returns the LONG scalar value.  It is the callers
 * responsibility to ensure the entry is scalar and of the right signed type.
 */
uint32 ifd_entry_unsigned_int(
  ifd_ifdentry_t*  p_entry)
{
  HQASSERT((p_entry != NULL),
           "ifd_entry_unsigned_int: NULL IFD entry pointer");
  HQASSERT((((p_entry->flags)&ENTRY_ARRAY) == 0),
           "ifd_entry_unsigned_int: IFD entry is an array");

  switch ( p_entry->type ) {
  case ENTRY_TYPE_ASCII:
    return((uint32)p_entry->u.tiff_ascii[0]);

  case ENTRY_TYPE_BYTE:
    return((uint32)p_entry->u.tiff_byte[0]);

  case ENTRY_TYPE_SHORT:
    return((uint32)p_entry->u.tiff_short[0]);

  default:
    HQFAIL("ifd_entry_unsigned_int: unknown IFD entry type");
    /* FALLTHROUGH */
  case ENTRY_TYPE_IFD13:
  case ENTRY_TYPE_LONG:
    return(p_entry->u.tiff_long);
  }
  /* NEVER REACHED */

} /* Function ifd_entry_unsigned_int */


/** ifd_entry_signed_int() returns the LONG scalar value.  It is the callers
 * responsibility to ensure the entry is scalar and of the right signed type.
 */
int32 ifd_entry_signed_int(
  ifd_ifdentry_t*  p_entry)
{
  HQASSERT((p_entry != NULL),
           "ifd_entry_signed_int: NULL IFD entry pointer");
  HQASSERT((((p_entry->flags)&ENTRY_ARRAY) == 0),
           "ifd_entry_signed_int: IFD entry is an array");

  switch ( p_entry->type ) {

  case ENTRY_TYPE_SSHORT:
    return((int32)p_entry->u.tiff_sshort[0]);

  case ENTRY_TYPE_SBYTE:
    return((int32)p_entry->u.tiff_sbyte[0]);

  default:
    HQFAIL("ifd_entry_unsigned_int: unknown IFD entry type");
    /* FALLTHROUGH */

  case ENTRY_TYPE_SLONG:
    return(p_entry->u.tiff_slong);

  }
  /* NEVER REACHED */

} /* Function ifd_entry_signed_int */

void ifd_entry_float(
  ifd_ifdentry_t*  p_entry,   /* I */
  tiff_float_t* flt )         /* O */
{
  *flt = p_entry->u.tiff_float;
}

/** ifd_entry_rational() returns the RATIONAL scalar value.  It is the callers
 * responsibility to ensure the entry is scalar and of the right type.
 */
void ifd_entry_rational(
  ifd_ifdentry_t*  p_entry,        /* I */
  tiff_rational_t   rational)       /* O */
{
  HQASSERT((p_entry != NULL),
           "ifd_entry_rational: NULL IFD entry pointer");
  HQASSERT((((p_entry->flags)&ENTRY_ARRAY) == 0),
           "ifd_entry_rational: IFD entry is an array");
  HQASSERT((p_entry->type == ENTRY_TYPE_RATIONAL),
           "ifd_entry_rational: IFD entry is not RATIONAL type");
  HQASSERT((rational != NULL),
           "ifd_entry_rational: NULL pointer to returned rational");

  /* Rational values are stored indirectly as an array of two LONGs */
  rational[RATIONAL_NUMERATOR] = ((tiff_long_t*)(p_entry->p_data))[RATIONAL_NUMERATOR];
  rational[RATIONAL_DENOMINATOR] = ((tiff_long_t*)(p_entry->p_data))[RATIONAL_DENOMINATOR];

} /* Function ifd_entry_rational */

/** ifd_entry_srational() same as ifd_entry_rational but signed
 */
void ifd_entry_srational(
  ifd_ifdentry_t*  p_entry,        /* I */
  tiff_srational_t   srational)       /* O */
{
  HQASSERT((p_entry != NULL),
           "ifd_entry_srational: NULL IFD entry pointer");
  HQASSERT((((p_entry->flags)&ENTRY_ARRAY) == 0),
           "ifd_entry_srational: IFD entry is an array");
  HQASSERT((p_entry->type == ENTRY_TYPE_SRATIONAL),
           "ifd_entry_srational: IFD entry is not SRATIONAL type");
  HQASSERT((srational != NULL),
           "ifd_entry_srational: NULL pointer to returned SRATIONAL");

  /* Rational values are stored indirectly as an array of two LONGs */
  srational[RATIONAL_NUMERATOR] = ((tiff_slong_t*)(p_entry->p_data))[RATIONAL_NUMERATOR];
  srational[RATIONAL_DENOMINATOR] = ((tiff_slong_t*)(p_entry->p_data))[RATIONAL_DENOMINATOR];

} /* Function ifd_entry_srational */


/** return a pointer to the entry string.
   Note that this string may contain multiple strings separated by NULLs
   It is the callers responsibility to find out the total length of the string.
*/
uint8* ifd_entry_string( ifd_ifdentry_t*  p_entry)
{
  HQASSERT((p_entry != NULL),
           "ifd_entry_string: NULL IFD entry pointer");
  HQASSERT((p_entry->type == ENTRY_TYPE_ASCII),
           "ifd_entry_string: IFD entry is not ASCII type");

  return (uint8*)p_entry->p_data;
}


/** from a byte array extract a non-terminated string */
uint8* ifd_entry_array_fetch_string(
  ifd_ifdentry_t*  p_entry,
  uint32 offset,
  uint32 length,
  uint8* dest)
{
  void*   p_data;
  uint8*  ret = NULL;
  uint32  end = offset + length;

  HQASSERT((p_entry != NULL),
           "ifd_entry_array_fetch_string: NULL entry pointer");
  if (end > p_entry->count)
    return NULL;

  switch ( p_entry->type ) {
  case ENTRY_TYPE_ASCII:
  case ENTRY_TYPE_BYTE:
  case ENTRY_TYPE_UNDEFINED:
    if ( ((p_entry->flags)&ENTRY_INDIRECT) != 0 ) {
      p_data = (tiff_ascii_t*)(p_entry->p_data) + offset;
    } else {
      HQASSERT((end < 4),
               "ifd_entry_array_fetch_string: index too large for ASCII in value field");
      p_data = (tiff_ascii_t*)p_entry->u.tiff_ascii + offset;
    }

    if (dest)
      HqMemCpy(dest, p_data, length);

    ret = p_data;
    break;

  default:
    HQFAIL("ifd_entry_array_fetch_string: unhandled type");
    break;
  }

  return(ret);
}


/** Compare a non-termianted string with substring in array */
Bool ifd_entry_array_check_substring(
  ifd_ifdentry_t*  p_entry,
  uint32 offset,
  uint32 length,
  uint8* src)
{
  uint8*   p_data;
  uint32  end = offset + length;

  HQASSERT((p_entry != NULL),
           "ifd_entry_array_check_substring: NULL entry pointer");
  HQASSERT((src != NULL),
           "ifd_entry_array_check_substring: NULL dest pointer");

  if (end > p_entry->count)
    return FALSE;

  switch ( p_entry->type ) {
  case ENTRY_TYPE_ASCII:
  case ENTRY_TYPE_BYTE:
  case ENTRY_TYPE_UNDEFINED:
    if ( ((p_entry->flags)&ENTRY_INDIRECT) != 0 ) {
      p_data = (tiff_ascii_t*)(p_entry->p_data) + offset;
    } else {
      HQASSERT((end < 4),
               "ifd_entry_array_fetch_string: index too large for ASCII in value field");
      p_data = (tiff_ascii_t*)p_entry->u.tiff_ascii + offset;
    }

    while (offset++ < end) {
      if (*src++ != *p_data++)
        return FALSE;
    }

    break;

  default:
    HQFAIL("ifd_entry_array_check_substring: unhandled type");
    return FALSE;
  }

  return TRUE;
}

/** ifd_entry_array_index() returns the given indexed element of the array entry
 * widened to a uint32.  It is the callers responsibility to ensure that the entry
 * is an array and that the index is in range.
 */
uint32 ifd_entry_array_index(
  ifd_ifdentry_t*  p_entry,      /* I */
  uint32            index)        /* I */
{
  void*   p_data;
  uint32  value;

  HQASSERT((p_entry != NULL),
           "ifd_entry_array_index: NULL entry pointer");
  HQASSERT((index < p_entry->count),
           "ifd_entry_array_index: index out of range");

  switch ( p_entry->type ) {
  case ENTRY_TYPE_ASCII:
    if ( ((p_entry->flags)&ENTRY_INDIRECT) != 0 ) {
      p_data = (tiff_ascii_t*)(p_entry->p_data) + index;
      value = (uint32)*((tiff_ascii_t*)p_data);

    } else {
      HQASSERT((index < 4),
               "ifd_entry_array_index: index too large for ASCII in value field");
      value = (uint32)p_entry->u.tiff_ascii[index];
    }
    break;

  case ENTRY_TYPE_BYTE:
  case ENTRY_TYPE_UNDEFINED:
    if ( ((p_entry->flags)&ENTRY_INDIRECT) != 0 ) {
      p_data = (tiff_byte_t*)(p_entry->p_data) + index;
      value = (uint32)*((tiff_byte_t*)p_data);

    } else {
      HQASSERT((index < 4),
               "ifd_entry_array_index: index too large for BYTE in value field");
      value = (uint32)p_entry->u.tiff_byte[index];
    }
    break;

  case ENTRY_TYPE_SHORT:
    if ( ((p_entry->flags)&ENTRY_INDIRECT) != 0 ) {
      p_data = (tiff_short_t*)(p_entry->p_data) + index;
      value = (uint32)*((tiff_short_t*)p_data);

    } else {
      HQASSERT((index < 2),
               "ifd_entry_array_index: index too large for SHORT in value field");
      value = (uint32)(int32)(p_entry->u.tiff_short[index]);
    }
    break;

  case ENTRY_TYPE_IFD13:
  case ENTRY_TYPE_LONG:
    if ( ((p_entry->flags)&ENTRY_INDIRECT) != 0 ) {
      p_data = (tiff_long_t*)(p_entry->p_data) + index;
      value = (uint32)*((tiff_long_t*)p_data);

    } else {
      HQASSERT((index < 1),
               "ifd_entry_array_index: index too large for LONG in value field");
      value = (uint32)(p_entry->u.tiff_long);
    }
    break;

  default:
    HQFAIL("ifd_entry_array_index: unhandled type");
    value = 0;
    break;
  }

  return(value);

} /* Function ifd_entry_array_index */


/** ifd_signed_entry_array_index() same as
 * ifd_entry_array_index but for signed values.
 */
int32 ifd_signed_entry_array_index(
  ifd_ifdentry_t*  p_entry,      /* I */
  uint32            index)        /* I */
{
  void*   p_data;
  int32  value;

  HQASSERT((p_entry != NULL),
           "ifd_signed_entry_array_index: NULL entry pointer");
  HQASSERT((index < p_entry->count),
           "ifd_signed_entry_array_index: index out of range");

  switch ( p_entry->type ) {
  case ENTRY_TYPE_SBYTE:
    if ( ((p_entry->flags)&ENTRY_INDIRECT) != 0 ) {
      p_data = (tiff_sbyte_t*)(p_entry->p_data) + index;
      /* Gack! */
      value = (int32)*((tiff_sbyte_t*)p_data);

    } else {
      HQASSERT((index < 4),
               "ifd_signed_entry_array_index: index too large for SBYTE in value field");
      value = (int32)(p_entry->u.tiff_sbyte[index]);
    }
    break;

  case ENTRY_TYPE_SSHORT:
    if ( ((p_entry->flags)&ENTRY_INDIRECT) != 0 ) {
      p_data = (tiff_sshort_t*)(p_entry->p_data) + index;
      value = *((tiff_sshort_t*)p_data);

    } else {
      HQASSERT((index < 2),
               "ifd_signed_entry_array_index: index too large for SSHORT in value field");
      value = (int32)(p_entry->u.tiff_sshort[index]);
    }
    break;

  case ENTRY_TYPE_SLONG:
    if ( ((p_entry->flags)&ENTRY_INDIRECT) != 0 ) {
      p_data = (tiff_slong_t*)(p_entry->p_data) + index;
      value = *((tiff_slong_t*)p_data);

    } else {
      HQASSERT((index < 1),
               "ifd_signed_entry_array_index: index too large for SLONG in value field");
      value = (p_entry->u.tiff_slong);
    }
    break;

  default:
    HQFAIL("ifd_signed_entry_array_index: unhandled type");
    value = 0;
    break;
  }

  return(value);

} /* Function ifd_signed_entry_array_index */


/** ifd_entry_rational_array_index() returns the given indexed element of the array entry
 * of rational values.  It is the callers responsibility to ensure that the entry
 * is an array and that the index is in range.
 */
void ifd_entry_rational_array_index(
  ifd_ifdentry_t*  p_entry,      /* I */
  uint32            index,        /* I */
  tiff_rational_t   value)        /* O */
{
  void*   p_data;

  HQASSERT((p_entry != NULL),
           "ifd_entry_array_index: NULL entry pointer");
  HQASSERT((index < p_entry->count),
           "ifd_entry_array_index: index out of range");
  HQASSERT((p_entry->type == ENTRY_TYPE_RATIONAL)||
            (p_entry->type == ENTRY_TYPE_SRATIONAL),
           "ifd_entry_rational_array_index: bad type");
  HQASSERT((p_entry->flags & ENTRY_ARRAY),
           "ifd_entry_rational_array_index: IFD entry is not an array");
  HQASSERT((p_entry->flags & ENTRY_INDIRECT),
           "ifd_entry_rational_array_index: IFD entry is not indirect");

  p_data = (tiff_long_t*)(p_entry->p_data) + index;
  value[RATIONAL_NUMERATOR] = ((tiff_long_t*)(p_data))[RATIONAL_NUMERATOR];
  value[RATIONAL_DENOMINATOR] = ((tiff_long_t*)(p_data))[RATIONAL_DENOMINATOR];

} /* Function ifd_entry_array_index */



/** ifd_entry_type() returns the type of the IFD entry.
 */
tiff_short_t ifd_entry_type(
  ifd_ifdentry_t*  p_entry)        /* I */
{
  HQASSERT((p_entry != NULL),
           "ifd_entry_type: NULL IFD entry pointer");

  return(p_entry->type);

} /* Function ifd_entry_type */


/** ifd_entry_count() returns the count of values for the IFD entry.
 */
uint32 ifd_entry_count(
  ifd_ifdentry_t*  p_entry)        /* I */
{
  HQASSERT((p_entry != NULL),
           "ifd_entry_count: NULL IFD entry pointer");

  return(p_entry->count);

} /* Function ifd_entry_count */


/** ifd_entry_offset() returns the file offset to the IFD entry data.
 */
uint32 ifd_entry_offset(
  ifd_ifdentry_t*  p_entry)        /* I */
{
  HQASSERT((p_entry != NULL),
           "ifd_entry_offset: NULL IFD entry pointer");
  HQASSERT((((p_entry->flags)&ENTRY_INDIRECT) != 0),
           "ifd_entry_offset: getting offset of IFD entry that is not offset");

  /* Entry offset is held in as a LONG */
  return(p_entry->u.tiff_long);

} /* Function ifd_entry_offset */



/* Log stripped */
