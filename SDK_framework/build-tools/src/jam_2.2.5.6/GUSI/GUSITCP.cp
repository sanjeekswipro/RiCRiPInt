/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSITCP.cp		-	TCP Stream Sockets
Author	:	Matthias Neeracher

	This file was derived from the socket library by

		Charlie Reiman	<creiman@ncsa.uiuc.edu> and
		Tom Milligan	<milligan@madhaus.utcs.utoronto.ca>

Language	:	MPW C/C++

$Log: GUSITCP.cp,v $
Revision 1.2  1999/04/23  08:43:48  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.5  1994/12/30  20:18:41  neeri
Wake up process from completion procedures.
Minor corrections.

Revision 1.4  1994/08/10  00:04:13  neeri
Sanitized for universal headers.

Revision 1.3  1994/05/04  01:47:24  neeri
Long writes on nonblocking sockets would fail.

Revision 1.2  1994/05/01  23:48:59  neeri
Enable recvfrom with non-null from address.
Try to behave better when other side closes connection.

Revision 1.1  1994/02/25  02:30:45  neeri
Initial revision

Revision 0.6  1993/08/25  00:00:00  neeri
return correct peer address from accept()

Revision 0.5  1993/01/31  00:00:00  neeri
Support for inetd

Revision 0.4  1993/01/21  00:00:00  neeri
Simplify and correct code

Revision 0.3  1993/01/17  00:00:00  neeri
Be more careful about user interrupts.

Revision 0.2  1993/01/08  00:00:00  neeri
tcp_notify was setting the wrong state to unconnected

Revision 0.1  1992/09/08  00:00:00  neeri
A big part should work now				

*********************************************************************/

#include "GUSIINET_P.h"

/********************** Completion procedures ***********************/

#if !defined(powerc) && !defined(__powerc)
#pragma segment GUSIResident
#endif

pascal void tcp_notify(
	StreamPtr,
	u_short					eventCode,
	Ptr						userDataPtr,
	u_short,
	struct ICMPReport *)
{
	TCPSocket *	sock	=	*(TCPSocket **) userDataPtr;

	switch (eventCode) {
	case TCPClosing:
		sock->sstate = SOCK_STATE_CLOSING;
		break;

	case TCPTerminate:
		sock->sstate = SOCK_STATE_UNCONNECTED;
		break;
	}
	sock->Ready();
}

#if USESROUTINEDESCRIPTORS
RoutineDescriptor	u_tcp_notify = 
		BUILD_ROUTINE_DESCRIPTOR(uppTCPNotifyProcInfo, tcp_notify);
#else
#define u_tcp_notify tcp_notify
#endif

void tcp_connect_done(AnnotatedPB *pb)
{
	TCPSocket *	sock	=	(TCPSocket *) pb->Owner();
	TCPiopb *	tcp	=	pb->TCP();

	if (!(sock->asyncerr = tcp->ioResult)) {
		sock->sa.sin_addr.s_addr 	= tcp->csParam.open.localHost;
		sock->sa.sin_port 			= tcp->csParam.open.localPort;
		sock->peer.sin_addr.s_addr	= tcp->csParam.open.remoteHost;
		sock->peer.sin_port 			= tcp->csParam.open.remotePort;
		sock->sstate 					= SOCK_STATE_CONNECTED;
	}
	sock->Ready();
}

#if USESROUTINEDESCRIPTORS
RoutineDescriptor	u_tcp_connect_done = 
		BUILD_ROUTINE_DESCRIPTOR(uppTCPIOCompletionProcInfo, tcp_connect_done);
#else
#define u_tcp_connect_done tcp_connect_done
#endif

void tcp_listen_done(AnnotatedPB *pb)
{
	TCPSocket *	sock	=	(TCPSocket *) pb->Owner();
	TCPiopb *	tcp	=	pb->TCP();

	switch(tcp->ioResult) {
	case noErr:
		sock->peer.sin_addr.s_addr	= tcp->csParam.open.remoteHost;
		sock->peer.sin_port 			= tcp->csParam.open.remotePort;
		sock->sstate 					= SOCK_STATE_LIS_CON;
		sock->asyncerr 				= 0;
		break;

	case openFailed:
	case invalidStreamPtr:
	case connectionExists:
	case duplicateSocket:
	case commandTimeout:
	default:
		sock->sstate 					= SOCK_STATE_UNCONNECTED;
		sock->asyncerr 				= tcp->ioResult;
		break;
	}
	sock->Ready();
}

