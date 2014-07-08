/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSINetDB.cp	-	Convert internet names to adresses
Author	:	Matthias Neeracher

	This file was derived from the socket library by

		Charlie Reiman	<creiman@ncsa.uiuc.edu> and
		Tom Milligan	<milligan@madhaus.utcs.utoronto.ca>

Language	:	MPW C++

$Log: GUSINetDB.cp,v $
Revision 1.2  1999/04/23  08:43:41  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.3  1994/08/10  00:07:30  neeri
Sanitized for universal headers.

Revision 1.2  1994/05/01  23:43:31  neeri
getservbyname() without /etc/services would fail.

Revision 1.1  1994/02/25  02:29:36  neeri
Initial revision

Revision 0.5  1993/10/31  00:00:00  neeri
Deferred opening of resolver

Revision 0.4  1993/07/29  00:00:00  neeri
Real getservent code (adapted from Sak Wathanasin)

Revision 0.3  1993/01/19  00:00:00  neeri
Can't set aliases to NULL.

Revision 0.2  1992/11/21  00:00:00  neeri
Remove force_active

Revision 0.1  1992/09/14  00:00:00  neeri
Maybe it works, maybe it doesn't

*********************************************************************/

#include "GUSIINET_P.h"

#include "TFileSpec.h"
#include "Folders.h"
#include "PLStringFuncs.h"

#if !defined(powerc) && !defined(__powerc)
#pragma segment GUSIResident
#endif

pascal void DNRDone(struct hostInfo *, Boolean * done)
{
	*done = true;
}

#if USESROUTINEDESCRIPTORS
RoutineDescriptor	uDNRDone = 
		BUILD_ROUTINE_DESCRIPTOR(uppResultProcInfo, DNRDone);
#else
#define uDNRDone DNRDone
#endif

#if !defined(powerc) && !defined(__powerc)
#pragma segment GUSIINET
#endif

int h_errno;

/*
 *   Gethostbyname and gethostbyaddr each return a pointer to an
 *   object with the following structure describing an Internet
 *   host referenced by name or by address, respectively. This
 *   structure contains the information obtained from the MacTCP
 *   name server.
 *
 *   struct    hostent
 *   {
 *        char *h_name;
 *        char **h_aliases;
 *        int  h_addrtype;
 *        int  h_length;
 *        char **h_addr_list;
 *   };
 *   #define   h_addr  h_addr_list[0]
 *
 *   The members of this structure are:
 *
 *   h_name       Official name of the host.
 *
 *   h_aliases    A zero terminated array of alternate names for the host.
 *
 *   h_addrtype   The type of address being  returned; always AF_INET.
 *
 *   h_length     The length, in bytes, of the address.
 *
 *   h_addr_list  A zero terminated array of network addresses for the host.
 *
 *   Error return status from gethostbyname and gethostbyaddr  is
 *   indicated by return of a null pointer.  The external integer
 *   h_errno may then  be checked  to  see  whether  this  is  a
 *   temporary  failure  or  an  invalid  or  unknown  host.  The
 *   routine herror  can  be  used  to  print  an error  message
 *   describing the failure.  If its argument string is non-NULL,
 *   it is printed, followed by a colon and a space.   The  error
 *   message is printed with a trailing newline.
 *
 *   h_errno can have the following values:
 *
 *     HOST_NOT_FOUND  No such host is known.
 *
 *     TRY_AGAIN	This is usually a temporary error and
 *					means   that  the  local  server  did  not
 *					receive a response from  an  authoritative
 *					server.   A  retry at some later time may
 *					succeed.
 *
 *     NO_RECOVERY	Some unexpected server failure was encountered.
 *	 				This is a non-recoverable error.
 *
 *     NO_DATA		The requested name is valid but  does  not
 *					have   an IP  address;  this  is not  a
 *					temporary error. This means that the  name
 *					is known  to the name server but there is
 *					no address  associated  with  this  name.
 *					Another type of request to the name server
 *					using this domain name will result in  an
 *					answer;  for example, a mail-forwarder may
 *					be registered for this domain.
 *					(NOT GENERATED BY THIS IMPLEMENTATION)
 */

static struct hostInfo macHost;

#define MAXALIASES 0
static char *aliasPtrs[MAXALIASES+1] = {NULL};
static ip_addr *addrPtrs[NUM_ALT_ADDRS+1];

static struct hostent  unixHost =
{
	macHost.cname,
	aliasPtrs,
	AF_INET,
	sizeof(ip_addr),
	(char **) addrPtrs
};

inline struct in_addr make_in_addr(ip_addr addr)
{
	struct in_addr	res;

	res.s_addr	=	addr;

	return res;
}

struct hostent * gethostbyname(char *name)
{
	Boolean done;
	int i;

	if (INETSockets.Resolver()) {
		h_errno = NO_RECOVERY;	
		return NULL;
	}
	
	for (i=0; i<NUM_ALT_ADDRS; i++)
		macHost.addr[i] = 0;

