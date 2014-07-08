/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSISocket.cp	-	Default implementations
Author	:	Matthias Neeracher
Language	:	MPW C/C++

$Log: GUSISocket.cp,v $
Revision 1.2  1999/04/23  08:43:43  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.2  1994/12/30  20:17:44  neeri
Remove built in INETd support.

Revision 1.1  1994/02/25  02:30:35  neeri
Initial revision

Revision 0.5  1993/06/27  00:00:00  neeri
Socket::ftruncate, Socket::{pre,post}_select

Revision 0.4  1993/02/09  00:00:00  neeri
Initialize lurking related fields.

Revision 0.3  1992/09/15  00:00:00  neeri
Sockets are regular files for fstat()

Revision 0.2  1992/05/21  00:00:00  neeri
Implement select()

Revision 0.1  1992/04/27  00:00:00  neeri
getsockopt()

*********************************************************************/

#include "GUSI_P.h"

#include <Time.h>

// While most of the calls can't be substituted, some of them can
// and a few others have reasonable defaults

Socket::Socket()
{
	refCount	=	0;
}

Socket::~Socket()
{
}

int Socket::bind(void *, int)
{
	return GUSI_error(EOPNOTSUPP);
}

int Socket::connect(void *, int)
{
	return GUSI_error(EOPNOTSUPP);
}

int Socket::listen(int)
{
	return GUSI_error(EOPNOTSUPP);
}

Socket * Socket::accept(void *, int *)
{
	return (Socket *) GUSI_error_nil(EOPNOTSUPP);
}

int Socket::read(void * buffer, int buflen)
{
	int	fromlen = 0;
	
	return recvfrom(buffer, buflen, 0, nil, &fromlen);
}

int Socket::write(void * buffer, int buflen)
{
	return sendto(buffer, buflen, 0, nil, 0);
}

int Socket::recvfrom(void *, int, int, void *, int *)
{
	return GUSI_error(EOPNOTSUPP);
}

int Socket::sendto(void *, int, int, void *, int)
{
	return GUSI_error(EOPNOTSUPP);
}

int Socket::getsockname(void *, int *)
{
	return GUSI_error(EOPNOTSUPP);
}

int Socket::getpeername(void *, int *)
{
	return GUSI_error(EOPNOTSUPP);
}

int Socket::getsockopt(int, int, void *, int *)
{
	return GUSI_error(EOPNOTSUPP);
}

int Socket::setsockopt(int, int, void *, int)
{
	return GUSI_error(EOPNOTSUPP);
}

int Socket::fcntl(unsigned int, int)
{
	return GUSI_error(EOPNOTSUPP);
}

int Socket::ioctl(unsigned int, void *)
{
	return GUSI_error(EOPNOTSUPP);
}

int Socket::fstat(struct stat * buf)
{
	buf->st_dev			=	0;
	buf->st_ino			=	0;
	buf->st_mode		=	S_IFSOCK | 0666 ;
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

long Socket::lseek(long, int)
{
	return GUSI_error(ESPIPE);
}

int Socket::ftruncate(long)
{
	return GUSI_error(EINVAL);
}

int Socket::isatty()
{
	return 0;
}

int Socket::shutdown(int)
{
	return GUSI_error(EOPNOTSUPP);
}

void Socket::pre_select(Boolean, Boolean, Boolean)
{
}

int Socket::select(Boolean *, Boolean *, Boolean *)
{
	return 0;
}

void Socket::post_select(Boolean, Boolean, Boolean)
{
}
