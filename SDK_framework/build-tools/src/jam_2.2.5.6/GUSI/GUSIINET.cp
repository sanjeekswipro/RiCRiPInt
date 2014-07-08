/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIINET.cp		-	TCP/IP Sockets, general routines
Author	:	Matthias Neeracher

	This file was derived from the socket library by 
	
		Charlie Reiman	<creiman@ncsa.uiuc.edu> and
		Tom Milligan	<milligan@madhaus.utcs.utoronto.ca>
		  
Language	:	MPW C/C++

$Log: GUSIINET.cp,v $
Revision 1.2  1999/04/23  08:43:39  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.4  1994/12/30  20:05:05  neeri
Remove built in INETd support.
Wake up process from completion routines to improve performance.
Fix problem occurring when a netdb routine was called before the first socket().

Revision 1.3  1994/08/10  00:26:25  neeri
Sanitized for universal headers.

Revision 1.2  1994/05/01  23:39:54  neeri
Defer PB allocation.

Revision 1.1  1994/03/08  22:06:58  neeri
Initial revision

Revision 0.7  1993/10/31  00:00:00  neeri
Deferred opening of MacTCP Services

Revision 0.6  1993/07/17  00:00:00  neeri
htonl and friends

Revision 0.5  1993/02/07  00:00:00  neeri
New configuration technique

Revision 0.4  1993/01/31  00:00:00  neeri
Inetd support

Revision 0.3  1992/08/23  00:00:00  neeri
INETSocket::Available()

Revision 0.2  1992/08/20  00:00:00  neeri
Wrote most of the functions

Revision 0.1  1992/08/16  00:00:00  neeri
Split

*********************************************************************/

#include "GUSIINET_P.h"
#include <machine/endian.h>

#include <LowMem.h>

#if !defined(powerc) && !defined(__powerc)
#pragma segment GUSIINET
#endif

/***************************** Globals ******************************/

#define NUM_PBS	32

INETSocketDomain	INETSockets;

/***************************** Peanuts ******************************/

unsigned long (htonl)(unsigned long h)
{
	return h;
}

unsigned short	(htons)(unsigned short h)
{
	return h;
}

unsigned long (ntohl)(unsigned long n)
{
	return n;
}

unsigned short	(ntohs)(unsigned short n)
{
	return n;
}

/***************** Our resident MacTCP error expert *****************/

/*
 * Convert a MacTCP err code into a unix error code.
 */
 
int TCP_error(int MacTCPerr)
{
	switch ( MacTCPerr ) {
	case 0:
		return 0;
	case ipBadLapErr:
	case ipBadCnfgErr:
	case ipNoCnfgErr:
	case ipLoadErr:
	case ipBadAddr:
		errno = ENXIO;			/* device not configured */	/* a cheap cop out */
		break;
	case connectionClosing:
		errno = ESHUTDOWN;		/* Can't send after socket shutdown */
		break;
	case connectionExists:
		errno = EISCONN;		/* Socket is already connected */
		break;
	case connectionTerminated:
		errno = ENOTCONN;		/* Connection reset by peer */  /* one of many possible */
		break;
	case openFailed:
		errno = ECONNREFUSED;	/* Connection refused */
		break;
	case duplicateSocket:		/* technically, duplicate port */
		errno = EADDRINUSE;		/* Address already in use */
		break;
	case ipDestDeadErr:
		errno = EHOSTDOWN;		/* Host is down */
		break;
	case ipRouteErr:
		errno = EHOSTUNREACH;	/* No route to host */
		break;
	default:
		errno = MacTCPerr > 0 ? MacTCPerr : EFAULT;		/* cop out; an internal err, unix err, or no err */
		break;
	}

	return -1;
}

/************************ INETSocket members ************************/

