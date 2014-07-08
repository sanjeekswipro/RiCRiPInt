/** \file
 * \ingroup interface
 *
 * $HopeName: COREinterface_control!swdevice.h(EBDSDK_P.1) $
 *
 * Copyright (C) 1992-2013 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * This file provides the information required to set up
 * devices outside the rip.
 *
 * The typical method will be to:
 *
 * \li define a series of routines to manage devices of each required type;
 *
 * \li setup a static structure forming the device type. For example:
 *
 * \verbatim
 *    static DEVICETYPE my_device_type = {
 *       my_device_type_number,
 *       ...
 *       & my_device_read_file,
 *        ...
 *     };
 * \endverbatim
 *
 * \li add the address of the structure to a static, null terminated array of
 * such structures. Remember that an os device type of 0 must always be
 * provided.
 * e.g.:
 *
 * \verbatim
 *    static DEVICETYPE * all_my_devices [] = {
 *      & my_os_device_type,
 *      ...
 *      & my_device_type,
 *      ...
 *      NULL
 *    };
 * \endverbatim
 *
 * \li put the address of this array into the SWSTART array with the
 * correct tag (see swstart.h) and pass the array to the rip via
 * SwStart()
 *
 * OEM's please note: except for OS_DEVICE_TYPE and any numbers specifically
 * mentioned in product documentation (which will normally be listed above),
 * you must ONLY USE DEVICE TYPE NUMBERS IN THE RANGE ALLOCATED TO YOU BY
 * HARLEQUIN. Current or future releases of the Harlequin RIP may fail if
 * you do not.
 *
 * <strong>Core RIP Device Interface Version information</strong>
 *
 * This file is exported to PS DeviceType plugins, and they expect
 * binary compatibility with this interface as far as possible.
 *
 * If this interface changes in ANY non-backwards compatible way, the
 * Major version number will be incremented.
 *
 * For any other changes, which can still support plugins with the
 * same major version number, only the minor version number will be
 * incremented.
 */

#ifndef __SWDEVICE_H__
#define __SWDEVICE_H__

/**
 * \defgroup PLUGIN_swdevice DEVICE interface
 * \ingroup interface
 * \{
 */

#include "ripcall.h" /* RIPCALL default definition. */

