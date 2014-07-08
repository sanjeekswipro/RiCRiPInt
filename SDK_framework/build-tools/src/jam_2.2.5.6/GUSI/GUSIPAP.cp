/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIPAP.cp		-	Printer Access Protocol Sockets
Author	:	Matthias Neeracher

	Based on code from 
		Sak Wathanasin <sw@nan.co.uk>
		David A. Holzgang, _Programming the LaserWriter_, Addison-Wesley 1991
		Apple's Q&A stack
	
Language	:	MPW C/C++

$Log: GUSIPAP.cp,v $
Revision 1.2  1999/04/23  08:43:59  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.4  1994/12/30  20:13:59  neeri
New file name dispatch scheme.

Revision 1.3  1994/08/10  00:06:51  neeri
Sanitized for universal headers.

Revision 1.2  1994/05/01  23:30:32  neeri
Enable recvfrom with non-NULL from address.

Revision 1.1  1994/02/25  02:29:50  neeri
Initial revision

Revision 0.4  1993/12/30  00:00:00  neeri
Fiddle with select

Revision 0.3  1993/09/01  00:00:00  neeri
Throw out nonbreaking spaces

Revision 0.2  1993/04/03  00:00:00  neeri
close() has to do shutdown as well.

Revision 0.1  1993/03/01  00:00:00  neeri
Be more clever about handling shutdowns

*********************************************************************/

#include "GUSIFile_P.h"

#include <Resources.h>
#include <AppleTalk.h>
#include <Errors.h>
#include <Folders.h>
#include <PLStringFuncs.h>
#include <TextUtils.h>
#include <Timer.h>

#include <iostream.h>
#include <sys/types.h>

const long NiceDoggie	=	20;		// Rest Hellhound for 20msec
const long MaxPAP			=	4096;		//	Maximum transaction size

class PAPSocket; 							// That's what this file's all about

class PAPID {
	void		GetPAPCode(short vRefNum, long dirID, StringPtr name);
	
	Handle	papCode;
	Handle	papName;	
public:
	PAPID();
	
	~PAPID();
	
	Ptr	Code()			{	return *papCode;	}
	Ptr	Name()			{	return *papName;	}
	operator void *()		{	return papCode;	}
};

struct PAPPB {
	TMTask			timer;
	short				length;
	short				eof;
	short				state;
	PAPSocket *		sock;
};

struct PAPStatusRec { 
	long  	systemStuff;
   char  	statusstr[256];
};
        
class PAPSocket : public Socket	{		
	friend class PAPSocketDomain;	
#if !defined(powerc) && !defined(__powerc) 
	friend pascal void PAPReadTimer();
	friend pascal void PAPWriteTimer();
#endif
	friend pascal void PAPReadHellHound(PAPPB *);
	friend pascal void PAPWriteHellHound(PAPPB *);

	enum {
		opening,
		open,
		closed
	}						status;
	Boolean				nonblocking;
	Boolean				readPending;
	Boolean				writePending;
	Boolean				readShutDown;
	Boolean				writeShutDown;
	short					papRefNum;
	RingBuffer *		rb;
	RingBuffer *		wb;
	long					ourA5;
	PAPID					papID;
	PAPStatusRec		papStatus;
	PAPPB					wpb;
	PAPPB					rpb;
	
					PAPSocket();
					
	virtual 		~PAPSocket();
	
	int			Powerup();
public:
	virtual int	fcntl(unsigned int cmd, int arg);
	virtual int	recvfrom(void * buffer, int buflen, int flags, void * from, int *);
	virtual int sendto(void * buffer, int buflen, int flags, void * to, int);
	virtual int select(Boolean * canRead, Boolean * canWrite, Boolean * exception);
	virtual int	ioctl(unsigned int request, void *argp);
	virtual int shutdown(int how);
};	

class PAPSocketDomain : public FileSocketDomain {
public:
	PAPSocketDomain()	:	FileSocketDomain(AF_PAP, true, false)	{	}
	
	virtual Boolean Yours(const GUSIFileRef & ref, Request request);
	virtual Socket * open(const GUSIFileRef & ref, int oflag);
};

PAPSocketDomain	PAPSockets;

/***************************** PAP glue *****************************/

#if !defined(powerc) && !defined(__powerc)
pascal short PAPOpen(
	short * 			refNum, 
	char * 			printerName,
	short 			flowQuantum, 
	PAPStatusRec * statusBuf, 
	short *			compState,
	Ptr				papCode
)	=	{0x205F, 0x4EA8, 0x0000};
 
