/*********************************************************************
File		:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIAtlkTest	-	Test appletalk sockets
Author	:	Matthias Neeracher <neeri@iis.ethz.ch>
Language	:	MPW C

$Log: Examples:GUSIAtlkTest.c,v $
Revision 1.2  1999/04/23  08:57:00  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.2  1994/12/31  01:05:05  neeri
ANSIfy (omitting parameter names was illegal).

Revision 1.1  1994/02/25  02:46:44  neeri
Initial revision

Revision 0.6  1992/10/14  00:00:00  neeri
Fix usage messages

Revision 0.5  1992/09/08  00:00:00  neeri
Factor out more common code

Revision 0.4  1992/07/31  00:00:00  neeri
Test select()

Revision 0.3  1992/07/26  00:00:00  neeri
Rewrote for symbolic adresses and new testing gear.

Revision 0.2  1992/05/13  00:00:00  neeri
Adapted for AppleTalk sockets

Revision 0.1  1992/04/17  00:00:00  neeri
Handle SIOW activate/update

*********************************************************************/

#include <GUSI.h>
#include <GUSITest.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include "Events.h"

extern int GUSIDefaultSpin(spin_msg, long);

sa_constr_atlk	Constr	=	{
	1,
	{
		{
			nil, "\pGUSIAtlkTest"
		},
		{
			nil, ""
		},
		{
			nil, ""
		},
		{
			nil, ""
		}
	}
};

void Socket(char ch1, char ch2, const char * line)
{
	sock	=	socket(AF_APPLETALK, SOCK_STREAM, 0);
	
	if (sock == -1)	{
		printf("# socket() returned error %s\n", Explain());
		Where();
	}
}

void Bind(char ch1, char ch2, const char * cmd)
{
	struct sockaddr_atlk_sym	addr;
	Str63							obj;

	if (sock == -1)	{
		printf("# socket is not open\n");
		Where();
		
		return;
	}

	if (sscanf(cmd, "%s", (char *) (obj+1)) == 1) {
		addr.family	=	ATALK_SYMADDR;
		obj[0]	=	strlen((char *) (obj+1));
		NBPSetEntity((Ptr) &addr.name, obj, "\pGUSIAtlkTest", "\p*");
	} else {
		Usage(ch1, ch2);
				
		return;
	}

	if (bind(sock, (struct sockaddr *) &addr, sizeof(struct sockaddr_atlk_sym)))	{
		printf("bind(\"%s\") returned error %s\n", (char *) (obj+1), Explain());
		Where();
	}
}

void Accept(char ch1, char ch2, const char * line)
{
	int						len;
	struct sockaddr_atlk	addr;

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

	len	=	sizeof(struct sockaddr_atlk);
	sock	=	accept(accsock = sock, (struct sockaddr *) &addr, &len);
	
	if (sock < 0)	{
		printf("# accept() returned error %s\n", Explain());
		sock		=	accsock;
		accsock	=	-1;
	} else
		printf(
			"# accepted connection from [%d]%d:%d\n", 
			addr.addr.aNet, 
			addr.addr.aNode,
			addr.addr.aSocket);
	
	Where();
}
	
void Connect(char ch1, char ch2, const char * cmd)
{
	int								len;
	struct sockaddr_atlk_sym	symaddr;
	struct sockaddr_atlk			numaddr;
	Str63								obj;

	if (sock == -1)	{
		printf("# socket is not open\n");
		Where();
		
		return;
	}

	if (sscanf(cmd, "%s", (char *) (obj+1)) == 1) {
		symaddr.family	=	ATALK_SYMADDR;
		obj[0]	=	strlen((char *) (obj+1));
		NBPSetEntity((Ptr) &symaddr.name, obj, "\pGUSIAtlkTest", "\p*");

		if (connect(sock, (struct sockaddr *) &symaddr, sizeof(struct sockaddr_atlk_sym)))	{
			printf("# connect(\"%s\") returned error %s\n", (char *) (obj+1), Explain());
			Where();
		}
	} else {
		len = sizeof(struct sockaddr_atlk);
		
		if (choose(AF_APPLETALK, 0, "Yeah ?", &Constr, 0, &numaddr, &len))	{
			printf("# choose() returned error %s\n", Explain());
			Where();
			
			return;
		}

		if (connect(sock, (struct sockaddr *) &numaddr, len))	{
			printf(
				"# connect([%d]%d:%d) returned error %s\n", 
				numaddr.addr.aNet, 
				numaddr.addr.aNode,
				numaddr.addr.aSocket,
				Explain());
			Where();
		}
	}
}	

main(int argc, char ** argv)
{
	printf("GUSIAtlkTest		MN 10Jun94\n\n");

	COMMAND('s', 's', Socket,  "", 				"Create a stream socket");
	COMMAND('a', 'c', Accept,  "", 				"Accept a connection");
	COMMAND('b', 'd', Bind,  	"objectname", 	"Bind to address");
	COMMAND('c', 'o', Connect, "[objectname]","Connect to address");
	
	AddSocketCommands();
	
	GUSISetEvents(GUSISIOWEvents);
	RunTest(argc, argv);
	CleanupSockets();
}