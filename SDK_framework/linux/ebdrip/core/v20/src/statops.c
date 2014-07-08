/** \file
 * \ingroup ps
 *
 * $HopeName: SWv20!src:statops.c(EBDSDK_P.1) $
 *
 * Copyright (C) 1989-2014 Global Graphics Software Ltd. All rights reserved.
 * Global Graphics Software Ltd. Confidential Information.
 *
 * \brief
 * PostScript operators from statusdict.
 */

#include <math.h>

#include "core.h"
#include "coreinit.h"
#include "corejob.h"
#include "pscontext.h"
#include "swenv.h" /* get_rtime */
#include "swoften.h"
#include "swdevice.h"
#include "swerrors.h"
#include "objects.h"
#include "fileio.h"
#include "mm.h"
#include "mmcompat.h"
#include "namedef_.h"
#include "devices.h"

#include "std_file.h"
#include "often.h"
#include "bitblts.h"
#include "matrix.h"
#include "params.h"
#include "miscops.h"
#include "psvm.h"
#include "stacks.h"
#include "dicthash.h" /* insert_hash */
#include "display.h"
#include "ndisplay.h"
#include "graphics.h"
#include "binscan.h"
#include "showops.h"

#include "control.h"
#include "dlstate.h"
#include "render.h"
#include "bandtable.h"
#include "devops.h"
#include "stackops.h"
#include "tstream.h"
#include "fileops.h"
#include "interrupts.h"

#include "statops.h"

int32 hwareiomode = 0 ;
int32 swareiomode = 0 ;


static Bool bootsysfile = TRUE ;
static int32 baud9b = 9600 ;
static int32 baud25b = 9600;
static int32 option9b = 0;
static int32 option25b = 0;
static int32 baud9i = 0;
static int32 option9i = 3;
static int32 baud25i = 9600;
static int32 option25i = 0;
static int32 pagetype = 0;
static Bool pstackorder = TRUE;
static Bool hqnlogo = FALSE;

static Bool readchecksetbool(Bool *toset, corecontext_t *corecontext);
static Bool readcheckrangesetint(int32 *toset ,
                                 int32 rangemin ,
                                 int32 rangemax, corecontext_t *corecontext);
static Bool sccbi( int32 *baud9 ,
                   int32 *option9 ,
                   int32 *baud25 ,
                   int32 *option25 );
static Bool setsccbi(int32 *baud9 ,
                     int32 *option9 ,
                     int32 *baud25 ,
                     int32 *option25,
                     corecontext_t *corecontext);

/* these are in seconds */
int32 jobtimeout = ( 2 * 60 * 60 ) ;
static int32 manualtimeout = ( 1 * 60 ) ;
/* moved to user params:
   waittimeout = ( 5 * 60 ) -- lw compatability 300 */
static int32 timeout = 0 ;

#define IDLE_NUMBER_DEFAULT -1 /* -1 implies standard. */
static int32 idlenumber = IDLE_NUMBER_DEFAULT ;

static uint8 *idlestuff = NULL ;
static uint8 defaultidlestuff[] = {
  0 , 100 , 100 , 0 , 94 ,
  4 , 100 , 100 , 0 , 81 ,  8 , 100 , 100 , 0 , 81 ,
  4 , 120 , 120 , 0 , 81 ,  8 , 120 , 120 , 0 , 81 ,
  4 , 140 , 140 , 0 , 62 ,  8 , 140 , 140 , 0 , 81 ,
  5 , 100 , 100 , 0 , 26 ,  9 , 100 , 100 , 0 , 26 ,
  5 , 120 , 120 , 0 , 26 ,  9 , 120 , 100 , 0 , 26 ,
  5 , 140 , 140 , 0 , 26 ,  9 , 140 , 100 , 0 , 26 ,
  1 , 100 , 100 , 0 , 26
} ;

#define HARDWAREIOMODELIMIT 2

static int32 blink = 1;

/* ----------------------------------------------------------------------------
   function:            ###blink_()       author:              Andrew Cave
   creation date:       11-Aug-1989       last modification:   ##-###-####
   arguments:           none .
   description:

   Sets the current number of blinks for the activity monitor.

---------------------------------------------------------------------------- */
Bool blink_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  oInteger(inewobj) = blink ;
  return push( & inewobj , & operandstack ) ;
}

Bool setblink_(ps_context_t *pscontext)
{
  int32 lblink ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! stack_get_integers(&operandstack, & lblink , 1 ))
    return FALSE ;

  if (( lblink < 0 ) || ( lblink > 10 ))
    return error_handler( RANGECHECK) ;

  blink = lblink ;
  pop( & operandstack ) ;

  return TRUE ;
}

Bool resetprinter_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push( & tnewobj , & operandstack );
}

Bool sendpcmd_(ps_context_t *pscontext)
{
  int32 status ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! stack_get_integers(&operandstack, & status , 1 ))
    return FALSE ;
  pop( & operandstack ) ;
  oInteger(inewobj) = status ;
#ifdef isprinter
  if ( printer )
    oInteger(inewobj) = sendprintercmd( status ) ;
#endif
  return push( & inewobj , & operandstack ) ;
}

/* ----------------------------------------------------------------------------
   function:            ###interrupt_()   author:              Andrew Cave
   creation date:       11-Aug-1989       last modification:   ##-###-####
   arguments:           none .
   description:

   Enables interrupts.  For SW this is Control-C.

---------------------------------------------------------------------------- */
Bool setinterrupt_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  raise_interrupt();
  if (allow_interrupt)
    dosomeaction = TRUE ;
  return TRUE ;
}

Bool clearinterrupt_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  clear_interrupts();
  return TRUE ;
}

Bool disableinterrupt_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  allow_interrupt = FALSE ;
  return TRUE ;
}

Bool enableinterrupt_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  allow_interrupt = TRUE ;
  if (user_interrupt()) {
    dosomeaction = TRUE;
  }
  return TRUE ;
}

Bool interruptenabled_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push( allow_interrupt ? &tnewobj : &fnewobj , &operandstack );
}

