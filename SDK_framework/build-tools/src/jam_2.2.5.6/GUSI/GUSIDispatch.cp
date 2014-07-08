/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIDispatch.cp-	Dispatch calls to their correct recipient
Author	:	Matthias Neeracher
Language	:	MPW C/C++

$Log: GUSIDispatch.cp,v $
Revision 1.2  1999/04/23  08:43:33  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.5  1995/01/08  21:53:17  neeri
InitConsole() on close.

Revision 1.4  1994/12/30  19:48:09  neeri
Remove (theoretical) support for pre-System 6 systems.
Remove built-in support for INETd.
Fix problems in connection with ROM PowerPC library.
Move open() to GUSIFileDispatch.cp.
Support AF_UNSPEC domains.
More work on spinning performance.

Revision 1.3  1994/08/10  00:30:30  neeri
Sanitized for universal headers.
Prevent overly fast spinning.

Revision 1.2  1994/05/01  23:47:34  neeri
Extend fflush() kludge.
Define _lastbuf for MPW 3.2 compatibility.

Revision 1.1  1994/02/25  02:28:36  neeri
Initial revision

Revision 0.27  1993/11/24  00:00:00  neeri
Flush stdio before closing

Revision 0.26  1993/11/22  00:00:00  neeri
Extend two time loser for EBADF

Revision 0.25  1993/11/12  00:00:00  neeri
Two time loser workaround for flush bug

Revision 0.24  1993/06/27  00:00:00  neeri
{pre,post}_select

Revision 0.23  1993/06/27  00:00:00  neeri
ftruncate

Revision 0.22  1993/06/20  00:00:00  neeri
Further subtleties in console handling 

Revision 0.21  1993/05/21  00:00:00  neeri
Suffixes

Revision 0.20  1993/05/15  00:00:00  neeri
Try to keep errno always set on error returns

Revision 0.19  1993/05/13  00:00:00  neeri
Limit Search for configuration resource to application

Revision 0.18  1993/01/31  00:00:00  neeri
Introducing daemons (pleased to meet you, hope you guess my name)

Revision 0.17  1993/01/17  00:00:00  neeri
Be more careful about user aborts.

Revision 0.16  1993/01/03  00:00:00  neeri
GUSIConfiguration

Revision 0.15  1992/11/25  00:00:00  neeri
Still trying to get standard descriptors for standalone programs right. sigh.

Revision 0.14  1992/10/05  00:00:00  neeri
Small fix in event dispatching

Revision 0.13  1992/09/12  00:00:00  neeri
getdtablesize()

Revision 0.12  1992/08/30  00:00:00  neeri
Move hasPPC to GUSIPPC.cp, AppleTalkIdentity

Revision 0.11  1992/08/05  00:00:00  neeri
Change the way standard I/O channels are opened

Revision 0.10  1992/08/03  00:00:00  neeri
Move Scatter/Gather to GUSIBuffer.cp

Revision 0.9  1992/07/30  00:00:00  neeri
Features with initializers

Revision 0.8  1992/07/13  00:00:00  neeri
hasProcessMgr

Revision 0.7  1992/06/27  00:00:00  neeri
choose(), hasNewSF

Revision 0.6  1992/06/06  00:00:00  neeri
Feature

Revision 0.5  1992/04/19  00:00:00  neeri
C++ Rewrite

Revision 0.4  1992/04/18  00:00:00  neeri
Changed read/write/send/recv dispatchers

Revision 0.3  1992/04/17  00:00:00  neeri
Spin routines

Revision 0.2  1992/04/16  00:00:00  neeri
User interrupt stuff

Revision 0.1  1992/03/31  00:00:00  neeri
unix domain socket calls

*********************************************************************/

#include "GUSIFile_P.h"
#include "GUSIMPW_P.h"
#include <IOCtl.h>
#include <SetJmp.h>
#include <Signal.h>
#include <CursorCtl.h>
#include <Resources.h>
#include <Events.h> 
#include <Windows.h>
#include <Desk.h>
#include <Script.h>
#include <OSEvents.h>
#include <Traps.h>
#include <CommResources.h>
#include <CTBUtilities.h>
#include <Connections.h>
#include <FileTransfers.h>
#include <Terminals.h>
#include <EPPC.h>
#include <PLStringFuncs.h>
#include <LowMem.h>
#include <Processes.h>

#if defined(powerc) || defined(__powerc)
#include <FragLoad.h>
#endif

/***************************** Globals ******************************/

