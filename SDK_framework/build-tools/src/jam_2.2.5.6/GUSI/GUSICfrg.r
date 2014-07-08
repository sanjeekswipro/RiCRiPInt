/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSICfrg.r		-	Resources for shared library
Author	:	Matthias Neeracher
Language	:	MPW Rez 3.0

$Log: GUSICfrg.r,v $
Revision 1.2  1999/04/23  08:56:48  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.4  1995/01/23  01:27:57  neeri
Version 1.5.2

Revision 1.3  1995/01/08  22:15:04  neeri
Version 1.5.1

Revision 1.2  1994/12/30  19:54:53  neeri
Bump version number to 1.5 final.

Revision 1.1  1994/12/30  19:42:53  neeri
Initial revision

*********************************************************************/

#include "SysTypes.r"
#include "CodeFragmentTypes.r"

resource 'cfrg' (0) {
	{
      kPowerPC,				/* Target machine's Architecture. */
      kFullLib,				/* This is not an update. */
		0x01528000,				/* Current version. */
		0x01508000,				/* Definition version. */
		kDefaultStackSize,	/* Stack size of application. */
		kNoAppSubFolder,		/* Not used here.  Can be the resource-id of an 'alis'
	                           resource.  Used to provide additional location
							   		to search for libraries. */
	  	kIsLib,					/* This is an shard library. */
	  	kOnDiskFlat,     		/* This code fragment is on disk, in the data fork. */
	  	kZeroOffset,		   /* Offset of code into data fork. */
	  	kWholeFork,     		/* Code takes up all of data fork (can give a size). */
	  	"GUSI"        			/* Name of application. */
   }
};

resource 'vers' (1) {
	0x01, 0x52, release, 0x00, verUS,
	"1.5.2",
	"GUSI 1.5.2 Copyright © 1992-1994 Matthias Neeracher"
};
