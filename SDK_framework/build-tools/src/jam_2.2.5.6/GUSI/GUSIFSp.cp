/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	GUSIFSp.cp		-	Dealing with paths
Author	:	Matthias Neeracher <neeri@iis.ethz.ch>
Language	:	MPW C++

$Log: GUSIFSp.cp,v $
Revision 1.2  1999/04/23  08:43:45  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.5  1995/01/23  01:28:19  neeri
Fixed TFileSpec::TFileSpec(short wd, ...)

Revision 1.4  1994/12/30  20:00:56  neeri
Add encoded FSSpecs.

Revision 1.3  1994/08/10  00:27:23  neeri
Sanitized for universal headers.
Fixed a deadly bug with multiple nonexistent path components.

Revision 1.2  1994/05/01  23:38:45  neeri
Another rename() fix.

Revision 1.1  1994/03/08  22:06:44  neeri
Initial revision

Revision 0.12  1994/02/04  00:00:00  neeri
Make TFileSpec constructors preserve case to allow above

Revision 0.11  1993/10/24  00:00:00  neeri
Allow changing case in a rename

Revision 0.10  1993/09/27  00:00:00  neeri
FSpSmartMove

Revision 0.9  1993/07/17  00:00:00  neeri
LastInfo

Revision 0.8  1993/06/21  00:00:00  neeri
Throw out the inline

Revision 0.7  1993/03/01  00:00:00  neeri
Bless

Revision 0.6  1993/02/06  00:00:00  neeri
Use FSMakeFSSpec if possible

Revision 0.5  1993/01/15  00:00:00  neeri
IsParentOf

Revision 0.4  1992/11/15  00:00:00  neeri
Rename GUSIFSp_P.h to TFileSpec.h (there we go again)

Revision 0.3  1992/11/15  00:00:00  neeri
Forgot a few consts

Revision 0.2  1992/09/12  00:00:00  neeri
Renamed Paths.h to GUSIFSp_P.h

Revision 0.1  1992/09/06  00:00:00  neeri
Clear ioACUser

*********************************************************************/

#include "GUSI_P.h"
#include "TFileSpec.h"

#include <Errors.h>
#include <Memory.h>
#include <Aliases.h>
#include <string.h>
#include <ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <TextUtils.h>
#include <PLStringFuncs.h>

OSErr 		TFileSpec::error;
short 		TFileSpec::curVol;
long			TFileSpec::curDir	=	-1;
CInfoPBRec	TFileSpec::lastInfo;

OSErr TFileSpec::ChDir(const TFileSpec & spec)
{
	TFileSpec nudir(spec);
	
	nudir += (StringPtr) "\p";
	
	if (error)
		return error;
	
	curVol	=	nudir.vRefNum;
	curDir	=	nudir.parID;

	return noErr;
}

static OSErr CurrentDir(short & vRefNum, long & parID)
{
	OSErr		error;
	WDPBRec	vol;
	Str63		name;
	
	vol.ioNamePtr	=	name;
	
	if (error = PBHGetVolSync(&vol))
		return error;
		
	vRefNum	=	vol.ioWDVRefNum;
	parID		=	vol.ioWDDirID;
	
	return noErr;
}

OSErr TFileSpec::DefaultDir()
{
	if (curDir != -1)	{
		vRefNum	=	curVol;
		parID		=	curDir;
		
		return noErr;
	} else
		return error = CurrentDir(vRefNum, parID);
}

OSErr TFileSpec::Default()
{
	if (!DefaultDir())
		--*this;
	
	return error;
}
 
TFileSpec::TFileSpec(const TFileSpec & spec)
{
	vRefNum = spec.vRefNum;
	parID   = spec.parID;
	if (spec.name[0] < 64)
		PLstrcpy(name, spec.name);
	else
		memcpy(name, spec.name, 64);
}

TFileSpec::TFileSpec(const FSSpec & spec, Boolean useAlias)							
{
	vRefNum = spec.vRefNum;
	parID   = spec.parID;
	if (spec.name[0] < 64)
		PLstrcpy(name, spec.name);
	else
		memcpy(name, spec.name, 64);

	if (!useAlias && hasAlias)
		Resolve();
}

