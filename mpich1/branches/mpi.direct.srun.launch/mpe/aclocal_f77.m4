dnl
dnl/*D
dnl PAC_PROG_F77_NAME_MANGLE - Determine how the Fortran compiler mangles
dnl names 
dnl
dnl Synopsis:
dnl PAC_PROG_F77_NAME_MANGLE([action])
dnl
dnl Output Effect:
dnl If no action is specified, one of the following names is defined:
dnl.vb
dnl If fortran names are mapped:
dnl   lower -> lower                  F77_NAME_LOWER
dnl   lower -> lower_                 F77_NAME_LOWER_USCORE
dnl   lower -> UPPER                  F77_NAME_UPPER
dnl   lower_lower -> lower__          F77_NAME_LOWER_2USCORE
dnl   mixed -> mixed                  F77_NAME_MIXED
dnl   mixed -> mixed_                 F77_NAME_MIXED_USCORE
dnl.ve
dnl If an action is specified, it is executed instead.
dnl 
dnl Notes:
dnl We assume that if lower -> lower (any underscore), upper -> upper with the
dnl same underscore behavior.  Previous versions did this by 
dnl compiling a Fortran program and running strings -a over it.  Depending on 
dnl strings is a bad idea, so instead we try compiling and linking with a 
dnl C program, since that is why we are doing this anyway.  A similar approach
dnl is used by FFTW, though without some of the cases we check (specifically, 
dnl mixed name mangling)
dnl
dnlD*/
dnl
AC_DEFUN(PAC_PROG_F77_NAME_MANGLE,[
AC_CACHE_CHECK([for Fortran 77 name mangling],
pac_cv_prog_f77_name_mangle,
[
   # Check for strange behavior of Fortran.  For example, some FreeBSD
   # systems use f2c to implement f77, and the version of f2c that they 
   # use generates TWO (!!!) trailing underscores
   # Currently, WDEF is not used but could be...
   #
   # Eventually, we want to be able to override the choices here and
   # force a particular form.  This is particularly useful in systems
   # where a Fortran compiler option is used to force a particular
   # external name format (rs6000 xlf, for example).
   rm -f conftest*
   cat > conftest.f <<EOF
       subroutine MY_name( a )
       return
       end
EOF
   # This is the ac_compile line used if LANG_FORTRAN77 is selected
   if test "X$ac_fcompile" = "X" ; then
       ac_fcompile='${F77-f77} -c $FFLAGS conftest.f 1>&AC_FD_CC'
   fi
   if AC_TRY_EVAL(ac_fcompile) && test -s conftest.o ; then
	mv conftest.o fconftestf.o
   else 
	echo "configure: failed program was:" >&AC_FD_CC
        cat conftest.f >&AC_FD_CC
   fi

   AC_LANG_SAVE
   AC_LANG_C   
   save_LIBS="$LIBS"
   dnl FLIBS comes from AC_F77_LIBRARY_LDFLAGS
   LIBS="fconftestf.o $FLIBS $LIBS"
   AC_TRY_LINK(,my_name();,pac_cv_prog_f77_name_mangle="lower")
   if test  "X$pac_cv_prog_f77_name_mangle" = "X" ; then
     AC_TRY_LINK(,my_name_();,pac_cv_prog_f77_name_mangle="lower underscore")
   fi
   if test  "X$pac_cv_prog_f77_name_mangle" = "X" ; then
     AC_TRY_LINK(,MY_NAME();,pac_cv_prog_f77_name_mangle="upper")
   fi
   if test  "X$pac_cv_prog_f77_name_mangle" = "X" ; then
     AC_TRY_LINK(,my_name__();,
       pac_cv_prog_f77_name_mangle="lower doubleunderscore")
   fi
   if test  "X$pac_cv_prog_f77_name_mangle" = "X" ; then
     AC_TRY_LINK(,MY_name();,pac_cv_prog_f77_name_mangle="mixed")
   fi
   if test  "X$pac_cv_prog_f77_name_mangle" = "X" ; then
     AC_TRY_LINK(,MY_name_();,pac_cv_prog_f77_name_mangle="mixed underscore")
   fi
   LIBS="$save_LIBS"
   AC_LANG_RESTORE
   rm -f fconftest*
])
# Make the actual definition
pac_namecheck=`echo X$pac_cv_prog_f77_name_mangle | sed 's/ /-/g'`
ifelse([$1],,[
case $pac_namecheck in
    X) AC_MSG_WARN([Cannot determine Fortran naming scheme]) ;;
    Xlower) AC_DEFINE(F77_NAME_LOWER) ;;
    Xlower-underscore) AC_DEFINE(F77_NAME_LOWER_USCORE) ;;
    Xlower-doubleunderscore) AC_DEFINE(F77_NAME_LOWER_2USCORE) ;;
    Xupper) AC_DEFINE(F77_NAME_UPPER) ;;
    Xmixed) AC_DEFINE(F77_NAME_MIXED) ;;
    Xmixed-underscore) AC_DEFINE(F77_NAME_MIXED_USCORE) ;;
    *) AC_MSG_WARN([Unknown Fortran naming scheme]) ;;
