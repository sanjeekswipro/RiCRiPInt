/*********************************************************************
File		:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIUnixTest	-	Test unix domain sockets
Author	:	Matthias Neeracher <neeri@iis.ethz.ch>
Language	:	MPW C

$Log: Examples:GUSIUnixTest.c,v $
Revision 1.2  1999/04/23  08:56:31  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.1  1994/02/25  02:48:37  neeri
Initial revision

Revision 0.4  1992/09/08  00:00:00  neeri
Factor out more common code

Revision 0.3  1992/07/26  00:00:00  neeri
Fixed a few minor bugs

Revision 0.2  1992/07/25  00:00:00  neeri
Adapt to new testing gear & implementation

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

void Socket(char ch1, char, const char *)
{
	sock	=	socket(AF_UNIX, (ch1 == 's') ? SOCK_STREAM : SOCK_DGRAM, 0);
	
	if (sock == -1)	{
		printf("# socket() returned error %s\n", Explain());
		Where();
	}
}

void Bind(char, char, const char * cmd)
{
	int						len;
	struct sockaddr_un	addr;

	if (sock == -1)	{
		printf("# socket is not open\n");
		Where();
		
		return;
	}

	if (sscanf(cmd, "%s", addr.sun_path) == 1) {
		addr.sun_family	=	AF_UNIX;
		len					=	strlen(addr.sun_path)+2;
	} else {
		len = sizeof(struct sockaddr_un);
		
		if (choose(AF_UNIX, 0, "", nil, CHOOSE_NEW, &addr, &len))	{
			printf("# choose() returned error %s\n", Explain());
			Where();
			
			return;
		}
	}

	if (bind(sock, (struct sockaddr *) &addr, len))	{
		printf("bind(\"%s\") returned error %s\n", addr.sun_path, Explain());
		Where();
	}
}

void Accept(char, char, const char *)
{
	int						len;
	struct sockaddr_un	addr;

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

	len	=	sizeof(struct sockaddr_un);
	sock	=	accept(accsock = sock, (struct sockaddr *) &addr, &len);
	
	if (sock < 0)	{
		printf("# accept() returned error %s\n", Explain());
		sock		=	accsock;
		accsock	=	-1;
	} else {
		addr.sun_path[len-2]	= 0;
		
		printf("# accepted connection from \"%s\"\n", addr.sun_path);
	}
	
	Where();
}
	
void Connect(char, char, const char * cmd)
{
	int						len;
	struct sockaddr_un	addr;

	if (sock == -1)	{
		printf("# socket is not open\n");
		Where();
		
		return;
	}
	
	if (sscanf(cmd, "%s", addr.sun_path) == 1) {
		addr.sun_family	=	AF_UNIX;
		len					=	strlen(addr.sun_path)+2;
	} else {
		len = sizeof(struct sockaddr_un);
		
		if (choose(AF_UNIX, 0, "", nil, 0, &addr, &len))	{
			printf("# choose() returned error %s\n", Explain());
			Where();
			
			return;
		}
	}

	if (connect(sock, (struct sockaddr *) &addr, len))	{
		printf("connect(\"%s\") returned error %s\n", addr.sun_path, Explain());
		Where();
	}
}	

main(int argc, char ** argv)
{
	printf("GUSIUnixTest		MN 08Sep92\n\n");

	COMMAND('s', 's', Socket,  "", 				"Create a stream socket");
	COMMAND('d', 's', Socket,  "", 				"Create a datagram socket");
	COMMAND('b', 'd', Bind,  	"[filename]", 	"Bind to address");
	COMMAND('c', 'o', Connect, "[filename]", 	"Connect to address");
	COMMAND('a', 'c', Accept,  "", 				"Accept a connection");
	
	AddSocketCommands();
	
	GUSISetEvents(GUSISIOWEvents);
	RunTest(argc, argv);
	CleanupSockets();
}