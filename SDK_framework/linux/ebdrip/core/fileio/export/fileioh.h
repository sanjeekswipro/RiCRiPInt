/** \file
 * \ingroup fileio
 *
 * $HopeName: COREfileio!export:fileioh.h(EBDSDK_P.1) $
 *
 * Copyright (C) 2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * Combined handle header file for COREfileio compound. Provides typedefs and
 * function definitions for files & filters compound. Include fileio.h instead
 * if you need access to the structure details.
 */

#ifndef __FILEIOH_H__
#define __FILEIOH_H__

#include "swdevice.h" /* Needs DEVICELIST typedef */

struct OBJECT ; /* from COREobjects */
struct STACK ;  /* from COREobjects */
struct mm_pool_t ; /* from SWmm_common */
struct core_init_fns ; /* from SWcore */

/** \{ */

typedef struct FILELIST FILELIST ;
typedef struct SLIST SLIST ;

/** This macro can be used with *constant strings only* to supply
    the name and length parameters to init_filelist_struct or any other
    function that takes a uint8 and a length. The strings surrounding the
    argument are to provoke an error if a non-constant string is passed to
    the macro. */
#define NAME_AND_LENGTH(_str) (uint8 *)("" _str ""), sizeof("" _str "") - 1

/*---------------------------- INITIALISATION -------------------------------*/

void fileio_C_globals(struct core_init_fns *fns) ;

void filter_C_globals(struct core_init_fns *fns) ;

/** \brief Call to purge a filelist to a savelevel. */
void fileio_restore(int32 slevel) ;

/** \brief Report if all files are closed on device. */
Bool fileio_check_device(DEVICELIST *dev) ;

/** \brief Create an object reflecting a file's savelevel and type. */
void file_store_object(struct OBJECT *fileo, FILELIST *file, uint8 litexec) ;

/** \brief Ensure no references to FILELISTs from the specified PDF execution
 * context persist in the local and global lists. */
void fileio_close_pdf_filters(int32 pdfContextId, FILELIST *purged);


/*------------------------------- FILES -------------------------------------*/

/** \brief Return values for filename parsing. */
enum file_parse_result {
  PARSEERROR =       -1,  /**< Filename parse error. */
  NODEVICEORFILE =    0,  /**< Filename is empty. */
  DEVICEANDFILE =     1,  /**< Filename has device and file parts. */
  JUSTFILE =          2,  /**< Filename has file part but no device part. */
  JUSTDEVICE =        3   /**< Filename has device part but no file part. */
} ;

/** The parse_filename function splits a name and length into its device part
    and filename part, and returns pointers to NULL-terminated strings
    containing these parts. The return value indicates which parts were
    present. The null-terminated strings are statically allocated, and should
    be copied or used immediately.

    \param[in] name The filing system name to be parsed.
    \param len The length of the name to be parsed.
    \param[out] pbuff_device A pointer to the device part of the name, if
    present.
    \param[out] pbuff_file A pointer to the file part of the name, if present.

    \result One of the \c file_parse_result values, indicating how the name
    was decomposed.
*/
int32 parse_filename(/*@notnull@*/ /*@in@*/ uint8 *name, int32 len,
                     /*@notnull@*/ /*@out@*/ uint8 **pbuff_device,
                     /*@notnull@*/ /*@out@*/ uint8 **pbuff_file) ;

/** \brief Iterators for file lists.

    The fileio compound maintains lists of files in global and local VM. The
    routines iterate over these lists, returning NULL when there are no more
    files that match the search criteria. If any flags are set, then only
    devices with those flags set will be returned. File lists may *not* be
    inserted or deleted while an iterator is active. The contents of the
    iterator structure are private and should not be inspected or modified by
    clients. */
typedef struct filelist_iterator_t {
  FILELIST *current ;
  Bool flags ;
} filelist_iterator_t, *filelist_iterator_h ;

/** \brief Start an iteration over filelist the structures.

    \param iter A pointer to a filelist iterator, which will be initialised
    to prepare for an iteration over local and/or global filelists.
    \param local TRUE if local filelists are to be traversed, FALSE if they
    are to be ignored.
    \param global TRUE if global filelists are to be traversed, FALSE if they
    are to be ignored.

    \return The first filelist in the appropriate lists, or NULL if there is
    no appropriate filelist. Return order is not guaranteed to be complete or
    correct if filelists are added or removed during iteration.
*/
FILELIST *filelist_first(filelist_iterator_h iter, Bool local, Bool global) ;

