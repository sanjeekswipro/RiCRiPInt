/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIUDP.cp		-	UDP Datagram Sockets
Author	:	Matthias Neeracher

	This file was derived from the socket library by 
	
		Charlie Reiman	<creiman@ncsa.uiuc.edu> and
		Tom Milligan	<milligan@madhaus.utcs.utoronto.ca>
		  
Language	:	MPW C/C++

$Log: GUSIUDP.cp,v $
Revision 1.2  1999/04/23  08:43:47  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.3  1994/12/30  20:19:35  neeri
Wake up process from completion routines.

Revision 1.2  1994/08/10  00:02:50  neeri
Sanitized for universal headers.

Revision 1.1  1994/02/25  02:31:00  neeri
Initial revision

Revision 0.5  1994/02/15  00:00:00  neeri
udp_read_ahead_done didn't record the packet source

Revision 0.4  1993/06/21  00:00:00  neeri
forgot to reset asyncerr

Revision 0.3  1993/06/17  00:00:00  neeri
getsockname() didn't work for some sorts of bind()

Revision 0.2  1992/09/14  00:00:00  neeri
select() didn't return enough goodies.

Revision 0.1  1992/08/25  00:00:00  neeri
Started putting some real work in

*********************************************************************/

#include "GUSIINET_P.h"

/********************** Completion procedures ***********************/

#if !defined(powerc) && !defined(__powerc)
#pragma segment GUSIResident
#endif

void udp_read_ahead_done(AnnotatedPB *pb)
{
	UDPSocket *		sock	=	(UDPSocket *) pb->Owner();
	UDPiopb *		udp	=	pb->UDP();

	if (!udp->ioResult) {
		sock->recvBuf	= udp->csParam.receive.rcvBuff;
		sock->recvd		= udp->csParam.receive.rcvBuffLen;
	} else {
		sock->recvBuf	= nil;
		sock->recvd		= 0;
	}
	sock->asyncerr 				= 	udp->ioResult;
	sock->peer.sin_len 			= sizeof(struct sockaddr_in);
	sock->peer.sin_family 		= AF_INET;
	sock->peer.sin_addr.s_addr	=	udp->csParam.receive.remoteHost;
	sock->peer.sin_port			=	udp->csParam.receive.remotePort;
	sock->Ready();
}

#if USESROUTINEDESCRIPTORS
RoutineDescriptor	u_udp_read_ahead_done = 
		BUILD_ROUTINE_DESCRIPTOR(uppUDPIOCompletionProcInfo, udp_read_ahead_done);
#else
#define u_udp_read_ahead_done udp_read_ahead_done
#endif

void udp_send_done(AnnotatedPB *pb)
{	
	UDPSocket *		sock	=	(UDPSocket *) pb->Owner();
	UDPiopb *		udp	=	pb->UDP();
	
	((miniwds *)udp->csParam.send.wdsPtr)->terminus	= udp->ioResult;
	((miniwds *)udp->csParam.send.wdsPtr)->ptr		= nil;
	
	sock->Ready();
}

#if USESROUTINEDESCRIPTORS
RoutineDescriptor	u_udp_send_done = 
		BUILD_ROUTINE_DESCRIPTOR(uppUDPIOCompletionProcInfo, udp_send_done);
#else
#define u_udp_send_done udp_send_done
#endif

#if !defined(powerc) && !defined(__powerc)
#pragma segment GUSIINET
#endif

/************************ UDPSocket members *************************/

UDPSocket::UDPSocket()
	:	INETSocket()
{
	sstate = SOCK_STATE_NO_STREAM;
}

UDPSocket::UDPSocket(StreamPtr stream)
	:	INETSocket(stream)
{
	OSErr			err;
	UDPiopb *	pb;

	pb												= GetPB();
	pb->ioCompletion							= UDPIOCompletionProc(&u_udp_read_ahead_done);
	pb->csCode 									= UDPRead;
	pb->csParam.receive.timeOut			= 0 /* infinity */;
	pb->csParam.receive.secondTimeStamp	= 0/* must be zero */;

	/* We know that there is a read pending, so we make a synchronous call */
	
	if (err = PBControlSync(ParmBlkPtr(pb)))
		TCP_error(err);
	
	if (!pb->ioResult) {
		recvBuf	= pb->csParam.receive.rcvBuff;
		recvd		= pb->csParam.receive.rcvBuffLen;
	} else {
		recvBuf	= nil;
		recvd		= 0;
	}
	asyncerr 				=	pb->ioResult;
	peer.sin_len 			= sizeof(struct sockaddr_in);
	peer.sin_family 		= AF_INET;
	peer.sin_addr.s_addr	=	pb->csParam.receive.remoteHost;
	peer.sin_port			=	pb->csParam.receive.remotePort;
}

UDPSocket::~UDPSocket()
{
	UDPiopb *	pb;
	
	if (sstate == SOCK_STATE_NO_STREAM)
		return;
	
	pb					= GetPB();	
	pb->csCode 		= UDPRelease;
	
	if (!PBControlSync(ParmBlkPtr(pb)))
		DisposPtr(pb->csParam.create.rcvBuff);
}

