#!/bin/sh

#
# CVS Id: $Id: rename_symb.sh,v 1.11 2002/09/21 18:55:04 lacour Exp $
#
# This script is intended to collect all the global symbols in the
# MPICH library for the Globus2 device.  Once all the global symbols
# are found, this script generates an output file containing
# "#define"s to rename the global symbols.  The global symbols found
# in the library are *appended* to those already renamed in the
# current macro def file (this is necessary due to ROMIO: depending on
# the file system available on the system, the global symbols will be
# different).  This macro def file is placed in the Globus2 device
# directory.  This script should be executed before each release of
# MPICH.
#
# This script works with Globus-v2.x and Globus-v1.1.x.  Globus-v2.x
# is preferred.
#


######################################################################
### Script configuration:

TRAPPED_SIGNALS="INT QUIT USR1 USR2 TERM"
# time stamp:
DATE="`date`"
DATE_STRING="`echo $DATE | tr ' ' '_'`"
# temporary files:
TMP_RAW_GLOBALS="/tmp/tmp_raw_global_symbols_$$_$DATE_STRING"
TMP_FILTERED_GLOBALS="/tmp/tmp_filter_global_symbols_$$_$DATE_STRING"
TMP_OUTPUT="/tmp/tmp_global_c_symb.h_$$_$DATE_STRING"
EXCLUDED_SYMB="/tmp/tmp_excluded_symb_$$_$DATE_STRING"
TRAP_FILE="/tmp/tmp_trap_file_$$_$DATE_STRING"
# log files:
file_distclean="make.distclean.log.$$"
file_configure="configure.log.$$"
file_make="make.log.$$"
# CONFIGURE_OPTIONS="--disable-f77 --disable-f90 --disable-f90modules --without-mpe --without-romio --disable-cxx"
# We need to build ROMIO because it adds global symbols.
# We need to build Fortran77 because it adds global symbols to libmpichg2.a
CONFIGURE_OPTIONS="--disable-f90 --disable-f90modules --without-mpe --disable-cxx"
EXTRA_CONFIG_OPT=""
DEVICE_OPTION="--with-device=globus2:-flavor="
LIBS="lib/libmpichg2.a lib/libpmpichg2.a"
# We must remove all the global symbols from mpid/globus2/pr_mpi_g.o
# because pr_mpi_g.c does NOT read the macro def file:
EXCLUDE_LIB="mpid/globus2/pr_mpi_g.o"
# output file relative to the ROOT_DIR directory
DEFAULT_OUTPUT="mpid/globus2/global_c_symb.h"
OUTPUT="/dev/null"
FORMER_OUTPUT="/dev/null"
FLAVOR=""
# minimum number of global symbols the current macro def file sould
# already contain:
MIN_GLOB_SYMB=1000


### Default settings:
GLOBUS2_FLAVOR="vendorcc32mpi"
GLOBUS1_FLAVOR="mpi,nothreads"
ROOT_DIR="`dirname $0`/../.."
ROOT_DIR="`( cd $ROOT_DIR ; pwd )`"
PREFIX="_mpich_g2_"
ALLOW_NON_MPI="no"
VERBOSE="no"
KEEP_LOG="no"
FORCE_INCOMPLETE="no"


######################################################################
### Print the actions executed when in verbose mode

Show ()
{
   if [ "$VERBOSE" = "yes" ]; then
      echo "# $*"
   fi
}


######################################################################
### Clean up the temporary files and restore the macro def file if
### necessary

clean_exit ()
{
   Show "--> clean_exit()"
   Show "   restoring the original macro def file if relevant"
   cp -f $FORMER_OUTPUT $OUTPUT > /dev/null 2>&1
   Show "   deleting the temporary files"
   rm -f $TMP_RAW_GLOBALS $TMP_FILTERED_GLOBALS $TRAP_FILE $TMP_OUTPUT $EXCLUDED_SYMB > /dev/null 2>&1
   if [ "$#" -eq 0 ]; then
      ec=0
   else
      ec="$1"
   fi
   if [ "$ec" -eq 0  -a  "$KEEP_LOG" = "no" ]; then
      Show "   deleting the log files"
      rm -f $file_distclean $file_make $file_configure > /dev/null 2>&1
   fi
   Show "   exiting with error code $ec"
   exit $ec
}


