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
#    Test for Blue Gene/L specific files. If found define HAVE_BG
##*****************************************************************************


AC_DEFUN([X_AC_BLUEGENE],
[
   AC_ARG_WITH(db2,
      AC_HELP_STRING([--with-db2-dir=PATH],
                     [Specify path to DB2 library's parent directory]),
      [ trydb2dir=$withval ]
   )

   bg_default_dirs="/bgl/BlueLight/ppcfloor/bglsys /opt/IBM/db2/V8.1 /u/bgdb2cli/sqllib /home/bgdb2cli/sqllib"

   for bg_dir in $trydb2dir "" $bg_default_dirs; do
      # Skip directories that don't exist
      if test ! -z "$bg_dir" -a ! -d "$bg_dir" ; then
         continue;
      fi

      # Search for required BG API libraries in the directory
      if test -z "$have_bg_ar" -a -f "$bg_dir/lib/bglbootload.a" -a -f "$bg_dir/lib/bglsp440supt.a" ; then
         if test ! -f "$bg_dir/lib64/libbglbridge_s.a" ; then
            # Establish a link as required. Libtool requires the "lib" prefix
            # to function properly. See 
            # "How to use --whole-archive arg with libtool"
            # http://www.mail-archive.com/libtool@gnu.org/msg02792.html
            AC_MSG_ERROR([$bg_dir/lib64/libbglbridge_s.a is required and does not exist])
         fi

         have_bg_ar=yes
         bg_ldflags="$bg_ldflags -Wl,-rpath $bg_dir/lib64 -Wl,-L$bg_dir/lib64 -Wl,-whole-archive -Wl,-lbglbridge_s -Wl,-no-whole-archive $bg_dir/lib/bglbootload.a $bg_dir/lib/bglsp440supt.a -lsaymessage -lbgldb -lbglmachine -ltableapi -Wl,-rpath /usr/lib64 -L/usr/lib64 -lexpat"
      
      fi
      
      # Search for required DB2 library in the directory
      if test -z "$have_db2" -a -f "$bg_dir/lib64/libdb2.so" ; then
         have_db2=yes
         bg_ldflags="$bg_ldflags -Wl,-rpath $bg_dir/lib64 -L$bg_dir/lib64 -ldb2"
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
      ac_with_readline="no"

      saved_LDFLAGS="$LDFLAGS"
      LDFLAGS="$saved_LDFLAGS $bg_ldflags"
      AC_TRY_LINK(
	[ int rm_set_serial(char *); ],
	[ rm_set_serial(""); ],
	have_bg_files=yes, [])
      LDFLAGS="$saved_LDFLAGS"
   fi
   if test ! -z "$have_bg_files" ; then
      BG_INCLUDES="$bg_includes"
      BG_LDFLAGS="$bg_ldflags"
      AC_DEFINE(HAVE_BG_FILES, 1, [Define to 1 if have Blue Gene files])

      AC_MSG_CHECKING(for BG serial value)
      bg_serial="BGL"
      AC_ARG_WITH(bg-serial,
         AC_HELP_STRING([--with-bg-serial=NAME],
            [set BG_SERIAL value [[BGL]]]),
         [bg_serial="$withval"]
      )
     AC_MSG_RESULT($bg_serial)
     AC_DEFINE_UNQUOTED(BG_SERIAL, "$bg_serial", 
        [Define the BG_SERIAL value])
   fi

   AC_SUBST(BG_INCLUDES)
   AC_SUBST(BG_LDFLAGS)
])