const GUSIConfiguration GUSIConfig;		// Change the order of these declarations
SocketTable					Sockets;			// 	and you'll regret it (ARM ¤12.6.1)
GUSISpinFn 					GUSISpin 	= GUSIDefaultSpin;
static GUSIEvtHandler *	evtHandler	= nil;
static short				evtMask		= 0;
static int					errorSock	= -1;
static int					errorType	= 0;
static int					errorCount	= 0;
const int					errorMax		= 3;
Boolean						CatchStdIO	= false;

Feature 	hasMakeFSSpec(
				gestaltFSAttr,
				(1<<gestaltHasFSSpecCalls),
				(1<<gestaltHasFSSpecCalls));
Feature 	hasAlias(
				gestaltAliasMgrAttr,
				(1<<gestaltAliasMgrPresent),
				(1<<gestaltAliasMgrPresent));
Feature	hasNewSF(
				gestaltStandardFileAttr,
				(1<<gestaltStandardFile58),
				(1<<gestaltStandardFile58));
Feature 	hasProcessMgr(
				gestaltOSAttr,
				(1<<gestaltLaunchControl),
				(1<<gestaltLaunchControl));
Feature hasCRM_P(
				gestaltCRMAttr,
				(1<<gestaltCRMPresent),
				(1<<gestaltCRMPresent));
Feature hasCRM(hasCRM_P, InitCRM);
Feature hasCTB(hasCRM, InitCTBUtilities);
Feature hasStdNBP_P(
				gestaltStdNBPAttr,
				(1<<gestaltStdNBPPresent),
				(1<<gestaltStdNBPPresent));
Feature hasStdNBP(hasCTB, hasStdNBP_P);
Feature hasCM(hasCTB, InitCM);
Feature hasFT(hasCTB, InitFT);
Feature hasTM(hasCTB, InitTM);
Feature hasAppleEvents(
				gestaltAppleEventsAttr,
				(1<<gestaltAppleEventsPresent),
				(1<<gestaltAppleEventsPresent));
Feature hasRevisedTimeMgr(
			gestaltTimeMgrVersion,
			2L);

/*********************** GUSIConfiguration members ************************/

Boolean 	GUSIConfiguration::firstTime = false;
short		GUSIConfiguration::we;

GUSIConfiguration::GUSIConfiguration()
{
	typedef GUSIConfiguration **	GUSIConfHdl;
	short	oldResFile = CurResFile();
	
	if (!firstTime)
		we = oldResFile;
	else
		UseResFile(we);
		
	GUSIConfHdl config 	=	GUSIConfHdl(Get1Resource('GU·I', GUSIRsrcID));
	long			confSize	=	config ? GetHandleSize(Handle(config)) : 0;
	
	if (confSize)
		*this = **config;
	
	if (confSize < 4 || !defaultType)
		defaultType	=	'TEXT';
	if (confSize < 8 || !defaultCreator)
		defaultCreator	=	'MPS ';
	if (confSize < 9) 
		autoSpin	=	1;			// do automatic spin on read/write
	if (confSize < 10) {
		noChdir	=	false;	// Use chdir()
		accurStat=	false;	// st_nlink = # of entries + 2
		hasConsole=	false;
	}
	if (confSize < 14)
		version = '0102';
	if (version < '0120')
		numSuffices = 0;
	
	if (!numSuffices)
		suffices = nil;
	else if (suffices = new GUSISuffix[numSuffices]) {
		memcpy(suffices, &(*config)->numSuffices+1, numSuffices*sizeof(GUSISuffix));
		for (int i=0; i<numSuffices; i++)
			for (int j=0; j<4; j++)
				if (((char *) (suffices+i))[j] == ' ')
					((char *) (suffices+i))[j] = 0;
	}
	
	if (!firstTime) {
		firstTime	=	true;
		
		if (!noChdir)
			chdir(":");
	} else
		UseResFile(oldResFile);
}

void GUSIConfiguration::SetDefaultFType(const TFileSpec & name) const
{
	FInfo	info;	

	if (HGetFInfo(name.vRefNum, name.parID, name.name, &info))
		return;

	Ptr dot = PLstrrchr(name.name, '.');
	
	if (dot && (name.name[0] - (dot-Ptr(name.name))) <= 4) {
		char searchsuffix[5];
		
		strncpy(searchsuffix, dot+1, name.name[0] - (dot-Ptr(name.name)));
		
		for (int i = 0; i<numSuffices; i++)
			if (!strncmp(suffices[i].suffix, searchsuffix, 4)) {
				info.fdType 	=	suffices[i].suffType;
				info.fdCreator	=	suffices[i].suffCreator;
				
				goto determined;
			}
	}

	info.fdType 	=	defaultType;
	info.fdCreator	=	defaultCreator;

determined:	
	HSetFInfo(name.vRefNum, name.parID, name.name, &info);
}