TFileSpec::TFileSpec(short vRefNum, long parID, ConstStr31Param name, Boolean useAlias)
{
	OSErr	err;
	
	if (!hasMakeFSSpec || 
		((err = FSMakeFSSpec(vRefNum, parID, name, this)) && (err != fnfErr))
	) {
		this->vRefNum	=	vRefNum;
		this->parID		=	parID;
		memcpy(this->name, name, *name+1);
	}
	
	if (!useAlias && hasAlias)
		Resolve();	
		
	if (EqualString(this->name, name, false, true))
		memcpy(this->name, name, *name+1);		
}

TFileSpec::TFileSpec(short wd, ConstStr31Param name, Boolean useAlias)
{
	OSErr	err;
	
	if (!hasMakeFSSpec || 
		((err = FSMakeFSSpec(wd, 0, name, this)) && (err != fnfErr))
	) {
		WDPBRec 	wdPB;
		
		wdPB.ioNamePtr 	= nil;
		wdPB.ioVRefNum 	= wd;
		wdPB.ioWDIndex 	= 0;
		wdPB.ioWDProcID 	= 0;
		
		/* Change the Working Directory number in vRefnum into a real vRefnum */
		/* and DirID. The real vRefnum is returned in ioVRefnum, and the real */
		/* DirID is returned in ioWDDirID. */
		
		if (error = PBGetWDInfoSync(&wdPB))
			return;
		
		vRefNum	= wdPB.ioWDVRefNum;
		parID		= wdPB.ioWDDirID;
		memcpy(this->name, name, *name+1);
	}
	
	if (!useAlias && hasAlias)
		Resolve();
		
	if (EqualString(this->name, name, false, true))
		memcpy(this->name, name, *name+1);		
}

TFileSpec::TFileSpec(OSType object, short vol, long dir)
{
	if (object == kTempFileType && dir) {
		vRefNum	=	vol;
		parID		=	dir;
	} else if (
		error = 
			FindFolder(
				vol, (object == kTempFileType) ? kTemporaryFolderType : object,
				true, &vRefNum, &parID)
	) 
		return;
		
	if (object == kTempFileType) {
		static long nr = -1;
		
		strcpy((char *) name, (char *) "\ptmp00000");
		
		do {
			nr = (nr + 1) % 100000;
			
			sprintf((char *) name+4, "%05ld", nr);
		} while (Exists());
	} else {
		*this -= 1;
	}
}

void TFileSpec::Bless()
{
	if (hasMakeFSSpec) {
		FSSpec	spec	=	*this;	// Dont know what happens to an aliased name
	
		error = FSMakeFSSpec(spec.vRefNum, spec.parID, spec.name, this);
		
		if (EqualString(spec.name, name, false, true))
			memcpy(name, spec.name, *spec.name+1);		
	} else
		error = noErr;
}

#define	maxPathLen 512

static char fullPath[maxPathLen];

/* Convert an FSSpec into a full pathname. The pathname is accumulated on the
   high end of path to avoid excessive copying.
*/

char * TFileSpec::FullPath() const
{
	char *			curPath;
	
	/* Special case: a volume was specified */
	if (parID == fsRtParID)	{
		memcpy(fullPath, name+1, *name);
		curPath		=	fullPath+*name;
		curPath[0]	=	':';
		curPath[1]	=	0;
		error			=	noErr;
		
		return fullPath;
	}
		
	fullPath[maxPathLen-1]	=	0;
	curPath 						=	fullPath+maxPathLen-*name-1;
	
	memcpy(curPath, name+1, *name);
	
   lastInfo.dirInfo.ioNamePtr = (StringPtr) fullPath;
   lastInfo.dirInfo.ioDrParID = parID;

   do {
      lastInfo.dirInfo.ioVRefNum 	= vRefNum;
      lastInfo.dirInfo.ioFDirIndex 	= -1;
      lastInfo.dirInfo.ioDrDirID 	= lastInfo.dirInfo.ioDrParID;

      if (error = PBGetCatInfoSync(&lastInfo))
		 	return "";
      *--curPath	=	':';
		curPath	-=	*fullPath;
		memmove(curPath, fullPath+1, *fullPath);
   } while (lastInfo.dirInfo.ioDrDirID != fsRtDirID);

	return curPath;
}