/* ----------------------------------------------------------------------------
   function:            ###dosysstart_()  author:              Andrew Cave
   creation date:       11-Aug-1989       last modification:   ##-###-####
   arguments:           none .
   description:

   Returns TRUE if want to execute a $(ROOT)/start.ps on bootup.

---------------------------------------------------------------------------- */
Bool dosysstart_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push( bootsysfile ? & tnewobj : & fnewobj , & operandstack ) ;
}

Bool setdosysstart_(ps_context_t *pscontext)
{
  return readchecksetbool(&bootsysfile, pscontext->corecontext) ;
}

/* ----------------------------------------------------------------------------
   function:            ###sccbatch_()    author:              Andrew Cave
   creation date:       11-Aug-1989       last modification:   ##-###-####
   arguments:           none .
   description:

   Returns/sets the settings for either serial channel.

---------------------------------------------------------------------------- */
Bool sccbatch_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return sccbi( & baud9b , & option9b ,
               & baud25b , & option25b ) ;
}

Bool setsccbatch_(ps_context_t *pscontext)
{
  return setsccbi( & baud9b , & option9b ,
                   & baud25b , & option25b, pscontext->corecontext) ;
}

/* ----------------------------------------------------------------------------
   function:            ###sccinteractive_() author:              Andrew Cave
   creation date:       11-Aug-1989          last modification:   ##-###-####
   arguments:           none .
   description:

   Returns/sets the settings for either serial channel.

---------------------------------------------------------------------------- */
Bool sccinteractive_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return sccbi( & baud9i , & option9i ,
               & baud25i , & option25i ) ;
}

Bool setsccinteractive_(ps_context_t *pscontext)
{
  return setsccbi( & baud9i , & option9i ,
                   & baud25i , & option25i, pscontext->corecontext) ;
}

/* ----------------------------------------------------------------------------
   function:            hardwareiomode_() author:              Andrew Cave
   creation date:       11-Aug-1989       last modification:   ##-###-####
   arguments:           none .
   description:

   Returns/sets the current setting of the hardware I/O mode.

---------------------------------------------------------------------------- */
Bool hardwareiomode_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  oInteger(inewobj) = hwareiomode ;
  return push( & inewobj , & operandstack ) ;
}

Bool sethardwareiomode_(ps_context_t *pscontext)
{
  int32 itemp ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! stack_get_integers(&operandstack, & itemp , 1 ))
    return FALSE ;

  if (( itemp < 0 ) || ( itemp > HARDWAREIOMODELIMIT ))
    return error_handler( RANGECHECK ) ;

  hwareiomode = itemp ;
  pop( & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            ###softwareiomode_() author:              Andrew Cave
   creation date:       11-Aug-1989          last modification:   ##-###-####
   arguments:           none .
   description:

   Returns/sets the current setting of the software I/O mode.

---------------------------------------------------------------------------- */
Bool softwareiomode_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  oInteger(inewobj) = swareiomode ;
  return push( & inewobj , & operandstack ) ;
}

Bool setsoftwareiomode_(ps_context_t *pscontext)
{
  int32 itemp ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! stack_get_integers(&operandstack, & itemp , 1 ))
    return FALSE ;

  if (( itemp < 0 ) || ( itemp > 5 ))
    return error_handler( RANGECHECK ) ;

  swareiomode = itemp ;
  pop( & operandstack ) ;
  return TRUE ;
}


/* ----------------------------------------------------------------------------
   function:            ####pagetype_()   author:              Andrew Cave
   creation date:       11-Aug-1989       last modification:   ##-###-####
   arguments:           none .
   description:

   Returns/sets the default pagetype for the printer.

---------------------------------------------------------------------------- */
Bool pagetype_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  oInteger(inewobj) = pagetype ;
  return push( & inewobj , & operandstack ) ;
}

Bool setpagetype_(ps_context_t *pscontext)
{
  return readcheckrangesetint(&pagetype, 0, 1, pscontext->corecontext) ;
}

/* ----------------------------------------------------------------------------
   function:            ####margins_()    author:              Andrew Cave
   creation date:       11-Aug-1989       last modification:   ##-###-####
   arguments:           none .
   description:

   Opens/closes the apple talk connection.

---------------------------------------------------------------------------- */
static int32 xmargin = 0 , ymargin = 0 ;

Bool margins_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  oInteger(inewobj) = ymargin ;
  if ( ! push( & inewobj , & operandstack ))
    return FALSE ;
  oInteger(inewobj) = xmargin ;
  return push( & inewobj , & operandstack ) ;
}

Bool setmargins_(ps_context_t *pscontext)
{
  int32 margins[ 2 ] ;
  corecontext_t *corecontext = pscontext->corecontext ;

  if ( ! stack_get_integers(&operandstack, margins , 2 ))
    return FALSE ;

  if ( NOTINEXITSERVER(corecontext))
    return error_handler( INVALIDACCESS ) ;

  ymargin = margins[ 0 ] ;
  xmargin = margins[ 1 ] ;

  npop( 2 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            pagecount_()      author:              Andrew Cave
   creation date:       11-Aug-1989       last modification:   ##-###-####
   arguments:           none .
   description:

   Returns the number of pages printed by this invocation of SW.

---------------------------------------------------------------------------- */
Bool pagecount_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  oInteger(inewobj) = showno ;
  return push( & inewobj , & operandstack ) ;
}

/* ----------------------------------------------------------------------------
   function:            ###pagestackorder_() author:              Andrew Cave
   creation date:       11-Aug-1989          last modification:   ##-###-####
   arguments:           none .
   description:

   Returns/sets the page stacking order.

---------------------------------------------------------------------------- */
Bool pagestackorder_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push( pstackorder ? & tnewobj : & fnewobj , & operandstack ) ;
}

Bool setpagestackorder_(ps_context_t *pscontext)
{
  return readchecksetbool(&pstackorder, pscontext->corecontext) ;
}

