##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#    Morris Jette <jette1@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_BG
#
#  DESCRIPTION:
#    Test for Blue Gene/L specific files. 
#    If found define HAVE_BG and HAVE_FRONT_END.
##*****************************************************************************


AC_DEFUN([X_AC_BGL],
[
   	AC_ARG_WITH(db2, AS_HELP_STRING(--with-db2-dir=PATH,Specify path to DB2 library's parent directory), [ trydb2dir=$withval ])

	# test for bluegene emulation mode
   	AC_ARG_ENABLE(bluegene-emulation, AS_HELP_STRING(--enable-bluegene-emulation,Run SLURM in bluegene mode on a non-bluegene system), 
	[ case "$enableval" in
	  yes) bluegene_emulation=yes ;;
	  no)  bluegene_emulation=no ;;
	  *)   AC_MSG_ERROR([bad value "$enableval" for --enable-bluegene-emulation])  ;;
    	esac ])	
 			
	if test "x$bluegene_emulation" = "xyes"; then
		AC_DEFINE(HAVE_BG, 1, [Define to 1 if emulating or running on Blue Gene system])
      		AC_DEFINE(HAVE_FRONT_END, 1, [Define to 1 if running slurmd on front-end only])
    		AC_MSG_NOTICE([Running in bluegene emulation mode])
		bg_default_dirs=""
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
      		AC_DEFINE(HAVE_BG, 1, [Define to 1 if emulating or running on Blue Gene system])
      		AC_DEFINE(HAVE_FRONT_END, 1, [Define to 1 if running slurmd on front-end only])
      		# ac_with_readline="no"
		# Test to make sure the api is good
                have_bg_files=yes
      		saved_LDFLAGS="$LDFLAGS"
      	 	LDFLAGS="$saved_LDFLAGS $bg_ldflags"
         	AC_LINK_IFELSE([AC_LANG_PROGRAM([[ int rm_set_serial(char *); ]], [[ rm_set_serial(""); ]])],[have_bg_files=yes],[AC_MSG_ERROR(There is a problem linking to the BG/L api.)])
		LDFLAGS="$saved_LDFLAGS"         	
   	fi

  	if test ! -z "$have_bg_files" ; then
      		BG_INCLUDES="$bg_includes"
		AC_DEFINE(HAVE_BG_FILES, 1, [Define to 1 if have Blue Gene files])

      		AC_DEFINE_UNQUOTED(BG_BRIDGE_SO, "$bg_bridge_so", [Define the BG_BRIDGE_SO value])
		AC_DEFINE_UNQUOTED(BG_DB2_SO, "$bg_db2_so", [Define the BG_DB2_SO value])
		AC_MSG_CHECKING(for BG serial value)
      		bg_serial="BGL"
      		AC_ARG_WITH(bg-serial,
         	AS_HELP_STRING(--with-bg-serial=NAME,set BG_SERIAL value [[BGL]]), [bg_serial="$withval"])
     		AC_MSG_RESULT($bg_serial)
     		AC_DEFINE_UNQUOTED(BG_SERIAL, "$bg_serial", [Define the BG_SERIAL value])	
   	fi

   	AC_SUBST(BG_INCLUDES)
])

AC_DEFUN([X_AC_BGP],
[
   	if test ! -z $HAVE_BG ; then 
		bg_default_dirs=""
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
         		have_bg_ar=yes
			bg_ldflags="$bg_ldflags -L$bg_dir/lib64 -L/usr/lib64 -Wl,--unresolved-symbols=ignore-in-shared-libs -l$libname"
        	fi
      
      		# Search for headers in the directory
      		if test -z "$have_bg_hdr" -a -f "$bg_dir/include/rm_api.h" ; then
         		have_bg_hdr=yes
         		bg_includes="-I$bg_dir/include"
      		fi
   	done
	
   	if test ! -z "$have_bg_ar" -a ! -z "$have_bg_hdr" ; then
      		AC_DEFINE(HAVE_BG, 1, [Define to 1 if emulating or running on Blue Gene system])
      		AC_DEFINE(HAVE_FRONT_END, 1, [Define to 1 if running slurmd on front-end only])
      		# ac_with_readline="no"
		# Test to make sure the api is good
                saved_LDFLAGS="$LDFLAGS"
      	 	LDFLAGS="$saved_LDFLAGS $bg_ldflags"
         	AC_LINK_IFELSE([AC_LANG_PROGRAM([[ int rm_set_serial(char *); ]], [[ rm_set_serial(""); ]])],[have_bg_files=yes],[AC_MSG_ERROR(There is a problem linking to the BG/P api.)])
		LDFLAGS="$saved_LDFLAGS"         	
   	fi

  	if test ! -z "$have_bg_files" ; then
      		BG_INCLUDES="$bg_includes"
		AC_DEFINE(HAVE_BG_FILES, 1, [Define to 1 if have Blue Gene files])
		AC_DEFINE(HAVE_BGP_FILES, 1, [Define to 1 if have BG/P files])

      		AC_DEFINE_UNQUOTED(BG_BRIDGE_SO, "$soloc", [Define the BG_BRIDGE_SO value])
		
		AC_MSG_CHECKING(for BG serial value)
        	bg_serial="BGP"
    		AC_ARG_WITH(bg-serial,
         	AS_HELP_STRING(--with-bg-serial=NAME,set BG_SERIAL value [[BGP]]), [bg_serial="$withval"])
     		AC_MSG_RESULT($bg_serial)
     		AC_DEFINE_UNQUOTED(BG_SERIAL, "$bg_serial", [Define the BG_SERIAL value])	
   	fi

   	AC_SUBST(BG_INCLUDES)
])
