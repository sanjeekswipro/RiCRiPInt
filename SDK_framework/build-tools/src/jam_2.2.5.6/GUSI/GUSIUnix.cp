/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIUnix.cp		-	Implementation of Unix domain calls
Author	:	Matthias Neeracher
Language	:	MPW C/C++

$Log: GUSIUnix.cp,v $
Revision 1.2  1999/04/23  08:43:50  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.3  1994/12/30  20:20:19  neeri
Add (untested) PowerPC support.

Revision 1.2  1994/08/10  00:00:35  neeri
Sanitized for universal headers.

Revision 1.1  1994/02/25  02:31:11  neeri
Initial revision

Revision 0.13  1993/02/07  00:00:00  neeri
New configuration scheme

Revision 0.12  1993/01/17  00:00:00  neeri
Be more careful about User interrupts

Revision 0.11  1992/11/15  00:00:00  neeri
Rename GUSIFSp_P.h to TFileSpec.h (there we go again)

Revision 0.10  1992/09/13  00:00:00  neeri
Always complete write

Revision 0.9  1992/09/12  00:00:00  neeri
Renamed Paths.h to GUSIFSp_P.h

Revision 0.8  1992/08/10  00:00:00  neeri
select() for accept/connect

Revision 0.7  1992/07/26  00:00:00  neeri
Error in using memccpy()

Revision 0.6  1992/07/13  00:00:00  neeri
In spirit of Unix implementation, use file, not hash table

Revision 0.5  1992/05/21  00:00:00  neeri
Implemented select()

Revision 0.4  1992/04/24  00:00:00  neeri
Introducing UnixStreamSocket, UnixDgramSocket

Revision 0.3  1992/04/20  00:00:00  neeri
C++ rewrite

Revision 0.2  1992/04/18  00:00:00  neeri
On with the show, good health to you

Revision 0.1  1992/03/31  00:00:00  neeri
unix domain socket calls

*********************************************************************/

#include "GUSIFile_P.h"
#include "TFileSpec.h"

#include <Processes.h>
#include <Resources.h>
#include <LowMem.h>
#include <Finder.h>

/*
 * In a fit of hubris, I had assumed that the format of vtables in a C++ 
 * object would remain constant forever, and furthermore hadn't paid 
 * attention to issues of PowerPC compatibility. 
 *
 * To remedy the situation, I have to emulate the original CFront generated 
 * code to some degree.
 */
 
#if defined(powerc) || defined(__powerc)
#define EMULATE_CFRONT
#define EMULATE_VIRTUAL 
#else
#define EMULATE_VIRTUAL virtual
#endif

#if !defined(powerc) && !defined(__powerc)
#pragma segment GUSIUnix
#endif

class UnixSocketDomain : public SocketDomain {
public:
	UnixSocketDomain()	:	SocketDomain(AF_UNIX)	{		}

	virtual Socket * 	socket(int type, short protocol);
	virtual int 		choose(
								int 		type, 
								char * 	prompt, 
								void * 	constraint,		
								int 		flags,
 								void * 	name, 
								int * 	namelen);
};

static UnixSocketDomain	UnixSockets;

class UnixSocket;									// That's what this file's all about
class UnixStreamSocket;
class UnixDgramSocket;

#if defined(powerc) || defined (__powerc)
#pragma options align=mac68k
#endif
struct UnixSocketAddr : TFileSpec {
	Boolean		valid;
	Boolean		owned;
	
	UnixSocketAddr();
	UnixSocketAddr(TFileSpec spec);
};
#if defined(powerc) || defined (__powerc)
#pragma options align=reset
#endif

// The interface of this class should never be changed now the first version 
// of GUSI is released. All further changes should be done by making a subclass.
// The Version() method must return *increasing* version numbers.

// Version 2 adds support for aborting a connection, see below

#if defined(powerc) || defined (__powerc)
#pragma options align=mac68k
#endif
#ifdef EMULATE_CFRONT
class UnixChannel {
#else
class UnixChannel : public SingleObject {
#endif
	UnixSocketAddr	address;
	
	void UnBind();
protected:
	UnixSocket * 	sock;
public:
	UnixChannel	*	nextListener;
	int				errno;
#ifdef EMULATE_CFRONT
	UniversalProcPtr * emulated_vtable;
#endif
	
	UnixChannel(UnixSocket * owner);
	EMULATE_VIRTUAL ~UnixChannel();
	
	EMULATE_VIRTUAL int			Version();
	
	EMULATE_VIRTUAL Boolean	Bind(UnixSocketAddr & addr);
	EMULATE_VIRTUAL Boolean	Connect(UnixChannel * ch);
	EMULATE_VIRTUAL Boolean	Accept(UnixChannel * ch);

	EMULATE_VIRTUAL int		Read(void * buffer, int len);
	EMULATE_VIRTUAL int		Write(void * buffer, int len);
	EMULATE_VIRTUAL int		Send(UnixChannel * from, void * buffer, int len);
	EMULATE_VIRTUAL int 		ReadAvail();
	EMULATE_VIRTUAL int		WriteAvail();
	EMULATE_VIRTUAL int		BufSize();
	EMULATE_VIRTUAL void 	DiscardRead(int len);
	EMULATE_VIRTUAL void 	ShutDown(int how);
	EMULATE_VIRTUAL void		Disconnect();
	
	EMULATE_VIRTUAL int		GUSI_error(int err);

