/*********************************************************************
File		:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIFileTest	-	Test plain files
Author	:	Matthias Neeracher <neeri@iis.ethz.ch>
Language	:	MPW C

$Log: Examples:GUSIFileTest.c,v $
Revision 1.2  1999/04/23  08:56:37  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.2  1994/12/31  01:10:41  neeri
ANSIfy.
Test FSSpec encoding.

Revision 1.1  1994/02/25  02:46:54  neeri
Initial revision

Revision 0.9  1993/07/29  00:00:00  neeri
scandir

Revision 0.8  1993/07/18  00:00:00  neeri
dirent -> struct dirent

Revision 0.7  1992/12/20  00:00:00  neeri
Allow defaults for choose()

Revision 0.6  1992/12/08  00:00:00  neeri
Pwd()

Revision 0.5  1992/10/27  00:00:00  neeri
Forgot to adapt it to dirent.h

Revision 0.4  1992/09/07  00:00:00  neeri
RdLink()

Revision 0.3  1992/07/25  00:00:00  neeri
Isolated testing gear in GUSITest

Revision 0.2  1992/07/13  00:00:00  neeri
Test choose()

Revision 0.1  1992/06/14  00:00:00  neeri
More tests

*********************************************************************/

#include "GUSI.h"
#include "GUSITest.h"
#include "TFileSpec.h"
#include <Types.h>
#include <dirent.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <stdlib.h>

void Stat(char ch1, char ch2, const char * cmd)
{
	int 			res;
	struct stat	statbuf;
	char			filename[80];

	if (sscanf(cmd, "%s", filename) != 1)
		Usage(ch1, ch2);
	else {
		if (ch2 == 'l') {
			cmd	=	"lstat";
			res 	=	lstat(filename, &statbuf);
		} else {
			cmd 	= 	"stat";
			res 	=	stat(filename, &statbuf);
		}
	
		if (res)	{
			printf("# %s(\"%s\") returned error %s\n", cmd, filename, Explain());
		} else {
			printf("# %s(\"%s\") =\n", cmd, filename);
			DUMP(statbuf.st_dev,d);
			DUMP(statbuf.st_ino,d);
			DUMP(statbuf.st_mode,o);
			DUMP(statbuf.st_nlink,d);
			DUMP(statbuf.st_uid,d);
			DUMP(statbuf.st_gid,d);
			DUMP(statbuf.st_rdev,d);
			DUMP(statbuf.st_size,d);
			DUMP(statbuf.st_atime,u);
			DUMP(statbuf.st_mtime,u);
			DUMP(statbuf.st_ctime,u);
			DUMP(statbuf.st_blksize,d);
			DUMP(statbuf.st_blocks,d);
		}	
		Where();
	}
}

void ChDir(char ch1, char ch2, const char * cmd)
{
	char			directory[80];

	if (sscanf(cmd, "%s", directory) != 1)
		Usage(ch1, ch2);
	else if (chdir(directory))	{
		printf("# chdir(\"%s\") returned error %s\n", directory, Explain());
		Where();
	}
}

void MkDir(char ch1, char ch2, const char * cmd)
{
	char			directory[80];

	if (sscanf(cmd, "%s", directory) != 1)
		Usage(ch1, ch2);
	else if (mkdir(directory))	{
		printf("# mkdir(\"%s\") returned error %s\n", directory, Explain());
		Where();
	}
}

void RmDir(char ch1, char ch2, const char * cmd)
{
	char			directory[80];

	if (sscanf(cmd, "%s", directory) != 1)
		Usage(ch1, ch2);
	else if (rmdir(directory))	{
		printf("# rmdir(\"%s\") returned error %s\n", directory, Explain());
		Where();
	}
}

