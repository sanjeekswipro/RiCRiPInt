#######################################################################
#	Project	:	GUSI				-	Grand Unified Socket Interface
#	File		:	Makefile			-	Build everything except examples
#	Author	:	Matthias Neeracher
#	Language	:	MPW Shell/Make
#
#  $Log: Makefile,v $
#  Revision 1.4  1999/04/28  08:55:39  peterg
#  Back out the changes -- the JamGUSI library doesn't work
#
#  Revision 1.3  1999/04/23  11:11:54  build
#  Build a static version of the GUSI library
#
#  Revision 1.2  1999/04/23  08:49:17  peterg
#  Automatic checkin:
#  changed attribute _comment to '#  '
#
#  Revision 1.1  1999/04/22  16:05:50  build
#  new unit
#  Grand Unified Socket Interface
#
#  Revision 1.10  1995/01/23  01:28:54  neeri
#  Version 1.5.2
#
#  Revision 1.9  1995/01/08  22:14:27  neeri
#  Needed to bump PEF version numbers, too.
#
#  Revision 1.8  1995/01/08  22:11:18  neeri
#  1.5.1 Distribution
#
#  Revision 1.7  1994/12/31  03:24:16  neeri
#  1.5 Distribution.
#
#  Revision 1.6  1994/12/30  20:23:34  neeri
#  Bumped PowerPC version number to 1.5 final.
#
#  Revision 1.5  1994/08/10  00:32:35  neeri
#  Sanitized for universal headers.
#
#  Revision 1.4  1994/05/04  01:46:02  neeri
#  Version 1.4.1
#
#  Revision 1.3  1994/05/01  23:45:47  neeri
#  Install utime.h.
#
#  Revision 1.2  1994/02/25  01:14:06  neeri
#  Clean up log.
#
#  Revision 1.1  1994/02/25  00:46:15  neeri
#  Initial revision
#
#	Revision 0.29  1994/01/11  00:00:00  neeri
#	Release 1.3.5
#	
#	Revision 0.28  1993/11/17  00:00:00  neeri
#	Release 1.3.4, introducing GUSIRemoteConsole
#	
#	Revision 0.27  1993/07/17  00:00:00  neeri
#	Release 1.3.0
#	
#	Revision 0.26  1993/06/20  00:00:00  neeri
#	Release 1.2.0
#	
#	Revision 0.25  1993/06/01  00:00:00  neeri
#	Introduce more conservative dependences
#	
#	Revision 0.24  1993/04/04  00:00:00  neeri
#	Release 1.1.1
#	
#	Revision 0.23  1993/03/14  00:00:00  neeri
#	Use Stuff to build distribution
#	
#	Revision 0.22  1993/01/09  00:00:00  neeri
#	Make list of targets to be built more flexible
#	
#	Revision 0.21  1992/12/18  00:00:00  neeri
#	Watch out for filenames with spaces
#	
#	Revision 0.20  1992/12/06  00:00:00  neeri
#	Automatic Distribution
#	
#	Revision 0.19  1992/11/14  00:00:00  neeri
#	Subset libraries were not working correctly
#	
#	Revision 0.18  1992/10/27  00:00:00  neeri
#	More robust deletion
#	
#	Revision 0.17  1992/10/18  00:00:00  neeri
#	Separate clean and dirty variants
#	
#	Revision 0.16  1992/09/24  00:00:00  neeri
#	Install
#	
#	Revision 0.15  1992/09/08  00:00:00  neeri
#	GUSISocketTest.c
#	
#	Revision 0.14  1992/09/08  00:00:00  neeri
#	Subset libraries
#	
#	Revision 0.13  1992/09/01  00:00:00  neeri
#	Make GUSI configurable
#	
#	Revision 0.12  1992/08/16  00:00:00  neeri
#	TCP/IP files: GUSIINET.cp, GUSITCP.cp, GUSIUDP.cp
#	
#	Revision 0.11  1992/08/03  00:00:00  neeri
#	GUSIBuffer.cp
#	
#	Revision 0.10  1992/08/02  00:00:00  neeri
#	GUSIPPCTest
#	
#	Revision 0.9  1992/07/26  00:00:00  neeri
#	Rewrote GUSIAtlkTest
#	
#	Revision 0.8  1992/07/25  00:00:00  neeri
#	GUSITest
#	
#	Revision 0.7  1992/06/15  00:00:00  neeri
#	GUSIFSp
#	
#	Revision 0.6  1992/06/06  00:00:00  neeri
#	PPC Toolbox domain
#	
#	Revision 0.5  1992/05/12  00:00:00  neeri
#	GUSIAtlkTest
#	
#	Revision 0.4  1992/04/19  00:00:00  neeri
#	Rewrite in C++
#	
#	Revision 0.3  1992/04/17  00:00:00  neeri
#	Reorganize Libraries
#	
#	Revision 0.2  1992/04/12  00:00:00  neeri
#	GUSIUnixTest
#	
#	Revision 0.1  1992/03/24  00:00:00  neeri
#	GUSIDispatch
#	
#######################################################################

