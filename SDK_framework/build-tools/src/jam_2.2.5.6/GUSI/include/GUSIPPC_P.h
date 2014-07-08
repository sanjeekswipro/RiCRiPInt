/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIPPC_P.h		-	Common definitions for PPC Sockets
Author	:	Matthias Neeracher
Language	:	MPW C++

$Log: include:GUSIPPC_P.h,v $
Revision 1.2  1999/04/23  08:56:40  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.1  1994/02/25  02:57:23  neeri
Initial revision

Revision 0.1  1993/11/07  00:00:00  neeri
Extracted from GUSIPPC.cp

*********************************************************************/

#include "GUSI_P.h"
#include <PPCToolBox.h>

#include <sys/types.h>

class PPCSocketDomain : public SocketDomain {
	Boolean	ppcInit;
public:
	PPCSocketDomain();
	
	virtual	Socket * 	socket(int type, short protocol);
	virtual 	int 			choose(
									int 		type, 
									char * 	prompt, 
									void * 	constraint,		
									int 		flags,
									void * 	name, 
									int * 	namelen);
};

extern PPCSocketDomain	PPCSockets;
