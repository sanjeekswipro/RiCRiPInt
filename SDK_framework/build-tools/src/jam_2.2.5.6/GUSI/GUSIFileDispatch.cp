/*********************************************************************
Project	:	GUSI						-	Grand Unified Socket Interface
File		:	GUSIFileDispatch.cp	-	Dispatch calls to their correct recipient
Author	:	Matthias Neeracher
Language	:	MPW C/C++

$Log: GUSIFileDispatch.cp,v $
Revision 1.2  1999/04/23  08:43:35  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.1  1994/12/30  19:59:55  neeri
Initial revision

*********************************************************************/

#include "GUSIFile_P.h"
#include "GUSIMPW_P.h"
#include <IOCtl.h>
#include <Errors.h>
#include <utime.h>

#if defined(powerc) || defined(__powerc)
#include <FragLoad.h>
#endif

/************************ External routines *************************/

int open(const char * filename, int oflag)
{
	extern int 				StandAlone;
	static int 				ignore = 3;
	Socket * 				sock;
	int						fd;

	Sockets.InitConsole();

	GUSIFileRef				ref(filename, oflag & O_ALIAS);
	FileSocketDomain	*	domain;

#if !defined(powerc) && !defined(__powerc)
	// Standalone programs open console manually. We have the console 
	// already open, so we'll have to re-open it. This is extremely 
	// unelegant and should be thrown out somehow.
	
	if (StandAlone && ignore && ref.IsDevice()) {
		close(3 - ignore);
		
		--ignore;
	}
#endif

	if (domain = FileSocketDomain::FindDomain(ref, FileSocketDomain::willOpen)) {
		if (sock = domain->open(ref, oflag))
			if ((fd = Sockets.Install(sock)) != -1)
				return fd;
			else
				delete sock;
	
		if (!errno)
			return GUSI_error(ENOMEM);
		else
			return -1;
	} else
		return GUSI_error(EINVAL);
}

int creat(const char* filename)
{
	return open(filename, O_WRONLY | O_TRUNC | O_CREAT);
}

int remove(const char *filename)
{
	GUSIFileRef			ref(filename, true);
	FileSocketDomain*	dom;
	
	if (dom = FileSocketDomain::FindDomain(ref, FileSocketDomain::willRemove))
		return dom->remove(ref);
	else
		return GUSI_error(EINVAL);
}

int unlink(char* filename)
{
	GUSIFileRef			ref(filename, true);
	FileSocketDomain*	dom;
	
	if (dom = FileSocketDomain::FindDomain(ref, FileSocketDomain::willRemove))
		return dom->remove(ref);
	else
		return GUSI_error(EINVAL);
}

int rename(const char *oldname, const char *newname)
{
	GUSIFileRef			ref(oldname, true);
	FileSocketDomain*	dom;
	
	if (dom = FileSocketDomain::FindDomain(ref, FileSocketDomain::willRename))
		return dom->rename(ref, newname);
	else
		return GUSI_error(EINVAL);
}

void fsetfileinfo(char *filename, unsigned long newcreator, unsigned long newtype)
{
	GUSIFileRef			ref(filename);
	FileSocketDomain*	dom;
	
	if (dom = FileSocketDomain::FindDomain(ref, FileSocketDomain::willSetFileInfo))
		dom->fsetfileinfo(ref, newcreator, newtype);
	else
		GUSI_error(EINVAL);
}

void fgetfileinfo(char *filename, unsigned long * creator, unsigned long * type)
{
	GUSIFileRef			ref(filename);
	FileSocketDomain*	dom;
	
	if (dom = FileSocketDomain::FindDomain(ref, FileSocketDomain::willGetFileInfo))
		dom->fgetfileinfo(ref, creator, type);
	else
		GUSI_error(EINVAL);
}

int faccess(char* filename, unsigned int cmd, long* arg)
{
	switch (cmd) {
	case F_DELETE:
		return remove(filename);
	case F_RENAME:
		return rename(filename, (char *) arg);
	default:
		break;
	}
	
	GUSIFileRef			ref(filename);
	FileSocketDomain*	dom;

	if (dom = FileSocketDomain::FindDomain(ref, FileSocketDomain::willFAccess))
		return dom->faccess(ref, cmd, arg);
	else
		return GUSI_error(EINVAL);
}

int truncate(const char* filename, off_t offset)
{
	int	res;
	int	fd	= 	open(filename, O_RDWR);
	
	if (fd == -1)
		return -1;
	
	res = ftruncate(fd, offset);

	close(fd);
	
	return res;
}

int stat(const char * path, struct stat * buf)
{
	GUSIFileRef			ref(path);
	FileSocketDomain*	dom;
	
	if (dom = FileSocketDomain::FindDomain(ref, FileSocketDomain::willStat))
		return dom->stat(ref, buf);
	else
		return GUSI_error(EINVAL);
}

int	lstat(const char * path, struct stat * buf)
{
	GUSIFileRef			ref(path, true);
	FileSocketDomain*	dom;
	
	if (dom = FileSocketDomain::FindDomain(ref, FileSocketDomain::willStat))
		return dom->stat(ref, buf);
	else
		return GUSI_error(EINVAL);
}

