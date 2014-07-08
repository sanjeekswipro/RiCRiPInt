/*********************************************************************
File		:	GUSI				-	Grand Unified Socket Interface
File		:	GUSITest_P.h	-	Common testing gear
Author	:	Matthias Neeracher <neeri@iis.ethz.ch>
Language	:	MPW C

$Log: Examples:GUSITest_P.h,v $
Revision 1.2  1999/04/23  08:56:51  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.1  1994/02/25  02:48:27  neeri
Initial revision

*********************************************************************/

#include <CType.h>

#define NROFCHARS				26
#define DECODE(ch)			((ch) ? (ch) - (isupper(ch) ? 'A' : 'a') + 1 : 0)
#define CMDCODE(ch1,ch2)	(DECODE(ch1)*(NROFCHARS+1)+DECODE(ch2))
#define NROFCMDS				(NROFCHARS+1)*(NROFCHARS+1)

typedef struct {
	TestCmd			proc;
	const char *	syntax;
	const char *	help;
} CmdDef;

extern CmdDef commands[NROFCMDS];

#define DISPATCH(ch1,ch2)	commands[CMDCODE(ch1,ch2)].proc
#define USAGE(ch1,ch2) 		commands[CMDCODE(ch1,ch2)].syntax
#define HELPMSG(ch1,ch2) 	commands[CMDCODE(ch1,ch2)].help

