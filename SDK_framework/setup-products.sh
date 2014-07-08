#!/bin/sh
#
# This setup file has to be run before building the Harlequin Embedded SDK
#
# It takes one command line option that controls whether existing files are overwritten
#	replace	  - any existing files are overwritten
#	noreplace - no  existing files are overwritten    [ default ]
#


if [ `uname` = NetBSD ]; then
  bldhost=netbsd
  dflttgt=netbsd-386
else
  bldhost=linux
  dflttgt=linux-pentium
fi


# Process command line option to set overwrite-files option
overwrite=-n
if [ $1 ]; then
  if [   $1 = replace ]; then
    overwrite=-o
  elif [ $1 != noreplace ]; then
    # invalid option, display a one-line hint
    echo setup-products.sh  [ replace | noreplace ]
    exit
  fi
fi


# Local folder names, must match those on delivery medium
topdir=`pwd`
tooldir=$topdir/build-tools

# Folder holding the product source code archive(s)
proddir=$topdir/product-sources
if [ ! -e $proddir ]; then
  echo Must run this script file in delivery folder
  exit
fi

# Target folder for builds
blddir=$topdir/$bldhost
if [ ! -e $blddir ]; then
  mkdir -p $blddir
  overwrite=-n
fi

# Location of 'internal' build tools 
cvdir=$blddir/CV_variables
mkdir -p $cvdir


# --------------  Local functions  ----------------

# Unpack a product component
# - param : name of archive containing component
unpack() {

  # Extract contents into build directory
  # - use overwrite flag to determine whether to replace any existing files
  unzip -d $blddir -q $overwrite $1

  # Get product name for this component
  local prodname=`basename $1 .zip`
  prodname=`expr $prodname : '\(.*\)-.*'`
  if [ "$prodname" = "EBD$bldhost" ]; then 
    prodname=ebdrip

    # cd to unzipped build directory
    oldcd=`pwd`
    cd $blddir/$prodname

    # Remove any unwanted files
    rm -f buildvar*.txt
    rm -f *dox*.warn
    
    # Make a local copy of the common build script if we dont have one already
    # (never overwrite an existing local copy as it may have been modified)
    if [ ! -e build.sh -a -e $prodname/scripts/build.sh ]; then
      cp -p $prodname/scripts/build.sh ./
    fi

    # Generate a default build script
    newscript=$blddir/$prodname/default-build.sh
    echo '#!/bin/sh'>$newscript
    echo # Default build script for Harlequin Embedded SDK >>$newscript
    echo $blddir/$prodname/build.sh release depend ebdrip -t $dflttgt -va pdls=all -va minimal=1 -va corefeatures=none -va ICUbuiltin=1 -va cpp_compiler=gcc_4_5_3 -va ebd_pdfout=epdfouty -va rebuild=sw >>$newscript
    chmod +x $newscript

    cd $oldcd
  fi
}


# Append an environment definition ("Compile Variable") to toolmap file
# - params : CV name, CV target directory (e.g. builddir/CV_variables/CV_JAM_2_2_5_6)
dotoolmap() {

  # Set CV name & destination
  cvname=$2
  cvloc=$1/$2
  toolname=`expr $cvname : 'CV_\([[:alpha:]]*\)_.*'`
  toolvsn=`expr $cvname : 'CV_[[:alpha:]]*_\(.*\)'`
  if [ $toolname ]; then
    # CV name gives the target tool name & version
    lctoolname=$( echo "$toolname" | tr -s  '[:upper:]'  '[:lower:]' )
    tdotvsn=$( echo "$toolvsn" | tr '_' '.' )
    # some tool folder names include the version in dotted notation

    # Construct definition for toolmap file
    tline="$cvname=$cvloc ; export $cvname"
    if [ "$toolname" = "DOXYGEN" ]; then  tline="$cvname=$cvloc/${lctoolname}_$tdotvsn ; export $cvname"; fi
    if [ "$toolname" = "JAM" ]; then      tline="$cvname=$cvloc/${lctoolname}_$toolvsn ; export $cvname"; fi
    if [ "$toolname" = "SIGNTOOL" ]; then tline="$cvname=$cvloc/$lctoolname ; export $cvname"; fi
    if [ "$toolname" = "ZIP" ]; then      tline="$cvname=$cvloc/bin ; export $cvname"; fi

    # Append definition to toolmap file
    echo $tline>>$blddir/toolmap.sh
  fi
}

# --------------  Local functions (end) ------------


# Get list of RIP product components
find $proddir/ -name "*$bldhost-source*zip" >$blddir/prodlist.tmp 

# Unzip each component archive in turn
while read zipfname ; do
    unpack $zipfname
done < $blddir/prodlist.tmp

# Extra stuff for any product documentation
docdir=$blddir/doc
if [ -e $blddir/pdf ]; then
  mkdir -p $docdir
  mv $blddir/pdf $docdir/ 
fi
if [ -e $blddir/src ]; then
  mkdir -p $docdir
  mv $blddir/src $docdir/ 
fi

# Create toolmap file based on set of CV variable folders
echo '#!/bin/sh'>$blddir/toolmap.sh
echo '# Define environment variables used by Harlequin Embedded SDK make files'>>$blddir/toolmap.sh
find $cvdir/ -maxdepth 1 -type d -name 'CV_[A-Z]*' >$blddir/cvlist.tmp 
while read cvname ; do
    cvname=`basename $cvname`
    dotoolmap $cvdir $cvname
done < $blddir/cvlist.tmp
chmod +x $blddir/toolmap.sh

