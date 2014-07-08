/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIFile_P.h	-	Common Definitions for File Sockets
Author	:	Matthias Neeracher
Language	:	MPW C/C++

$Log: include:GUSIFile_P.h,v $
Revision 1.2  1999/04/23  08:56:45  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.2  1994/12/31  02:04:05  neeri
Reorganize file name dispatching.

Revision 1.1  1994/08/10  00:40:37  neeri
Initial revision

*********************************************************************/

#include "GUSI_P.h"

extern int File_error(OSErr err);
extern Boolean IsDevice(const char * fn);

class FileSocket : public Socket {
	friend class FileSocketDomain;
protected:
	short		fRefNum;
					FileSocket(short fRefNum)	:	fRefNum(fRefNum)	{}
	
	void SetFRefNum(short fRefNum) { this->fRefNum = fRefNum; }
public:
	virtual int	fcntl(unsigned int cmd, int arg);
	virtual int	ioctl(unsigned int request, void *argp);
	virtual int	fstat(struct stat * buf);
	virtual int select(Boolean * canRead, Boolean * canWrite, Boolean * exception);
	virtual 		~FileSocket();
};

class MPWFileSocket : public FileSocket {
	friend class FileSocketDomain;
protected:
	int			fd;
					MPWFileSocket(int fd);
					
	static MPWFileSocket	*	open(const char * name, int flags);
public:
	virtual int	read(void * buffer, int buflen);
	virtual int write(void * buffer, int buflen);
	virtual int	fcntl(unsigned int cmd, int arg);
	virtual int	ioctl(unsigned int request, void *argp);
	virtual long lseek(long offset, int whence);
	virtual int ftruncate(long offset);
	virtual int	fstat(struct stat * buf);
	virtual int	isatty();
	virtual 		~MPWFileSocket();
};


// A GUSIFileRef is an immutable structure, consisting of a 
// pointer to a path name and (unless the name is a device
// name, a pointer to a TFileSpec. 

class GUSIFileRef {
	OSErr				error;
	Boolean			hasInfo;
	TFileSpec		file;
	CInfoPBRec		info;
public:
	const char *	name;
	TFileSpec *		spec;
	
	GUSIFileRef(const char * name, Boolean useAlias = false);
	
	Boolean 					IsDevice() 	const { 	return !spec; 	}
	OSErr						Error()		const { 	return error; 	}
	const CInfoPBRec *	Info()		const;	// conceptually const
};

// FileSocketDomain is the spiritual leader for all domains that deal
// with file names. Descendants claim their interest in device names 
// and normal names on creation. 
//    When their Yours() routine is called, they have to give a definite
// answer on whether they'll take responsibility for a given file/request
// combination. They have the right to cache information in static areas,
// as no second call to Yours() will be made before the actual routine is
// called.

class FileSocketDomain : public SocketDomain {
	friend SocketTable::SocketTable();
	friend int SocketTable::Install(Socket * sock, int from, int start);

	virtual Socket * stdopen(int fd);

	FileSocketDomain *	nextDeviceDomain;
	FileSocketDomain * 	nextFileDomain;
protected:
	static FileSocketDomain *	firstDeviceDomain;
	static FileSocketDomain * 	lastDeviceDomain;
	static FileSocketDomain *	firstFileDomain;
	static FileSocketDomain * 	lastFileDomain;

	FileSocketDomain(
		int 		domain, 
		Boolean 	doesDevices, 
		Boolean 	doesFiles);
public:
	FileSocketDomain();
	virtual ~FileSocketDomain();
	
	enum Request {
		willOpen,
		willRemove,
		willRename,
		willGetFileInfo,
		willSetFileInfo,
		willFAccess,
		willStat,
		willChmod,
		willUTime
	};
	
	static FileSocketDomain * FindDomain(const GUSIFileRef & ref, Request request);
		
	virtual Boolean Yours(const GUSIFileRef & ref, Request request);
	
	virtual Socket * open(const GUSIFileRef & ref, int oflag);
	virtual int remove(const GUSIFileRef & ref);
	virtual int rename(const GUSIFileRef & ref, const char *newname);
	virtual void 
		fgetfileinfo(
			const GUSIFileRef & ref, 
			unsigned long * creator, unsigned long * type);
	virtual void 
		fsetfileinfo(
			const GUSIFileRef & ref, 
			unsigned long creator, unsigned long type);
	virtual int 
		faccess(const GUSIFileRef & ref, unsigned int cmd, long* arg);
	virtual int stat(const GUSIFileRef & ref, struct stat * buf);
	virtual int chmod(const GUSIFileRef & ref, mode_t mode);
	virtual int utime(const GUSIFileRef & ref, const struct utimbuf * times);

	virtual int choose(
						int 		type, 
						char * 	prompt, 
						void * 	constraint,		
						int 		flags,
 						void * 	name, 
						int * 	namelen);
};

extern FileSocketDomain 	FileSockets;

extern "C" int fwalk(int (*func)(FILE * stream));

/*********************** Prototypes for MPW routines ***********************/

#if !defined(powerc) && !defined(__powerc)
extern "C" {
int file_open(const char * name, int flags);
int file_close(int s);
int file_read(int s, char *buffer, unsigned buflen);
int file_write(int s, char *buffer, unsigned buflen);
int file_fcntl(int s, unsigned int cmd, int arg);
int file_ioctl(int d, unsigned int request, long *argp);  /* argp is really a caddr_t */
long file_lseek(int fd, long offset, int whence);
int file_faccess(char *fileName, unsigned int cmd, long * arg);
}
#else
int (*file_open)(const char * name, int flags);
int (*file_close)(int s);
int (*file_read)(int s, char *buffer, unsigned buflen);
int (*file_write)(int s, char *buffer, unsigned buflen);
int (*file_fcntl)(int s, unsigned int cmd, int arg);
int (*file_ioctl)(int d, unsigned int request, long *argp);  /* argp is really a caddr_t */
long (*file_lseek)(int fd, long offset, int whence);
int (*file_faccess)(char *fileName, unsigned int cmd, long * arg);
#endif
