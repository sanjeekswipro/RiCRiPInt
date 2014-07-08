/*********************************************************************
File		:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIPPCTest		-	Test PPC sockets
Author	:	Matthias Neeracher <neeri@iis.ethz.ch>
Language	:	MPW C

$Log: Examples:GUSIPPCTest.c,v $
Revision 1.2  1999/04/23  08:56:32  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.2  1994/12/31  01:05:05  neeri
ANSIfy (omitting parameter names was illegal).

Revision 1.1  1994/02/25  02:47:36  neeri
Initial revision

Revision 0.3  1993/06/20  00:00:00  neeri
New sa_constr

Revision 0.2  1992/10/14  00:00:00  neeri
Fix NBP type, usage messages

Revision 0.1  1992/09/08  00:00:00  neeri
Factor out common socket routines

*********************************************************************/

#include <GUSI.h>
#include <GUSITest.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <Events.h>
#include <TextUtils.h>

extern int GUSIDefaultSpin(spin_msg, long);

sa_constr_ppc	Constr	=	{
	PPC_CON_NEWSTYLE+PPC_CON_MATCH_TYPE,
	"\p",
	{
		smRoman,
		"\p",
		ppcByString,
		"\pGUSIPPCTest"
	}
};

void Socket(char ch1, char ch2, const char * line)
{
	sock	=	socket(AF_PPC, SOCK_STREAM, 0);
	
	if (sock == -1)	{
		printf("# socket() returned error %s\n", Explain());
		Where();
	}
}

void Bind(char ch1, char ch2, const char * cmd)
{
	struct sockaddr_ppc	addr;

	if (sock == -1)	{
		printf("# socket is not open\n");
		Where();
		
		return;
	}

	addr.family	=	AF_PPC;
	addr.port.nameScript						=	smRoman;
	addr.port.portKindSelector				=	ppcByString;
	addr.location.locationKindSelector	=	ppcNBPTypeLocation;
	
	if (sscanf(cmd, "%P %P", addr.port.name, &addr.location.u.nbpType) != 2) {
		Usage(ch1, ch2);
			
		return;
	}
			
	strcpy((char *) &addr.port.u.portTypeStr, (char *) "\pGUSIPPCTest");

	if (bind(sock, (struct sockaddr *) &addr, sizeof(struct sockaddr_ppc)))	{
		printf("bind() returned error %s\n", Explain());
		Where();
	}
}

void Accept(char ch1, char ch2, const char * line)
{
	int						len;
	struct sockaddr_ppc	addr;

	if (sock == -1)	{
		printf("# socket is not open\n");
		Where();
		
		return;
	}
	if (accsock != -1)	{
		printf("# can't accept more than one connection\n");
		Where();
		
		return;
	}

	len	=	sizeof(struct sockaddr_ppc);
	sock	=	accept(accsock = sock, (struct sockaddr *) &addr, &len);
	
	if (sock < 0)	{
		printf("# accept() returned error %s\n", Explain());
		sock		=	accsock;
		accsock	=	-1;
	} else {
		printf(
			"# accepted connection from %P[%P]", 
			addr.port.name, 
			addr.port.u.portTypeStr);
		
		switch (addr.location.locationKindSelector) {
		case ppcNBPLocation:
			printf(
				"@%P:%P:%P\n", 
				addr.location.u.nbpEntity.objStr, 
				addr.location.u.nbpEntity.typeStr, 
				addr.location.u.nbpEntity.zoneStr);
			break;
		case ppcNBPTypeLocation:
			printf("@%P\n", addr.location.u.nbpType);
			break;
		default:
			printf("\n");
			break;
		}
	}
	
	Where();
}
	
void Connect(char ch1, char ch2, const char * cmd)
{
	int						len;
	struct sockaddr_ppc	addr;

	if (sock == -1)	{
		printf("# socket is not open\n");
		Where();
		
		return;
	}
	
	len = sizeof(struct sockaddr_ppc);
		
	if (choose(AF_PPC, 0, "Yeah ?", &Constr, 0, &addr, &len))	{
		printf("# choose() returned error %s\n", Explain());
		Where();
			
		return;
	}
	if (connect(sock, (struct sockaddr *) &addr, sizeof(struct sockaddr_ppc)))	{
		printf("# connect() returned error %s\n", Explain());
		Where();
	}
}	

main(int argc, char ** argv)
{
	printf("GUSIPPCTest		MN 10Jun94\n\n");

	COMMAND('s', 's', Socket,  "", 				"Create a stream socket");
	COMMAND('b', 'd', Bind,  	"Name Type", 	"Bind to address");
	COMMAND('c', 'o', Connect, "", 				"Connect to address");
	COMMAND('a', 'c', Accept,  "", 				"Accept a connection");
	
	AddSocketCommands();
	
	GUSISetEvents(GUSISIOWEvents);
	RunTest(argc, argv);
	CleanupSockets();
}