inline void GUSIConfiguration::DoAutoSpin() const 
{
	if (autoSpin)
		SAFESPIN(0, SP_AUTO_SPIN, autoSpin);
}

/************************ Handle nonstandard consoles *************************/

static void InitConsole()
{
	CatchStdIO = true;
	file_ioctl(0, FIOINTERACTIVE, 0);
	file_ioctl(1, FIOINTERACTIVE, 0);
	file_ioctl(2, FIOINTERACTIVE, 0);
	CatchStdIO = false;
	freopen("dev:console", "r", stdin);
	freopen("dev:console", "w", stdout);
	freopen("dev:console", "w", stderr); 
	
	stderr->_flag |= _IOLBF;
}

void SocketTable::InitConsole()
{
	if (needsConsole) {
		needsConsole = false;
		::InitConsole();
	}
}

/************************ External routines *************************/

int getdtablesize()
{
	return GUSI_MAX_FD;
}

int socket(int domain, int type, int protocol)
{
	SocketDomain *	dom;
	Socket * 		sock;
	int				fd;

	Sockets.InitConsole();
	
	if (dom = SocketDomain::Domain(domain))
		if (sock = dom->socket(type, protocol))
			if ((fd = Sockets.Install(sock)) != -1)
				return fd;
			else
				delete sock;

	if (!errno)
		return GUSI_error(ENOMEM);
	else
		return -1;
}

int choose(int domain, int type, char * prompt, void * constraint, int flags, void * name, int * namelen)
{
	SocketDomain *	dom;

	if (dom = SocketDomain::Domain(domain))
		return dom->choose(type, prompt, constraint, flags, name, namelen);

	return -1;
}

int bind(int s, const struct sockaddr *name, int namelen)
{
	Socket *	sock	=	Sockets[s];

	return sock ? sock->bind((void *) name, namelen) : -1;
}

int connect(int s, const struct sockaddr *addr, int addrlen)
{
	Socket *	sock	=	Sockets[s];

	return sock ? sock->connect((void *) addr, addrlen) : -1;
}

int listen(int s, int qlen)
{
	Socket *	sock	=	Sockets[s];

	return sock ? sock->listen(qlen) : -1;
}

int accept(int s, struct sockaddr *addr, int *addrlen)
{
	Socket *	sock	=	Sockets[s];

	if (sock)
		if (sock	= sock->accept(addr, addrlen))
			if ((s = Sockets.Install(sock)) != -1)
				return s;
			else
				delete sock;

	return -1;
}

int close(int s)
{
	errorSock	=	-1;
	
	return Sockets.Remove(s);
}

int read(int s, char *buffer, unsigned buflen)
{
	GUSIConfig.DoAutoSpin();
	
	Socket *	sock	=	Sockets[s];

	return sock ? sock->read(buffer, buflen) : -1;
}

int readv(int s, const struct iovec *iov, int count)
{
	GUSIConfig.DoAutoSpin();
	
	Socket *	sock	=	Sockets[s];

	if (sock)	{
		Scatterer	scatt(iov, count);

		if (scatt)
			return scatt.length(sock->read(scatt.buffer(), scatt.buflen()));
		else
			return GUSI_error(ENOMEM);
	} else
		return -1;
}

int recv(int s, void *buffer, int buflen, int flags)
{
	GUSIConfig.DoAutoSpin();
	
	int 		fromlen 	=	0;
	Socket *	sock		=	Sockets[s];

	return sock ? sock->recvfrom(buffer, buflen, flags, nil, &fromlen) : -1;
}

int recvfrom(int s, void *buffer, int buflen, int flags, struct sockaddr *from, int *fromlen)
{
	GUSIConfig.DoAutoSpin();
	
	Socket *	sock	=	Sockets[s];

	return sock ? sock->recvfrom(buffer, buflen, flags, from, fromlen) : -1;
}

int recvmsg(int s, struct msghdr *msg, int flags)
{
	GUSIConfig.DoAutoSpin();
	
	Socket *	sock	=	Sockets[s];

	if (sock)	{
		Scatterer	scatt(msg->msg_iov, msg->msg_iovlen);

		if (scatt)
			return
				scatt.length(
					sock->recvfrom(
						scatt.buffer(),
						scatt.buflen(),
						flags,
						msg->msg_name,
						(int *)&msg->msg_namelen));
		else
			return GUSI_error(ENOMEM);
	} else
		return -1;
}