#if USESROUTINEDESCRIPTORS
RoutineDescriptor	u_tcp_listen_done = 
		BUILD_ROUTINE_DESCRIPTOR(uppTCPIOCompletionProcInfo, tcp_listen_done);
#else
#define u_tcp_listen_done tcp_listen_done
#endif

void tcp_recv_done(AnnotatedPB *pb)
{
	TCPSocket *		sock	=	(TCPSocket *) pb->Owner();
	TCPiopb *		tcp	=	pb->TCP();
	register	int 	readin;

	if (!tcp->ioResult) {
		readin = tcp->csParam.receive.rcvBuffLen;
		sock->recvd   = readin;
	}
	sock->Ready();
}

#if USESROUTINEDESCRIPTORS
RoutineDescriptor	u_tcp_recv_done = 
		BUILD_ROUTINE_DESCRIPTOR(uppTCPIOCompletionProcInfo, tcp_recv_done);
#else
#define u_tcp_recv_done tcp_recv_done
#endif

void tcp_send_done(AnnotatedPB *pb)
{
	TCPSocket *	sock	=	(TCPSocket *) pb->Owner();
	TCPiopb *	tcp	=	pb->TCP();

	switch (tcp->ioResult) {
	case noErr:
		((wdsEntry *)(tcp->csParam.send.wdsPtr))->length = 0;	/* mark it free */
		sock->asyncerr = noErr;
		break;

	case ipNoFragMemErr:
	case connectionClosing:
	case connectionTerminated:
	case connectionDoesntExist:
		sock->sstate 	= SOCK_STATE_UNCONNECTED;
		sock->asyncerr = ENOTCONN;
		break;

	case ipDontFragErr:
	case invalidStreamPtr:
	case invalidLength:
	case invalidWDS:
	default:
		sock->sstate 	= SOCK_STATE_UNCONNECTED;
		sock->asyncerr = tcp->ioResult;
		break;
	}
	sock->Ready();
}

#if USESROUTINEDESCRIPTORS
RoutineDescriptor	u_tcp_send_done = 
		BUILD_ROUTINE_DESCRIPTOR(uppTCPIOCompletionProcInfo, tcp_send_done);
#else
#define u_tcp_send_done tcp_send_done
#endif

#if !defined(powerc) && !defined(__powerc)
#pragma segment GUSIINET
#endif

/************************ TCPSocket members *************************/


TCPSocket::TCPSocket()
	:	INETSocket()
{
	TCPiopb	pb;

	sstate 	= SOCK_STATE_UNCONNECTED;
	self 	 	= new(TCPSocket *);
	*self		= this;

	pb.ioCRefNum 						= INETSockets.Driver();
	pb.csCode 							= TCPCreate;
	pb.csParam.create.rcvBuff 		= (char *)NewPtr(STREAM_BUFFER_SIZE);
	pb.csParam.create.rcvBuffLen 	= STREAM_BUFFER_SIZE;
	pb.csParam.create.notifyProc 	= TCPNotifyProcPtr(&u_tcp_notify);
	pb.csParam.create.userDataPtr	= Ptr(self);

   if (!pb.csParam.create.rcvBuff) {
		GUSI_error(ENOBUFS);
		return;
	}
		
	switch(PBControlSync(ParmBlkPtr(&pb)))
	{
		case noErr:                 	break;
		case invalidLength:         	GUSI_error(ENOBUFS); 	return;
		case invalidBufPtr:         	GUSI_error(ENOBUFS); 	return;
		case insufficientResources: 	GUSI_error(EMFILE); 		return;
		default: 							GUSI_error(ENETDOWN); 	return;
	}

	peer.sin_family 		= AF_INET;
	peer.sin_addr.s_addr = 0;
	peer.sin_port 			= 0;

	bzero(&peer.sin_zero[0], 8);

	asyncerr 				= 0;
	stream 					= pb.tcpStream;
}