/** \brief Get the next file from a filelist iteration.

    \param iter A pointer to a filelist iterator, which should have previously
    been initialised by a call to \c filelist_first.
    \return The next filelist in the appropriate lists, or NULL if there is
    no appropriate filelist. Return order is not guaranteed to be complete or
    correct if filelists are added or removed during iteration.
*/
FILELIST *filelist_next(filelist_iterator_h iter) ;

/** \brief Open a file, filling in a file object describing the new file, and
    adding it to the global/local VM lists if appropriate.

    \param fnameobject A string object naming the device and/or file name to
    open. The name may be an absolute device, a device and a file in
    %device%file syntax, or just a file name (which will be searched for on
    all searchable devices).
    \param openflags The combination of device flags to use to open the file
    on its device. The device flags are SW_RDONLY, SW_WRONLY, SW_RDWR (note
    this is *not* the exclusion of the previous two flags), SW_APPEND,
    SW_CREAT, SW_TRUNC, SW_EXCL, SW_FROMPS, and SW_FONT. The device flags are
    defined in the Core RIP interface file swdevice.h. The device flags
    provided should be consistent with the \a psflags parameter.
    \param psflags The read/write flags to apply to the \c FILELIST object
    created to represent the file. The flags should be one or more of READ_FLAG
    and WRITE_FLAG. The filelist flags should be consistent with the
    \a openflags parameter.
    \param appendmode A flag indicating if the file should be opened for
    appending. If true, a seek will be performed to the end of the file after
    opening it.
    \param baseflag This should be normally be 0. The PostScript file operator
    passes BASE_FILE_FLAG when the file modifier '&' is used, to indicate that
    we should re-used a standard file's \c FILELIST structure. This flag may
    be merged with psflags in a future release.
    \param result A pointer in which a new object describing the file will be
    stored.
    \retval TRUE returned if the file was open successfully.
    \retval FALSE returned if the file could not be opened.
*/
Bool file_open(/*@in@*/ /*@notnull@*/ struct OBJECT * fnameobject,
               int32 openflags, int32 psflags,
               Bool appendmode, int32 baseflag,
               /*@out@*/ /*@notnull@*/ struct OBJECT * result);

/** \brief Close a file. This leaves the filelist structure linked in the
    global/local list, where it will be picked up and re-used if possible.

    \param theofile A pointer to the file object to close.
    \retval TRUE if the file was closed successfully.
    \retval FALSE if the file could no be closed successfully.
*/
Bool file_close(/*@notnull@*/ /*@in@*/ register struct OBJECT *theofile);

/** \brief Test if a file is seekable.

    \param flptr The file to test for seekability.
    \retval TRUE if the file is seekable.
    \retval FALSE if the file is not seekable.
 */
Bool file_seekable(/*@notnull@*/ /*@in@*/ FILELIST *flptr) ;

/** \brief Return stat information for file if it exists. */
Bool file_stat(
/*@in@*/ /*@notnull@*/
  struct OBJECT*  fobject,
/*@out@*/ /*@notnull@*/
  Bool*           exists,
/*@out@*/ /*@notnull@*/
  STAT*           stat);

/** \brief Return the device from which data for the passed file/filter is
    ultimately read. */
DEVICELIST* get_device(FILELIST* flptr);

/** \brief Implements the common operation of reading bytes from a file or
    filter into a buffer.

    \param[in] flptr The file or filter to read from.
    \param[out] buffer The buffer to read bytes into.
    \param[in] bytes_wanted The number of bytes to read.
    \param[out] bytes_read An optional pointer in which the number of bytes
    actually read is stored. If this parameter is \c NULL, then the return
    value must be used to distinguish if all of the bytes wanted were read.
    \retval 1 The entire \a bytes_wanted request was returned in \a buffer.
    \retval -1 A lesser number of bytes was read from the source before an
    EOF indication was returned.
    \retval 0 An error occurred. It is the caller's responsibility to
    call theFileLastError() or signal the error in some other way.
 */
int32 file_read(/*@in@*/ /*@notnull@*/ FILELIST *flptr,
                /*@out@*/ /*@notnull@*/ uint8 *buffer,
                int32 bytes_wanted,
                /*@out@*/ /*@null@*/ int32 *bytes_read) ;