/* ----------------------------------------------------------------------------
   function:            diskonline_()     author:              Andrew Cave
   creation date:       11-Aug-1989       last modification:   ##-###-####
   arguments:           none .
   description:

   Returns if a writeable disk is registered - always TRUE.

---------------------------------------------------------------------------- */
Bool diskonline_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push( & tnewobj , & operandstack ) ;
}

/* ----------------------------------------------------------------------------
   function:            ramsize_()        author:              Andrew Cave
   creation date:       11-Aug-1989       last modification:   ##-###-####
   arguments:           none .
   description:

   Returns the minimum RAM size.  Is 4 Meg.

---------------------------------------------------------------------------- */
Bool ramsize_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  oInteger(inewobj) = CAST_SIZET_TO_INT32(mm_total_size()) ;

  return push( & inewobj , & operandstack ) ;
}
/* ----------------------------------------------------------------------------
   function:            ###dostartpage_() author:              Andrew Cave
   creation date:       11-Aug-1989       last modification:   ##-###-####
   arguments:           none .
   description:

   Returns/sets the if a test page is printed on startup.

---------------------------------------------------------------------------- */
Bool dostartpage_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push( hqnlogo ? & tnewobj : & fnewobj , & operandstack ) ;
}

Bool setdostartpage_(ps_context_t *pscontext)
{
  return readchecksetbool(&hqnlogo, pscontext->corecontext) ;
}

/* ----------------------------------------------------------------------------
   function:            ####defaulttimeouts_() author:              Andrew Cave
   creation date:       11-Aug-1989            last modification:   ##-###-####
   arguments:           none .
   description:

   Returns/sets the default timeout values.

---------------------------------------------------------------------------- */
Bool defaulttimeouts_(ps_context_t *pscontext)
{
  OBJECT * statusdict_waittimeout;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  oInteger(inewobj) = jobtimeout ;
  if ( ! push( & inewobj , & operandstack ))
    return FALSE ;
  oInteger(inewobj) = manualtimeout ;
  if ( ! push( & inewobj , & operandstack ))
    return FALSE ;

  /* get the value from statusdict */
  statusdict_waittimeout = fast_extract_hash_name(& systemdict,
                                                  NAME_statusdict);
  if (! statusdict_waittimeout)
    return error_handler (UNREGISTERED);
  statusdict_waittimeout = fast_extract_hash_name(statusdict_waittimeout,
                                                  NAME_waittimeout);
  if (! statusdict_waittimeout)
    return error_handler (UNREGISTERED);
  oInteger(inewobj) = oInteger(*statusdict_waittimeout);

  return push( & inewobj , & operandstack ) ;
}

