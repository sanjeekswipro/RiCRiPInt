/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIMPWFile.cp	-	MPW compatible file sockets
Author	:	Matthias Neeracher <neeri@iis.ee.ethz.ch>
Language	:	MPW C

$Log: GUSIMPWFile.cp,v $
Revision 1.2  1999/04/23  08:44:13  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.2  1994/12/30  20:10:03  neeri
fRefNum is obtained lazily.
Add specialized fstat() member.

Revision 1.1  1994/08/10  00:23:40  neeri
Initial revision

*********************************************************************/

#include "GUSIFile_P.h"

#include <IOCtl.h>
#include <StdIO.h>

/************************ MPWFileSocket members ************************/

MPWFileSocket * MPWFileSocket::open(const char * name, int flags)
{
#if defined(powerc) || defined(__powerc)
	if (!file_open)
		return (MPWFileSocket *) GUSI_error_nil(ENOEXEC);
#endif

	int					fd		=	file_open(name, flags);
	MPWFileSocket *	sock;
	
	if (fd == -1)
		return (MPWFileSocket *) nil;
	else if (sock = new MPWFileSocket(fd))
		return sock;
	else
		return (MPWFileSocket *)GUSI_error_nil(ENOMEM);
}

MPWFileSocket::MPWFileSocket(int fd)
	: FileSocket(0), fd(fd)
{
}

int MPWFileSocket::read(void * buffer, int buflen)
{
	return file_read(fd, (char *) buffer, buflen);
}

int MPWFileSocket::write(void * buffer, int buflen)
{
	return file_write(fd, (char *) buffer, buflen);
}

int MPWFileSocket::fcntl(unsigned int cmd, int arg)
{
	return file_fcntl(fd, cmd, arg);
}

int MPWFileSocket::ioctl(unsigned int request, void *argp)
{
	return file_ioctl(fd, request, (long *) argp);
}

long MPWFileSocket::lseek(long offset, int whence)
{
	long	res;
	Ptr	buf;

	res = file_lseek(fd, offset, whence);
	
	if (res != -1)
		return res;
		
	if (whence != SEEK_SET) {
		res = file_lseek(fd, 0, whence);
		
		if (res == -1)
			return res;
			
		offset += res;
	}
	
	res = file_lseek(fd, 0, SEEK_END);
	
	buf = NewPtrClear(1024);
	
	while (offset >= res + 1024)
		if (file_write(fd, buf, 1024) == -1)
			return -1;
		else
			res += 1024;

	if (offset > res && file_write(fd, buf, unsigned(offset-res)) == -1)
		return -1;

	return offset;
}

int MPWFileSocket::ftruncate(long offset)
{
	if (lseek(offset, SEEK_SET) == -1)
		return -1;
	
	if (file_ioctl(fd, FIOSETEOF, (long *) offset) == -1)
		return -1;
	
	return 0;
}

int MPWFileSocket::fstat(struct stat * buf)
{
	short	fRef;
	
	if (fRefNum || file_ioctl(fd, FIOREFNUM, (long *) &fRef) != -1) {
		if (!fRefNum)
			SetFRefNum(fRef);

		return FileSocket::fstat(buf);
	} else {
		// Pseudofile
		
		buf->st_dev			=	0;
		buf->st_ino			=	0;
		buf->st_mode		=	S_IFCHR | 0666;
		buf->st_nlink		=	1;
		buf->st_uid			=	0;
		buf->st_gid			=	0;
		buf->st_rdev		=	0;
		buf->st_size		=	1;
		buf->st_atime		=	time(NULL);
		buf->st_mtime		=	time(NULL);
		buf->st_ctime		=	time(NULL);
		buf->st_blksize	=	1;
		buf->st_blocks		=	1;

		return 0;
	}
}

int MPWFileSocket::isatty()
{
	short	fRef;
	
	return file_ioctl(fd, FIOREFNUM, (long *) &fRef) == -1;
}

MPWFileSocket::~MPWFileSocket()
{
	file_close(fd);
}

extern FILE _iob[];
extern FILE * _lastbuf;