#######################################################################
# Configurable parts
#

#
# Paths where GUSI components will go 
#

GUSICIncludes		=	{CIncludes}
GUSI68KCLibraries	=	{CLibraries}
GUSIPPCCLibraries	=	{PPCLibraries}
GUSIRIncludes		=	{RIncludes}

#
# Specify one of:
#  68K			Build 68K version only
#	PPC			Build PPC version only
#	Both			Build both 68K and PPC versions
#

Focus		=	Both

#
# Comment out GUSI.ps if you don't have SpiderWEB and TeXShop
#

GUSI_PS	=	GUSI.ps

#
# NO USER SERVICEABLE PARTS BELOW
#

#######################################################################
# Defines
#

#
# Header files 
#

GUSI_HEADERS =						¶
	GUSI.h							¶
	GUSI_P.h							¶
	GUSIFile_P.h					¶
	TFileSpec.h						¶
	compat.h							¶
	dirent.h							¶
	'machine/ansi.h'				¶
	'machine/endian.h'			¶
	'machine/types.h'				¶
	netdb.h							¶
	'netinet/in.h'					¶
	'netinet/tcp.h'				¶
	'sys/cdefs.h'					¶
	'sys/errno.h'					¶
	'sys/fcntl.h'					¶
	'sys/ioctl.h'					¶
	'sys/socket.h'					¶
	'sys/stat.h'					¶
	'sys/time.h'					¶
	'sys/types.h'					¶
	'sys/uio.h'						¶
	'sys/un.h'						¶
	'sys/unistd.h'					¶
	unistd.h							¶
	utime.h

#
#	68K Object Files
#

GUSI_OBJECTS =								¶
	:Obj:GUSIFSp.cp.o.68K				¶
	:Obj:GUSIDispatch.cp.o.68K			¶
	:Obj:GUSIFileDispatch.cp.o.68K	¶
	:Obj:GUSISocket.cp.o.68K			¶
	:Obj:GUSIFile.cp.o.68K				¶
	:Obj:GUSIMPWFile.cp.o.68K			¶
	:Obj:GUSISIOW.cp.o.68K				¶
	:Obj:GUSIBuffer.cp.o.68K			¶
	:Obj:GUSIAppleTalk.cp.o.68K		¶
	:Obj:GUSIPPC.cp.o.68K				¶
	:Obj:GUSIINET.cp.o.68K				¶
	:Obj:GUSITCP.cp.o.68K				¶
	:Obj:GUSIUDP.cp.o.68K				¶
	:Obj:GUSINetDB.cp.o.68K				¶
	:Obj:GUSIPAP.cp.o.68K				¶
	:Obj:GUSIUnix.cp.o.68K				¶
	:Obj:dnr.c.o.68K

#
#	PowerPC Object Files
#