Bool setdefaulttimeouts_(ps_context_t *pscontext)
{
  register int32 i ;
  register OBJECT *theo ;
  int32 timevals[ 3 ] ;
  corecontext_t *corecontext = pscontext->corecontext ;

  for ( i = 0 ; i < 3 ; ++i ) {
    theo = stackindex( i , & operandstack ) ;
    if (oType(*theo) != OINTEGER )
      return error_handler( TYPECHECK ) ;

    timevals[ i ] = oInteger(*theo) ;
    if ( timevals[ i ] < 0 )
      return error_handler( RANGECHECK ) ;
  }

  if ( NOTINEXITSERVER(corecontext))
    if ( ! INNORMALSERVER(corecontext))
      return error_handler( INVALIDACCESS ) ;

  if ( INNORMALSERVER(corecontext)) {
    OBJECT * statusdict;
    jobtimeout = timevals[ 2 ] ;
    manualtimeout = timevals[ 1 ] ;

    /* Now set the appropriate value in statusdict */
    statusdict = fast_extract_hash_name(& systemdict, NAME_statusdict);
    if (! statusdict)
      return error_handler (UNREGISTERED);
    oInteger(inewobj) =  timevals[ 0 ];
    oName(nnewobj) = system_names + NAME_waittimeout;
    if (!insert_hash(statusdict, &nnewobj, &inewobj))
      return error_handler (UNREGISTERED);
  }

  npop( 3 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            ####password_()      author:              Andrew Cave
   creation date:       11-Aug-1989          last modification:   ##-###-####
   arguments:           none .
   description:

   Check's/sets the password.

---------------------------------------------------------------------------- */
Bool setpassword_(ps_context_t *pscontext)
{
  OBJECT *passwdo , *newpasswdo ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  passwdo = stackindex( 1 , &operandstack ) ;
  newpasswdo = theTop( operandstack ) ;

  /* Different from normal for "security": pop passwords before operation in
     case of failure. */
  npop( 2 , & operandstack ) ;

  if ( !check_job_password(pscontext->corecontext, passwdo) ||
       !set_job_password(pscontext->corecontext, newpasswdo) )
    return push( &fnewobj , &operandstack ) ;

  return push( & tnewobj , & operandstack ) ;
}

Bool checkpassword_(ps_context_t *pscontext)
{
  OBJECT *passwdo , *theo ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  passwdo = theTop( operandstack ) ;

  if ( check_job_password(pscontext->corecontext, passwdo) )
    theo = &tnewobj ;
  else
    theo = &fnewobj ;
  Copy( passwdo , theo  ) ;

  return TRUE ;
}



/* ----------------------------------------------------------------------------
   function:            ####jobtimeout_()    author:              Andrew Cave
   creation date:       11-Aug-1989          last modification:   ##-###-####
   arguments:           none .
   description:

   Checks/sets the job timeout (the version in user params).

---------------------------------------------------------------------------- */
static int32 end_timeout = MAXINT32 ;

int32 curr_jobtimeout( void )
{
  int32 curr = get_rtime() / 1000;

  return timeout ? (( end_timeout - curr ) > 0 ?
                    end_timeout - curr : 1 ) : 0 ;
}

Bool jobtimeout_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  oInteger(inewobj) = curr_jobtimeout();
  return push( & inewobj , & operandstack ) ;
}

void setjobtimeout(corecontext_t *context, int32 new_timeout)
{
  timeout = new_timeout ;

  if ( timeout ) {
    end_timeout = timeout + get_rtime() / 1000;
  }
  else
    end_timeout = MAXINT32 ;

  corejob_set_timeout(context->page->job, new_timeout);
}

Bool setjobtimeout_(ps_context_t *pscontext)
{
  int32 ltimeout ;

  if ( ! stack_get_integers(&operandstack, & ltimeout , 1 ))
    return FALSE ;

  if ( ltimeout < 0 )
    return error_handler( RANGECHECK ) ;

  pop( & operandstack ) ;

  setjobtimeout(ps_core_context(pscontext), ltimeout);

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            setstdio_()        author:              Andrew Cave
   creation date:       11-Aug-1989        last modification:   ##-###-####
   arguments:           none .
   description:

   Sets the standard i/o files.

---------------------------------------------------------------------------- */
Bool setstdio_(ps_context_t *pscontext)
{
  register OBJECT *theo ;
  register FILELIST *flinptr , *floutptr ;
  uint16 filter_id_in, filter_id_out ;
  FILELIST * flptr_diversion = NULL;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( theStackSize( operandstack ) < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OFILE )
    return error_handler( TYPECHECK) ;

  if ( ! oCanWrite(*theo))
    if ( ! object_access_override(theo))
      return error_handler( INVALIDACCESS ) ;

  floutptr = oFile(*theo) ;
  if ( isIInputFile( floutptr ))
    return error_handler( IOERROR ) ;
  filter_id_out = theLen(*theo) ;
#if 0
  if ( ! isIOpenFileFilter( theo, floutptr ))
    return error_handler( IOERROR ) ;
#endif

  theo = stackindex( 1 , & operandstack ) ;
  if ( oType(*theo) != OFILE )
    return error_handler( TYPECHECK) ;

  if ( ! oCanRead(*theo))
    if ( ! object_access_override(theo))
      return error_handler( INVALIDACCESS ) ;

  flinptr = oFile(*theo) ;
  if ( isIOutputFile( flinptr ))
    return error_handler( IOERROR ) ;
  filter_id_in = theLen(*theo) ;
#if 0
  if ( ! isIOpenFileFilter( theo, flinptr ))
    return error_handler( IOERROR ) ;
#endif

  /* are we diverting the current standard input file, and is it now
     being changed to something different? If so, close the diversion
     file and set everything back to normal. Then reopen the T-stream
     on the new stdin file. Remember the flile pointer so we can
     reopen it */
  if (flinptr != theIStdin (workingsave)) {
    flptr_diversion = diverted_tstream ();
    if (flptr_diversion != NULL) {
      if (! terminate_tstream (theIStdin (workingsave)))
        return FALSE;
    }
  }

  /* Set global file i/o pointers to these two. */
  theIStdin ( workingsave ) = flinptr  ;
  theIStdout( workingsave ) = floutptr ;
  theISaveStdinFilterId( workingsave )  = filter_id_in ;
  theISaveStdoutFilterId( workingsave ) = filter_id_out ;


  if (flptr_diversion != NULL) {
    /* Open the t-stream file again for writing. Reconstruct the name
       (as per filename_ below) and call file_open */
    uint8 filename[LONGESTFILENAME];
    DEVICELIST * dev = theIDeviceList(flptr_diversion);
    OBJECT stringo = OBJECT_NOTVM_NOTHING, fileo = OBJECT_NOTVM_NOTHING;

    strcpy((char *) filename, "%");
    strcat((char *) filename, (char *) theIDevName(dev));
    strcat((char *) filename, "%");
    if (theINLen(flptr_diversion) > 0)
      strcat((char *) filename, (char *) theICList(flptr_diversion));

    OCopy(stringo, snewobj);
    oString(stringo) = filename;
    theLen(stringo) = (uint16) strlen((char *) filename);

    /* Open the t-stream file again for writing */
    if (! file_open(& stringo, SW_WRONLY | SW_CREAT | SW_TRUNC,
                    WRITE_FLAG, /* append_mode = */ FALSE,
                    /* base_flag = */ 0, & fileo))
      return FALSE;

    /* and set it up as a T Stream again */
    if (! start_tstream(theIStdin (workingsave),
                        oFile(fileo),
                        FALSE))
      return FALSE;
  }

  npop( 2 , & operandstack ) ;
  return TRUE ;
}

/* ---------------------------------------------------------------------- */
/* stdiostatus_() returns two bools to indicate the status of the stdin and
 * stdout file objects as actually used by the RIP.  This is because while the
 * server loop sets up stdin and stdout in serverdict before calling setstdio,
 * end user PS can also call setstdio but not update serverdict, thereby getting
 * the RIP and serverdict out of synch.  At the moment this is only important
 * for the executive since it tries to detect stdin being closed so it can close
 * itself.
 */
Bool stdiostatus_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push2((isIOpenFile(theIStdin(workingsave)) ? &tnewobj : &fnewobj),
               (isIOpenFile(theIStdout(workingsave)) ? &tnewobj : &fnewobj),
               &operandstack);
}

/* ---------------------------------------------------------------------- */
Bool setstderr_(ps_context_t *pscontext)
{
  register OBJECT *theo ;
  register FILELIST *floutptr ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if ( oType(*theo) != OFILE )
    return error_handler( TYPECHECK) ;

  if ( ! oCanWrite(*theo))
    if ( ! object_access_override(theo))
      return error_handler( INVALIDACCESS ) ;

  floutptr = oFile(*theo) ;
  if ( isIInputFile( floutptr ))
    return error_handler( IOERROR ) ;
#if 0
  if ( ! isIOpenFileFilter( theo, floutptr ))
    return error_handler( IOERROR ) ;
#endif

/* Set global file i/o pointers to this. */
  theIStderr( workingsave ) = floutptr ;
  theISaveStderrFilterId( workingsave )  = theLen(*theo) ;

  pop( & operandstack ) ;
  return TRUE ;
}

/* ---------------------------------------------------------------------- */
Bool setteestream_(ps_context_t *pscontext)
{
  /* This operator takes a (usually fully-qualified) filename
  (string). It checks to see if there is a file open on %stdin% (there
  should be). Then it opens a file on the name given and substitutes a
  set of file reading routines in between the stdin file and its
  implementation. If the stdin file changes (with setstdio above), the
  t-stream file is reopened */

  FILELIST * stdinfile = theIStdin(workingsave);
  OBJECT * theo ;
  OBJECT fileo = OBJECT_NOTVM_NOTHING;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if (isEmpty (operandstack))
    return error_handler(STACKUNDERFLOW);

  theo = theTop(operandstack);
  if (oType(*theo) != OSTRING)
    return error_handler(TYPECHECK);

  if (stdinfile == NULL)
    return error_handler(IOERROR);

  if (! isIOpenFile(stdinfile))
    return error_handler (IOERROR);

  if (isIOutputFile(stdinfile))
    return error_handler(IOERROR);
  HQASSERT(isIInputFile(stdinfile), "file is neither input nor output");

  /* Open the t-stream file for writing */
  if (! file_open(theo, SW_WRONLY | SW_CREAT | SW_TRUNC,
                  WRITE_FLAG, /* append_mode = */ FALSE,
                  /* base_flag = */ 0, & fileo))
    return FALSE;

  if ( ! start_tstream(stdinfile, oFile(fileo), FALSE) )
    return FALSE;

  pop (& operandstack);
  return TRUE;
}

/* ----------------------------------------------------------------------------
   function:            ###scc###_()      author:              Andrew Cave
   creation date:       11-Aug-1989       last modification:   ##-###-####
   arguments:           none .
   description:

   Returns/sets the settings for either serial channel.

---------------------------------------------------------------------------- */
Bool openscc_(ps_context_t *pscontext)
{
  int32 i ;
  OBJECT *theo ;
  int32 ivals[ 4 ] ;
  OBJECT infile = OBJECT_NOTVM_NOTHING, outfile = OBJECT_NOTVM_NOTHING ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

/* baud(int) parity(int 0-5) transparent(boolean) */
  if ( theStackSize( operandstack ) < 3 )
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if (oType(*theo) != OBOOLEAN )
    return error_handler( TYPECHECK ) ;

  for ( i = 1 ; i < 4 ; ++i ) {
    theo = stackindex( i , & operandstack ) ;
    if (oType(*theo) != OINTEGER )
      return error_handler( TYPECHECK ) ;

    ivals[ i ] = oInteger(*theo) ;
  }
  if (( ivals[ 1 ] < 0 ) || ( ivals[ 1 ] > 3 ))
    return error_handler( RANGECHECK ) ;
  if (( ivals[ 2 ] < 0 ) || ( ivals[ 2 ] > 100000 ))
    return error_handler( RANGECHECK ) ;
  if (( ivals[ 3 ] != 0 ) && ( ivals[ 3 ] != 1 ) &&
      ( ivals[ 3 ] != 9 ) && ( ivals[ 3 ] != 25 ))
    return error_handler( RANGECHECK ) ;
  npop( 4 , & operandstack ) ;

  SetIOpenFlag(&std_files[STDIN]) ;
  file_store_object(&infile, &std_files[STDIN], LITERAL) ;

  if (( ivals[ 3 ] == 0 ) || ( ivals[ 3 ] == 9 )) {
    SetIOpenFlag(&std_files[STDOUT]) ;
    file_store_object(&outfile, &std_files[STDOUT], LITERAL) ;
  }
  else {
    SetIOpenFlag(&std_files[STDERR]) ;
    file_store_object(&outfile, &std_files[STDERR], LITERAL) ;
  }

  return push2(&infile, &outfile, &operandstack) ;
}


Bool closescc_(ps_context_t *pscontext)
{
  int32 sccfileno ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! stack_get_integers(&operandstack, & sccfileno , 1 ))
    return FALSE ;

  if (( sccfileno != 0 ) && ( sccfileno != 1 ) &&
      ( sccfileno != 9 ) && ( sccfileno != 25 ))
    return error_handler( RANGECHECK ) ;
  pop( & operandstack ) ;

  return TRUE ;
}

