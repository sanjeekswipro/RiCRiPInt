/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIPPC.cp		-	PPC Sockets
Author	:	Matthias Neeracher
Language	:	MPW C/C++

$Log: GUSIPPC.cp,v $
Revision 1.2  1999/04/23  08:43:53  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.4  1994/12/30  20:14:30  neeri
Wake up process from completion procedure.

Revision 1.3  1994/08/10  00:06:01  neeri
Sanitized for universal headers.

Revision 1.2  1994/05/01  23:30:32  neeri
Enable recvfrom with non-NULL from address.

Revision 1.1  1994/02/25  02:30:02  neeri
Initial revision

Revision 0.13  1993/12/30  00:00:00  neeri
Fiddle with select()

Revision 0.12  1993/09/01  00:00:00  neeri
Throw out nonbreaking spaces

Revision 0.11  1993/06/20  00:00:00  neeri
Changed sa_constr_ppc

Revision 0.10  1993/02/07  00:00:00  neeri
New configuration technique

Revision 0.9  1992/12/17  00:00:00  neeri
Forgot to clear errno in PPCSocketDomain::socket()

Revision 0.8  1992/12/06  00:00:00  neeri
Check flags

Revision 0.7  1992/09/13  00:00:00  neeri
Always complete write

Revision 0.6  1992/09/07  00:00:00  neeri
Implement ioctl()

Revision 0.5  1992/08/30  00:00:00  neeri
Move hasPPC here

Revision 0.4  1992/08/10  00:00:00  neeri
Correct select()

Revision 0.3  1992/08/03  00:00:00  neeri
Introduce additional buffering

Revision 0.2  1992/08/03  00:00:00  neeri
Approximately correct, except for sync/async

Revision 0.1  1992/08/02  00:00:00  neeri
Put some further work in

*********************************************************************/

#include "GUSIPPC_P.h"

#include <Errors.h>
#include <ADSP.h>
#include <Devices.h>
#include <GestaltEqu.h>
#include <PLStringFuncs.h>
#include <LowMem.h>

class PPCSocket; 							// That's what this file's all about

struct PPCPB {
	PPCParamBlockRec	ppc;
	PPCSocket *			sock;
};

class PPCSocket : public Socket	{		
	friend class PPCSocketDomain;	
	friend pascal void PPCReadHellHound(PPCPB * pb);
	friend pascal void PPCWriteHellHound(PPCPB * pb);

	enum {
		notBound, 
		notOpen,
		isListening,
		isOpen,
		isAccepted}		status;
	Boolean				nonblocking;
	Boolean				readPending;
	Boolean				writePending;
	Boolean				readShutDown;
	Boolean				writeShutDown;
	LocationNameRec	location;
	PPCPortRec			port;
	LocationNameRec	peerLoc;
	PPCPortRec			peerPort;
	PPCPB					pb;
	PPCPB	*				rpb;
	RingBuffer *		rb;
	RingBuffer *		wb;

#if !defined(powerc) && !defined (__powerc)
	Ptr						processA5;	/* Our A5 world */
#endif	
	
					PPCSocket();
					PPCSocket(const PPCSocket & acceptFrom);
					
	virtual 		~PPCSocket();
	
	int			Alloc();
	void			HellHoundsOnMyTrail();
public:
	void			Ready();

	virtual int	bind(void * name, int namelen);
	virtual int getsockname(void * name, int * namelen);
	virtual int getpeername(void *name, int *namelen);
	virtual int	fcntl(unsigned int cmd, int arg);
	virtual int listen(int qlen);
	virtual int connect(void * address, int addrlen);
	virtual Socket * accept(void * address, int * addrlen);
	virtual int recvfrom(void * buffer, int buflen, int flags, void * from, int * fromlen);
	virtual int sendto(void * buffer, int buflen, int flags, void * to, int tolen);
	virtual int shutdown(int how);
	virtual int select(Boolean * canRead, Boolean * canWrite, Boolean * exception);
	virtual int	ioctl(unsigned int request, void *argp);
};	

PPCSocketDomain	PPCSockets;

#if defined(powerc) || defined (__powerc)
inline void PPCSocket::Ready() 
{
	PPCSockets.Ready();
}
#endif	

/************************ PPC Toolbox initialization ************************/

pascal OSErr PPCInit_P()
{
	OSErr		err;
	long		attr;
		
	if (err = Gestalt(gestaltPPCToolboxAttr, &attr))
		return err;
		
	if (!(attr & gestaltPPCSupportsRealTime))
		err = PPCInit();

	return err;
}

Feature hasPPC(PPCInit_P);

/********************* Link stuffing procedures *********************/

