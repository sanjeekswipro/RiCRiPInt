/*********************************************************************
File		:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIPAPTest		-	Test PAP sockets
Author	:	Matthias Neeracher <neeri@iis.ethz.ch>
Language	:	MPW C

$Log: Examples:GUSIPAPTest.c,v $
Revision 1.2  1999/04/23  08:56:34  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.2  1994/12/31  01:06:30  neeri
ANSIfy (omitting parameter names was illegal).

Revision 1.1  1994/02/25  02:47:26  neeri
Initial revision

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

void Open(char ch1, char ch2, const char * line)
{
	sock	=	open("Dev:Printer", O_RDWR);
	
	if (sock == -1)	{
		printf("# open() returned error %s\n", Explain());
		Where();
	}
}

main(int argc, char ** argv)
{
	printf("GUSIPAPTest		MN 15Feb93\n\n");

	COMMAND('o', 'p', Open,  	"", 				"Open connection to printer");
	
	AddSocketCommands();
	
	GUSISetEvents(GUSISIOWEvents);
	RunTest(argc, argv);
	CleanupSockets();
}