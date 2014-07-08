/*********************************************************************
Project	:	GUSI					-	Grand Unified Socket Interface
File		:	GUSIAppleTalk.cp	-	Appletalk Sockets
Author	:	Matthias Neeracher
Language	:	MPW C/C++

$Log: GUSIAppleTalk.cp,v $
Revision 1.2  1999/04/23  08:44:07  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.4  1994/12/30  19:37:23  neeri
Wake up process on completion to improve performance.

Revision 1.3  1994/08/10  00:31:28  neeri
Sanitized for universal headers.

Revision 1.2  1994/05/01  23:29:28  neeri
Enable recvfrom with non-NULL from address.

Revision 1.1  1994/02/25  02:28:11  neeri
Initial revision

Revision 0.18  1993/12/30  00:00:00  neeri
Fiddle with select()

Revision 0.17  1993/11/17  00:00:00  neeri
Delay opening AppleTalk services

Revision 0.16  1993/09/01  00:00:00  neeri
Throw out nonbreaking spaces

Revision 0.15  1993/02/07  00:00:00  neeri
New configuration technique

Revision 0.14  1993/01/17  00:00:00  neeri
Handle user interrupts more carefully.

Revision 0.13  1992/12/07  00:00:00  neeri
Use flags

Revision 0.12  1992/10/05  00:00:00  neeri
I was a teenage NBP werewolf

Revision 0.11  1992/09/13  00:00:00  neeri
Always complete write

Revision 0.10  1992/09/07  00:00:00  neeri
Implement ioctl()

Revision 0.9  1992/08/10  00:00:00  neeri
Improve select()

Revision 0.8  1992/07/28  00:00:00  neeri
Separate creating symaddrs from registering them

Revision 0.7  1992/07/26  00:00:00  neeri
Error in using memccpy()

Revision 0.6  1992/07/21  00:00:00  neeri
Support symbolic addresses

Revision 0.5  1992/07/13  00:00:00  neeri
Make AppleTalkSockets available to other socket classes.

Revision 0.4  1992/05/18  00:00:00  neeri
Out of band data

Revision 0.3  1992/05/18  00:00:00  neeri
Basic functions work

Revision 0.2  1992/05/12  00:00:00  neeri
NBP stuff

Revision 0.1  1992/05/10  00:00:00  neeri
ADSPStreams

*********************************************************************/

#include "GUSI_P.h"

#include <Errors.h>
#include <ADSP.h>
#include <Devices.h>
#include <GestaltEqu.h>
#include <PLStringFuncs.h>
#include <LowMem.h>

#include <Strings.h>

#include <sys/types.h>

class AtlkSymAddr;

class AppleTalkSocketDomain : public SocketDomain {
	short						dspRefNum;
	
	void						DoMPPOpen();
public:
	AppleTalkSocketDomain();
	
	AddrBlock				node;
	
				short			GetDSP();		
				Boolean		Validate();
	virtual	Socket * 	socket(int type, short protocol);
	virtual int choose(
						int 		type, 
						char * 	prompt, 
						void * 	constraint,		
						int 		flags,
 						void * 	name, 
						int * 	namelen);
};

class AppleTalkSocket : public Socket	{		// That's what this file's all about
	friend class AppleTalkSocketDomain;
protected:
	Boolean			nonblocking;
	Boolean			ownSocket;
	Boolean			readShutDown;
	Boolean			writeShutDown;
	u_char			socket;
	AddrBlock		peer;
	AtlkSymAddr *	symaddr;

					AppleTalkSocket(u_char sock = 0);

	virtual 		~AppleTalkSocket();

#if !defined(powerc) && !defined (__powerc)
	friend pascal void ADSPCompletion68K();
	Ptr			processA5;	/* Our A5 world */
#endif	
public:
	virtual int	bind(void * name, int namelen);
	virtual int getsockname(void * name, int * namelen);
	virtual int getpeername(void *name, int *namelen);
	virtual int	fcntl(unsigned int cmd, int arg);
	virtual int	ioctl(unsigned int request, void *argp);
	void			Ready();
};

struct ADSPSockBuffers {
	enum {qSize	=	4150};

	u_char	attnBuf[attnBufSize];
	u_char	sendBuf[qSize];
	u_char	recvBuf[qSize];
};

