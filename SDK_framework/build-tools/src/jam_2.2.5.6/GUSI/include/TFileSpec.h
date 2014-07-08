/*********************************************************************
Project	:	GUSI				-	Grand Unified Socket Interface
File		:	TFileSpec.h		-	C and C++ routines to deal with path names
Author	:	Matthias Neeracher
Language	:	MPW C/C++

$Log: include:TFileSpec.h,v $
Revision 1.2  1999/04/23  08:56:55  peterg
Automatic checkin:
changed attribute _comment to ''

Revision 1.4  1994/12/31  00:29:31  neeri
FSSpec encoding.

Revision 1.3  1994/08/10  00:37:29  neeri
Sanitized for universal headers.

Revision 1.2  1994/05/01  23:51:12  neeri
Added TFileSpec(const TFileSpec &) copy constructor.

Revision 1.1  1994/02/25  02:57:43  neeri
Initial revision

Revision 0.12  1993/09/27  00:00:00  neeri
FSpSmartMove(), Special constructors

Revision 0.11  1993/07/17  00:00:00  neeri
TFileSpec::LastInfo()

Revision 0.10  1993/03/14  00:00:00  neeri
1.1.0 Baseline

Revision 0.9  1993/02/06  00:00:00  neeri
DefaultDir()

Revision 0.8  1993/01/15  00:00:00  neeri
IsParentOf, operator ==

Revision 0.7  1993/01/03  00:00:00  neeri
Prevent multiple includes

Revision 0.6  1992/12/08  00:00:00  neeri
Made Default() public

Revision 0.5  1992/11/15  00:00:00  neeri
Made suitable for use from C programs

Revision 0.4  1992/11/15  00:00:00  neeri
Renamed once again

Revision 0.3  1992/11/15  00:00:00  neeri
Forgot a few consts

Revision 0.2  1992/09/12  00:00:00  neeri
Renamed

Revision 0.1  1992/09/08  00:00:00  neeri
Permission flags were the wrong way around

*********************************************************************/

#ifndef _TFILESPEC_
#define _TFILESPEC_

/* These routines run both under System 7 and under older systems. */

#include <Files.h>
#include <Folders.h>

#define kTempFileType	'TMPF'

#ifdef __cplusplus

/************************** The C++ only interface ***************************/

#if defined(powerc) || defined (__powerc)
#pragma options align=mac68k
#endif
class TFileSpec : public FSSpec {
	static OSErr		error;
	static short		curVol;
	static long			curDir;
	static CInfoPBRec	lastInfo;

	OSErr		DefaultDir();
	Boolean	IsEncodedFSSpec(const char * path, Boolean useAlias);
public:
	// Return last error
	static OSErr		Error()							{	return error;		}

	// Return last info. Valid after [], Exists
	static const CInfoPBRec * LastInfo()			{	return &lastInfo;	}
	
	// Change current directory
	static OSErr		ChDir(const TFileSpec & spec);

	// Set to current directory
	OSErr	Default();

	TFileSpec()																{}

	// Construct from TFileSpec
	TFileSpec(const TFileSpec & spec);
	
	// Construct from FSSpec
	TFileSpec(const FSSpec & spec, Boolean useAlias = false);

	// Construct from components
	TFileSpec(short vRefNum, long parID, ConstStr31Param name, Boolean useAlias = false);

	// Construct from working directory & file name
	TFileSpec(short wd, ConstStr31Param name, Boolean useAlias = false);

	// Construct from full or relative path
	TFileSpec(const char * path, Boolean useAlias = false);

	// Construct using FindFolder()	
	TFileSpec(OSType object, short vol = kOnSystemDisk, long dir = 0);
	
	// This is currently an expensive no-op, but if you call this before passing
	// the FSSpec to a FSp routine, you are guaranteed to remain compatible forever.
	void Bless();

	// Give full pathname
	char *	FullPath() const;

	// Give path relative to current directory (as defined by HSetVol())
	char *	RelPath() const;

	// Give encoded path. Encoding is:
	//
	// 1 byte:   DC1  (ASCII 0x11)
	// 4 bytes:  Volume reference number in zero-padded hex
	// 8 bytes:  Directory ID in zero-padded hex
	// n bytes:  Partial pathname, starting with ':'
	//
	// Needless to say, passing encoded values to anything but a GUSI routine is 
	// a bad idea.
	char *	Encode() const;
	