pascal short PAPRead(
	short				refNum, 
	char *			buffer,
	short *			length, 
	short *			eof,
	short *			compState,
	Ptr				papCode
)	=	{0x205F, 0x4EA8, 0x0004};

pascal short PAPWrite(
	short		 		refNum,
 	char *			buffer,
 	short				length,
 	short				eof,
 	short *			compState,
	Ptr				papCode
)	=	{0x205F, 0x4EA8, 0x0008};

pascal short PAPStatus(
	char	*			printerName,
 	PAPStatusRec *	statusBuff,
 	AddrBlock *		netAddr,
	Ptr				papCode
)	=	{0x205F, 0x4EA8, 0x000C};

pascal short PAPClose(
 	short				refNum,
	Ptr				papCode
)	=	{0x205F, 0x4EA8, 0x0010};

pascal short PAPUnload(
	Ptr				papCode
)	=	{0x205F, 0x4EA8, 0x0014};
#else
short PAPOpen(
	short * 			refNum, 
	char * 			printerName,
	short 			flowQuantum, 
	PAPStatusRec * statusBuf, 
	short *			compState,
	Ptr				papCode
) {
	return CallUniversalProc(
		(UniversalProcPtr)(papCode + 0),
		kPascalStackBased 
			|	RESULT_SIZE(kTwoByteCode)
			|	STACK_ROUTINE_PARAMETER(1, kFourByteCode)
			|	STACK_ROUTINE_PARAMETER(2, kFourByteCode)
			|	STACK_ROUTINE_PARAMETER(3, kTwoByteCode)
			|	STACK_ROUTINE_PARAMETER(4, kFourByteCode)
			|	STACK_ROUTINE_PARAMETER(5, kFourByteCode),
		refNum, printerName, flowQuantum, statusBuf, compState);
}
 
short PAPRead(
	short				refNum, 
	char *			buffer,
	short *			length, 
	short *			eof,
	short *			compState,
	Ptr				papCode
) {
	return CallUniversalProc(
		(UniversalProcPtr)(papCode + 4),
		kPascalStackBased 
			|	RESULT_SIZE(kTwoByteCode)
			|	STACK_ROUTINE_PARAMETER(1, kTwoByteCode)
			|	STACK_ROUTINE_PARAMETER(2, kFourByteCode)
			|	STACK_ROUTINE_PARAMETER(3, kFourByteCode)
			|	STACK_ROUTINE_PARAMETER(4, kFourByteCode)
			|	STACK_ROUTINE_PARAMETER(5, kFourByteCode),
		refNum, buffer, length, eof, compState);
}

short PAPWrite(
	short		 		refNum,
 	char *			buffer,
 	short				length,
 	short				eof,
 	short *			compState,
	Ptr				papCode
) {
	return CallUniversalProc(
		(UniversalProcPtr)(papCode + 8),
		kPascalStackBased 
			|	RESULT_SIZE(kTwoByteCode)
			|	STACK_ROUTINE_PARAMETER(1, kTwoByteCode)
			|	STACK_ROUTINE_PARAMETER(2, kFourByteCode)
			|	STACK_ROUTINE_PARAMETER(3, kTwoByteCode)
			|	STACK_ROUTINE_PARAMETER(4, kTwoByteCode)
			|	STACK_ROUTINE_PARAMETER(5, kFourByteCode),
		refNum, buffer, length, eof, compState);
}

short PAPStatus(
	char	*			printerName,
 	PAPStatusRec *	statusBuff,
 	AddrBlock *		netAddr,
	Ptr				papCode
) {
	return CallUniversalProc(
		(UniversalProcPtr)(papCode + 12),
		kPascalStackBased 
			|	RESULT_SIZE(kTwoByteCode)
			|	STACK_ROUTINE_PARAMETER(1, kFourByteCode)
			|	STACK_ROUTINE_PARAMETER(2, kFourByteCode)
			|	STACK_ROUTINE_PARAMETER(3, kFourByteCode),
		printerName, statusBuff, netAddr);
}

short PAPClose(
 	short				refNum,
	Ptr				papCode
)	{
	return CallUniversalProc(
		(UniversalProcPtr)(papCode + 16),
		kPascalStackBased 
			|	RESULT_SIZE(kTwoByteCode)
			|	STACK_ROUTINE_PARAMETER(1, kTwoByteCode),
		refNum);
}