Bool sccfiles_(ps_context_t *pscontext)
{
  int32 sccfileno ;
  OBJECT infile = OBJECT_NOTVM_NOTHING, outfile = OBJECT_NOTVM_NOTHING ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! stack_get_integers(&operandstack, & sccfileno , 1 ))
    return FALSE ;

  if (( sccfileno != 0 ) && ( sccfileno != 1 ) &&
      ( sccfileno != 9 ) && ( sccfileno != 25 ))
    return error_handler( RANGECHECK ) ;

  pop( & operandstack ) ;

  SetIOpenFlag(&std_files[STDIN]) ;
  file_store_object(&infile, &std_files[STDIN], LITERAL) ;

  if (( sccfileno == 0 ) || ( sccfileno == 9 )) {
    SetIOpenFlag(&std_files[STDOUT]) ;
    file_store_object(&outfile, &std_files[STDOUT], LITERAL) ;
  }
  else {
    SetIOpenFlag(&std_files[STDERR]) ;
    file_store_object(&outfile, &std_files[STDERR], LITERAL) ;
  }

  return push2(&infile, &outfile, &operandstack) ;
}

/* ----------------------------------------------------------------------------
   function:            ###sccconfig_()      author:              Andrew Cave
   creation date:       19-Nov-1990          last modification:   ##-###-####
   arguments:           none .
   description:

   Returns/reads the serial communication configuration paramater.

---------------------------------------------------------------------------- */
Bool sccconfig = FALSE ;
Bool getsccconfig_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  return push( sccconfig ? & tnewobj : & fnewobj , & operandstack ) ;
}

Bool setsccconfig_(ps_context_t *pscontext)
{
  return readchecksetbool(&sccconfig, pscontext->corecontext) ;
}

