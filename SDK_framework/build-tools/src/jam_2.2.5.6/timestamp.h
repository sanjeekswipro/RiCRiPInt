/*
 * Copyright 1993, 1995 Christopher Seiwald.
 *
 * This file is part of Jam - see jam.c for Copyright information.
 */

# ifndef _TIMESTAMP_H
# define _TIMESTAMP_H

/*
 * timestamp.h - get the timestamp of a file or archive member
 */

# ifdef USE_WIN32_API
# define timestamp_t LARGE_INTEGER
# define TIME_NONZERO(X) ((X).LowPart || (X).HighPart)
# define TIME_ZERO time_zero
extern const timestamp_t time_zero;
# define TIME_NEWER(X,Y) ((X).HighPart > (Y).HighPart \
	|| ((X).HighPart == (Y).HighPart && (X).LowPart > (Y).LowPart))
# else
# define timestamp_t time_t
# define TIME_NONZERO(X) (X)
# define TIME_ZERO 0
# define TIME_NEWER(X,Y) ((X) > (Y))
# endif
# define TIME_NEWEST(X,Y) (TIME_NEWER(X,Y)?(X):(Y))

void timestamp(char *, timestamp_t *);
void donestamps();

# endif