GUSI_PPC_OBJECTS =							¶
	:PPC_Obj:GUSIFSp.cp.o.PPC				¶
	:PPC_Obj:GUSIDispatch.cp.o.PPC		¶
	:PPC_Obj:GUSIFileDispatch.cp.o.PPC	¶
	:PPC_Obj:GUSIMPWGlue.cp.o.PPC			¶
	:PPC_Obj:GUSISocket.cp.o.PPC			¶
	:PPC_Obj:GUSIFile.cp.o.PPC				¶
	:PPC_Obj:GUSIMPWFile.cp.o.PPC			¶
	:PPC_Obj:GUSIBuffer.cp.o.PPC			¶
	:PPC_Obj:GUSIAppleTalk.cp.o.PPC		¶
	:PPC_Obj:GUSIPPC.cp.o.PPC				¶
	:PPC_Obj:GUSIINET.cp.o.PPC				¶
	:PPC_Obj:GUSITCP.cp.o.PPC				¶
	:PPC_Obj:GUSIUDP.cp.o.PPC				¶
	:PPC_Obj:GUSINetDB.cp.o.PPC			¶
	:PPC_Obj:GUSIPAP.cp.o.PPC				¶
	:PPC_Obj:GUSIUnix.cp.o.PPC				¶
	:PPC_Obj:dnr.c.o.PPC

#
# PowerPC Export Files
#

GUSI_PPC_EXPORTS =						¶
	GUSIAppleTalk.cp.x					¶
	GUSIBuffer.cp.x						¶
	GUSIDispatch.cp.x						¶
	GUSIFile.cp.x							¶
	GUSIFileDispatch.cp.x				¶
	GUSIFSp.cp.x							¶
	GUSIINET.cp.x							¶
	GUSIMPWFile.cp.x						¶
	GUSIMPWGlue.cp.x						¶
	GUSINetDB.cp.x							¶
	GUSIPAP.cp.x							¶
	GUSIPPC.cp.x							¶
	GUSISocket.cp.x						¶
	GUSITCP.cp.x							¶
	GUSIUDP.cp.x							¶
	GUSIUnix.cp.x							¶
	dnr.c.x

#
# 68K Specific Targets
#

GUSI_TARGETS =						¶
	:Lib:GUSI.o 					¶
	:Lib:GUSI_Everything.cfg	¶
	:Lib:GUSI_Appletalk.cfg		¶
	:Lib:GUSI_Internet.cfg		¶
	:Lib:GUSI_Unix.cfg			¶
	:Lib:GUSI_PPC.cfg				¶
	:Lib:GUSI_PAP.cfg				¶
	:Lib:GUSIRemoteConsole.o	¶
	:Lib:GUSIINETd.o

#
# PowerPC Specific Targets
#

GUSI_PPC_TARGETS =				¶
	:Lib:GUSI.xcoff				¶
	:Lib:GUSI						¶
	:Lib:GUSISIOW.xcoff			¶
	:Lib:GUSIINETd.xcoff
#	:Lib:GUSIRemoteConsole.o

#
# PowerPC LibrariesÉ
#

PPCLibs = 											¶
		"{PPCLibraries}"InterfaceLib.xcoff	¶
		"{PPCLibraries}"StdCLib.xcoff			¶
		"{PPCLibraries}"CPlusLib.o				¶
		"{PPCLibraries}"StdCRuntime.o			¶
		"{PPCLibraries}"PPCToolLibs.o			¶
		"{PPCLibraries}"PPCCRuntime.o

#
# É and how the shared ones have to be renamed.
#

PPCLibEqu = 													¶
		-l InterfaceLib.xcoff=InterfaceLib 				¶
      -l StdCLib.xcoff=StdCLib

#
# 68K debugging options
#

Debug68K				=	-sym on -mbg on

#
# PowerPC debugging options. Note that -sym on disables optimization!
#

DebugPPC				=	-sym on