#ifdef __cplusplus
extern "C" {
#endif

#define PS_DEVICETYPE_INTF_VERSION_MAJOR   1
#define PS_DEVICETYPE_INTF_VERSION_MINOR   3

/* ------------------------------------------- */


#define OS_DEVICE_TYPE 0         /**< \brief a device type with this
                                     number is always required */
#define NULL_DEVICE_TYPE 1       /**< \brief this is a built in device type
                                     like the UNIX device /dev/null */
#define ABS_DEVICE_TYPE 10       /**< \brief this is a device type provided
                                     for device absolute devices which map to
                                     a particular file on the os device: for
                                     example %state% => %os%state */
#define SEMAPHORE_DEVICE_TYPE 12 /**< \brief this is a device type which must
                                     be provided for any parallel core RIP
                                     implementation */
#define RELATIVE_DEVICE_TYPE 17  /**< \brief This makes multiple absolute
                                    devices look like one relative device */
#define COMPRESS_DEVICE_TYPE 23
#define TRAP_DEVICE_TYPE     25  /**< \brief used by the core for sending
                                    trapping info */
#define FONT_ND_CRYPT_DEVICE_TYPE 128 /**< \brief used by the core to encrypt
                                    or decrypt OEM encrypted fonts */

#define LONGESTDEVICENAME 50

#define MINPOSSIBLESTRATEGY   1
#define MAXPOSSIBLESTRATEGY 128


#define DeviceNoError             0 /**< \brief Returned by DEVICELIST_LAST_ERROR() */
#define DeviceInvalidAccess       1 /**< \brief Returned by DEVICELIST_LAST_ERROR() */
#define DeviceIOError             2 /**< \brief Returned by DEVICELIST_LAST_ERROR() */
#define DeviceLimitCheck          3 /**< \brief Returned by DEVICELIST_LAST_ERROR() */
#define DeviceUndefined           4 /**< \brief Returned by DEVICELIST_LAST_ERROR() */
#define DeviceUnregistered        5 /**< \brief Returned by DEVICELIST_LAST_ERROR() */
#define DeviceInterrupted         6 /**< \brief Returned by DEVICELIST_LAST_ERROR() */
#define DeviceVMError             7 /**< \brief Returned by DEVICELIST_LAST_ERROR() */
#define DeviceReOutput            8 /**< \brief Returned by DEVICELIST_LAST_ERROR() */
#define DeviceNotReady            9 /**< \brief Returned by DEVICELIST_LAST_ERROR() */
#define DeviceCancelPage         10 /**< \brief Returned by DEVICELIST_LAST_ERROR() */
#define DeviceReOutputPageBuffer 11 /**< \brief Returned by DEVICELIST_LAST_ERROR() */
#define DeviceTimeout            12 /**< \brief Returned by DEVICELIST_LAST_ERROR() */

#define DeviceIOCtl_ShortRead       0   /**< \brief Opcode for
                                            DEVICELIST_IOCTL(). Arg is
                                            number of bytes to read */
#define DeviceIOCtl_PDFFilenameToPS 1   /**< \brief Opcode for
                                            DEVICELIST_IOCTL(). Arg is
                                            a pointer to a PDF_FILESPEC */
#define DeviceIOCtl_OSDeviceName    2   /**< \brief Opcode for
                                            DEVICELIST_IOCTL(). Arg is
                                            a pointer to an OS_FILESPEC */
/* Opcodes private to each device start at 100. */


/* PDF_FILESPEC flags */
#define PDF_FILESPEC_None     0x00       /**< \brief unspecified */
#define PDF_FILESPEC_PDF      0x01       /**< \brief PDF filename */
#define PDF_FILESPEC_DOS      0x02       /**< \brief Dos based filename */
#define PDF_FILESPEC_Mac      0x03       /**< \brief Mac based filename */
#define PDF_FILESPEC_Unix     0x04       /**< \brief Unix based filename */
#define PDF_FILESPEC_FS       0x08       /**< \brief filesystem slot is defined */


typedef HqnFileDescriptor DEVICE_FILEDESCRIPTOR ;
#ifdef PLATFORM_IS_64BIT
#define DEVICE_FILEDESCRIPTOR_TO_INT32(x) CAST_INTPTRT_TO_INT32(x)

#else /* Assume 32 bit platform. */
#define DEVICE_FILEDESCRIPTOR_TO_INT32(x) (x)
#endif

#define INT32_TO_DEVICE_FILEDESCRIPTOR(x) (x)

/** Convert a (void*) to a device file descriptor. Allocated data
   structures are assumed to be 4 byte aligned. Right shift is to
   ensure that the device file descriptor is not a negative value
   which is an invalid device file descriptor. */
#define VOIDPTR_TO_DEVICE_FILEDESCRIPTOR(d) ((DEVICE_FILEDESCRIPTOR)((uintptr_t)(d) >> 1))
/** Convert a device file descriptor which was converted from a (void*)
   via the macro VOIDPTR_TO_DEVICE_FILEDESCRIPTOR back to its original
   value. */
#define DEVICE_FILEDESCRIPTOR_TO_VOIDPTR(d) ((void *)((uintptr_t)(d) << 1))

typedef struct _filespec_name {
  uint8 * clist;
  int32 len;
} PDF_FILESPEC_NAME;

/**
 * \brief PDF_FILESPEC - a pointer to PDF_FILESPEC will be passed as
 * the arg to DEVICELIST_IOCTL() when the opcode is
 * \c DeviceIOCtl_PDFFilenameToPS.
 *
 * The constants PDF_FILESPEC_XXX specify information about the arguments
 * passed through in the members filename and filesytem. All the other
 * members must be specified.
 *
 * On entry to DEVICELIST_IOCTL() the buffer member must contain a
 * pointer to and the length of the buffer to be written to. If the
 * call is successful buffer.len will contain the byte count of the
 * contents written into the buffer.
 */
typedef struct _pdf_filespec {
  uint32 flags;
  PDF_FILESPEC_NAME filename;          /**< \brief "string" filespec
                                           filename */
  uint8 * filesystem;                  /**< \brief "null terminated
                                           string" filesystem name or
                                           NULL */
  uint8 * current_device;              /**< \brief "null terminated
                                           string" cwdevice for the
                                           existing pdf file */
  PDF_FILESPEC_NAME current_filename;  /**< \brief "string"
                                           cwdirectory for the
                                           existing pdf file */
  PDF_FILESPEC_NAME buffer;            /**< \brief "string" filespec
                                           filename */
} PDF_FILESPEC;

typedef struct _os_filespec_name {
  uint8 * clist;
  int32 len;
} OS_FILESPEC_NAME;

/**
 * \brief A pointer to OS_FILESPEC will be passed as the arg to
 * DEVICELIST_IOCTL() routine when opcode is DeviceIOCtl_OSDeviceName.
 *
 * The filename specifies the name of a file for which an %os%
 * device filename is required.
 *
 * On entry to DEVICELIST_IOCTL() the buffer member must contain a
 * pointer to and the length of the buffer to be written to. If the
 * call is successful buffer.len will contain the byte count of the
 * contents written into the buffer.
 */
typedef struct _os_filespec {
  uint8 * current_device;             /**< \brief "null terminated
                                          string" cwdevice for the
                                          existing file */
  OS_FILESPEC_NAME current_filename;  /**< \brief "string" filespec
                                          filename for the existing
                                          file */
  OS_FILESPEC_NAME buffer;            /**< \brief "string" filespec
                                          filename */
} OS_FILESPEC;


/* ------------------------------------------------------------------------ */

#define ParamBoolean 1 /**< \brief Type of parameter passed to the device */
#define ParamInteger 2 /**< \brief Type of parameter passed to the device */
#define ParamString  3 /**< \brief Type of parameter passed to the device */
#define ParamFloat   4 /**< \brief Type of parameter passed to the device */
#define ParamArray   5 /**< \brief Type of parameter passed to the device */
#define ParamDict    6 /**< \brief Type of parameter passed to the device */
#define ParamNull    7 /**< \brief Type of parameter passed to the device */

#define ParamAccepted    1 /**< \brief return code from
                               DEVICELIST_SET_PARAM() */
#define ParamTypeCheck   2 /**< \brief return code from
                               DEVICELIST_SET_PARAM() */
#define ParamRangeCheck  3 /**< \brief return code from
                               DEVICELIST_SET_PARAM() */
#define ParamConfigError 4 /**< \brief return code from
                               DEVICELIST_SET_PARAM() */
#define ParamIgnored     5 /**< \brief return code from
                               DEVICELIST_SET_PARAM() */
#define ParamError     (-1) /**< \brief return code from
                                DEVICELIST_SET_PARAM(). Set if
                                last_error should be checked */

/**
 * \brief A structure describing the parameter to set and its value.
 */
typedef struct DEVICEPARAM {
/*@owned@*/ /*@notnull@*/
  uint8 *paramname ;    /**< \brief string is in caller's memory  */
  int32 paramnamelen ;  /**< \brief length of the name of the
                            parameter */
  int32 type ;          /**< \brief one of the types defined above */
  union {
    void * deviceparam_paramval_initialiser;
    /* NB: deviceparam_paramval_initialiser MUST BE FIRST; ANSI permits static init
     * of unions via the first element.  Some compilers insist that this be addressy
     * flavoured if it is to be initialised to something relocatable, so having a
     * special field, of void *, just for this purpose is cleanest.  See task 8014.
     * Don't ever use this arm of the union, never let its name be said.
     */
    int32 intval ;
    int32 boolval ;
/*@owned@*/ /*@notnull@*/
    uint8 *strval ;
    float floatval ;
    struct DEVICEPARAM *compobval;
  } paramval ;          /**< \brief value of the parameter */
  int32 strvallen ;     /**< \brief now used for length of compound
                            objects also... */
} DEVICEPARAM ;

#define theDevParamName( _dp )       ((_dp).paramname)
#define theIDevParamName( _dp )      ((_dp)->paramname)
#define theDevParamNameLen( _dp )    ((_dp).paramnamelen)
#define theIDevParamNameLen( _dp )   ((_dp)->paramnamelen)
#define theDevParamType( _dp )       ((_dp).type)
#define theIDevParamType( _dp )      ((_dp)->type)
#define theDevParamInteger( _dp )    ((_dp).paramval.intval)
#define theIDevParamInteger( _dp )   ((_dp)->paramval.intval)
#define theDevParamBoolean( _dp )    ((_dp).paramval.boolval)
#define theIDevParamBoolean( _dp )   ((_dp)->paramval.boolval)
#define theDevParamString( _dp )     ((_dp).paramval.strval)
#define theIDevParamString( _dp )    ((_dp)->paramval.strval)
#define theDevParamFloat( _dp )      ((_dp).paramval.floatval)
#define theIDevParamFloat( _dp )     ((_dp)->paramval.floatval)
#define theDevParamStringLen( _dp )  ((_dp).strvallen)
#define theIDevParamStringLen( _dp ) ((_dp)->strvallen)

#define setDevParamTypeAndName( _dp, _type, _name, _nameLength) \
  theDevParamType(_dp) = (_type); \
  theDevParamName(_dp) = (_name); \
  theDevParamNameLen(_dp) = (_nameLength);

/* Access macros for compound objects */
#define theDevParamArray( _dp )      ((_dp).paramval.compobval)
#define theIDevParamArray( _dp )     ((_dp)->paramval.compobval)
#define theDevParamDict( _dp )       ((_dp).paramval.compobval)
#define theIDevParamDict( _dp )      ((_dp)->paramval.compobval)
#define theDevParamDictLen( _dp )    ((_dp).strvallen)
#define theIDevParamDictLen( _dp )   ((_dp)->strvallen)
#define theDevParamArrayLen( _dp )   ((_dp).strvallen)
#define theIDevParamArrayLen( _dp )  ((_dp)->strvallen)


#define SW_RDONLY    0x001   /**< \brief DEVICELIST_OPEN() flag. Open
                                 for reading only */
#define SW_WRONLY    0x002   /**< \brief DEVICELIST_OPEN() flag. Open
                                 for writing only */
#define SW_RDWR      0x004   /**< \brief DEVICELIST_OPEN() flag. Open
                                 for reading or writing */
#define SW_APPEND    0x008   /**< \brief DEVICELIST_OPEN() flag. Open
                                 (write guaranteed at end) */
#define SW_CREAT     0x010   /**< \brief DEVICELIST_OPEN() flag. Open
                                 with file create */
#define SW_TRUNC     0x020   /**< \brief DEVICELIST_OPEN() flag. Open
                                 with truncation */
#define SW_EXCL      0x040   /**< \brief DEVICELIST_OPEN() flag. Open
                                 with exclusive access */
#define SW_FROMPS    0x080   /**< \brief DEVICELIST_OPEN() flag. Open
                                 by the PostScript "file" operator */
#define SW_FONT      0x100   /**< \brief DEVICELIST_OPEN()
                                 flag. Possibly try to open font
                                 resource if any */

#define SW_SET       0         /**< \brief DEVICELIST_SEEK()
                                   flag. Absolute offset. */
#define SW_INCR      1         /**< \brief DEVICELIST_SEEK()
                                   flag. Relative to current offset. */
#define SW_XTND      2         /**< \brief DEVICELIST_SEEK()
                                   flag. Relative to end of file. */

#define SW_BYTES_AVAIL_REL 0    /**< \brief DEVICELIST_BYTES() reason
                                    code. Immediately available after
                                    current pos */
#define SW_BYTES_TOTAL_ABS 1    /**< \brief DEVICELIST_BYTES() reason
                                    code. Total extent of file in
                                    bytes */

/**
 * \brief Calls to DEVICELIST_NEXT() must pass a FILEENTRY structure
 * which the routine will fill in.
 */
typedef struct FILEENTRY {
  int32 namelength ;
/*@dependent@*/ /*@notnull@*/
  uint8 *name ;
} FILEENTRY ;

#define theIDFileName( _fe )     ((_fe)->name)
#define theDFileName( _fe )      ((_fe).name)
#define theIDFileNameLen( _fe )  ((_fe)->namelength)
#define theDFileNameLen( _fe )   ((_fe).namelength)

#define FileNameNoMatch     0 /**< \brief DEVICELIST_NEXT() return code */
#define FileNameMatch       1 /**< \brief DEVICELIST_NEXT() return code */
#define FileNameError       (-1) /**< \brief DEVICELIST_NEXT() return code */
#define FileNameIOError     (-1) /**< \brief DEVICELIST_NEXT() return
                                     code. Synonymous with \c
                                     FileNameError, for upward
                                     compatibility */
#define FileNameRangeCheck  (-2) /**< \brief DEVICELIST_NEXT() return code */

#ifdef RS6000
#undef STAT   /* RS/6000, unfortunately, has its own STAT declaration */
#endif

/**
 * \brief Calls to DEVICELIST_STATUS_FILE() must pass a \c STAT
 * structure to the routine which it will fill in
 */
typedef struct STAT {
  HqU32x2  bytes ;      /**< \brief size in bytes */
  uint32   referenced ; /**< \brief time in unspecified units */
  uint32   modified ;   /**< \brief time in unspecified units */
  uint32   created ;    /**< \brief time in unspecified units */
} STAT ;

#define theStatPages( _s )       ((_s).pages)
#define theIStatPages( _s )      ((_s)->pages)
#define theStatBytes( _s )       ((_s).bytes)
#define theIStatBytes( _s )      ((_s)->bytes)
#define theStatReferenced( _s )  ((_s).referenced)
#define theIStatReferenced( _s ) ((_s)->referenced)
#define theStatModified( _s )    ((_s).modified)
#define theIStatModified( _s )   ((_s)->modified)
#define theStatCreated( _s )     ((_s).created)
#define theIStatCreated( _s )    ((_s)->created)


/*
 * \brief Calls to DEVICELIST_STATUS_DEVICE() must pass a \c DEVSTAT
 * structure to the routine which it will fill in
 */
typedef struct DEVSTAT {
  uint32  block_size ; /**< \brief block size */
  HqU32x2 size ;       /**< \brief size of device, in bytes */
  HqU32x2 free ;       /**< \brief free space on device, in bytes */
/*@dependent@*/ /*@null@*/
  uint8 * start ;      /**< \brief %pagebuffer% device only */
} DEVSTAT ;

#define theDevStatSize( _ds )    ((_ds).size)
#define theIDevStatSize( _ds )   ((_ds)->size)
#define theDevStatFree( _ds )    ((_ds).free)
#define theIDevStatFree( _ds )   ((_ds)->free)
#define theDevStatStart( _ds )   ((_ds).start)
#define theIDevStatStart( _ds )  ((_ds)->start)

/**
 * \brief PostScript sees devices though the list of \c devicelist
 * structures. These are instances of device types. Do not try to
 * change any fields in this structure
 */

typedef struct devicelist {
  int32                 flags ;        /**< \brief flags controlling the
                                           device */
/*@owned@*/ /*@notnull@*/
  uint8                 *name ;        /**< \brief the device name,
                                           null terminated */
/*@owned@*/ /*@notnull@*/
  struct DeviceType     *devicetype ;  /**< \brief calls to interface
                                           with device */
/*@owned@*/ /*@notnull@*/
  uint8                 *private_data ; /**< \brief device instance
                                            specific data */
/*@dependent@*/ /*@null@*/
  struct devicelist     *next ;
} DEVICELIST ;

#define DEVICEABSOLUTE  0x0000   /**< \brief Device Type Flag. Flag
                                     "set" if device must not take
                                     file name */
#define DEVICERELATIVE  0x0001   /**< \brief Device Type Flag. Flag set
                                     if device must take file name */
#define DEVICEWRITABLE  0x0002   /**< \brief Device Type Flag. Set if
                                     device can be written to */
#define DEVICESMALLBUFF 0x0004   /**< \brief Device Type Flag. A
                                     smaller buffer is used */
#define DEVICELINEBUFF  0x0008   /**< \brief Device Type Flag. Files of
                                     dev type will be line buffered */

/* Devices use the above flags, as well as... */
#define DEVICEENABLED   0x0010   /**< \brief Device Type Flag. Flag set
                                     if device in search path */
#define DEVICEREMOVABLE 0x0020   /**< \brief Device Type Flag. Set if
                                     media can be removed */
#define DEVICENOSEARCH  0x0040   /**< \brief Device Type Flag. Set if a
                                     general filenameforall omits the
                                     device (SearchOrder set to -1) */
#define DEVICEUNDISMOUNTABLE 0x0080 /**< \brief Device Type Flag. Set
                                        if device can't be
                                        dismounted */

/*
 * The Harlequin RIP asks the device interface for a NULL terminated list of
 * device types.

   Note to Harlequin staff:


       DO NOT CHANGE THIS STRUCTURE: IT IS PUBLISHED TO OEM CUSTOMERS


   First the prototypes for the function pointers contained in the device type:
 */

/** \brief Procedure for periodic service calls to the device.
    \param dev The current device.
    \param recursion Number of levels of call recursion for this tickle
           procedure. This will be 0 for calls from the Harlequin RIP.
    \return -4 indicates that progress information is generated for files
            being read.
            -3 causes the RIP to call any tickle functions that have timed
            out and are waiting to be called.
            -2 causes a PDL timeout error to be signalled.
            -1 causes an interrupt error to be signalled.
            0  indicates no action need be taken.
            Positive values are looked up as keys in the dictionary
            \c serviceinterrupt (in \c execdict), and the value, a procedure,
            is executed. Entries may be defined by a startup file.
            Negative return values take priority over positive return
            values.
*/
typedef int32 (RIPCALL *DEVICELIST_TICKLE) RIPPROTO((
               /*@in@*/ /*@notnull@*/
               struct DeviceType * dev,
               int32 recursion
              ));

/** \brief Return last error for this device.
    \param dev The current device.
    \return One of the values:
            \c DeviceNoError,
            \c DeviceInvalidAccess,
            \c DeviceIOError,
            \c DeviceLimitCheck,
            \c DeviceUndefined,
            \c DeviceUnregistered,
            \c DeviceInterrupted.
            The \c %pagebuffer% device may also return one of:
            \c DeviceVMError,
            \c DeviceReOutput,
            \c DeviceNotReady,
            \c DeviceCancelPage,
            \c DeviceReOutputPageBuffer,
 */
typedef int32 (RIPCALL *DEVICELIST_LAST_ERROR) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev
            ));