	done = false;

	if (StrToAddr(name, &macHost, ResultUPP(&uDNRDone), (char *) &done) == cacheFault)
		SPINP(!done,SP_NAME,0L);

	switch (macHost.rtnCode) {
	case noErr: break;

	case nameSyntaxErr:	h_errno = HOST_NOT_FOUND;	return(NULL);
	case cacheFault:		h_errno = NO_RECOVERY;		return(NULL);
	case noResultProc:	h_errno = NO_RECOVERY;		return(NULL);
	case noNameServer:	h_errno = HOST_NOT_FOUND;	return(NULL);
	case authNameErr:		h_errno = HOST_NOT_FOUND;	return(NULL);
	case noAnsErr:			h_errno = TRY_AGAIN;			return(NULL);
	case dnrErr:			h_errno = NO_RECOVERY;		return(NULL);
	case outOfMemory:		h_errno = TRY_AGAIN;			return(NULL);
	default:					h_errno = NO_RECOVERY;		return(NULL);
	}

	/* was the 'name' an IP address? */
	if (macHost.cname[0] == 0) {
		h_errno = HOST_NOT_FOUND;
		return(NULL);
	}

	/* for some reason there is a dot at the end of the name */
	i = int(strlen(macHost.cname)) - 1;
	if (macHost.cname[i] == '.')
		macHost.cname[i] = 0;

	for (i=0; i<NUM_ALT_ADDRS && macHost.addr[i]!=0; i++)
		addrPtrs[i] =	(ip_addr *) &macHost.addr[i];

	addrPtrs[i] = NULL;

	return &unixHost;
}

struct hostent * gethostbyaddr(const char *addrP, int, int)
{
	Boolean	done;
	int 		i;

	if (INETSockets.Resolver()) {
		h_errno = NO_RECOVERY;	
		return NULL;
	}

	for (i=0; i<NUM_ALT_ADDRS; i++)
		macHost.addr[i] = 0;

	done = false;

	if (AddrToName(*(ip_addr *)addrP, &macHost, ResultUPP(&uDNRDone), (char *) &done) == cacheFault)
		SPINP(!done,SP_ADDR,0L);

	switch (macHost.rtnCode) {
	case noErr: 			break;

	case cacheFault:		h_errno = NO_RECOVERY;		return(NULL);
	case noNameServer:	h_errno = HOST_NOT_FOUND;	return(NULL);
	case authNameErr:		h_errno = HOST_NOT_FOUND;	return(NULL);
	case noAnsErr:			h_errno = TRY_AGAIN;			return(NULL);
	case dnrErr:			h_errno = NO_RECOVERY;		return(NULL);
	case outOfMemory:		h_errno = TRY_AGAIN;			return(NULL);
	default:					h_errno = NO_RECOVERY;		return(NULL);
	}

	/* for some reason there is a dot at the end of the name */
	i = int(strlen(macHost.cname)) - 1;
	if (macHost.cname[i] == '.')
		macHost.cname[i] = 0;

	for (i=0; i<NUM_ALT_ADDRS; i++)
		addrPtrs[i] = (ip_addr *) &macHost.addr[i];

	addrPtrs[NUM_ALT_ADDRS] = NULL;

	return &unixHost;
}

char * inet_ntoa(struct in_addr inaddr)
{
	if (INETSockets.Resolver()) {
		h_errno = NO_RECOVERY;	
		return NULL;
	}
	
	(void) AddrToStr(inaddr.s_addr, macHost.cname);

	return macHost.cname;
}

struct in_addr inet_addr(char *address)
{
	if (INETSockets.Resolver()) {
		h_errno = NO_RECOVERY;	
		return make_in_addr(0xFFFFFFFF);
	}
	
	if (StrToAddr(address,&macHost,NULL,NULL) != noErr)
		return make_in_addr(0xFFFFFFFF);

	/* was the 'address' really a name? */
	if (macHost.cname[0] != 0)
		return make_in_addr(0xFFFFFFFF);

	return make_in_addr(macHost.addr[0]);
}

/*
 * gethostname()
 *
 * Try to get my host name from DNR. If it fails, just return my
 * IP address as ASCII. This is non-standard, but it's a mac,
 * what do you want me to do?
 */

int gethostname(char *machname, int buflen)
{
	in_addr ipaddr;
	struct	hostent *hp;
	struct GetAddrParamBlock pbr;

	pbr.ioCRefNum 	= INETSockets.Driver();
	pbr.csCode 		= ipctlGetAddr;

	if (PBControlSync(ParmBlkPtr(&pbr)))
		goto resign;

	ipaddr	=	make_in_addr(pbr.ourAddress);

	hp = gethostbyaddr((char *) &ipaddr, sizeof(in_addr), AF_INET);

	if (!hp)
		goto resign;
	else
		strncpy(machname, hp->h_name, unsigned(buflen));

	machname[buflen-1] = 0;  /* extra safeguard */

	return 0;

resign:
	sprintf(machname, "%d.%d.%d.%d",
							ipaddr.s_addr>>24,
							ipaddr.s_addr>>16 & 0xff,
							ipaddr.s_addr>>8 & 0xff,
							ipaddr.s_addr & 0xff);
	return 0;
}


