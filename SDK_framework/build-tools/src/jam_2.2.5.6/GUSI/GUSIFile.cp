/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIFile.cp		-	Implementation of file calls
Author	:	Matthias Neeracher <neeri@iis.ethz.ch>
Language	:	MPW C

$Log: GUSIFile.cp,v $
Revision 1.2  1999/04/23  08:44:02  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.4  1994/12/30  19:56:13  neeri
New dispatching mechanism for file names.
Add chmod().

Revision 1.3  1994/08/10  00:29:25  neeri
Sanitized for universal headers.

Revision 1.2  1994/05/01  23:37:55  neeri
Added utime().
Fixed stat() permissions for locked files.

Revision 1.1  1994/03/08  23:51:24  neeri
Initial revision

Revision 0.34  1993/09/11  00:00:00  neeri
Trying to avoid stream.h unless absolutely necessary	

Revision 0.33  1993/08/25  00:00:00  neeri
Need 2 include TextUtils since E.T.O. #12

Revision 0.32  1993/07/17  00:00:00  neeri
Adapt to BSD 4.3, scandir

Revision 0.31  1993/06/27  00:00:00  neeri
f?truncate

Revision 0.30  1993/02/23  00:00:00  neeri
fgetfileinfo

Revision 0.29  1993/02/01  00:00:00  neeri
Made nlink for directories return something meaningful

Revision 0.28  1993/01/15  00:00:00  neeri
rename() has to be more careful about renaming order

Revision 0.27  1993/01/15  00:00:00  neeri
choose() should *not* count the string terminator.

Revision 0.26  1993/01/03  00:00:00  neeri
Respect configuration

Revision 0.25  1993/01/01  00:00:00  neeri
fsetfileinfo

Revision 0.24  1992/12/20  00:00:00  neeri
do_putfile now respects default

Revision 0.23  1992/12/13  00:00:00  neeri
stat now returns DirID/FileNo, not parID for the ino field

Revision 0.22  1992/12/08  00:00:00  neeri
getcwd()

Revision 0.21  1992/11/28  00:00:00  neeri
TEXT files are now considered executable

Revision 0.20  1992/11/15  00:00:00  neeri
Rename GUSIFSp_P.h to TFileSpec.h (there we go again)

Revision 0.19  1992/10/28  00:00:00  neeri
Forgot to change to dirent

Revision 0.18  1992/09/15  00:00:00  neeri
Slight error in do_stat()

Revision 0.17  1992/09/12  00:00:00  neeri
Rename Paths.h to GUSIFSp_P.h

Revision 0.16  1992/09/08  00:00:00  neeri
readlink()

Revision 0.15  1992/09/06  00:00:00  neeri
Adapt alias resolution to new libraries

Revision 0.14  1992/07/13  00:00:00  neeri
CopyIconFamily

Revision 0.13  1992/06/27  00:00:00  neeri
choose()

Revision 0.12  1992/06/26  00:00:00  neeri
symlink is starting to work

Revision 0.11  1992/06/21  00:00:00  neeri
symlink()

Revision 0.10  1992/06/15  00:00:00  neeri
Separated path stuff

Revision 0.9  1992/05/21  00:00:00  neeri
Implemented select()

Revision 0.8  1992/04/20  00:00:00  neeri
C++ rewrite

Revision 0.7  1992/04/05  00:00:00  neeri
lseek

Revision 0.6  1992/03/22  00:00:00  neeri
Adapted for GUSI, change chdir stuff

Revision 0.5  1992/02/11  00:00:00  neeri
Incorporated bug fix by John Reekie

Revision 0.4  1991/12/12  00:00:00  neeri
FSp2RelPath

Revision 0.3  1991/12/09  00:00:00  neeri
Radical overhaul

Revision 0.2  1991/05/28  00:00:00  neeri
isatty()

Revision 0.1  1991/05/28  00:00:00  neeri
Created

*********************************************************************/

#include "GUSIFile_P.h"
#include "TFileSpec.h"

#include <Errors.h>
#include <Resources.h>
#include <Script.h>
#include <Finder.h>
#include <Folders.h>
#include <Devices.h>
#include <Memory.h>
#include <Aliases.h>
#include <string.h>
#include <PLStringFuncs.h>
#include <ioctl.h>
#include <fcntl.h>
#include <errno.h>
#ifdef GUSI_FILE_DEBUG
#include <stream.h>
#endif
#include <StdLib.h>
#include <Time.h>
#include <TextUtils.h>
#include <unistd.h>
#include <utime.h>

extern int StandAlone;

FileSocketDomain			FileSockets;	

/********************* FileSocketDomain members *********************/
	