	EMULATE_VIRTUAL UnixSocketAddr & 
							Address();
};

class UnixChannel2 : public UnixChannel {
public:
	UnixChannel2(UnixSocket * owner);

#ifndef EMULATE_CFRONT
	virtual int			Version();
#endif
	EMULATE_VIRTUAL void		Orphan();
	EMULATE_VIRTUAL void		AbortConnect(UnixChannel * ch);
};
#if defined(powerc) || defined (__powerc)
#pragma options align=reset
#endif


#ifdef EMULATE_CFRONT
class UnixChannel;

void Delete_UnixChannel(UnixChannel * chan)
{
	delete chan;
}

enum {      
	piDelete = 
		kCStackBased 
		| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
};

RoutineDescriptor rdDelete = BUILD_ROUTINE_DESCRIPTOR(piDelete, Delete_UnixChannel);

int Version_UnixChannel(UnixChannel * chan)
{
	return chan->Version();
}

enum {      
	piVersion = 
		kCStackBased 
			| RESULT_SIZE(kFourByteCode) 
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
};
			
RoutineDescriptor rdVersion = BUILD_ROUTINE_DESCRIPTOR(piVersion, Version_UnixChannel);

Boolean Bind_UnixChannel(UnixChannel * chan, UnixSocketAddr & addr)
{
	return chan->Bind(addr);
}

enum {      
	piBind = 
		kCStackBased 
			| RESULT_SIZE(kOneByteCode) 
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
};
			
RoutineDescriptor rdBind = BUILD_ROUTINE_DESCRIPTOR(piBind, Bind_UnixChannel);

Boolean Connect_UnixChannel(UnixChannel * chan, UnixChannel * ch)
{
	return chan->Connect(ch);
}

enum {      
	piConnect =
		kCStackBased 
			| RESULT_SIZE(kOneByteCode) 
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
};
			
RoutineDescriptor rdConnect = BUILD_ROUTINE_DESCRIPTOR(piConnect, Connect_UnixChannel);

Boolean Accept_UnixChannel(UnixChannel * chan, UnixChannel * ch)
{
	return chan->Accept(ch);
}

enum {      
	piAccept =
		kCStackBased 
			| RESULT_SIZE(kOneByteCode) 
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
};
			
RoutineDescriptor rdAccept = BUILD_ROUTINE_DESCRIPTOR(piAccept, Accept_UnixChannel);

int Read_UnixChannel(UnixChannel * chan, void * buffer, int len)
{
	return chan->Read(buffer, len);
}

enum {      
	piRead =
		kCStackBased 
			| RESULT_SIZE(kFourByteCode) 
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(3, kFourByteCode)
};
			
RoutineDescriptor rdRead = BUILD_ROUTINE_DESCRIPTOR(piRead, Read_UnixChannel);

int Write_UnixChannel(UnixChannel * chan, void * buffer, int len)
{
	return chan->Write(buffer, len);
}

enum {      
	piWrite = 
		kCStackBased 
			| RESULT_SIZE(kFourByteCode) 
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(3, kFourByteCode)
};
			
RoutineDescriptor rdWrite = BUILD_ROUTINE_DESCRIPTOR(piWrite, Write_UnixChannel);

int Send_UnixChannel(UnixChannel * chan, UnixChannel * from, void * buffer, int len)
{
	return chan->Send(from, buffer, len);
}

enum {      
	piSend = 
		kCStackBased 
			| RESULT_SIZE(kFourByteCode) 
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(3, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(4, kFourByteCode)
};
			
RoutineDescriptor rdSend = BUILD_ROUTINE_DESCRIPTOR(piSend, Send_UnixChannel);

int ReadAvail_UnixChannel(UnixChannel * chan)
{
	return chan->ReadAvail();
}

enum {      
	piReadAvail = 
		kCStackBased 
			| RESULT_SIZE(kFourByteCode) 
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
};
			
RoutineDescriptor rdReadAvail = BUILD_ROUTINE_DESCRIPTOR(piReadAvail, ReadAvail_UnixChannel);

int WriteAvail_UnixChannel(UnixChannel * chan)
{
	return chan->WriteAvail();
}

enum {      
	piWriteAvail = 
		kCStackBased 
			| RESULT_SIZE(kFourByteCode) 
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
};
			
RoutineDescriptor rdWriteAvail = BUILD_ROUTINE_DESCRIPTOR(piWriteAvail, WriteAvail_UnixChannel);

int BufSize_UnixChannel(UnixChannel * chan)
{
	return chan->BufSize();
}

enum {      
	piBufSize = 
		kCStackBased 
			| RESULT_SIZE(kFourByteCode) 
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
};
			
RoutineDescriptor rdBufSize = BUILD_ROUTINE_DESCRIPTOR(piBufSize, BufSize_UnixChannel);

void DiscardRead_UnixChannel(UnixChannel * chan, int len)
{
	chan->DiscardRead(len);
}

enum {      
	piDiscardRead = 
		kCStackBased 
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
};
			
RoutineDescriptor rdDiscardRead = BUILD_ROUTINE_DESCRIPTOR(piDiscardRead, DiscardRead_UnixChannel);

void ShutDown_UnixChannel(UnixChannel * chan, int how)
{
	chan->ShutDown(how);
}

enum {      
	piShutDown = 
		kCStackBased 
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
};
			
RoutineDescriptor rdShutDown = BUILD_ROUTINE_DESCRIPTOR(piShutDown, ShutDown_UnixChannel);

void Disconnect_UnixChannel(UnixChannel * chan)
{
	chan->Disconnect();
}

enum {      
	piDisconnect = 
		kCStackBased 
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
};
			
RoutineDescriptor rdDisconnect = BUILD_ROUTINE_DESCRIPTOR(piDisconnect, Disconnect_UnixChannel);

int GUSI_error_UnixChannel(UnixChannel * chan, int err)
{
	return chan->GUSI_error(err);
}

enum {      
	piGUSI_error = 
		kCStackBased 
			| RESULT_SIZE(kFourByteCode) 
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
};
			
RoutineDescriptor rdGUSI_error = BUILD_ROUTINE_DESCRIPTOR(piGUSI_error, GUSI_error_UnixChannel);

UnixSocketAddr & Address_UnixChannel(UnixChannel * chan)
{
	return chan->Address();
}

enum {      
	piAddress = 
		kCStackBased 
			| RESULT_SIZE(kFourByteCode) 
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
};
			
RoutineDescriptor rdAddress = BUILD_ROUTINE_DESCRIPTOR(piAddress, Address_UnixChannel);

void Orphan_UnixChannel(UnixChannel2 * chan)
{
	chan->Orphan();
}

enum {      
	piOrphan =
		kCStackBased 
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
};
			
RoutineDescriptor rdOrphan = BUILD_ROUTINE_DESCRIPTOR(piOrphan, Orphan_UnixChannel);

void AbortConnect_UnixChannel(UnixChannel2 * chan, UnixChannel * ch)
{
	chan->AbortConnect(ch);
}

enum {      
	piAbortConnect =
		kCStackBased 
			| STACK_ROUTINE_PARAMETER(1, kFourByteCode)
			| STACK_ROUTINE_PARAMETER(2, kFourByteCode)
};
			
RoutineDescriptor rdAbortConnect = BUILD_ROUTINE_DESCRIPTOR(piAbortConnect, AbortConnect_UnixChannel);

UniversalProcPtr UnixChannel_EmulatedVTable[] = {
	0,
	&rdDelete,
	&rdVersion,
	&rdBind,
	&rdConnect,
	&rdAccept,
	&rdRead,
	&rdWrite,
	&rdSend,
	&rdReadAvail,
	&rdWriteAvail,
	&rdBufSize,
	&rdDiscardRead,
	&rdShutDown,
	&rdDisconnect,
	&rdGUSI_error,
	&rdAddress,
	&rdOrphan,
	&rdAbortConnect,
	0,
};
#endif

#ifdef EMULATE_CFRONT
#define CUP							CallUniversalProc
#define EMUL_Delete(p) CUP(p->emulated_vtable[1], piDelete, p)
#define EMUL_Version(p) (int) CUP(p->emulated_vtable[2], piVersion, p)
#define EMUL_Bind(p, x)	(Boolean) CUP(p->emulated_vtable[3], piBind, p, x)
#define EMUL_Connect(p, x) (Boolean) CUP(p->emulated_vtable[4], piConnect, p, x)
#define EMUL_Accept(p, x) (Boolean) CUP(p->emulated_vtable[5], piAccept, p, x)
#define EMUL_Read(p, x, y) (int) CUP(p->emulated_vtable[6], piRead, p, x, y)
#define EMUL_Write(p, x, y) (int) CUP(p->emulated_vtable[7], piWrite, p, x, y)
#define EMUL_Send(p, x, y, z) (int) CUP(p->emulated_vtable[8], piSend, p, x, y, z)
#define EMUL_ReadAvail(p) (int) CUP(p->emulated_vtable[9], piReadAvail, p)
#define EMUL_WriteAvail(p) (int) CUP(p->emulated_vtable[10], piWriteAvail, p)
#define EMUL_BufSize(p) (int) CUP(p->emulated_vtable[11], piBufSize, p)
#define EMUL_DiscardRead(p, x) CUP(p->emulated_vtable[12], piDiscardRead, p, x)
#define EMUL_ShutDown(p, x) CUP(p->emulated_vtable[13], piShutdown, p, x)
#define EMUL_Disconnect(p) CUP(p->emulated_vtable[14], piDisconnect, p)
#define EMUL_GUSI_error(p, x) (int) CUP(p->emulated_vtable[15], piGUSI_error, p, x)
#define EMUL_Address(p) ((UnixSocketAddr &) CUP(p->emulated_vtable[16], piAddress, p))
#define EMUL_Orphan(p) CUP(p->emulated_vtable[17], piOrphan, p)
#define EMUL_AbortConnect(p, x) CUP(p->emulated_vtable[18], piAbortConnect, p, x)
#else
#define EMUL_Delete(p) delete p
#define EMUL_Version(p) p->Version()
#define EMUL_Bind(p, x) p->Bind(x)
#define EMUL_Connect(p, x) p->Connect(x)
#define EMUL_Accept(p, x) p->Accept(x)
#define EMUL_Read(p, x, y) p->Read(x, y)
#define EMUL_Write(p, x, y) p->Write(x, y)
#define EMUL_Send(p, x, y, z) p->Send(x, y, z)
#define EMUL_ReadAvail(p) p->ReadAvail()
#define EMUL_WriteAvail(p) p->WriteAvail()
#define EMUL_BufSize(p) p->BufSize()
#define EMUL_DiscardRead(p, x) p->DiscardRead(x)
#define EMUL_ShutDown(p, x) p->ShutDown(x)
#define EMUL_Disconnect(p) p->Disconnect()
#define EMUL_GUSI_error(p, x) p->GUSI_error(x)
#define EMUL_Address(p) p->Address()
#define EMUL_Orphan(p) ((UnixChannel2 *)p)->Orphan()
#define EMUL_AbortConnect(p, x) ((UnixChannel2 *)p)->AbortConnect(x)
#endif

class UnixSocket : public Socket {
	friend class UnixChannel;
	friend class UnixChannel2;
protected:
	char				status;
	char				state;
	Boolean			nonblocking;
	char				protocol;
	Ptr				readBuf;
	short				readBufSize;
	short				readPos;
	short 			validBytes;
	short				curListener;
	short				maxListener;
	UnixChannel *	chan;
	UnixChannel	*	peer;
	UnixChannel *	firstListener;
	UnixChannel *	lastListener;