typedef ADSPSockBuffers * ADSPBufPtr;

class ADSPSocket;

#if defined(powerc) || defined(__powerc)
#pragma options align=mac68k
#endif

struct AnnotatedADSPParamBlock : public DSPParamBlock {
	AnnotatedADSPParamBlock(ADSPSocket * owner) : sock(owner) { };
	
	ADSPSocket *	sock;
};

#if defined(powerc) || defined(__powerc)
#pragma options align=reset
#endif

class ADSPSocket : public AppleTalkSocket {
	friend class AppleTalkSocketDomain;

					ADSPSocket(u_char sock = 0);

	TRCCB	*		ccb;
	DSPPBPtr		pb;
	ADSPBufPtr	bufs;

	int			Init();
	void			UnInit(Boolean abort);
	Boolean		Waiting();
	Boolean		Up();
public:
	virtual int listen(int qlen);
	virtual int connect(void * address, int addrlen);
	virtual Socket * accept(void * address, int * addrlen);
	virtual int recvfrom(void * buffer, int buflen, int flags, void * from, int * fromlen);
	virtual int sendto(void * buffer, int buflen, int flags, void * to, int tolen);
	virtual int shutdown(int how);
	virtual int	ioctl(unsigned int request, void *argp);
	virtual int select(Boolean * canRead, Boolean * canWrite, Boolean * exception);

	virtual 		~ADSPSocket();
};

class AtlkSymAddr {
	NamesTableEntry * nte;
	Boolean	legit;
public:
	AtlkSymAddr(const EntityName & name);

	~AtlkSymAddr();

	void Register(u_char socket);
};

int AtlkLookup(const EntityName & name, AddrBlock * addr);

AppleTalkSocketDomain	AppleTalkSockets;
const AddrBlock			NoFilter	=	{0, 0, 0};

/************************ AppleTalkSocket members ************************/

AppleTalkSocket::AppleTalkSocket(u_char sock)
{
	socket			=	sock;
	ownSocket		=	!sock;
	nonblocking		=	false;
	readShutDown	=	false;
	writeShutDown	=	false;
	symaddr			=	nil;
	peer				=	NoFilter;

#if !defined(powerc) && !defined (__powerc)
	processA5		= LMGetCurrentA5();
#endif	
}


inline void AppleTalkSocket::Ready() 
{
	AppleTalkSockets.Ready();
}

AppleTalkSocket::~AppleTalkSocket()
{
	if (socket && ownSocket)	{
		MPPParamBlock	mpp;

		mpp.DDP.socket	=	socket;

		PCloseSkt(&mpp, false);
	}

	if (symaddr)
		delete symaddr;
}

int AppleTalkSocket::fcntl(unsigned int cmd, int arg)
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

int AppleTalkSocket::ioctl(unsigned int request, void *argp)
{
	switch (request)	{
	case FIONBIO:
		nonblocking	=	(Boolean) *(long *) argp;

		return 0;
	default:
		return GUSI_error(EOPNOTSUPP);
	}
}

int AppleTalkSocket::bind(void *sa_name, int)
{

	switch (*(short *) sa_name)	{
	case AF_APPLETALK:
		{
			struct sockaddr_atlk *	addr = (struct sockaddr_atlk *) sa_name;

			if (socket || !addr->addr.aSocket)
				return GUSI_error(EINVAL);

			socket		=	addr->addr.aSocket;
			ownSocket	=	false;
		}
		break;
	case ATALK_SYMADDR:
		{
			struct sockaddr_atlk_sym *	addr = (struct sockaddr_atlk_sym *) sa_name;

			symaddr		=	new AtlkSymAddr(addr->name);

			if (errno)	{
				delete symaddr;
				symaddr = nil;

				return -1;
			}
		}
		break;
	default:
		return GUSI_error(EAFNOSUPPORT);
	}

	return 0;
}

int AppleTalkSocket::getsockname(void *name, int *namelen)
{
	struct sockaddr_atlk	addr;

	addr.family			=	AF_APPLETALK;
	addr.addr			=	AppleTalkSockets.node;
	addr.addr.aSocket	=	socket;

	memcpy(name, &addr, *namelen = min(*namelen, int(sizeof(struct sockaddr_atlk))));

	return 0;
}