Socket * FileSocketDomain::stdopen(int fd)
{
#if defined(powerc) || defined(__powerc)
	if (!file_open)
		return (Socket *) GUSI_error_nil(ENOEXEC);
	else
#endif
		return new MPWFileSocket(fd);
}

Boolean IsDevice(const char * fn)
{
	return 	(	
		(fn[0] | 0x20) == 'd'
	&& (fn[1] | 0x20) == 'e'
	&& (fn[2] | 0x20) == 'v'
	&& fn[3] == ':');
}

int File_error(OSErr err)
{
	switch (err) {
	case noErr:
		errno = 0;
		
		return 0;
	case bdNamErr:
		return GUSI_error(ENOTDIR);
	case fnfErr:
	case dirNFErr:
		return GUSI_error(ENOENT);
	case dupFNErr:
		return GUSI_error(EEXIST);
	case dirFulErr:
		return GUSI_error(ENOSPC);
	case fBsyErr:
		return GUSI_error(EIO);
	default:
		return GUSI_error(EINVAL);
	}
}

Socket * FileSocketDomain::open(const GUSIFileRef & ref, int oflag)
{
	Socket *	sock;
	
	if (ref.IsDevice())
		return MPWFileSocket::open(ref.name, oflag);
	else {
		Boolean			fresh;
		
		if (ref.Error())	{
			File_error(ref.Error());
			
			return nil;
		}
		
		fresh	= (oflag & O_CREAT) && !ref.spec->Exists();
		
		sock = (Socket *) MPWFileSocket::open(ref.spec->RelPath(), oflag);
		
		if (sock && fresh)
			GUSIConfig.SetDefaultFType(*ref.spec);
	}
	
	return sock;
}

#define SFSaveDisk		(* (short *) 0x0214)
#define CurDirStore		(* (long *)  0x0398)

static long 					currentDir;
static SFReply					reply;
static char *					customPrompt;

static int do_getfile(
					TFileSpec * 	defaultFile, 
					TFileSpec * 	result,
					short				numTypes,
					SFTypeList		types)
{
	Point		myPoint	=	{75, 75};
	
	if (defaultFile) {
		*defaultFile += (StringPtr) "\p";
		
		SFSaveDisk 	= -defaultFile->vRefNum;
		CurDirStore = defaultFile->parID;
	}
	
	SFGetFile(myPoint, (StringPtr) "\p", NULL, numTypes, types, NULL, &reply);
	
	if (reply.good)
		*result = TFileSpec(reply.vRefNum, reply.fName);
	
	return reply.good;
}

static pascal Boolean FolderFFilter(ParmBlkPtr p)
{
	return !(p->fileParam.ioFlAttrib & ioDirMask);
}

#if USESROUTINEDESCRIPTORS
RoutineDescriptor	uFolderFFilter = 
		BUILD_ROUTINE_DESCRIPTOR(uppFileFilterProcInfo, FolderFFilter);
#else
#define uFolderFFilter FolderFFilter
#endif

static ControlHandle GetDlgCtrl(DialogPtr dlg, short item)
{
	short 	kind;
	Handle	hdl;
	Rect		box;
	
	GetDItem(dlg, item, &kind, &hdl, &box);
	return (ControlHandle) hdl;
}

static pascal short GetDirDlgHook(short item, DialogPtr dlgPtr)
{
	switch (item) {
	case sfHookFirstCall:
		if (customPrompt)
			setitext(Handle(GetDlgCtrl(dlgPtr, 13)), customPrompt);
		break;
	case 11:
		if (reply.fType) {
			if (!reply.fName[0])
				currentDir	=	reply.fType;
			else {
				TFileSpec dir(reply.vRefNum, CurDirStore, reply.fName);
	
				dir += (StringPtr) "\p";
				
				currentDir = dir.parID;
			}
				
			return	1;
		}
		break;
	
	case 12:
		currentDir	=	CurDirStore;
		
		return 1;
	case sfHookNullEvent:
		if (!reply.fType)
			HiliteControl(GetDlgCtrl(dlgPtr, 11), 255);
		else
			HiliteControl(GetDlgCtrl(dlgPtr, 11), 0);
		break;
	}
	
	return item;
}

#if USESROUTINEDESCRIPTORS
RoutineDescriptor	uGetDirDlgHook = 
		BUILD_ROUTINE_DESCRIPTOR(uppDlgHookProcInfo, GetDirDlgHook);
#else
#define uGetDirDlgHook GetDirDlgHook
#endif

