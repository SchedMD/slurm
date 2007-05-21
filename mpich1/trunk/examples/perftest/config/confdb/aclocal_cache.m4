dnl
dnl/*D
dnl AC_CACHE_LOAD - Replacement for autoconf cache load 
dnl
dnl Notes:
dnl Caching in autoconf is broken (through version 2.13).  The problem is 
dnl that the cache is read
dnl without any check for whether it makes any sense to read it.
dnl A common problem is a build on a shared file system; connecting to 
dnl a different computer and then building within the same directory will
dnl lead to at best error messages from configure and at worse a build that
dnl is wrong but fails only at run time (e.g., wrong datatype sizes used).
dnl Later versions of autoconf do include some checks for changes in the
dnl environment that impact the choices, but still misses problems with
dnl multiple different systems.
dnl 
dnl This fixes that by requiring the user to explicitly enable caching 
dnl before the cache file will be loaded.
dnl
dnl To use this version of 'AC_CACHE_LOAD', you need to include
dnl 'aclocal_cache.m4' in your 'aclocal.m4' file.  The sowing 'aclocal.m4'
dnl file includes this file.
dnl
dnl If no --enable-cache or --disable-cache option is selected, the
dnl command causes configure to keep track of the system being configured
dnl in a config.system file; if the current system matches the value stored
dnl in that file (or there is neither a config.cache nor config.system file),
dnl configure will enable caching.  In order to ensure that the configure
dnl tests make sense, the values of CC, F77, F90, and CXX are also included 
dnl in the config.system file.  In addition, the value of PATH is included
dnl to ensure that changes in the PATH that might select a different version
dnl of a program with the same name (such as a native make versus gnumake)
dnl are detected.
dnl
dnl Bugs:
dnl This does not work with the Cygnus configure because the enable arguments
dnl are processed *after* AC_CACHE_LOAD (!).  To address this, we avoid 
dnl changing the value of enable_cache, and use real_enable_cache, duplicating
dnl the "notgiven" value.
dnl
dnl The environment variable CONFIGURE_DEBUG_CACHE, if set to yes,
dnl will cause additional data to be written out during the configure process.
dnl This can be helpful in debugging the cache file process.
dnl
dnl See Also:
dnl PAC_ARG_CACHING
dnl D*/
define([AC_CACHE_LOAD],
[if test "$CONFIGURE_DEBUG_CACHE" = yes ; then 
    oldopts="$-"
    clearMinusX=no
    set -x 
    if test "$oldopts" != "$-" ; then 
        clearMinusX=yes
    fi
fi 
if test "X$cache_system" = "X" ; then
    # A default file name, just in case
    cache_system="config.system"
    if test "$cache_file" != "/dev/null" ; then
        # Get the directory for the cache file, if any
	changequote(,)
        dnl Be careful to ensure that there is no doubled slash
        cache_system=`echo $cache_file | sed -e 's%^\(.*/\)[^/]*%\1config.system%'`
	changequote([,])
        test "x$cache_system" = "x$cache_file" && cache_system="config.system"
#    else
#        We must *not* set enable_cache to no because we need to know if
#        enable_cache was not set.  
#        enable_cache=no
    fi
fi
dnl
dnl The "action-if-not-given" part of AC_ARG_ENABLE is not executed until
dnl after the AC_CACHE_LOAD is executed (!).  Thus, the value of 
dnl enable_cache if neither --enable-cache or --disable-cache is selected
dnl is null.  Just in case autoconf ever fixes this, we test both cases.
dnl
dnl Include PATH in the cache.system file since changing the path can
dnl change which versions of programs are found (such as vendor make
dnl or GNU make).
dnl
#
# Get a test value and flag whether we should remove/replace the 
# cache_system file (do so unless cache_system_ok is yes)
# FC and F77 should be synonyms.  Save both in case
# We include the xxxFLAGS in case the user is using the flags to change
# the language (either input or output) of the compiler.  E.g., 
# using -xarch=v9 on Solaris to select 64 bit output or using -D_BSD_SOURCE 
# with gcc to get different header files on input.
cleanargs=`echo "$CC $F77 $FC $CXX $F90 $CFLAGS $FFLAGS $CXXFLAGS $F90FLAGS $PATH" | tr '"' ' '`
if uname -srm >/dev/null 2>&1 ; then
    cache_system_text="`uname -srm` $cleanargs"
else
    cache_system_text="-no-uname- $cleanargs"
fi
cache_system_ok=no
#
if test -z "$real_enable_cache" ; then
    real_enable_cache=$enable_cache
    if test -z "$real_enable_cache" ; then real_enable_cache="notgiven" ; fi
fi
if test "X$real_enable_cache" = "Xnotgiven" ; then
    # check for valid cache file
    if test -z "$cache_system" ; then cache_system="config.system" ; fi
    if uname -srm >/dev/null 2>&1 ; then
        if test -f "$cache_system" -a -n "$cache_system_text" ; then
	    if test "$cache_system_text" = "`cat $cache_system`" ; then
	        real_enable_cache="yes"
                cache_system_ok=yes
	    fi
        elif test ! -f "$cache_system" -a -n "$cache_system_text" ; then
	    # remove the cache file because it may not correspond to our
	    # system
	    if test "$cache_file" != "/dev/null" ; then 
	        rm -f $cache_file
	    fi
	    real_enable_cache="yes"
        fi
    fi
fi
if test "X$real_enable_cache" = "Xyes" -a "$cache_file" = "/dev/null" ; then
    real_enable_cache=no
fi
if test "X$real_enable_cache" = "Xyes" ; then
  if test -r "$cache_file" ; then
    echo "loading cache $cache_file"
    if test -w "$cache_file" ; then
        # Clean the cache file (ergh)
	PAC_CACHE_CLEAN
    fi
    . $cache_file
  else
    echo "Configure in `pwd` creating cache $cache_file"
    > $cache_file
    rm -f $cache_system
  fi
else
  cache_file="/dev/null"
fi
# Remember our location and the name of the cachefile
pac_cv_my_conf_dir=`pwd`
dnl do not include the cachefile name, since this may contain the process
dnl number and cause comparisons looking for changes to the cache file
dnl to detect a change that isn't real.
dnl pac_cv_my_cachefile=$cachefile
#
# Update the cache_system file if necessary
if test "$cache_system_ok" != yes ; then
    if test -n "$cache_system" ; then
        rm -f $cache_system
        echo $cache_system_text > $cache_system
    fi
fi
if test "$clearMinusX" = yes ; then
    set +x
fi
])
dnl
dnl/*D 
dnl PAC_ARG_CACHING - Enable caching of results from a configure execution
dnl
dnl Synopsis:
dnl PAC_ARG_CACHING
dnl
dnl Output Effects:
dnl Adds '--enable-cache' and '--disable-cache' to the command line arguments
dnl accepted by 'configure'.  
dnl
dnl See Also:
dnl AC_CACHE_LOAD
dnl D*/
dnl Add this call to the other ARG_ENABLE calls.  Note that the values
dnl set here are redundant; the LOAD_CACHE call relies on the way autoconf
dnl initially processes ARG_ENABLE commands.
AC_DEFUN(PAC_ARG_CACHING,[
AC_ARG_ENABLE(cache,
[--enable-cache  - Turn on configure caching],
enable_cache="$enableval",enable_cache="notgiven")
])
dnl