char * TFileSpec::RelPath() const
{
	short			curVRef;
	long			curDirID;
	char *		curPath;
	
	if (CurrentDir(curVRef, curDirID))
		return FullPath();
		
	/* Special case: a volume was specified */
	if (parID == fsRtParID)	{
		if (vRefNum == curVRef && curDirID == fsRtDirID)
			curPath = fullPath;
		else {
			memcpy(fullPath, name+1, *name);
			curPath		=	fullPath+*name;
		}
		
		curPath[0]	=	':';
		curPath[1]	=	0;
		
		return fullPath;
	}

	lastInfo.dirInfo.ioNamePtr		=	(StringPtr)fullPath;
	lastInfo.dirInfo.ioVRefNum 	= 	curVRef;
	lastInfo.dirInfo.ioFDirIndex 	= 	-1;
	lastInfo.dirInfo.ioDrDirID 	= 	curDirID;
	
	if (error = PBGetCatInfoSync(&lastInfo))
		return "";
	
	if (curVRef == vRefNum 
		&& lastInfo.dirInfo.ioDrParID == parID
		&& !memcmp (fullPath, name, *fullPath+1)
	)	{
		fullPath[0]	=	':';
		fullPath[1] = 	0;
		
		return fullPath;
	}
		
	fullPath[maxPathLen-1]	=	0;
	curPath 						=	fullPath+maxPathLen-*name-1;
	
	memcpy(curPath, name+1, *name);
	
   lastInfo.dirInfo.ioNamePtr = (StringPtr) fullPath;
   lastInfo.dirInfo.ioDrParID = parID;

   do {
      *--curPath	=	':';
		 
		/* Test fur current directory */
		if (curVRef == vRefNum && curDirID == lastInfo.dirInfo.ioDrParID)
			return strchr(curPath+1, ':') ? curPath : curPath+1;
			
		lastInfo.dirInfo.ioVRefNum 	= vRefNum;
		lastInfo.dirInfo.ioFDirIndex = -1;
		lastInfo.dirInfo.ioDrDirID 	= lastInfo.dirInfo.ioDrParID;
		
		if (error = PBGetCatInfoSync(&lastInfo))
			return "";
		curPath	-=	*fullPath;
		memmove(curPath, fullPath+1, *fullPath);
   } while (lastInfo.dirInfo.ioDrDirID != fsRtDirID);

	return curPath;
}	

// Give encoded path. Encoding is:
//
// 1 byte:   DC1  (ASCII 0x11)
// 4 bytes:  Volume reference number in zero-padded hex
// 8 bytes:  Directory ID in zero-padded hex
// n bytes:  Partial pathname, starting with ':'
//
// Needless to say, passing encoded values to anything but a GUSI routine is 
// a bad idea.
char * TFileSpec::Encode() const
{
	sprintf(fullPath, "\021%04hX%08X:%P", vRefNum, parID, name);
	
	return fullPath;
}


OSErr	TFileSpec::CatInfo(CInfoPBRec & info, Boolean dirInfo) const
{
   info.dirInfo.ioVRefNum 		= vRefNum;
   info.dirInfo.ioDrDirID 		= parID;
   info.dirInfo.ioNamePtr 		= (StringPtr) name;
   info.dirInfo.ioFDirIndex 	= dirInfo ? -1 : 0;
#ifdef OnceThisFieldIsDefined
	info.dirInfo.ioACUser 		= 0;
#else
	info.dirInfo.filler2 		= 0;
#endif
		
   return error = PBGetCatInfoSync(&info);
}

TFileSpec TFileSpec::operator--()
{
  	CatInfo(lastInfo, true);
	
	if (!error)
		parID	= lastInfo.dirInfo.ioDrParID;
	
	return *this;
}

