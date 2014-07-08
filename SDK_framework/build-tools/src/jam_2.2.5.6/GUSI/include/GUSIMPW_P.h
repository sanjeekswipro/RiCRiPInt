/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIMPW_P.h		-	Internals of MPW runtime architecture
Author	:	Matthias Neeracher
Language	:	MPW C/C++

$Log: include:GUSIMPW_P.h,v $
Revision 1.2  1999/04/23  08:56:59  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.2  1994/12/31  02:14:49  neeri
CatchStdIO

Revision 1.1  1994/08/10  00:38:49  neeri
Sanitized for universal headers.

*********************************************************************/

/* This file defines some internal structures of the MPW C Library. 
   PowerPC GUSI unfortunately needs to access these to cooperate 
	with stdio. As these structures seem to be embedded in the ROM 
	of the PowerMacs, it doesn't seem to be likely that they will 
	change anytime soon. 
*/

/* These should always remain 32 bit quantities */

typedef int 		int32;
typedef long 		long32;
typedef unsigned	unsigned32;

struct MPWIOEntry;

extern "C" {
typedef int32	(*MPWAccessFunction)(char * name, unsigned int cmd, long *arg);
typedef int32	(*MPWIOFunction)(MPWIOEntry * entry);
typedef int32	(*MPWIOCtlFunction)(MPWIOEntry * entry, unsigned int cmd, long *arg);

int32 MPWFAccessGlue(char * name, unsigned int cmd, long *arg);
int32	MPWCloseGlue(MPWIOEntry * entry);
int32	MPWReadGlue(MPWIOEntry * entry);
int32	MPWWriteGlue(MPWIOEntry * entry);
int32	MPWIOCtlGlue(MPWIOEntry * entry, unsigned int cmd, long *arg);
}

struct MPWIOVector {
	OSType				type;
	UniversalProcPtr	faccess;
	UniversalProcPtr	close;
	UniversalProcPtr	read;
	UniversalProcPtr	write;
	UniversalProcPtr	ioctl;
};

struct MPWIOEntry {
	short				flags;	// See open() mode in FCntl.h
	OSErr				error;
	MPWIOVector	*	funcs;
	int32				userData;
	unsigned32		length;
	char *			buffer;
};

struct MPWSeekArg {
	int32		whence;
	long32	offset;
};

extern MPWIOVector GUSIIOGlue;
extern Boolean		 CatchStdIO;

extern "C" {
MPWIOEntry * 	_getIOPort(int32 * fd);
int32				_addDevHandler(
						int32 				slot, 
						OSType				type,
						MPWAccessFunction	faccess,
						MPWIOFunction		close,
						MPWIOFunction		read,
						MPWIOFunction		write,
						MPWIOCtlFunction	ioctl);
}
