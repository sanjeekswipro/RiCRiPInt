/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIMPWGlue.cp	-	Internals of MPW runtime architecture
Author	:	Matthias Neeracher
Language	:	MPW C/C++

$Log: GUSIMPWGlue.cp,v $
Revision 1.2  1999/04/23  08:43:57  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.2  1994/12/30  20:12:45  neeri
New file name dispatch scheme.
Repair problems with ROM PowerPC library.

Revision 1.1  1994/08/10  00:22:29  neeri
Initial revision

*********************************************************************/

#if !defined(powerc) && !defined(__powerc)
#error "This file is most likely neither compatible nor needed with your compiler"
#else
#include "GUSI_P.h"
#include "GUSIMPW_P.h"

#include <IOCtl.h>

enum {
	uppMPWFAccessInfo = kCStackBased
		 | RESULT_SIZE(SIZE_CODE(sizeof(int32)))
		 | STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(char *)))
		 | STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(unsigned int)))
		 | STACK_ROUTINE_PARAMETER(3, SIZE_CODE(sizeof(long *)))
};

enum {
	uppMPWIOInfo = kCStackBased
		 | RESULT_SIZE(SIZE_CODE(sizeof(int32)))
		 | STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(MPWIOEntry *)))
};

enum {
	uppMPWIOCtlInfo = kCStackBased
		 | RESULT_SIZE(SIZE_CODE(sizeof(int32)))
		 | STACK_ROUTINE_PARAMETER(1, SIZE_CODE(sizeof(MPWIOEntry *)))
		 | STACK_ROUTINE_PARAMETER(2, SIZE_CODE(sizeof(unsigned int)))
		 | STACK_ROUTINE_PARAMETER(3, SIZE_CODE(sizeof(long *)))
};

RoutineDescriptor MPWFAccessDesc 	= BUILD_ROUTINE_DESCRIPTOR(uppMPWFAccessInfo, 	MPWFAccessGlue);
RoutineDescriptor MPWCloseDesc 		= BUILD_ROUTINE_DESCRIPTOR(uppMPWIOInfo, 			MPWCloseGlue);
RoutineDescriptor MPWReadDesc 		= BUILD_ROUTINE_DESCRIPTOR(uppMPWIOInfo, 			MPWReadGlue);
RoutineDescriptor MPWWriteDesc 		= BUILD_ROUTINE_DESCRIPTOR(uppMPWIOInfo, 			MPWWriteGlue);
RoutineDescriptor MPWIOCtlDesc 		= BUILD_ROUTINE_DESCRIPTOR(uppMPWIOCtlInfo, 		MPWIOCtlGlue);

MPWIOVector	GUSIIOGlue	=	{
	0, 
	&MPWFAccessDesc, 
	&MPWCloseDesc,
	&MPWReadDesc,
	&MPWWriteDesc,
	&MPWIOCtlDesc
};

inline int32 MapError(MPWIOEntry * entry, int res)
{
	entry->error 	=	noErr;
	return (res < 0) ? errno : 0;
}

int32 MPWFAccessGlue(char * name, unsigned int cmd, long *arg)
{
	if (cmd != F_OPEN)
		return EINVAL;
	
	if (strcmp(name, "dev:console"))
		if (CatchStdIO ) {
			if (!strcmp(name, "stdin"))
				close(0);
			else if (!strcmp(name, "stdout"))
				close(1);
			else if (!strcmp(name, "stderr"))
				close(2);
			else 
				return -1;
			name = "dev:console";
		} else 
			return -1;
		
	return open(name, (int) arg) < 0 ? errno : 0;
}

int32	MPWCloseGlue(MPWIOEntry * entry)
{
	return MapError(entry, close(entry->userData));
}

int32	MPWReadGlue(MPWIOEntry * entry)
{
	int32	res;
	
	res = read(entry->userData, entry->buffer, entry->length);
	
	if (res > 0) {
		entry->buffer += res;
		entry->length -= res;
	}
	
	return MapError(entry, res);
}

int32	MPWWriteGlue(MPWIOEntry * entry)
{
	int32	res;
	
	res = write(entry->userData, entry->buffer, entry->length);
	
	if (res > 0) {
		entry->buffer += res;
		entry->length -= res;
	}
	
	return MapError(entry, res);
}

int32	MPWIOCtlGlue(MPWIOEntry * entry, unsigned int cmd, long *arg)
{
	int 		res;
	Socket *	sock;
	
	switch (cmd) {
	case FIOLSEEK:
		res = lseek(entry->userData, ((MPWSeekArg *)arg)->offset, ((MPWSeekArg *)arg)->whence);
		break;
	case FIODUPFD:
		sock	=	Sockets[entry->userData];
		
		res = sock ? Sockets.Install(sock, entry->userData, int32(arg)) : -1;
		break;
	default:
		res = ioctl(entry->userData, cmd, arg);
		break;
	}
	
	return MapError(entry, res);
}

extern "C" void __CPlusInit();

extern "C" void GUSIInit()
{
	__CPlusInit();
}

#endif