int AppleTalkSocket::getpeername(void *name, int *namelen)
{
	struct sockaddr_atlk	addr;

	if (!peer.aNet && !peer.aNode && !peer.aSocket)
		return GUSI_error(ENOTCONN);

	addr.family			=	AF_APPLETALK;
	addr.addr			=	peer;

	memcpy(name, &addr, *namelen = min(*namelen, int(sizeof(struct sockaddr_atlk))));

	return 0;
}

/********************* ADSPSocket members *********************/

void ADSPCompletion(AnnotatedADSPParamBlock * pb)
{
	pb->sock->Ready();
}

#if !defined(powerc) && !defined(__powerc)
#pragma segment GUSIResident

AnnotatedADSPParamBlock * GetADSPInfo() = 0x2008;					// MOVE.L A0,D0

pascal void ADSPCompletion68K()
{
	AnnotatedADSPParamBlock *	pb		=	GetADSPInfo();
	long saveA5 							=  SetA5(long(pb->sock->processA5));
	
	ADSPCompletion(pb);
	
	SetA5(saveA5);
}

#define uADSPCompletion	ADSPCompletion68K

#pragma segment GUSI
#else
RoutineDescriptor	uADSPCompletion =
		BUILD_ROUTINE_DESCRIPTOR(uppADSPCompletionProcInfo, ADSPCompletion);	
#endif

ADSPSocket::ADSPSocket(u_char sock)
	:	AppleTalkSocket(sock)
{
	ccb			=	nil;
	pb				=	nil;
	bufs 			=	nil;

	if (!AppleTalkSockets.GetDSP())
		GUSI_error(EPFNOSUPPORT);					// Just an educated guess
}

ADSPSocket::~ADSPSocket()
{
	UnInit(false);
}

inline Boolean ADSPSocket::Waiting()
{
	return pb->ioResult == 1 && !(ccb->userFlags & (eClosed | eTearDown));
}

inline Boolean ADSPSocket::Up()
{
	return !(ccb->userFlags & (eClosed | eTearDown));
}

int ADSPSocket::listen(int)
{
	if (ccb)
		return GUSI_error(EISCONN);

	ccb 	=	new TRCCB;
	if (!ccb)
		goto memErrCCB;

	pb		=	new AnnotatedADSPParamBlock(this);
	if (!pb)
		goto memErrPB;

	pb->ioCRefNum						=	AppleTalkSockets.GetDSP();
	pb->csCode							=	dspCLInit;
	pb->u.initParams.ccbPtr			=	ccb;
	pb->u.initParams.localSocket	=	socket;

	if (PBControlSync(ParmBlkPtr(pb)))
		goto memErr;

	socket		=	pb->u.initParams.localSocket;

	if (symaddr)
		symaddr->Register(socket);

	pb->csCode								=	dspCLListen;
	pb->ioCompletion						=	ADSPCompletionUPP(&uADSPCompletion);
	pb->u.openParams.filterAddress	=	NoFilter;

	PBControlAsync(ParmBlkPtr(pb));

	return 0;

memErr:
	delete pb;
	pb	=	nil;
memErrPB:
	delete ccb;
	ccb	=	nil;
memErrCCB:
	return GUSI_error(ENOMEM);
}

int ADSPSocket::Init()
{
	if (ccb)
		if (pb->csCode == dspOpen && pb->ioResult && pb->ioResult != 1)
			return 0;							// Second chance for lose on open
		else
			return GUSI_error(EISCONN);	// Got a live un', don't reconnect

	ccb 	=	new TRCCB;
	if (!ccb)
		goto memErrCCB;

	pb		=	new AnnotatedADSPParamBlock(this);
	if (!pb)
		goto memErrPB;

	bufs	=	new ADSPSockBuffers;
	if (!bufs)
		goto memErrBufs;

	pb->ioCRefNum						=	AppleTalkSockets.GetDSP();
	pb->csCode							=	dspInit;
	pb->u.initParams.ccbPtr			=	ccb;
	pb->u.initParams.userRoutine	=	nil;
	pb->u.initParams.sendQSize		=	bufs->qSize;
	pb->u.initParams.sendQueue		=	bufs->sendBuf;
	pb->u.initParams.recvQSize		=	bufs->qSize;
	pb->u.initParams.recvQueue		=	bufs->recvBuf;
	pb->u.initParams.attnPtr		=	bufs->attnBuf;
	pb->u.initParams.localSocket	=	socket;

	if (!PBControlSync(ParmBlkPtr(pb)))	{
		socket		=	pb->u.initParams.localSocket;

		if (symaddr)
			symaddr->Register(socket);

		return 0;
	}

	delete bufs;
	bufs	=	nil;
memErrBufs:
	delete pb;
	pb		=	nil;
memErrPB:
	delete ccb;
	ccb	=	nil;
memErrCCB:
	return GUSI_error(ENOMEM);
}