TFileSpec TFileSpec::operator-=(int levels)
{
	while (levels-- > 0)	{
		--*this;
		if (this->Error())
			break;
	}
	
	return *this;
}

TFileSpec TFileSpec::operator-(int levels) const
{
	TFileSpec	spec	=	*this;
	
	return spec -= levels;
}

OSErr TFileSpec::Resolve(const CInfoPBRec & info)
{
	Boolean		isFolder;
	Boolean		wasAlias;
	
	return error = 
		(hasAlias && IsAlias(info)) ? 
			ResolveAliasFile(this, true, &isFolder, &wasAlias) :
			noErr;
}

OSErr TFileSpec::Resolve(Boolean gently)
{
	CatInfo(lastInfo);
	
	if (error)
		if (gently)
			return error = noErr;
		else
			return error;
	else
		return Resolve(lastInfo);
}

Boolean TFileSpec::Exists() const
{
	Boolean		res;
	
	res 	= !CatInfo(lastInfo);
	error	=	noErr;
	
	return res;
}

Boolean TFileSpec::operator==(const TFileSpec & other) const
{
	return 	vRefNum == other.vRefNum 
		&&		parID == other.parID 
		&& 	EqualString(name, other.name, false, true);
}

Boolean TFileSpec::operator!=(const TFileSpec & other) const
{
	return 	vRefNum != other.vRefNum 
		||		parID != other.parID 
		|| 	!EqualString(name, other.name, false, true);
}

Boolean TFileSpec::IsParentOf(const TFileSpec & other) const
{
	for (TFileSpec oth = other - 1; !oth.Error() && *this != oth; --oth);
	
	return !oth.Error();
}

TFileSpec TFileSpec::operator+=(ConstStr31Param name)
{
	if (*name > 63)
		return bdNamErr;
		
   if (CatInfo(lastInfo))
		goto punt;
	
	// Resolve if an alias
	
	if (IsAlias(lastInfo))
		if (Resolve(lastInfo) || CatInfo(lastInfo))
			goto punt;
	
	if (IsFile(lastInfo)) {
		error = bdNamErr;
		
		goto punt;
	}
	
	parID	= lastInfo.dirInfo.ioDrDirID;

	memcpy(this->name, name, *name+1);

punt:	
	return *this;
}

TFileSpec TFileSpec::operator+(ConstStr31Param name) const
{
	TFileSpec	spec	=	*this;
	
	return spec += name;
}

TFileSpec TFileSpec::operator+=(const char * name)
{
	int	len = int(strlen(name));
	
	if (len > 63)
		return bdNamErr;
		
   if (CatInfo(lastInfo))
		goto punt;
	
	// Resolve if an alias
	
	if (IsAlias(lastInfo))
		if (Resolve(lastInfo) || CatInfo(lastInfo))
			goto punt;
	
	if (IsFile(lastInfo)) {
		error = bdNamErr;
		
		goto punt;
	}
	
	parID	= lastInfo.dirInfo.ioDrDirID;

	memcpy(this->name+1, name, *this->name = len);

punt:	
	return *this;
}

TFileSpec TFileSpec::operator+(const char * name) const
{
	TFileSpec	spec	=	*this;
	
	return spec += name;
}

TFileSpec TFileSpec::operator[](short index) const
{
	TFileSpec	spec 	= *this;
	
   lastInfo.dirInfo.ioVRefNum 	= spec.vRefNum;
   lastInfo.dirInfo.ioDrDirID	 	= spec.parID;
   lastInfo.dirInfo.ioNamePtr 	= spec.name;
   lastInfo.dirInfo.ioFDirIndex 	= index;

   error = PBGetCatInfoSync(&lastInfo);
	
	return spec;
}