int write(int s, const char *buffer, unsigned buflen)
{
	/* fflush() in the MPW stdio library doesn't take no for an answer.
		Our workaround is to treat a second subsequent ESHUTDOWN or EBADF as 
		an invitation to lie by pretending the write worked.
	*/
	
	int	len;
	
	GUSIConfig.DoAutoSpin();
	
	Socket *	sock	=	Sockets[s];

	if (sock && (len = sock->write((char *) buffer, buflen)) != -1)
		return len;
		
	switch (errno) {
	case EINTR:
	case EWOULDBLOCK:
	case EINPROGRESS:
	case EALREADY:
		break;
	default:
		if (errorSock == s && errorType == errno) {
			if (++errorCount == errorMax) {
				errorSock = -1;
			
				return buflen;
			}
		} else {
			errorSock = s;
			errorType = errno;
			errorCount= 1;
		}
	}
	return -1;
}

int writev(int s, const struct iovec *iov, int count)
{
	GUSIConfig.DoAutoSpin();
	
	Socket *	sock	=	Sockets[s];

	if (sock)	{
		Gatherer	gath(iov, count);

		if (gath)
			return gath.length(sock->write(gath.buffer(), gath.buflen()));
		else
			return GUSI_error(ENOMEM);
	} else
		return -1;
}

int send(int s, const void *buffer, int buflen, int flags)
{
	GUSIConfig.DoAutoSpin();
	
	Socket *	sock	=	Sockets[s];

	return sock ? sock->sendto((void *)buffer, buflen, flags, nil, 0) : -1;
}

int sendto(int s, const void *buffer, int buflen, int flags, const struct sockaddr *to, int tolen)
{
	GUSIConfig.DoAutoSpin();
	
	Socket *	sock	=	Sockets[s];

	return sock ? sock->sendto((void *)buffer, buflen, flags, (void *) to, tolen) : -1;
}

int sendmsg(int s, const struct msghdr *msg,int flags)
{
	GUSIConfig.DoAutoSpin();
	
	Socket *	sock	=	Sockets[s];

	if (sock)	{
		Gatherer	gath(msg->msg_iov, msg->msg_iovlen);

		if (gath)
			return
				gath.length(
					sock->sendto(
						gath.buffer(),
						gath.buflen(),
						flags,
						msg->msg_name,
						msg->msg_namelen));
		else
			return GUSI_error(ENOMEM);
	} else
		return -1;
}

int select(int width, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
	Socket	*	sock;
	long 			count;
	int 			s;
	long 			starttime, waittime;
	fd_set 		rd, wd, ed;
	Boolean		r,w,e;
	Boolean *	canRead;
	Boolean *	canWrite;
	Boolean *	exception;

	count = 0;
	FD_ZERO(&rd);
	FD_ZERO(&wd);
	FD_ZERO(&ed);

	if (timeout)
		waittime =  timeout->tv_sec*60 + timeout->tv_usec/16666;
	else
		waittime =	2000000000;	// Slightly more than a year; close enough to "no timeout"
		
	starttime = TickCount();

	// Check files for kosherness

	for (s = 0; s < width ; ++s)
		if (	(readfds && FD_ISSET(s,readfds))
			||	(writefds && FD_ISSET(s,writefds))
			||	(exceptfds && FD_ISSET(s,exceptfds))
		)
			if (!Sockets[s])
				return GUSI_error(EBADF);
	
	for (s = 0; s < width ; ++s)
		if (sock = Sockets[s]) {
			r = readfds && FD_ISSET(s,readfds);
			w = writefds && FD_ISSET(s,writefds);
			e = exceptfds && FD_ISSET(s,exceptfds);

			if (r || w || e)
				sock->pre_select(r, w, e);
		}
		
	do {
		for (s = 0; s < width ; ++s)  {
			if (sock = Sockets[s]) {
				r = false;
				w = false;
				e = false;

				canRead = (readfds && FD_ISSET(s,readfds)) ? &r : nil;
				canWrite = (writefds && FD_ISSET(s,writefds)) ? &w : nil;
				exception = (exceptfds && FD_ISSET(s,exceptfds)) ? &e : nil;

				if (canRead || canWrite || exception)	{
					count	+= sock->select(canRead, canWrite, exception);

					if (r)
						FD_SET(s,&rd);
					if (w)
						FD_SET(s,&wd);
					if (e)
						FD_SET(s,&ed);
				}
			}
		}
		SPIN(false, SP_SELECT, waittime);
	}  while(!count && TickCount() - starttime < waittime);

	for (s = 0; s < width ; ++s)
		if (sock = Sockets[s]) {
			r = readfds && FD_ISSET(s,readfds);
			w = writefds && FD_ISSET(s,writefds);
			e = exceptfds && FD_ISSET(s,exceptfds);

			if (r || w || e)
				sock->post_select(r, w, e);
		}
		

	if (readfds)
		*readfds = rd;
	if (writefds)
		*writefds = wd;
	if (exceptfds)
		*exceptfds = ed;

	return count;
}

