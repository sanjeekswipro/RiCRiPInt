# $Id: Makefile,v 1.16 2014/04/02 19:18:01 rogerb Exp $
# $HopeName: HQNjam!Makefile(trunk.16) $

SOURCES = \
	command.c compile.c execunix.c execvms.c expand.c \
	filent.c fileos2.c fileunix.c filevms.c glob.c hash.c \
	headers.c jam.c jamgram.c lists.c make.c make1.c \
	newstr.c option.c parse.c pathunix.c pathvms.c regexp.c \
	rules.c scan.c search.c timestamp.c variable.c \
	harlequin.c

# Define default constants.
CC = cc
TARGET = -o jam
CFLAGS = -O2
RESOURCES =
LINKLIBS =

# "Generic" Unix - works for Linux (all distributions, I assume - "Linux" being the kernal) and Irix 6 on MIPS.
# The C code contains #ifdef lines to spot different platforms and define constants as appropriate.
UNIXCC = $(CC)
UNIXTARGET = $(TARGET)
UNIXCFLAGS = $(CFLAGS)
UNIXRESOURCES = $(RESOURCES)
UNIXLINKLIBS = $(LINKLIBS)

# Mac OS X. We need a bigger stack on Mac OS X for Jam to work.
MACOSXCC = $(CC)
MACOSXTARGET = $(TARGET)
MACOSXCFLAGS = $(CFLAGS) -Wl,-stack_size,100000
MACOSXRESOURCES = $(RESOURCES)
MACOSXLINKLIBS = $(LINKLIBS)

# Mac OS X universal binary.
# We need a bigger stack on Mac OS X for Jam to work.
MACOSXUBCC = /usr/bin/gcc-4.0
MACOSXUBTARGET = $(TARGET)
MACOSXUBCFLAGS = $(CFLAGS) -Wl,-stack_size,100000 -isysroot /Developer/SDKs/MacOSX10.4u.sdk -Wl,-syslibroot,/Developer/SDKs/MacOSX10.4u.sdk -arch i386 -arch ppc
MACOSXUBRESOURCES = $(RESOURCES)
MACOSXUBLINKLIBS = $(LINKLIBS)

# Solaris 2 on SPARC.
SOLARIS2CC = gcc
SOLARIS2TARGET = $(TARGET)
SOLARIS2CFLAGS = $(CFLAGS)
SOLARIS2RESOURCES = $(RESOURCES)
SOLARIS2LINKLIBS = $(LINKLIBS)

# AIX needs -lbsd, and has no identifying cpp symbol
# Use _AIX41 if you're not on 3.2 anymore.
AIXCC = ($CC)
AIXTARGET = $(TARGET)
AIXCFLAGS = -D_AIX
AIXRESOURCES = $(RESOURCES)
AIXLINKLIBS = -lbsd

# NT (with Microsoft VC6 compiler)
# Use FATFS if building on a DOS FAT file system
NTCC = cl /nologo
NTTARGET = /Fejam
# Optimisation CFLAGS: -Ox
# Debugging CFLAGS: -MLd -Od -Yd -Zi
Include = "$(CV_MSVCNT_6_0)\\include"
NTCFLAGS = /I $(Include) -DNT -Ox
NTRESOURCES = rc /i $(Include) jam.rc
Lib = "$(CV_MSVCNT_6_0)\\lib"
NTSOURCES=$(SOURCES)
NTLINKLIBS = $(Lib)\\oldnames.lib $(Lib)\\kernel32.lib $(Lib)\\libc.lib $(Lib)\\uuid.lib jam.res

# Win32 (with Microsoft VC10 compiler)
WINCC = cl /nologo
WINTARGET = /Fejam
# Optimisation CFLAGS: -Ox
# Debugging CFLAGS: -MTd -Od -Yd -Zi
WINCFLAGS = -DNT -Ox -MT
WINSDKDIR = C:\Program Files\Microsoft SDKs\Windows\v7.0A
WINSDKINC = "$(WINSDKDIR)\include"
WINSDKLIB = "$(WINSDKDIR)\lib"
WINVCINC = "$(CV_MSVCNT_10_0)\include"
WINVCLIB = "$(CV_MSVCNT_10_0)\lib"
WINRESOURCES = rc /i $(WINSDKINC) /i $(WINVCINC) jam.rc
WINSOURCES=$(SOURCES)
WINLINKLIBS = $(WINVCLIB)\oldnames.lib $(WINSDKLIB)\kernel32.lib $(WINSDKLIB)\uuid.lib $(WINVCLIB)\libcmt.lib jam.res


Unix:
	$(UNIXRESOURCES)
	$(UNIXCC) $(UNIXTARGET) $(UNIXCFLAGS) $(SOURCES) $(UNIXLINKLIBS)

MacOSX:
	$(MACOSXRESOURCES)
	$(MACOSXCC) $(MACOSXTARGET) $(MACOSXCFLAGS) $(SOURCES) $(MACOSXLINKLIBS)

MacOSXUB:
	$(MACOSXUBRESOURCES)
	$(MACOSXUBCC) $(MACOSXUBTARGET) $(MACOSXUBCFLAGS) $(SOURCES) $(MACOSXUBLINKLIBS)

Solaris2:
	$(SOLARIS2RESOURCES)
	$(SOLARIS2CC) $(SOLARIS2TARGET) $(SOLARIS2CFLAGS) $(SOURCES) $(SOLARIS2LINKLIBS)

AIX:
	$(AIXRESOURCES)
	$(AIXCC) $(AIXTARGET) $(AIXCFLAGS) $(AIXSOURCES) $(AIXLINKLIBS)

NT:
	$(NTRESOURCES)
	$(NTCC) $(NTTARGET) $(NTCFLAGS) $(NTSOURCES) $(NTLINKLIBS)

Win32:
	$(WINRESOURCES)
	$(WINCC) $(WINTARGET) $(WINCFLAGS) $(WINSOURCES) $(WINLINKLIBS)

# $Log: Makefile,v $
# Revision 1.16  2014/04/02  19:18:01  rogerb
# [Bug #369545]
# need explicit include/library paths when using nmake
#
# Revision 1.15  2014/04/02  18:23:58  rogerb
# [Bug #369545]
# add Win32 target for building with MSVC 10  ( and later )
#
# Revision 1.14  2007/10/12  08:37:16  miker
# [Bug #1]
# Add Mac OS X universal binary
#
# Revision 1.13  2004/06/02  14:26:14  johnk
# Remove hard coded include PATH on Windows
# Add SOURCES to compile
# Move to VC 6
#
# Revision 1.12  2002/10/04  13:50:40  dhicks
# Fixing errors introduced by emacs.
#
# Revision 1.11  2002/10/04  13:46:14  build
# Change stack size for Mac OS X build.
#
# Revision 1.10  2002/09/26  14:30:19  dhicks
# Add some more to it.
#
# Revision 1.9  2002/09/26  11:16:37  dhicks
# Fixing documentation
#
# Revision 1.8  2002/09/26  09:11:20  dhicks
# Adding conditional compiles for different platforms.
#
# Revision 1.7  2002/01/30  11:59:39  peterc
# [Bug #24757]
# Use our CV variable to locate MSVC on Windows