/* ----------------------------------------------------------------------------
   function:            ###idlefonts_()      author:              Andrew Cave
   creation date:       15-Aug-1989          last modification:   ##-###-####
   arguments:           none .
   description:

   Returns/reads the idle fonts.

---------------------------------------------------------------------------- */
static Bool ps_idlefonts_swstart(struct SWSTART *params)
{
  UNUSED_PARAM(struct SWSTART *, params) ;

  if ( (idlestuff = mm_alloc_static(150)) == NULL )
    return FALSE ;

  return TRUE ;
}

Bool setidlefonts_(ps_context_t *pscontext)
{
  register int32 i , limit ;
  register OBJECT *theo ;
  int32 allthevals[ 150 ] ;
  corecontext_t *corecontext = pscontext->corecontext ;

  if (( limit = num_to_mark()) < 0 )
    return error_handler( UNMATCHEDMARK ) ;
/* Must be a multiple of five numbers. */
  if (( limit % 5 ) || ( limit > 150 ))
    return error_handler( RANGECHECK ) ;
/* Byte range check all the values. */
  for ( i = 0 ; i < limit ; ++i ) {
    theo = stackindex( i , & operandstack ) ;
    if (oType(*theo) != OINTEGER )
      return error_handler( TYPECHECK ) ;

    allthevals[ i ] = oInteger(*theo) ;
    if (( allthevals[ i ] < 0 ) || ( allthevals[ i ] > 255 ))
      return error_handler( RANGECHECK ) ;
  }
/* Must be outside exitserver context. */
  if ( NOTINEXITSERVER(corecontext))
    return error_handler( INVALIDACCESS ) ;

/* Update our array - unless only a mark present. */
  if ( ! limit )
    idlenumber = IDLE_NUMBER_DEFAULT ;
  else {
    idlenumber = limit ;
    for ( i = 0 ; limit-- > 0 ; ++i )
      idlestuff[ i ] = ( uint8 )allthevals[ limit ] ;
  }
/* Finally clear up to & inlclude mark. */
  return cleartomark_(pscontext) ;
}

Bool idlefonts_(ps_context_t *pscontext)
{
  register int32 i ;
  OBJECT omark = OBJECT_NOTVM_MARK ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! push(&omark, &operandstack) )
    return FALSE ;

/* Standardset of characters if empty. */
  if ( idlenumber == IDLE_NUMBER_DEFAULT ) {
    for ( i = 0 ; i < NUM_ARRAY_ITEMS(defaultidlestuff) ; ++i ) {
      oInteger(inewobj) = defaultidlestuff[ i ] ;
      if ( ! push( & inewobj , & operandstack ))
        return FALSE ;
    }
  }
  else {
    for ( i = 0 ; i < idlenumber ; ++i ) {
      oInteger(inewobj) = ( int32 )idlestuff[ i ] ;
      if ( ! push( & inewobj , & operandstack ))
        return FALSE ;
    }
  }
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            ###disk###_()        author:              Andrew Cave
   creation date:       15-Aug-1989          last modification:   ##-###-####
   arguments:           none .
   description:

   Returns the number of free/total pages on the disk.

---------------------------------------------------------------------------- */
Bool diskstatus_(ps_context_t *pscontext)
{
  DEVSTAT status;
  SYSTEMVALUE free_space;
  SYSTEMVALUE total_space;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ((*theIStatusDevice(osdevice))(osdevice, &status) < 0)
  {
    free_space = total_space = 0.0;
  }
  else
  {
    free_space  = HqU32x2ToDouble(&status.free);
    total_space = HqU32x2ToDouble(&status.size);
  }

  /* As in devstatus, return the results in 1024 byte blocks.
     Note 2017 says: "a page is 1024 characters", whilst 3010 says: "a page
     is typically 1024 characters".  We go with the former here to meet the
     assumptions made by font downloaders etc.  Previously we'd been using
     the underlying device's blocksize, status.block_size (which may well be
     what 'typically' means in the 3010 description) */
#define BLOCK_SIZE 1024
  free_space = (free_space + (SYSTEMVALUE)BLOCK_SIZE - 1) /
    (SYSTEMVALUE)BLOCK_SIZE;
  total_space = (total_space + (SYSTEMVALUE)BLOCK_SIZE - 1) /
    (SYSTEMVALUE)BLOCK_SIZE;

#define DISK_PAGES_LIMIT ((SYSTEMVALUE) 2097251.0)
  if ( free_space > DISK_PAGES_LIMIT ) free_space = DISK_PAGES_LIMIT;
  if ( total_space > DISK_PAGES_LIMIT ) total_space = DISK_PAGES_LIMIT;

  oInteger(inewobj) = (int32)free_space;
  if ( ! push( & inewobj , & operandstack ))
    return FALSE ;

  oInteger(inewobj) = (int32)total_space;
  return push( & inewobj , & operandstack ) ;
}

Bool initializedisk_(ps_context_t *pscontext)
{
  int32 thevals[ 2 ] ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! stack_get_integers(&operandstack, thevals , 2 ))
    return FALSE ;

  if (( thevals[ 0 ] < 0 ) || ( thevals[ 1 ] < 0 ))
    return error_handler( RANGECHECK ) ;

  npop( 2 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            ###diskpercent_()    author:              Andrew Cave
   creation date:       15-Aug-1989          last modification:   ##-###-####
   arguments:           none .
   description:

   Returns the user/system splitup of the disk.

---------------------------------------------------------------------------- */
int32 userdiskpercent = 75 ;
Bool userdiskpercent_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  oInteger(inewobj) = userdiskpercent ;
  return push( & inewobj , & operandstack ) ;
}

Bool setuserdiskpercent_(ps_context_t *pscontext)
{
  int32 dpercent ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  if ( ! stack_get_integers(&operandstack, & dpercent , 1 ))
    return FALSE ;

  if (( dpercent < 0 ) || ( dpercent > 100 ))
    return error_handler( RANGECHECK) ;

  userdiskpercent = dpercent ;
  pop( & operandstack ) ;

  return TRUE ;
}

