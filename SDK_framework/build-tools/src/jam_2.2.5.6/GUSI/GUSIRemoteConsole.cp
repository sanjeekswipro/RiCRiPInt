/*********************************************************************
Project	:	GUSI						-	Grand Unified Socket Interface
File		:	GUSIRemoteConsole.cp	-	Divert standard I/O to other 
												application.
Author	:	Matthias Neeracher
Language	:	MPW C++

$Log: GUSIRemoteConsole.cp,v $
Revision 1.2  1999/04/23  08:44:05  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.3  1994/12/30  20:17:07  neeri
New file name dispatch scheme.

Revision 1.2  1994/08/10  00:05:04  neeri
Sanitized for universal headers.

Revision 1.1  1994/02/25  02:30:15  neeri
Initial revision

*********************************************************************/

#pragma force_active on

#include "GUSIPPC_P.h"
#include "GUSIFile_P.h"

#include <Script.h>
#include <TextUtils.h>
#include <PLStringFuncs.h>
#include <LowMem.h>

class GUSIRemoteConsoleDomain : public FileSocketDomain {
	static Socket *	console;
public:
	GUSIRemoteConsoleDomain()	:	FileSocketDomain(AF_UNSPEC, true, false)	{	}
	
	virtual Boolean Yours(const GUSIFileRef & ref, Request request);
	virtual Socket * open(const GUSIFileRef & ref, int oflag);
};

GUSIRemoteConsoleDomain	GUSIRemoteConsoleSockets;

Socket * GUSIRemoteConsoleDomain::console	=	(Socket *) nil;

Boolean GUSIRemoteConsoleDomain::Yours(const GUSIFileRef & ref, FileSocketDomain::Request request)
{
	return !ref.spec && (request == willOpen) && 
		(	equalstring((char *) ref.name, "dev:stdin", false, true)
		|	equalstring((char *) ref.name, "dev:stdout", false, true)
		|	equalstring((char *) ref.name, "dev:stderr", false, true)
		|	equalstring((char *) ref.name, "dev:console", false, true));
}

static sa_constr_ppc	RemoteConConstr	=	{
	PPC_CON_NEWSTYLE+PPC_CON_MATCH_TYPE,
	"\p",
	{
		smRoman,
		"\p",
		ppcByString,
		"\pRemote Console"
	}
};

Socket * GUSIRemoteConsoleDomain::open(const GUSIFileRef & ref, int flags)
{
	char 				title[256];
	
	strncpy(title, ref.name, 7);
	title[7] = 0;
	
	if (equalstring(title, (char *) "stdin", false, true)) {
		flags = O_RDONLY;
	} else if (equalstring(title, (char *) "stdout", false, true)) {
		flags = O_WRONLY;
	} else if (equalstring(title, (char *) "stderr", false, true)) {
		flags = O_WRONLY;
	}

	if (!console) {
		if (!(console = PPCSockets.socket(SOCK_STREAM, 0)))
			goto lose;
		
		sockaddr_ppc	addr;
		int				len = sizeof(sockaddr_ppc);
	
		addr.family									=	AF_PPC;
		addr.port.nameScript						=	smRoman;
		addr.port.portKindSelector				=	ppcByString;
		addr.location.locationKindSelector	=	ppcNBPTypeLocation;
		
		PLstrcpy(addr.port.name, LMGetCurApName());
		PLstrcpy(addr.location.u.nbpType, "\pPPCToolBox");
		PLstrcpy(addr.port.u.portTypeStr, "\pRemote Console Client");
		
		if (console->bind((struct sockaddr *) &addr, sizeof(sockaddr_ppc))) {
			if (errno != EADDRINUSE)
				goto lose;
				
			strcpy((char *) addr.port.name+addr.port.name[0]+1, " - 2");
			addr.port.name[0] += 4;
			
			while (console->bind((struct sockaddr *) &addr, sizeof(sockaddr_ppc))) {
				if (errno != EADDRINUSE ||Êaddr.port.name[addr.port.name[0]] == '9')
					goto lose;
				++addr.port.name[addr.port.name[0]];
			}
		}
		if (PPCSockets.choose(0, "Please choose a console to connect to", &RemoteConConstr, 0, &addr, &len))
			goto lose;
		if (console->connect(&addr, len))
			goto lose;
	}
	
	return console;

lose:
	exit(1);
}