dnl Clean the cache of extraneous quotes that AC_CACHE_SAVE may add
dnl
AC_DEFUN([PAC_CACHE_CLEAN],[
    rm -f confcache
    sed -e "s/'\\\\''//g" -e "s/'\\\\/'/" -e "s/\\\\'/'/" \
		-e "s/'\\\\''//g" $cache_file > confcache
    if cmp -s $cache_file confcache ; then
        :
    else
        if test -w $cache_file ; then
	    echo "updating cache $cache_file"
            cat confcache > $cache_file
        else
            echo "not updating unwritable cache $cache_file"
        fi
    fi	
    rm -f confcache
    if test "$DEBUG_AUTOCONF_CACHE" = "yes" ; then
        echo "Results of cleaned cache file:"
	echo "--------------------------------------------------------"
	cat $cache_file
	echo "--------------------------------------------------------"
    fi
])

dnl/*D
dnl PAC_SUBDIR_CACHE - Create a cache file before ac_output for subdirectory
dnl configures.
dnl 
dnl Synopsis:
dnl PAC_SUBDIR_CACHE(when)
dnl
dnl Input Parameter:
dnl . when - Indicates when the cache should be created (optional)
dnl          If 'always', create a new cache file.  This option
dnl          should be used if any of the cache parameters (such as 
dnl          CFLAGS or LDFLAGS) may have changed.
dnl
dnl Output Effects:
dnl 	
dnl Create a cache file before ac_output so that subdir configures don't
dnl make mistakes. 
dnl We can't use OUTPUT_COMMANDS to remove the cache file, because those
dnl commands are executed *before* the subdir configures.
dnl
dnl D*/
AC_DEFUN(PAC_SUBDIR_CACHE,[])
AC_DEFUN(PAC_SUBDIR_CACHE_OLD,[
if test "x$1" = "xalways" -o \( "$cache_file" = "/dev/null" -a "X$real_enable_cache" = "Xnotgiven" \) ; then
    # Use an absolute directory to help keep the subdir configures from getting
    # lost
    cache_file=`pwd`/$$conf.cache
    touch $cache_file
    dnl 
    dnl For Autoconf 2.52+, we should ensure that the environment is set
    dnl for the cache.  Make sure that we get the values and set the 
    dnl xxx_set variables properly
    ac_cv_env_CC_set=set
    ac_cv_env_CC_value=$CC
    ac_cv_env_CFLAGS_set=${CFLAGS+set}
    ac_cv_env_CFLAGS_value=$CFLAGS
    ac_cv_env_CPP_set=set
    ac_cv_env_CPP_value=$CPP
    ac_cv_env_CPPFLAGS_set=${CPPFLAGS+set}
    ac_cv_env_CPPFLAGS_value=$CPPFLAGS
    ac_cv_env_LDFLAGS_set=${LDFLAGS+set}
    ac_cv_env_LDFLAGS_value=$LDFLAGS
    ac_cv_env_LIBS_set=${LIBS+set}
    ac_cv_env_LIBS_value=$LIBS
    ac_cv_env_FC_set=${FS+set}
    ac_cv_env_FC_value=$FC
    ac_cv_env_F77_set=${F77+set}
    ac_cv_env_F77_value=$F77
    ac_cv_env_F90_set=${F90+set}
    ac_cv_env_F90_value=$F90
    ac_cv_env_FFLAGS_set=${FFLAGS+set}
    ac_cv_env_FFLAGS_value=$FFLAGS
    ac_cv_env_CXX_set=${CXX+set}
    ac_cv_env_CXX_value=$CXX

    ac_env_CC_set=set
    ac_env_CC_value=$CC
    ac_env_CFLAGS_set=${CFLAGS+set}
    ac_env_CFLAGS_value=$CFLAGS
    ac_env_CPP_set=set
    ac_env_CPP_value=$CPP
    ac_env_CPPFLAGS_set=${CPPFLAGS+set}
    ac_env_CPPFLAGS_value=$CPPFLAGS
    ac_env_LDFLAGS_set=${LDFLAGS+set}
    ac_env_LDFLAGS_value=$LDFLAGS
    ac_env_LIBS_set=${LIBS+set}
    ac_env_LIBS_value=$LIBS
    ac_env_FC_set=${FS+set}
    ac_env_FC_value=$FC
    ac_env_F77_set=${F77+set}
    ac_env_F77_value=$F77
    ac_env_F90_set=${F90+set}
    ac_env_F90_value=$F90
    ac_env_FFLAGS_set=${FFLAGS+set}
    ac_env_FFLAGS_value=$FFLAGS
    ac_env_CXX_set=${CXX+set}
    ac_env_CXX_value=$CXX

    dnl other parameters are
    dnl build_alias, host_alias, target_alias

    # It turns out that A C CACHE_SAVE can't be invoked more than once
    # with data that contains blanks.  What happens is that the quotes
    # that it adds get quoted and then added again.  To avoid this,
    # we strip off the outer quotes for all cached variables
    dnl We add pac_cv_my_conf_dir to give the source of this cachefile,
    dnl and pac_cv_my_cachefile to indicate how it chose the cachefile.
    pac_cv_my_conf_dir=`pwd`
    pac_cv_my_cachefile=$cachefile
    AC_CACHE_SAVE
    PAC_CACHE_CLEAN
    ac_configure_args="$ac_configure_args -enable-cache"
fi
dnl Unconditionally export these values.  Subdir configures break otherwise
export CC
export CFLAGS
export LDFLAGS
export LIBS
export CPPFLAGS
export CPP
export FC
export F77
export F90
export CXX
export FFLAGS
export CCFLAGS
])
AC_DEFUN(PAC_SUBDIR_CACHE_CLEANUP,[])
AC_DEFUN(PAC_SUBDIR_CACHE_CLEANUP_OLD,[
if test "$cache_file" != "/dev/null" -a "X$real_enable_cache" = "Xnotgiven" ; then
   rm -f $cache_file
   cache_file=/dev/null
fi
])