/** \brief Call to initialise a device.
    \param dev The current device.
    \return 0 for success, -1 for failure.
 */
typedef int32 (RIPCALL *DEVICELIST_INIT) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev
            ));

/** \brief Call to open a file on device.
    \param dev The current device.
    \param filename The file name to open.
    \param flags One of the following:
                 \c SW_RDONLY,
                 \c SW_WRONLY,
                 \c SW_RDWR,
                 \c SW_APPEND,
                 \c SW_CREAT,
                 \c SW_TRUNC,
                 \c SW_EXCL.
    \return A non-negative file descriptor if successful, -1 for failure.
 */
typedef DEVICE_FILEDESCRIPTOR (RIPCALL *DEVICELIST_OPEN) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev,
            /*@in@*/ /*@notnull@*/
              uint8 * filename,
              int32 openflags
            ));

/** \brief Call to read data from file on device.
    \param dev The current device.
    \param descriptor A descriptor returned by a previous open call.
    \param buff Buffer into which the data read is stored.
    \param len The maximum number of bytes to read.
    \return The number of bytes of data read on success, 0 if the file is at
            EOF, or -1 for failure.
 */
typedef int32 (RIPCALL *DEVICELIST_READ) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev,
              DEVICE_FILEDESCRIPTOR descriptor,
            /*@out@*/ /*@notnull@*/
              uint8 * buff,
              int32 len
            ));