UDPiopb * UDPSocket::GetPB()
{
	AnnotatedPB *	pb		=	INETSockets.GetPB();
	pb->UDP()->ioCRefNum =	INETSockets.Driver();
	pb->UDP()->udpStream	=	stream;
	
	pb->SetOwner(this);
	
	return pb->UDP();
}

unsigned long UDPSocket::Available()
{
	return (asyncerr == inProgress) ? 0 : recvd;
}

int UDPSocket::NewStream()
{
	OSErr			err;
	UDPiopb *	pb;

	pb										= GetPB();
	pb->csCode 							= UDPCreate;
	pb->csParam.create.rcvBuff 	= (char *)NewPtr(STREAM_BUFFER_SIZE);
	pb->csParam.create.rcvBuffLen	= STREAM_BUFFER_SIZE;
	pb->csParam.create.notifyProc	= NULL;
	pb->csParam.create.localPort 	= sa.sin_port;
	
	if (err = PBControlSync(ParmBlkPtr(pb)))
		return TCP_error(err);
		
	stream 		= pb->udpStream;
	sa.sin_port = pb->csParam.create.localPort;
	
	sstate 	= SOCK_STATE_UNCONNECTED;
	recvd 	= 0;
	recvBuf 	= 0;
	asyncerr	= inProgress;

	return ReadAhead();
}

int UDPSocket::FlushReadAhead()
{
	OSErr			err;
	UDPiopb *	pb;

	/* flush the read-ahead buffer if its not from our new friend */
	pb										= GetPB();
	pb->ioCompletion					= nil;
	pb->csCode 							= UDPBfrReturn;
	pb->csParam.receive.rcvBuff	= recvBuf;
	recvBuf								= 0;
	recvd   								= 0;
	
	if (err = PBControlAsync(ParmBlkPtr(pb)))
		return TCP_error(err);
	else
		return 0;
}

int UDPSocket::ReadAhead()
{
	OSErr			err;
	UDPiopb *	pb;

	pb												= GetPB();
	pb->ioCompletion							= UDPIOCompletionProc(&u_udp_read_ahead_done);
	pb->csCode 									= UDPRead;
	pb->csParam.receive.timeOut			= 0 /* infinity */;
	pb->csParam.receive.secondTimeStamp	= 0/* must be zero */;
	asyncerr										= 1;
	
	if (err = PBControlAsync(ParmBlkPtr(pb)))
		return TCP_error(asyncerr = err);
	
	return 0;
}

/*
 *	getsockname(name, namelen)
 *
 *		getsockname returns the current name for the  socket.
 *		Namelen should  be initialized to
 *		indicate the amount of space pointed to by name.  On  return
 *		it contains the actual size of the name returned (in bytes).
 *		
 *		A 0 is returned if the call succeeds, -1 if it fails.
 */

int UDPSocket::getsockname(void *name, int *namelen)
{
	if (sstate == SOCK_STATE_NO_STREAM)
		if (NewStream())
			return -1;

	return INETSocket::getsockname(name, namelen);
}

/*
 *	UDPSocket::connect - initiate a connection on a MacTCP socket
 *
 *		This  call specifies the address to  which  datagrams
 *		are  to  be  sent, and the only address from which datagrams
 *		are to be received.  
 *			 
 *		UDP sockets may use connect() multiple times to change
 *		their association. UDP sockets may dissolve the association
 *		by connecting to an invalid address, such as a null
 *		address.
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

int UDPSocket::connect(void * address, int addrlen)
{
	struct sockaddr_in *	addr	= (struct sockaddr_in *) address;
		
	if (addrlen != int(sizeof(struct sockaddr_in)))
		return GUSI_error(EINVAL);

	if (addr->sin_family != AF_INET)
		return GUSI_error(EAFNOSUPPORT);
	
	/* make the stream if its not made already */
	if (sstate == SOCK_STATE_NO_STREAM) {
		if (NewStream())
			return -1;
	} else if (recvBuf)
		if (FlushReadAhead())
			return -1;
	
	/* record our peer */
	peer.sin_len 			= sizeof(struct sockaddr_in);
	peer.sin_addr.s_addr	= addr->sin_addr.s_addr;
	peer.sin_port 			= addr->sin_port;
	peer.sin_family 		= AF_INET;
	sstate 					= SOCK_STATE_CONNECTED;
	
	return 0;
}