/** \brief Implements the common operation of skipping bytes from a file or
    filter.
    \param[in] flptr The file or filter to skip on.
    \param[in] bytes_to_skip The number of bytes to skip over.
    \param[out] bytes_skipped An optional pointer in which the number of bytes
    skipped is stored. If this parameter is \c NULL, then the return
    value must be used to distinguish if all of the bytes wanted were skipped.
    \retval 1 The entire \a bytes_to_skip request were skipped.
    \retval -1 A lesser number of bytes were skipped from the source before an
    EOF indication was returned.
    \retval 0 An error occurred. It is the caller's responsibility to
    call theFileLastError() or signal the error in some other way.
 */
int32 file_skip(/*@in@*/ /*@notnull@*/ FILELIST *flptr,
                int32 bytes_to_skip,
                /*@out@*/ /*@null@*/ int32 *bytes_skipped) ;

/** \brief Implements the common operation of writing bytes to a file or
    filter from a buffer.

    \param[in] flptr The file or filter to write to.
    \param[out] buffer The buffer to write bytes from.
    \param[in] bytes_ready The number of bytes ready to write.
    \retval TRUE The entire \a bytes_ready request was written to \a flptr.
    \retval FALSE An error occurred. It is the caller's responsibility to
    call theFileLastError() or signal the error in some other way.
 */
Bool file_write(/*@in@*/ /*@notnull@*/ FILELIST *flptr,
                /*@in@*/ /*@notnull@*/ const uint8 *buffer,
                int32 bytes_ready) ;

/** \brief Allow direct read access to a file's buffer, for efficiency.
    Returns FALSE if no bytes were available (i.e. EOF was found, or there
    was an error); TRUE if something valid is in the buffer. bytes_returned
    is the number of bytes available. Do not use for filters; the last char
    state machine may mess up the results. */
Bool GetFileBuff(/*@notnull@*/ /*@in@*/ register FILELIST *flptr,
                 int32 max_bytes_wanted ,
                 /*@notnull@*/ /*@in@*/ uint8 **return_ptr,
                 /*@notnull@*/ /*@out@*/ int32 *bytes_returned) ;

/** \brief If buffer is empty, attempt to fill it. Does not change the
    file-position. Returns TRUE iff buffer is then not empty. */
Bool EnsureNotEmptyFileBuff(/*@notnull@*/ /*@in@*/ FILELIST *flptr) ;

/** \brief Allow direct write access to a file's buffer, for efficiency.
    Returns FALSE if no space could be made (i.e. not enough room was
    available and flushing the buffer raised an error); TRUE if space was
    made. bytes_returned is the amount of space available. */
Bool PutFileBuff(/*@notnull@*/ /*@in@*/ register FILELIST *flptr,
                 int32 max_bytes_wanted ,
                 /*@notnull@*/ /*@in@*/ uint8 **return_ptr,
                 /*@notnull@*/ /*@out@*/ int32 *bytes_returned) ;

/* Functions implementing default file behaviour */
int32 FileError(register FILELIST *flptr) ;
Bool FileEncodeError(FILELIST *filter);
Bool FileDecodeError(FILELIST *filter, int32 *pb);
int32 FileFlushBufError(int32 c, FILELIST *flptr) ;
Bool FileInitError(FILELIST* flptr, struct OBJECT* args, struct STACK* stack);
int32 FileError2(register FILELIST *flptr, Hq32x2 *n) ;
int32 FileError2Const(register FILELIST *flptr, const Hq32x2 *n) ;
int32 FileCloseError(register FILELIST *flptr, int32 flag) ;
int32 FileFillBuff(register FILELIST *flptr) ;
int32 FileFlushBuff(int32 c, register FILELIST *flptr) ;
Bool FileInit(FILELIST *flptr, struct OBJECT *args, struct STACK *stack) ;
int32 FileClose(register FILELIST *flptr, int32 flag) ;
void FileDispose(register FILELIST *flptr) ;
Bool FileLastError(register FILELIST *flptr) ;
int32 FileBytes(register FILELIST *flptr, Hq32x2 *pos) ;
int32 FileReset(register FILELIST *flptr) ;
int32 FilePos(register FILELIST *flptr, Hq32x2* pos) ;
int32 FileSetPos(register FILELIST *flptr, const Hq32x2* offset) ;
int32 FileFlushFile(register FILELIST *flptr) ;