					UnixSocket(short prot);
	void			defaultbind();
public:
	enum {channelUnknown, channelAncient, channelSupportsConnAbort};
	
	virtual int	bind(void * name, int namelen);
	virtual int getsockname(void * name, int * namelen);
	virtual int getpeername(void * name, int * namelen);
	virtual int	fcntl(unsigned int cmd, int arg);
	virtual int	ioctl(unsigned int request, void *argp);
	virtual int shutdown(int how);
	virtual int select(Boolean * canRead, Boolean * canWrite, Boolean * exception);
	virtual 		~UnixSocket();
};

class UnixStreamSocket : public UnixSocket {
	friend class UnixSocketDomain;
	
					UnixStreamSocket();
public:
	virtual int listen(int qlen);
	virtual int connect(void * address, int addrlen);
	virtual Socket * accept(void * address, int * addrlen);
	virtual int recvfrom(void * buffer, int buflen, int flags, void * from, int * fromlen);
	virtual int sendto(void * buffer, int buflen, int flags, void * to, int tolen);
	
	virtual 		~UnixStreamSocket();
};

class UnixDgramSocket : public UnixSocket {
	friend class UnixSocketDomain;
	
					UnixDgramSocket();
public:
	virtual int connect(void * address, int addrlen);
	virtual int recvfrom(void * buffer, int buflen, int flags, void * from, int * fromlen);
	virtual int sendto(void * buffer, int buflen, int flags, void * to, int tolen);
	