static int do_getfolder(
					char *			prompt,
					TFileSpec * 	defaultFile, 
					TFileSpec * 	result)
{
	Point		myPoint	=	{75, 75};
	
	if (defaultFile) {
		*defaultFile += (StringPtr) "\p";
		
		SFSaveDisk 	= -defaultFile->vRefNum;
		CurDirStore = defaultFile->parID;
	}
	
	customPrompt = prompt && *prompt ? prompt : nil;
	
	SFPGetFile(
		myPoint, 
		(StringPtr) "\p", 
		FileFilterUPP(&uFolderFFilter), 
		-1, 
		nil, 
		DlgHookUPP(&uGetDirDlgHook), 
		&reply,
		GUSIRsrcID,				
		nil);

	if (reply.good) {
		result->vRefNum	=	-SFSaveDisk;
		result->parID		=	currentDir;
		--*result;
	}
	
	return reply.good;
}

static int 	do_putfile(	
					char	*			prompt,
					TFileSpec * 	defaultFile, 
					TFileSpec * 	result)
{
	Point			myPoint	=	{75, 75};
	StringPtr	defName;
	Str255		prmpt;
	
	prompt = (char *) memccpy((char *) prmpt + 1, prompt, 0, 254);
	
	if (prompt)
		prmpt[0] = prompt - (char *) prmpt - 2;
	else
		prmpt[0] = 254;
		
	if (defaultFile) {
		SFSaveDisk 	= -defaultFile->vRefNum;
		CurDirStore = defaultFile->parID;
		defName		= defaultFile->name;
	} else
		defName		= (StringPtr) "\p";
	
	SFPutFile(myPoint, prmpt, defName, NULL, &reply);
	
	if (reply.good)
		*result		= TFileSpec(reply.vRefNum, reply.fName);
	
	return reply.good;
}

int FileSocketDomain::choose(int, char * prompt, void * constraint, int flags, void * name, int * namelen)
{
	sa_constr_file * 	constr = (sa_constr_file *) constraint;
	TFileSpec			file;
	TFileSpec			df;
	TFileSpec *			defaultFile;
	Boolean				good;
	char *				path;
	int					len;
	
	if (flags & CHOOSE_DEFAULT) {
		df = TFileSpec((char *) name);
		defaultFile = df.Error() ? nil : &df;
	} else
		defaultFile = nil;
		
	if (flags & CHOOSE_NEW)
		good = do_putfile(prompt, defaultFile, &file);
	else if (flags & CHOOSE_DIR)
		good = do_getfolder(prompt, defaultFile, &file);
	else if (constr)
		good = do_getfile(defaultFile, &file, constr->numTypes, constr->types);
	else
		good = do_getfile(defaultFile, &file, -1, nil);
	
	if (good) {
		path	=	file.FullPath();
		len	=	int(strlen(path));
		
		if (len < *namelen)
			memcpy(name, path, (*namelen = len)+1);
		else
			return GUSI_error(EINVAL);
	} else
		return GUSI_error(EINTR);
	
	return 0;
}

int FileSocketDomain::remove(const GUSIFileRef & ref)
{
	if (ref.IsDevice())
		return GUSI_error(EINVAL);
	if (ref.Error())
		return File_error(ref.Error());
		
	return File_error(HDelete(ref.spec->vRefNum, ref.spec->parID, ref.spec->name));
}

int FileSocketDomain::rename(const GUSIFileRef & ref, const char *newname)
{	
	if (ref.IsDevice())
		return GUSI_error(EINVAL);
	if (ref.Error())
		return File_error(ref.Error());
	if (IsDevice(newname))
		return GUSI_error(EINVAL);
		
	TFileSpec	newnm(newname, true);
	
	return File_error(FSpSmartMove((TFileSpec *)ref.spec, &newnm));
}

void FileSocketDomain::fsetfileinfo(const GUSIFileRef & ref, unsigned long newcreator, unsigned long newtype)
{
	if (ref.IsDevice()) {
		GUSI_error(EINVAL);
		
		return;
	}
		
	FInfo			info;	

	if (ref.Error() || !ref.spec->Exists()
		|| HGetFInfo(ref.spec->vRefNum, ref.spec->parID, ref.spec->name, &info)
	) {
		GUSI_error(EIO);
		
		return;
	}
	
	info.fdType 	=	newtype;
	info.fdCreator	=	newcreator;
	
	if (HSetFInfo(ref.spec->vRefNum, ref.spec->parID, ref.spec->name, &info))
		GUSI_error(EIO);
	
	errno = 0;
}

void FileSocketDomain::fgetfileinfo(const GUSIFileRef & ref, unsigned long * creator, unsigned long * type)
{
	if (ref.IsDevice()) {
		GUSI_error(EINVAL);
		
		return;
	}
		
	FInfo			info;	

	if (ref.Error() || !ref.spec->Exists() 
		|| HGetFInfo(ref.spec->vRefNum, ref.spec->parID, ref.spec->name, &info)
	) {
		GUSI_error(EIO);
		
		return;
	}
	
	if (creator)
		*creator = info.fdCreator;
	
	if (type)
		*type = info.fdType;
	
	errno = 0;
}