/*
 *	getservbybname()
 *
 */

static char * servlist[] =
{
	"echo   		  7/udp",
	"discard   	  9/udp",
	"time   		 37/udp",
	"domain   	 53/udp",
	"sunrpc   	111/udp",
	"tftp  		 69/udp",
	"biff   		512/udp",
	"who   		513/udp",
	"talk   		517/udp",
	"ftp-data  	 20/tcp",
	"ftp  		 21/tcp",
	"telnet  	 23/tcp",
	"smtp  		 25/tcp",
	"time  		 37/tcp",
	"whois  		 43/tcp",
	"domain 	 	 53/tcp",
	"hostnames  101/tcp",
	"nntp   		119/tcp",
	"finger  	 79/tcp",
	"ntp   		123/tcp",
	"uucp   		540/tcp",
	NULL
};

static char 				servline[128];
static struct servent	serv;
static FILE * 				servfil;
static int					servptr;
static char *				servalias[8];
static int					servstay = 0;

void setservent(int stayopen)
{
	if (servfil && servfil != (FILE *) -1) {
		rewind(servfil);
	}
	servptr	= 0;
	servstay = servstay || stayopen;
}

void endservent()
{
	if (servfil && servfil != (FILE *) -1) {
		fclose(servfil);
		servfil = NULL;
	}
	
	servstay = 0;
}

struct servent *  getservent()
{
	char *	p;
	int		aliascount;
	
	if (!servfil) {
		TFileSpec serv;
		
		if (!FindFolder(
				kOnSystemDisk, 
				kPreferencesFolderType, 
				kDontCreateFolder, 
				&serv.vRefNum,
				&serv.parID)
		) {
			PLstrcpy(serv.name, (StringPtr) "\p/etc/services");
		
			if (servfil = fopen(serv.FullPath(), "r"))
				goto retry;
		}	
		servfil 	= (FILE *) -1;
		servptr	= 0;
	}
	
retry:
	if (servfil == (FILE *) -1)
		if (!servlist[servptr])
			return (struct servent *) NULL;
		else
			strcpy(servline, servlist[servptr++]);
	else if (!(fgets(servline, 128, servfil)))
		return (struct servent *) NULL;
		
	if (p = strpbrk(servline, "#\n"))
		*p = 0;
	if (!servline[0])
		goto retry;
	
	if (!(serv.s_name = strtok(servline, " \t")))
		goto retry;
		
	if (!(p = strtok(NULL, " \t")))
		goto retry;
	
	if (!(serv.s_proto = strpbrk(p, "/,")))
		goto retry;
		
	*serv.s_proto++ 	= 0;
	serv.s_port 		= htons(atoi(p));
	serv.s_aliases 	= servalias;
	
	for (aliascount = 0; aliascount < 7; ) 
		if (!(servalias[aliascount++] = strtok(NULL, " \t")))
			break;
	
	servalias[aliascount] = NULL;
	
	return &serv;
}

struct servent * getservbyname(const char * name, const char * proto)
{
	struct servent * 	ent;
	char ** 				al;
	setservent(0);
	
	while (ent = getservent()) {
		if (!strcmp(name, ent->s_name))
			goto haveName;
		
		for (al = ent->s_aliases; *al; ++al)
			if (!strcmp(name, *al))
				goto haveName;
		
		continue;
haveName:
		if (!proto || !strcmp(proto, ent->s_proto))
			break;
	}
	
	if (!servstay)
		endservent();
	
	return ent;
}

struct servent * getservbyport(int port, const char * proto)
{
	struct servent * ent;
	
	setservent(0);
	
	while (ent = getservent())
		if (port == ent->s_port && (!proto || !strcmp(proto, ent->s_proto)))
			break;
	
	if (!servstay)
		endservent();
	
	return ent;
}

static	char	tcp[] = "tcp";
static	char	udp[] = "udp";
#define	MAX_PROTOENT			10
static 	struct protoent		protoents[MAX_PROTOENT];
static 	int						protoent_count=0;

struct protoent * getprotobyname(const char * name)
{
	struct protoent *pe;

	pe = &protoents[protoent_count];
	if (strcmp(name, "udp") == 0) {
		pe->p_name = udp;
		pe->p_proto = IPPROTO_UDP;
	} else if (strcmp (name, "tcp") == 0)  {
		pe->p_name = tcp;
		pe->p_proto = IPPROTO_TCP;
	} else {
		errno = EPROTONOSUPPORT;
		return NULL;
	}
	pe->p_aliases = aliasPtrs;
	protoent_count = (protoent_count +1) % MAX_PROTOENT;
	return pe;
}