TCPSocket::TCPSocket(StreamPtr stream)
	:	INETSocket(stream)
{
	AppleEvent				theEvent, myReply;
	AEDesc					theAddress;
	long						theType = 'inet';
	ProcPtr					theProc	= ProcPtr(&u_tcp_notify);
	ProcessSerialNumber	PSN;

	self 	 	= new(TCPSocket *);
	*self		= this;
	asyncerr = 0;

	GetCurrentProcess(&PSN);
	AECreateDesc(typeApplSignature, (Ptr) &theType, sizeof(theType), &theAddress);
	AECreateAppleEvent('INET', 'TNFY',  &theAddress, kAutoGenerateReturnID, kAnyTransactionID, &theEvent);
	
	AEPutParamPtr(&theEvent, 'STRM', typeLongInteger, (Ptr) &stream, sizeof(stream));
	AEPutParamPtr(&theEvent, 'ASR ', typeLongInteger, (Ptr) &theProc, sizeof(ProcPtr));
	AEPutParamPtr(&theEvent, 'USRP', typeLongInteger, (Ptr) &self, sizeof(long));
	AEPutParamPtr(&theEvent, keyProcessSerialNumber, typeProcessSerialNumber, (Ptr) &PSN, sizeof(ProcessSerialNumber));
	
	AESend(&theEvent, &myReply, kAEWaitReply, kAEHighPriority, 120, nil, nil);

	AEDisposeDesc(&myReply);
	AEDisposeDesc(&theEvent);
	AEDisposeDesc(&theAddress);

	TCPiopb * pb;

	pb				= GetPB();
	pb->csCode 	= TCPStatus;

	PBControlSync(ParmBlkPtr(pb));
	
	sa.sin_addr.s_addr 	= 	pb->csParam.status.localHost;
	sa.sin_port				=	pb->csParam.status.localPort;
	peer.sin_addr.s_addr = 	pb->csParam.status.remoteHost;
	peer.sin_port			=	pb->csParam.status.remotePort;
}

TCPSocket::TCPSocket(TCPSocket * sock)
{
	stream		= sock->stream;
	status		= sock->status;
	nonblocking	= sock->nonblocking;
	recvBuf		= sock->recvBuf;
	recvd			= sock->recvd;
	sa				= sock->sa;
	peer			= sock->peer;
	sstate		= sock->sstate;
	asyncerr		= 0;
	
	// The reason for this strange code is that stream->userData points to
	// sock.self and cannot be changed while the stream is alive
	
	self			= sock->self;
	*self			= this;
	sock->self	= new(TCPSocket *);
	*sock->self	= sock;
}

