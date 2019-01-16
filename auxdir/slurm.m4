##*****************************************************************************
#  AUTHOR:
#    Mark A. Grondona <mgrondona@llnl.gov>
#
#  SYNOPSIS:
#    Various X_AC_SLURM* macros for use in slurm
#
##*****************************************************************************


AC_DEFUN([X_AC_SLURM_PORTS],
[
  AC_MSG_CHECKING(for slurmctld default port)
  AC_ARG_WITH(slurmctld-port,
    AS_HELP_STRING(--with-slurmctld-port=N,set slurmctld default port [[6817]]),
        [ if test `expr match "$withval" '[[0-9]]*$'` -gt 0; then
            slurmctldport="$withval"
          fi
        ]
  )
  AC_MSG_RESULT(${slurmctldport=$1})
  AC_DEFINE_UNQUOTED(SLURMCTLD_PORT, [$slurmctldport],
                     [Define the default port number for slurmctld])
  AC_SUBST(SLURMCTLD_PORT)


  AC_MSG_CHECKING(for slurmd default port)
  AC_ARG_WITH(slurmd-port,
    AS_HELP_STRING(--with-slurmd-port=N,set slurmd default port [[6818]]),
        [ if test `expr match "$withval" '[[0-9]]*$'` -gt 0; then
            slurmdport="$withval"
          fi
        ]
  )
  AC_MSG_RESULT(${slurmdport=$2})
  AC_DEFINE_UNQUOTED(SLURMD_PORT, [$slurmdport],
                     [Define the default port number for slurmd])
  AC_SUBST(SLURMD_PORT)


  AC_MSG_CHECKING(for slurmdbd default port)
  AC_ARG_WITH(slurmdbd-port,
    AS_HELP_STRING(--with-slurmdbd-port=N,set slurmdbd default port [[6819]]),
        [ if test `expr match "$withval" '[[0-9]]*$'` -gt 0; then
            slurmdbdport="$withval"
          fi
        ]
  )
  AC_MSG_RESULT(${slurmdbdport=$3})
  AC_DEFINE_UNQUOTED(SLURMDBD_PORT, [$slurmdbdport],
                     [Define the default port number for slurmdbd])
  AC_SUBST(SLURMDBD_PORT)

  AC_MSG_CHECKING(for slurmctld default port count)
  AC_ARG_WITH(slurmctld-port-count,
    AS_HELP_STRING(--with-slurmctld-port-count=N,set slurmctld default port count [[1]]),
        [ if test `expr match "$withval" '[[0-9]]*$'` -gt 0; then
             slurmctldportcount="$withval"
          fi
        ]
  )
  AC_MSG_RESULT(${slurmctldportcount=$4})
  AC_DEFINE_UNQUOTED(SLURMCTLD_PORT_COUNT, [$slurmctldportcount],
                     [Define the default port count for slurmctld])
  AC_SUBST(SLURMCTLD_PORT_COUNT)
])

dnl
dnl Generic option for system dimensions
dnl
AC_DEFUN([X_AC_DIMENSIONS], [
  AC_MSG_CHECKING([System dimensions])
  AC_ARG_WITH([dimensions],
    AS_HELP_STRING(--with-dimensions=N, set system dimension count for generic computer system),
    [ if test `expr match "$withval" '[[0-9]]*$'` -gt 0; then
        dimensions="$withval"
        x_ac_dimensions=yes
      fi
    ],
    [x_ac_dimensions=no]
  )
  if test "$x_ac_dimensions" = yes; then
    if test $dimensions -lt 1; then
      AC_MSG_ERROR([Invalid dimensions value $dimensions])
    fi
    AC_MSG_RESULT([$dimensions]);
    AC_DEFINE_UNQUOTED(SYSTEM_DIMENSIONS, [$dimensions], [Define system dimension count])
  else
    AC_MSG_RESULT([not set]);
  fi
])