int getsockname(int s, struct sockaddr *name, int *namelen)
{
	Socket *	sock	=	Sockets[s];

	return sock ? sock->getsockname(name, namelen) : -1;
}

int getpeername(int s, struct sockaddr *name, int *namelen)
{
	Socket *	sock	=	Sockets[s];

	return sock ? sock->getpeername(name, namelen) : -1;
}

int shutdown(int s, int how)
{
	Socket *	sock	=	Sockets[s];

	return sock ? sock->shutdown(how) : -1;
}

int fcntl(int s, unsigned int cmd, int arg)
{
	Socket *	sock	=	Sockets[s];

	if (sock)
		return (cmd == F_DUPFD) ? Sockets.Install(sock, s, arg) : sock->fcntl(cmd, arg);
	else
		return -1;
}

int dup(int s)
{
	Socket *	sock	=	Sockets[s];

	return sock ? Sockets.Install(sock, s) : -1;
}

int dup2(int s, int s1)
{
	Socket *	sock	=	Sockets[s];

	if (!sock)
		return -1;

	if (Sockets[s1])
		Sockets.Remove(s1);

	return Sockets.Install(sock, s, s1);
}

int ioctl(int s, unsigned int request, long *argp)
{
	Socket *	sock	=	Sockets[s];

	if (!sock)
		return -1;
	
	switch(request) {
	case FIOLSEEK:
		return sock->lseek(((MPWSeekArg *)argp)->offset, ((MPWSeekArg *)argp)->whence);
	case FIODUPFD:
		return Sockets.Install(sock, s, int(argp));
	default:
		return sock->ioctl(request, argp);
	}
}

int getsockopt(int s, int level, int optname, void *optval, int * optlen)
{
	Socket *	sock	=	Sockets[s];

	return sock ? sock->getsockopt(level, optname, optval, optlen) : -1;
}

int setsockopt(int s, int level, int optname, const void *optval, int optlen)
{
	Socket *	sock	=	Sockets[s];

	return sock ? sock->setsockopt(level, optname, (void *) optval, optlen) : -1;
}

int fstat(int s, struct stat * buf)
{
	Socket *	sock	=	Sockets[s];

	return sock ? sock->fstat(buf) : -1;
}

long lseek(int s, long offset, int whence)
{
	Socket *	sock	=	Sockets[s];

	return sock ? sock->lseek(offset, whence) : -1;
}

int ftruncate(int s, long offset)
{
	Socket *	sock	=	Sockets[s];

	return sock ? sock->ftruncate(offset) : -1;
}

int isatty(int s)
{
	Socket *	sock	=	Sockets[s];

	return sock ? sock->isatty() : -1;
}

int GUSI_error(int err)
{
	errno =	err;

	return -1;
}

void * GUSI_error_nil(int err)
{
	errno =	err;

	return nil;
}

void GUSISetSpin(GUSISpinFn routine)
{
	GUSISpin = routine;
}

GUSISpinFn GUSIGetSpin()
{
	return GUSISpin;
}

int GUSISetEvents(GUSIEvtTable table)
{
	short	evt;

	evtHandler	=	table;
	evtMask		=	0;

	for (evt = 0; evt<16; ++evt)
		if (evtHandler[evt])
			evtMask	|=	1 << evt;

	return 0;
}

GUSIEvtHandler * GUSIGetEvents(void)
{
	return evtHandler;
}

/*********************** SocketDomain members ***********************/

SocketDomain *			SocketDomain::domains[GUSI_MAX_DOMAIN];
ProcessSerialNumber	SocketDomain::process;

SocketDomain * SocketDomain::Domain(int domain)
{
	if (domain < 0 || domain >= GUSI_MAX_DOMAIN || !domains[domain])	{
		GUSI_error(EINVAL);

		return nil;
	} else
		return domains[domain];
}