short PAPUnload(
	Ptr				papCode
)	{
	return CallUniversalProc(
		(UniversalProcPtr)(papCode + 20),
		kPascalStackBased 
			|	RESULT_SIZE(kTwoByteCode));
}
#endif

/********************* Link stuffing procedures *********************/

#if !defined(powerc) && !defined(__powerc)
#pragma segment GUSIResident

PAPPB * GetPAPInfo() = 0x2009;					// MOVE.L A1,D0

pascal void PAPReadTimer()
{
	PAPPB *	pb		=	GetPAPInfo();
	long		oldA5	=	SetA5(pb->sock->ourA5);
	
	PAPReadHellHound(pb);
	
	SetA5(oldA5);
}

pascal void PAPWriteTimer()
{
	PAPPB *	pb		=	GetPAPInfo();
	long		oldA5	=	SetA5(pb->sock->ourA5);
	
	PAPWriteHellHound(pb);
	
	SetA5(oldA5);
}

#define uPAPReadTimer	PAPReadTimer
#define uPAPWriteTimer	PAPWriteTimer
#endif

pascal void PAPReadHellHound(PAPPB * pb)
{
	if (pb->state > 0) {
		PrimeTime(QElemPtr(pb), NiceDoggie);		// See you again 
		
		return;
	}
	
	if (!pb->sock->rb)									// We're closing
		return;
	
	PAPSocket &		sock	=	*pb->sock;	
	RingBuffer & 	buf 	=	*sock.rb;
	Boolean &		pend	=	sock.readPending;
		
	if (buf.Locked())
		buf.Later(Deferred(PAPReadHellHound), pb);
	else	{
		buf.Later(nil, nil);
		if (pend) {
			pend	=	false;
			
			if (pb->state)	{
				pb->sock->readShutDown	=	true;
				
				return;
			}
			
			buf.Validate(pb->length);
			
			if (pb->eof){
				pb->sock->readShutDown	=	true;
				
				return;
			}
		}
		
		if (!buf.Free()) 
			buf.Later(Deferred(PAPReadHellHound), pb);
		else {
			char *	buffer;
			long		max	=	MaxPAP;
			
			buffer			=	buf.Producer(max);
			pb->length		=	short(max);
			pend				=	true;
			
			PAPRead(
				sock.papRefNum,
				buffer, 
				&pb->length,
				&pb->eof,
				&pb->state,
				sock.papID.Code());
				
			PrimeTime(QElemPtr(pb), NiceDoggie);		// See you again 
		}
	}

}

pascal void PAPWriteHellHound(PAPPB * pb)
{
	if (pb->state > 0) {
		PrimeTime(QElemPtr(pb), NiceDoggie);		// See you again 
		
		return;
	}
	
	if (!pb->sock->wb)									// We're closing
		return;
		
	PAPSocket &		sock	=	*pb->sock;	
	RingBuffer &	buf 	=	*sock.wb;
	Boolean &		pend	=	sock.writePending;
		
	if (buf.Locked())
		buf.Later(Deferred(PAPWriteHellHound), pb);
	else	{
		buf.Later(nil, nil);
		
		if (pend) {
			if (pb->state)	{
				pb->sock->writeShutDown	=	true;
				pb->eof						=	1;
				
				return;
			}

			buf.Invalidate(pb->length);
			pend	=	false;
		}
		
		if (!buf.Valid()) {
			if (pb->sock->writeShutDown && !pb->eof)
				PAPWrite(
					sock.papRefNum,
 					0,
 					0,
 					pb->eof = 1,
 					&pb->state,
					sock.papID.Code());
			else
				buf.Later(Deferred(PAPWriteHellHound), pb);
		} else {
			char * 	buffer;
			long		max	=	MaxPAP;
			
			buffer			=	buf.Consumer(max);
			pb->length		=	short(max);
			pend				=	true;
			
			PAPWrite(
				sock.papRefNum,
 				buffer,
 				pb->length,
 				0,
 				&pb->state,
				sock.papID.Code());
				
			PrimeTime(QElemPtr(pb), NiceDoggie);		// See you again
		}
	}
}

#if defined(powerc) || defined(__powerc)
RoutineDescriptor	uPAPReadTimer =
		BUILD_ROUTINE_DESCRIPTOR(uppTimerProcInfo, PAPReadHellHound);	
RoutineDescriptor	uPAPWriteTimer =
		BUILD_ROUTINE_DESCRIPTOR(uppTimerProcInfo, PAPWriteHellHound);	
#endif