TCPSocket::~TCPSocket()
{
	TCPiopb *	pb;

	do {
		pb					= GetPB();
		pb->csCode 		= TCPStatus;

		PBControlSync(ParmBlkPtr(pb));

		SAFESPIN(false, SP_MISC, 0);
	} while (!errno && pb->csParam.status.amtUnackedData > 0);

	pb												= GetPB();
	pb->ioCompletion							= nil;
	pb->csCode 									= TCPClose;
	pb->csParam.close.validityFlags 		= timeoutValue | timeoutAction;
	pb->csParam.close.ulpTimeoutValue 	= 60 /* seconds */;
	pb->csParam.close.ulpTimeoutAction 	= 1 /* 1:abort 0:report */;

	switch (PBControlAsync(ParmBlkPtr(pb)))
	{
		case noErr:
		case connectionClosing:
			break;
		case connectionDoesntExist:
		case connectionTerminated:
			break;
		case invalidStreamPtr:
		default:
			return;
	}

	{
		rdsEntry	rdsarray[TCP_MAX_WDS+1];
		int		passcount;
		const int maxpass =4;

		pb					=	GetPB();

		for (passcount=0; passcount<maxpass; passcount++) {
			pb->csCode 											= TCPNoCopyRcv;
			pb->csParam.receive.commandTimeoutValue	= 1; /* seconds, 0 = blocking */
			pb->csParam.receive.rdsPtr 					= (Ptr)rdsarray;
			pb->csParam.receive.rdsLength 				= TCP_MAX_WDS;

			if (PBControlSync(ParmBlkPtr(pb)))
				break;

			pb->csCode 											= TCPRcvBfrReturn;
			pb->csParam.receive.rdsPtr 					= (Ptr)rdsarray;

			PBControlSync(ParmBlkPtr(pb));

			SAFESPIN(false, SP_MISC, 0);
			
			if (errno)
				break;
		}

		if (passcount == maxpass) {		/* remote side isn't being nice */
			/* then try again */

			PBControlSync(ParmBlkPtr(pb));

			for (passcount=0; passcount<maxpass; passcount++) {
				pb->csCode 											= TCPNoCopyRcv;
				pb->csParam.receive.commandTimeoutValue	= 1; /* seconds, 0 = blocking */
				pb->csParam.receive.rdsPtr 					= (Ptr)rdsarray;
				pb->csParam.receive.rdsLength 				= TCP_MAX_WDS;

				if (PBControlSync(ParmBlkPtr(pb)))
					break;

				pb->csCode 											= TCPRcvBfrReturn;
				pb->csParam.receive.rdsPtr 					= (Ptr)rdsarray;

				PBControlSync(ParmBlkPtr(pb));

				SAFESPIN(false, SP_MISC, 0);
				
				if (errno)
					break;
			}
		}
	}

	/* destroy the stream */
	pb->csCode 	= TCPRelease;

	if (PBControlSync(ParmBlkPtr(pb)))
		return;

	DisposPtr(pb->csParam.create.rcvBuff); /* there is no release pb */
	
	delete self;
}

TCPiopb * TCPSocket::GetPB()
{
	AnnotatedPB *	pb		=	INETSockets.GetPB();
	pb->TCP()->ioCRefNum =	INETSockets.Driver();
	pb->TCP()->tcpStream	=	stream;

	pb->SetOwner(this);

	return pb->TCP();
}

u_long TCPSocket::Available()
{
	TCPiopb * pb;

	pb				= GetPB();
	pb->csCode 	= TCPStatus;

	PBControlSync(ParmBlkPtr(pb));

	return pb->csParam.status.amtUnreadData;
}

/*
 *	connect - initiate a connection on a MacTCP socket
 *
 *		This call attempts to make a
 *		connection to another socket. The other socket is specified
 *		by an internet address and port.
 *
 *		TCP sockets may successfully connect() only once;
 *
 *		If the connection or binding succeeds, then 0 is returned.
 *		Otherwise a -1 is returned, and a more specific error code
 *		is stored in errno.
 *
 *		EAFNOSUPPORT        The address family in addr is not AF_INET.
 *
 *		EHOSTUNREACH        The TCP connection came up half-way and
 *                          then failed.
 */

int TCPSocket::connect(void * address, int addrlen)
{
	OSErr						err;
	struct sockaddr_in *	addr	=	(struct sockaddr_in *) address;
	TCPiopb	*				pb;

	if (addrlen != int(sizeof(struct sockaddr_in)))
		return GUSI_error(EINVAL);

	if (addr->sin_family != AF_INET)
		return GUSI_error(EAFNOSUPPORT);

	/* Make sure this socket can connect. */
	if (sstate == SOCK_STATE_CONNECTING)
		if (asyncerr)
			return GUSI_error(ECONNREFUSED);
		else
			return GUSI_error(EALREADY);
	if (sstate != SOCK_STATE_UNCONNECTED)
		return GUSI_error(EISCONN);

	sstate = SOCK_STATE_CONNECTING;

	pb													= GetPB();
	pb->ioCompletion								= TCPIOCompletionProc(&u_tcp_connect_done);
	pb->csCode 										= TCPActiveOpen;
	pb->csParam.open.validityFlags 			= timeoutValue | timeoutAction;
	pb->csParam.open.ulpTimeoutValue 		= 60 /* seconds */;
	pb->csParam.open.ulpTimeoutAction 		= 1 /* 1:abort 0:report */;
	pb->csParam.open.commandTimeoutValue 	= 0;
	pb->csParam.open.remoteHost 				= addr->sin_addr.s_addr;
	pb->csParam.open.remotePort 				= addr->sin_port;
	pb->csParam.open.localHost 				= 0;
	pb->csParam.open.localPort 				= sa.sin_port;
	pb->csParam.open.dontFrag 					= 0;
	pb->csParam.open.timeToLive 				= 0;
	pb->csParam.open.security 					= 0;
	pb->csParam.open.optionCnt 				= 0;

	if (err = PBControlAsync(ParmBlkPtr(pb)))
	{
		sstate = SOCK_STATE_UNCONNECTED;
		return TCP_error(err);
	}

	if (nonblocking)
		return GUSI_error(EINPROGRESS);

	/* sync connect - spin till TCPActiveOpen completes */

	SAFESPIN(pb->ioResult==inProgress, SP_MISC, 0);

	if (errno || pb->ioResult) {
		sstate = SOCK_STATE_UNCONNECTED;
		
		if (errno)
			return -1;
		else
			return TCP_error(pb->ioResult);
	} else
		return 0;
}