######################################################################
### sort a list of words (args # 3, 4, 5, ...) according to the
### pattern given as argument # 2.  This pattern goes first if the
### 1st argument is '+', or last of 1st arg. is '-'

order_list ()
{
   position="$1"
   shift
   pattern="$1"
   shift
   present=""
   absent=""
   for word in $*; do
      present="$present `echo $word | grep $pattern`"
      absent="$absent `echo $word | grep -v $pattern`"
   done
   # final result is echo'ed with no quote to squeeze redundant spaces
   if [ "$position" = "+" ]; then
      echo $present $absent
   else
      echo $absent $present
   fi
}


######################################################################
### Return the list of Globus-v2.x available flavors sorted in
### decreasing order of preference

globus2_flavors ()
{
   Show "   Searching for the available Globus-v2.x flavors"
   if [ ! -d "$GLOBUS_LOCATION/etc/globus_core" ]; then
      return 0
   fi
   __list="`/bin/ls $GLOBUS_LOCATION/etc/globus_core/flavor_*.gpt`"
   if [ -n "$__list" ]; then
      __new_list=""
      for __fl in $__list; do
         __new_list="$__new_list `basename $__fl | sed -e 's/^flavor_//g' -e 's/\\.gpt$//g'`"
      done
      __list="`echo $__new_list | tr ' ' '\n' | sort -u | tr '\n' ' '`"
      Show "   Sorting for the Globus-v2.x flavors"
      # have non debug flavors first
      __list="`order_list - dbg $__list`"
      # have vendorcc first
      __list="`order_list + vendorcc $__list`"
      # have non threaded flavors first
      __list="`order_list - thr $__list`"
      # have MPI flavors first (most important)
      __list="`order_list + mpi $__list`"
      echo "$__list"
   fi
}


######################################################################
### Return the list of Globus-v1.1.x available flavors sorted in
### decreasing order of preference

globus1_flavors ()
{
   Show "   Searching for the available Globus-v1.1.x flavors"
   if [ ! -d "$GLOBUS_INSTALL_PATH/development" ]; then
      return 0
   fi
   __list="`/bin/ls $GLOBUS_INSTALL_PATH/development | cut -d_ -f2-`"
   if [ -n "$__list" ]; then
      __new_list=`echo "$__list" | sort -u | tr '_\n' ', '`
      Show "   Sorting for the Globus-v1.1.x flavors"
      # have non debug flavors first
      __list="`order_list - debug $__new_list`"
      # have vendorcc first
      __list="`order_list - gnu $__list`"
      # have non threaded flavors first
      __list="`order_list + nothreads $__list`"
      # have MPI flavors first (most important)
      __list="`order_list + mpi $__list`"
      echo "$__list"
   fi
}


######################################################################
### Display usage message and exit

usage ()
{
   Show "--> usage()"
   if [ "$#" -ne 0 ]; then
      echo $*
      echo "---"
      echo "Use option '-h' or '-help' to get help."
   else
      set -- "[-root <dir>]" "[-flavor <flavor>]" "[-prefix <prefix>]" \
             "[-nonmpi]" "[-h | -help]" "[-v]" "[-keeplog]" \
             "[-config <extra configure options>]" "[-force]"
      prgrm="Usage: `basename $0`"
      prgrm_white="`echo \"$prgrm\" | sed -e 's/./ /g'`"
      line="$prgrm"
      init_column=`echo \"$prgrm\" | wc -c`
      column="$init_column"
      # this value could be taken from "stty -a"...
      term_column=80
      while [ "$#" -gt 0 ]; do
         opt=" $1"
         shift
         length=`echo "$opt" | wc -c`
         column="`expr $column + $length`"
         if [ "$column" -ge "$term_column" ]; then
            echo "$line"
            line="$prgrm_white"
            column="`expr $init_column + $length`"
         fi
         line="${line}${opt}"
      done
      echo "$line"
cat << EOF
 -root <dir>: MPICH root directory
              (default is $ROOT_DIR)
 -flavor <flavor>: Globus flavor to configure MPICH-G2
                   (Globus-v1.1.x default="$GLOBUS1_FLAVOR")
                   (Globus-v2.x default="$GLOBUS2_FLAVOR")
 -prefix <prefix>: prefix used to rename the global symbols
                   (default="$PREFIX")
 -nonmpi: allow NON-MPI flavor (default is to force MPI flavor)
 -h | -help: print this message
 -v: uselessly verbose mode (default is relatively silent)
 -keeplog: keep the log files (default is to remove them)
 -config <opt>: use <opt> as extra configure options (default is empty)
 -force: force the script to run using an incomplete macro def file
         (by default, the current macro def file should already rename at least
          $MIN_GLOB_SYMB global symbols)
EOF
   fi
   echo "Aborting."
   clean_exit 1
}