/* Utility function for some of the above. */
static Bool readchecksetbool(Bool *toset, corecontext_t *corecontext)
{
  register OBJECT *theo ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if (oType(*theo) != OBOOLEAN )
    return error_handler( TYPECHECK) ;

  if ( NOTINEXITSERVER(corecontext))
    return error_handler( INVALIDACCESS ) ;

  (*toset) = oBool(*theo) ;

  pop( & operandstack ) ;
  return TRUE ;
}

static Bool readcheckrangesetint(int32 *toset ,
                                 int32 rangemin ,
                                 int32 rangemax,
                                 corecontext_t *corecontext)
{
  int32 itemp ;

  if ( ! stack_get_integers(&operandstack, & itemp , 1 ))
    return FALSE ;

  if (( itemp < rangemin ) || ( itemp > rangemax ))
    return error_handler( RANGECHECK ) ;

  if ( NOTINEXITSERVER(corecontext))
    return error_handler( INVALIDACCESS ) ;

  (*toset) = itemp ;
  pop( & operandstack ) ;
  return TRUE ;
}

static Bool sccbi( int32 *baud9 ,
                   int32 *option9 ,
                   int32 *baud25 ,
                   int32 *option25 )
{
  register OBJECT *theo ;

  if ( isEmpty( operandstack ))
    return error_handler( STACKUNDERFLOW ) ;

  theo = theTop( operandstack ) ;
  if (oType(*theo) != OINTEGER )
    return error_handler( TYPECHECK) ;

  switch ( oInteger(*theo)) {
  case 0 :
  case 9 :
    oInteger(inewobj) = (*option9) ;
    if ( ! push( & inewobj , & operandstack ))
      return FALSE ;
    oInteger(inewobj) = (*baud9) ;
    Copy( theo , ( & inewobj )) ;
    break ;
  case 1 :
  case 25 :
    oInteger(inewobj) = (*option25) ;
    if ( ! push( & inewobj , & operandstack ))
      return FALSE ;
    oInteger(inewobj) = (*baud25) ;
    Copy( theo , ( & inewobj )) ;
    break ;
  default:
    return error_handler( RANGECHECK) ;
  }
  return TRUE ;
}