int TCPSocket::listen(int)
{
	OSErr			err;
	TCPiopb *	pb;

	if (sstate != SOCK_STATE_UNCONNECTED)
		return GUSI_error(EISCONN);

	sstate 											= SOCK_STATE_LISTENING;
	pb													= GetPB();
	pb->ioCRefNum 									= INETSockets.Driver();
	pb->ioCompletion								= TCPIOCompletionProc(&u_tcp_listen_done);
	pb->csCode 										= TCPPassiveOpen;
	pb->csParam.open.validityFlags 			= timeoutValue | timeoutAction;
	pb->csParam.open.ulpTimeoutValue 		= 255 /* seconds */;
	pb->csParam.open.ulpTimeoutAction 		= 0 /* 1:abort 0:report */;
	pb->csParam.open.commandTimeoutValue 	= 0 /* infinity */;
	pb->csParam.open.remoteHost 				= 0;
	pb->csParam.open.remotePort 				= 0;
	pb->csParam.open.localHost 				= 0;
	pb->csParam.open.localPort 				= sa.sin_port;
	pb->csParam.open.dontFrag 					= 0;
	pb->csParam.open.timeToLive 				= 0;
	pb->csParam.open.security 					= 0;
	pb->csParam.open.optionCnt 				= 0;

	if (err = PBControlAsync(ParmBlkPtr(pb))) {
		sstate = SOCK_STATE_UNCONNECTED;
		
		return TCP_error(err);
	}
	
	SAFESPIN(!pb->csParam.open.localPort, SP_MISC, 0);
		
	if (errno) {
		sstate = SOCK_STATE_UNCONNECTED;
		
		return -1;
	}

	sa.sin_addr.s_addr 	= pb->csParam.open.localHost;
	sa.sin_port 			= pb->csParam.open.localPort;

	return 0;
}