int FileSocketDomain::faccess(const GUSIFileRef & ref, unsigned int cmd, long* arg)
{
	if (ref.IsDevice())
#if defined(powerc) || defined(__powerc)
		if (!file_faccess)
			return GUSI_error(ENOEXEC);
		else 
#endif
			return file_faccess((char *) ref.name, cmd, arg);
	
	if (ref.Error())
		return File_error(ref.Error());

#if defined(powerc) || defined(__powerc)
	if (!file_faccess)
		return GUSI_error(EINVAL);
	else
#endif
		return file_faccess(ref.spec->RelPath(), cmd, arg);
}

static OSErr GetVolume(const CInfoPBRec & cb, ParamBlockRec & pb)
{
	Str63				name;

	pb.volumeParam.ioNamePtr	=	name;
	pb.volumeParam.ioVRefNum	=	cb.hFileInfo.ioVRefNum;
	pb.volumeParam.ioVolIndex	=	0;

	return PBGetVInfo(&pb, false);
}

static int do_stat(const CInfoPBRec & cb, const ParamBlockRec & pb, struct stat & buf)
{
	buf.st_dev		=	pb.ioParam.ioVRefNum;
	buf.st_ino		=	cb.dirInfo.ioDrDirID;
	buf.st_nlink	=	1;
	buf.st_uid		=	0;
	buf.st_gid		=	0;
	buf.st_rdev		=	0;
	buf.st_atime	=	cb.hFileInfo.ioFlMdDat;
	buf.st_mtime	=	cb.hFileInfo.ioFlMdDat;
	buf.st_ctime	=	cb.hFileInfo.ioFlCrDat;
	buf.st_blksize	=	pb.volumeParam.ioVAlBlkSiz;

	if (!IsFile(cb))	{
		TFileSpec		spec;
		CInfoPBRec		info;

		spec.vRefNum	=	pb.ioParam.ioVRefNum;
		spec.parID		=	cb.dirInfo.ioDrDirID;
		
		++buf.st_nlink;
		
		if (GUSIConfig.accurStat) {
			for (int i = 0; i++ < cb.dirInfo.ioDrNmFls;) {
				spec	=	spec[i];
				if (!spec.Error() && !spec.CatInfo(info) && !IsFile(info))
					++buf.st_nlink;
			}
		} else {
			buf.st_nlink	+=	cb.dirInfo.ioDrNmFls;
		}
		
		buf.st_mode	=	S_IFDIR | 0777;
		buf.st_size	=	cb.dirInfo.ioDrNmFls;
	} else if (IsAlias(cb)) {
		buf.st_mode	=	S_IFLNK | 0777;
		buf.st_size	=	cb.hFileInfo.ioFlRLgLen;		/* Data fork is ignored	*/
	} else if (cb.hFileInfo.ioFlFndrInfo.fdType == '·OCK') {
		buf.st_mode	=	S_IFSOCK | 0666;
		buf.st_size	=	cb.hFileInfo.ioFlRLgLen;		/* Data fork is ignored	*/
	} else {
		buf.st_mode	=	S_IFREG | 0666;

		if (cb.hFileInfo.ioFlAttrib & 0x01)
			buf.st_mode&=	~0222;

		switch (cb.hFileInfo.ioFlFndrInfo.fdType) {
		case 'APPL':
		case 'MPST':
		case 'TEXT':
			buf.st_mode|=	0111;
			break;
		default:
			break;
		}
		
		buf.st_size	=	cb.hFileInfo.ioFlLgLen;		/* Resource fork is ignored	*/
	}

	buf.st_blocks	=	(buf.st_size + buf.st_blksize - 1) / buf.st_blksize;

	return 0;
}

static int do_special_stat(struct stat & buf)
{
	buf.st_dev			=	0;
	buf.st_ino			=	0;
	buf.st_mode			=	S_IFCHR | 0666 ;
	buf.st_nlink		=	1;
	buf.st_uid			=	0;
	buf.st_gid			=	0;
	buf.st_rdev			=	0;
	buf.st_size			=	1;
	buf.st_atime		=	time(NULL);
	buf.st_mtime		=	time(NULL);
	buf.st_ctime		=	time(NULL);
	buf.st_blksize		=	1;
	buf.st_blocks		=	1;
	
	return 0;
}