dnl
dnl To link to share object or the static version
dnl
AC_DEFUN([X_AC_LIBSLURM], [
  AC_MSG_CHECKING([Link to libslurm.so instead of libslurm.o])
  AC_ARG_WITH([shared-libslurm],
    AS_HELP_STRING(--without-shared-libslurm, statically link to libslurm.o instead of the shared libslurm lib - can dramatically increase the footprint of Slurm.),
    [ case "$withval" in
      yes) x_ac_shared_libslurm=yes ;;
      no)  x_ac_shared_libslurm=no ;;
      *)   AC_MSG_RESULT([doh!])
           AC_MSG_ERROR([bad value "$withval" for --without-shared-libslurm]) ;;
        esac
      ]
  )

  if test "$x_ac_shared_libslurm" = no; then
    LIB_SLURM_BUILD='$(top_builddir)/src/api/libslurm.o'
    LIB_SLURM=$LIB_SLURM_BUILD
    AC_MSG_RESULT([static]);
  else
    # The *_BUILD variables are here to make sure these are made before
    # compiling the bin
    LIB_SLURM_BUILD='$(top_builddir)/src/api/full_version.map $(top_builddir)/src/api/libslurmfull.la'
    # You will notice " or ' each does something different when resolving
    # variables.  Some need to be resolved now ($libdir) and others
    # ($(top_builddir)) need to be resolved when dealing with the Makefile.am's
    LIB_SLURM="-Wl,-rpath=$libdir/slurm"
    LIB_SLURM=$LIB_SLURM' -L$(top_builddir)/src/api/.libs -lslurmfull'

    AC_MSG_RESULT([shared]);
  fi

  AC_SUBST(LIB_SLURM)
  AC_SUBST(LIB_SLURM_BUILD)
])

dnl
dnl Check for program_invocation_name
dnl
AC_DEFUN([X_AC_SLURM_PROGRAM_INVOCATION_NAME],
[
  AC_MSG_CHECKING([for program_invocation_name])

  AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <stdio.h>
extern char *program_invocation_name;]], [[char *p; p = program_invocation_name; printf("%s\n", p);]])],[got_program_invocation_name=yes],[
  ])

  AC_MSG_RESULT(${got_program_invocation_name=no})

  if test "x$got_program_invocation_name" = "xyes"; then
    AC_DEFINE(HAVE_PROGRAM_INVOCATION_NAME, 1,
              [Define if libc sets program_invocation_name]
             )
  fi
])dnl AC_PROG_INVOCATION_NAME