/** \brief Call to write data to file on device.
    \param dev The current device.
    \param descriptor A descriptor returned by a previous open call.
    \param buff Buffer from which the data written is taken.
    \param len The maximum number of bytes to write.
    \return The number of bytes of data written on success, or -1 for failure.
 */
typedef int32 (RIPCALL *DEVICELIST_WRITE) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev,
              DEVICE_FILEDESCRIPTOR descriptor,
            /*@in@*/ /*@notnull@*/
              uint8 * buff,
              int32 len
            ));

/** \brief Call to close file on device.
    \param dev The current device.
    \param descriptor A descriptor returned by a previous open call.
    \return 0 for success, -1 for failure.
 */
typedef int32 (RIPCALL *DEVICELIST_CLOSE) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev,
              DEVICE_FILEDESCRIPTOR descriptor
            ));

/** \brief Call to abort action on the device.
    \param dev The current device.
    \param descriptor A descriptor returned by a previous open call.
    \return 0 for success, -1 for failure.
 */
typedef int32 (RIPCALL *DEVICELIST_ABORT) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev,
              DEVICE_FILEDESCRIPTOR descriptor
            ));

/** \brief Call to seek file on device.
    \param dev The current device.
    \param descriptor A descriptor returned by a previous open call.
    \param destn The location to seek to.
    \param flags One of \c SW_SET, \c SW_INCR, \c SW_XTND.
    \return TRUE for success, FALSE for failure.
 */