int FileSocketDomain::stat(const GUSIFileRef & ref, struct stat * buf)
{
	const CInfoPBRec *	cb;
	ParamBlockRec			pb;
	
	if (ref.IsDevice()) {
		return do_special_stat(*buf);
	} else {
		if (ref.Error() || !(cb = ref.Info()) || GetVolume(*cb, pb)) {
			errno = ENOENT;
	
			return -1;
		} else 
			return do_stat(*cb, pb, *buf);
	}
}

int FileSocketDomain::chmod(const GUSIFileRef & ref, mode_t mode)
{
	const CInfoPBRec * cb;
	
	if (ref.IsDevice())
		return GUSI_error(EINVAL);
		
	if (ref.Error() || !(cb = ref.Info()))
		return GUSI_error(ENOENT);

	if (!(cb->dirInfo.ioFlAttrib & 0x10))
		if (mode & S_IWUSR) {
			if (cb->hFileInfo.ioFlAttrib & 0x01)
				HRstFLock(ref.spec->vRefNum, ref.spec->parID, ref.spec->name);
		} else {
			if (!(cb->hFileInfo.ioFlAttrib & 0x01))
				HSetFLock(ref.spec->vRefNum, ref.spec->parID, ref.spec->name);
		}
	
	return 0;
}

int FileSocketDomain::utime(const GUSIFileRef & ref, const struct utimbuf * times)
{
	if (ref.IsDevice())
		return GUSI_error(EINVAL);
	
	CInfoPBRec				cb;
	const CInfoPBRec *	cbp;

	if (ref.Error() || !(cbp = ref.Info()))
		return GUSI_error(ENOENT);
	
	cb 							=	*cbp;
	cb.hFileInfo.ioVRefNum	=	ref.spec->vRefNum;
	cb.hFileInfo.ioDirID		=	ref.spec->parID;
	cb.hFileInfo.ioNamePtr	=	ref.spec->name;
	cb.hFileInfo.ioFlMdDat	=	times ? times->modtime : time(nil);
	// times->actime is ignored. The Mac has no access times.
	
	return File_error(PBSetCatInfoSync(&cb));
}

/************************ FileSocket members ************************/

int FileSocket::fcntl(unsigned int, int)
{
	return GUSI_error(EOPNOTSUPP);
}

int FileSocket::ioctl(unsigned int, void *)
{
	return GUSI_error(EOPNOTSUPP);
}

static OSErr GetFDCatInfo(short fRefNum, CInfoPBRec & cb)
{
	OSErr				err;
	FCBPBRec			fcb;
	Str255			fname;

	fcb.ioNamePtr	= 	fname;
	fcb.ioRefNum	=	fRefNum;
	fcb.ioFCBIndx	= 	0;
	if (err = PBGetFCBInfoSync(&fcb))
		return err;

	cb.hFileInfo.ioNamePtr		=	fname;
	cb.hFileInfo.ioDirID			=	fcb.ioFCBParID;
	cb.hFileInfo.ioVRefNum		=	fcb.ioFCBVRefNum;
	cb.hFileInfo.ioFDirIndex	=	0;

	return PBGetCatInfoSync(&cb);
}

int FileSocket::fstat(struct stat * buf)
{
	CInfoPBRec		cb;
	ParamBlockRec	pb;

	if (GetFDCatInfo(fRefNum, cb) || GetVolume(cb, pb)) {
		errno = ENOENT;

		return -1;
	} else
		return do_stat(cb, pb, *buf);
}

int FileSocket::select(Boolean * canRead, Boolean * canWrite, Boolean *)
{
	int	goodies	=	0;
	
	// Simplicistic implementation 
	
	if (canRead)	{
		*canRead = true;
		++goodies;
	}
	
	if (canWrite)	{
		*canWrite = true;
		++goodies;
	}
	
	return goodies;
}

FileSocket::~FileSocket()
{
}

/***************** Things that happen to real files only *****************/

OSErr VRef2Icon(short vRef, Handle * icon)
{
	OSErr				err;
	HParmBlkPtr		hpp;
	ParmBlkPtr		pp;
	HParamBlockRec	hpb;
	
	hpp								=	&hpb;
	pp									=	(ParmBlkPtr) hpp;
	hpp->volumeParam.ioVRefNum	=	vRef;
	hpp->volumeParam.ioNamePtr	=	nil;
	hpp->volumeParam.ioVolIndex=	0;
	if (err = PBHGetVInfoSync(hpp))
		return err;
		
	pp->cntrlParam.ioVRefNum	=	hpb.volumeParam.ioVDrvInfo;
	pp->cntrlParam.ioCRefNum	=	hpb.volumeParam.ioVDRefNum;
	pp->cntrlParam.csCode		=	21;
	
	if (err = PBControlSync(pp))
		return err;
	
	PtrToHand(*(Ptr *) pp->cntrlParam.csParam, icon, 256);
	
	return noErr;
}