esac
],[$1])
])
dnl
dnl/*D
dnl PAC_PROG_F77_CHECK_SIZEOF - Determine the size in bytes of a Fortran
dnl type
dnl
dnl Synopsis:
dnl PAC_PROG_F77_CHECK_SIZEOF(type,[cross-size])
dnl
dnl Output Effect:
dnl Sets SIZEOF_F77_uctype to the size if bytes of type.
dnl If type is unknown, the size is set to 0.
dnl If cross-compiling, the value cross-size is used (it may be a variable)
dnl For example 'PAC_PROG_F77_CHECK_SIZEOF(real)' defines
dnl 'SIZEOF_F77_REAL' to 4 on most systems.  The variable 
dnl 'pac_cv_sizeof_f77_<type>' (e.g., 'pac_cv_sizeof_f77_real') is also set to
dnl the size of the type. 
dnl If the corresponding variable is already set, that value is used.
dnl If the name has an '*' in it (e.g., 'integer*4'), the defined name 
dnl replaces that with an underscore (e.g., 'SIZEOF_F77_INTEGER_4').
dnl
dnl Notes:
dnl If the 'cross-size' argument is not given, 'autoconf' will issue an error
dnl message.  You can use '0' to specify undetermined.
dnl
dnlD*/
AC_DEFUN(PAC_PROG_F77_CHECK_SIZEOF,[
changequote(<<, >>)dnl
dnl The name to #define.
dnl If the arg value contains a variable, we need to update that
define(<<PAC_TYPE_NAME>>, translit(sizeof_f77_$1, [a-z *], [A-Z__]))dnl
dnl The cache variable name.
define(<<PAC_CV_NAME>>, translit(pac_cv_f77_sizeof_$1, [ *], [__]))dnl
changequote([, ])dnl
AC_CACHE_CHECK([for size of Fortran type $1],PAC_CV_NAME,[
AC_REQUIRE([PAC_PROG_F77_NAME_MANGLE])
rm -f conftest*
cat <<EOF > conftest.f
      subroutine isize( )
      $1 i(2)
      call cisize( i(1), i(2) )
      end
EOF
if test "X$ac_fcompile" = "X" ; then
    ac_fcompile='${F77-f77} -c $FFLAGS conftest.f 1>&AC_FD_CC'
fi
if AC_TRY_EVAL(ac_fcompile) && test -s conftest.o ; then
    mv conftest.o conftestf.o
    AC_LANG_SAVE
    AC_LANG_C
    save_LIBS="$LIBS"
    dnl Add the Fortran linking libraries
    LIBS="conftestf.o $FLIBS $LIBS"
    AC_TRY_RUN([#include <stdio.h>
#ifdef F77_NAME_UPPER
#define cisize_ CISIZE
#define isize_ ISIZE
#elif defined(F77_NAME_LOWER) || defined(F77_NAME_MIXED)
#define cisize_ cisize
#define isize_ isize
#endif
static int isize_val;
void cisize_(char *i1p, char *i2p)
{ 
   isize_val = (int)(i2p - i1p);
}
main()
{
    FILE *f = fopen("conftestval", "w");
    if (!f) exit(1);
    isize_();
    fprintf(f,"%d\n", isize_val );
    exit(0);
}], eval PAC_CV_NAME=`cat conftestval`,eval PAC_CV_NAME=0,
ifelse([$2],,,eval PAC_CV_NAME=$2))
    LIBS="$save_LIBS"
    AC_LANG_RESTORE
else 
    echo "configure: failed program was:" >&AC_FD_CC
    cat conftest.f >&AC_FD_CC
    ifelse([$2],,eval PAC_CV_NAME=0,eval PAC_CV_NAME=$2)
fi
])
AC_DEFINE_UNQUOTED(PAC_TYPE_NAME,$PAC_CV_NAME)
undefine([PAC_TYPE_NAME])
undefine([PAC_CV_NAME])
])
dnl
dnl/*D
dnl PAC_PROG_F77_EXCLAIM_COMMENTS
dnl
dnl Synopsis:
dnl PAC_PROG_F77_EXCLAIM_COMMENTS([action-if-true],[action-if-false])
dnl
dnl Notes:
dnl Check whether '!' may be used to begin comments in Fortran.
dnl
dnl This macro requires a version of autoconf `after` 2.13; the 'acgeneral.m4'
dnl file contains an error in the handling of Fortran programs in 
dnl 'AC_TRY_COMPILE' (fixed in our local version).
dnl
dnlD*/
AC_DEFUN(PAC_PROG_F77_EXCLAIM_COMMENTS,[
AC_CACHE_CHECK([whether Fortran accepts ! for comments],
pac_cv_prog_f77_exclaim_comments,[
AC_LANG_SAVE
AC_LANG_FORTRAN77
AC_TRY_COMPILE(,[
!      This is a comment
],pac_cv_prog_f77_exclaim_comments="yes",
pac_cv_prog_f77_exclaim_comments="no")
AC_LANG_RESTORE
])
if test "$pac_cv_prog_f77_exclaim_comments" = "yes" ; then
    ifelse([$1],,:,$1)
else
    ifelse([$2],,:,$2)
fi
])dnl
dnl
dnl/*D
dnl PAC_F77_CHECK_COMPILER_OPTION - Check that a compiler option is accepted
dnl without warning messages
dnl
dnl Synopsis:
dnl PAC_F77_CHECK_COMPILER_OPTION(optionname,action-if-ok,action-if-fail)
dnl
dnl Output Effects:
dnl
dnl If no actions are specified, a working value is added to 'FOPTIONS'
dnl
dnl Notes:
dnl This is now careful to check that the output is different, since 
dnl some compilers are noisy.
dnl 
dnl We are extra careful to prototype the functions in case compiler options
dnl that complain about poor code are in effect.
dnl
dnl Because this is a long script, we have ensured that you can pass a 
dnl variable containing the option name as the first argument.
dnlD*/
AC_DEFUN(PAC_F77_CHECK_COMPILER_OPTION,[
AC_MSG_CHECKING([that Fortran 77 compiler accepts option $1])
ac_result="no"
save_FFLAGS="$FFLAGS"
FFLAGS="$1 $FFLAGS"
rm -f conftest.out
cat >conftest2.f <<EOF
        subroutine try()
        end
EOF
cat >conftest.f <<EOF
        program main
        end
EOF
dnl It is important to use the AC_TRY_EVAL in case F77 is not a single word
dnl but is something like "f77 -64" (where the switch has changed the
dnl compiler)
ac_fscompilelink='${F77-f77} $save_FFLAGS -o conftest conftest.f $LDFLAGS >conftest.bas 2>&1'
ac_fscompilelink2='${F77-f77} $FFLAGS -o conftest conftest.f $LDFLAGS >conftest.out 2>&1'
if AC_TRY_EVAL(ac_fscompilelink) && test -x conftest ; then
   if AC_TRY_EVAL(ac_fscompilelink2) && test -x conftest ; then
      if diff -b conftest.out conftest.bas >/dev/null 2>&1 ; then
         AC_MSG_RESULT(yes)
         AC_MSG_CHECKING([that routines compiled with $1 can be linked with ones compiled  without $1])       
         rm -f conftest2.out
         rm -f conftest.bas
	 ac_fscompile3='${F77-f77} -c $save_FFLAGS conftest2.f >conftest2.out 2>&1'
	 ac_fscompilelink4='${F77-f77} $FFLAGS -o conftest conftest2.o conftest.f $LDFLAGS >conftest.bas 2>&1'
         if AC_TRY_EVAL(ac_fscompile3) && test -s conftest2.o ; then
            if AC_TRY_EVAL(ac_fscompilelink4) && test -x conftest ; then
               if diff -b conftest.out conftest.bas >/dev/null 2>&1 ; then
	          ac_result="yes"
	       else 
		  echo "configure: Compiler output differed in two cases" >&AC_FD_CC
                  diff -b conftest.out conftest.bas >&AC_FD_CC
	       fi
	    else
	       echo "configure: failed program was:" >&AC_FD_CC
	       cat conftest.f >&AC_FD_CC
	    fi
	  else
	    echo "configure: failed program was:" >&AC_FD_CC
	    cat conftest2.f >&AC_FD_CC
	  fi
      else
	# diff
        echo "configure: Compiler output differed in two cases" >&AC_FD_CC
        diff -b conftest.out conftest.bas >&AC_FD_CC
      fi
   else
      # try_eval(fscompilelink2)
      echo "configure: failed program was:" >&AC_FD_CC
      cat conftest.f >&AC_FD_CC
   fi
   if test "$ac_result" != "yes" -a -s conftest.out ; then
	cat conftest.out >&AC_FD_CC
   fi
else
    # Could not compile without the option!
    echo "configure: Could not compile program" >&AC_FD_CC
    cat conftest.f >&AC_FD_CC
    cat conftest.bas >&AC_FD_CC
fi
if test "$ac_result" = "yes" ; then
     AC_MSG_RESULT(yes)	  
     ifelse([$2],,FOPTIONS="$FOPTIONS $1",$2)
else
     AC_MSG_RESULT(no)
     $3
fi
FFLAGS="$save_FFLAGS"
rm -f conftest*
])
dnl
dnl/*D
dnl PAC_PROG_F77_CMDARGS - Determine how to access the command line from
dnl Fortran 77
dnl
dnl Output Effects:
dnl  The following variables are set:
dnl.vb
dnl    F77_GETARG         - Statement to get an argument i into string s
dnl    F77_IARGC          - Routine to return the number of arguments
dnl    FXX_MODULE         - Module command when using Fortran 90 compiler
dnl    F77_GETARGDECL     - Declaration of routine used for F77_GETARG
dnl    F77_GETARG_FFLAGS  - Flags needed when compiling/linking
dnl    F77_GETARG_LDFLAGS - Flags needed when linking
dnl.ve
dnl If 'F77_GETARG' has a value, then that value and the values for these
dnl other symbols will be used instead.  If no approach is found, all of these
dnl variables will have empty values.
dnl If no other approach works and a file 'f77argdef' is in the directory, 
dnl that file will be sourced for the values of the above four variables.
dnl
dnl In most cases, you should add F77_GETARG_FFLAGS to the FFLAGS variable
dnl and F77_GETARG_LDFLAGS to the LDFLAGS variable, to ensure that tests are
dnl performed on the compiler version that will be used.
dnl
dnl 'AC_SUBST' is called for all six variables.
dnl
dnl One complication is that on systems with multiple Fortran compilers, 
dnl some libraries used by one Fortran compiler may have been (mis)placed
dnl in a common location.  We have had trouble with libg2c in particular.
dnl To work around this, we test whether iargc etc. work first.  This
dnl will catch most systems and will speed up the tests.
dnl
dnl Next, the libraries are only added if they are needed to complete a 
dnl link; they aren't added just because they exist.
dnl
dnl f77argdef
dnlD*/
dnl
dnl Random notes
dnl You can export the command line arguments from C to the g77 compiler
dnl using
dnl    extern char **__libc_argv;
dnl    extern int  __libc_argc;
dnl    f_setarg( __libc_argc, __libc_argv );
dnl
AC_DEFUN(PAC_PROG_F77_CMDARGS,[
found_cached="yes"
AC_MSG_CHECKING([for routines to access the command line from Fortran 77])
AC_CACHE_VAL(pac_cv_prog_f77_cmdarg,
[
    AC_MSG_RESULT([searching...])
    found_cached="no"
    # First, we perform a quick check.  Does iargc and getarg work?
    fxx_module="${FXX_MODULE:-}"
    f77_getargdecl="${F77_GETARGDECL:-external getarg}"
    f77_getarg="${F77_GETARG:-call GETARG(i,s)}"
    f77_iargc="${F77_IARGC:-IARGC()}"
    #    
    # Grumble.  The Absoft Fortran compiler computes i - i as 0 and then
    # 1.0 / 0 at compile time, even though the code may never be executed.
    # What we need is a way to generate an error, so the second usage of i
    # was replaced with f77_iargc.  
    cat > conftest.f <<EOF
        program main
$fxx_module
        integer i, j
        character*20 s
        $f77_getargdecl
        i = 0
        $f77_getarg
        i=$f77_iargc
        if (i .gt. 1) then
            j = i - $f77_iargc
            j = 1.0 / j
        endif
        end
EOF
    found_answer="no"
    if test -z "$ac_fcompilelink" ; then
        ac_fcompilelink="${F77-f77} -o conftest $FFLAGS $flags conftest.f $LDFLAGS $LIBS 1>&AC_FD_CC"
    fi
    AC_MSG_CHECKING([if ${F77-f77} $flags $libs works with GETARG and IARGC])
    if AC_TRY_EVAL(ac_fcompilelink) && test -x conftest ; then
	if test "$ac_cv_prog_f77_cross" = "no" ; then
	    if ./conftest >/dev/null 2>&1 ; then
		found_answer="yes"
	        FXX_MODULE="$fxx_module"
		F77_GETARGDECL="$f77_getargdecl"
		F77_GETARG="$f77_getarg"
		F77_IARGC="$f77_iargc"
		AC_MSG_RESULT(yes)
     	    fi
        fi
    fi    
    if test $found_answer = "no" ; then
	AC_MSG_RESULT(no)
    # Grumph.  Here are a bunch of different approaches
    # We have several axes the check:
    # Library to link with (none, -lU77 (HPUX), -lg2c (LINUX f77))
    # PEPCF90 (Intel ifc)
    # The first line is a dummy
    # (we experimented with using a <space>, but this caused other 
    # problems because we need <space> in the IFS)
    trial_LIBS="0 -lU77 -lPEPCF90"
    if test "$NOG2C" != "1" ; then
        trial_LIBS="$trial_LIBS -lg2c"
    fi
    # Discard libs that are not availble:
    save_IFS="$IFS"
    # Make sure that IFS includes a space, or the tests that run programs
    # may fail
    IFS=" 
"
    save_trial_LIBS="$trial_LIBS"
    trial_LIBS=""
    cat > conftest.f <<EOF
        program main
        end
EOF
    ac_fcompilelink_test='${F77-f77} -o conftest $FFLAGS conftest.f $LDFLAGS $libs $LIBS 1>&AC_FD_CC'
    for libs in $save_trial_LIBS ; do
	if test "$libs" = "0" ; then
	    lib_ok="yes"
        else
	    AC_MSG_CHECKING([whether Fortran 77 links with $libs])
	    if AC_TRY_EVAL(ac_fcompilelink_test) && test -x conftest ; then
		AC_MSG_RESULT([yes])
	        lib_ok="yes"
	    else
		AC_MSG_RESULT([no])
	        lib_ok="no"
	    fi
	fi
	if test "$lib_ok" = "yes" ; then
	    trial_LIBS="$trial_LIBS
$libs"
        fi
    done

    # Options to use when compiling and linking
    # +U77 is needed by HP Fortran to access getarg etc.
    # The -N109 was used for getarg before we realized that GETARG
    # was necessary with the (non standard conforming) Absoft compiler
    # (Fortran is monocase; Absoft uses mixedcase by default)
    # The -f is used by Absoft and is the compiler switch that folds 
    # symbolic names to lower case.  Without this option, the compiler
    # considers upper- and lower-case letters to be unique.
    # The -YEXT_NAMES=LCS will cause external names to be output as lower
    # case letter for Absoft F90 compilers (default is upper case)
    # The first line is "<space><newline>, the space is important
    # To make the Absoft f77 and f90 work together, we need to prefer the
    # upper case versions of the arguments.  They also require libU77.
    # -YCFRL=1 causes Absoft f90 to work with g77 and similar (f2c-based) 
    # Fortran compilers
    #
    # Problem:  The Intel efc compiler hangs when presented with -N109 .
    # The only real fix for this is to detect this compiler and exclude
    # the test.  We may want to reorganize these tests so that if we
    # can compile code without special options, we never look for them.
    # 
    using_intel_efc="no"
    pac_test_msg=`$F77 -V 2>&1 | grep 'Intel(R) Fortran Itanium'`
    if test "$pac_test_msg" != "" ; then
	using_intel_efc="yes"
    fi
    if test "$using_intel_efc" = "yes" ; then
        trial_FLAGS="000"
    else
        trial_FLAGS="000
-N109
-f
-YEXT_NAMES=UCS
-YEXT_NAMES=LCS
-YCFRL=1
+U77"
    fi
    # Discard options that are not available:
    save_IFS="$IFS"
    IFS=" 
"
    save_trial_FLAGS="$trial_FLAGS"
    trial_FLAGS=""
    for flag in $save_trial_FLAGS ; do
	if test "$flag" = " " -o "$flag" = "000" ; then
	    opt_ok="yes"
        else
            PAC_F77_CHECK_COMPILER_OPTION($flag,opt_ok=yes,opt_ok=no)
        fi
	if test "$opt_ok" = "yes" ; then
	    if test "$flag" = " " -o "$flag" = "000" ; then 
		fflag="" 
	    else 
		fflag="$flag" 
	    fi
	    # discard options that don't allow mixed-case name matching
	    cat > conftest.f <<EOF
        program main
        call aB()
        end
        subroutine Ab()
        end
EOF
	    if test -n "$fflag" ; then flagval="with $fflag" ; else flagval="" ; fi
	    AC_MSG_CHECKING([that Fortran 77 routine names are case-insensitive $flagval])
	    dnl we can use double quotes here because all is already
            dnl evaluated
            ac_fcompilelink_test="${F77-f77} -o conftest $fflag $FFLAGS conftest.f $LDFLAGS $LIBS 1>&AC_FD_CC"
	    if AC_TRY_EVAL(ac_fcompilelink_test) && test -x conftest ; then
	        AC_MSG_RESULT(yes)
	    else
	        AC_MSG_RESULT(no)
	        opt_ok="no"
            fi
        fi
        if test "$opt_ok" = "yes" ; then
	    trial_FLAGS="$trial_FLAGS
$flag"
        fi
    done
    IFS="$save_IFS"
    # Name of routines.  Since these are in groups, we use a case statement
    # and loop until the end (accomplished by reaching the end of the
    # case statement
    # For one version of Nag F90, the names are 
    # call f90_unix_MP_getarg(i,s) and f90_unix_MP_iargc().
    trial=0
    while test -z "$pac_cv_prog_f77_cmdarg" ; do
        case $trial in 
	0) # User-specified values, if any
	   if test -z "$F77_GETARG" -o -z "$F77_IARGC" ; then 
	       trial=`expr $trial + 1`
	       continue 
           fi
           MSG="Using environment values of F77_GETARG etc."
	   ;;
	1) # Standard practice, uppercase (some compilers are case-sensitive)
	   FXX_MODULE=""
	   F77_GETARGDECL="external GETARG"
	   F77_GETARG="call GETARG(i,s)"
	   F77_IARGC="IARGC()"
	   MSG="GETARG and IARGC"
	   ;;
	2) # Standard practice, lowercase
	   FXX_MODULE=""
           F77_GETARGDECL="external getarg"
	   F77_GETARG="call getarg(i,s)"
	   F77_IARGC="iargc()"
	   MSG="getarg and iargc"
	   ;;
	3) # Posix alternative
	   FXX_MODULE=""
	   F77_GETARGDECL="external pxfgetarg"
	   F77_GETARG="call pxfgetarg(i,s,l,ier)"
	   F77_IARGC="ipxfargc()"
	   MSG="pxfgetarg and ipxfargc"
	   ;;
	4) # Nag f90_unix_env module
	   FXX_MODULE="        use f90_unix_env"
	   F77_GETARGDECL=""
	   F77_GETARG="call getarg(i,s)"
	   F77_IARGC="iargc()"
	   MSG="f90_unix_env module"
	   ;;
        5) # Nag f90_unix module
	   FXX_MODULE="        use f90_unix"
	   F77_GETARGDECL=""
	   F77_GETARG="call getarg(i,s)"
	   F77_IARGC="iargc()"
	   MSG="f90_unix module"
	   ;;
	6) # user spec in a file
	   if test -s f77argdef ; then
		. ./f77argdef
	       MSG="Using definitions in the file f77argdef"
	   else
	        trial=`expr $trial + 1`
		continue
	   fi
	   ;;
        *) # exit from while loop
	   FXX_MODULE=""
	   F77_GETARGDECL=""
	   F77_GETARG=""
	   F77_IARGC=""
           break
	   ;;
	esac
	# Create the program.  Make sure that we can run it.
	# Force a divide-by-zero if there is a problem (but only at runtime!
        # (the Absoft compiler does divide-by-zero at compile time)
        cat > conftest.f <<EOF
        program main