static int mode2flags(const char* mode, short * ioflags)
{
	int	flags = 0;
	int	rw;
	
	*ioflags = 0;
	
	rw = mode[1] == '+' || mode[2] == '+';
	if (mode[1] == 'b' || mode[2] == 'b') {
		flags 	|= O_BINARY;
		*ioflags	|= _IOBINARY;
	}
	if (mode[1] == 'p' || mode[2] == 'p'|| mode[3] == 'p')
		flags |= 0x1000;
		
	switch (mode[0]) {
	case 'r':
		flags 	|= rw ? O_RDWR : O_RDONLY;
		*ioflags	|= rw ? _IORW : _IOREAD;
		break;
	case 'w':
		flags 	|= (rw ? O_RDWR : O_WRONLY)|O_CREAT|O_TRUNC;
		*ioflags	|= rw ? _IORW : _IOWRT;
		break;
	case 'a':
		flags 	|= (rw ? O_RDWR : O_WRONLY)|O_CREAT|O_APPEND;
		*ioflags	|= rw ? _IORW : _IOWRT;
		break;
	default:
		return -1;
	}
	
	return flags;	
}

const int FILEBlockSize = 63;

struct FILEBlock {
	FILEBlock *	nextBlock;
	FILE 			files[FILEBlockSize];
};

FILEBlock * moreiob;
FILEBlock * lastiob = (FILEBlock *) &moreiob;

static FILE * findfile()
{
	FILE * 		stream;
	FILEBlock * iob;
	
	for (stream = _iob; stream < _lastbuf; ++stream)
		if (!(stream->_flag & (_IOREAD | _IOWRT | _IORW)))
			return stream;
	
	for (iob = moreiob; iob; iob = iob->nextBlock)
		for (stream = iob->files; stream < iob->files + FILEBlockSize; ++stream)
			if (!(stream->_flag & (_IOREAD | _IOWRT | _IORW)))
				return stream;
	
	if (!(iob = (FILEBlock *) NewPtr(sizeof(FILEBlock))))	{
		errno = ENOMEM;
		return NULL;
	}
	
	iob->nextBlock 		= nil;
	lastiob->nextBlock	= iob;
	lastiob					= iob;
	
	return iob->files;
}

FILE *fdreopen(int fd, short flags, FILE* stream)
{
	stream->_cnt 	= 0;
	stream->_ptr 	= NULL;
	stream->_base 	= NULL;
	stream->_end 	= NULL;
	stream->_size 	= NULL;
	stream->_flag 	= flags;
	stream->_file 	= fd;
	
	return stream;
}

FILE *fopen(const char *filename, const char *mode)
{
	FILE *	stream;
	int	 	flags;
	short		ioflags;
	int 		fd;
	
	if (	(	stream 	= findfile()) 
		&& (	flags	= mode2flags(mode, &ioflags)) >= 0 
		&& (	fd 	= open(filename, flags)) >= 0
	) 
		return fdreopen(fd, ioflags, stream);
	else
		return NULL;
}
	
FILE *freopen(const char *filename, const char *mode, FILE *stream)
{
	int	 	flags;
	short		ioflags;
	int 		fd;
	
	flags = errno;
	fclose(stream);
	errno = flags;
	
	if (	(	flags	= mode2flags(mode, &ioflags)) >= 0 
		&& (	fd 	= open(filename, flags)) >= 0
	) 
		return fdreopen(fd, ioflags, stream);
	else
		return NULL;
}

FILE *fdopen(int fd, const char *mode)
{
	FILE *	stream;
	int	 	flags;
	short		ioflags;

	if (	(	stream 	= findfile()) 
		&& (	flags	= mode2flags(mode, &ioflags)) >= 0  
	)
		return fdreopen(fd, ioflags, stream);
	else
		return NULL;
}

int fwalk(int (*func)(FILE * stream))
{
	FILE * 		stream;
	FILEBlock * iob;
	int			res = 0;
	
	for (stream = _iob; stream < _lastbuf; ++stream)
		if (stream->_flag & (_IOREAD | _IOWRT | _IORW))
			res |= func(stream);
	
	for (iob = moreiob; iob; iob = iob->nextBlock)
		for (stream = iob->files; stream < iob->files + FILEBlockSize; ++stream)
			if (stream->_flag & (_IOREAD | _IOWRT | _IORW))
				res |= func(stream);
	
	return res;
}

int fclose(FILE * stream)
{
	if (!stream)
		return -1;
		
	if (stream->_flag & (_IOREAD | _IOWRT | _IORW)) {
		int err = (stream->_flag & _IONBF) ? 0 : fflush(stream);
		
		if (close(stream->_file) < 0) {
			err = -1;
			errno = ENOENT;
		}
	
		if (stream->_flag & _IOMYBUF) 
			DisposePtr(Ptr(stream->_base));
		
		stream->_base = nil;
		stream->_ptr = nil;
		stream->_flag = 0;
		stream->_cnt = 0;
	
		return err;
	}
	
	return 0;
}