	virtual 		~UnixDgramSocket();
};

struct UnixSocketID {
	UnixChannel	*			chan;
	
	AddrBlock				machine;
	ProcessSerialNumber	process;
	
				UnixSocketID()										{}
				UnixSocketID(UnixChannel * ch);
	Boolean	Validate();
};

/********************** UnixSocketAddr members ***********************/

inline UnixSocketAddr::UnixSocketAddr() 
	: valid(false), owned(true)			
{
}

inline UnixSocketAddr::UnixSocketAddr(TFileSpec spec) 
	: TFileSpec(spec), valid(false), owned(true)			
{
}

/************************ UnixSocket members ************************/

static UnixSocketAddr * CanonizeName(void * name, int len);
static void 				UncanonizeName(UnixSocketAddr & name, void * addr, int * addrlen);
static UnixChannel * 	LookupName(UnixSocketAddr name);

UnixSocket::UnixSocket(short prot)
{
	GUSI_error(0);
	
	readBufSize		=	DEFAULT_BUFFER_SIZE;
	status			=	SOCK_STATUS_USED;
	state				=	SOCK_STATE_UNCONNECTED;
	nonblocking		=	false;
	protocol			=	prot;
	readPos			=	0;
	validBytes		=	0;
	curListener		=	0;
	maxListener		=	0;
	firstListener	=	nil;
	lastListener	=	nil;
	chan				=	nil;
	peer				=	nil;
	readBuf			=	NewPtr(readBufSize);
	
	if (!readBuf)
		GUSI_error(ENOMEM);
}

UnixSocket::~UnixSocket()
{
	if (readBuf)
		DisposPtr(readBuf);
	
	if (protocol == SOCK_STREAM)
		if (peer)
			EMUL_Disconnect(peer);
		else 
			while (firstListener) {
				EMUL_Disconnect(firstListener);
				
				firstListener = firstListener->nextListener;
			}
		
	if (chan)
		delete chan;
}

void UnixSocket::defaultbind()
{
	struct sockaddr_un	addr;
	
	addr.sun_family	=	AF_UNIX;
	tmpnam(addr.sun_path);
	
	bind(&addr, strlen(addr.sun_path)+2);
}

int UnixSocket::fcntl(unsigned int cmd, int arg)
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

int UnixSocket::ioctl(unsigned int request, void *argp)
{
	switch (request)	{
	case FIONBIO:
		nonblocking	=	(Boolean) *(long *) argp;
		
		return 0;
	case FIONREAD:
		if (chan)
			*(long *) argp = EMUL_ReadAvail(chan);
		else
			*(long *) argp	= 0;

		return 0;
	default:
		return GUSI_error(EOPNOTSUPP);
	}
}

int UnixSocket::bind(void *sa_name, int sa_namelen)
{
	UnixSocketAddr *	addr;
	
	if (chan && chan->Address().valid)
			return GUSI_error(EINVAL);
	
	addr	=	CanonizeName(sa_name, sa_namelen);
	
	if (!addr)
		return -1;
	
	if (LookupName(*addr))
		return GUSI_error(EADDRINUSE);

	if (!chan && !(chan = new UnixChannel2(this)))
		return GUSI_error(ENOMEM);
	
	if (!chan->Bind(*addr))
		return GUSI_error(ENOMEM);
	
	return 0;
}

int UnixSocket::getsockname(void *name, int *namelen)
{
	UncanonizeName(chan->Address(), name, namelen);
	
	return 0;
}

int UnixSocket::getpeername(void *name, int *namelen)
{
	if (!peer)
		return GUSI_error(ENOTCONN);
		
	UncanonizeName(EMUL_Address(peer), name, namelen);
	
	return 0;
}

int UnixSocket::select(Boolean * canRead, Boolean * canWrite, Boolean *)
{
	int	goodies = 0;
	
	if (canRead)
		if ((state == SOCK_STATE_LIS_CON) || !chan || chan->ReadAvail())	{
			*canRead	= 	true;
			++goodies;
		} 
	
	if (canWrite)
		if (!peer || EMUL_WriteAvail(peer))	{
			*canWrite	= 	true;
			++goodies;
		} 
	
	return goodies;
}

int UnixSocket::shutdown(int how)
{
	if (!chan)
		return GUSI_error(ENOTCONN);

	chan->ShutDown(how);
	
	return 0;
}

/********************* UnixStreamSocket members *********************/

UnixStreamSocket::UnixStreamSocket()
	:	UnixSocket(SOCK_STREAM)
{
}

UnixStreamSocket::~UnixStreamSocket()
{
}

int UnixStreamSocket::listen(int qlen)
{
	if (state != SOCK_STATE_UNCONNECTED)
		return GUSI_error(EISCONN);
		
	state			=	SOCK_STATE_LISTENING;
	maxListener	=	qlen;
	
	return 0;
}

int UnixStreamSocket::connect(void *sa_name, int sa_namelen)
{
	UnixSocketAddr *	addr;
	UnixChannel    *	other;
	
	if (peer)
		return GUSI_error(EISCONN);
	
	addr	=	CanonizeName(sa_name, sa_namelen);
	
	if (!addr)
		return -1;
	
	other = LookupName(*addr);
	
	if (!other)
		return GUSI_error(EADDRNOTAVAIL);

	if (!chan && !(chan = new UnixChannel2(this)))
		return GUSI_error(ENOMEM);
	
	if (!chan->Address().valid)
		defaultbind();
	
	if (!EMUL_Connect(other, chan))
		return GUSI_error(ECONNREFUSED);
		
	state	=	SOCK_STATE_CONNECTING;
	
	if (nonblocking)
		return GUSI_error(EINPROGRESS);
		
	SAFESPIN(state == SOCK_STATE_CONNECTING, SP_MISC, 0);
	
	if (state == SOCK_STATE_CONNECTED)
		return 0;
	else if (!errno)
		return GUSI_error(ECONNREFUSED);
		
	// User abort, a tricky situation. What we do depends on the peer channel
	// version.
	
	if (EMUL_Version(other) >= channelSupportsConnAbort)	// The new way
		EMUL_AbortConnect(other, chan);	// This type cast is safe
	else																	// The old way
		((UnixChannel2 *) other)->UnixChannel2::Orphan();	// This type cast is safe			
	
	return -1;
}

Socket * UnixStreamSocket::accept(void * address, int * addrlen)
{
	UnixStreamSocket *	newsock;
	
	if (state != SOCK_STATE_LISTENING && state != SOCK_STATE_LIS_CON)
		return (Socket *) GUSI_error_nil(ENOTCONN);

restart:	
	if (!curListener)
		if (nonblocking)
			return (Socket *) GUSI_error_nil(EWOULDBLOCK);
		else
			SPINP(state == SOCK_STATE_LISTENING, SP_MISC, 0);
			
	if (!curListener)
			return (Socket *) GUSI_error_nil(EFAULT);					// I *really* hope this won't happen 

	newsock	=	new UnixStreamSocket;
	
	if (!newsock)
		return (Socket *) GUSI_error_nil(ENOMEM);
	
	newsock->state			=	SOCK_STATE_CONNECTED;
	newsock->peer			=	firstListener;
	newsock->chan			=	new UnixChannel2(newsock);
	
	if (!newsock->chan)	{
		delete newsock;
		
		return (Socket *) GUSI_error_nil(ENOMEM);
	}
	
	newsock->chan->Address()			=	chan->Address();
	newsock->chan->Address().owned	=	false;
	
	firstListener	=	firstListener->nextListener;
	
	if (!firstListener)
		lastListener = nil;
	
	if (!--curListener)
		state = SOCK_STATE_LISTENING;
	
	if (EMUL_Accept(newsock->peer, newsock->chan) == -1)	{
		newsock->peer = nil;
		
		delete newsock;
		
		goto restart;
	}

	UncanonizeName(EMUL_Address(newsock->peer), address, addrlen);
	
	return newsock;
}

int UnixStreamSocket::recvfrom(void * buffer, int buflen, int flags, void * from, int *)
{
	int				avail;
	
	if (flags || from)
		return GUSI_error(EOPNOTSUPP);
	if (!chan || !peer)
		return GUSI_error(ENOTCONN);	
		
	if ((avail = chan->ReadAvail()) == -1)
		if (chan->errno == ESHUTDOWN)
			return 0;
		else
			return GUSI_error(chan->errno);
	
	if (nonblocking && !avail)
		return GUSI_error(EWOULDBLOCK);
	
	errno	=	0;
	SPIN(!(avail = chan->Read(buffer, buflen)), SP_STREAM_READ, 0);
	if (avail == -1 && !errno)
		if (chan->errno == ESHUTDOWN)
			return 0;
		else
			GUSI_error(chan->errno);
			
	return avail;
}

int UnixStreamSocket::sendto(void * buffer, int buflen, int flags, void * to, int)
{
	int 	avail;
	int	done;
	
	if (flags || to)
		return GUSI_error(EOPNOTSUPP);
	if (!chan || !peer)
		return GUSI_error(ENOTCONN);	
		
	if ((avail = EMUL_WriteAvail(peer)) == -1)
		return GUSI_error(peer->errno);
	
	if (nonblocking && !avail)
		return GUSI_error(EWOULDBLOCK);
	
	for (
		done = errno = 0; 
		!errno && buflen; 
		buflen -= avail, buffer = Ptr(buffer) + avail
	)	{
		SPIN(!(avail = EMUL_Write(peer, buffer, buflen)), SP_STREAM_WRITE, 0);
		if (avail == -1) {
			if (!errno)
				GUSI_error(peer->errno);
				
			break;
		} 
		
		done += avail;
		
		if (nonblocking)
			break;
	}
			
	return done ? done : (buflen ? -1 : 0);
}

/********************* UnixDgramSocket members **********************/

UnixDgramSocket::UnixDgramSocket()
	:	UnixSocket(SOCK_DGRAM)
{
}

UnixDgramSocket::~UnixDgramSocket()
{
}

int UnixDgramSocket::connect(void *sa_name, int sa_namelen)
{
	UnixSocketAddr *	addr;
	
	addr	=	CanonizeName(sa_name, sa_namelen);
	
	if (!addr)
		return -1;
	
	peer = LookupName(*addr);
	
	if (!peer)
		return GUSI_error(EADDRNOTAVAIL);
	
	state	=	SOCK_STATE_CONNECTED;
	
	return 0;
}

int UnixDgramSocket::recvfrom(void * buffer, int buflen, int flags, void * from, int * fromlen)
{
	UnixChannel * 	partner;
	int				length;
	int				avail;
	
	if (flags)
		return GUSI_error(EOPNOTSUPP);
	if (!chan || !chan->Address().valid)
		return GUSI_error(ENOTCONN);	// fuck UNIX semantics; it's impossible for another
												// socket to communicate with this one, anyway
		
	if ((avail = chan->ReadAvail()) == -1)
		return GUSI_error(chan->errno);
	
	// Datagram sockets communicate the sender's address in the first 4 bytes
	// The transfer will be atomic, so only one spin is needed
	
	if (nonblocking && !avail)
		return GUSI_error(EWOULDBLOCK);
	
	errno	=	0;
	SPIN(!(avail = chan->Read(&partner, sizeof(UnixChannel *))), SP_DGRAM_READ, 0);
	if (errno)
		return -1;
	else if (avail == -1)
		return GUSI_error(chan->errno);
	else if (avail < int(sizeof(UnixChannel *)))
		return GUSI_error(EFAULT);

	if ((avail = chan->Read(&length, sizeof(long))) == -1)
		return GUSI_error(chan->errno);
	
	buflen = min(buflen, length);
	
	if ((avail = chan->Read(buffer, buflen)) == -1)
		return GUSI_error(chan->errno);
		
	chan->DiscardRead(length - buflen);
	
	UncanonizeName(EMUL_Address(partner), from, fromlen);
		
	return avail;
}

int UnixDgramSocket::sendto(void * buffer, int buflen, int flags, void * to, int tolen)
{
	UnixSocketAddr *	addr;
	UnixChannel * 		partner;
	int					length;
	int					avail;
	
	if (flags)
		return GUSI_error(EOPNOTSUPP);
	if (!chan)
		chan = new UnixChannel2(this);
	if (peer)	
		partner	=	peer;
	else {
		addr	=	CanonizeName(to, tolen);
		
		if (!addr)
			return -1;
		
		partner = LookupName(*addr);
		
		if (!partner)
			return GUSI_error(EADDRNOTAVAIL);
	}
			
	length	=	sizeof(UnixChannel *) + sizeof(int) + buflen;
	
	if (length > EMUL_BufSize(partner))
		return GUSI_error(EMSGSIZE);
	if ((avail = EMUL_WriteAvail(partner)) == -1)
		return GUSI_error(partner->errno);
	if (avail < length)
		if (nonblocking)
			return GUSI_error(EWOULDBLOCK);
		else
			SPIN((avail=EMUL_WriteAvail(partner)) != -1 && avail<length, SP_DGRAM_WRITE, 0);
	
	if (avail == -1)
		return GUSI_error(partner->errno);
		
	errno	=	0;
	SPIN(!(avail = EMUL_Send(partner, chan, buffer, buflen)), SP_DGRAM_WRITE, 0);
	if (errno)
		return -1;
	else if (avail == -1)
		return GUSI_error(partner->errno);
		
	return avail;
}

/*********************** UnixChannel members ************************/

UnixChannel::UnixChannel(UnixSocket * owner)
	:	address(UnixSocketAddr()), sock(owner)
{
#ifdef EMULATE_CFRONT
	emulated_vtable = UnixChannel_EmulatedVTable;
#endif
}

UnixChannel::~UnixChannel()
{
	if (address.valid)
		UnBind();
}

void UnixChannel::UnBind()
{
	if (address.owned)
		HDelete(address.vRefNum, address.parID, address.name);		
}

int UnixChannel::Version()
{
#ifdef EMULATE_CFRONT
	return 3;
#else
	return 1;
#endif
}

Boolean UnixChannel::Bind(UnixSocketAddr & addr)
{
	short				resFile;
	short				oldResFile	=	CurResFile();
	Handle			ID;
	UnixSocketID	me(this);
	FInfo				info;
	
	address	=	addr;
	
	if (PtrToHand(&me, &ID, sizeof(UnixSocketID)))
		return false;
		
	HDelete(address.vRefNum, address.parID, address.name);
	HCreate(address.vRefNum, address.parID, address.name, 'GU·I', '·OCK');
	HCreateResFile(address.vRefNum, address.parID, address.name);
	resFile = HOpenResFile(address.vRefNum, address.parID, address.name, fsRdWrPerm);
	
	if (resFile == -1)	{
		DisposeHandle(ID);
		return false;
	}
	
	AddResource(ID, 'GU·I', GUSIRsrcID, (StringPtr) "\p");
	
	if (ResError()) {
		CloseResFile(resFile);
		HDelete(address.vRefNum, address.parID, address.name);
		DisposeHandle(ID);
		UseResFile(oldResFile);
		return false;
	}

	CopyIconFamily(LMGetCurApRefNum(), GUSIRsrcID, resFile, kCustomIconResource);
	
	CloseResFile(resFile);
	UseResFile(oldResFile);

	HGetFInfo(address.vRefNum, address.parID, address.name, &info);
	info.fdFlags	|=	(1 << 10);
	info.fdFlags	&= ~(1 << 8);
	HSetFInfo(address.vRefNum, address.parID, address.name, &info);
		
	return true;
}

Boolean UnixChannel::Connect(UnixChannel * ch)
{
	if (sock->curListener >= sock->maxListener)
		return false;
		
	switch (sock->state)	{
	case SOCK_STATE_LISTENING:
		sock->state	=	SOCK_STATE_LIS_CON;
		// Fall through
	case SOCK_STATE_LIS_CON:
		if (!sock->lastListener)
			sock->firstListener	=	ch;
		else
			sock->lastListener->nextListener = ch;
			
		sock->lastListener = ch;
		++sock->curListener;
		
		return true;
	default:
		return false;
	}
}

Boolean UnixChannel::Accept(UnixChannel * ch)
{
	if (!sock)
		return true;
		
	sock->peer	=	ch;
	sock->state	=	SOCK_STATE_CONNECTED;
	
	return true;
}

int UnixChannel::Read(void * buffer, int len)
{
	if (sock->status & SOCK_STATUS_NOREAD)
		return GUSI_error(ESHUTDOWN);
		
	Ptr	startBuf	= (Ptr) buffer;
	int	section;
		
	if (sock->validBytes > 0)	{
		section	=	sock->readBufSize-sock->readPos;
		
		if (section > sock->validBytes)
			section = sock->validBytes;
		if (section > len)
			section = len;
		
		BlockMove(sock->readBuf+sock->readPos, buffer, section);
		
		buffer				= (char *) buffer + section;
		sock->readPos		+=	section;
		sock->validBytes	-=	section;
		len 					-= section;
		
		if (sock->readPos == sock->readBufSize)
			sock->readPos	=	0;
	} else if (sock->state != SOCK_STATE_CONNECTED)
		return GUSI_error(ESHUTDOWN);
		
	if (len > 0 && sock->validBytes > 0)	{
		section 	=	(len > sock->validBytes) ? sock->validBytes : len;
		
		BlockMove(sock->readBuf, buffer, section);
		
		buffer				= (char *) buffer + section;
		sock->readPos		+=	section;
		sock->validBytes	-=	section;
	}
		
	return (char *) buffer-startBuf;		
}

int UnixChannel::Write(void * buffer, int len)
{	
	if (!sock || (sock->status & SOCK_STATUS_NOWRITE))
		return GUSI_error(ESHUTDOWN);

	Ptr	startBuf	=	(Ptr) buffer;
	int	avail		=	sock->readBufSize - sock->validBytes;
	int	section	=	avail - sock->readPos;
		
	if (section > 0)	{
		if (section > len)
			section = len;
			
		BlockMove(buffer, sock->readBuf+sock->readPos+sock->validBytes, section);
		
		buffer 				= (char *) buffer + section;
		sock->validBytes	+=	section;
		len 					-= section;
		avail					-=	section;
	}
	
	if (len > 0 && avail > 0)	{
		section 	=	(len > avail) ? avail : len;
		
		BlockMove(buffer, sock->readBuf+sock->readPos-avail, section);
		
		buffer 				= (char *) section;
		sock->validBytes	+=	section;
	}
		
	return (char *) buffer-startBuf;		
}

int UnixChannel::Send(UnixChannel * from, void * buffer, int len)
{
	int length	=	sizeof(UnixChannel *) + sizeof(int) + len;

	if (sock->peer && sock->peer != from)
		return GUSI_error(ECONNREFUSED);
	
	if (WriteAvail() < length)
		return 0;
	
	Write(&from, sizeof(UnixChannel *));
	Write(&len, sizeof(int));
	
	return Write(buffer, len);
}

int UnixChannel::ReadAvail()
{
	if (sock->status & SOCK_STATUS_NOREAD)
		return GUSI_error(ESHUTDOWN);
		
	return sock->validBytes;
}

int UnixChannel::WriteAvail()
{
	if (!sock || (sock->status & SOCK_STATUS_NOWRITE))
		return GUSI_error(ESHUTDOWN);
		
	return sock->readBufSize - sock->validBytes;
}

int UnixChannel::BufSize()
{
	return sock->readBufSize;
}

void UnixChannel::ShutDown(int how)
{
	switch(how) {
	case 0 : 
		sock->status |= SOCK_STATUS_NOREAD;
		break;
	case 1 : 
		sock->status |= SOCK_STATUS_NOWRITE;
		break;
	case 2 :
		sock->status |= SOCK_STATUS_NOREAD | SOCK_STATUS_NOWRITE;
	}
}

void UnixChannel::DiscardRead(int len)
{
	if (sock->validBytes <= len)	{
		sock->validBytes	=	0;
		sock->readPos		=	0;
	} else {
		sock->validBytes	-=	len;
		sock->readPos		= (sock->readPos+len) % sock->readBufSize;
	}
}

void UnixChannel::Disconnect()
{
	if (sock) {
		sock->peer	=	nil;
		sock->state	=	SOCK_STATE_UNCONNECTED;
	}
}

int UnixChannel::GUSI_error(int err)
{
	errno	=	err;

	return -1;
}

UnixSocketAddr & UnixChannel::Address()
{
	return address;
}

/*********************** UnixChannel2 members ************************/

UnixChannel2::UnixChannel2(UnixSocket * owner)
	:	UnixChannel(owner)
{
}

#ifndef EMULATE_CFRONT
int UnixChannel2::Version()
{
	return 2;
}
#endif

void UnixChannel2::Orphan()
{
	// Sever ties to owner
	
	sock->chan	=	nil;
	sock 			= 	nil;
}

void UnixChannel2::AbortConnect(UnixChannel * ch)
{
	UnixChannel	* prev = nil;
	
	for (UnixChannel * chan = sock->firstListener; chan; chan = chan->nextListener) {
		if (chan == ch)	{	// Got it !
			if (prev)
				prev->nextListener	=	chan->nextListener;
			else
				sock->firstListener	=	chan->nextListener;
			
			if (!chan->nextListener)
				sock->lastListener	=	prev;
			
			if (!--sock->curListener)
				sock->state = SOCK_STATE_LISTENING;
			
			break;
		}
		prev = chan;
	}
}

/********************* UnixSocketID members **********************/

UnixSocketID::UnixSocketID(UnixChannel * ch)
	: chan(ch)
{
	short	net;
	short node;
	
	AppleTalkIdentity(net, node);
	
	machine.aNet	=	net;
	machine.aNode	=	node;
	machine.aSocket=	0;
	
	if (!hasProcessMgr || GetCurrentProcess(&process))	{
		process.highLongOfPSN = kNoProcess;
		process.lowLongOfPSN = kNoProcess;
	}	
}

Boolean UnixSocketID::Validate()
{
	UnixSocketID	me(nil);
	ProcessInfoRec	info;
	
	if (memcmp(&machine, &me.machine, sizeof(AddrBlock)))
		return false;
		
	if (!hasProcessMgr)
		return (!process.highLongOfPSN && !process.lowLongOfPSN);
	
	info.processInfoLength	=	sizeof(ProcessInfoRec);
	info.processName			=	nil;
	info.processAppSpec		=	nil;
	
	return !GetProcessInformation(&process, &info);
}

/********************* UnixSocketDomain member **********************/

extern "C" void GUSIwithUnixSockets()
{
	UnixSockets.DontStrip();
}

Socket * UnixSocketDomain::socket(int type, short)
{
	UnixSocket * sock	=	nil;
	
	switch (type)	{
	case SOCK_STREAM:
		sock = new UnixStreamSocket();
		break;
	case SOCK_DGRAM:
		sock = new UnixDgramSocket();
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

int UnixSocketDomain::choose(int, char * prompt, void *, int flags, void * name, int * namelen)
{
	struct sockaddr_un *	addr = (sockaddr_un *) name;
	sa_constr_file			constr;
	
	addr->sun_family	= AF_UNIX;

	constr.numTypes	=	1;
	constr.types[0]	=	'·OCK';
	
	*namelen -= 2;
	if (FileSockets.choose(0, prompt, &constr, flags, addr->sun_path, namelen))
		return -1;
	
	*namelen += 2;
	
	return 0;
}


/************************* Name conversions *************************/

static char					canonPath[120];
static UnixSocketAddr	canonAddr;

static UnixSocketAddr * CanonizeName(void * name, int len)
{
	struct sockaddr_un *	addr = (sockaddr_un *) name;
	
	if (!name || !len || addr->sun_family != AF_UNIX)	{
		GUSI_error(EAFNOSUPPORT);
		
		return nil;
	}
	
	len -= 2;
	memcpy(canonPath, addr->sun_path, len);
	
	canonPath[len] = 	0;
	canonAddr		=	TFileSpec(canonPath);
	
	if (canonAddr.Error())
		return nil;
	else
		canonAddr.valid	=	true;
		
	return &canonAddr;
}

static void UncanonizeName(UnixSocketAddr & name, void * addr, int * addrlen)
{
	struct sockaddr_un *	uaddr = (sockaddr_un *) addr;
	char	* 					path	=	name.FullPath();
	char 	* 					end;
	
	if (!addr || *addrlen < int(sizeof(short)))	{
		if (addrlen)
			*addrlen = 0;
		
		return;
	}
	
	*addrlen 			-= 2;
	uaddr->sun_family = 	AF_UNIX;
	
	if (end = (char *) memccpy(uaddr->sun_path, path, 0, *addrlen))
		*addrlen = end-uaddr->sun_path-1;
	
	*addrlen 			+= 	2;
}

static UnixChannel * LookupName(UnixSocketAddr name)
{
	short				file;
	Handle			hdl;
	UnixChannel *	cur	=	nil;
	UnixSocketID	id;

	file	=	HOpenResFile(name.vRefNum, name.parID, name.name, fsRdPerm);

	if (file != -1)	{
		if (hdl = Get1Resource('GU·I', GUSIRsrcID))	{
			id = **(UnixSocketID **) hdl;
			
			if (id.Validate())
				cur = id.chan;
		}
		CloseResFile(file);
	}
	
	return cur;
}