dnl
dnl Check for Bigendian arch and set SLURM_BIGENDIAN acc'dngly
dnl
AC_DEFUN([X_AC_SLURM_BIGENDIAN],
[
  AC_C_BIGENDIAN
  if test "x$ac_cv_c_bigendian" = "xyes"; then
    AC_DEFINE(SLURM_BIGENDIAN,1,
             [Define if your architecture's byteorder is big endian.])
  fi
])dnl AC_SLURM_BIGENDIAN

dnl
dnl Perform Slurm Project version setup
dnl
AC_DEFUN([X_AC_SLURM_VERSION],
[
#
# Determine project/version from META file.
#  These are substituted into the Makefile and config.h.
#
PROJECT="`perl -ne 'print,exit if s/^\s*NAME:\s*(\S*).*/\1/i' $srcdir/META`"
AC_DEFINE_UNQUOTED(PROJECT, "$PROJECT", [Define the project's name.])
AC_SUBST(PROJECT)

# Automake desires "PACKAGE" variable instead of PROJECT
PACKAGE=$PROJECT

## Build the API version
## NOTE: We map API_MAJOR to be (API_CURRENT - API_AGE) to match the
##  behavior of libtool in setting the library version number. For more
##  information see src/api/Makefile.am
for name in CURRENT REVISION AGE; do
   API=`perl -ne "print,exit if s/^\s*API_$name:\s*(\S*).*/\1/i" $srcdir/META`
   eval SLURM_API_$name=$API
done
SLURM_API_MAJOR=`expr $SLURM_API_CURRENT - $SLURM_API_AGE`
SLURM_API_VERSION=`printf "0x%02x%02x%02x" ${SLURM_API_MAJOR#0} ${SLURM_API_AGE#0} ${SLURM_API_REVISION#0}`

AC_DEFINE_UNQUOTED(SLURM_API_VERSION,  $SLURM_API_VERSION,  [Define the API's version])
AC_DEFINE_UNQUOTED(SLURM_API_CURRENT,  $SLURM_API_CURRENT,  [API current version])
AC_DEFINE_UNQUOTED(SLURM_API_MAJOR,    $SLURM_API_MAJOR,    [API current major])
AC_DEFINE_UNQUOTED(SLURM_API_AGE,      $SLURM_API_AGE,      [API current age])
AC_DEFINE_UNQUOTED(SLURM_API_REVISION, $SLURM_API_REVISION, [API current rev])
AC_SUBST(SLURM_API_VERSION)
AC_SUBST(SLURM_API_CURRENT)
AC_SUBST(SLURM_API_MAJOR)
AC_SUBST(SLURM_API_AGE)
AC_SUBST(SLURM_API_REVISION)

# rpm make target needs Version in META, not major and minor version numbers
VERSION="`perl -ne 'print,exit if s/^\s*VERSION:\s*(\S*).*/\1/i' $srcdir/META`"
# If you ever use AM_INIT_AUTOMAKE(subdir-objects) do not define VERSION
# since it will do it this automatically
AC_DEFINE_UNQUOTED(VERSION, "$VERSION", [Define the project's version.])
AC_SUBST(VERSION)

SLURM_MAJOR="`perl -ne 'print,exit if s/^\s*MAJOR:\s*(\S*).*/\1/i' $srcdir/META`"
SLURM_MINOR="`perl -ne 'print,exit if s/^\s*MINOR:\s*(\S*).*/\1/i' $srcdir/META`"
SLURM_MICRO="`perl -ne 'print,exit if s/^\s*MICRO:\s*(\S*).*/\1/i' $srcdir/META`"
RELEASE="`perl -ne 'print,exit if s/^\s*RELEASE:\s*(\S*).*/\1/i' $srcdir/META`"

# NOTE: SLURM_VERSION_NUMBER excludes any non-numeric component 
# (e.g. "pre1" in the MICRO), but may be suitable for the user determining 
# how to use the APIs or other differences. 
SLURM_VERSION_NUMBER="`printf "0x%02x%02x%02x" ${SLURM_MAJOR#0} ${SLURM_MINOR#0} ${SLURM_MICRO#0}`"
AC_DEFINE_UNQUOTED(SLURM_VERSION_NUMBER, $SLURM_VERSION_NUMBER, [Slurm Version Number])
AC_SUBST(SLURM_VERSION_NUMBER)

if test "$SLURM_MAJOR.$SLURM_MINOR.$SLURM_MICRO" != "$VERSION"; then
    AC_MSG_ERROR([META information is inconsistent: $VERSION != $SLURM_MAJOR.$SLURM_MINOR.$SLURM_MICRO!])
fi

# Check to see if we're on an unstable branch (no prereleases yet)
if echo "$RELEASE" | grep -e "UNSTABLE"; then 
   DATE=`date +"%Y%m%d%H%M"`
   SLURM_RELEASE="unstable svn build $DATE" 
   SLURM_VERSION_STRING="$SLURM_MAJOR.$SLURM_MINOR ($SLURM_RELEASE)"
else
   SLURM_RELEASE="`echo $RELEASE | sed 's/^0\.//'`"
   SLURM_VERSION_STRING="$SLURM_MAJOR.$SLURM_MINOR.$SLURM_MICRO"
   test $RELEASE = "1" || SLURM_VERSION_STRING="$SLURM_VERSION_STRING-$SLURM_RELEASE"
fi
AC_DEFINE_UNQUOTED(SLURM_MAJOR, "$SLURM_MAJOR", 
                   [Define the project's major version.])
AC_DEFINE_UNQUOTED(SLURM_MINOR, "$SLURM_MINOR",
                   [Define the project's minor version.])
AC_DEFINE_UNQUOTED(SLURM_MICRO, "$SLURM_MICRO",
                   [Define the project's micro version.])
AC_DEFINE_UNQUOTED(RELEASE, "$RELEASE", [Define the project's release.])
AC_DEFINE_UNQUOTED(SLURM_VERSION_STRING, "$SLURM_VERSION_STRING",
                   [Define the project's version string.])

AC_SUBST(SLURM_MAJOR)
AC_SUBST(SLURM_MINOR)
AC_SUBST(SLURM_MICRO)
AC_SUBST(RELEASE)
AC_SUBST(SLURM_VERSION_STRING)

]) dnl AC_SLURM_VERSION
 
dnl
dnl Test if we want to include rpath in the executables (default=yes)
dnl Doing so is generally discouraged due to problems this causes in upgrading
dnl software and general incompatability issues
dnl
AC_DEFUN([X_AC_RPATH], [
  ac_with_rpath=yes

  AC_MSG_CHECKING([whether to include rpath in build])
  AC_ARG_WITH(
    [rpath],
    AS_HELP_STRING(--without-rpath, Do not include rpath in build),
      [ case "$withval" in
        yes) ac_with_rpath=yes ;;
        no)  ac_with_rpath=no ;;
        *)   AC_MSG_RESULT([doh!])
             AC_MSG_ERROR([bad value "$withval" for --without-rpath]) ;;
        esac
      ]
  )
  AC_MSG_RESULT([$ac_with_rpath])
])