#
# PowerPC optimization options. Note that -sym on above disables optimization!
#

PPCCOpt				=	-opt local

#
# Tool options
#

GUSIInc				=	-i : -i :include
COptions 			= 	-r {GUSIInc} {Debug68K}
CPlusOptions 		= 	-mf {GUSIInc} {Debug68K}
PPCCExt				=	-d macintosh -appleext on
PPCCOptions 		=  -shared_lib_export on {PPCCOpt} {PPCCExt} {GUSIInc} {DebugPPC}
LOptions				= 	-sym on -mf -w
ROptions 			= 	-i :

#
# Default Build Rules
#

:Obj:			Ä	:
:PPC_Obj:	Ä	:
.c.o.68K	Ä	.c
	{C} {COptions} {DepDir}{Default}.c -o {TargDir}{Default}.c.o.68K
.c.o.PPC	Ä	.c
	PPCC {PPCCOptions} {DepDir}{Default}.c -o {TargDir}{Default}.c.o.PPC
.cp.o.68K	Ä	.cp
	{CPlus} {CPlusOptions} {DepDir}{Default}.cp -o {TargDir}{Default}.cp.o.68K
.cp.o.PPC	Ä	.cp
	PPCC {PPCCOptions} {DepDir}{Default}.cp -o {TargDir}{Default}.cp.o.PPC

#######################################################################
# Top Level Targets
#

Help			Ä
	Echo "Top level targets are:"
	Echo "¶tInstall    - Install GUSI"
	Echo "¶tUninstall  - Try to remove GUSI"
	Echo "¶tGUSI       - Rebuild GUSI (use with care)"
	Echo "¶tGUSITest   - Build GUSI Examples"

#
# Build
#

GUSI.Common		Ä	GUSI.ps

GUSI.PPC			Ä	{GUSI_PPC_TARGETS} GUSI.Common

GUSI.68K			Ä	{GUSI_TARGETS} GUSI.Common

GUSI.Both		Ä	GUSI.PPC GUSI.68K

GUSI 				Ä	GUSI.{Focus}

#
# Install
#

Install.Common	Ä	
	:SafeInstall -to "{GUSICIncludes}" -from include	{GUSI_HEADERS} GUSIRsrc_P.h
	:SafeInstall -to "{GUSIRIncludes}"	GUSI.r GUSI.rsrc :include:GUSIRsrc_P.h
	:SafeDel {OBSOLETE_HEADERS}
	:SafeInstall -y -to `findfolder -preferences`	'/etc/services'

Install.PPC		Ä	Install.Common
	:SafeInstall -to "{GUSIPPCCLibraries}"						{GUSI_PPC_TARGETS}

Install.68K		Ä	Install.Common
	:SafeInstall -to "{GUSI68KCLibraries}"						{GUSI_TARGETS}

Install.Both	Ä	Install.PPC Install.68K

Install			Ä	Install.{Focus}

#
# Uninstall
#

Uninstall.Common	Ä
	For I in {GUSI_HEADERS}
		:SafeDel "{GUSICIncludes}{i}"
	End	
	:SafeDel "{RIncludes}GUSI.r" "{RIncludes}GUSI.rsrc" "{RIncludes}GUSIRsrc_P.h" 
	
Uninstall.68K		Ä	Uninstall.Common
	For I in {GUSI_TARGETS}
		:SafeDel "{GUSI68KCLibraries}{i}"
	End	
	
Uninstall.PPC		Ä	Uninstall.Common
	For I in {GUSI_PPC_TARGETS}
		:SafeDel "{GUSIPPCCLibraries}{i}"
	End	

#
# Examples
#
	