void List(char ch1, char ch2, const char * cmd)
{
	int					count;
	int					i;
	struct dirent **	entries;
	char *				dirend;
	char					directory[80];
	struct stat			statbuf;

	if (sscanf(cmd, "%s", directory) != 1)
		strcpy(directory, ":");
	
	if ((count = scandir(directory, &entries, nil, nil)) < 0) {
		printf("# scandir(\"%s\") returned error %s\n", directory, Explain());
		goto error;
	}
	
	printf("# directory \"%s\" =\n", directory);
	
	dirend = directory + strlen(directory);
	if (dirend[-1] != ':')
		*dirend++ = ':';
	
	for (i = 0; i < count; ++i)
		if (ch2 == 's')
			printf("#    %s\n", entries[i]->d_name);
		else {
			strcpy(dirend, entries[i]->d_name);
			
			if (lstat(directory, &statbuf)) 
				printf("# lstat(\"%s\") returned error %s\n", entries[i]->d_name, Explain());
			else
				printf("#    %c %7d %s\n", 
					(statbuf.st_mode & S_IFMT) == S_IFREG ? 'F' :
					(statbuf.st_mode & S_IFMT) == S_IFDIR ? 'D' :
					(statbuf.st_mode & S_IFMT) == S_IFLNK ? 'L' : '?',
					statbuf.st_size,
					entries[i]->d_name);
		}
		
error:
	Where();
}

void Type(char ch1, char ch2, const char * cmd)
{
	FILE * 		fl;
	char			line[500];
	char			filename[80];

	if (sscanf(cmd, "%s", filename) != 1)
		Usage(ch1, ch2);
	else {
		fl = fopen(filename, "r");
		
		if (!fl)
			printf("# open(\"%s\") returned error %s\n", filename, Explain());
		else {
			printf("# \"%s\" =\n", filename);
			while (fgets(line, 500, fl))
				fputs(line, stdout);
		}
		
		fclose(fl);
		
		Where();
	}
}

void Encode(char ch1, char ch2, const char * cmd)
{
	OSErr			err;
	char			line[500];
	char			filename[80];

	if (sscanf(cmd, "%s", filename) != 1)
		Usage(ch1, ch2);
	else {
		FSSpec spec;
		
		if (err = Path2FSSpec(filename, &spec))
			fprintf(stderr, "Path2FSSpec(%s) returned error %d\n", filename, err);
		else
			fprintf(stderr, "%s -> %s\n", filename, FSp2Encoding(&spec));
	}
}

void Edit(char ch1, char ch2, const char * cmd)
{
	FILE * 		fl;
	char			line[500];
	char			filename[80];

	if (sscanf(cmd, "%s", filename) != 1)
		Usage(ch1, ch2);
	else {
		fl = fopen(filename, "w");
		
		if (!fl)
			printf("# open(\"%s\") returned error %s\n", filename, Explain());
		else	{
			printf("# Enter \"%s\", terminate with \".\"\n", filename);
			while (fgets(line, 500, stdin))
				if (strcmp(line, ".\n"))
					fputs(line, fl);
				else 
					break;
		
			fclose(fl);
		}
	}
}

void Rm(char ch1, char ch2, const char * cmd)
{
	char			filename[80];

	if (sscanf(cmd, "%s", filename) != 1)
		Usage(ch1, ch2);
	else if (remove(filename))	{
		printf("# remove(\"%s\") returned error %s\n", filename, Explain());
		Where();
	}
}

void Mv(char ch1, char ch2, const char * cmd)
{
	struct stat	statbuf;
	char			oldfilename[80];
	char			newfilename[80];

	if (sscanf(cmd, "%s %s", oldfilename, newfilename) != 2)
		Usage(ch1, ch2);
	else {
		if (!stat(newfilename, &statbuf) && (statbuf.st_mode & S_IFMT) == S_IFDIR) {
			char *	fn;
			char * 	next;
			int 		len 	= 	strlen(newfilename);
			
			/* Extract file name part from oldfilename */
			for (fn = oldfilename; (next = strchr(fn, ':')) && next[1]; fn = next);
			
			if (newfilename[len-1] != ':')
				newfilename[len++-1] = ':';
			
			strcpy(newfilename+len, fn);
		}
		
		if (rename(oldfilename, newfilename))	{
			printf("# rename(\"%s\", \"%s\") returned error %s\n", oldfilename, newfilename, Explain());
			Where();
		}
	}
}