typedef OSType	TTypeMap[2];

static TTypeMap map[]	=	{
	{'amnu', 'faam'},
	{'ctrl', 'fact'},
	{'extn', 'faex'},
	{'pref', 'fapf'},
	{'prnt', 'fapn'},
	{'empt', 'trsh'},
	{'trsh', 'trsh'},
	{'strt', 'fast'},
	{'macs', 'fasy'},
	{     0,      0}
};

#ifdef GUSI_FILE_DEBUG
ostream & operator<<(ostream & str, OSType * ty)
{
	return str 	<< "'"
					<< char((*ty >> 24) & 0xFF) 
					<< char((*ty >> 16) & 0xFF)
					<< char((*ty >> 8) & 0xFF)
					<< char(*ty & 0xFF)
					<< "'";
}
#endif

void OurResidentAliasExpert(
	TFileSpec & file, 
	OSType * 	fCreator, 
	OSType * 	fType,
	TFileSpec * iconFile,
	short * 		iconID)
{
	Boolean						appleShare;
	CInfoPBRec					info;
	GetVolParmsInfoBuffer	volParms;
	HParamBlockRec				pb;
	
	*fCreator = 'MACS';
	*iconFile	=	file;
	*iconID		=	kCustomIconResource;
	
	if (file.parID == fsRtParID)
		appleShare	=	true;
	else {
		if (file.CatInfo(info))
			goto error;
		
		appleShare = !IsFile(info);
	}
	
	if (appleShare)	{
		pb.ioParam.ioNamePtr	=	nil;
		pb.ioParam.ioVRefNum	=	file.vRefNum;
		pb.ioParam.ioBuffer	=	Ptr(&volParms);
		pb.ioParam.ioReqCount=	sizeof(GetVolParmsInfoBuffer);
		
		if (PBHGetVolParmsSync(&pb) || !volParms.vMServerAdr)	
			appleShare	=	false;
	}
	
	if (appleShare)
		if (file.parID == fsRtParID)
			*fType	=	'srvr';
		else if (!HasRdPerm(info))
			*fType 	=	'fadr';
		else
			*fType	=	'faet';
	else if (file.parID == fsRtParID)
		*fType	=	'hdsk';
	else if (!IsFile(info)) 
		if (DirIsMounted(info))
			*fType = 'famn';
		else if (DirIsExported(info))
			*fType = 'fash';
		else if (DirIsShared(info))
			*fType = 'faet';
		else 
			*fType = 'fdrp';

	if (file.parID == fsRtParID)	{
		iconFile->parID	=	fsRtDirID;
		PLstrcpy(iconFile->name, (StringPtr) "\pIcon\n");
	} else if (!IsFile(info))	{
		if (info.dirInfo.ioDrDirID < 9)	{
			short vRef;
			long	dirID;
			
			for (TTypeMap * mapp = map; **mapp; ++mapp)
				if (!FindFolder(file.vRefNum, (*mapp)[0], false, &vRef, &dirID))
					if (dirID == info.dirInfo.ioDrDirID) {
						*fType = (*mapp)[1];

						break;
					}
		}
		*iconFile += (StringPtr) "\pIcon\n";
	} else {
		*fType	=	info.hFileInfo.ioFlFndrInfo.fdType;
		*fCreator=	info.hFileInfo.ioFlFndrInfo.fdCreator;
	}
	
#ifdef GUSI_FILE_DEBUG
	cerr << "Type = " << fType << ", creator = " << fCreator << endl;
	cerr << "Look for custom icons in " << iconFile->FullPath() << endl;
#endif

	return;
error:
	*fType		=	0;
	*fCreator	=	0;
}

static OSType iconTypes[]	=	{
	'ICN#',
	'ics#',
	'icl4',
	'ics4',
	'icl8', 
	'ics8',
	0
};

Boolean CopyIconFamily(short srcResFile, short srcID, short dstResFile, short dstID)
{
	Handle	icon;
	Boolean	success	=	false;
	OSType * types;

	for (types = iconTypes; *types; ++types)	{
		UseResFile(srcResFile);
		if (icon = Get1Resource(*types, srcID))	{
			UseResFile(dstResFile);
			DetachResource(icon);
			AddResource(icon, *types, dstID, (StringPtr) "\p");
		
			success = success || !ResError();
		}
	}
	
	return success;
}