void ADSPSocket::UnInit(Boolean abort)
{
	if (ccb && pb)	{
		pb->csCode					=	bufs ? dspRemove : dspCLRemove;
		pb->u.closeParams.abort	=	abort;

		PBControlSync(ParmBlkPtr(pb));
	}

	if (ccb)
		delete ccb;
	if (pb)
		delete pb;
	if (bufs)
		delete bufs;
	
	ccb	=	nil;
	pb		=	nil;
	bufs	=	nil;
}

int ADSPSocket::connect(void *sa_name, int)
{
	switch (*(short *) sa_name)	{
	case AF_APPLETALK:
		{
			sockaddr_atlk *	addr 	=	(struct sockaddr_atlk *) sa_name;
			peer							=	addr->addr;
		}
		break;
	case ATALK_SYMADDR:
		{
			struct sockaddr_atlk_sym *	addr 	= 	(struct sockaddr_atlk_sym *) sa_name;

			if (AtlkLookup(addr->name, &peer))
				return -1;
		}
		break;
	default:
		return GUSI_error(EAFNOSUPPORT);
	}

	if (Init())
		return -1;

	pb->csCode								=	dspOpen;
	pb->ioCompletion						=	ADSPCompletionUPP(&uADSPCompletion);
	pb->u.openParams.remoteAddress	=	peer;
	pb->u.openParams.filterAddress	=	NoFilter;
	pb->u.openParams.ocMode				=	ocRequest;
	pb->u.openParams.ocInterval		=	0;
	pb->u.openParams.ocMaximum			=	0;

	PBControlAsync(ParmBlkPtr(pb));

	if (nonblocking)
		return GUSI_error(EINPROGRESS);

	SAFESPIN(Waiting(), SP_MISC, 0);

	if (errno)	{
		UnInit(true);
		
		return -1;
	} else if (pb->ioResult == noErr) {
		return 0;
	} else
		return GUSI_error(ECONNREFUSED);
}

Socket * ADSPSocket::accept(void * address, int * addrlen)
{
	ADSPSocket *	newsock;
	sockaddr_atlk	addr;

	if (!pb || pb->csCode != dspCLListen)
		return (Socket *) GUSI_error_nil(ENOTCONN);

	if (nonblocking && pb->ioResult == 1)
		return (Socket *) GUSI_error_nil(EWOULDBLOCK);

	SPINP(Waiting(), SP_MISC, 0);

	if (pb->ioResult)
		return (Socket *) GUSI_error_nil(EFAULT);

	newsock	=	new ADSPSocket(socket);

	if (!newsock)
		return (Socket *) GUSI_error_nil(ENOMEM);
	if (newsock->Init())
		return (Socket *) nil;

	newsock->pb->csCode							=	dspOpen;
	newsock->pb->ioCompletion					=	ADSPCompletionUPP(&uADSPCompletion);
	newsock->pb->u.openParams					=	pb->u.openParams;
	newsock->pb->u.openParams.ocMode			=	ocAccept;
	newsock->pb->u.openParams.ocInterval	=	0;
	newsock->pb->u.openParams.ocMaximum		=	0;
	PBControlAsync(ParmBlkPtr(newsock->pb));

	SAFESPIN(newsock->Waiting(), SP_MISC, 0);

	pb->csCode								=	dspCLListen;
	pb->ioCompletion						=	ADSPCompletionUPP(&uADSPCompletion);
	pb->u.openParams.filterAddress	=	NoFilter;

	PBControlAsync(ParmBlkPtr(pb));

	if (errno || newsock->pb->ioResult)	{
		delete newsock;

		return (Socket *) (errno ? nil : GUSI_error_nil(ECONNREFUSED));
	}

	if (address && addrlen)	{
		addr.family	=	AF_APPLETALK;
		addr.addr	=	pb->u.openParams.remoteAddress;

		memcpy(address, &addr, *addrlen = min(*addrlen, int(sizeof(sockaddr_atlk))));
	}

	return newsock;
}