static Boolean ReadNHex(const char * nr, int count, unsigned long * val)
{
	for (*val = 0; count--; ++nr) {
		*val <<= 4;
		switch (*nr) {
		case '0':
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
			*val |= *nr - '0';	
			break;
		case 'a':
		case 'b':
		case 'c':
		case 'd':
		case 'e':
		case 'f':
			*val |= *nr - 'a' + 10;	
			break;
		case 'A':
		case 'B':
		case 'C':
		case 'D':
		case 'E':
		case 'F':
			*val |= *nr - 'A' + 10;	
			break;
		default:
			return false;
		}
	}
	
	return true;
}

Boolean TFileSpec::IsEncodedFSSpec(const char * path, Boolean useAlias)
{
	// To be as robust as possible against conflicts, we won't tolerate any
	// deviations from the pattern:
	
	// 1 byte:   DC1  (ASCII 0x11)
	// 4 bytes:  Volume reference number in zero-padded hex
	// 8 bytes:  Directory ID in zero-padded hex
	// n bytes:  Partial pathname, starting with ':'

	if (*path++ != 0x11)		// Magic character DC1
		return false;
	
	unsigned long 	val;
	
	if (ReadNHex(path, 4, &val)) {
		vRefNum = short(val);
		if (ReadNHex(path+4, 8, &val)) {
			parID = long(val);
			path += 12;
			
			if (*path == ':' && !strchr(path+1, ':')) {
				*(char *)path 	= strlen(path+1);
				
				if (hasMakeFSSpec) {
					if (error = FSMakeFSSpec(vRefNum, parID, (ConstStr255Param) path, this))
						if (error == fnfErr)
							error = noErr;
				} else {
					this->vRefNum	=	vRefNum;
					this->parID		=	parID;
					memcpy(this->name, path, *path+1);
				}
				
				if (!useAlias && hasAlias)
					Resolve();	
					
				if (EqualString(this->name, (ConstStr255Param) path, false, true))
					memcpy(this->name, path, *path+1);
				
				*(char *)path 	= ':';
				
				return true;
			}
		}
	}
	return false;
}

TFileSpec::TFileSpec(const char * path, Boolean useAlias)
{
	if (IsEncodedFSSpec(path, useAlias))
		return;

	int			pathLen	= 	int(strlen(path));
	StringPtr	name		=	(StringPtr) fullPath;
	char *		nextPath;
		
	if (hasMakeFSSpec) {
		DefaultDir();
		
		nextPath = (char *) memccpy((char *) name + 1, path, 0, 254);
	
		if (nextPath)
			name[0] = nextPath - (char *) name - 2;
		else
			name[0] = 254;
		
		switch (error = FSMakeFSSpec(vRefNum, parID, name, this)) {
		case fnfErr:
			error = noErr;
			
			return;
		case noErr:
			if (!useAlias && hasAlias)
				Resolve();

			if (nextPath = strrchr(path, ':'))
				path = nextPath+1;

			nextPath = (char *) memccpy((char *) name + 1, path, 0, 254);
	
			if (nextPath)
				name[0] = nextPath - (char *) name - 2;
			else
				name[0] = 254;
		
			if (EqualString(this->name, name, false, true))
				memcpy(this->name, name, *name+1);		
			
			return;
		default:
			break;
		}
	}
	
	if (path[0] == ':' || !(nextPath = strchr(path, ':'))) {
		Default();
		
		if (*path == ':')
			++path;
	} else {
		ParamBlockRec	vol;
		
		if (nextPath - (char *) path > 62) {
			error = bdNamErr;
			
			return;
		}
			
		memcpy(name+1, (char *) path, *name = nextPath - (char *)  path + 1);
		
		vol.volumeParam.ioNamePtr	=	name;
		vol.volumeParam.ioVolIndex	=	-1;
		
		if (error = PBGetVInfoSync(&vol))
			return;
		
		vRefNum	=	vol.volumeParam.ioVRefNum;
		parID		=	fsRtDirID;
		
		path = nextPath + 1;

		--*this;
	}
		
	if (error)
		return;
	
	while (*path) {
		if (*path == ':')	{
			--*this;
			++path;
			
			if (error)
				return;
			else
				continue;
		}
		
		if (nextPath = strchr(path, ':'))
			*nextPath = 0;
		
		*this += path;

		if (nextPath)
			*nextPath = ':';

		if (error)
			return;
			
		if (nextPath)
			path = nextPath + 1;
		else
			break;
	}

	if (!useAlias && hasAlias)
		Resolve();
}