######################################################################
### Parse the command line options

parse_command_line ()
{
   while [ "$#" -gt 0 ]; do
      opt="$1"
      shift
      Show "analysing option \"$opt\""
      case "$opt" in
         "-root")
            if [ "$#" -eq 0 ]; then
               usage "Missing argument of option \"$opt\""
            else
               ROOT_DIR="$1"
               shift
            fi ;;
         "-flavor")
            if [ "$#" -eq 0 ]; then
               usage "Missing argument of option \"$opt\""
            else
               FLAVOR="$1"
               shift
            fi ;;
         "-prefix")
            if [ "$#" -eq 0 ]; then
               usage "Missing argument of option \"$opt\""
            else
               PREFIX="$1"
               shift
            fi ;;
         "-keeplog") KEEP_LOG="yes" ;;
         "-config")
            if [ "$#" -eq 0 ]; then
               usage "Missing argument of option \"$opt\""
            else
               EXTRA_CONFIG_OPT="$1"
               shift
            fi ;;
         "-nonmpi") ALLOW_NON_MPI="yes" ;;
         "-force") FORCE_INCOMPLETE="yes" ;;
         "-v") VERBOSE="yes" ;;
         "-h"|"-help") usage ;;
         *) usage "Unknown option \"$opt\"" ;;
      esac
   done
}


######################################################################
### Check the settings are correct (and change to the MPICH root
### directory)