INETSocket::INETSocket()
	: Socket()
{
	bzero(&sa, sizeof(struct sockaddr_in));
	bzero(&peer, sizeof(struct sockaddr_in));
	
	sa.sin_family	= AF_INET;
	sa.sin_len		= sizeof(struct sockaddr_in);
	status			= SOCK_STATUS_USED;
	nonblocking		= false;	

#if !defined(powerc) && !defined (__powerc)
	processA5		= LMGetCurrentA5();
#endif	
	
	INETSockets.OpenSocket();
}

#if !defined(powerc) && !defined (__powerc)
void INETSocket::Ready() 
{
	long saveA5 = SetA5(long(processA5));
	
	INETSockets.Ready();
	
	SetA5(saveA5);
}
#endif	

INETSocket::INETSocket(StreamPtr stream)
	: Socket(), stream(stream)
{
	sa.sin_family	= AF_INET;
	sa.sin_len		= sizeof(struct sockaddr_in);
	peer.sin_family= AF_INET;
	peer.sin_len	= sizeof(struct sockaddr_in);
	status			= SOCK_STATUS_USED;
	sstate			= SOCK_STATE_CONNECTED;
	nonblocking		= false;	
	
	INETSockets.OpenSocket();

#if !defined(powerc) && !defined (__powerc)
	processA5		= LMGetCurrentA5();
#endif	
}

INETSocket::~INETSocket()
{
	INETSockets.CloseSocket();
}

unsigned long INETSocket::Available()
{
	return 0;
}

/*
 *	bind(name, namelen)
 *
 *		bind requests that the name (ip address and port) pointed to by 
 *		name be assigned to the socket.
 *		
 *		The return value is 0 on success or -1 if an error occurs,
 *		in which case global variable errno is set to one of:
 *
 *		EAFNOSUPPORT        The address family in name is not AF_INET.
 *		
 *		EINVAL              The socket is already bound to an address.
 *		
 *		EADDRNOTAVAIL       The specified address is  not  available
 * 	                        from the local machine. ie. the address
 *	                        portion of name was not this machine's address.
 *
 *		MacTCP does not separate name binding and connection establishment.
 *		Therefore the port number is not verified, just stored for later use.
 *
 *		If a specific local port is not required, bind is optional in this
 *		implementation.
 */	

