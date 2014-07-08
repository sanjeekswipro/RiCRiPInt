/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIBuffer.cp	-	Circular buffers and Scatter/Gather
Author	:	Matthias Neeracher
Language	:	MPW C/C++

$Log: GUSIBuffer.cp,v $
Revision 1.2  1999/04/23  08:44:09  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.2  1994/12/30  19:40:32  neeri
fix a bug which was sabotaging all vectored routines.

Revision 1.1  1994/02/25  02:28:25  neeri
Initial revision

Revision 0.4  1993/07/17  00:00:00  neeri
proc -> defproc

Revision 0.3  1993/02/10  00:00:00  neeri
The above was incorrect, next attempt

Revision 0.2  1992/08/23  00:00:00  neeri
Optimize buffer size for empty buffers

Revision 0.1  1992/08/02  00:00:00  neeri
Deferred procedures

*********************************************************************/

#ifndef GUSI_BUFFER_DEBUG
#define NDEBUG
#endif

#include "GUSI_P.h"

/**************************** RingBuffer ****************************/

#ifndef NDEBUG
int RingDist(Ptr start, Ptr end, Ptr from, Ptr to)
{
	if (from > to)
		return RingDist(start, end, from, end)+RingDist(start, end, start, to);
	else
		return to-from;
}

#define RINGEQ(from, to, val)	\
	(RingDist(buffer, endbuf, (from), (to)) == ((val)%(endbuf-buffer)))
#define INVAR									\
	(	(free >= 0) 							\
	&& (valid >= 0) 							\
	&&	RINGEQ(consume, produce, valid+spare) \
	&&	RINGEQ(produce, consume, free))
#endif

RingBuffer::RingBuffer(u_short bufsiz)
{
	buffer	=	NewPtr(bufsiz);
	endbuf	=	buffer+bufsiz;
	consume	=	buffer;
	produce	=	buffer;
	free		=	bufsiz;
	valid		=	0;
	spare		=	0;
	lock		=	false;
	defproc	=	nil;
	
	assert(INVAR);
	assert(endbuf - buffer == bufsiz);
}

RingBuffer::~RingBuffer()
{
	Defer();
	if (buffer)
		DisposPtr(buffer);
}

Ptr RingBuffer::Producer(long & len)
{
	Defer();
	
#ifndef NDEBUG
	long	oldlen	=	len;
#endif

	if (!valid) {
		produce = buffer;
		consume = buffer;
		spare   = 0;
	}
	
	u_short	streak	=	endbuf - produce;
	Ptr		res		=	produce;
	
	if (streak >= free)
		streak = free;
	else if (streak < (free >> 1) && streak < len) {
		spare 	=  streak;
		produce	=	buffer;
		free		-=	spare;
		streak	=	free;
		res 		=	produce;
	}
	
	if (len > streak)
		len	=	streak;
	
	assert(INVAR);
	assert(len <= oldlen);
	
	Undefer();
	
	return res;
}

Ptr RingBuffer::Consumer(long & len)
{
	Defer();
	
#ifndef NDEBUG
	long	oldlen	=	len;
#endif

	u_short	streak	=	endbuf - consume - spare;
	Ptr		res		=	consume;
	
	if (streak > valid)
		streak = valid;
	if (len > streak)
		len	=	streak;
	
	assert(INVAR);
	assert(len <= oldlen);
	
	Undefer();
	
	return res;
}

void RingBuffer::Validate(long len)
{
	Defer();
	
	valid 	+= (unsigned short) len;
	free		-=	(unsigned short) len;
	produce	+=	len;
	
	if (produce == endbuf)
		produce	=	buffer;
	
	assert(INVAR);
	
	Undefer();
}

void RingBuffer::Invalidate(long len)
{
	Defer();
	
	free 		+= (unsigned short) len;
	valid		-=	(unsigned short) len;
	consume	+=	len;
	
	if (consume == endbuf-spare) {
		consume	=	buffer;
		spare		=	0;
	} else if (!valid && free == len)	{		// Maximize streak for empty buffer
		consume	=	buffer;
		produce	=	buffer;
	}
	
	assert(INVAR);
	
	Undefer();
}

void RingBuffer::Consume(Ptr to, long & len)
{
#ifndef NDEBUG
	Ptr	oldto		=	to;
	long	oldlen	=	len;
#endif

	long	part;
	long	rest;
	Ptr	buf;
	
	for (rest = len; (part = rest) && valid; rest -= part)	{
		buf	=	Consumer(part);
		BlockMove(buf, to, part);
		Invalidate(part);
		to		+= part;
	}
	
	len	-=	rest;
	
	assert(INVAR);
	assert(len <= oldlen);
	assert(to-oldto == len);
}

void RingBuffer::Produce(Ptr from, long & len)
{
#ifndef NDEBUG
	Ptr	oldfrom	=	from;
	long	oldlen	=	len;
#endif

	long	part;
	long	rest;
	Ptr	buf;
	
	for (rest = len; (part = rest) && free; rest -= part)	{
		buf	=	Producer(part);
		BlockMove(from, buf, part);
		Validate(part);
		
		from	+= part;
	}
	
	len	-=	rest;
	
	assert(INVAR);
	assert(len <= oldlen);
	assert(from-oldfrom == len);
}

/************************** Scatter/Gather **************************/

ScattGath::ScattGath(const struct iovec *iov, int cnt)	{
	io		=	iov;
	count	=	cnt;

	if (count < 1)	{
		buf		=	nil;
		len		=	0;
		scratch	=	nil;
	} else if (count == 1)	{	
		buf		=	(void *) iov->iov_base;
		len		=	(int)	iov->iov_len;
		scratch	=	nil;
	} else {
		for (len = 0; cnt--; ++iov)
			len += (int) iov->iov_len;
		
		scratch = NewHandle(len);
		
		if (scratch)	{
			HLock(scratch);
			buf	=	(void *) *scratch;
		} else
			buf 	=	nil;
	}
}

ScattGath::~ScattGath()
{
	if (scratch)
		DisposHandle(scratch);
}

Scatterer::Scatterer(const struct iovec *iov, int count) 
	: ScattGath(iov, count)
{
}

Scatterer::~Scatterer()
{
	int	sect;
	
	if (count > 1 && buf)
		for (char * bptr = (char *) buf; count-- && len; ++io)	{
			sect	=	min(len, io->iov_len);
			
			memcpy(io->iov_base, bptr, sect);
			
			bptr	+=	sect;
			len 	-=	sect;
		}
}

Gatherer::Gatherer(const struct iovec *iov, int count) 
	: ScattGath(iov, count)
{
	if (count > 1 && buf)
		for (char * bptr = (char *) buf; count--; ++iov)	{
			memcpy(bptr, iov->iov_base, iov->iov_len);
			
			bptr	+=	iov->iov_len;
		}
}

Gatherer::~Gatherer()
{
}