check_settings ()
{
   Show "--> check_settings()"
   # check the MPICH root directory exists
   if [ ! -d "$ROOT_DIR" ]; then
      echo "Directory \"$ROOT_DIR\" not found."
      echo "Aborting."
      clean_exit 2
   fi
   Show "   moving to directory $ROOT_DIR"
   cd $ROOT_DIR

   # check this directory contains a 'configure' script and an
   # 'mpid/globus2' directory
   Show "   checking if \"$ROOT_DIR\" is an MPICH root directory"
   if [ \( ! -x "configure" \)  -o  \( ! -d "mpid/globus2" \) ]; then
      echo "Make sure \"$ROOT_DIR\" is an MPICH root directory."
      echo "Aborting."
      clean_exit 3
   fi

   # Are we using Globus-v1.1.x or Globus-v2.x?
   Show "   are we using Globus-v1.1.x or Globus-v2.x?"
   if [ -n "$GLOBUS_LOCATION"  -a  -n "$GLOBUS_INSTALL_PATH" ]; then
      echo "Both variables 'GLOBUS_LOCATION' and 'GLOBUS_INSTALL_PATH' are defined."
      echo "Prefering Globus-v2.x, using 'GLOBUS_LOCATION'."
      GLOBUS_INSTALL_PATH=''
   fi
   GLOBUS_VERSION=0
   if [ -n "$GLOBUS_LOCATION" ]; then
      GLOBUS_VERSION=2
      if [ -z "$FLAVOR" ]; then
         FLAVOR="$GLOBUS2_FLAVOR"
      fi
   fi
   if [ -n "$GLOBUS_INSTALL_PATH" ]; then
      GLOBUS_VERSION=1
      if [ -z "$FLAVOR" ]; then
         FLAVOR="$GLOBUS1_FLAVOR"
      fi
   fi
   if [ "$GLOBUS_VERSION" -eq 0 ]; then
      echo "If you want to use Globus-v2.x (preferred), define 'GLOBUS_LOCATION'."
      echo "If you want to use Globus-v1.1.x, define 'GLOBUS_INSTALL_PATH'."
      echo "Aborting."
      clean_exit 15
   fi
   echo "Using Globus-v${GLOBUS_VERSION}.x"

   case "$GLOBUS_VERSION" in

      "1")
         # Check GLOBUS_INSTALL_PATH points to an existing
         # Globus-v1.1.x directory.  Also check the FLAVOR exists.
         Show "   checking directory \"$GLOBUS_INSTALL_PATH\" exists"
         if [ ! -d "$GLOBUS_INSTALL_PATH" ]; then
            echo "Directory \"$GLOBUS_INSTALL_PATH\" not found."
            echo "Check your GLOBUS_INSTALL_PATH environment variable."
            echo "Aborting."
            clean_exit 4
         fi
         Show "   checking directory \"$GLOBUS_INSTALL_PATH/development\" exists"
         if [ ! -d "$GLOBUS_INSTALL_PATH/development" ]; then
            echo "Directory \"$GLOBUS_INSTALL_PATH/development\" not found."
            echo "Check your installation of Globus-v1.x."
            echo "Aborting."
            clean_exit 16
         fi
         Show "   checking flavor \"$FLAVOR\" exists"
         fl_list="`globus1_flavors`"
         if [ -z "$fl_list" ]; then
            echo "No flavors found in $GLOBUS_INSTALL_PATH/development."
            echo "Aborting."
            clean_exit 19
         else
            for my_fl_elt in `echo $FLAVOR | tr ',' ' '`; do
               new_fl_list=""
               for flvr in $fl_list; do
                  for fl_elt in `echo $flvr | tr ',' ' '`; do
                     if [ "$fl_elt" = "$my_fl_elt" ]; then
                        new_fl_list="$new_fl_list $flvr"
                     fi
                  done
               done
               fl_list="$new_fl_list"
            done
            if [ -z "$fl_list" ]; then
               echo "Flavor \"$FLAVOR\" not found."
               echo "Use option \"-flavor\" to choose <flavor> amongst: `globus1_flavors`"
               echo "Aborting."
               clean_exit 17
            fi
         fi ;;

      "2")
         # Check GLOBUS_LOCATION points to an existing Globus-v2.x
         # directory.  Also check the FLAVOR exists.
         Show "   checking directory \"$GLOBUS_LOCATION\" exists"
         if [ ! -d "$GLOBUS_LOCATION" ]; then
            echo "Directory \"$GLOBUS_LOCATION\" not found."
            echo "Check your GLOBUS_LOCATION environment variable."
            echo "Aborting."
            clean_exit 5
         fi
         Show "   checking directory \"$GLOBUS_LOCATION/etc/globus_core\" exists"
         if [ ! -d "$GLOBUS_LOCATION/etc/globus_core" ]; then
            echo "Directory \"$GLOBUS_LOCATION/etc/globus_core\" not found."
            echo "Check your installation of Globus-v2.x."
            echo "Aborting."
            clean_exit 6
         fi
         Show "   checking flavor \"$FLAVOR\" exists"
         if [ ! -r "$GLOBUS_LOCATION/etc/globus_core/flavor_${FLAVOR}.gpt" ]; then
            echo "Flavor \"$FLAVOR\" not found."
            list="`globus2_flavors`"
            if [ -n "$list" ]; then
               echo "Use option \"-flavor\" to choose <flavor> amongst: $list"
            else
               echo "No flavors found in $GLOBUS_INSTALL_PATH/etc/globus_core."
               echo "Aborting."
               clean_exit 20
            fi
            echo "Aborting."
            clean_exit 10
         fi ;;

   esac

   # Check the flavor is an MPI flavor, unless NON-MPI flavor is allowed
   Show "   checking MPI or NON-MPI flavor"
   if [ -z "`echo $FLAVOR | grep mpi`"  -a   "$ALLOW_NON_MPI" = "no" ]; then
      cat << EOF
Flavor "$FLAVOR" does not look like an MPI flavor.
Use option "-nonmpi" to bypass this test.
An MPI flavor should be used to find the global symbols because the
output macro def file (with "#define"'s) is read only when MPICH-G2 is
build on top of a vendor MPI.
Aborting.
EOF
      clean_exit 12
   fi
   Show "check_settings() -->"
}


######################################################################
### Notify an error and exit (1st arg: command which failed, 2nd arg:
### optional log file).

notify_error ()
{
   Show "--> notify_error()"
   echo "######################################################################"
   echo "Working directory:"
   pwd
   echo "Error while running:"
   echo $1
   if [ "$2" ]; then
      echo "For details, see file:"
      echo "$2"
   fi
   clean_exit 7
}