void SocketDomain::Ready()
{
	if (hasProcessMgr)
		WakeUpProcess(&process);
}

SocketDomain::SocketDomain(int domain)
{
#ifdef PREVENT_DUPLICATE_DOMAINS
	if (domains[domain])	{
		Str63	msg;

		sprintf((char *) msg+1, "Duplicate declaration for domain %d\n", domain);
		msg[0] = (unsigned char)strlen((char *) msg+1);

		DebugStr(msg);
	}
#endif
	if (domain)									// Ignore AF_UNSPEC domains
		domains[domain]	=	this;
	
	if (hasProcessMgr && !process.highLongOfPSN && !process.lowLongOfPSN)
		GetCurrentProcess(&process);
}

SocketDomain::~SocketDomain()
{
}

// Default implementations of socket() just returns an error

Socket * SocketDomain::socket(int, short)
{
	GUSI_error(EOPNOTSUPP);

	return nil;
}

int SocketDomain::choose(int, char *, void *, int, void *, int *)
{
	return GUSI_error(EOPNOTSUPP);
}

void SocketDomain::DontStrip()
{
}

/*********************** SocketTable members ************************/

static void InitStandardLib()
{
#if defined(powerc) || defined(__powerc)
	ConnectionID 	StdCLib;
	SymClass			symClass;
	Ptr 				whoCares;
	Str255			error;
		
	if (GetSharedLibrary(
			StringPtr("\pStdCLib"), kPowerPCArch, kLoadLib, &StdCLib, &whoCares, error)
	)
		return;
	
	if (FindSymbol(StdCLib, StringPtr("\popen"), (Ptr *) &file_open, &symClass))
		goto failed_on_open;
	if (FindSymbol(StdCLib, StringPtr("\pclose"), (Ptr *) &file_close, &symClass))
		goto failed_on_close;
	if (FindSymbol(StdCLib, StringPtr("\pread"), (Ptr *) &file_read, &symClass))
		goto failed_on_read;
	if (FindSymbol(StdCLib, StringPtr("\pwrite"), (Ptr *) &file_write, &symClass))
		goto failed_on_write;
	if (FindSymbol(StdCLib, StringPtr("\pfcntl"), (Ptr *) &file_fcntl, &symClass))
		goto failed_on_fcntl;
	if (FindSymbol(StdCLib, StringPtr("\pioctl"), (Ptr *) &file_ioctl, &symClass))
		goto failed_on_ioctl;
	if (FindSymbol(StdCLib, StringPtr("\plseek"), (Ptr *) &file_lseek, &symClass))
		goto failed_on_lseek;
	if (FindSymbol(StdCLib, StringPtr("\pfaccess"), (Ptr *) &file_faccess, &symClass))
		goto failed_on_faccess;
	
	if (FindSymbol(StdCLib, StringPtr("\p_univGetDevHandler"), (Ptr *) &whoCares, &symClass)) {
		// The ROM library takes (PowerPC) procedure pointers, while the
		// RAM library installed by StdCLibInit takes UPPs. Weird, huh?
		GUSIIOGlue.faccess	=	UniversalProcPtr(MPWFAccessGlue);
		GUSIIOGlue.close		=	UniversalProcPtr(MPWCloseGlue);
		GUSIIOGlue.read		=	UniversalProcPtr(MPWReadGlue);
		GUSIIOGlue.write		=	UniversalProcPtr(MPWWriteGlue);
		GUSIIOGlue.ioctl		=	UniversalProcPtr(MPWIOCtlGlue);
	}

	return;
	
failed_on_faccess:
	file_faccess = nil;
failed_on_lseek:
	file_lseek = nil;
failed_on_ioctl:
	file_ioctl = nil;
failed_on_fcntl:
	file_fcntl = nil;
failed_on_write:
	file_write = nil;
failed_on_read:
	file_read = nil;
failed_on_close:
	file_close = nil;
failed_on_open:
	file_open = nil;
#endif
}

static void FlushStdio()
{
	fwalk(fflush);
}

SocketTable::SocketTable()
{
	InitStandardLib();
	atexit(FlushStdio);
	
	needsConsole = false;
	if (GUSIConfig.hasConsole) {
#if defined(powerc) || defined(__powerc)
		_addDevHandler(
			1, 
			'GU·I',
			MPWFAccessGlue,
			MPWCloseGlue,
			MPWReadGlue,
			MPWWriteGlue,
			MPWIOCtlGlue);
#endif
		needsConsole = true;
	} else for (int i = 0; i < 3; i++) {
		sockets[i]	= 	FileSockets.stdopen(i);

		++sockets[i]->refCount;
	}
}
	