#if !defined(powerc) && !defined(__powerc)
#pragma segment GUSIResident

void PPCSocket::Ready() 
{
	long saveA5 = SetA5(long(processA5));
	
	PPCSockets.Ready();
	
	SetA5(saveA5);
}

#pragma segment GUSI
#endif	

#if USESROUTINEDESCRIPTORS
RoutineDescriptor	uPPCReadHellHound = 
		BUILD_ROUTINE_DESCRIPTOR(uppPPCCompProcInfo, PPCReadHellHound);
#else
#define uPPCReadHellHound PPCReadHellHound
#endif

pascal void PPCReadHellHound(PPCPB * pb)
{
	if (!pb->sock->rb)									// We're closing
		return;
		
	RingBuffer & 	buf 	=	*pb->sock->rb;
	PPCReadPBRec & p	 	=	pb->ppc.readParam;
	Boolean &		pend	=	pb->sock->readPending;
	
	if (buf.Locked())
		buf.Later(Deferred(PPCReadHellHound), pb);
	else	{
		pb->sock->Ready();
		
		buf.Later(nil, nil);
		if (pend) {
			pend	=	false;
			
			if (p.ioResult)	{
				pb->sock->readShutDown	=	true;
				
				return;
			}
			
			buf.Validate(p.actualLength);
		}
		
		if (!buf.Free()) 
			buf.Later(Deferred(PPCReadHellHound), pb);
		else {
			long	max	=	1000000;
			
			p.ioCompletion	=	PPCCompUPP(&uPPCReadHellHound);
			p.bufferPtr		=	buf.Producer(max);
			p.bufferLength	=	max;
			pend				=	true;
			
			PPCReadAsync(&p);
		}
	}
}

#if USESROUTINEDESCRIPTORS
RoutineDescriptor	uPPCWriteHellHound = 
		BUILD_ROUTINE_DESCRIPTOR(uppPPCCompProcInfo, PPCWriteHellHound);
#else
#define uPPCWriteHellHound PPCWriteHellHound
#endif

pascal void PPCWriteHellHound(PPCPB * pb)
{
	if (!pb->sock->wb)									// We're closing
		return;
		
	RingBuffer & 	 buf 	=	*pb->sock->wb;
	PPCWritePBRec & p	 	=	pb->ppc.writeParam;
	Boolean &		 pend	=	pb->sock->writePending;
	
	if (buf.Locked())
		buf.Later(Deferred(PPCWriteHellHound), pb);
	else	{
		pb->sock->Ready();
		
		buf.Later(nil, nil);
		
		if (pend) {
			pend	=	false;
			
			if (p.ioResult)	{
				pb->sock->writeShutDown	=	true;
				
				return;
			}

			buf.Invalidate(p.actualLength);
		}
		
		if (!buf.Valid()) 
			buf.Later(Deferred(PPCWriteHellHound), pb);
		else {
			long	max	=	1000000;
			
			p.ioCompletion	=	PPCCompUPP(&uPPCWriteHellHound);
			p.bufferPtr		=	buf.Consumer(max);
			p.bufferLength	=	max;
			p.more			=	false;
			p.userData		=	0;
			p.blockCreator	=	'GU·I';
			p.blockType		=	'GU·I';
			pend				=	true;
			
			PPCWriteAsync(&p);
		}
	}
}

#if !defined(powerc) && !defined(__powerc)
#pragma segment GUSI
#endif

/************************ PPCSocket members ************************/

PPCSocket::PPCSocket()
{
	status			=	PPCSocket::notBound;
	nonblocking		=	false;
	pb.sock			=	this;
	rpb				=	nil;
	rb					=	nil;
	wb					=	nil;
	readPending		=	false;
	writePending	=	false;
	readShutDown	=	false;
	writeShutDown	=	false;

#if !defined(powerc) && !defined (__powerc)
	processA5		= LMGetCurrentA5();
#endif	
}

PPCSocket::PPCSocket(const PPCSocket & acceptFrom)
{
	status			=	PPCSocket::isAccepted;
	nonblocking		=	acceptFrom.nonblocking;
	pb.ppc			=	acceptFrom.pb.ppc;
	pb.sock			=	this;
	rpb				=	nil;
	rb					=	nil;
	wb					=	nil;
	readPending		=	false;
	writePending	=	false;
	readShutDown	=	false;
	writeShutDown	=	false;
	location			=	acceptFrom.location;
	port				=	acceptFrom.port;
	peerLoc			=	acceptFrom.peerLoc;
	peerPort			=	acceptFrom.peerPort;

#if !defined(powerc) && !defined (__powerc)
	processA5		= LMGetCurrentA5();
#endif	
}