GUSITest					GUSIFileTest			¶
GUSIFinger				GUSIPAPTest				¶
GUSIUnixTest			GUSIUnixTest2			¶
GUSIAtlkTest			GUSIAtlkTest2			¶
GUSIPPCTest				GUSIPPCTest2			¶
GUSIINETTest			GUSIINETTest2			¶
GUSITest.68K			GUSIFileTest.68K		¶
GUSIFinger.68K			GUSIPAPTest.68K		¶
GUSIUnixTest.68K		GUSIUnixTest2.68K		¶
GUSIAtlkTest.68K		GUSIAtlkTest2.68K		¶
GUSIPPCTest.68K		GUSIPPCTest2.68K		¶
GUSIINETTest.68K		GUSIINETTest2.68K		¶
GUSITest.PPC			GUSIFileTest.PPC		¶
GUSIPAPTest.PPC									¶
GUSIAtlkTest.PPC		GUSIAtlkTest2.PPC		¶
GUSIPPCTest.PPC		GUSIPPCTest2.PPC		¶
GUSIINETTest.PPC		GUSIINETTest2.PPC		¶
GUSITest.Both			GUSIFileTest.Both		¶
GUSIFinger.Both		GUSIPAPTest.Both		¶
GUSIUnixTest.Both		GUSIUnixTest2.Both	¶
GUSIAtlkTest.Both		GUSIAtlkTest2.Both	¶
GUSIPPCTest.Both		GUSIPPCTest2.Both		¶
GUSIINETTest.Both		GUSIINETTest2.Both	Ä	$OutOfDate
	Delete :Examples:GUSI
	Perl -e 'symlink(":Lib:GUSI", ":Examples:GUSI")'
	Directory Examples
		Set OldEcho {Echo}
		Set Echo 0
		BuildProgram {Targ}
		Set Echo {OldEcho}
	Directory ::

GUSICleanDep	Ä
	:SafeDel ':Obj:Å.dn' ':Obj:Å.rn' ':Obj:Å.df'

Clean	Ä
	:SafeDel ':Obj:Å' ':PPC_Obj:Å' ':Examples:Obj:Å.o' 	¶
		'Å.toc' 'Å.tex' 'Å.log' 'Å.dump' ':Examples:Å.xÅ'

RealClean Ä	Clean
	:SafeDel 'Å.dvi' 'Å.ps' ':Lib:Å' '-t APPL :Examples:GUSIÅ'

Distribution Ä GUSI
	:Distribute GUSI.distr GUSI_152.sit

Distr Ä Distribution

#######################################################################
# Standard Library Manipulations
#
	
:Obj:Alias.dump 	Ä  "{Libraries}Runtime.o" "{CLibraries}StdCLib.o" "{CLibraries}CPlusLib.o"
	DumpObj -mods "{Libraries}Runtime.o" > :Obj:Alias.dump
	DumpObj -mods "{CLibraries}StdCLib.o" >> :Obj:Alias.dump
	DumpObj -mods "{CLibraries}CPlusLib.o" >> :Obj:Alias.dump
	
:Obj:Alias.rn	Ä	Aliases :Obj:Alias.dump
	MakeAliases :Obj:Alias.dump

:Obj:GUSI.dn		Ä	:Obj:preGUSI.o GUSI.ext
	DumpObj -mods :Obj:preGUSI.o	| Internalize Obj 68K

:PPC_Obj:GUSI.export	Ä	GUSI.ext {GUSI_PPC_OBJECTS}
	Internalize PPC_Obj PPC {GUSI_PPC_EXPORTS}

:Obj:StdLib.o	Ä :Obj:Alias.rn
	Lib -w -o :Obj:StdLib.o				¶
		-sg GUSILib=Main					¶
		`Catenate :Obj:Alias.rn`		¶
		"{Libraries}Runtime.o"			¶
		"{CLibraries}StdCLib.o"			¶
		"{CLibraries}CPlusLib.o"		¶
		-df :Obj:Alias1.df

#######################################################################
# Library Build
#