typedef int32 (RIPCALL *DEVICELIST_SEEK) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev,
              DEVICE_FILEDESCRIPTOR descriptor,
            /*@in@*/ /*@notnull@*/
              Hq32x2 * destn,
              int32 flags
            ));

/** \brief Call to get bytes available for open file.
    \param dev The current device.
    \param descriptor A descriptor returned by a previous open call.
    \param bytes The number of bytes available.
    \param reason One of \c SW_BYTES_AVAIL_REL, \c SW_BYTES_AVAIL_ABS.
    \return TRUE if there are bytes available (in which case return 0 in bytes
            if the number is not known), FALSE for failure.
 */
typedef int32 (RIPCALL *DEVICELIST_BYTES) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev,
              DEVICE_FILEDESCRIPTOR descriptor,
            /*@out@*/ /*@notnull@*/
              Hq32x2 * bytes,
              int32 reason
            ));

/** \brief Call to check status of file.
    \param dev The current device.
    \param filename The file to check status.
    \param statbuff Pointer to a structure in which to put the file status.
    \return 0 for success, -1 for failure.
 */
typedef int32 (RIPCALL *DEVICELIST_STATUS_FILE) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev,
            /*@in@*/ /*@notnull@*/
              uint8 * filename,
            /*@out@*/ /*@notnull@*/
              STAT * statbuff
            )) ;

/** \brief Call to start listing files.
    \param dev The current device.
    \param pattern A file pattern to match.
    \return A pointer to an iterator structure if successful, NULL for
            failure. If it is known that no files match the pattern, then NULL
            may be returned, and an immediate call to the
            \c DEVICELIST_LAST_ERROR routine should return \c DeviceNoError.
 */
typedef void * (RIPCALL *DEVICELIST_START_LIST) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev,
            /*@in@*/ /*@notnull@*/
              uint8 * pattern
            ));

/** \brief Call to get next file in list.
    \param dev The current device.
    \param handle A file list iterator pointer returned by the
           \c DEVICELIST_START_LIST routine.
    \param pattern The pattern passed to the \c DEVICELIST_START_LIST routine.
    \param entry A structure in which to store the next matching filename.
    \return One of the values:
            \c FileNameMatch,
            \c FileNameRangeCheck,
            \c FileNameNoMatch,
            \c FileNameError (or -1).
 */
typedef int32 (RIPCALL *DEVICELIST_NEXT) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev,
            /*@in@*/ /*@notnull@*/
              void ** handle,
            /*@in@*/ /*@notnull@*/
              uint8 * pattern,
            /*@out@*/ /*@notnull@*/
              FILEENTRY *entry
            ));

/** \brief Call to end listing.
    \param dev The current device.
    \param handle A file list iterator pointer returned by the
           \c DEVICELIST_START_LIST routine.
    \return 0 for success, -1 for failure.
 */
typedef int32 (RIPCALL *DEVICELIST_END_LIST) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev,
            /*@in@*/ /*@notnull@*/
              void * handle
            ));

/** \brief Rename file on the device.
    \param dev The current device.
    \param file1 The name of a file to rename.
    \param file2 The to which \c file1 will be renamed.
    \return 0 for success, -1 for failure.
 */
typedef int32 (RIPCALL *DEVICELIST_RENAME) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev,
            /*@in@*/ /*@notnull@*/
              uint8 * file1,
            /*@in@*/ /*@notnull@*/
              uint8 * file2
            ));