PPCSocket::~PPCSocket()
{
	if (rb)	{
		delete rb;
		
		rb = nil;
	}
	
	if (wb)	{
		delete wb;
		
		wb = nil;
	}

	switch (status) {
	case PPCSocket::isAccepted:
		PPCEndSync(&pb.ppc.endParam);
		
		break;										// Don't close the port
	case PPCSocket::isListening:
	case PPCSocket::isOpen:
		PPCEndSync(&pb.ppc.endParam);

		/* Fall through */
	case PPCSocket::notOpen:
		PPCCloseSync(&pb.ppc.closeParam);
		/* Fall through */
	case PPCSocket::notBound:
		break;
	}
}

int PPCSocket::Alloc()
{
	if (!rpb)
		rpb	=	new PPCPB;
	
	if (!rpb)
		goto error;
	
	rpb->sock	=	this;
	
	if (!rb)
		rb	=	new RingBuffer(2048);
	
	if (!rb)	
		goto error;
	if (!*rb)
		goto errRB;
	
	if (!wb)
		wb =	new RingBuffer(2048);
	
	if (!wb)	
		goto errRB;
	if (!*wb)
		goto errWB;
	
	return 0;

errWB:
	delete wb;
	
	wb	=	nil;
errRB:
	delete rb;
	
	rb	=	nil;
error:
	return GUSI_error(ENOMEM);
}

void PPCSocket::HellHoundsOnMyTrail()
{
	rpb->ppc.readParam.sessRefNum		=	pb.ppc.startParam.sessRefNum;
	
	PPCReadHellHound(rpb);
	PPCWriteHellHound(&pb);
}

int PPCSocket::fcntl(unsigned int cmd, int arg)
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

int PPCSocket::ioctl(unsigned int request, void *argp)
{
	switch (request)	{
	case FIONBIO:
		nonblocking	=	(Boolean) *(long *) argp;
		
		return 0;
	case FIONREAD:
		switch(status)	{
		case PPCSocket::isAccepted:
		case PPCSocket::isOpen:
			break;
		default:
			return GUSI_error(ENOTCONN);	
		}
	
		*(unsigned long *) argp	= rb->Valid();
		
		return 0;
	default:
		return GUSI_error(EOPNOTSUPP);
	}
}

int PPCSocket::bind(void *sa_name, int)
{
	struct sockaddr_ppc *	addr = (struct sockaddr_ppc *) sa_name;
	
	if (addr->family != AF_PPC)
		GUSI_error(EAFNOSUPPORT);
		
	if (status != PPCSocket::notBound)
		return GUSI_error(EINVAL);
	
	location = addr->location;
	port		= addr->port;
	
	pb.ppc.openParam.ioCompletion		=	nil;
	pb.ppc.openParam.serviceType		=	ppcServiceRealTime;
	pb.ppc.openParam.resFlag			=	0;
	pb.ppc.openParam.portName			=	&port;
	pb.ppc.openParam.locationName		=	&location;
	pb.ppc.openParam.networkVisible	=	true;
	
	switch (PPCOpenSync(&pb.ppc.openParam))	{
	case noErr:
		break;
	case nameTypeErr:
	case badReqErr:
	case badPortNameErr:
	case badLocNameErr:
		return GUSI_error(EINVAL);
	case noGlobalsErr:
		return GUSI_error(ENOMEM);
	case portNameExistsErr:
	case nbpDuplicate:
		return GUSI_error(EADDRINUSE);
	default:
		return GUSI_error(EFAULT);
	}
	
	status =	PPCSocket::notOpen;
	
	return 0;
}

int PPCSocket::getsockname(void *name, int *namelen)
{
	struct sockaddr_ppc	addr;
	
	addr.family			=	AF_PPC;
	addr.location		=	location;
	addr.port			=	port;
	
	memcpy(name, &addr, *namelen = min(*namelen, int(sizeof(struct sockaddr_ppc))));
	
	return 0;
}

int PPCSocket::getpeername(void *name, int *namelen)
{
	struct sockaddr_ppc	addr;
	
	addr.family			=	AF_PPC;
	addr.location		=	peerLoc;
	addr.port			=	peerPort;
	
	memcpy(name, &addr, *namelen = min(*namelen, int(sizeof(struct sockaddr_ppc))));
	
	return 0;
}