#if !defined(powerc) && !defined(__powerc)
#pragma segment GUSIPAP
#endif

/************************** PAPID members **************************/

void PAPID::GetPAPCode(short vRefNum, long dirID, StringPtr name)
{
	short		res;
	
	res = HOpenResFile(vRefNum, dirID, name, fsRdPerm);
	
	if (res == -1)
		return;
		
	papCode = Get1Resource('PDEF', 10);
	
	if (papCode) {
		DetachResource(papCode);
		MoveHHi(papCode);
		HLock(papCode);
	} else 
		goto done;

	papName = Get1Resource('PAPA', -8192);
	
	if (papName) {
		DetachResource(papName);
		MoveHHi(papName);
		HLock(papName);
	} else {
		DisposeHandle(papCode);
		
		papCode = nil;
	}

done:	
	CloseResFile(res);
}

PAPID::PAPID()
{
	OSErr				err;
	short				saveRes;
	short				prVol;
	long				prDir;
	StringHandle	printer;
		
	papCode	=	nil;
	papName	=	nil;

	if (err = FindFolder(
					kOnSystemDisk, 
					kExtensionFolderType, 
					kDontCreateFolder,
					&prVol,
					&prDir)
	)
		return;
		
	saveRes = CurResFile();
	UseResFile(0);
			
	if (printer = StringHandle(Get1Resource('STR ', -8192))) {
		HLock(Handle(printer));
		
		GetPAPCode(prVol, prDir, *printer);
		
		ReleaseResource(Handle(printer));
	} 

	if (!papCode)
		GetPAPCode(prVol, prDir, (StringPtr) "\pLaserWriter");
	
	UseResFile(saveRes);
}

PAPID::~PAPID()
{
	if (papName)	
		DisposeHandle(papName);
	if (papCode)
		DisposeHandle(papCode);
}

/************************ PAPSocket members ************************/

PAPSocket::PAPSocket()
{
	status			=	PAPSocket::opening;
	nonblocking		=	false;
	rb					=	new RingBuffer(4096);
	wb					=	new RingBuffer(4096);
	readPending		=	false;
	writePending	=	false;
	readShutDown	=	false;
	writeShutDown	=	false;
	ourA5				=	SetCurrentA5();
	
	if (!papID) {
		GUSI_error(ENETDOWN);
		
		return;
	} else if (!rb || !wb) {
		GUSI_error(ENOMEM);
		
		return;
	}
	
	if (PAPOpen(&papRefNum, papID.Name(), 8, &papStatus, &wpb.state, papID.Code()))
		GUSI_error(ENETDOWN);
}

PAPSocket::~PAPSocket()
{
	char dummy;
	
	shutdown(1);												// Got nothing more to say
	while (recvfrom(&dummy, 1, 0, nil, nil) > 0)		// Wait for printer to complete
		;
		
	PAPUnload(papID.Code());

	if (rb)
		delete rb;
	
	if (wb)
		delete wb;
}

int PAPSocket::Powerup()
{
	switch (status) {
	case PAPSocket::opening:
		if (wpb.state > 0 && nonblocking)
			return GUSI_error(EWOULDBLOCK);
			
		SPIN(wpb.state > 0, SP_MISC, 0);
		
		if (wpb.state) {
			status = PAPSocket::closed;
			
			return GUSI_error(ENETDOWN);
		} 
		
		status	=	PAPSocket::open;
				
		rpb.timer.tmAddr		=	TimerUPP(&uPAPReadTimer);
		rpb.timer.tmCount		=	0;
		rpb.timer.tmWakeUp	=	0;
		rpb.timer.tmReserved	=	0;
		rpb.state            =  0;
		rpb.sock					=	this;
		
		wpb.timer.tmAddr		=	TimerUPP(&uPAPWriteTimer);
		wpb.timer.tmCount		=	0;
		wpb.timer.tmWakeUp	=	0;
		wpb.timer.tmReserved	=	0;
		wpb.sock					=	this;
		wpb.eof					=	0;
		
		InsTime((QElem *) &rpb.timer);
		InsTime((QElem *) &wpb.timer);
		
		PAPReadHellHound(&rpb);
		PAPWriteHellHound(&wpb);
	
		return 0;
	case PAPSocket::open:
		return 0;
	case PAPSocket::closed:
		return GUSI_error(ENOTCONN);
	}
}