int INETSocket::bind(void * addr, int namelen)
{
	struct sockaddr_in *name	=	(struct sockaddr_in *)addr;
		
	if (namelen < int(sizeof(struct sockaddr_in)))
		return GUSI_error(EINVAL);

	if (name->sin_family != AF_INET)
		return GUSI_error(EAFNOSUPPORT);

	if (sa.sin_port != 0) /* already bound */
		return GUSI_error(EINVAL);

	/*
	 *	If client passed a local IP address, assure it is the right one
	 */
	if (name->sin_addr.s_addr != 0) 
	{
		struct GetAddrParamBlock pbr;
		
		pbr.ioCRefNum 	= INETSockets.Driver();
		pbr.csCode 		= ipctlGetAddr;
		
		if (PBControlSync(ParmBlkPtr(&pbr)))
			return GUSI_error(ENETDOWN);
		
		if (name->sin_addr.s_addr != pbr.ourAddress)
			return GUSI_error(EADDRNOTAVAIL);
	}

	/*
	 *	NOTE: can't check a TCP port for EADDRINUSE
	 *	just save the address and port away for connect or listen or...
	 */
	sa.sin_addr.s_addr 	= name->sin_addr.s_addr;
	sa.sin_port 			= name->sin_port;
	
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

int INETSocket::getsockname(void *name, int *namelen)
{
	if (*namelen < 0)
		return GUSI_error(EINVAL);

	memcpy(name, &sa, *namelen = min(*namelen, int(sizeof(struct sockaddr_in))));
	return 0;
}

/*
 *	getpeername(name, namelen)
 *
 *		getpeername returns the name of the peer connected to socket.
 *
 *		The  int  pointed  to  by the namelen parameter
 *		should be  initialized  to  indicate  the  amount  of  space
 *		pointed  to  by name.  On return it contains the actual size
 *		of the name returned (in bytes).  The name is  truncated  if
 *		the buffer provided is too small.
 *		
 *		A 0 is returned if the call succeeds, -1 if it fails.
 */

int INETSocket::getpeername(void *name, int *namelen)
{
	if (*namelen < 0)
		return GUSI_error(EINVAL);

	memcpy(name, &peer, *namelen = min(*namelen, int(sizeof(struct sockaddr_in))));
	return 0;
}

/*
 *	shutdown(how)
 *
 *		shutdown call causes all or part of a full-duplex
 *		connection on the socket to be shut down.  If
 *		how is 0, then further receives will be disallowed.  If  how
 *		is  1,  then further sends will be disallowed.  If how is 2,
 *		then further sends and receives will be disallowed.
 *		
 *		A 0 is returned if the call succeeds, -1 if it fails.
 */

int INETSocket::shutdown(int how)
{
	switch(how) {
	case 0 : 
		status |= SOCK_STATUS_NOREAD;
		break;
	case 1 : 
		status |= SOCK_STATUS_NOWRITE;
		break;
	case 2 :
		status |= SOCK_STATUS_NOREAD | SOCK_STATUS_NOWRITE;
		break;
	default :
		return GUSI_error(EINVAL);
	}
	
	return 0;
}

/*
 *	fcntl() operates on the socket according to the order in cmd:
 *
 *		F_GETFL	returns the descriptor status flags. The only
 *				flag supported is FNDELAY for non-blocking i/o.
 *
 *		F_SETFL	sets descriptor status flags. The only
 *		 		flag supported is FNDELAY for non-blocking i/o.
 *
 *		Upon successful completion, the value  returned  depends  on
 *		cmd as follows:
 * 		F_GETFL   Value of flags.
 *			F_SETFL   0.
 *
 *		On error, a value of -1  is returned and errno is set to indicate 
 *		the error.
 *
 *		EBADF           s is not a valid open descriptor.
 *
 *		EMFILE          cmd is F_DUPFD and socket descriptor table is full.
 *
 *		EINVAL          cmd is F_DUPFD and arg  is  negative  or
 *                      greater   than   the  maximum  allowable
 *                      number (see getdtablesize).
 */
int INETSocket::fcntl(unsigned int cmd, int arg)
{
	switch(cmd) {
	/*
	 *  Get socket status.  This is like getsockopt().
	 *  Only supported descriptor status is FNDELAY.
	 */
	case F_GETFL : 
		if (nonblocking)
			return FNDELAY;
		else
			return 0;
	/*
	 *  Set socket status.  This is like setsockopt().
	 *  Only supported descriptor status is FNDELAY.
	 */
	case F_SETFL : 
		if (arg & FNDELAY)
			nonblocking = true;
		else
			nonblocking = false;
		
		return 0;
	default:
		return GUSI_error(EOPNOTSUPP);
	}
}

int INETSocket::ioctl(unsigned int request, void *argp)
{
	struct ifreq *	ifr;
	int				size;
	
	/*
	 * Interpret high order word to find amount of data to be copied 
	 * to/from the user's address space.
	 */
	size =(request &~(IOC_INOUT | IOC_VOID)) >> 16;
	
	/*
	 * Zero the buffer on the stack so the user gets back something deterministic.
	 */
	if ((request & IOC_OUT) && size)
		bzero((Ptr)argp, size);

	ifr =(struct ifreq *)argp;
	switch(request) {
	/* Non-blocking I/O */
	case FIONBIO:
		nonblocking = (Boolean) *(int *) argp;
		return 0;
	/* Number of bytes on input Q */
	case FIONREAD:
		*(unsigned long *) argp	= Available();
		
		return 0;
	default :
		return GUSI_error(EOPNOTSUPP);
	}
}

/********************* INETSocketDomain members *********************/

extern "C" void GUSIwithInternetSockets()
{
	INETSockets.DontStrip();
}

INETSocketDomain::INETSocketDomain()
	:	SocketDomain(AF_INET)	
{
	GUSIConfiguration	conf;		// GUSIConfig isn't yet guaranteed to work	
	
	driverState		= 1;
	resolverState	= 1;
	drvrRefNum 		= 0;
	inetCount		= 0;
	
	/* allocate storage for pbs */
	pbLast = 0;
	pbList = nil;
}

short INETSocketDomain::Driver()
{
	ParamBlockRec 		pb; 

	if (driverState == 1) {
		pb.ioParam.ioCompletion	= 0L; 
		pb.ioParam.ioNamePtr 	= (StringPtr) "\p.IPP"; 
		pb.ioParam.ioPermssn 	= fsCurPerm;
		
		driverState 				= PBOpenSync(&pb);
		drvrRefNum 					= pb.ioParam.ioRefNum; 
	}
	
	return driverState ? 0 : drvrRefNum;
}

OSErr	INETSocketDomain::Resolver()
{
	Driver();
	if (resolverState == 1)
		resolverState = OpenResolver(nil);
	
	return resolverState;
}

/*
 *	INETSocketDomain::socket(type, protocol)
 *
 *		Create a MacTCP socket and return a descriptor.
 *
 *		Type may be SOCK_STREAM to create a TCP socket or 
 *		SOCK_DGRAM to create a UDP socket.
 *				 
 *		Protocol is ignored. (isn't it always?)
 *				 
 *		TCP sockets provide sequenced, reliable, two-way connection
 *		based byte streams.
 *
 *		A TCP socket must be in a connected
 *		state before any data may be sent or received on it. A 
 *		connection to another socket is created with a connect() call
 *		or the listen() and accept() calls.
 *		Once connected, data may be transferred using read() and
 *		write() calls or some variant of the send() and recv()
 *		calls. When a session has been completed a close() may  be
 *		performed.
 *
 *		
 *		A UDP socket supports the exchange of datagrams (connectionless, 
 *		unreliable messages of a fixed maximum length) with  
 *		correspondents named in send() calls. Datagrams are
 *		generally received with recv(), which returns the next
 *		datagram with its return address.
 *
 *		An fcntl() or ioctl() call can be used to enable non-blocking I/O.
 *
 *		The return value is a descriptor referencing the socket or -1
 *		if an error occurs, in which case global variable errno is
 *		set to one of:
 *
 *			ENOMEM					Failed to allocate memory for the socket
 *                              data structures.
 *
 *			ESOCKTNOSUPPORT     Type wasn't SOCK_STREAM or SOCK_DGRAM.
 *
 *			EMFILE              The socket descriptor table is full.
 */

Socket * INETSocketDomain::socket(int type, short)
{
	INETSocket * sock	=	nil;
	
	errno	=	0;

	if (!Driver())
		return (Socket *) GUSI_error_nil(ENETDOWN);
		
	switch (type)	{
	case SOCK_STREAM:
		sock = new TCPSocket();
		break;
	case SOCK_DGRAM:
		sock = new UDPSocket();
		break;
	default:
		GUSI_error(ESOCKTNOSUPPORT);
	}	
	
	if (sock && errno)	{
		delete sock;
		
		return nil;
	} 
		
	return sock;
}

AnnotatedPB * INETSocketDomain::GetPB()
{
	AnnotatedPB	*	curPB;
	
	do {
		if (++pbLast == NUM_PBS)
			pbLast = 0;
		
		curPB = pbList + pbLast;
	} while (curPB->Busy());
	
	return curPB;
}

void INETSocketDomain::OpenSocket()
{
	if (!inetCount++) 
		if (!(pbList = new AnnotatedPB[NUM_PBS])) {
			errno		 	= 	ENOMEM;
			inetCount	=	0;
		}
}

void INETSocketDomain::CloseSocket()
{
	if (!--inetCount) {
		delete [] pbList;
		
		pbList = nil;
	}
}