int PPCSocket::listen(int)
{	
	switch (status)	{
	case PPCSocket::notBound:
		return GUSI_error(EINVAL);
	case PPCSocket::isOpen:
	case PPCSocket::isListening:
		return GUSI_error(EISCONN);
	default:
		break;
	}
	
	pb.ppc.informParam.autoAccept		=	true;
	pb.ppc.informParam.portName			= &peerPort;
	pb.ppc.informParam.locationName 	= &peerLoc;
	pb.ppc.informParam.userName			=	nil;
	
	if (PPCInformAsync(&pb.ppc.informParam))
		return GUSI_error(EINVAL);
		
	status = PPCSocket::isListening;
	
	return 0;
}

int PPCSocket::connect(void *sa_name, int)
{
	Boolean 						guest;
	struct sockaddr_ppc *	addr = (struct sockaddr_ppc *) sa_name;
	Str32							uname;
	
	switch (status)	{
	case PPCSocket::notBound:
		return GUSI_error(EINVAL);
	case PPCSocket::isOpen:
	case PPCSocket::isListening:
	case PPCSocket::isAccepted:
		return GUSI_error(EISCONN);
	default:
		break;
	}
	
	if (Alloc())
		return -1;

	if (addr->family != AF_PPC)
		GUSI_error(EAFNOSUPPORT);
	
	peerLoc	= addr->location;
	peerPort	= addr->port;
	uname[0] = 0;
		
	pb.ppc.startParam.serviceType		=	ppcServiceRealTime;
	pb.ppc.startParam.resFlag			=	0;
	pb.ppc.startParam.portName			= &peerPort;
	pb.ppc.startParam.locationName 	= &peerLoc;
	pb.ppc.startParam.userData			=	0;
	
	if (StartSecureSession(&pb.ppc.startParam, uname, true, true, &guest, (StringPtr) "\p"))
		return GUSI_error(EINVAL);
		
	status 									= 	PPCSocket::isOpen;
	
	HellHoundsOnMyTrail();
	
	return 0;
}

Socket * PPCSocket::accept(void * address, int * addrlen)
{
	PPCSocket	*	newsock;
	
	if (status != PPCSocket::isListening)
		return (Socket *) GUSI_error_nil(ENOTCONN);

	if (nonblocking && pb.ppc.informParam.ioResult == 1)
		return (Socket *) GUSI_error_nil(EWOULDBLOCK);
		
	SPINP(pb.ppc.informParam.ioResult == 1, SP_MISC, 0);

	if (pb.ppc.informParam.ioResult)
		return (Socket *) GUSI_error_nil(EFAULT);
	
	newsock	=	new PPCSocket(*this);

	if (!newsock)
		return (Socket *) GUSI_error_nil(ENOMEM);
		
	if (newsock->Alloc())	{
		delete newsock;
		
		return (Socket *) GUSI_error_nil(ENOMEM);
	}
	
	newsock->HellHoundsOnMyTrail();
	                                                
	if (address && addrlen)
		getpeername(address, addrlen);

	pb.ppc.informParam.autoAccept		=	true;
	pb.ppc.informParam.portName		= &peerPort;
	pb.ppc.informParam.locationName 	= &peerLoc;
	pb.ppc.informParam.userName		=	nil;
	
	PPCInformAsync(&pb.ppc.informParam);
		
	return newsock;
}

int PPCSocket::recvfrom(void * buffer, int buflen, int flags, void * from, int * fromlen)
{
	long	len	=	buflen;
	
	if (from)
		getpeername(from, fromlen);
	if (flags)
		return GUSI_error(EOPNOTSUPP);
	
	switch(status)	{
	case PPCSocket::isAccepted:
	case PPCSocket::isOpen:
		break;
	default:
		return GUSI_error(ENOTCONN);	
	}
	
	if (!rb->Valid())	
		if (readShutDown)
			return 0;
		else if (nonblocking)
			return GUSI_error(EWOULDBLOCK);
		else
			SPIN(!rb->Valid(), SP_STREAM_READ, 0);
	
	rb->Consume(Ptr(buffer), len);
	
	return len;
}

int PPCSocket::sendto(void * buffer, int buflen, int flags, void * to, int)
{
	long	len	=	buflen;
	long	done	=	0;
	
	if (to)
		return GUSI_error(EOPNOTSUPP);
	if (flags)
		return GUSI_error(EOPNOTSUPP);
	
	switch(status)	{
	case PPCSocket::isAccepted:
	case PPCSocket::isOpen:
		break;
	default:
		return GUSI_error(ENOTCONN);	
	}
	
	if (writeShutDown)
		return GUSI_error(ESHUTDOWN);
	
	if (!wb->Free())
		if (nonblocking)
			return GUSI_error(EWOULDBLOCK);
		
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
		
		SPIN(!wb->Free(), SP_STREAM_WRITE, 0);
	}
	
	return done;
}