/* Convert a FSSpec into a full pathname. */
char * FSp2FullPath(const FSSpec * desc)
{
	TFileSpec	spec(*desc);
	
	return spec.FullPath();
}

/* Convert a FSSpec into a relative pathname. */
char * FSp2RelPath(const FSSpec * desc)
{
	TFileSpec	spec(*desc);
	
	return spec.RelPath();
}

/* Encode a FSSpec*/
char * FSp2Encoding(const FSSpec * desc)
{
	TFileSpec	spec(*desc);
	
	return spec.Encode();
}

/* Convert a working directory & file name into a FSSpec. */
OSErr WD2FSSpec(short wd, ConstStr31Param name, FSSpec * desc)
{
	TFileSpec	spec(wd, name);
	
	*desc = spec;
	
	return spec.Error();
}

/* Convert a pathname into a file spec. */
OSErr Path2FSSpec(const char * path, FSSpec * desc)
{
	TFileSpec	spec(path);
	
	*desc = spec;
	
	return spec.Error();
}

/* Convert a working directory & file name into a FSSpec. */
OSErr Special2FSSpec(OSType object, short vol, long dirID, FSSpec * desc)
{
	TFileSpec	spec(object, vol, dirID);
	
	*desc = spec;
	
	return spec.Error();
}

/* Return FSSpec of (vRefNum, parID) */
OSErr FSpUp(FSSpec * desc)
{
	TFileSpec	spec(*desc);

	*desc = --spec;
	
	return spec.Error();
}

/* Return FSSpec of file in directory denoted by desc */
OSErr FSpDown(FSSpec * desc, ConstStr31Param name)
{
	TFileSpec	spec(*desc);

	*desc	= spec + name;
	
	return spec.Error();
}

/* Call GetCatInfo for file system object. */
OSErr	FSpCatInfo(const FSSpec * desc, CInfoPBRec * info)
{
	OSErr			err;
	TFileSpec	spec(*desc);
	
	if (err = spec.CatInfo(*info))
		return err;
	
	info->hFileInfo.ioNamePtr = (StringPtr) desc->name;
	
	return noErr;
}

/* Return FSSpec of nth file in directory denoted by (vRefNum, parID) */
OSErr FSpIndex(FSSpec * desc, short n)
{
	TFileSpec	spec(*desc);
	
	*desc = spec[n];
	
	return spec.Error();
}

static OSErr DefaultVRef(short & vRef)
{
	OSErr				err;
	ParamBlockRec	vol;
	
	vol.volumeParam.ioNamePtr = nil;
	
	if (err = PBGetVolSync(&vol))
		return err;
	
	vRef = vol.volumeParam.ioVRefNum;
	
	return noErr;
}