$FXX_MODULE
        integer i, j
        character*20 s
        $F77_GETARGDECL
        i = 0
        $F77_GETARG
        i=$F77_IARGC
        if (i .gt. 1) then
            j = i - $F77_IARGC
            j = 1.0 / j
        endif
        end
EOF
    #
    # Now, try to find some way to compile and link that program, looping 
    # over the possibilities of options and libraries
        save_IFS="$IFS"
        IFS=" 
"
        for libs in $trial_LIBS ; do
            if test -n "$pac_cv_prog_f77_cmdarg" ; then break ; fi
	    if test "$libs" = " " -o "$libs" = "0" ; then libs="" ; fi
            for flags in $trial_FLAGS ; do
	        if test "$flags" = " " -o "$flags" = "000"; then flags="" ; fi
                AC_MSG_CHECKING([if ${F77-f77} $flags $libs works with $MSG])
		IFS="$save_IFS"
		dnl We need this here because we've fiddled with IFS
	        ac_fcompilelink_test="${F77-f77} -o conftest $FFLAGS $flags conftest.f $LDFLAGS $libs $LIBS 1>&AC_FD_CC"
		found_answer="no"
                if AC_TRY_EVAL(ac_fcompilelink_test) && test -x conftest ; then
		    if test "$ac_cv_prog_f77_cross" = "no" ; then
			if ./conftest >/dev/null 2>&1 ; then
			    found_answer="yes"
			fi
		    else 
			found_answer="yes"
		    fi
                fi
	        IFS=" 