	// Give information about the current object. If dirInfo is true, give information
	//   about the current object's directory.
	OSErr		CatInfo(CInfoPBRec & info, Boolean dirInfo = false) const;

	// If object is an alias file, resolve it. If gently is true, nonexisting files
	//   are tolerated.
	OSErr		Resolve(Boolean gently = true);

	// Resolve an existing object for which we already have a CInfoPBRec
	OSErr		Resolve(const CInfoPBRec & info);

	// true if object exists
	Boolean	Exists() const;

	// true if object is a parent directory of other
	Boolean	IsParentOf(const TFileSpec & other) const;

	// Replace object with its parent directory
	TFileSpec 	operator--();

	// Equivalent to calling -- levels times
	TFileSpec 	operator-=(int levels);

	// Equivalent to calling -= on a *copy* of the current object
	TFileSpec	operator-(int levels) const;

	// Replace directory object by object with given name inside the directory
	TFileSpec 	operator+=(ConstStr31Param name);
	TFileSpec	operator+=(const char * name);

	// Non-destructive version of +=
	TFileSpec	operator+(ConstStr31Param name) const;
	TFileSpec	operator+(const char * name) const;

	// Return the index-th object in the *parent* directory of the current object
	TFileSpec 	operator[](short index) const;

	// Return if the two filespecs (not) are equal
	Boolean		operator==(const TFileSpec & other) const;
	Boolean		operator!=(const TFileSpec & other) const;
};
#if defined(powerc) || defined(__powerc)
#pragma options align=reset
#endif

inline Boolean IsFile(const CInfoPBRec & info)
{
	return !(info.dirInfo.ioFlAttrib & 0x10);
}

inline Boolean IsAlias(const CInfoPBRec & info)
{
	return
		!(info.hFileInfo.ioFlAttrib & 0x10) &&
		(info.hFileInfo.ioFlFndrInfo.fdFlags & (1 << 15));
}

inline Boolean DirIsExported(const CInfoPBRec & info)
{
	return (info.hFileInfo.ioFlAttrib & 0x20);
}

inline Boolean DirIsMounted(const CInfoPBRec & info)
{
	return (info.hFileInfo.ioFlAttrib & 0x08);
}

inline Boolean DirIsShared(const CInfoPBRec & info)
{
	return (info.hFileInfo.ioFlAttrib & 0x04);
}

inline Boolean HasRdPerm(const CInfoPBRec & info)
{
#ifdef OnceThisFieldIsDefined
	return !(info.dirInfo.ioACUser & 0x02);
#else
	return !(info.dirInfo.filler2 & 0x02);
#endif
}

inline Boolean HasWrPerm(const CInfoPBRec & info)
{
#ifdef OnceThisFieldIsDefined
	return !(info.dirInfo.ioACUser & 0x04);
#else
	return !(info.dirInfo.filler2 & 0x04);
#endif
}

extern "C" {
#endif

/* Routines shared between C and C++ */

/* Convert a working directory & file name into a FSSpec. */
OSErr WD2FSSpec(short wd, ConstStr31Param name, FSSpec * desc);

/* Convert a FSSpec into a full pathname. */
char * FSp2FullPath(const FSSpec * desc);

/* Works like FSp2FullPath, but creates a *relative* pathname if the object
   is contained in the current directory.
*/
char * FSp2RelPath(const FSSpec * desc);

/* Encode FSSpec as described above for TFileSpec::Encode */
char * FSp2Encoding(const FSSpec * desc);

/* Call GetCatInfo for file system object. */
OSErr	FSpCatInfo(const FSSpec * desc, CInfoPBRec * info);

/* Return FSSpec of (vRefNum, parID) */
OSErr FSpUp(FSSpec * desc);

/* Return FSSpec of file in directory denoted by desc */
OSErr FSpDown(FSSpec * desc, ConstStr31Param name);

/* Return FSSpec of nth file in directory denoted by (vRefNum, parID) */
OSErr FSpIndex(FSSpec * desc, short n);

/* Convert a pathname into a file spec. */
OSErr Path2FSSpec(const char * path, FSSpec * desc);

/* Convert a special object into a file spec. */
OSErr Special2FSSpec(OSType object, short vol, long dirID, FSSpec * desc);

/* Move & Rename File */
OSErr FSpSmartMove(const FSSpec * from, const FSSpec * to);

#ifdef __cplusplus
}
#endif
#endif