/** \brief Remove file from device.
    \param dev The current device.
    \param filename The name of a file to remove.
    \return 0 for success, -1 for failure.
 */
typedef int32 (RIPCALL *DEVICELIST_DELETE) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev,
            /*@in@*/ /*@notnull@*/
              uint8 * filename
            ));

/** \brief Call to set a device parameter.
    \param dev The current device.
    \param param A structure describing the parameter to set and its value.
    \return One of the values:
            \c ParamAccepted,
            \c ParamTypeCheck,
            \c ParamRangeCheck,
            \c ParamConfigError,
            \c ParamIgnored,
            \c ParamError (or -1).

 Parameters provided by the setdevparams operator are passed to the
 device via DEVICELIST_SET_PARAM() to interpret using the following
 types and constants:
            \c ParamBoolean,
            \c ParamInteger,
            \c ParamString,
            \c ParamFloat,
            \c ParamArray,
            \c ParamDict,
            \c ParamNull.

 Note that composite PostScript values are not allowed as parameters.
 */
typedef int32 (RIPCALL *DEVICELIST_SET_PARAM) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev,
            /*@out@*/ /*@notnull@*/
              DEVICEPARAM * param
            ));

/** \brief Call to start getting device parameters.
    \param dev The current device.
    \return The number of device parameters to enumerate (this may be 0 if
            there are no device parameters), or -1 for failure.
 */
typedef int32 (RIPCALL *DEVICELIST_START_PARAM) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev
            ));

/** \brief Call to get the next device parameter.
    \param dev The current device.
    \param param A structure in which to store the parameter value. If the
                 \c paramname field is NULL, then store the next parameter
                 in an iteration, otherwise store the value of the requested
                 parameter.
    \return One of the values:
            \c ParamIgnored,
            \c ParamAccepted,
            \c ParamError (or -1).
 */
typedef int32 (RIPCALL *DEVICELIST_GET_PARAM) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev,
            /*@out@*/ /*@notnull@*/
              DEVICEPARAM * param
            ));

/** \brief Call to get the status of the device.
    \param dev The current device.
    \param devstat A structure in which to return the status of the device.
    \return 0 for success, -1 for failure.
 */
typedef int32 (RIPCALL *DEVICELIST_STATUS_DEVICE) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev,
            /*@out@*/ /*@notnull@*/
              DEVSTAT * devstat
            ));

/** \brief Call to dismount the device.
    \param dev The current device.
    \return 0 for success, -1 for failure. This should normally return 0, even
            if most device operations are not implemented.
 */
typedef int32 (RIPCALL *DEVICELIST_DISMOUNT) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev
            ));

/** \brief Optional call to return the buffer size that a device requires for
           best operation.
    \param dev The current device.
    \return The buffer size requested in bytes. If this is less than zero,
            then the \c DEVICESMALLBUFF flag is examined to determine the
            size of the buffer.
 */
typedef int32 (RIPCALL *DEVICELIST_BUFFER_SIZE) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev
            ));

/** \brief Miscellaneous control options.
    \param dev The current device.
    \param fileDescriptor A descriptor returned by a previous open call.
    \param opcode A device-specific value, or one of the generic values:
           \c DeviceIOCtl_ShortRead This indicates that the next file read
              should not be optimised for sequential access.
           \c DeviceIOCtl_PDFFilenameToPS The data value \c arg should be
              cast to a \c PDF_FILESPEC. The filename in this structure should
              be converted from a platform specific form to a generic form.
           \c DeviceIOCtl_OSDeviceName The data value \c arg should be
              cast to an \c OS_FILESPEC. The filename in this structure should
              be converted from a platform specific form to a suitable form
              for to \c %os% device.
           \c DeviceIOCtl_RasterRequirements The data value \c arg should be
              cast to a \c RASTER_REQUIREMENTS pointer.
           \c DeviceIOCtl_RenderingStart The data value \c arg should be
              cast to a \c RASTER_REQUIREMENTS pointer.
           \c DeviceIOCtl_GetBufferForRaster The data value \c arg should be
              cast to a \c RASTER_DESTINATION pointer.
           \c DeviceIOCtl_RasterStride The data value \c arg should be
              cast to a pointer to a uint32.
    \param arg A data parameter for the control function.
    \retval 0 for success
    \retval -1 for failure.
    \retval -2 if the opcode is not implemented.
 */
typedef int32 (RIPCALL *DEVICELIST_IOCTL) RIPPROTO((
            /*@in@*/ /*@notnull@*/
              DEVICELIST * dev,
              DEVICE_FILEDESCRIPTOR fileDescriptor,
              int32 opcode,
              intptr_t arg
            ));

typedef int32 (RIPCALL *DEVICELIST_SPARE) RIPPROTO((
              void
            ));

/** \brief This is the structure used to represent a device in the
 * Harlequin RIP.
 *
 * A number of macros, eg \c theIDevTypeNumber, are for convenience in
 * accessing the fields of the structures; OEM's: don't feel obliged
 * to use them. You may want to call e.g. writes on a device in your
 * own code, and the macros provide a notation for doing so.
 */