######################################################################
### create the trap file

make_trap_file ()
{
   Show "--> make_trap_file()"
   echo "echo \"Clean ending...\"" > $TRAP_FILE
   echo "clean_exit" >> $TRAP_FILE
   Show "make_trap_file() -->"
}


######################################################################
### Main program:

# set the default flavors in function of the flavors available in
# GLOBUS_LOCATION and GLOBUS_INSTALL_PATH:
list="`globus1_flavors`"
if [ -n "$list" ]; then
   GLOBUS1_FLAVOR="`echo \"$list\" | awk '{print $1}'`"
fi
list="`globus2_flavors`"
if [ -n "$list" ]; then
   GLOBUS2_FLAVOR="`echo \"$list\" | awk '{print $1}'`"
fi

# parse the command line arguments:
parse_command_line "$@"

# check the settings are correct (and change to the MPICH root
# directory):
check_settings

# make the trap file in case a signal is caught:
make_trap_file
# Bug in shell on SGI/IRIX Lemon: this trap has no effect if it's
# registered in the function make_trap_file().
Show "registering the trap for signals: $TRAPPED_SIGNALS"
trap ". $TRAP_FILE" $TRAPPED_SIGNALS

# find the global symbols already renamed in the current macro def
# file:

if [ -f "$DEFAULT_OUTPUT" ]; then
   FORMER_OUTPUT="${DEFAULT_OUTPUT}.old.$DATE_STRING"
   # the following trick is to avoid losing the original macro
   # definition file in case a signal is caught...
   Show "saving the former macro def file \"$DEFAULT_OUTPUT\" as \"$FORMER_OUTPUT\""
   cp -f $DEFAULT_OUTPUT $FORMER_OUTPUT
fi
OUTPUT="$DEFAULT_OUTPUT"
Show "deleting the former macro def file \"$OUTPUT\""
rm -f $OUTPUT
Show "creating an empty macro def file \"$OUTPUT\""
# touch $OUTPUT
# have to include the global_fort_symb.h because the Makefile of Globus2
# rename those symbols with sed:
echo '#include "global_fort_symb.h"' > $OUTPUT

Show "collecting the global symbols already listed in the former macro def file \"$FORMER_OUTPUT\""
egrep '^#define [^ ]* [^ ]*' $FORMER_OUTPUT | awk '{print $2}' | sort -u > $TMP_RAW_GLOBALS

Show "counting the number of global symbols found in the former macro def file \"$FORMER_OUTPUT\""
former_number="`wc -l $TMP_RAW_GLOBALS | awk '{print $1}'`"
if [ "$FORCE_INCOMPLETE" = "no"  -a  "$former_number" -le "$MIN_GLOB_SYMB" ]; then
   cat << EOF
######################################################################
WARNING: it looks like you currently have an incomplete macro def file
"$OUTPUT"
You may have to add *by hand* some ROMIO symbols (named in function of
the File Systems available).

Use option "-force" to force the script to run on this incomplete
macro def file.
EOF
   clean_exit 18
fi

# build the MPICH-G2 library (C bindings only) without symbol
# renaming:

echo "Cleaning up the MPICH directory tree:"
cmd="make distclean"
echo "   $cmd > $file_distclean"
$cmd > $file_distclean 2>&1

echo "Configuring MPICH-G2:"
cmd="./configure $CONFIGURE_OPTIONS ${DEVICE_OPTION}${FLAVOR} $EXTRA_CONFIG_OPT"
echo "   $cmd > $file_configure"
$cmd > $file_configure 2>&1  ||  notify_error "$cmd" $file_configure

echo "Building MPICH-G2:"
cmd="make"
echo "   $cmd > $file_make"
$cmd > $file_make 2>&1  ||  notify_error "$cmd" $file_make

Show "checking the required C libraries are present"
for file in $EXCLUDE_LIB; do
   cmd="cd `dirname $file` ; make `basename $file`"
   ( eval $cmd ) >> $file_make 2>&1 || notify_error "$cmd" $file_make
done
for file in $LIBS $EXCLUDE_LIB; do
   if [ ! -r "$file" ]; then
      echo "Could not find $file"
      echo "Aborting."
      clean_exit 8
   fi
done