static Bool setsccbi(int32 *baud9 ,
                     int32 *option9 ,
                     int32 *baud25 ,
                     int32 *option25,
                     corecontext_t *corecontext)
{
  int32 i ;
  OBJECT *theo ;
  int32 ivals[ 3 ] ;

  if ( theStackSize( operandstack ) < 2 )
    return error_handler( STACKUNDERFLOW ) ;

  for ( i = 0 ; i < 3 ; ++i ) {
    theo = stackindex( i , & operandstack ) ;
    if (oType(*theo) != OINTEGER )
      return error_handler( TYPECHECK ) ;

    ivals[ i ] = oInteger(*theo) ;
  }
  if (( ivals[ 0 ] < 0 ) || ( ivals[ 0 ] > 7 ))
    return error_handler( RANGECHECK ) ;
  if (( ivals[ 1 ] < 0 ) || ( ivals[ 1 ] > 100000 ))
    return error_handler( RANGECHECK ) ;
  switch ( ivals[ 2 ] ) {
  case 0 :
  case 9 :
    if ( ! ivals[ 1 ] )
      if ( ! (*baud25))
        return error_handler( INVALIDACCESS ) ;

    if ( NOTINEXITSERVER(corecontext))
      return error_handler( INVALIDACCESS ) ;

    (*baud9) = ivals[ 1 ] ;
    (*option9) = ivals[ 0 ] ;
    break ;
  case 1 :
  case 25 :
    if ( ! ivals[ 1 ] )
      if ( ! (*baud9))
        return error_handler( INVALIDACCESS ) ;

    if ( NOTINEXITSERVER(corecontext))
      return error_handler( INVALIDACCESS ) ;

    (*baud25) = ivals[ 1 ] ;
    (*option25) = ivals[ 0 ] ;
    break ;
  default:
    return error_handler( RANGECHECK) ;
  }
  npop( 3 , & operandstack ) ;
  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            switchsetting_()     author:              Andrew Cave
   creation date:       15-Aug-1989          last modification:   ##-###-####
   arguments:           none .
   description:

   Returns the user/system splitup of the disk.

---------------------------------------------------------------------------- */
int32 switchsetting = 0 ;
Bool switchsetting_(ps_context_t *pscontext)
{
  UNUSED_PARAM(ps_context_t *, pscontext) ;

  oInteger(inewobj) = switchsetting ;
  return push( & inewobj , & operandstack ) ;
}


/* ----------------------------------------------------------------------------
   function:            filelinenumber_   author:              Luke Tunmer
   creation date:       15-Aug-1991       last modification:   ##-###-####
   arguments:
   description:

   Returns the line number from the filelist argument on the operand stack.
   -file-   -->  line-number  true    % if file is an input file
            -->  false                % if CountLines is false or file is an
                                      % output file
---------------------------------------------------------------------------- */
Bool filelinenumber_(ps_context_t *pscontext)
{
  register int32    ssize ;
  register int32    ln ;
  register OBJECT   *o1 ;
  register FILELIST *flptr ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  o1 = TopStack( operandstack , ssize ) ;
  if ( oType(*o1) != OFILE )
    return error_handler( TYPECHECK ) ;

  flptr = oFile(*o1) ;

  /* dont allow filters here any more (not that they were ever useful), because
     the filter may be dead and it is then invalid to look into its filelist
     structure */
  if (isIFilter(flptr) || !isIInputFile(flptr) ||
      !ps_core_context(pscontext)->systemparams->CountLines) {
    Copy( o1 , &fnewobj ) ;
    return TRUE ;
  }

  ln = theIFileLineNo( flptr ) ;
  if ( ln < 0 )
    ln = - ln - 1 ;
  oInteger(inewobj) = ln ;

  Copy( o1 , &inewobj ) ;

  push( &tnewobj , &operandstack ) ;

  return TRUE ;
}



/* ----------------------------------------------------------------------------
   function:            filename_         author:              Luke Tunmer
   creation date:       15-Aug-1991       last modification:   ##-###-####
   arguments:
   description:

   Returns the name of the file given on the operand stack.

   --file--  string  -->  filename-string true   % if the file is open
                     -->  filename-string false  % otherwise
---------------------------------------------------------------------------- */
Bool filename_(ps_context_t *pscontext)
{
  register int32    slen , flen ;
  register int32    ssize ;
  register OBJECT   *o1 , *o2 , *o3 ;
  register FILELIST *flptr ;
  register DEVICELIST *dev ;
  uint8 *str ;
  int32 closed_or_dead_filter;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( ssize < 1 )
    return error_handler( STACKUNDERFLOW ) ;

  o1 = stackindex( 1, &operandstack ) ;
  if ( oType(*o1) != OFILE )
    return error_handler( TYPECHECK ) ;

  o2 = TopStack( operandstack , ssize ) ;
  if ( oType(*o2) != OSTRING )
    return error_handler( TYPECHECK ) ;
  if ( ! oCanWrite(*o2))
    if ( ! object_access_override(o2) )
      return error_handler( INVALIDACCESS ) ;
  slen = theLen(*o2) ;

  flptr = oFile(*o1) ;
  str = oString(*o2) ;

  /* we can't allow this operation on dead filters, so for consistency don't
     allow it on any closed filter; in this case return an empty string */
  closed_or_dead_filter = isIFilter (flptr) && ! isIOpenFileFilter (o1, flptr);

  dev = theIDeviceList( flptr ) ;

  if (closed_or_dead_filter) {
    flen = 0;
    oString(*o2) = NULL;
  } else if ( dev ) { /* a standard file with a device attatched */
    flen = strlen_int32( (char *)theIDevName( dev )) + theINLen( flptr ) + 2 ;
    if ( slen < ( flen + 1 ))
      return error_handler( RANGECHECK ) ;

    strcpy( (char *)str , "%" ) ;
    strcat( (char *)str , (char *)theIDevName( dev )) ;
    strcat( (char *)str , "%" ) ;
    if ( theINLen( flptr ))
      strcat( (char *)str , (char *)theICList( flptr )) ;
  } else { /* a filter or the statementedit or lineedit files */
    flen = theINLen( flptr ) ;
    if ( slen < flen + 1 )
      return error_handler( RANGECHECK ) ;
    strcpy( (char *)str , (char *)theICList( flptr )) ;
  }
  theLen(*o2) = (uint16)flen ;
  Copy( o1 , o2 ) ;

  o3 = (! closed_or_dead_filter && isIOpenFile( flptr )) ? &tnewobj : &fnewobj ;
  Copy( o2 , o3 ) ;

  return TRUE ;
}

/* ----------------------------------------------------------------------------
   function:            filetype_         author:              Luke Tunmer
   creation date:       15-Aug-1991       last modification:   ##-###-####
   arguments:
   description:

   Returns the file type from the filelist argument on the operand stack.
   -file-   -->  filetype  true       % if file is open
            -->  filetype  false      % otherwise

   filetype = { realfile | editfile | filterfile | stdfile }
---------------------------------------------------------------------------- */
Bool filetype_(ps_context_t *pscontext)
{
  register int32    ssize ;
  register OBJECT   *o1 , *o2 ;
  register FILELIST *flptr ;
  register uint8    *filetype ;
  register uint32   fileflags ;
  int32 filetypelen ;
  int32 file_is_open ;

  UNUSED_PARAM(ps_context_t *, pscontext) ;

  ssize = theStackSize( operandstack ) ;
  if ( EmptyStack( ssize ))
    return error_handler( STACKUNDERFLOW ) ;

  o1 = TopStack( operandstack , ssize ) ;
  if ( oType(*o1) != OFILE )
    return error_handler( TYPECHECK ) ;

  flptr = oFile(*o1) ;
  /* strictly speaking we shouldnt allow beyond here on a dead filter, but once
     a filter always a filter, so let it pass; set the open file boolean ok though */
  file_is_open = isIOpenFileFilter( o1, flptr ) ;

  fileflags = theIFlags( flptr ) ;
  if ( fileflags & STDFILE_FLAG )
  {
    filetype = (uint8*)"stdfile" ;
    filetypelen = 7 ;
  }
  else if ( fileflags & REALFILE_FLAG )
  {
    filetype = (uint8*)"realfile" ;
    filetypelen = 8 ;
  }
  else if ( fileflags & FILTER_FLAG )
  {
    filetype = (uint8*)"filterfile" ;
    filetypelen = 10 ;
  }
  else
  {
    filetype = (uint8*)"editfile" ;
    filetypelen = 8 ;
  }

  oName(nnewobj) = cachename( filetype , filetypelen ) ;

  o2 = &nnewobj ;
  Copy( o1 , o2 ) ;

  o2 = file_is_open ? &tnewobj : &fnewobj ;
  push( o2 , &operandstack ) ;

  return TRUE ;
}

/** File runtime initialisation */
static void init_C_globals_statops(void)
{
  hwareiomode = 0 ;
  swareiomode = 0 ;
  bootsysfile = TRUE ;
  baud9b = 9600 ;
  baud25b = 9600;
  option9b = 0;
  option25b = 0;
  baud9i = 0;
  option9i = 3;
  baud25i = 9600;
  option25i = 0;
  pagetype = 0;
  pstackorder = TRUE;
  hqnlogo = FALSE;
  jobtimeout = ( 2 * 60 * 60 ) ;
  manualtimeout = ( 1 * 60 ) ;
  timeout = 0 ;
  idlenumber = IDLE_NUMBER_DEFAULT ;
  idlestuff = NULL ;
  blink = 1;
  xmargin = 0 ;
  ymargin = 0 ;
  end_timeout = MAXINT32 ;
  sccconfig = FALSE ;
  userdiskpercent = 75 ;
  switchsetting = 0 ;
}

void ps_idlefonts_C_globals(core_init_fns *fns)
{
  init_C_globals_statops() ;
  fns->swstart = ps_idlefonts_swstart ;
}

/* Log stripped */