OSErr FSpSmartMove(const FSSpec * from, const FSSpec * to)
{
	OSErr			err;
	TFileSpec	fromspec(*from, true);
	TFileSpec	tospec(*to, true);
	TFileSpec	toparent = tospec - 1;
	TFileSpec	corner;
	CInfoPBRec	fromInfo;
	CInfoPBRec	toInfo;
	
	if (!fromspec.vRefNum)
		if (err = DefaultVRef(fromspec.vRefNum))
			return err;
			
	if (!tospec.vRefNum)
		if (err = DefaultVRef(tospec.vRefNum))
			return err;
			
	if (fromspec.vRefNum != tospec.vRefNum)
		return badMovErr;
		
	Boolean		diffname	=	!EqualString(fromspec.name, tospec.name, false, true);
	Boolean		diffdir	=	fromspec.parID != tospec.parID;
	Boolean		toexists = !tospec.CatInfo(toInfo);
	Boolean		lockfrom;
	Boolean		lockto;
	TFileSpec	tmpto;
	
	if (err = fromspec.CatInfo(fromInfo))
		return err;
	
	if (!IsFile(fromInfo) && fromspec.IsParentOf(tospec))
		return badMovErr;
				
	if (lockfrom = IsFile(fromInfo) && fromInfo.hFileInfo.ioFlAttrib & 0x01)
		HRstFLock(fromspec.vRefNum, fromspec.parID, fromspec.name);

	if (!diffname && !diffdir)	{ /* Files are identical, except possibly for case */
		err = noErr;
		
		goto cleanupcase;
	}
	
	if (toexists)
		if (!IsFile(toInfo) && toInfo.dirInfo.ioDrNmFls)
			return fBsyErr;
		else {
			tmpto = TFileSpec(kTempFileType, tospec.vRefNum, tospec.parID);
			
			if (lockto = IsFile(toInfo) && toInfo.hFileInfo.ioFlAttrib & 0x01)
				HRstFLock(tospec.vRefNum, tospec.parID, tospec.name);
				
			if (err = HRename(tospec.vRefNum, tospec.parID, tospec.name, tmpto.name))
				return err;
		}
		
	if (!diffdir) {
		err = HRename(fromspec.vRefNum, fromspec.parID, fromspec.name, tospec.name);
		
		goto cleanuptmp;
	} else if (!diffname) {
		err = 
			CatMove(
				fromspec.vRefNum, fromspec.parID, fromspec.name, 
				toparent.parID, toparent.name);
		
		goto cleanuptmp;
	}
	
	corner = TFileSpec(fromspec.vRefNum, fromspec.parID, tospec.name);
	
	if (!corner.Exists()) {
		err = HRename(fromspec.vRefNum, fromspec.parID, fromspec.name, tospec.name);
		
		if (!err)
			if (err = 
				CatMove(
					fromspec.vRefNum, fromspec.parID, tospec.name,
					toparent.parID, toparent.name)
			)
				HRename(fromspec.vRefNum, fromspec.parID, tospec.name, fromspec.name);
		
		goto cleanuptmp;
	}
		
	{
		TFileSpec	secondcorner(kTempFileType, tospec.vRefNum, tospec.parID);
		
		memcpy(corner.name, secondcorner.name, secondcorner.name[0]+1);
		
		while (corner.Exists() || secondcorner.Exists()) {
			TFileSpec newcorner(kTempFileType, secondcorner.vRefNum, secondcorner.parID);
			
			memcpy(corner.name, newcorner.name, newcorner.name[0]+1);
			memcpy(secondcorner.name, newcorner.name, newcorner.name[0]+1);
		}
	}
	
	err = HRename(fromspec.vRefNum, fromspec.parID, fromspec.name, corner.name);
	
	if (!err)
		if (err = 
			CatMove(
				fromspec.vRefNum, fromspec.parID, corner.name,
				toparent.parID, toparent.name)
		) 
			HRename(fromspec.vRefNum, fromspec.parID, corner.name, fromspec.name);
		else if (err =
			HRename(tospec.vRefNum, tospec.parID, corner.name, tospec.name)
		) {
			TFileSpec fromparent = fromspec - 1;
			
			CatMove(
				tospec.vRefNum, tospec.parID, corner.name,
				fromparent.parID, fromparent.name);
			HRename(fromspec.vRefNum, fromspec.parID, corner.name, fromspec.name);			
		}
	
cleanuptmp:
	if (toexists)
		if (err) {
			HRename(tmpto.vRefNum, tmpto.parID, tmpto.name, tospec.name);
			
			if (lockto)
				HSetFLock(tospec.vRefNum, tospec.parID, tospec.name);
		} else
			HDelete(tmpto.vRefNum, tmpto.parID, tmpto.name);

cleanupcase:
	if (!err && !diffname)
		err = HRename(tospec.vRefNum, tospec.parID, tospec.name, tospec.name);
	if (lockfrom)
		if (err)
			HSetFLock(fromspec.vRefNum, fromspec.parID, fromspec.name);
		else
			HSetFLock(tospec.vRefNum, tospec.parID, tospec.name);
		
	return err;
}