typedef struct DeviceType {
  int32     devicenumber ;        /**< \brief The device ID number. */
  int32     devicetypeflags ;     /**< \brief Flags to indicate specifics of device. */
  int32     sizeof_private ;      /**< \brief The size of the private data. */
  int32     tickle_control ;      /**< \brief Minimum clock ticks between calls to
                                       tickle function. */
/*@null@*/
  DEVICELIST_TICKLE tickle_proc ; /**< \brief Procedure to service the device. */
  DEVICELIST_LAST_ERROR last_error ;
                                  /**< \brief Return last error for this device. */
  DEVICELIST_INIT device_init ;   /**< \brief Call to initialise device. */
  DEVICELIST_OPEN open_file ;     /**< \brief Call to open file on device. */
  DEVICELIST_READ read_file ;     /**< \brief Call to read data from file on device. */
  DEVICELIST_WRITE write_file ;   /**< \brief Call to write data to file on device. */
  DEVICELIST_CLOSE close_file ;   /**< \brief Call to close file on device. */
  DEVICELIST_ABORT abort_file ;   /**< \brief Call to abort action on the device. */
  DEVICELIST_SEEK seek_file ;     /**< \brief Call to seek file on device. */
  DEVICELIST_BYTES bytes_file ;   /**< \brief Call to get bytes available
                                       for open file. */
  DEVICELIST_STATUS_FILE status_file ;
                                  /**< \brief Call to check status of file. */
  DEVICELIST_START_LIST start_file_list ;
                                  /**< \brief Call to start listing files. */
  DEVICELIST_NEXT next_file ;     /**< \brief Call to get next file in list. */
  DEVICELIST_END_LIST end_file_list ;
                                  /**< \brief Call to end listing. */
  DEVICELIST_RENAME rename_file ; /**< \brief Rename file on the device. */
  DEVICELIST_DELETE delete_file ; /**< \brief Remove file from device. */
  DEVICELIST_SET_PARAM set_param ;
                                  /**< \brief Call to set device parameter. */
  DEVICELIST_START_PARAM start_param ;
                                  /**< \brief Call to start getting device parameters. */
  DEVICELIST_GET_PARAM get_param ;
                                  /**< \brief Call to get the next device parameter. */
  DEVICELIST_STATUS_DEVICE status_device ;
                                  /**< \brief Call to get the status of the device. */
  DEVICELIST_DISMOUNT device_dismount ;
                                  /**< \brief Call to dismount the device. */
  DEVICELIST_BUFFER_SIZE buffer_size ;
                                  /**< \brief Optional call to return buffer size. */
/*@null@*/
  DEVICELIST_IOCTL ioctl_call ; /**< \brief For "special" control notifications. */
/*@null@*/
  DEVICELIST_SPARE spare ;      /**< \brief Spare slot. */
} DEVICETYPE ;

#define theIDevTypeNumber( _dt )     ((_dt)->devicenumber)
#define theDevTypeNumber( _dt )      ((_dt).devicenumber)
#define theIDevTypeFlags( _dt )      ((_dt)->devicetypeflags)
#define theDevTypeFlags( _dt )       ((_dt).devicetypeflags)
#define theIDevTypePrivSize( _dt )   ((_dt)->sizeof_private)
#define theDevTypePrivSize( _dt )    ((_dt).sizeof_private)
#define theIDevTypeTickle( _dt )     ((_dt)->tickle_proc)
#define theDevTypeTickle( _dt )      ((_dt).tickle_proc)
#define theIDevTypeTickleControl( _dt ) ((_dt)->tickle_control)
#define theDevTypeTickleControl( _dt ) ((_dt).tickle_control)

#define theIDeviceFlags( _d )    ((_d)->flags)
#define theDeviceFlags( _d )     ((_d).flags)
#define theIDevName( _d )        ((_d)->name)
#define theDevName( _d )         ((_d).name)
#define theIDevType( _d )        ((_d)->devicetype)
#define theDevType( _d )         ((_d).devicetype)

#define theIDevInit( _d )        ((_d)->devicetype->device_init)
#define theDevInit( _d )         ((_d).devicetype->device_init)
#define theILastErr( _d )        ((_d)->devicetype->last_error)
#define theLastErr( _d )         ((_d).devicetype->last_error)
#define theIOpenFile( _d )       ((_d)->devicetype->open_file)
#define theOpenFile( _d )        ((_d).devicetype->open_file)
#define theIReadFile( _d )       ((_d)->devicetype->read_file)
#define theReadFile( _d )        ((_d).devicetype->read_file)
#define theIWriteFile( _d )      ((_d)->devicetype->write_file)
#define theWriteFile( _d )       ((_d).devicetype->write_file)
#define theICloseFile( _d )      ((_d)->devicetype->close_file)
#define theCloseFile( _d )       ((_d).devicetype->close_file)
#define theIAbortFile( _d )      ((_d)->devicetype->abort_file)
#define theAbortFile( _d )       ((_d).devicetype->abort_file)
#define theISeekFile( _d )       ((_d)->devicetype->seek_file)
#define theSeekFile( _d )        ((_d).devicetype->seek_file)
#define theIBytesFile( _d )       ((_d)->devicetype->bytes_file)
#define theBytesFile( _d )        ((_d).devicetype->bytes_file)
#define theIStatusFile( _d )     ((_d)->devicetype->status_file)
#define theStatusFile( _d )      ((_d).devicetype->status_file)
#define theIStartList( _d )      ((_d)->devicetype->start_file_list)
#define theStartList( _d )       ((_d).devicetype->start_file_list)
#define theINextList( _d )       ((_d)->devicetype->next_file)
#define theNextList( _d )        ((_d).devicetype->next_file)
#define theIEndList( _d )        ((_d)->devicetype->end_file_list)
#define theEndList( _d )         ((_d).devicetype->end_file_list)
#define theIRenameFile( _d )     ((_d)->devicetype->rename_file)
#define theRenameFile( _d )      ((_d).devicetype->rename_file)
#define theIDeleteFile( _d )     ((_d)->devicetype->delete_file)
#define theDeleteFile( _d )      ((_d).devicetype->delete_file)
#define theISetParam( _d )       ((_d)->devicetype->set_param)
#define theIStartParam( _d )     ((_d)->devicetype->start_param)
#define theIGetParam( _d )       ((_d)->devicetype->get_param)
#define theIStatusDevice( _d )   ((_d)->devicetype->status_device)
#define theStatusDevice( _d )    ((_d).devicetype->status_device)
#define theIDevDismount( _d )    ((_d)->devicetype->device_dismount)
#define theDevDismount( _d )     ((_d).devicetype->device_dismount)
#define theIGetBuffSize( _d )    ((_d)->devicetype->buffer_size)
#define theGetBuffSize( _d )     ((_d).devicetype->buffer_size)
#define theIIoctl( _d ) ((_d)->devicetype->ioctl_call)
#define theIoctl( _d )  ((_d).devicetype->ioctl_call)