#if defined(powerc) || defined(__powerc)
int SocketTable::Install(Socket * sock, int from, int start)
#else
int SocketTable::Install(Socket * sock, int, int start)
#endif
{
	short	fd;

	if (start<0 || start >= GUSI_MAX_FD)
		return GUSI_error(EINVAL);

	for (fd=start; fd<GUSI_MAX_FD; ++fd)
		if (!sockets[fd])	{
			sockets[fd] = sock;
		
			++sock->refCount;
#if defined(powerc) || defined(__powerc)
			goto goOn;
#else
			return fd;
#endif
		}

	return GUSI_error(EMFILE);

#if defined(powerc) || defined(__powerc)
goOn:
	int file = -(fd+1);
	
	MPWIOEntry * entry =	_getIOPort(&file);
	MPWIOEntry * fromEntry;
		
	if (entry && file == fd)
		if (from >= 0 
			&& (fromEntry = _getIOPort(&from)) 
			&& fromEntry->funcs != &GUSIIOGlue
		) {
			/* Dup on a file socket is passed through to the standard C library */
         file_fcntl(from, F_DUPFD, fd);
			--sock->refCount;								// known to be greater than 1
			sockets[fd] = FileSockets.stdopen(fd);
			++sockets[fd]->refCount;
		} else {
			entry->flags	= 3;
			entry->funcs 	= &GUSIIOGlue;
			entry->userData= fd;
		}
	
	return fd;
#endif
}

int SocketTable::Remove(int fd)
{
	Socket *	sock;

	InitConsole();

	if (fd<0 || fd >= GUSI_MAX_FD || !(sock = sockets[fd]))
		return GUSI_error(EBADF);

	sockets[fd] 	=	nil;

	if (!--sock->refCount)
		delete sock;

#if defined(powerc) || defined(__powerc)
	MPWIOEntry * entry =	_getIOPort(&fd);
	
	if (entry)
		entry->flags = 0;
#endif
	return 0;
}

Socket * SocketTable::operator[](int fd)
{
	Socket * sock;

	InitConsole();
	
	if (fd<0 || fd >= GUSI_MAX_FD || !(sock = sockets[fd]))	{
		GUSI_error(EBADF);

		return nil;
	} else
		return sock;
}

SocketTable::~SocketTable()
{
	int i;

	// Flush stdio files (necessary to flush buffers)

	fwalk(fflush);

	// If we didn't need a console so far, we certainly don't need one now!
	// Doing this further up would be dangerous for small write only apps
	
	needsConsole = false;

	// Now close stdio files, just to be sure

	fwalk(fclose);

	// Close all files

	for (i = 0; i<GUSI_MAX_FD; ++i)
		if (sockets[i])
			close(i);
}

/********************** Default spin function ***********************/

/* Borrowed from tech note 263 */

#define kMaskModifiers  	0xFE00     	// we need the modifiers without the
                                   		// command key for KeyTrans
#define kMaskVirtualKey 	0x0000FF00 	// get virtual key from event message
                                   		// for KeyTrans
#define kUpKeyMask      	0x0080
#define kShiftWord      	8          	// we shift the virtual key to mask it
                                   		// into the keyCode for KeyTrans
#define kMaskASCII1     	0x00FF0000 	// get the key out of the ASCII1 byte
#define kMaskASCII2     	0x000000FF 	// get the key out of the ASCII2 byte
#define kPeriod         	0x2E       	// ascii for a period