void Link(char ch1, char ch2, const char * cmd)
{
	char			oldfilename[80];
	char			newfilename[80];

	if (sscanf(cmd, "%s %s", oldfilename, newfilename) != 2)
		Usage(ch1, ch2);
	else {		
		if (symlink(oldfilename, newfilename))	{
			printf("# symlink(\"%s\", \"%s\") returned error %s\n", oldfilename, newfilename, Explain());
			Where();
		}
	}
}

void RdLink(char ch1, char ch2, const char * cmd)
{
	char path[200];
	char link[200];
	int  len;

	if (sscanf(cmd, "%s", path) != 1)
		Usage(ch1, ch2);
	
	len = readlink(path, link, 199);
	
	if (len < 0)
		printf("# readlink(\"%s\") returned error %s\n", path, Explain());
	else {
		link[len] = 0;
		printf("# readlink(\"%s\") returned \"%s\"\n", path, link);
	}
		
	Where();
}

void Pwd(char ch1, char ch2, const char * cmd)
{
	char * buf;
	
	buf = getcwd(NULL, 1024);
	
	if (!buf)
		printf("# getcwd() returned error %s\n", Explain());
	else {
		printf("# getcwd() returned \"%s\"\n", buf);
		
		free(buf);
	}
		
	Where();
}

void Choose(char ch1, char ch2, const char * cmd)
{
	int				flags;
	int				len	=	250;
	char				fType[5];
	char				name[250];
	sa_constr_file	constr;
	
	flags = ((ch1 == 'g') ? 0 : CHOOSE_NEW) | ((ch2 == 'f') ? 0 : CHOOSE_DIR);

	if (flags) {
		if (sscanf(cmd, "%s", name) == 1)
			flags |= CHOOSE_DEFAULT;
	} else if (sscanf(cmd, "%s", fType) == 1) {
		constr.numTypes	=	1;
		constr.types[0]	=	*(OSType *) fType;
	} else
		constr.numTypes	=	-1;
	
	if (choose(AF_FILE, 0, "What's up ?", &constr, flags, name, &len))
		printf("# choose(%d) returned error %s\n", flags, Explain());
	else
		printf("# choose(%d) returned \"%s\"\n", flags, name);
		
	Where();
}

main(int argc, char ** argv)
{
	printf("GUSIFileTest		MN 25Jul92\n\n");

	COMMAND('s', 't', Stat,  "filename", 			"Call stat() on a file");
	COMMAND('s', 'l', Stat,  "filename", 			"Call lstat() on a file");
	COMMAND('c', 'd', ChDir, "directory", 			"Call chdir()");
	COMMAND('l', 's', List,  "[ directory ]", 	"List a directory");
	COMMAND('l', 'l', List,  "[ directory ]", 	"List a directory with more info");
	COMMAND('m', 'd', MkDir, "directory",			"Make a new directory");
	COMMAND('r', 'd', RmDir, "directory",			"Delete an empty directory");
	COMMAND('t', 'y', Type,  "filename",			"Type out the contents of a file");
	COMMAND('e', 'd', Edit,  "filename",			"Enter the contents of a new file");
	COMMAND('m', 'v', Mv,  	 "oldfile newfile",	"Rename/Move a file");
	COMMAND('r', 'm', Rm, 	 "filename",			"Delete a file");
	COMMAND('r', 'l', RdLink,"filename",			"Read symbolic link");
	COMMAND('l', 'n', Link,  "oldfile newfile",	"Create a symbolic link");
	COMMAND('g', 'f', Choose,"[ type ]",			"Let the user choose a file");
	COMMAND('g', 'd', Choose,"[ default ]",		"Let the user choose a directory");
	COMMAND('p', 'f', Choose,"[ default ]",		"Let the user create a file");
	COMMAND('p', 'd', Choose,"[ default ]",		"Let the user create a directory");
	COMMAND('p', 'w', Pwd,	 "",						"Print current directory");
	COMMAND('e', 'n', Encode,"filename",			"Translate filename to encoded FSSpec");
	
	RunTest(argc, argv);
}