/*------------------------------ FILTERS ------------------------------------*/

/** \} */
/** \addtogroup filters */
/** \{ */

/** \brief Find standard filter by name. */
FILELIST *filter_standard_find(/*@notnull@*/ /*@in@*/ uint8 *name, int32 len);

/** \brief Standard filter addition; use for statically allocated filters at
    startup time only. */
void filter_standard_add(/*@notnull@*/ /*@in@*/ FILELIST *filter) ;

/** \brief Return corresponding decode/encode filter for encode/decode
    filter, specified by system name. */
Bool filter_standard_inverse(int32 filter_name_num,
                             /*@notnull@*/ /*@out@*/ int32 *result) ;

/** \brief Return system name corresponding to filter. Returns -1 if filter
    is not a standard filter. */
int32 filter_standard_name(/*@notnull@*/ /*@in@*/ FILELIST *flptr);

/** \brief External filter management. These routines register and deregister
    filters introduced via ScriptWorks devices and the Filter resource
    category. */
FILELIST *filter_external_find(/*@notnull@*/ /*@in@*/ uint8 *name,
                               int32 len,
                               /*@notnull@*/ /*@out@*/ int32 *find_error,
                               Bool fromPS ) ;

Bool filter_external_define(struct OBJECT *filter, struct OBJECT *dict) ;
Bool filter_external_forall(struct STACK *restack) ;
Bool filter_external_exists(uint8 *name, int32 name_length) ;
Bool filter_external_undefine(uint8 *name, int32 name_length) ;

/** \brief Set the file's IO error flag and error flag (if a filter) and
    return EOF. */
int32 ioerror_handler(/*@notnull@*/ /*@in@*/ FILELIST *flptr) ;

/** \brief Get the target or source for a filter from object, and insert it
    as underlying file of the filter. */
Bool filter_target_or_source(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                             /*@notnull@*/ /*@in@*/ struct OBJECT *theo) ;

/** \brief Create a copy of a filter template and initialise it. Add the
    filter to the head of the file list specified. If no reusable filter can
    be found, call allocation function to get a new one. */
Bool filter_create_with_alloc(/*@notnull@*/ /*@in@*/ FILELIST *nflptr,
                              /*@notnull@*/ /*@in@*/ FILELIST **flptr,
                              /*@null@*/ /*@in@*/ struct OBJECT *args,
                              /*@null@*/ /*@in@*/ struct STACK *stack,
                              int32 pdf_context_id,
                              /*@notnull@*/ /*@in@*/
                              FILELIST *(*alloc)(/*@notnull@*/ /*@in@*/ struct mm_pool_t *),
                              /*@notnull@*/ /*@in@*/ struct mm_pool_t *) ;

/** \brief Create a copy of a filter template and initialise it. Add the
    filter to the appropriate file/filter lists for the current VM mode. */
Bool filter_create(/*@notnull@*/ /*@in@*/ FILELIST *nflptr,
                   /*@notnull@*/ /*@out@*/ FILELIST **flptr,
                   /*@null@*/ /*@in@*/ struct OBJECT *args,
                   /*@null@*/ /*@in@*/ struct STACK *stack) ;

/** \brief Similar to filter_create, but fills in a file object. */
Bool filter_create_object(/*@notnull@*/ /*@in@*/ FILELIST *nflptr,
                          /*@notnull@*/ /*@in@*/ struct OBJECT *filter,
                          /*@null@*/ /*@in@*/ struct OBJECT *args,
                          /*@null@*/ /*@in@*/ struct STACK *stack) ;

/** \brief Layer a filter on an existing file/filter, returning a new
    file with the same global state. */
Bool filter_layer(/*@notnull@*/ /*@in@*/ FILELIST *infile,
                  /*@notnull@*/ /*@in@*/ uint8 *name,
                  uint32 namelen,
                  /*@null@*/ /*@in@*/ struct OBJECT *args,
                  /*@notnull@*/ /*@out@*/ FILELIST **layered) ;

/** \brief Layer a filter on an existing file/filter object, returning a new
    object with the same global state. */
Bool filter_layer_object(/*@notnull@*/ /*@in@*/ struct OBJECT *infile,
                         /*@notnull@*/ /*@in@*/ uint8 *name,
                         uint32 namelen,
                         /*@null@*/ /*@in@*/ struct OBJECT *args,
                         /*@notnull@*/ /*@out@*/ struct OBJECT *filtered) ;

