##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>, Danny Auble <da@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_BG X_AC_BGQ
#
#  DESCRIPTION:
#    Test for Blue Gene specific files.
#    If found define HAVE_BG and HAVE_FRONT_END and others
##*****************************************************************************

AC_DEFUN([X_AC_BG],
[
	ac_real_bluegene_loaded=no
	ac_bluegene_loaded=no

	AC_MSG_CHECKING([whether BG is explicitly disabled])
	AC_ARG_ENABLE(
		[bluegene],
		AS_HELP_STRING(--disable-bluegene,Disable Bluegene support for BGAS nodes (or wherever you run a Slurm on a bluegene system not wanting it to act like a Bluegene)),
		[ case "$enableval" in
			  yes) ac_bluegene_loaded=no ;;
			  no) ac_bluegene_loaded=yes  ;;
			  *) AC_MSG_RESULT([doh!])
			     AC_MSG_ERROR([bad value "$enableval" for --disable-bluegene])  ;;
		  esac ]
	)

	AC_MSG_RESULT([${ac_bluegene_loaded=yes}])
])

AC_DEFUN([X_AC_BGQ],
[
	# test for bluegene emulation mode
   	AC_ARG_ENABLE(bgq-emulation, AS_HELP_STRING(--enable-bgq-emulation,Run SLURM in BG/Q mode on a non-bluegene system),
	[ case "$enableval" in
	  yes) bgq_emulation=yes ;;
	  no)  bgq_emulation=no ;;
	  *)   AC_MSG_ERROR([bad value "$enableval" for --enable-bgq-emulation])  ;;
    	esac ])

	# Skip if already set
   	if test "x$ac_bluegene_loaded" = "xyes" ; then
		bg_default_dirs=""
	elif test "x$bgq_emulation" = "xyes"; then
      		AC_DEFINE(HAVE_4D, 1, [Define to 1 if 4-dimensional architecture])
  		AC_DEFINE(SYSTEM_DIMENSIONS, 4, [4-dimensional schedulable architecture])
		AC_DEFINE(HAVE_BG, 1, [Define to 1 if emulating or running on Blue Gene system])
      		AC_DEFINE(HAVE_BGQ, 1, [Define to 1 if emulating or running on Blue Gene/Q system])
      		AC_DEFINE(HAVE_FRONT_END, 1, [Define to 1 if running slurmd on front-end only])
    		AC_MSG_NOTICE([Running in BG/Q emulation mode])
		bg_default_dirs=""
 		#define ac_bluegene_loaded so we don't load another bluegene conf
		ac_bluegene_loaded=yes
		ac_bgq_loaded=yes
	else
		bg_default_dirs="/bgsys/drivers/ppcfloor"
	fi

	libname=bgsched
	loglibname=log4cxx
	runjoblibname=runjob_client

   	for bg_dir in $trydb2dir "" $bg_default_dirs; do
      	# Skip directories that don't exist
      		if test ! -z "$bg_dir" -a ! -d "$bg_dir" ; then
			continue;
      		fi

		soloc=$bg_dir/hlcs/lib/lib$libname.so
      		# Search for required BG API libraries in the directory
      		if test -z "$have_bg_ar" -a -f "$soloc" ; then
			have_bgq_ar=yes
			if test "$ac_with_rpath" = "yes"; then
				bg_libs="$bg_libs -Wl,-rpath -Wl,$bg_dir/hlcs/lib -L$bg_dir/hlcs/lib -l$libname"
			else
				bg_libs="$bg_libs -L$bg_dir/hlcs/lib -l$libname"
			fi
		fi

  		soloc=$bg_dir/extlib/lib/lib$loglibname.so
    		if test -z "$have_bg_ar" -a -f "$soloc" ; then
			have_bgq_ar=yes
			if test "$ac_with_rpath" = "yes"; then
				bg_libs="$bg_libs -Wl,-rpath -Wl,$bg_dir/extlib/lib -L$bg_dir/extlib/lib -l$loglibname"
			else
				bg_libs="$bg_libs -L$bg_dir/extlib/lib -l$loglibname"
			fi
		fi

		soloc=$bg_dir/hlcs/lib/lib$runjoblibname.so
		# Search for required BG API libraries in the directory
		if test -z "$have_bg_ar" -a -f "$soloc" ; then
			have_bgq_ar=yes
			if test "$ac_with_rpath" = "yes"; then
				runjob_ldflags="$runjob_ldflags -Wl,-rpath -Wl,$bg_dir/hlcs/lib -L$bg_dir/hlcs/lib -l$runjoblibname"
			else
				runjob_ldflags="$runjob_ldflags -L$bg_dir/hlcs/lib -l$runjoblibname"
			fi
		fi

		# Search for headers in the directory
      		if test -z "$have_bg_hdr" -a -f "$bg_dir/hlcs/include/bgsched/bgsched.h" ; then
			have_bgq_hdr=yes
			bg_includes="-I$bg_dir -I$bg_dir/hlcs/include"
      		fi
     		if test -z "$have_bg_hdr" -a -f "$bg_dir/extlib/include/log4cxx/logger.h" ; then
			have_bgq_hdr=yes
			bg_includes="$bg_includes -I$bg_dir/extlib/include"
    		fi
   	done

   	if test ! -z "$have_bgq_ar" -a ! -z "$have_bgq_hdr" ; then
      		# ac_with_readline="no"
		# Test to make sure the api is good
		saved_LIBS="$LIBS"
		saved_CPPFLAGS="$CPPFLAGS"
		LIBS="$saved_LIBS $bg_libs"
		CPPFLAGS="$saved_CPPFLAGS -m64 $bg_includes"
		AC_LANG_PUSH(C++)
		AC_LINK_IFELSE([AC_LANG_PROGRAM(
                                [[#include <bgsched/bgsched.h>
#include <log4cxx/logger.h>]],
				[[ bgsched::init("");
 log4cxx::LoggerPtr logger_ptr(log4cxx::Logger::getLogger( "ibm" ));]])],
			        [have_bgq_files=yes],
				[AC_MSG_ERROR(There is a problem linking to the BG/Q api.)])
		# In later versions of the driver IBM added a better function
		# to see if blocks were IO connected or not.  Here is a check
		# to not break backwards compatibility
		AC_LINK_IFELSE([AC_LANG_PROGRAM(
                                [[#include <bgsched/bgsched.h>
				#include <bgsched/Block.h>]],
				[[ bgsched::Block::checkIO("", NULL, NULL);]])],
			        [have_bgq_new_io_check=yes],
				[AC_MSG_RESULT(Using old iocheck.)])
		# In later versions of the driver IBM added an "action" to a
		# block.  Here is a check to not break backwards compatibility
		AC_LINK_IFELSE([AC_LANG_PROGRAM(
                                [[#include <bgsched/bgsched.h>
				#include <bgsched/Block.h>]],
				[[ bgsched::Block::Ptr block_ptr;
				block_ptr->getAction();]])],
			        [have_bgq_get_action=yes],
				[AC_MSG_RESULT(Blocks do not have actions!)])
		AC_LANG_POP(C++)
		LIBS="$saved_LIBS"
		CPPFLAGS="$saved_CPPFLAGS"
   	fi

  	if test ! -z "$have_bgq_files" ; then
		BG_LDFLAGS="$bg_libs"
		RUNJOB_LDFLAGS="$runjob_ldflags"
      		BG_INCLUDES="$bg_includes"
		CFLAGS="$CFLAGS -m64"
   		CXXFLAGS="$CXXFLAGS $CFLAGS"
      		AC_DEFINE(HAVE_4D, 1, [Define to 1 if 4-dimensional architecture])
  		AC_DEFINE(SYSTEM_DIMENSIONS, 4, [4-dimensional architecture])
      		AC_DEFINE(HAVE_BG, 1, [Define to 1 if emulating or running on Blue Gene system])
      		AC_DEFINE(HAVE_BGQ, 1, [Define to 1 if emulating or running on Blue Gene/Q system])
      		AC_DEFINE(HAVE_FRONT_END, 1, [Define to 1 if running slurmd on front-end only])
		AC_DEFINE(HAVE_BG_FILES, 1, [Define to 1 if have Blue Gene files])
		#AC_DEFINE_UNQUOTED(BG_BRIDGE_SO, "$soloc", [Define the BG_BRIDGE_SO value])
		if test ! -z "$have_bgq_new_io_check" ; then
			AC_DEFINE(HAVE_BG_NEW_IO_CHECK, 1, [Define to 1 if using code with new iocheck])
		fi

		if test ! -z "$have_bgq_get_action" ; then
			AC_DEFINE(HAVE_BG_GET_ACTION, 1, [Define to 1 if using code where blocks have actions])
		fi

    		AC_MSG_NOTICE([Running on a legitimate BG/Q system])
		# AC_MSG_CHECKING(for BG serial value)
		# bg_serial="BGQ"
    		# AC_ARG_WITH(bg-serial,, [bg_serial="$withval"])
     		# AC_MSG_RESULT($bg_serial)
     		# AC_DEFINE_UNQUOTED(BG_SERIAL, "$bg_serial", [Define the BG_SERIAL value])
 		#define ac_bluegene_loaded so we don't load another bluegene conf
		ac_bluegene_loaded=yes
		ac_real_bluegene_loaded=yes
		ac_bgq_loaded=yes
	fi

   	AC_SUBST(BG_INCLUDES)
   	AC_SUBST(BG_LDFLAGS)
	AC_SUBST(RUNJOB_LDFLAGS)
])