int ADSPSocket::recvfrom(void * buffer, int buflen, int flags, void * from, int * fromlen)
{
	// To avoid blocking in the ADSP driver, if no data is available, we only read
	// 1 byte and then the rest of the segment that has arrived
	
	int	trylen;
	int 	actlen;
	
	if (from)
		getpeername(from, fromlen);
	if (flags & ~MSG_OOB)
		return GUSI_error(EOPNOTSUPP);
	if (!pb)
		return GUSI_error(ENOTCONN);

	if (pb->csCode == dspOpen && pb->ioResult)	{
		if (pb->ioResult == 1)	{
			if (nonblocking)
				return GUSI_error(EWOULDBLOCK);

			SPIN(Waiting(), SP_MISC, 0);
		}

		if (pb->ioResult)
			return GUSI_error(ECONNREFUSED);
	}

	if (flags & MSG_OOB)
		if (ccb->userFlags & eAttention)	{
			memcpy(Ptr(buffer), ccb->attnPtr, buflen = min(buflen, ccb->attnSize));

			ccb->userFlags ^= eAttention;

			return buflen;
		} else
			return GUSI_error(EINVAL);

	pb->csCode			=	dspStatus;

	PBControlSync(ParmBlkPtr(pb));

	if (pb->u.statusParams.recvQPending)
		trylen = pb->u.statusParams.recvQPending;
	else
		if (nonblocking)
			return GUSI_error(EWOULDBLOCK);
		else if (readShutDown)
			return 0;
		else
			trylen = 1;

	while (!pb->u.statusParams.recvQPending && Up()) {
		SPIN(0, SP_STREAM_READ, 0);
		PBControlSync(ParmBlkPtr(pb));
	}

	pb->csCode					=	dspRead;
	pb->ioCompletion			=	ADSPCompletionUPP(&uADSPCompletion);
	pb->u.ioParams.reqCount	=	min(buflen, trylen);
	pb->u.ioParams.dataPtr	=	(u_char *) buffer;

	PBControlAsync(ParmBlkPtr(pb));
	
	SPIN(Waiting(), SP_STREAM_READ, 0);
	
	actlen = pb->u.ioParams.actCount;

	if (pb->ioResult)
		readShutDown = true;
	else {
		buflen -= actlen;
		
		if (actlen == 1 && buflen) {
			pb->csCode			=	dspStatus;

			PBControlSync(ParmBlkPtr(pb));
			
			if (trylen = pb->u.statusParams.recvQPending) {
				pb->csCode					=	dspRead;
				pb->u.ioParams.reqCount	=	min(buflen, trylen);
				pb->u.ioParams.dataPtr	=	((u_char *) buffer) + actlen;
				
				if (PBControlSync(ParmBlkPtr(pb)))
					readShutDown = true;
				
				actlen += pb->u.ioParams.actCount;
			}
		}
	}

	return actlen;
}