#define theIPrivate( _d )        ((_d)->private_data)
#define thePrivate( _d )         ((_d).private_data)
#define theIRootPart( _d )       ((_d)->private_data)
#define theRootPart( _d )        ((_d).private_data)
#define theINextDev( _d )        ((_d)->next)
#define theNextDev( _d )         ((_d).next)

#define isDeviceEnabled( _dev )   ((_dev)->flags&DEVICEENABLED)
#define isDeviceRelative( _dev )  ((_dev)->flags&DEVICERELATIVE)
#define isDeviceRemovable( _dev ) ((_dev)->flags&DEVICEREMOVABLE)
#define isDeviceWritable( _dev )  ((_dev)->flags&DEVICEWRITABLE)
#define isDeviceSmallBuff( _dev ) ((_dev)->flags&DEVICESMALLBUFF)
#define isDeviceLineBuff( _dev )  ((_dev)->flags&DEVICELINEBUFF)
#define isDeviceNoSearch( _dev )  ((_dev)->flags&DEVICENOSEARCH)
#define isDeviceUndismountable(_dev) ((_dev)->flags&DEVICEUNDISMOUNTABLE)

#define SetEnableDevice( _dev )   ((_dev)->flags |= DEVICEENABLED)
#define ClearEnableDevice( _dev ) ((_dev)->flags &= ~DEVICEENABLED)

#define SetNoSearchDevice( _dev )   ((_dev)->flags |= DEVICENOSEARCH)
#define ClearNoSearchDevice( _dev ) ((_dev)->flags &= ~DEVICENOSEARCH)

#define SetUndismountableDevice(_dev) ((_dev)->flags |= DEVICEUNDISMOUNTABLE)
#define ClearUndismountableDevice(_dev) ((_dev)->flags &=~DEVICEUNDISMOUNTABLE)


/** \brief SwFindDevice looks up a device by name, returns NULL on failure. */
extern DEVICELIST * RIPCALL SwFindDevice(
/*@in@*/ /*@notnull@*/
  uint8 * deviceName
);

/** A "well known" device name that can be passed to SwFindDevice. */
#define SW_DEVICE_NAME_PROGRESS "progress"


/**
 * \brief Dynamically allocate memory from the core rip's memory space.
 *
 * The three functions, SwAlloc(), SwRealloc(), and SwFree(), allow
 * external devices to dynamically allocate memory from the core rip's
 * memory space.  These functions can be safely called any time after
 * SwStart, but large blocks should be freed as soon as possible to
 * avoid interfering with jobs.
 */
extern /*@only@*/ /*@null@*/ uint8 * RIPCALL SwAlloc RIPPROTO((
  int32 bytes
));

/**
 * \brief Dynamically allocate memory from the core rip's memory space.
 *
 * See SwAlloc().
 */
extern /*@only@*/ /*@null@*/ uint8 * RIPCALL SwRealloc RIPPROTO((
/*@only@*/ /*@notnull@*/
  void * pointer,
  int32 bytes
));

/**
 * \brief Dynamically allocate memory from the core rip's memory space.
 *
 * See SwAlloc().
 */
extern void RIPCALL SwFree RIPPROTO((
/*@only@*/ /*@notnull@*/
  void * pointer
));

/**
 * \brief For use in DEVICELIST_NEXT() to assist pattern matching on
 * file names
 */
extern int32 RIPCALL SwPatternMatch RIPPROTO((
/*@in@*/ /*@notnull@*/
  uint8 * pattern,
/*@in@*/ /*@notnull@*/
  uint8 * string
));

/**
 * \brief For use in DEVICELIST_NEXT() to assist pattern matching on
 * file names
 */
extern int32 RIPCALL SwLengthPatternMatch RIPPROTO((
/*@in@*/ /*@notnull@*/
  uint8 * pattern,
  int32 plen,
/*@in@*/ /*@notnull@*/
  uint8 * string,
  int32 slen
));

/**
 * \brief For use when adding custom filters via the device interface
 */
extern int32 RIPCALL SwReadFilterBytes RIPPROTO((
/*@in@*/ /*@notnull@*/
  DEVICELIST * dev,
/*@out@*/ /*@notnull@*/
  uint8 ** ret_buff
));

/**
 * \brief For use when adding custom filters via the device interface
 */
extern int32 RIPCALL SwReplaceFilterBytes RIPPROTO((
/*@in@*/ /*@notnull@*/
  DEVICELIST * dev,
  int32 len
));

/**
 * \brief For use when adding custom filters via the device interface
 */
extern int32 RIPCALL SwWriteFilterBytes RIPPROTO((
/*@in@*/ /*@notnull@*/
  DEVICELIST * dev,
/*@in@*/ /*@notnull@*/
  uint8 * buff,
  int32 len
));

/**
 * \brief For use when adding custom filters via the device interface
 */
extern int32 RIPCALL SwSeekFilterBytes RIPPROTO((
/*@in@*/ /*@notnull@*/
  DEVICELIST *dev,
  int32 offset
));

/**
 * \brief For use when adding custom filters via the device interface
 */
extern int32 RIPCALL SwTestAndTickle RIPPROTO((
  void
));

#ifndef NULL
#define NULL 0
#endif

#ifndef EOF
#define EOF (-1)
#endif

#ifdef __cplusplus
}
#endif

/** \} */


#endif  /* ! __SWDEVICE_H__ */