:Obj:preGUSI.o	Ä	{GUSI_OBJECTS} :Obj:Alias.rn :Obj:StdLib.o
	Lib -p -w -o :Obj:preGUSI.o	¶
		{GUSI_OBJECTS}				¶
		:Obj:StdLib.o				¶
		-sg Main=GUSIResident	¶
		-df :Obj:Alias.df

:Lib:GUSI.o		Ä	:Obj:preGUSI.o :Obj:GUSI.dn
	Set Echo 0
	Echo "¶t¶tLib" -w -o :Lib:GUSI.o ...
	Lib -w -o :Lib:GUSI.o :Obj:preGUSI.o `Catenate :Obj:GUSI.dn`
	Set Echo 1

:Lib:GUSI_Everything.cfg	Ä	GUSICfg.c
	C -d GUSI_Everything -o :Lib:GUSI_Everything.cfg GUSICfg.c

:Lib:GUSI_Appletalk.cfg	Ä	GUSICfg.c
	C -d GUSI_Appletalk -o :Lib:GUSI_Appletalk.cfg GUSICfg.c

:Lib:GUSI_Internet.cfg	Ä	GUSICfg.c
	C -d GUSI_Internet -o :Lib:GUSI_Internet.cfg GUSICfg.c

:Lib:GUSI_Unix.cfg	Ä	GUSICfg.c
	C -d GUSI_Unix -o :Lib:GUSI_Unix.cfg GUSICfg.c

:Lib:GUSI_PPC.cfg	Ä	GUSICfg.c
	C -d GUSI_PPC -o :Lib:GUSI_PPC.cfg GUSICfg.c

:Lib:GUSI_PAP.cfg	Ä	GUSICfg.c
	C -d GUSI_PAP -o :Lib:GUSI_PAP.cfg GUSICfg.c

:Lib:GUSIRemoteConsole.o	Ä	GUSIRemoteConsole.cp
	{CPlus} {CPlusOptions} GUSIRemoteConsole.cp -o :Lib:GUSIRemoteConsole.o

:Lib:GUSIINETd.o	Ä	GUSIINETd.cp
	{CPlus} {CPlusOptions} GUSIINETd.cp -o :Lib:GUSIINETd.o

:Lib:GUSIINETd.xcoff	Ä	GUSIINETd.cp
	PPCC {PPCCOptions} GUSIINETd.cp -o :Lib:GUSIINETd.xcoff

:Lib:GUSI.xcoff	Ä	{GUSI_PPC_OBJECTS} :PPC_Obj:GUSI.export
	PPCLink -xm sharedLibrary {GUSI_PPC_OBJECTS} {PPCLibs} ¶
		-export `Catenate :PPC_Obj:GUSI.export` -sym on -o :Lib:GUSI.xcoff
	MakeSym -cttes :Lib:GUSI.xcoff
	
:Lib:GUSI			ÄÄ	:Lib:GUSI.xcoff
	MakePEF :Lib:GUSI.xcoff -ft 'shlb' -fc 'cfrg'  -o :Lib:GUSI {PPCLibEqu} ¶
		-i GUSIInit -c 0x01528000 -d 0x01508000 -u 0x01508000
	
:Lib:GUSI			ÄÄ GUSICfrg.r
	Rez GUSICfrg.r -a -o :Lib:GUSI

:Lib:GUSISIOW.xcoff	Ä	:Obj:GUSISIOW.cp.o.PPC
	PPCLink -xm library :Obj:GUSISIOW.cp.o.PPC -o :Lib:GUSISIOW.xcoff

#######################################################################
# Clean Namespace
#

GUSI_C.o		Ä	GUSI.o GUSI.dn
	Lib -o GUSI_C.o				¶
		GUSI.o						¶
		`Catenate GUSI_C.dn`

#######################################################################
# Documentation
#
	
GUSI.tex			Ä	GUSI.web
	cweave GUSI
	
GUSI.dvi			Ä	GUSI.tex
	tex GUSI

GUSI.ps			Ä	GUSI.dvi
	dvips -a -Z GUSI.dvi -o GUSI.ps