int PAPSocket::fcntl(unsigned int cmd, int arg)
{
	switch (cmd)	{
	case F_GETFL:
		if (nonblocking)
			return FNDELAY;
		else
			return 0;
	case F_SETFL:
		if (arg & FNDELAY)
			nonblocking = true;
		else
			nonblocking = false;
			
		return 0;
	default:
		return GUSI_error(EOPNOTSUPP);
	}
}

int PAPSocket::ioctl(unsigned int request, void *argp)
{
	switch (request)	{
	case FIONBIO:
		nonblocking	=	(Boolean) *(long *) argp;
		
		return 0;
	case FIONREAD:
		if (Powerup())
			return -1;
			
		if (status != PAPSocket::open)
			return GUSI_error(ENOTCONN);	
	
		*(unsigned long *) argp	= rb->Valid();
		
		return 0;
	default:
		return GUSI_error(EOPNOTSUPP);
	}
}

int PAPSocket::recvfrom(void * buffer, int buflen, int flags, void * from, int * fromlen)
{
	/* This behaviour borders on the pathological, but this is what I currently
	   believe to be the least troublesome behaviour that is still correct.
	*/
	if (from)
		*fromlen = 0;
	if (flags)
		return GUSI_error(EOPNOTSUPP);

	if (Powerup())
		return -1;
		
	if (!rb->Valid())	
		if (readShutDown)
			return 0;
		else if (nonblocking)
			return GUSI_error(EWOULDBLOCK);
		else
			SPIN(!rb->Valid() && !readShutDown, SP_STREAM_READ, 0);
	
	long	len	=	buflen;
	
	rb->Consume(Ptr(buffer), len);
	
	return len;
}

int PAPSocket::sendto(void * buffer, int buflen, int flags, void * to, int)
{
	if (to)
		return GUSI_error(EOPNOTSUPP);
	if (flags)
		return GUSI_error(EOPNOTSUPP);
	
	if (Powerup())
		return -1;
		
	if (writeShutDown)
		return GUSI_error(ESHUTDOWN);
	
	if (!wb->Free())
		if (nonblocking)
			return GUSI_error(EWOULDBLOCK);
		
	long	len	=	buflen;
	long	done	=	0;
	
	for (;;) {
		wb->Produce(Ptr(buffer), len);
		
		done		+=	len;
		
		if (nonblocking)
			break;
		
		buflen	-=	int(len);
		
		if (!buflen)
			break;
		
		buffer 	=	Ptr(buffer) + len;
		len		=	buflen;
		
		SPIN(!wb->Free() && !writeShutDown, SP_STREAM_WRITE, 0);
		
		if (writeShutDown)
			break;
	}
	
	return done;
}

int PAPSocket::shutdown(int how)
{
	if (how < 0 || how > 2)
		return GUSI_error(EINVAL);
	
	if (how) {
		writeShutDown	=	true;
		
		if (status == PAPSocket::open)
			wb->Undefer();							//	Wake up write hellhound
	}
	if (!(how & 1))
		readShutDown	=	true;
		
	return 0;
}

int PAPSocket::select(Boolean * canRead, Boolean * canWrite, Boolean *)
{
	int		goodies 	= 	0;
	
	if (canRead)
		switch (status) {
		case PAPSocket::open:
			if (rb->Valid() || readShutDown) {
				*canRead = true;
				++goodies;
			}
			break;
		case PAPSocket::opening:
			break;
		case PAPSocket::closed:
			*canRead = true;
			++goodies;
			break;
		}
	
	if (canWrite)
		switch (status) {
		case PAPSocket::opening:
			if (!(wpb.state > 0)) {
				*canWrite = true;
				++goodies;
			}
			break;
		case PAPSocket::open:
			if (wb->Free() || writeShutDown) {
				*canWrite = true;
				++goodies;
			}
			break;
		case PAPSocket::closed:
			*canRead = true;
			++goodies;
			break;
		}
		
	return goodies;
}

/********************* PAPSocketDomain member **********************/

extern "C" void GUSIwithPAPSockets()
{
	PAPSockets.DontStrip();
}

Boolean PAPSocketDomain::Yours(const GUSIFileRef & ref, FileSocketDomain::Request request)
{
	return !ref.spec && (request == willOpen) 
		&& equalstring((char *) ref.name, "dev:printer", false, true);
}

Socket * PAPSocketDomain::open(const GUSIFileRef &, int)
{
	Socket *	sock;
	
	errno = 0;
	sock 	= new PAPSocket();
	
	if (sock && errno) {
		delete sock;
		
		return nil;
	} else
		return sock;
}