"
		if test "$found_answer" = "yes" ; then
	            AC_MSG_RESULT([yes])
		    pac_cv_prog_f77_cmdarg="$MSG"
		    pac_cv_prog_f77_cmdarg_fflags="$flags"
		    pac_cv_prog_f77_cmdarg_ldflags="$libs"
		    break
	        else
                    AC_MSG_RESULT([no])
		    echo "configure: failed program was:" >&AC_FD_CC
                    cat conftest.f >&AC_FD_CC
	        fi
            done
        done
        IFS="$save_IFS"   
	rm -f conftest.*
        trial=`expr $trial + 1`   
    done
fi
pac_cv_F77_GETARGDECL="$F77_GETARGDECL"
pac_cv_F77_IARGC="$F77_IARGC"
pac_cv_F77_GETARG="$F77_GETARG"
pac_cv_FXX_MODULE="$FXX_MODULE"
])
if test "$found_cached" = "yes" ; then 
    AC_MSG_RESULT([$pac_cv_prog_f77_cmdarg])
elif test -z "$pac_cv_F77_IARGC" ; then
    AC_MSG_WARN([Could not find a way to access the command line from Fortran 77])
fi
# Set the variable values based on pac_cv_prog_xxx
F77_GETARGDECL="$pac_cv_F77_GETARGDECL"
F77_IARGC="$pac_cv_F77_IARGC"
F77_GETARG="$pac_cv_F77_GETARG"
FXX_MODULE="$pac_cv_FXX_MODULE"
F77_GETARG_FFLAGS="$pac_cv_prog_f77_cmdarg_fflags"
F77_GETARG_LDFLAGS="$pac_cv_prog_f77_cmdarg_ldflags"
AC_SUBST(F77_GETARGDECL)
AC_SUBST(F77_IARGC)
AC_SUBST(F77_GETARG)
AC_SUBST(FXX_MODULE)
AC_SUBST(F77_GETARG_FFLAGS)
AC_SUBST(F77_GETARG_LDFLAGS)
])
dnl/*D
dnl PAC_PROG_F77_LIBRARY_DIR_FLAG - Determine the flag used to indicate
dnl the directories to find libraries in
dnl
dnl Notes:
dnl Many compilers accept '-Ldir' just like most C compilers.  
dnl Unfortunately, some (such as some HPUX Fortran compilers) do not, 
dnl and require instead either '-Wl,-L,dir' or something else.  This
dnl command attempts to determine what is accepted.  The flag is 
dnl placed into 'F77_LIBDIR_LEADER'.
dnl
dnlD*/
AC_DEFUN(PAC_PROG_F77_LIBRARY_DIR_FLAG,[
if test "X$F77_LIBDIR_LEADER" = "X" ; then
AC_CACHE_CHECK([for Fortran 77 flag for library directories],
pac_cv_prog_f77_library_dir_flag,
[
    rm -f conftest.*
    cat > conftest.f <<EOF
        program main
        end
EOF
    ac_fcompileldtest='${F77-f77} -o conftest $FFLAGS ${ldir}. conftest.f $LDFLAGS 1>&AC_FD_CC'
    for ldir in "-L" "-Wl,-L," ; do
        if AC_TRY_EVAL(ac_fcompileldtest) && test -s conftest ; then
	    pac_cv_prog_f77_library_dir_flag="$ldir"
	    break
       fi
    done
    rm -f conftest*
])
    AC_SUBST(F77_LIBDIR_LEADER)
    if test "X$pac_cv_prog_f77_library_dir_flag" != "X" ; then
        F77_LIBDIR_LEADER="$pac_cv_prog_f77_library_dir_flag"
    fi
fi
])
dnl/*D 
dnl PAC_PROG_F77_HAS_INCDIR - Check whether Fortran accepts -Idir flag
dnl
dnl Syntax:
dnl   PAC_PROG_F77_HAS_INCDIR(directory,action-if-true,action-if-false)
dnl
dnl Output Effect:
dnl  Sets 'F77_INCDIR' to the flag used to choose the directory.  
dnl
dnl Notes:
dnl This refers to the handling of the common Fortran include extension,
dnl not to the use of '#include' with the C preprocessor.
dnl If directory does not exist, it will be created.  In that case, the 
dnl directory should be a direct descendant of the current directory.
dnl
dnlD*/ 
AC_DEFUN(PAC_PROG_F77_HAS_INCDIR,[
checkdir=$1
AC_CACHE_CHECK([for include directory flag for Fortran],
pac_cv_prog_f77_has_incdir,[
if test ! -d $checkdir ; then mkdir $checkdir ; fi
cat >$checkdir/conftestf.h <<EOF
       call sub()
EOF
cat >conftest.f <<EOF
       program main
       include 'conftestf.h'
       end
EOF

ac_fcompiletest='${F77-f77} -c $FFLAGS ${idir}$checkdir conftest.f 1>&AC_FD_CC'
pac_cv_prog_f77_has_incdir="none"
# SGI wants -Wf,-I
for idir in "-I" "-Wf,-I" ; do
    if AC_TRY_EVAL(ac_fcompiletest) && test -s conftest.o ; then
        pac_cv_prog_f77_has_incdir="$idir"
	break
    fi
done
rm -f conftest*
rm -f $checkdir/conftestf.h
])
AC_SUBST(F77_INCDIR)
if test "X$pac_cv_prog_f77_has_incdir" != "Xnone" ; then
    F77_INCDIR="$pac_cv_prog_f77_has_incdir"
fi
])
dnl
dnl/*D
dnl PAC_PROG_F77_ALLOWS_UNUSED_EXTERNALS - Check whether the Fortran compiler
dnl allows unused and undefined functions to be listed in an external 
dnl statement
dnl
dnl Syntax:
dnl   PAC_PROG_F77_ALLOWS_UNUSED_EXTERNALS(action-if-true,action-if-false)
dnl
dnlD*/
AC_DEFUN(PAC_PROG_F77_ALLOWS_UNUSED_EXTERNALS,[
AC_CACHE_CHECK([whether Fortran allows unused externals],
pac_cv_prog_f77_allows_unused_externals,[
AC_LANG_SAVE
AC_LANG_FORTRAN77
dnl We can't use TRY_LINK, because it wants a routine name, not a 
dnl declaration.  The following is the body of TRY_LINK, slightly modified.
cat > conftest.$ac_ext <<EOF
       program main
       external bar
       end
EOF
if AC_TRY_EVAL(ac_link) && test -s conftest${ac_exeext}; then
  rm -rf conftest*
  pac_cv_prog_f77_allows_unused_externals="yes"
else
  echo "configure: failed program was:" >&AC_FD_CC
  cat conftest.$ac_ext >&AC_FD_CC
  rm -rf conftest*
  pac_cv_prog_f77_allows_unused_externals="no"
  $4
fi
rm -f conftest*
#
AC_LANG_RESTORE
])
if test "X$pac_cv_prog_f77_allows_unused_externals" = "Xyes" ; then
   ifelse([$1],,:,[$1])
else
   ifelse([$2],,:,[$2])
fi
])
dnl/*D 
dnl PAC_PROG_F77_HAS_POINTER - Determine if Fortran allows pointer type
dnl
dnl Synopsis:
dnl   PAC_PROG_F77_HAS_POINTER(action-if-true,action-if-false)
dnlD*/
AC_DEFUN(PAC_PROG_F77_HAS_POINTER,[
AC_CACHE_CHECK([whether Fortran has pointer declaration],
pac_cv_prog_f77_has_pointer,[
AC_LANG_SAVE
AC_LANG_FORTRAN77
AC_TRY_COMPILE(,[
        integer M
        pointer (MPTR,M)
        data MPTR/0/
],pac_cv_prog_f77_has_pointer="yes",pac_cv_prog_f77_has_pointer="no")
AC_LANG_RESTORE
])
if test "$pac_cv_prog_f77_has_pointer" = "yes" ; then
    ifelse([$1],,:,[$1])
else
    ifelse([$2],,:,[$2])
fi
])
dnl
dnl pac_prog_f77_run_proc_from_c( c main program, fortran routine, 
dnl                               action-if-works, action-if-fails, 
dnl                               cross-action )
dnl Fortran routine MUST be named ftest unless you include code
dnl to select the appropriate Fortran name.
dnl 
AC_DEFUN(PAC_PROG_F77_RUN_PROC_FROM_C,[
rm -f conftest*
cat <<EOF > conftest.f
$2
EOF
dnl
if test "X$ac_fcompile" = "X" ; then
    ac_fcompile='${F77-f77} -c $FFLAGS conftest.f 1>&AC_FD_CC'
fi
if AC_TRY_EVAL(ac_fcompile) && test -s conftest.o ; then
    mv conftest.o conftestf.o
    AC_LANG_SAVE
    AC_LANG_C
    save_LIBS="$LIBS"
    LIBS="conftestf.o $FLIBS $LIBS"
    AC_TRY_RUN([#include <stdio.h>
#ifdef F77_NAME_UPPER
#define ftest_ FTEST
#elif defined(F77_NAME_LOWER) || defined(F77_NAME_MIXED)
#define ftest_ ftest
#endif
$1
], [$3], [$4], [$5] )
    LIBS="$save_LIBS"
    AC_LANG_RESTORE
else 
    echo "configure: failed program was:" >&AC_FD_CC
    cat conftest.f >&AC_FD_CC
fi
rm -f conftest*
])
dnl
dnl PAC_PROG_F77_IN_C_LIBS
dnl
dnl Find the essential libraries that are needed to use the C linker to 
dnl create a program that includes a trival Fortran code.  
dnl
dnl For example, all pgf90 compiled objects include a reference to the
dnl symbol pgf90_compiled, found in libpgf90 .
dnl
AC_DEFUN(PAC_PROG_F77_IN_C_LIBS,[
AC_MSG_CHECKING([what Fortran libraries are needed to link C with Fortran])
F77_IN_C_LIBS="$FLIBS"
rm -f conftest*
cat <<EOF > conftest.f
        subroutine ftest
        end
EOF
dnl
if test "X$ac_fcompile" = "X" ; then
    ac_fcompile='${F77-f77} -c $FFLAGS conftest.f 1>&AC_FD_CC'
fi
if AC_TRY_EVAL(ac_fcompile) && test -s conftest.o ; then
    mv conftest.o mconftestf.o
    AC_LANG_SAVE
    AC_LANG_C
    save_LIBS="$LIBS"
    dnl First try with no libraries
    LIBS="mconftestf.o $save_LIBS"
    AC_TRY_LINK([#include <stdio.h>],[
#ifdef F77_NAME_UPPER
#define ftest_ FTEST
#elif defined(F77_NAME_LOWER) || defined(F77_NAME_MIXED)
#define ftest_ ftest
#endif
ftest_();
], [link_worked=yes], [link_worked=no] )
    if test "$link_worked" = "no" ; then
        flibdirs=`echo $FLIBS | tr ' ' '\012' | grep '\-L' | tr '\012' ' '`
        fliblibs=`echo $FLIBS | tr ' ' '\012' | grep -v '\-L' | tr '\012' ' '`
        for flibs in $fliblibs ; do
            LIBS="mconftestf.o $flibdirs $flibs $save_LIBS"
            AC_TRY_LINK([#include <stdio.h>],[
#ifdef F77_NAME_UPPER
#define ftest_ FTEST
#elif defined(F77_NAME_LOWER) || defined(F77_NAME_MIXED)
#define ftest_ ftest
#endif
ftest_();
], [link_worked=yes], [link_worked=no] )
            if test "$link_worked" = "yes" ; then 
	        F77_IN_C_LIBS="$flibdirs $flibs"
                break
            fi
        done
    if test "$link_worked" = "no" ; then
	# try to add libraries until it works...
        flibscat=""
        for flibs in $fliblibs ; do
	    flibscat="$flibscat $flibs"
            LIBS="mconftestf.o $flibdirs $flibscat $save_LIBS"
            AC_TRY_LINK([#include <stdio.h>],[
#ifdef F77_NAME_UPPER
#define ftest_ FTEST
#elif defined(F77_NAME_LOWER) || defined(F77_NAME_MIXED)
#define ftest_ ftest
#endif
ftest_();
], [link_worked=yes], [link_worked=no] )
            if test "$link_worked" = "yes" ; then 
	        F77_IN_C_LIBS="$flibdirs $flibscat"
                break
            fi
        done
    fi
    else
	# No libraries needed
	F77_IN_C_LIBS=""
    fi
    LIBS="$save_LIBS"
    AC_LANG_RESTORE
else 
    echo "configure: failed program was:" >&AC_FD_CC
    cat conftest.f >&AC_FD_CC
fi
rm -f conftest* mconftest*
if test -z "$F77_IN_C_LIBS" ; then
    AC_MSG_RESULT(none)
else
    AC_MSG_RESULT($F77_IN_C_LIBS)
fi
])
dnl
dnl Test to see if we should use C or Fortran to link programs whose
dnl main program is in Fortran.  We may find that neither work because 
dnl we need special libraries in each case.
dnl
AC_DEFUN([PAC_PROG_F77_LINKER_WITH_C],[
AC_TRY_COMPILE(,
long long a;,AC_DEFINE(HAVE_LONG_LONG,1,[Define if long long allowed]))
AC_MSG_CHECKING([for linker for Fortran main programs])
dnl
dnl Create a program that uses multiplication and division in case
dnl that requires special libraries
cat > conftest.c <<EOF
#include "confdefs.h"
#ifdef HAVE_LONG_LONG
int f(int a, long long b) { int c; c = a * ( b / 3 ) / (b-1); return c ; }
#else
int f(int a, long b) { int c; c = a * b / (b-1); return c ; }
#endif
EOF
AC_LANG_SAVE
AC_LANG_C
if AC_TRY_EVAL(ac_compile); then
    mv conftest.o conftest1.o
else
    AC_MSG_ERROR([Could not compile C test program])
fi
AC_LANG_FORTRAN77
cat > conftest.f <<EOF
        program main
        double precision d
        print *, "hi"
        end
EOF
if AC_TRY_EVAL(ac_compile); then
    if ${F77} -o conftest conftest.o conftest1.o $LDFLAGS 2>&AC_FD_CC ; then
	AC_MSG_RESULT([Use Fortran to link programs])
    elif ${CC} -o conftest conftest.o conftest1.o $LDFLAGS $FLIBS 2>&AC_FD_CC ; then
	AC_MSG_RESULT([Use C with FLIBS to link programs])
	F77LINKER="$CC"
        F77_LDFLAGS="$F77_LDFLAGS $FLIBS"
    else
	AC_MSG_RESULT([Unable to determine how to link Fortran programs with C])
    fi
else
    AC_MSG_ERROR([Could not compile Fortran test program])
fi
AC_LANG_RESTORE
])
dnl
dnl
dnl
AC_DEFUN(PAC_PROG_F77_CHECK_FLIBS,
[AC_MSG_CHECKING([Whether C can link with $FLIBS])
# Try to link a C program with all of these libraries
save_LIBS="$LIBS"
LIBS="$LIBS $FLIBS"
AC_TRY_LINK(,[int a;],runs=yes,runs=no)
LIBS="$save_LIBS"
AC_MSG_RESULT($runs)
if test "$runs" = "no" ; then
    AC_MSG_CHECKING([which libraries can be used])
    pac_ldirs=""
    pac_libs=""
    pac_other=""
    for name in $FLIBS ; do
        case $name in 
        -l*) pac_libs="$pac_libs $name" ;;
        -L*) pac_ldirs="$pac_ldirs $name" ;;
        *)   pac_other="$pac_other $name" ;;
        esac
    done
    save_LIBS="$LIBS"
    keep_libs=""
    for name in $pac_libs ; do 
        LIBS="$save_LIBS $pac_ldirs $pac_other $name"
        AC_TRY_LINK(,[int a;],runs=yes,runs=no)
        if test $runs = "yes" ; then keep_libs="$keep_libs $name" ; fi
    done
    AC_MSG_RESULT($keep_libs)
    LIBS="$save_LIBS"
    FLIBS="$pac_ldirs $pac_other $keep_libs"
fi
])