int ADSPSocket::sendto(void * buffer, int buflen, int flags, void * to, int)
{
	if (to)
		return GUSI_error(EOPNOTSUPP);
	if (flags & ~MSG_OOB)
		return GUSI_error(EOPNOTSUPP);
	if (!pb)
		return GUSI_error(ENOTCONN);

	if (pb->csCode == dspOpen && (pb->ioResult || !Up()))	{
		if (Waiting())	{
			if (nonblocking)
				return GUSI_error(EWOULDBLOCK);

			SPIN(Waiting(), SP_MISC, 0);
		}

		if (pb->ioResult)
			return GUSI_error(ECONNREFUSED);
	}

	if (writeShutDown)
			return GUSI_error(ESHUTDOWN);

	if (flags & MSG_OOB)	{
		if (buflen < 0 || buflen > 570)
			return GUSI_error(EINVAL);

		pb->csCode						=	dspAttention;
		pb->u.attnParams.attnCode	=	0;
		pb->u.attnParams.attnSize	=	buflen;
		pb->u.attnParams.attnData	=	(unsigned char *) buffer;

		PBControlSync(ParmBlkPtr(pb));

		if (pb->ioResult)
			return GUSI_error(EINVAL);
		else
			return buflen;
	}

	if (nonblocking)	{
		pb->csCode	=	dspStatus;

		PBControlSync(ParmBlkPtr(pb));

		if (!pb->u.statusParams.sendQFree)
			return GUSI_error(EWOULDBLOCK);
	}

	pb->csCode					=	dspWrite;
	pb->ioCompletion			=	ADSPCompletionUPP(&uADSPCompletion);
	pb->u.ioParams.reqCount	=
		nonblocking
			? 	min(buflen, pb->u.statusParams.sendQFree)
			:	buflen;
	pb->u.ioParams.dataPtr	=	(u_char *) buffer;
	pb->u.ioParams.eom		=	false;
	pb->u.ioParams.flush		=	true;

	PBControlAsync(ParmBlkPtr(pb));

	SPIN(Waiting(), SP_STREAM_WRITE, 0);

	if (pb->ioResult)
		writeShutDown = true;

	return pb->u.ioParams.actCount;
}

int ADSPSocket::shutdown(int how)
{
	if (how < 0 || how > 2)
		return GUSI_error(EINVAL);

	if (how)
		writeShutDown	=	true;
	if (!(how & 1))
		readShutDown	=	true;

	return 0;
}

int ADSPSocket::ioctl(unsigned int request, void *argp)
{
	switch (request)	{
	case FIONREAD:
		if (!pb)
			return GUSI_error(ENOTCONN);

		pb->csCode			=	dspStatus;

		PBControlSync(ParmBlkPtr(pb));

		*(unsigned long *) argp	= pb->u.statusParams.recvQPending;

		return 0;
	default:
		return AppleTalkSocket::ioctl(request, argp);
	}
}

int ADSPSocket::select(Boolean * canRead, Boolean * canWrite, Boolean * exception)
{
	int	goodies = 0;

	if (pb) {
		pb->csCode		=	dspStatus;
		PBControlSync(ParmBlkPtr(pb));
	}
	
	if (canRead)
		if ( !pb || readShutDown || !Up() || pb->u.statusParams.recvQPending
			||	(pb->csCode == dspCLListen && pb->ioResult != 1)
		)	{
			*canRead = true;
			++goodies;
		}

	if (canWrite)
		if ( !pb || writeShutDown || !Up() || pb->u.statusParams.sendQFree != 0
			|| (pb->csCode == dspOpen && pb->ioResult != 1)
		)	{
			*canWrite = true;
			++goodies;
		}
		
	if (exception && (ccb->userFlags & eAttention)) {
		*exception = true;
		++goodies;
	}

	return goodies;
}

/********************* AppleTalkSocketDomain members **********************/

extern "C" void GUSIwithAppleTalkSockets()
{
	AppleTalkSockets.DontStrip();
}

AppleTalkSocketDomain::AppleTalkSocketDomain()
	:	SocketDomain(AF_APPLETALK)
{
	dspRefNum	=	0;
	node.aNet	=	0xFFFF;
	node.aNode	=	0;
	node.aSocket=	0;
}

void AppleTalkSocketDomain::DoMPPOpen()
{
	short	myNode;
	short	myNet;

	if (AppleTalkIdentity(myNet, myNode))	{
		node.aNet	=	0xFFFF;
		node.aNode	=	0;
		node.aSocket=	0;
	} else {
		node.aNet	=	myNet;
		node.aNode	=	(u_char) myNode;
		node.aSocket=	1;
	}
}

Boolean AppleTalkSocketDomain::Validate()
{
	if (!node.aSocket)
		DoMPPOpen();

	return node.aSocket != 0;
}

short	AppleTalkSocketDomain::GetDSP()
{
	if (!dspRefNum)
		if (OpenDriver((StringPtr) "\p.DSP", &dspRefNum))
			dspRefNum	=	0;

	return dspRefNum;
}

