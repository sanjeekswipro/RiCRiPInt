/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIINET_P.h	-	Common definitions for TCP/IP Sockets
Author	:	Matthias Neeracher

	This file was derived from the socket library by 
	
		Charlie Reiman	<creiman@ncsa.uiuc.edu> and
		Tom Milligan	<milligan@madhaus.utcs.utoronto.ca>
		  
Language	:	MPW C/C++

$Log: include:GUSIINET_P.h,v $
Revision 1.2  1999/04/23  08:57:04  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.4  1994/12/31  02:17:14  neeri
Wakeup gear.

Revision 1.3  1994/08/10  00:39:17  neeri
Sanitized for universal headers.

Revision 1.2  1994/05/01  23:50:43  neeri
INETScoketDomain::Open/CloseSocket()

Revision 1.1  1994/02/25  02:57:11  neeri
Initial revision

Revision 0.7  1993/09/01  00:00:00  neeri
Turn terminus into a short

Revision 0.6  1993/06/17  00:00:00  neeri
UDPSocket::getsockname()

Revision 0.5  1993/01/31  00:00:00  neeri
Support for inetd

Revision 0.4  1993/01/21  00:00:00  neeri
Remove torecv

Revision 0.3  1993/01/08  00:00:00  neeri
tcp_notify was setting the wrong stream to unconnected

Revision 0.2  1992/08/23  00:00:00  neeri
Available()

Revision 0.1  1992/08/20  00:00:00  neeri
First approach

*********************************************************************/

#include "GUSI_P.h"

#include <Devices.h>
#include <MacTCPCommonTypes.h>
#include <TCPPB.h>
#include <UDPPB.h>
#include <MiscIPPB.h>
#include <AddressXlation.h>
#include <GetMyIPAddr.h>

#define STREAM_BUFFER_SIZE	8192
#define UDP_MAX_MSG			65507	/* MacTCP max legal udp message */
#define TCP_MAX_MSG			65535	/* MacTCP max legal tcp message */

#define TCP_MAX_WDS		4		/* arbitrary number of wds to alloc in sock_tcp_send */

class INETSocket : public Socket	{				// That's what this file's all about
	friend class INETSocketDomain;	

#if !defined(powerc) && !defined (__powerc)
	Ptr						processA5;	/* Our A5 world */
#endif	

protected:
	StreamPtr				stream;		/* stream pointer */
	byte						status;		/* Is file descriptor in use */
	Boolean					nonblocking;/* socket set for non-blocking I/O. */
	char *					recvBuf;		/* receive buffer */
	int						recvd;		/* amount received */
	struct sockaddr_in	sa;			/* My address. */
	struct sockaddr_in	peer;			/* Her address. */
	byte						sstate;		/* socket's connection state. */
	int						asyncerr;	/* Last async error to arrive.  zero if none. */
					INETSocket();
					INETSocket(StreamPtr stream);
					
	virtual 		~INETSocket();
	
	virtual u_long	Available();
public:
	void			Ready();

	virtual int		bind(void * name, int namelen);
	virtual int 	getsockname(void * name, int * namelen);
	virtual int 	getpeername(void * name, int * namelen);
	virtual int		fcntl(unsigned int cmd, int arg);
	virtual int		ioctl(unsigned int request, void *argp);
	virtual int 	shutdown(int how);
};	

class AnnotatedPB;

class TCPSocket : public INETSocket	{	
	friend class INETSocketDomain;
	friend class INETdSocketDomain;
	friend pascal void tcp_notify(StreamPtr, u_short, Ptr, u_short, struct ICMPReport *);
	friend void tcp_connect_done(AnnotatedPB *);
	friend void tcp_listen_done(AnnotatedPB *);
	friend void tcp_recv_done(AnnotatedPB *);
	friend void tcp_send_done(AnnotatedPB *);

// Since an userDataPtr may not be changed during the lifetime of a stream, we need
// this indirection. The class invariant *(this->self) == this holds.

	TCPSocket **	self;

						TCPSocket();
						TCPSocket(StreamPtr stream);
						TCPSocket(TCPSocket * sock);
					
	virtual 			~TCPSocket();
	TCPiopb *		GetPB();
	virtual u_long	Available();
public:
	virtual int 	connect(void * address, int addrlen);
	virtual int 	listen(int qlen);
	virtual Socket * accept(void * address, int * addrlen);
	virtual int 	recvfrom(void * buffer, int buflen, int flags, void * from, int * fromlen);
	virtual int 	sendto(void * buffer, int buflen, int flags, void * to, int tolen);
	virtual int 	select(Boolean * canRead, Boolean * canWrite, Boolean * exception);
};	

class UDPSocket : public INETSocket	{	
	friend class INETSocketDomain;	
	friend class INETdSocketDomain;	
	friend void udp_read_ahead_done(AnnotatedPB *);

						UDPSocket();
						UDPSocket(StreamPtr stream);
	virtual 			~UDPSocket();
	UDPiopb *		GetPB();
	virtual u_long	Available();
	int				NewStream();
	int				FlushReadAhead();
	int				ReadAhead();
public:
	virtual int 	getsockname(void * name, int * namelen);
	virtual int 	connect(void * address, int addrlen);
	virtual int 	recvfrom(void * buffer, int buflen, int flags, void * from, int * fromlen);
	virtual int 	sendto(void * buffer, int buflen, int flags, void * to, int tolen);
	virtual int 	select(Boolean * canRead, Boolean * canWrite, Boolean * exception);
};	

class AnnotatedPB {
	union {
		TCPiopb		tcp;
		UDPiopb		udp;
	} pb;
	INETSocket	*	sock;
public:
	void				SetOwner(INETSocket * s)		{	sock	=	s;							}
	INETSocket *	Owner()								{	return sock;						}
	TCPiopb *		TCP()									{	return &pb.tcp;					}
	UDPiopb *		UDP()									{	return &pb.udp;					}
	Boolean			Busy()								{	return pb.tcp.ioResult == 1;	}
};

#if defined(powerc) || defined (__powerc)
#pragma options align=mac68k
#endif
struct miniwds {
	u_short	length;
	char * 	ptr;
	short		terminus;	/* must be zero'd for use */
};
#if defined(powerc) || defined(__powerc)
#pragma options align=reset
#endif

class INETSocketDomain : public SocketDomain {
	OSErr				driverState;
	OSErr				resolverState;
	short				drvrRefNum;
	short				inetCount;							/* # of existing sockets */
	short 			pbLast;								/* last pb used */
	AnnotatedPB *	pbList;								/* The pb array */
public:
	INETSocketDomain();
	
	short				Driver();
	OSErr				Resolver();
	AnnotatedPB *	GetPB();
	void 				OpenSocket();
	void				CloseSocket();
	
	virtual Socket * 	socket(int type, short protocol);
};

extern INETSocketDomain	INETSockets;

int TCP_error(int MacTCPerr);

#if defined(powerc) || defined (__powerc)
inline void INETSocket::Ready() 
{
	INETSockets.Ready();
}
#endif	