Socket * TCPSocket::accept(void *from, int *fromlen)
{
	TCPSocket *		sock;
	TCPiopb *		pb;

	if (sstate == SOCK_STATE_UNCONNECTED)
		if (asyncerr) {
			TCP_error(asyncerr);
			asyncerr = 0;

			return nil;
		} else
			return (Socket *) GUSI_error_nil(ENOTCONN);

	if (sstate != SOCK_STATE_LISTENING && sstate != SOCK_STATE_LIS_CON)
		return (Socket *) GUSI_error_nil(ENOTCONN);

	if (sstate == SOCK_STATE_LISTENING) {
		if (nonblocking)
			return (Socket *) GUSI_error_nil(EWOULDBLOCK);

		/*	Spin till sock_tcp_listen_done runs. */
		SPINP(sstate == SOCK_STATE_LISTENING, SP_MISC, 0);

		/* got notification - was it success? */
		if (sstate != SOCK_STATE_LIS_CON) {
			(void) TCP_error(asyncerr);
			asyncerr = 0;
			return nil;
		}
	}

	/*
	 * Have connection.  Duplicate this socket.  The client gets the connection
	 * on the new socket and I create a new stream on the old socket and put it
	 * in listen state.
	 */
	sstate 	= SOCK_STATE_CONNECTED;
	sock		= new TCPSocket(this);

	if (!sock)
	{
		/*	Abort the incoming connection. */
		pb 				= GetPB();
		pb->csCode 		= TCPAbort;

		PBControlSync(ParmBlkPtr(pb));

		sstate = SOCK_STATE_UNCONNECTED;

		/* try and put the socket back in listen mode */
		if (listen(5) < 0)
		{
			sstate = SOCK_STATE_UNCONNECTED;
			return nil;		/* errno already set */
		}
		return (Socket *) GUSI_error_nil(ENOMEM);
	}

	/* Create a new MacTCP stream on the old socket and put it into */
	/* listen state to accept more connections. */
	sstate = SOCK_STATE_UNCONNECTED;

	pb											= GetPB();
	pb->csCode 								= TCPCreate;
	pb->csParam.create.rcvBuff 		= (char *)NewPtr(STREAM_BUFFER_SIZE);
	pb->csParam.create.rcvBuffLen 	= STREAM_BUFFER_SIZE;
	pb->csParam.create.notifyProc 	= TCPNotifyProcPtr(&u_tcp_notify);
	pb->csParam.create.userDataPtr	= Ptr(self);

   if (!pb->csParam.create.rcvBuff)
		return (Socket *) GUSI_error_nil(ENOBUFS);

	switch(PBControlSync(ParmBlkPtr(pb)))
	{
		case noErr:                 	break;
		case invalidLength:         	return (Socket *) GUSI_error_nil(ENOBUFS);
		case invalidBufPtr:         	return (Socket *) GUSI_error_nil(ENOBUFS);
		case insufficientResources: 	return (Socket *) GUSI_error_nil(EMFILE);
		default: 							return (Socket *) GUSI_error_nil(ENETDOWN);
	}

	peer.sin_family 		= AF_INET;
	peer.sin_addr.s_addr = 0;
	peer.sin_port 			= 0;

	bzero(&peer.sin_zero[0], 8);

	asyncerr 				= 0;
	stream 					= pb->tcpStream;

	if (listen(5) < 0) {
		/* nothing to listen on */
		sstate = SOCK_STATE_UNCONNECTED;

		/* kill the incoming connection */
		pb					= sock->GetPB();
		pb->csCode		= TCPRelease;

		if (!PBControlSync(ParmBlkPtr(pb)))
			DisposPtr(pb->csParam.create.rcvBuff); /* there is no release pb */

		return nil; /* errno set */
	}

	/* return address of partner */
	memcpy(from, &sock->peer, *fromlen = int(min(*fromlen, int(sizeof(struct sockaddr_in)))));

	return sock;
}

/*
 *	TCPSocket::recvfrom(s, buffer, buflen, flags, from, fromlen)
 *
 *		recvfrom() attempts to receive a message (ie a datagram)
 *		on the socket s.
 *
 *		from returns the address of the socket which sent the message.
 *		fromlen is the usual value-result length parameter.
 *
 *		Typically, read() is used with a TCP stream and recv() with
 *		UDP where the idea of a message makes more sense. But in fact,
 *		read() and recv() are equivalent.
 *
 *		Regardless of non-blocking status, if less data is available
 *		than has been requested, only that much data is returned.
 *
 *		If the socket is marked for non-blocking I/O, and the socket
 *		is empty, the operation will fail with the error EWOULDBLOCK.
 *		Otherwise, the operation will block until data is available
 *		or an error occurs.
 *
 *		A return value of zero indicates that the stream has been
 *		closed and all data has already been read. ie. end-of-file.
 *
 *		Flags is ignored.
 *
 *		If successful, the number of bytes actually received is
 *		returned. Otherwise, a -1 is returned and the global variable
 *		errno is set to indicate the error.
 *
 *		ESHUTDOWN    The socket has been shutdown for receive operations.
 */