# this filter finds the global symbols (nm -g) in the C libs of
# MPICH-G2 (both regular and profiling).  Then it removes the global
# symbols which are used / undefined ("U" in nm's output): that's to
# keep only the global symbols MPICH-G2 defines (for instance,
# MPICH-G2 may use the global symbol "malloc"... but does not define
# it... and we don't want to rename this symbol!).  Now the filter
# grabs the symbols themselves, removing all the useless characters in
# nm's output, sort the symbols, removes the multiple copies, and
# produces the "#define" lines.  The symbols are renamed using the
# prefix $PREFIX.

Show "checking the output format of command 'nm'"
_XPG="1"   # needed for IRIX (SGI)
export _XPG
format="`nm -P -g $LIBS | grep '|' | head -n 1`"
if [ -n "$format" ]; then
   echo "'nm -P' does not output the POSIX portable format."
   echo "Aborting."
   clean_exit 9
fi

Show "grepping the global symbols from the C libraries $LIBS"
nm -P -g $LIBS | egrep -v ':$' | awk '(NF > 2) && ($2 != "U") {print $1}' >> $TMP_RAW_GLOBALS

Show "grepping the global symbols from the excluded C object files $EXCLUDE_LIB"
# the following is a dummy string which may never be a global symbol,
# just to make sure the EXCLUDED_SYMB file is NOT empty (some 'weak'
# versions of egrep fail when option '-f' has an empty file)
echo "this_is_a_dummy_string_to_make_sure_this_file_is_not_empty" > $EXCLUDED_SYMB
nm -P -g $EXCLUDE_LIB | egrep -v ':$' | awk '(NF > 2) && ($2 != "U") { print $1 }' >> $EXCLUDED_SYMB

Show "filtering the global symbols (sort, uniq, exclude, ...)"
# The Fortran global symbols of the Message-Passing Interface are not
# renamed in this file (if -f77sed option is used at configure time,
# then the file mpid_fortdefs.h will rename those functions).
# "mpio_test" and "mpio_wait" are defined in C code and available as
# Fortran functions.  They should be added to
# mpid.globus2/mpid_fortdefs.h and to the "sed" script which renames
# the MPI calls in the user's application fortran code (if option
# "-f77sed" has been used while configuring).
# The 7 global symbols mpir_init_fcm, mpir_init_flog, mpir_getarg,
# mpir_iargc, mpir_get_fsize, mpir_init_fsize, mpir_init_bottom are
# defined as Fortran functions and used by MPICH's internal C code.
# In directory src/fortran/src/, these 7 functions are defined in
# files initfdte.f, farg.f, initfcmn.f: in those files, the functions
# are renamed by the Makefile using "sed".
egrep -v '\.' $TMP_RAW_GLOBALS | egrep -v -f $EXCLUDED_SYMB | sort -u | \
sed -e '/^PMPI_[[:upper:]_[:digit:]]*[[:upper:][:digit:]]$/d' \
    -e '/^MPI_[[:upper:]_[:digit:]]*[[:upper:][:digit:]]$/d' \
    -e '/^pmpi_[[:lower:]_[:digit:]]*[[:lower:][:digit:]]$/d' \
    -e '/^mpi_[[:lower:]_[:digit:]]*[[:lower:][:digit:]]$/d' \
    -e '/^pmpi_[[:lower:]_[:digit:]]*[[:lower:][:digit:]]_$/d' \
    -e '/^mpi_[[:lower:]_[:digit:]]*[[:lower:][:digit:]]_$/d' \
    -e '/^pmpi_[[:lower:]_[:digit:]]*[[:lower:][:digit:]]__$/d' \
    -e '/^mpi_[[:lower:]_[:digit:]]*[[:lower:][:digit:]]__$/d' \
    -e '/^mpio_test$/d' -e '/^mpio_test_$/d' \
    -e '/^mpio_test__$/d' -e '/^MPIO_TEST$/d' \
    -e '/^pmpio_test$/d' -e '/^pmpio_test_$/d' \
    -e '/^pmpio_test__$/d' -e '/^PMPIO_TEST$/d' \
    -e '/^mpio_wait$/d' -e '/^mpio_wait_$/d' \
    -e '/^mpio_wait__$/d' -e '/^MPIO_WAIT$/d' \
    -e '/^pmpio_wait$/d' -e '/^pmpio_wait_$/d' \
    -e '/^pmpio_wait__$/d' -e '/^PMPIO_WAIT$/d' \
    -e '/^mp[iq]r_init_fcm$/d' -e '/^mp[iq]r_init_fcm_$/d' \
    -e '/^mp[iq]r_init_fcm__$/d' -e '/^MP[IQ]R_INIT_FCM$/d' \
    -e '/^mp[iq]r_init_flog$/d' -e '/^mp[iq]r_init_flog_$/d' \
    -e '/^mp[iq]r_init_flog__$/d' -e '/^MP[IQ]R_INIT_FLOG$/d' \
    -e '/^mp[iq]r_getarg$/d' -e '/^mp[iq]r_getarg_$/d' \
    -e '/^mp[iq]r_getarg__$/d' -e '/^MP[IQ]R_GETARG$/d' \
    -e '/^mp[iq]r_iargc$/d' -e '/^mp[iq]r_iargc_$/d' \
    -e '/^mp[iq]r_iargc__$/d' -e '/^MP[IQ]R_IARGC$/d' \
    -e '/^mp[iq]r_get_fsize$/d' -e '/^mp[iq]r_get_fsize_$/d' \
    -e '/^mp[iq]r_get_fsize__$/d' -e '/^MP[IQ]R_GET_FSIZE$/d' \
    -e '/^mp[iq]r_init_fsize$/d' -e '/^mp[iq]r_init_fsize_$/d' \
    -e '/^mp[iq]r_init_fsize__$/d' -e '/^MP[IQ]R_INIT_FSIZE$/d' \
    -e '/^mp[iq]r_init_bottom$/d' -e '/^mp[iq]r_init_bottom_$/d' \
    -e '/^mp[iq]r_init_bottom__$/d' -e '/^MP[IQ]R_INIT_BOTTOM$/d' > $TMP_FILTERED_GLOBALS

