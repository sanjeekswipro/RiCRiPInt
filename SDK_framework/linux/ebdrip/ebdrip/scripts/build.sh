#!/bin/sh
#
# Script to build Harlequin Embedded SDK (HES)
#
# This script runs the make file used to build the HES.
#
#


bldtype=""
bldtgts=""
bldplat=""
bldfont=""
bldopts=""


# --------------  Local functions  ----------------

# Invoke the actual build (or clean) command
dobuild() {
  # get params
  local type="$1"
  local syst="$2"
  local tgts="$3"
  local vars="$4"
  local dstn="$5"
  local bdir=`dirname $0`
  
  # only copy built files if we know where to
  bldcopyto=""
  if [ "$dstn" != "" ]; then
    bldcopyto="-va copyto=$dstn"
  fi
  
  # move to build directory
  oldcd=`pwd`
  cd $bdir

  # check toolmap file is available
  if [ -e toolmap.sh ]; then
    . ./toolmap.sh
  elif [ -e ../toolmap.sh ]; then
    . ../toolmap.sh
  else
    done_message="Cannot find the toolmap file needed for this build"
    cd $oldcd
    return
  fi

  # invoke make via set script file
  bt/hqmake $type $syst -va thirdparty -va customised=EBDEVAL -va security=le -va dll=nd -va rt_libs=multi_threaded $vars $bldcopyto $tgts

  # delete any unwanted temporary files
  rm -f *dox*.warn

  cd $oldcd
}


# Display help text
showhelp() {
  echo "***"
  echo 
  echo   build.sh type_of_build what_to_build -t target_system -f font_library build_options
  echo 
  echo   -  type_of_build  indicates either how this build should be compiled and built
  echo   -                 one of:       release, rel, r,  debug, dbg, d,  asserted, assert, a
  echo   -                 defaults to:  debug
  echo   -
  echo   -  what_to_build  this does not need to be specified for normal incremental builds
  echo   -                 defaults to:  depend ebdrip
  echo   -                 if you wish to remove any existing built files from the build directories then
  echo   -                 set this to:  clean        to remove build files for the specified build type
  echo   -                          or:  clean all    to remove build files for all build types
  echo   -
  echo   -  target_system  the operating system and hardware on which the built HES will run
  echo   -                 for example:  linux-pentium
  echo   -                 no default value
  echo   -
  echo   -  font_library   the font library to be included in the build
  echo   -                 one of:       none, ufst5, ufst7
  echo   -                 defaults to:  ufst5
  echo   -
  echo   -  build_options  additional build configuration settings of the form  'va name=value'
  echo   -                 for example:  -va pdls=all
  echo 
  echo "***"
}


# Get command line options
parsecmdline() {
  local first=true
  local opttype=
  
  for cmdopt ; do
    if   [ $cmdopt = -t -o $cmdopt = -f -o $cmdopt = -va ]; then
      opttype=$cmdopt
    elif [ ! $opttype ]; then
      if   [ $first ]; then
        bldtype=$cmdopt
        first=
      elif [ $bldtgts ]; then
        bldtgts="$bldtgts $cmdopt"
      else
        bldtgts=$cmdopt
      fi
    elif [ $opttype = -t ]; then
      bldplat=$cmdopt
      opttype=none
    elif [ $opttype = -f ]; then
      bldfont=$cmdopt
      opttype=none
    elif [ $opttype = -va ]; then
      bldopts="$bldopts -va $cmdopt"
      opttype=none
    else
      echo unknown option on command line: $cmdopt
    fi
  done
}


# --------------  Local functions (end) ------------


# Define alternate sets of build flags
rls_setting="+r -a -d -g"
dbg_setting="-r -a +d"
ast_setting="-r +a +d"
tim_setting="-r -a +d +t -n"


# Display help text if nothing on command line
if [ ! $1 ]; then 
  showhelp
  exit
fi

# Parse command line
parsecmdline $*

# Identify build type & select matching build flags
# ( or display help text if asked )
if   [ $bldtype = release -o $bldtype = rel -o $bldtype = r ]; then
  build_flags="$rls_setting"
  bldtype=release
elif [ $bldtype = debug -o $bldtype = dbg -o $bldtype = d ]; then
  build_flags="$dbg_setting"
  bldtype=debug
elif [ $bldtype = asserted -o $bldtype = assert -o $bldtype = a ]; then
  build_flags="$ast_setting"
  bldtype=asserted
elif [ $bldtype = timing -o $bldtype = timed -o $bldtype = tim ]; then
  build_flags="$tim_setting"
  bldtype=timing
elif [ $bldtype = help -o $bldtype = h ]; then
  showhelp
  exit
fi

# Assign default build type if not set on command line
if [ "$build_flags" != "" ]; then
  build_targets="$bldtgts"
else
  build_targets="$bldtype $bldtgts"
  build_flags="$dbg_setting"
  bldtype=debug
fi

# Assign default build targets if none set on command line
if [ "$build_targets" = "" ]; then
  build_targets="depend ebdrip"
fi

# Set up target system parameter if required
if [ $bldplat ]; then
  target_system="-target $bldplat"
fi

# Set up font library parameter
if   [ "$bldfont" = "none" ]; then
  font_library="-va ebd_ufst=eufstn"
elif [ "$bldfont" = "ufst7" ]; then
  font_library="-va ebd_ufst=eufst7y"
else
  font_library="-va ebd_ufst=eufst5y"
fi

bldopts="$font_library $bldopts"


# Do the clean or build operation
done_message=Done

if [ "$build_targets" = "clean all" ]; then
  # - clean for all build types
  dobuild "$rls_setting" "$target_system" clean "$bldopts"
  dobuild "$dbg_setting" "$target_system" clean "$bldopts"
  dobuild "$ast_setting" "$target_system" clean "$bldopts"
  dobuild "$tim_setting" "$target_system" clean "$bldopts"
elif [ "$build_targets" = "clean" ]; then
  # - clean for the specified build type
  dobuild "$build_flags" "$target_system" clean "$bldopts"
else
  done_message=Built
  # - build the HES
  dobuild "$build_flags" "$target_system" "$build_targets" "$bldopts" ${bldplat}-$bldtype
fi


echo $done_message