int TCPSocket::recvfrom(void * buffer, int buflen, int, void * from, int * fromlen)
{
	TCPiopb	*	pb;
	u_long		dataavail;

	if (from)
		getpeername(from, fromlen);
	if (status & SOCK_STATUS_NOREAD)
		return GUSI_error(ESHUTDOWN);

	/* socket hasn't finished connecting yet */
	if (sstate == SOCK_STATE_CONNECTING)
	{
		if (nonblocking)
			return GUSI_error(EWOULDBLOCK);

		/* async connect and sync recv? */

		SPIN(sstate == SOCK_STATE_CONNECTING,SP_MISC,0);
	}

	/* socket is not connected */
	if (!(sstate == SOCK_STATE_CONNECTED))
	{
		if (sstate == SOCK_STATE_CLOSING)
			return 0;
			
		/* see if the connect died (pretty poor test) */
		if (sstate == SOCK_STATE_UNCONNECTED && asyncerr != 0 && asyncerr != 1)
		{
			(void) TCP_error(asyncerr);
			asyncerr = 0;
			return -1;
		}

		/* I guess he just forgot */
		return GUSI_error(ENOTCONN);
	}

	dataavail = Available();

	if (nonblocking && !dataavail)
		return GUSI_error(EWOULDBLOCK);
		
	recvBuf	= (char *) buffer;
	recvd		= 0;
	asyncerr	= inProgress;

	pb 													= GetPB();
	pb->ioCompletion									= TCPIOCompletionProc(&u_tcp_recv_done);
	pb->csCode 											= TCPRcv;
	pb->csParam.receive.commandTimeoutValue 	= 0; /* seconds, 0 = blocking */
	pb->csParam.receive.rcvBuff 					= recvBuf;
	pb->csParam.receive.rcvBuffLen 				= min(buflen,TCP_MAX_MSG);

	PBControlAsync(ParmBlkPtr(pb));

	/* This is potentially dangerous, as there doesn't seem to be a way to
		stop the receive call on an user abort.
	*/
	SPIN(pb->ioResult==inProgress, SP_STREAM_READ, buflen);

	if (pb->ioResult == commandTimeout)
		pb->ioResult = noErr;

	switch(pb->ioResult)
	{
		case noErr:
			asyncerr = noErr;

			return recvd;

		case connectionClosing:
		case connectionTerminated:
			return recvd;

		case commandTimeout: /* this one should be caught by sock_tcp_recv_done */
		case connectionDoesntExist:
		case invalidStreamPtr:
		case invalidLength:
		case invalidBufPtr:
		default:
			return TCP_error(pb->ioResult);
	}
}

/*
 *	TCPSocket::sendto(s, buffer, buflen, flags, to, tolen)
 *
 *		sendto() is used to transmit a message to another
 *		socket on the socket s.
 *
 *		Typically, write() is used with a TCP stream and send() with
 *		UDP where the idea of a message makes more sense. But in fact,
 *		write() and send() are equivalent.
 *
 *		Write() and send() operations are not considered complete
 *		until all data has been sent and acknowledged.
 *
 *		If a socket is marked for non-blocking I/O, the operation
 *		will return an 'error' of EINPROGRESS.
 *
 *		If the socket is not marked for non-blocking I/O, the write will
 *		block until space becomes available.
 *
 *		write() and send() may be used only when the socket is in a connected
 *		state, sendto() may be used at any time.
 *
 *		Flags is ignored.
 *
 *		These calls return the number of bytes sent, or -1 if an error
 *		occurred.
 *
 *		EINVAL          	The sum of the iov_len values in the iov array was
 *								greater than 65535 (TCP) or 65507 (UDP) or there
 *                      were too many entries in the array (16 for TCP or
 *                      6 for UDP).
 *
 *		ESHUTDOWN        The socket has been shutdown for send operations.
 *
 *		EMSGSIZE         The message is too big to send in one datagram. (UDP)
 *
 *		ENOBUFS          The transmit queue is full. (UDP)
 */