Show "counting the new number of global symbols"
new_number="`wc -l $TMP_FILTERED_GLOBALS | awk '{print $1}'`"

case "$GLOBUS_VERSION" in
   "1") glob_env_var="GLOBUS_INSTALL_PATH=\"$GLOBUS_INSTALL_PATH\"" ;;
   "2") glob_env_var="GLOBUS_LOCATION=\"$GLOBUS_LOCATION\"" ;;
esac
Show "creating header of the macro def file in temporary output file \"$TMP_OUTPUT\""
cat > $TMP_OUTPUT << EOF
/* This file was automatically generated by the script
 * "`basename $0`".  Date: $DATE
 * username: `whoami`
 * host: `uname -a`
 * GLOBUS_VERSION="$GLOBUS_VERSION"
 * $glob_env_var
 * CONFIGURE_OPTIONS="$CONFIGURE_OPTIONS"
 * DEVICE_OPTION="${DEVICE_OPTION}${FLAVOR}"
 * EXTRA_CONFIG_OPT="$EXTRA_CONFIG_OPT"
 * ROOT_DIR="$ROOT_DIR"
 * LIBS="$LIBS"
 * OUTPUT="$OUTPUT"
 * Number of global symbols in new macro def file: $new_number
 * FORMER_OUTPUT="$FORMER_OUTPUT"
 * Number of global symbols in former macro def file: $former_number
 * PREFIX="$PREFIX" */

#ifndef GLOBAL_SYMB_RENAMING
#define GLOBAL_SYMB_RENAMING

#include "global_fort_symb.h"

EOF

Show "inserting the #define's to rename all the global symbols in the temporary output file \"$TMP_OUTPUT\""
awk "{ print \"#define \" \$1 \" $PREFIX\" \$1 }" $TMP_FILTERED_GLOBALS >> $TMP_OUTPUT

Show "creating tail of of the macro def file in temporary output file \"$TMP_OUTPUT\""
cat >> $TMP_OUTPUT << EOF

#endif   /* GLOBAL_SYMB_RENAMING */

EOF

Show "copying the new macro def file to its final location \"$OUTPUT\""
mv -f $TMP_OUTPUT $OUTPUT
# protect the output file from the exit function
OUTPUT="/dev/null"
echo "I succeeded in my job."

echo "Cleaning up the MPICH directory tree:"
cmd="make distclean"
echo "   $cmd"
$cmd > /dev/null 2>&1

clean_exit