static Boolean CmdPeriod(EventRecord *theEvent)
{
  	Boolean  fTimeToQuit;
  	short    keyCode;
  	long     virtualKey, keyInfo, lowChar, highChar, state, keyCId;
  	Handle   hKCHR;
	Ptr 		KCHRPtr;

	fTimeToQuit = false;

	if (((*theEvent).what == keyDown) || ((*theEvent).what == autoKey)) {

		// see if the command key is down.  If it is, find out the ASCII
		// equivalent for the accompanying key.

		if ((*theEvent).modifiers & cmdKey ) {

			virtualKey = ((*theEvent).message & kMaskVirtualKey) >> kShiftWord;
			// And out the command key and Or in the virtualKey
			keyCode    = short(((*theEvent).modifiers & kMaskModifiers) | virtualKey);
			state      = 0;

			hKCHR = nil;  /* set this to nil before starting */
		 	KCHRPtr = (Ptr)GetEnvirons(smKCHRCache);

			if ( !KCHRPtr ) {
				keyCId = GetScript(short(GetEnvirons(smKeyScript)), smScriptKeys);

				hKCHR   = GetResource('KCHR',short(keyCId));
				KCHRPtr = *hKCHR;
			}

			if (KCHRPtr) {
				keyInfo = KeyTrans(KCHRPtr, keyCode, &state);
				if (hKCHR)
					ReleaseResource(hKCHR);
			} else
				keyInfo = (*theEvent).message;

			lowChar =  keyInfo &  kMaskASCII2;
			highChar = (keyInfo & kMaskASCII1) >> 16;
			if (lowChar == kPeriod || highChar == kPeriod)
				fTimeToQuit = true;

		}  // end the command key is down
	}  // end key down event

	return( fTimeToQuit );
}

Boolean GUSIInterrupt()
{
	EvQElPtr		eventQ;

	for (eventQ = (EvQElPtr) LMGetEventQueue()->qHead; eventQ; )
		if (CmdPeriod((EventRecord *) &eventQ->evtQWhat))
			return true;
		else
			eventQ = (EvQElPtr)eventQ->qLink;
	
	return false;
}

int GUSIDefaultSpin(spin_msg msg, long arg)
{
	static Boolean			inForeground	=	true;
	extern int				StandAlone;
	WindowPtr				win;
	EventRecord				ev;
	long						sleepTime 		=	6;	// 1/10 of a second by default

	if (inForeground)
		SpinCursor(msg == SP_AUTO_SPIN ? short(arg) : 1);

	if (!inForeground || StandAlone)	{
		if (GUSIInterrupt())
			goto interrupt;

		switch (msg) {
		case SP_SLEEP:
		case SP_SELECT:
			if (arg >= sleepTime)				// Only sleep if patience guaranteed
				break;
			// Otherwise, fall through	
		case SP_AUTO_SPIN:
			sleepTime = 1;
			break;
		default:
			break;
		}
		
		if (WaitNextEvent(osMask|highLevelEventMask|mDownMask|evtMask, &ev, sleepTime, nil))
			switch (ev.what) {
			case mouseDown:
				if (!evtHandler || !evtHandler[mouseDown])
					if (FindWindow(ev.where, &win) == inSysWindow)
						SystemClick(&ev, win);
	
				break;
			case osEvt:
				if (ev.message & 1)
					inForeground	=	true;
				else
					inForeground	=	false;
				break;
			case kHighLevelEvent:
				if (!evtHandler || !evtHandler[kHighLevelEvent])
					if (hasAppleEvents)	// actually pretty likely, if we get HL Events
						if (AEProcessAppleEvent(&ev))
							return -1;
				break;
			default:
				break;
			}
	
		if (ev.what >= 0 && ev.what < 24 && evtHandler && evtHandler[ev.what])
			evtHandler[ev.what](&ev);
	}

	return 0;

interrupt:
	FlushEvents(-1, 0);

	return -1;
}

/************************** Feature members **************************/

Feature::Feature(unsigned short trapNum, TrapType tTyp)
{
	good =
		NGetTrapAddress(trapNum, tTyp) != NGetTrapAddress(_Unimplemented, ToolTrap);
}

Feature::Feature(OSType type, long value)
{
	long		attr;

	good = (!Gestalt(type, &attr) && (attr >= value));
}

Feature::Feature(OSType type, long mask, long value)
{
	long		attr;

	good = (!Gestalt(type, &attr) && ((attr & mask) == value));
}

Feature::Feature(const Feature & precondition, OSErrInitializer init)
{
	good	=	precondition && !init();
}

Feature::Feature(OSErrInitializer init)
{
	good	=	!init();
}

Feature::Feature(const Feature & precondition, voidInitializer init)
{
	if (precondition)	{
		good = true;
		init();
	} else
		good = false;
}

Feature::Feature(voidInitializer init)
{
	good = true;
	init();
}

Feature::Feature(const Feature & cond1, const Feature & cond2)
{
	good = cond1 && cond2;
}

OSErr AppleTalkIdentity(short & net, short & node)
{
	static short	mynet;
	static short	mynode;
	static OSErr	err = 1;

	if (err == 1)
		if (!(err = MPPOpen()))
			err = GetNodeAddress(&mynode, &mynet);


	net	=	mynet;
	node	=	mynode;

	return err;
}