/*	
 *	UDPSocket::recvfrom(s, buffer, buflen, flags, from, fromlen)
 *		
 *		recv() and recvfrom() attempt to receive a message (ie a datagram) 
 *		on the socket s. 
 *
 *		from returns the address of the socket which sent the message.
 *		fromlen is the usual value-result length parameter. 
 *		
 *		Typically, read() is used with a TCP stream and recv() with
 *		UDP where the idea of a message makes more sense. But in fact,
 *		read() and recv() are equivalent.
 *		
 *		If a message (ie. datagram) is too long to fit in the supplied 
 *		buffer, excess bytes will be discarded..
 *
 *		If no messages are available at the socket, the receive call
 *		waits for a message to arrive, unless the socket is non-
 *		blocking in which case -1 is returned with errno set to 
 *		EWOULDBLOCK.
 *
 *		Flags is ignored.
 *		
 *		If successful, the number of bytes actually received is
 *		returned. Otherwise, a -1 is returned and the global variable
 *		errno is set to indicate the error. 
 *		
 *		ESHUTDOWN    The socket has been shutdown for receive operations.
 */

int UDPSocket::recvfrom(void * buffer, int buflen, int, void * from, int * fromlen)
{
	/* make the stream if its not made already */
	if (sstate == SOCK_STATE_NO_STREAM)
		if (NewStream())
			return -1;
			
	/* dont block a non-blocking socket */
	if (nonblocking && asyncerr == 1)
		return GUSI_error(EWOULDBLOCK);
	
	SPIN(asyncerr == 1, SP_DGRAM_READ,0);

	if (asyncerr!=noErr)
		return TCP_error(asyncerr);

	/* return the data to the user - truncate the packet if necessary */
	buflen = min(buflen,recvd);	
	BlockMove(recvBuf, buffer, buflen);

	if (from && *fromlen >= int(sizeof(struct sockaddr_in))) {
		*(struct sockaddr_in *) from = peer;
		*fromlen = sizeof (struct sockaddr_in);
	}	
	
	/* continue the read-ahead - errors which occur */
	/* here will show up next time around */
	
	FlushReadAhead();
	ReadAhead();
	
	return buflen;
}

/*
 *	UDPSocket::sendto(s, buffer, buflen, flags, to, tolen)
 *		sendto() is used to transmit a message to another 
 *		socket on the socket s.
 *
 *		Typically, write() is used with a TCP stream and send() with
 *		UDP where the idea of a message makes more sense. But in fact,
 *		write() and send() are equivalent.
 *
 *    Write() and send() operations are completed as soon as the
 *    data is placed on the transmission queue.
 *
 *		The address of the target is given by to.
 *
 *		The message must be short enough to fit into one datagram.
 *		
 *		Buffer space must be available to hold the message to be 
 *		transmitted, regardless of its non-blocking I/O state.
 *
 *		Flags is ignored.
 *		
 *		These calls return the number of bytes sent, or -1 if an error 
 *		occurred.
 *		
 *		EINVAL           The sum of the iov_len values in the iov array was
 *						 		greater than 65535 (TCP) or 65507 (UDP) or there
 *                       were too many entries in the array (16 for TCP or
 *                       6 for UDP).
 *
 *		ESHUTDOWN        The socket has been shutdown for send operations.
 *		
 *		EMSGSIZE         The message is too big to send in one datagram. (UDP)
 *
 *		ENOBUFS          The transmit queue is full. (UDP)
 */

int UDPSocket::sendto(void * buffer, int count, int, void * to, int)
{
	miniwds  	awds;
	OSErr    	err;
	UDPiopb *	pb;

	/* make the stream if its not made already */
	if (sstate == SOCK_STATE_NO_STREAM)
		if (NewStream())
			return -1;
	
	if (count > UDP_MAX_MSG)
		return GUSI_error(EMSGSIZE);
		
	awds.terminus = 0;
	awds.length = count;
	awds.ptr = (char *) buffer;
	
	// if no address passed, hope we have one already in peer field
	if (to == NULL)
		if (peer.sin_len)
			to = &peer;
		else
			return GUSI_error(EHOSTUNREACH);
	
	pb										= GetPB();
	pb->ioCompletion					= UDPIOCompletionProc(&u_udp_send_done);
	pb->csCode 							= UDPWrite;
	pb->csParam.send.remoteHost 	= ((struct sockaddr_in *)to)->sin_addr.s_addr;
	pb->csParam.send.remotePort 	= ((struct sockaddr_in *)to)->sin_port;
	pb->csParam.send.wdsPtr 		= (Ptr)&awds;
	pb->csParam.send.checkSum 		= true;
	pb->csParam.send.sendLength 	= 0/* must be zero */;
	
	if (err = PBControlAsync(ParmBlkPtr(pb)))
		return TCP_error(err);
	
	// get sneaky. compl. proc sets ptr to nil on completion, and puts result code in
	// terminus field.
	
	SPIN(awds.ptr != NULL, SP_DGRAM_WRITE, count);
	
	if (awds.terminus < 0)
		return TCP_error(awds.terminus);
	else
		return count;
}

int UDPSocket::select(Boolean * canRead, Boolean * canWrite, Boolean *)
{
	int		goodies 	= 	0;
	
	if (canRead || canWrite)
		if (sstate == SOCK_STATE_NO_STREAM)
			NewStream();
				
	if (canRead)
		if (asyncerr != 1) {
			*canRead = true;
			++goodies;
		}
		
	if (canWrite) {
		*canWrite = true;
		++goodies;
	}
	
	return goodies;
}
