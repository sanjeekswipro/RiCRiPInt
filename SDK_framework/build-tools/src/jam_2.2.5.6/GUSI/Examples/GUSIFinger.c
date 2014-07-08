/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIFinger.c	-	finger daemon
Author	:	Matthias Neeracher
Language	:	MPW C

$Log: Examples:GUSIFinger.c,v $
Revision 1.2  1999/04/23  08:56:43  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.1  1994/02/25  02:47:06  neeri
Initial revision

*********************************************************************/

#include <stdio.h>
#include <Resources.h>
#include <Processes.h>
#include <Memory.h>
#include <QuickDraw.h>
#include <Fonts.h>
#include <Windows.h>

void main()
{
	Handle	macName;
	Handle	whoName;
	
	long	secs;
	long	mins;
	long	hour;
	
	InitGraf(&qd.thePort);
	
	macName = GetResource('STR ', -16413);
	HLock(macName);
	whoName = GetResource('STR ', -16096);
	HLock(whoName);

	secs = TickCount() / 60;
	mins = secs / 60;
	hour = mins / 60;
	
	secs = secs - mins * 60;
	mins = mins - hour * 60;
	
	printf("Macintosh belongs to:\t%P\r", *whoName);
	printf("Macintosh is named:\t%P\r", *macName);
	printf("Has been up for:\t%02d:%02d:%02d\r", hour, mins, secs);
}