int chmod(const char * filename, mode_t mode)
{
	GUSIFileRef			ref(filename, true);
	FileSocketDomain*	dom;
	
	if (dom = FileSocketDomain::FindDomain(ref, FileSocketDomain::willChmod))
		return dom->chmod(ref, mode);
	else
		return GUSI_error(EINVAL);	
}

int utime(const char * path, const struct utimbuf * times)
{
	GUSIFileRef			ref(path);
	FileSocketDomain*	dom;
	
	if (dom = FileSocketDomain::FindDomain(ref, FileSocketDomain::willUTime))
		return dom->utime(ref, times);
	else
		return GUSI_error(EINVAL);
}

/************************ class GUSIFileRef *************************/

GUSIFileRef::GUSIFileRef(const char * name, Boolean useAlias)
	: hasInfo(false), name(name), file(name, useAlias)
{
	if (!::IsDevice(name)) {
		spec 	= &file;
		error	=	file.Error();
	} else {
		spec 	= 	nil;
		error	=	noErr;
	}
}

const CInfoPBRec * GUSIFileRef::Info() const
{
	if (!hasInfo) {
		if (!spec) {
			((GUSIFileRef *) this)->error = paramErr;
			
			return nil;
		}
		if (((GUSIFileRef *) this)->error = file.CatInfo(((GUSIFileRef *) this)->info))
			return nil;
		
		((GUSIFileRef *) this)->hasInfo = true;
	} 
		
	return &info;	
}

/************************ class FileSocketDomain *************************/

// Only the domain management part is found here. For the file specific part,
// see GUSIFile.cp

FileSocketDomain *	FileSocketDomain::firstDeviceDomain = &FileSockets;
FileSocketDomain * 	FileSocketDomain::lastDeviceDomain  = nil;
FileSocketDomain *	FileSocketDomain::firstFileDomain   = &FileSockets;
FileSocketDomain * 	FileSocketDomain::lastFileDomain    = nil;

void Enqueue(
	FileSocketDomain * 							current, 
	FileSocketDomain * FileSocketDomain::*	next,
	FileSocketDomain ** 							first, 
	FileSocketDomain ** 							last)
{
	current->*next = &FileSockets;
	
	if (*last)
		(*last)->*next = current;
	else
		*first = current;
	
	*last = current;
}

void Dequeue(
	FileSocketDomain * 							current, 
	FileSocketDomain * FileSocketDomain::*	next,
	FileSocketDomain ** 							first, 
	FileSocketDomain ** 							last)
{
	FileSocketDomain * pred = nil;
	
	if (!*first)
		return;
		
	if (*first == current)
		*first = current->*next;
	else {
		for (pred = *first; pred->*next && pred->*next != current;)
			pred = pred->*next;
		if (pred->*next == current)
			pred->*next = current->*next;
		else
			return;
	}
	
	if (*last == current)
		*last = pred;
}

FileSocketDomain::FileSocketDomain(
	int 		domain, 
	Boolean 	doesDevices, 
	Boolean 	doesFiles)
	: SocketDomain(domain)
{
	if (doesDevices)
		Enqueue(
			this, 
			&FileSocketDomain::nextDeviceDomain, &firstDeviceDomain, &lastDeviceDomain);
	if (doesFiles)
		Enqueue(
			this, 
			&FileSocketDomain::nextFileDomain, &firstFileDomain, &lastFileDomain);
}

static Boolean HandleFileDispatch = true;

FileSocketDomain::~FileSocketDomain()
{
	if (this == &FileSockets)
		HandleFileDispatch = false;
		
	Dequeue(
			this, 
			&FileSocketDomain::nextDeviceDomain, &firstDeviceDomain, &lastDeviceDomain);
	Dequeue(
			this, 
			&FileSocketDomain::nextFileDomain, &firstFileDomain, &lastFileDomain);
}
	
FileSocketDomain * FileSocketDomain::FindDomain(
	const GUSIFileRef & ref, Request request)
{
	FileSocketDomain * 							dom;
	FileSocketDomain * FileSocketDomain::* next;
	
	// We are already decomposing
	
	if (!HandleFileDispatch)
		return nil;
		
	if (ref.IsDevice()) {
		dom 	= 	firstDeviceDomain;
		next	=	&FileSocketDomain::nextDeviceDomain;
	} else {
		dom 	= 	firstFileDomain;
		next	=	&FileSocketDomain::nextFileDomain;
	}
	
	// FileSockets will handle *everything*, therefore loop guaranteed to terminate
	
	while (dom && !dom->Yours(ref, request))
		dom = dom->*next;
	
	return dom;
}

// Quod licet Jovi non licet bovi

FileSocketDomain::FileSocketDomain()
	:	SocketDomain(AF_FILE)
{
	nextFileDomain = nextDeviceDomain = nil;
}

Boolean FileSocketDomain::Yours(const GUSIFileRef &, Request)
{
	// Don't worry, we'll handle it
	
	return true;
};