int TCPSocket::sendto(void * buffer, int count, int flags, void * to, int)
{
	int			bytes,towrite;
	miniwds *	thiswds;
	short			wdsnum;
	TCPiopb *	pb;
	miniwds		wdsarray[TCP_MAX_WDS];

	if (status & SOCK_STATUS_NOWRITE)
		return GUSI_error(ESHUTDOWN);

	if (to != NULL) /* sendto */
		return GUSI_error(EOPNOTSUPP);
	if (sstate != SOCK_STATE_CONNECTED && sstate != SOCK_STATE_CONNECTING)
		return GUSI_error(ENOTCONN);

	/* socket hasn't finished connecting yet */
	if (sstate == SOCK_STATE_CONNECTING) {
		if (nonblocking)
			return GUSI_error(EALREADY);

		/* async connect and sync send? */
		SPIN(sstate == SOCK_STATE_CONNECTING, SP_MISC, 0);
	}

	/* socket is not connected */
	if (!(sstate == SOCK_STATE_CONNECTED)) {
		/* see if a previous operation failed */
		if (sstate == SOCK_STATE_UNCONNECTED && asyncerr != 0) {
			(void) TCP_error(asyncerr);
			asyncerr = 0;
			return -1;
		}

		/* I guess he just forgot */
		return GUSI_error(ENOTCONN);
	}

	pb					= GetPB();
	pb->csCode 		= TCPStatus;

	if (PBControlSync(ParmBlkPtr(pb)))
		bytes = 0;
	else {
		bytes = pb->csParam.status.sendWindow - pb->csParam.status.amtUnackedData;

		if (bytes < 0)
			bytes = 0;
	}

	if (nonblocking)
		if (!bytes)
			return GUSI_error(EWOULDBLOCK);
		else if (bytes < count)
			count = bytes;

	bytes	=	count;												/* save count before we nuke it */
	memset(wdsarray, 0, TCP_MAX_WDS*sizeof(miniwds));	/* clear up terminus and mark empty */
	thiswds = wdsarray;
	wdsnum = 0;

	while (count > 0) {
		/* make sure the thing that just finished worked ok */
		if (asyncerr) {
			(void) GUSI_error(asyncerr);
			asyncerr = 0;
			return -1;
		}

		towrite=min(count,TCP_MAX_MSG);

		/* find a clean wds */

		while (thiswds->length != 0) {
			wdsnum = (short)((wdsnum+1)%TCP_MAX_WDS); /* generates compiler warning w/o short - why? */
			if (wdsnum)
				thiswds++;
			else
				thiswds = wdsarray;
			SPIN(false, SP_STREAM_WRITE, count);	/* spin once */
		}

		/* find a clean pb */

		thiswds->length							= (short)towrite;
		thiswds->ptr								= (char *) buffer;
		pb												= GetPB();
		pb->ioCompletion							= TCPIOCompletionProc(&u_tcp_send_done);
		pb->csCode 									= TCPSend;
		pb->csParam.send.validityFlags 		= timeoutValue | timeoutAction;
		pb->csParam.send.ulpTimeoutValue 	= 60 /* seconds */;
		pb->csParam.send.ulpTimeoutAction 	= 1 /* 0:abort 1:report */;
		pb->csParam.send.pushFlag 				= count <= TCP_MAX_MSG;
		pb->csParam.send.urgentFlag 			= flags & MSG_OOB;
		pb->csParam.send.wdsPtr 				= (Ptr)thiswds;
		pb->csParam.send.sendFree 				= 0;
		pb->csParam.send.sendLength 			= 0;

		PBControlAsync(ParmBlkPtr(pb));

		SPIN(false, SP_STREAM_WRITE, count);
		count 	-= towrite;
		buffer	= (char *) buffer + towrite;
	}

	SPIN(pb->ioResult == inProgress, SP_STREAM_WRITE, 0);

	if (!pb->ioResult)
		return(bytes);
	else
		return TCP_error(pb->ioResult);
}

int TCPSocket::select(Boolean * canRead, Boolean * canWrite, Boolean *)
{
	int	goodies 	= 	0;

	if (canRead)
		switch (sstate) {
		case SOCK_STATE_LIS_CON:
			*canRead	= true;
			++goodies;
			break;
		case SOCK_STATE_CONNECTED:
			if (Available()) {
				*canRead	= true;
				++goodies;
			}
			break;
		case SOCK_STATE_UNCONNECTED:
		case SOCK_STATE_CLOSING:
			*canRead	= true;
			++goodies;
			break;
		}

	if (canWrite)
		switch (sstate) {
		case SOCK_STATE_CONNECTING:
			break;
		default:
			*canWrite = true;
			++goodies;
		}

	return goodies;
}