Boolean AddIconsToFile(
	const TFileSpec &	origFile,
	short 				aliasFile, 
	OSType 				fCreator, 
	OSType				fType, 
	const FSSpec &		iconFile, 
	short					iconID)
{
	short		iFile;
	Boolean	success;
	Handle 	icon;

	iFile = FSpOpenResFile(&iconFile, fsRdPerm);
	
	if (iFile == -1)
		goto noCustom;
	
	success = CopyIconFamily(iFile, iconID, aliasFile, kCustomIconResource);
	
	CloseResFile(iFile);
	
	if (success)
		return true;

#ifdef GUSI_FILE_DEBUG
	cerr << "No custom Icons found." << endl;
#endif

noCustom:	
	if (fType == 'hdsk' && fCreator == 'MACS')
		if (!VRef2Icon(origFile.vRefNum, &icon))	{
#ifdef GUSI_FILE_DEBUG
			cerr << "Found icon for disk drive." << endl;
#endif
			AddResource(icon, 'ICN#', kCustomIconResource, (StringPtr) "\p");
			
			return !ResError();
		}
				
	return false;
}

int symlink(const char* linkto, const char* linkname)
{
	if (IsDevice(linkto))
		return GUSI_error(EINVAL);
	if (IsDevice(linkname))
		return GUSI_error(EINVAL);
		
	OSType		fType;
	OSType		fCreator;
	short			iconID;
	short			aliasFile;
	AliasHandle	alias;
	Boolean		customIcon;
	TFileSpec	iconFile;
	FInfo			info;
	
	if (!hasAlias || !hasMakeFSSpec)
		return GUSI_error(EOPNOTSUPP);
		
	TFileSpec	oldnm(linkto);
	
	if (oldnm.Error() || !oldnm.Exists())
		return GUSI_error(EIO);
		
	TFileSpec	newnm(linkname, true);

	if (newnm.Error())
		return GUSI_error(EIO);
	
	if (newnm.Exists())
		return GUSI_error(EEXIST);
	
	OurResidentAliasExpert(oldnm, &fCreator, &fType, &iconFile, &iconID);
	
#ifdef GUSI_FILE_DEBUG
	cerr << "Creating " << newnm.FullPath() << endl;
#endif

	FSpCreateResFile(&newnm, fCreator, fType, smSystemScript);
	
	if (ResError())
		return GUSI_error(EIO);
	
#ifdef GUSI_FILE_DEBUG
	cerr << "Opening " << newnm.FullPath() << endl;
#endif

	aliasFile = FSpOpenResFile(&newnm, fsRdWrPerm);
	
	if (aliasFile == -1)
		goto deleteFile;
	
#ifdef GUSI_FILE_DEBUG
	cerr << "Creating alias for " << oldnm.FullPath() << " in " << newnm.FullPath() << endl;
#endif
	
	if (NewAlias(nil, &oldnm, &alias))
		goto closeFile;

#ifdef GUSI_FILE_DEBUG
	cerr << "Adding alias to file." << endl;
#endif
	
	AddResource((Handle) alias, 'alis', 0, oldnm.name);
	
	if (ResError())
		goto deleteAlias;
	
	customIcon = AddIconsToFile(oldnm, aliasFile, fCreator, fType, iconFile, iconID);

#ifdef GUSI_FILE_DEBUG
	cerr << "There were " << (customIcon ? "" : "no ") << "custom Icons." << endl;
#endif
		
	CloseResFile(aliasFile);

	FSpGetFInfo(&newnm, &info);
	info.fdFlags	|=	(1 << 15) | (customIcon ? (1 << 10) : 0);
	info.fdFlags	&= ~(1 << 8);
	FSpSetFInfo(&newnm, &info);
	
	return 0;

deleteAlias:
	DisposHandle((Handle) alias);
closeFile:
	CloseResFile(aliasFile);
deleteFile:
	FSpDelete(&newnm);	
	
	return GUSI_error(EIO);
}

int readlink(const char * path, char * buf, int bufsiz)
{
	if (IsDevice(path))
		return GUSI_error(EINVAL);
		
	char * 		end;
	TFileSpec	file(path, true);
	CInfoPBRec	info;
	
	if (file.CatInfo(info))
		goto error;
		
	if (!IsAlias(info))
		return GUSI_error(EINVAL);
	
	if (file.Resolve())
		goto error;

	end = (char *) memccpy(buf, file.FullPath(), 0, bufsiz);
	
	if (!end)
		return GUSI_error(ENAMETOOLONG);
	else
		return end - buf - 1;
		
error:
	return File_error(file.Error());
}

/************************* directory stuff **************************/

struct dir {
	short			index;
	short			vol;
	long			dirID;
	dirent		entry;
};

