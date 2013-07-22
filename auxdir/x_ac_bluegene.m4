##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>, Danny Auble <da@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_BGL X_AC_BGP X_AC_BGQ
#
#  DESCRIPTION:
#    Test for Blue Gene specific files.
#    If found define HAVE_BG and HAVE_FRONT_END and others
##*****************************************************************************


AC_DEFUN([X_AC_BGL],
[
	ac_real_bluegene_loaded=no
	ac_bluegene_loaded=no

   	AC_ARG_WITH(db2-dir, AS_HELP_STRING(--with-db2-dir=PATH,Specify path to parent directory of DB2 library), [ trydb2dir=$withval ])

	# test for bluegene emulation mode

  	AC_ARG_ENABLE(bluegene-emulation, AS_HELP_STRING(--enable-bluegene-emulation, deprecated use --enable-bgl-emulation),
	[ case "$enableval" in
	  yes) bluegene_emulation=yes ;;
	  no)  bluegene_emulation=no ;;
	  *)   AC_MSG_ERROR([bad value "$enableval" for --enable-bluegene-emulation])  ;;
    	esac ])

  	AC_ARG_ENABLE(bgl-emulation, AS_HELP_STRING(--enable-bgl-emulation,Run SLURM in BGL mode on a non-bluegene system),
	[ case "$enableval" in
	  yes) bgl_emulation=yes ;;
	  no)  bgl_emulation=no ;;
	  *)   AC_MSG_ERROR([bad value "$enableval" for --enable-bgl-emulation])  ;;
    	esac ])

	if test "x$bluegene_emulation" = "xyes" -o "x$bgl_emulation" = "xyes"; then
      		AC_DEFINE(HAVE_3D, 1, [Define to 1 if 3-dimensional architecture])
  		AC_DEFINE(SYSTEM_DIMENSIONS, 3, [3-dimensional architecture])
		AC_DEFINE(HAVE_BG, 1, [Define to 1 if emulating or running on Blue Gene system])
		AC_DEFINE(HAVE_BG_L_P, 1, [Define to 1 if emulating or running on Blue Gene/L or P system])
      		AC_DEFINE(HAVE_BGL, 1, [Define to 1 if emulating or running on Blue Gene/L system])
      		AC_DEFINE(HAVE_FRONT_END, 1, [Define to 1 if running slurmd on front-end only])
    		AC_MSG_NOTICE([Running in BG/L emulation mode])
		bg_default_dirs=""
 		#define ac_bluegene_loaded so we don't load another bluegene conf
		ac_bluegene_loaded=yes
	else
  	   	bg_default_dirs="/bgl/BlueLight/ppcfloor/bglsys /opt/IBM/db2/V8.1 /u/bgdb2cli/sqllib /home/bgdb2cli/sqllib"
	fi

   	for bg_dir in $trydb2dir "" $bg_default_dirs; do
      	# Skip directories that don't exist
      		if test ! -z "$bg_dir" -a ! -d "$bg_dir" ; then
			continue;
      		fi

      		# Search for required BG API libraries in the directory
      		if test -z "$have_bg_ar" -a -f "$bg_dir/lib64/libbglbridge.so" ; then
			have_bg_ar=yes
			bg_bridge_so="$bg_dir/lib64/libbglbridge.so"
       	 		bg_ldflags="$bg_ldflags -L$bg_dir/lib64 -L/usr/lib64 -Wl,--unresolved-symbols=ignore-in-shared-libs -lbglbridge -lbgldb -ltableapi -lbglmachine -lexpat -lsaymessage"
		fi

      		# Search for required DB2 library in the directory
      		if test -z "$have_db2" -a -f "$bg_dir/lib64/libdb2.so" ; then
			have_db2=yes
	 	 	bg_db2_so="$bg_dir/lib64/libdb2.so"
       	 		bg_ldflags="$bg_ldflags -L$bg_dir/lib64 -ldb2"
       		fi

      		# Search for headers in the directory
      		if test -z "$have_bg_hdr" -a -f "$bg_dir/include/rm_api.h" ; then
			have_bg_hdr=yes
			bg_includes="-I$bg_dir/include"
      		fi
   	done

   	if test ! -z "$have_bg_ar" -a ! -z "$have_bg_hdr" -a ! -z "$have_db2" ; then
      		# ac_with_readline="no"
		# Test to make sure the api is good
		have_bg_files=yes
      		saved_LDFLAGS="$LDFLAGS"
      	 	LDFLAGS="$saved_LDFLAGS $bg_ldflags -m64"
		AC_LINK_IFELSE([AC_LANG_PROGRAM([[ int rm_set_serial(char *); ]], [[ rm_set_serial(""); ]])],[have_bg_files=yes],[AC_MSG_ERROR(There is a problem linking to the BG/L api.)])
		LDFLAGS="$saved_LDFLAGS"
   	fi

  	if test ! -z "$have_bg_files" ; then
      		BG_INCLUDES="$bg_includes"
		CFLAGS="$CFLAGS -m64 --std=gnu99"
		CXXFLAGS="$CXXFLAGS $CFLAGS"
      		AC_DEFINE(HAVE_3D, 1, [Define to 1 if 3-dimensional architecture])
  		AC_DEFINE(SYSTEM_DIMENSIONS, 3, [3-dimensional architecture])
      		AC_DEFINE(HAVE_BG, 1, [Define to 1 if emulating or running on Blue Gene system])
		AC_DEFINE(HAVE_BG_L_P, 1, [Define to 1 if emulating or running on Blue Gene/L or P system])
      		AC_DEFINE(HAVE_BGL, 1, [Define to 1 if emulating or running on Blue Gene/L system])
      		AC_DEFINE(HAVE_FRONT_END, 1, [Define to 1 if running slurmd on front-end only])
		AC_DEFINE(HAVE_BG_FILES, 1, [Define to 1 if have Blue Gene files])

      		AC_DEFINE_UNQUOTED(BG_BRIDGE_SO, "$bg_bridge_so", [Define the BG_BRIDGE_SO value])
		AC_DEFINE_UNQUOTED(BG_DB2_SO, "$bg_db2_so", [Define the BG_DB2_SO value])
		AC_MSG_CHECKING(for BG serial value)
      		bg_serial="BGL"
      		AC_ARG_WITH(bg-serial,
		AS_HELP_STRING(--with-bg-serial=NAME,set BG_SERIAL value), [bg_serial="$withval"])
     		AC_MSG_RESULT($bg_serial)
     		AC_DEFINE_UNQUOTED(BG_SERIAL, "$bg_serial", [Define the BG_SERIAL value])
 		#define ac_bluegene_loaded so we don't load another bluegene conf
		ac_bluegene_loaded=yes
		ac_real_bluegene_loaded=yes
  	fi

   	AC_SUBST(BG_INCLUDES)
])