int PPCSocket::shutdown(int how)
{
	if (how < 0 || how > 2)
		return GUSI_error(EINVAL);
	
	if (how)
		writeShutDown	=	true;
	if (!(how & 1))
		readShutDown	=	true;
		
	return 0;
}

int PPCSocket::select(Boolean * canRead, Boolean * canWrite, Boolean *)
{
	int	goodies 	= 	0;
	
	if (canRead)
		switch (status) {
		case PPCSocket::isListening:
			if (pb.ppc.informParam.ioResult != 1) {
				*canRead = 	true;
				++goodies;
			}
			break;
		case PPCSocket::isAccepted:
		case PPCSocket::isOpen:
			if (rb->Valid() || readShutDown) {
				*canRead = 	true;
				++goodies;
			} 
			break;
		default:
			*canRead = 	true;
			++goodies;
			break;
		}
	
	if (canWrite)
		switch (status) {
		case PPCSocket::isAccepted:
		case PPCSocket::isOpen:
			if (wb->Free()) {
				*canWrite = true;
				++goodies;
			}
			break;
		default:
			*canWrite = true;
			++goodies;
			break;
		}
	
	return goodies;
}

/********************* PPCSocketDomain member **********************/

extern "C" void GUSIwithPPCSockets()
{
	PPCSockets.DontStrip();
}

PPCSocketDomain::PPCSocketDomain()
	:	SocketDomain(AF_PPC)	
{
}

Socket * PPCSocketDomain::socket(int type, short)
{
	PPCSocket * sock	=	nil;
	
	errno = 0;
	
	if (!hasPPC)
		GUSI_error(EOPNOTSUPP);
	else 
		switch (type)	{
		case SOCK_STREAM:
			sock = new PPCSocket();
			break;
		default:
			GUSI_error(ESOCKTNOSUPPORT);
		}	
	
	if (sock && errno)	{
		delete sock;
		
		return nil;
	} else
		return sock;
}

static sa_constr_ppc *	CurConstr;

static pascal Boolean GUSIBrowseFilter(LocationNamePtr, PortInfoPtr port)
{
	if (CurConstr->flags & PPC_CON_MATCH_NAME)
		if (PLstrcmp(port->name.name, CurConstr->match.name))
			return false;
	if (CurConstr->flags & PPC_CON_MATCH_TYPE)
		if (port->name.portKindSelector != ppcByString || PLstrcmp(port->name.u.portTypeStr, CurConstr->match.u.portTypeStr))
			return false;
	
	return true;
}

#if USESROUTINEDESCRIPTORS
RoutineDescriptor	uGUSIBrowseFilter = 
		BUILD_ROUTINE_DESCRIPTOR(uppPPCFilterProcInfo, GUSIBrowseFilter);
#else
#define uGUSIBrowseFilter GUSIBrowseFilter
#endif

int PPCSocketDomain::choose(int, char * prompt, void * constraint, int flags, void * name, int * namelen)
{
	sockaddr_ppc			addr;
	char *					end;
	Str255					promp;
	StringPtr				nbp	= nil;
	PortInfoRec				info;
	static sa_constr_ppc constr;
	
	if (flags & (CHOOSE_NEW | CHOOSE_DIR))
		return GUSI_error(EINVAL);
		
	end 		= 	(char *) memccpy(promp+1, prompt, 0, 254);
	promp[0]	=	end-(char *)promp-2;
	CurConstr=	(sa_constr_ppc *) constraint;
	
	if (!CurConstr || !(CurConstr->flags & PPC_CON_NEWSTYLE)) {
		if (CurConstr && ((char *) constraint)[0]) {
			constr.flags	=	PPC_CON_NEWSTYLE + PPC_CON_MATCH_NBP;
			nbp				=	StringPtr(constraint);
		} else
			constr.flags 	=	PPC_CON_NEWSTYLE;
		
		CurConstr	=	&constr;
	} else if (CurConstr->flags & PPC_CON_MATCH_NBP)
		nbp = CurConstr->nbpType;
	
	if (
		PPCBrowser(
			promp, 
			(StringPtr) "\p", 
			false, 
			&addr.location, 
			&info, 
			(CurConstr->flags & (PPC_CON_MATCH_NAME | PPC_CON_MATCH_TYPE)) ? PPCFilterUPP(&uGUSIBrowseFilter) : PPCFilterUPP(nil),
			nbp ? nbp : (StringPtr) "\pPPCToolBox")
	)
		return GUSI_error(EINTR);

	addr.family	=	AF_PPC;
	addr.port	=	info.name;
	
	memcpy(name, &addr, *namelen = min(*namelen, int(sizeof(sockaddr_ppc))));
	
	return 0;
}