DIR * opendir(const char * name)
{
	DIR *			d;
	TFileSpec	spec(name);
	
	if (spec.Error())
		goto error;
	
	spec += (StringPtr) "\p";
	
	if (spec.Error())
		goto error;
	
	d = (DIR *) NewPtr(sizeof(DIR));
	
	d->index	=	1;
	d->vol	=	spec.vRefNum;
	d->dirID	=	spec.parID;
	
	return d;
	
error:
	File_error(spec.Error());
	
	return nil;
}
	
struct dirent * readdir(DIR * dirp)
{
	TFileSpec	spec;
	int 			i; 
	int			e = errno;
	
	spec.vRefNum	=	dirp->vol;
	spec.parID		=	dirp->dirID;
	spec.name[0]	=  0;
	
	spec = spec[dirp->index++];
	
	if (spec.Error())
		goto error;
	
	dirp->entry.d_fileno	=	spec.LastInfo()->dirInfo.ioDrDirID;
	dirp->entry.d_namlen = *spec.name;
	
	memcpy(dirp->entry.d_name, (char *) spec.name+1, dirp->entry.d_namlen);
	
	dirp->entry.d_name[dirp->entry.d_namlen] = 0;
	
	i = dirp->entry.d_namlen;
	do 
		dirp->entry.d_name[i++] = 0;
	while (i & 3);
	
	dirp->entry.d_reclen = sizeof(u_long)+sizeof(u_short)+sizeof(u_short)+i;
	
	return &dirp->entry;
	
error:
	File_error(spec.Error());
	
	if (errno == ENOENT)
		errno = e;
		
	return nil;
}

long telldir(const DIR * dirp)
{
	return dirp->index;
}

void seekdir(DIR * dirp, long loc)
{
	dirp->index	=	(short) loc;
}

void rewinddir(DIR * dirp)
{
	dirp->index	=	1;
}

int closedir(DIR * dirp)
{
	DisposPtr((Ptr) dirp);
	
	return 0;
}

int scandir(
	const char * 		name, 
	struct dirent *** namelist,
   int (*want)(struct dirent *), 
	int (*cmp)(const void *, const void *))
{
	struct dirent *	entry;
	struct dirent *	copy;
	struct dirent **	names;
	int	 				count;
	DIR *					dirp;
	CInfoPBRec			info;
	TFileSpec			spec;
	
	if ((dirp = opendir(name)) == NULL)
		return -1;

	spec.vRefNum	=	dirp->vol;
	spec.parID		=	dirp->dirID;
	--spec;
	
	if (spec.CatInfo(info))
		return File_error(spec.Error());

	names = (struct dirent **) malloc(info.dirInfo.ioDrNmFls * sizeof(struct dirent *));
	if (names == NULL)
		return GUSI_error(ENOMEM);

	count = 0;
	while ((entry = readdir(dirp)) != NULL) {
		if (want && !(*want)(entry))
			continue;	/* Don't want this entry */

		if (!(copy = (struct dirent *)malloc(entry->d_reclen))) {
			free(names);
			
			return GUSI_error(ENOMEM);
		}
		
		memcpy(copy, entry, entry->d_reclen);

		names[count++] = copy;
	}
	
	closedir(dirp);
	
	if (count && cmp)
		qsort(names, count, sizeof(struct dirent *), cmp);
		
	*namelist = names;
	
	return count;
}

int chdir(const char * path)	
{
	TFileSpec	dir(path);
	
	if (dir.Error())
		return GUSI_error(ENOENT);
		
	return File_error(TFileSpec::ChDir(dir));
}

int mkdir(const char * path)	
{
	OSErr			err;
	long			nuDir;
	TFileSpec	dir(path, true);
	
	if (dir.Error())
		return File_error(dir.Error());
	
	if (err = DirCreate(dir.vRefNum, dir.parID, dir.name, &nuDir))
		return File_error(err);

	return 0;
}

int rmdir(const char * path)	
{
	OSErr			err;
	TFileSpec	dir(path);
	
	if (dir.Error())
		return GUSI_error(ENOENT);

	if (err = HDelete(dir.vRefNum, dir.parID, dir.name))
		switch (err)	{
		default:
			return File_error(err);
		case dirFulErr:
			return GUSI_error(ENOTEMPTY);
		}
	
	return 0;
}

char * getcwd(char * buf, size_t size)
{
	OSErr			err;
	TFileSpec	cwd;
	char * 		res;
	
	if (err = cwd.Default()) {
		File_error(err);
	
		return nil;
	}
		
	res = cwd.FullPath();
	
	if (size < strlen(res)+1)
		return (char *) GUSI_error_nil(ENAMETOOLONG);
	if (!buf && !(buf = (char *) malloc(size)))
		return (char *) GUSI_error_nil(ENOMEM);

	strcpy(buf, res);
	
	return buf;
}