/** \brief Function to return a character to the filter's input. */
uint8* FilterUnGetc(/*@notnull@*/ /*@in@*/ FILELIST *f);

/** \brief Forward definitions for image filter tag decoding. */
typedef struct imagefilter_match_t imagefilter_match_t ;

/* Functions implementing default filter behaviour */
int32 FilterReset(/*@notnull@*/ /*@in@*/ FILELIST *filter ) ;
int32 FilterSetPos(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                   /*@notnull@*/ /*@in@*/ const Hq32x2 *position ) ;
int32 FilterPos(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                /*@notnull@*/ /*@in@*/ Hq32x2* p ) ;
int32 FilterBytes(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                  /*@notnull@*/ /*@in@*/ Hq32x2* n ) ;
int32 FilterError(/*@notnull@*/ /*@in@*/ FILELIST *filter) ;
int32 FilterFlushBufError(int32 c, /*@notnull@*/ /*@in@*/ FILELIST *filter) ;
Bool FilterEncodeError(/*@notnull@*/ /*@in@*/ FILELIST *filter) ;
Bool FilterDecodeError(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                       /*@notnull@*/ /*@in@*/ int32* pb) ;
Bool  FilterDecodeInfoError(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                            /*@null@*/ /*@in@*/ imagefilter_match_t *match) ;
int32 FilterFillBuff(/*@notnull@*/ /*@in@*/ register FILELIST *filter) ;
void FilterDispose(/*@notnull@*/ /*@in@*/ FILELIST *flptr) ;
int32 FilterNoOp(/*@notnull@*/ /*@in@*/ FILELIST *flptr) ;
int32 FilterCloseNoOp(/*@notnull@*/ /*@in@*/ FILELIST *flptr, int32 flag ) ;
int32 FilterBytesAvailNoOp(/*@notnull@*/ /*@in@*/ FILELIST *flptr,
                           /*@notnull@*/ /*@in@*/ Hq32x2* n) ;
int32 FilterFilePosNoOp(/*@notnull@*/ /*@in@*/ FILELIST *flptr,
                        /*@notnull@*/ /*@in@*/ Hq32x2* n) ;
int32 FilterSetFilePosNoOp(/*@notnull@*/ /*@in@*/ FILELIST *flptr,
                           /*@notnull@*/ /*@in@*/ const Hq32x2 *n) ;
int32 FilterCheckArgs(/*@notnull@*/ /*@in@*/ FILELIST *filter,
                      /*@notnull@*/ /*@in@*/ struct OBJECT *args ) ;
int32 FilterFlushFile(/*@notnull@*/ /*@in@*/ FILELIST *filter) ;
int32 FilterFlushBuff(int32 c, /*@notnull@*/ /*@in@*/ FILELIST *filter) ;
int32 FilterCloseFile(/*@notnull@*/ /*@in@*/ FILELIST *filter, int32 flag) ;
Bool FilterLastError(/*@notnull@*/ /*@in@*/ FILELIST *filter) ;

#if defined( ASSERT_BUILD )
/** \brief Trace variable. */
extern int32 debug_filters ;
#endif

/** \} */

/*-------------------------- IMPORTED FUNCTIONS -----------------------------*/
/** \ingroup filters
    This function is called whenever a new filter is created, either by
    internal or external calls. It is called immediately before the filter's
    initialisation. Arguments to this function are the filter's FILELIST and
    the arguments which will be passed to its init routine. The hook
    procedure should only modify these if it *really* knows what it is doing.
    It is primarily provided so that filter types can be checked for
    preflight (such as DCTDecode). If the hook returns FALSE, then the filter
    will not be initialised. */
Bool filter_create_hook(FILELIST *filter, struct OBJECT *args, struct STACK *stack) ;

/** \ingroup fileio
    Hook function called when trying to open an unknown device-only file.
    This hook is used to connect standard files (%stdin, %statementedit,
    etc.) to file objects. It should fill in the file details in the object
    supplied and return TRUE, or return FALSE. The flags passed through are
    some combination of the device open file flags in swdevice.h. */
Bool file_standard_open(uint8 *name, int32 len, int32 flags, struct OBJECT *file) ;

/*
Log stripped */
#endif /* Protection from multiple inclusion */