Socket * AppleTalkSocketDomain::socket(int type, short)
{
	AppleTalkSocket * sock	=	nil;

	errno	=	0;

	if (!Validate())
		GUSI_error(ENETDOWN);
	else
		switch (type)	{
		case SOCK_STREAM:
			sock = new ADSPSocket();
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

int AppleTalkSocketDomain::choose(int, char * prompt, void * constraint, int flags, void * name, int * namelen)
{
	sa_constr_atlk * 	constr = (sa_constr_atlk *) constraint;
	Point					where;
	NBPReply				reply;
	sockaddr_atlk		addr;
	char *				end;
	Str255				promp;
	NLType				dummy;

	if (!hasStdNBP)
		return GUSI_error(EOPNOTSUPP);

	if (!Validate())
		return GUSI_error(ENETDOWN);

	if (flags & (CHOOSE_NEW | CHOOSE_DIR))
		return GUSI_error(EINVAL);
		
	SetPt(&where, 100, 100);

	memset(&reply.theEntity, 0, sizeof(EntityName));
	memcpy(&reply.theEntity.zoneStr, (StringPtr) "\p*", 2);

	end 		= 	(char *) memccpy(promp+1, prompt, 0, 254);
	promp[0]	=	end-(char *)promp-2;

	if (
		StandardNBP(
			where,
			promp,
			constr ? constr->numTypes : -1,
			constr ? constr->types : dummy,
			NameFilterUPP(nil),
			ZoneFilterUPP(nil),
			DlgHookUPP(nil),
			&reply)
		!= nlOk
	)
		return GUSI_error(EINTR);

	addr.family	=	AF_APPLETALK;
	addr.addr	= 	reply.theAddr;

	memcpy(name, &addr, *namelen = min(*namelen, int(sizeof(sockaddr_atlk))));

	return 0;
}

/*********************** AtlkSymAddr members ************************/

static int EntityLen(const EntityName & name)
{
	Ptr	nm	=	Ptr(&name);
	int	l1	=	*nm+1;
	nm += l1;
	int	l2	=	*nm+1;
	nm += l2;
	int	l3	=	*nm+1;

	return l1+l2+l3;
}

AtlkSymAddr::AtlkSymAddr(const EntityName & name)
{
	int				len	=	EntityLen(name);

	errno = 0;

	if (!AppleTalkSockets.Validate())	{
		GUSI_error(ENETDOWN);

		return;
	}

	nte	=	(NamesTableEntry *) NewPtr(9+len);
	legit	=	false;

	if (!nte)	{
		GUSI_error(ENOMEM);

		return;
	}

	nte->qNext =	nil;
	memcpy(&nte->nt.entityData, &name, len);
}

void AtlkSymAddr::Register(u_char socket)
{
	MPPParamBlock	mpp;

	errno = 0;

	nte->nt.nteAddress.aSocket	=	socket;

	mpp.NBPinterval	=	8;
	mpp.NBPcount		=	3;
	mpp.NBPntQElPtr	=	Ptr(nte);
	mpp.NBPverifyFlag	=	true;
	PRegisterName(&mpp, false);

	if (mpp.MPPioResult)	{
		DisposPtr(Ptr(nte));

		GUSI_error((mpp.MPPioResult == nbpDuplicate) ? EADDRINUSE : EFAULT);
	} else
		legit	=	true;
}

AtlkSymAddr::~AtlkSymAddr()
{
	if (nte)	{
		if (legit) {
			MPPParamBlock	mpp;

			mpp.NBPentityPtr	=	Ptr(&nte->nt.entityData);
			PRemoveName(&mpp, false);
		}

		DisposPtr(Ptr(nte));
	}
}

int AtlkLookup(const EntityName & name, AddrBlock * addr)
{
	EntityName		ent;
	char				found[256];
	MPPParamBlock	mpp;

	mpp.NBPinterval		=	8;
	mpp.NBPcount			=	3;
	mpp.NBPentityPtr		=	Ptr(&name);
	mpp.NBPretBuffPtr		=	found;
	mpp.NBPretBuffSize	=	256;
	mpp.NBPmaxToGet		=	1;
	PLookupName(&mpp, false);
	if (!mpp.MPPioResult)
		NBPExtract(found, mpp.NBPnumGotten, 1, &ent, addr);

	if (mpp.MPPioResult)
		return GUSI_error(EADDRNOTAVAIL);

	return 0;
}