AC_DEFUN([X_AC_BGP],
[
	# test for bluegene emulation mode
   	AC_ARG_ENABLE(bgp-emulation, AS_HELP_STRING(--enable-bgp-emulation,Run SLURM in BG/P mode on a non-bluegene system),
	[ case "$enableval" in
	  yes) bgp_emulation=yes ;;
	  no)  bgp_emulation=no ;;
	  *)   AC_MSG_ERROR([bad value "$enableval" for --enable-bgp-emulation])  ;;
    	esac ])

	# Skip if already set
   	if test "x$ac_bluegene_loaded" = "xyes" ; then
		bg_default_dirs=""
	elif test "x$bgp_emulation" = "xyes"; then
      		AC_DEFINE(HAVE_3D, 1, [Define to 1 if 3-dimensional architecture])
  		AC_DEFINE(SYSTEM_DIMENSIONS, 3, [3-dimensional architecture])
		AC_DEFINE(HAVE_BG, 1, [Define to 1 if emulating or running on Blue Gene system])
		AC_DEFINE(HAVE_BG_L_P, 1, [Define to 1 if emulating or running on Blue Gene/L or P system])
      		AC_DEFINE(HAVE_BGP, 1, [Define to 1 if emulating or running on Blue Gene/P system])
      		AC_DEFINE(HAVE_FRONT_END, 1, [Define to 1 if running slurmd on front-end only])
    		AC_MSG_NOTICE([Running in BG/P emulation mode])
		bg_default_dirs=""
 		#define ac_bluegene_loaded so we don't load another bluegene conf
		ac_bluegene_loaded=yes
	else
		bg_default_dirs="/bgsys/drivers/ppcfloor"
	fi

	libname=bgpbridge

   	for bg_dir in $trydb2dir "" $bg_default_dirs; do
      	# Skip directories that don't exist
      		if test ! -z "$bg_dir" -a ! -d "$bg_dir" ; then
			continue;
      		fi

		soloc=$bg_dir/lib64/lib$libname.so
      		# Search for required BG API libraries in the directory
      		if test -z "$have_bg_ar" -a -f "$soloc" ; then
			have_bgp_ar=yes
			bg_ldflags="$bg_ldflags -L$bg_dir/lib64 -L/usr/lib64 -Wl,--unresolved-symbols=ignore-in-shared-libs -l$libname"
		fi

      		# Search for headers in the directory
      		if test -z "$have_bg_hdr" -a -f "$bg_dir/include/rm_api.h" ; then
			have_bgp_hdr=yes
			bg_includes="-I$bg_dir/include"
      		fi
   	done

   	if test ! -z "$have_bgp_ar" -a ! -z "$have_bgp_hdr" ; then
      		# ac_with_readline="no"
		# Test to make sure the api is good
		saved_LDFLAGS="$LDFLAGS"
      	 	LDFLAGS="$saved_LDFLAGS $bg_ldflags -m64"
		AC_LINK_IFELSE([AC_LANG_PROGRAM([[ int rm_set_serial(char *); ]], [[ rm_set_serial(""); ]])],[have_bgp_files=yes],[AC_MSG_ERROR(There is a problem linking to the BG/P api.)])
		LDFLAGS="$saved_LDFLAGS"
   	fi

  	if test ! -z "$have_bgp_files" ; then
      		BG_INCLUDES="$bg_includes"
		CFLAGS="$CFLAGS -m64"
		CXXFLAGS="$CXXFLAGS $CFLAGS"
      		AC_DEFINE(HAVE_3D, 1, [Define to 1 if 3-dimensional architecture])
  		AC_DEFINE(SYSTEM_DIMENSIONS, 3, [3-dimensional architecture])
      		AC_DEFINE(HAVE_BG, 1, [Define to 1 if emulating or running on Blue Gene system])
		AC_DEFINE(HAVE_BG_L_P, 1, [Define to 1 if emulating or running on Blue Gene/L or P system])
      		AC_DEFINE(HAVE_BGP, 1, [Define to 1 if emulating or running on Blue Gene/P system])
      		AC_DEFINE(HAVE_FRONT_END, 1, [Define to 1 if running slurmd on front-end only])
		AC_DEFINE(HAVE_BG_FILES, 1, [Define to 1 if have Blue Gene files])
		AC_DEFINE_UNQUOTED(BG_BRIDGE_SO, "$soloc", [Define the BG_BRIDGE_SO value])

		AC_MSG_CHECKING(for BG serial value)
		bg_serial="BGP"
    		AC_ARG_WITH(bg-serial,, [bg_serial="$withval"])
     		AC_MSG_RESULT($bg_serial)
     		AC_DEFINE_UNQUOTED(BG_SERIAL, "$bg_serial", [Define the BG_SERIAL value])
 		#define ac_bluegene_loaded so we don't load another bluegene conf
		ac_bluegene_loaded=yes
		ac_real_bluegene_loaded=yes
	fi

   	AC_SUBST(BG_INCLUDES)
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
