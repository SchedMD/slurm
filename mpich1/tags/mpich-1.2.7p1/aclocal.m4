dnl
dnl Additional macros for using autoconf to build configure scripts
dnl
dnl NOTES ON ADDING TO THIS
dnl It is important to end ALL definitions with "dnl" to insure that 
dnl there are NO blank lines before the "/bin/sh" in the configure script.
dnl
dnl
dnl PAC_TEST_PROGRAM is like AC_TEST_PROGRAM, except that it makes it easier
dnl to find out what failed.
dnl
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
define(PAC_TEST_PROGRAM,
[AC_PROVIDE([$0])
AC_REQUIRE([AC_CROSS_CHECK])
if test "$cross_compiling" = 1 -a -z "$TESTCC" ; then
    ifelse([$4], , ,$4)
    Pac_CV_NAME=0
else
    if test -n "$TESTCC" ; then
      CCsav="$CC"
      CC="$TESTCC"
    fi
    cat > conftest.c <<EOF
#include "confdefs.h"
[$1]
EOF
    dnl This allows this script to work with both autoconf1 and 2
    if test -n "$compile" ; then 	
	eval $compile
    else
	eval $ac_compile
	eval $ac_link
    fi
    if test ! -s conftest ; then
        echo "Could not build executable program:" >>config.log
        if test -n "$compile" ; then
	    # Autoconf defined a compile command with all output to dev null
	    # For the configure log, rerun without the redirection
	    compiletmp='${CC-cc} $CFLAGS conftest.c -o conftest $LIBS >>config.log 2>&1'
	    echo $compiletmp >>config.log
	    eval $compiletmp 
	    echo "Return status was $?" >>config.log
        else
	    echo $ac_compile >>config.log
   	    eval $ac_compile >>config.log 2>&1
	    echo $ac_link >>config.log
	    eval $ac_link >>config.log 2>&1
        fi
	echo "Test program was" >> config.log
	cat conftest.c >> config.log
    ifelse([$3], , , [$3
])
    else
      rm -f conftestout
      if test -s conftest && (./conftest; exit) 2>conftestout; then
          ifelse([$2], , :, [$2
])
      else
        ifelse([$3], , , [$3
])
	if test -s conftestout ; then cat conftestout >> config.log ; fi
	echo "Test program was" >> config.log
	cat conftest.c >> config.log
        ifelse([$3], ,[echo "Execution of test program failed"])
      fi
    fi
  if test -n "$TESTCC" ; then
        CC="$CCsav"
  fi
  rm -fr conftest*
fi
])dnl
dnl
dnl Some systems (particularly parallel systems) do not return correct
dnl values for exit codes; for this reason, it is better to get the
dnl sizes by running the programs and then comparing them
dnl
dnl PAC_GET_TYPE_SIZE(typename,var_for_size)
dnl
dnl sets var_for_size to the size.  Ignores if the size cannot be determined
dnl Also sets typename_len to the size; if that is already set, just uses
dnl that
dnl
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
define(PAC_GET_TYPE_SIZE,
[Pac_name="$1"
 Pac_varname=`echo "$Pac_name" | sed -e 's/ /_/g' -e 's/\*/star/g'`
eval Pac_testval=\$"${Pac_varname}_len"
if test -z "$Pac_testval" ; then
   changequote(<<,>>)
   define(<<AC_TYPE_NAME>>,translit(CROSS_SIZEOF_$1,[a-z *],[A-Z_P]))dnl
   changequote([,])
   eval Pac_testval=\$"AC_TYPE_NAME"
fi
if test -n "$Pac_testval" ; then
    Pac_CV_NAME=$Pac_testval
else
 AC_MSG_CHECKING([for size of $Pac_name])
 rm -f conftestval
 PAC_TEST_PROGRAM([#include <stdio.h>
main() { 
  FILE *f=fopen("conftestval","w");
  if (!f) exit(1);
  fprintf( f, "%d\n", sizeof($Pac_name));
  exit(0);
}],Pac_CV_NAME=`cat conftestval`,Pac_CV_NAME="")
 if test -n "$Pac_CV_NAME" -a "$Pac_CV_NAME" != 0 ; then
    AC_MSG_RESULT($Pac_CV_NAME)
    eval ${Pac_varname}_len=$Pac_CV_NAME
 else
    AC_MSG_RESULT(unavailable)
 fi
fi
$2=$Pac_CV_NAME
])dnl
dnl
dnl
dnl Define test for 64-bit pointers
dnl
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
define(PAC_POINTER_64_BITS,
[
pointersize=""
PAC_GET_TYPE_SIZE(void *,pointersize)
AC_MSG_CHECKING([for pointers greater than 32 bits])
if test -z "$pointersize" ; then
    AC_MSG_RESULT(can not determine; assuming not)
elif test $pointersize -gt 4 ; then
    ifelse($1,,AC_DEFINE(POINTER_64_BITS),AC_DEFINE($1))
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
fi
])dnl
dnl
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
define(PAC_INT_LT_POINTER,[
intsize=""
PAC_GET_TYPE_SIZE(int,intsize)
if test -z "$pointersize" ; then
    PAC_GET_TYPE_SIZE(void *,pointersize)
fi
AC_MSG_CHECKING([for int large enough for pointers])
if test -n "$pointersize" -a -n "$intsize" ; then
    if test $pointersize -le $intsize ; then
       AC_MSG_RESULT(yes)
    else
       AC_DEFINE(INT_LT_POINTER)
       AC_MSG_RESULT(no)
    fi
else
    AC_MSG_RESULT(can not determine; assuming it is)
fi
])dnl
dnl
dnl Define the test for the long long type
dnl This is made more interesting because some compilers implement it, 
dnl but not correctly.  If they can't do it right, turn it off.
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
define(PAC_LONG_LONG_INT,
[AC_REQUIRE([AC_PROG_CC])dnl
AC_MSG_CHECKING([for long long])
PAC_TEST_PROGRAM([int main() {
/* See long double test; this handles the possibility that long long
   has the same problem on some systems */
exit(sizeof(long long) < sizeof(long)); }],
has_long_long=1,has_long_long=0,[has_long_long=${CROSS_HAS_LONG_LONG:-0};])
if test "$has_long_long" = 1 ; then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
fi
if test "$has_long_long" = 1 ; then
   AC_MSG_CHECKING([whether compiler can handle loops with long long])
   dnl We'd like to use AC_COMPILE_CHECK, but this example dies only when
   dnl used with arrays ([]).
   changequote(,)
   cat > conftest.c <<EOF
void MPIR_SUM_ext( invec, inoutvec, len )
void *invec, *inoutvec;
int  len;
{
    int i;
    long long *a = (long long *)inoutvec; long long *b = (long long *)invec;
    for ( i=0; i<len; i++ )
      a[i] = a[i]+b[i];
}
int main(){return 0;}
EOF
    changequote([,])
    if eval $compile ; then
        rm -rf conftest*
        AC_MSG_RESULT(yes)
    else
        rm -rf conftest*
        AC_MSG_RESULT(no!)
        has_long_long=0
    fi
fi
if test "$has_long_long" = 1 ; then
    AC_DEFINE(HAVE_LONG_LONG_INT)
fi
])dnl
dnl
dnl The AC_LONG_DOUBLE macro is junk because it assumes that the
dnl long double type is valid in the language!  In other words, that
dnl the compiler is ANSI C
dnl
define(PAC_LONG_DOUBLE,
[AC_REQUIRE([AC_PROG_CC])dnl
AC_MSG_CHECKING([for long double])
if test -n "$GCC"; then
AC_DEFINE(HAVE_LONG_DOUBLE)
AC_MSG_RESULT(yes)
else
AC_COMPILE_CHECK(,,long double a;return 0;,ldok=1,ldok=0)
if test $ldok = 1 ; then
    PAC_TEST_PROGRAM([int main() {
/* On Ultrix 4.3 cc, long double is 4 and double is 8.  */
exit(sizeof(long double) < sizeof(double)); }],
AC_DEFINE(HAVE_LONG_DOUBLE)AC_MSG_RESULT(yes),
AC_MSG_RESULT(no),
[if test -n "$CROSS_HAS_LONG_DOUBLE" ; then 
    if test "$CROSS_HAS_LONG_DOUBLE" = "yes" ; then 
	AC_DEFINE(HAVE_LONG_DOUBLE)
        AC_MSG_RESULT(yes)
    else 
        AC_MSG_RESULT(no)
    fi
fi])
else
    AC_MSG_RESULT(no)
fi
fi #GCC
])dnl
dnl
dnl PAC_HAVE_VOLATILE
dnl 
dnl Defines HAS_VOLATILE if the C compiler accepts "volatile" 
dnl
dnl *** DO NOT CHANGE WITHOUT CHANGING sowing/confdb VERSION ***
define(PAC_HAVE_VOLATILE,
[AC_MSG_CHECKING([for volatile])
AC_COMPILE_CHECK(,[volatile int a;],main();,
AC_DEFINE(HAS_VOLATILE)AC_MSG_RESULT(yes),AC_MSG_RESULT(no))
])dnl
dnl
dnl
dnl
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
define(PAC_WORDS_BIGENDIAN,
[AC_MSG_CHECKING([for byte ordering])
PAC_TEST_PROGRAM([main () {
  /* Are we little or big endian?  From Harbison&Steele.  */
  union
  {
    long l;
    char c[sizeof (long)];
  } u;
  u.l = 1;
  exit (u.c[sizeof (long) - 1] == 1);
}], ,pac_r=1)
if test -z "$pac_r" ; then
    AC_MSG_RESULT(little endian)
else
    AC_MSG_RESULT(big endian)
    ifelse($1,,AC_DEFINE(WORDS_BIGENDIAN),AC_DEFINE($1))
fi
])dnl
dnl
dnl Look for a non-standard library by looking in some named places.
dnl Check for both foo.a and libfoo.a (and .so)
dnl 
dnl PAC_FIND_USER_LIB(LIB-NAME[,LIB-LIST,ACTION-IF-FOUND,ACTION-IF-NOT-FOUND])
dnl (use foo to check for foo.a and libfoo.a)
dnl Checks the usual places, as well as /usr/local/LIBNAME and
dnl /usr/local/LIBNAME/lib .
dnl The location of the library may be found in pac_lib_file.
dnl The DIRECTORY of the library may be found in pac_lib_dir
dnl
define(PAC_FIND_USER_LIB,[
AC_MSG_CHECKING([for library $1])
pac_lib_file=""
pac_lib_dir=""
for dir in $2 \
    /usr \
    /usr/local \
    /usr/local/$1 \
    /usr/contrib \
    /usr/contrib/$1 \
    $HOME/$1 \
    /opt/$1 \
    /opt/local \
    /opt/local/$1 \
    /local/encap/$1 ; do
    for suffix in a so ; do
        if test -n "$pac_lib_dir" ; then break ; fi
        if test -r $dir/$1.$suffix ; then
	    pac_lib_file=$dir/$1.$suffix
            pac_lib_dir=$dir
	    break
        fi
        if test -r $dir/lib$1.$suffix ; then
	    pac_lib_file=$dir/lib$1.$suffix
            pac_lib_dir=$dir
	    break
        fi
        if test -r $dir/lib/$1.$suffix ; then
	    pac_lib_file=$dir/lib/$1.$suffix
            pac_lib_dir=$dir/lib
	    break
        fi
        if test -r $dir/lib/lib$1.$suffix ; then
	    pac_lib_file=$dir/lib/lib$1.$suffix
            pac_lib_dir=$dir/lib
	    break
        fi
    done
done
if test -n "$pac_lib_file" ; then 
  AC_MSG_RESULT(found $pac_lib_file)
  ifelse([$3],,,[$3])
else
  AC_MSG_RESULT(no)
  ifelse([$4],,,[$4])
fi
])dnl
dnl
dnl Look for a non-standard include by looking in some named places.
dnl Check for foo.h
dnl 
dnl PAC_FIND_USER_INCLUDE(FILE-NAME[,DIR-LIST,ACTION-IF-FOUND,ACTION-IF-NOT-FOUND])
dnl (use foo to check for foo.h)
dnl Checks the usual places, as well as /usr/local/FILENAME and
dnl /usr/local/FILENAME/include .
dnl The location of the include directory library may be found in pac_inc_dir.
dnl
define(PAC_FIND_USER_INCLUDE,[
AC_MSG_CHECKING([for include directory for $1])
pac_inc_dir=""
for dir in $2 \
    /usr \
    /usr/local \
    /usr/local/$1 \
    /usr/contrib \
    /usr/contrib/$1 \
    $HOME/$1 \
    /opt/$1 \
    /opt/local \
    /opt/local/$1 \
    /local/encap/$1 ; do
    if test -r $dir/$1.h ; then
	pac_inc_dir=$dir
	break
    fi
    if test -r $dir/include/$1.h ; then
	pac_inc_dir=$dir/include
	break
    fi
    if test -r $dir/lib/lib$1.a ; then
	pac_lib_file=$dir/lib/lib$1.a
	break
    fi
    if test -r $dir/lib/lib$1.so ; then
	pac_lib_file=$dir/lib/lib$1.so
	break
    fi
done
if test -n "$pac_inc_dir" ; then 
  AC_MSG_RESULT(found $pac_inc_dir)
  ifelse([$3],,,[$3])
else
  AC_MSG_RESULT(no)
  ifelse([$4],,,[$4])
fi
])dnl
dnl
dnl The AC_CHECK_HEADER assumes that you can use cpp to check for 
dnl headers for the (CROSS!) compiler.  This is ridiculous.
dnl I've FIXED the code from version 2
dnl
dnl ### Printing messages
dnl
dnl
dnl Check whether to use -n, \c, or newline-tab to separate
dnl checking messages from result messages.
dnl Idea borrowed from dist 3.0.
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
dnl Internal use only.
define(AC_PROG_ECHO_N,
ac_echo_n=yes
[if (echo "testing\c"; echo 1,2,3) | grep c >/dev/null; then
  # Stardent Vistra SVR4 grep lacks -e, says ghazi@caip.rutgers.edu.
  if (echo -n testing; echo 1,2,3) | sed s/-n/xn/ | grep xn >/dev/null; then
    ac_n= ac_c='
' ac_t='	'
  else
    ac_n=-n ac_c= ac_t=
  fi
else
  ac_n= ac_c='\c' ac_t=
fi
ac_echo_test=`echo foo 1>&1`
if test -z "$ac_echo_test" ; then
     print_error "Your sh shell does not handle the output redirection"
     print_error "1>&1 correctly.  Configure will work around this problem,"
     print_error "but you should report the problem to your vendor."
fi
define(pac_set_echo_n,1)dnl
])dnl
dnl AC_MSG_CHECKING(FEATURE-DESCRIPTION)
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
define(AC_FD_MSG,1)dnl
define(AC_MSG_CHECKING,[dnl
dnl ifdef(pac_set_echo_n,,[
if test -z "$ac_echo_n" ; then
AC_PROG_ECHO_N
fi
dnl ])
if test -z "$ac_echo_test" -a AC_FD_MSG = 1 ; then
echo $ac_n "checking $1""... $ac_c"
else
echo $ac_n "checking $1""... $ac_c" 1>&AC_FD_MSG
fi])dnl
dnl
dnl AC_MSG(msg)
dnl generates "msg..." (no newline)
define(AC_MSG,[dnl
ifdef(pac_set_echo_n,,[
if test -z "$ac_echo_n" ; then
AC_PROG_ECHO_N
fi])
if test -z "$ac_echo_test" -a AC_FD_MSG = 1 ; then
echo $ac_n "$1""... $ac_c"
else
echo $ac_n "$1""... $ac_c" 1>&AC_FD_MSG
fi])dnl
dnl
dnl AC_CHECKING(FEATURE-DESCRIPTION)
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
define(AC_CHECKING,dnl
[echo "checking $1" 1>&AC_FD_MSG])dnl
dnl
dnl AC_MSG_RESULT(RESULT-DESCRIPTION)
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
define(AC_MSG_RESULT,dnl
if test -z "$ac_echo_test" -a AC_FD_MSG = 1 ; then
[echo "$ac_t""$1"]
else
[echo "$ac_t""$1" 1>&AC_FD_MSG]
fi)dnl
dnl AC_MSG_WARN(msg)
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
define(AC_MSG_WARN,[AC_MSG_RESULT([Warning: $1])])
dnl AC_MSG_ERROR(msg)
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
define(AC_MSG_ERROR,[AC_MSG_RESULT([Error: $1])
exit 1])
dnl
dnl PAC_CHECK_HEADER(HEADER-FILE, ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND],
dnl PRE-REQ-HEADERS )
dnl
dnl BUG: AIX 4.1 can't handle a \055 (octal for -) in a tr string (sometimes;
dnl it works from the shell but not within a file)
dnl I've removed that and hoped that no header will include a - in the
dnl name
dnl
dnl This can fail if the header needs OTHER headers for the compile
dnl to succeed.  Those headers should be specified in the "pre-req-headers"
dnl For example 
dnl PAC_CHECK_HEADER(sys/vfs.h,AC_DEFINE(HAVE_SYS_VFS_H),,
dnl                  [#include <sys/types.h>])
dnl
define(PAC_CHECK_HEADER,dnl
[dnl Do the transliteration at runtime so arg 1 can be a shell variable.
changequote(,)dnl
ac_safe=`echo "$1" | tr '[a-z]./' '[A-Z]__'`
changequote([,])dnl
AC_MSG_CHECKING([for $1])
dnl AC_CACHE_VAL(ac_cv_header_$ac_safe,[dnl
AC_COMPILE_CHECK(,[$4]
[#include <$1>],main();,eval "ac_cv_header_$ac_safe=yes",
  eval "ac_cv_header_$ac_safe=no")dnl])dnl
if eval "test \"`echo '$ac_cv_header_'$ac_safe`\" = yes"; then
  AC_MSG_RESULT(yes)
  ifelse([$2], , :, [$2])
else
  AC_MSG_RESULT(no)
ifelse([$3], , , [$3
])dnl
fi
])dnl
dnl
dnl PAC_CHECK_HEADERS(HEADER-FILE... [, ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND]])
define(PAC_CHECK_HEADERS,[for ac_hdr in $1
do
PAC_CHECK_HEADER($ac_hdr,
[changequote(, )dnl
  ac_tr_hdr=HAVE_`echo $ac_hdr | tr '[a-z]./' '[A-Z]__'`
changequote([, ])dnl
  AC_DEFINE($ac_tr_hdr) $2], $3)dnl
done
])dnl
dnl
dnl WARNING: AC_HEADER_STDC uses CPP instead of CC!
dnl
dnl
dnl Check to see if malloc is declared as char *malloc or void *malloc
dnl If stdlib.h is not defined, then this will choose char*malloc.
dnl
define(PAC_MALLOC_RETURNS_VOID,
[AC_MSG_CHECKING(for malloc return type)
AC_COMPILE_CHECK(,[#include <stdlib.h>],[extern void *malloc();],
eval "ac_cv_malloc=void",eval "ac_cv_malloc=char")
AC_MSG_RESULT($ac_cv_malloc)
if test "$ac_cv_malloc" = void ; then
    AC_DEFINE(MALLOC_RET_VOID)
fi
])dnl
dnl
dnl Check that the compile actually runs.  Perform first arg is yes,
dnl second if false
dnl PAC_CHECK_COMPILER_OK(true-action, false-action)
dnl
define(PAC_CHECK_COMPILER_OK,[
AC_REQUIRE([AC_CROSS_CHECK])
AC_MSG_CHECKING(whether the compiler $CC runs)
AC_COMPILE_CHECK(,,return 0;,eval "ac_cv_ccworks=yes",eval "ac_cv_ccworks=no")
AC_MSG_RESULT($ac_cv_ccworks)
if test $ac_cv_ccworks = "yes" ; then
    ifelse([$1],,:,[$1])
else
# Generate output from failed test.  See COMPILE_CHECK code
# It really would be better if the compile tests put the output into
# a file for later analysis, like conftest.out
#
cat > conftest.c <<EOF
#include "confdefs.h"
int main() { exit(0); }
int t() { return 0; }
EOF
${CC-cc} $CFLAGS conftest.c -o conftest $LDFLAGS $LIBS
rm -f conftest* 
#
# End of output
    ifelse([$2],,:,[$2])
fi
])dnl
dnl
dnl Check that the compile accepts ANSI prototypes.  Perform first arg if yes,
dnl second if false.  Only test if it hasn't been tested for this compiler
dnl (and flags) before
dnl PAC_CHECK_CC_PROTOTYPES(true-action, false-action)
dnl
dnl *** DO NOT CHANGE WITHOUT CHANGING sowing/confdb VERSION ***
define(PAC_CHECK_CC_PROTOTYPES,[
if test "$ac_cv_ccansi" != "$CC $CFLAGS" ; then
AC_MSG_CHECKING(whether the compiler $CC accepts ANSI prototypes)
AC_COMPILE_CHECK(,[int f(double a){return 0;}],,eval "ac_cv_ccworks=yes",eval "ac_cv_ccworks=no")
AC_MSG_RESULT($ac_cv_ccworks)
if test $ac_cv_ccworks = "yes" ; then
    ifelse([$1],,:,[$1])
else
    ifelse([$2],,:,[$2])
fi
ac_cv_ccansi="$CC $CFLAGS"
fi
])dnl
dnl
dnl Check that the compile accepts ANSI const type.  Perform first arg if yes,
dnl second if false
dnl PAC_CHECK_CC_CONST(true-action, false-action)
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
dnl
define(PAC_CHECK_CC_CONST,[
AC_MSG_CHECKING(whether the compiler $CC accepts const modifier)
AC_COMPILE_CHECK(,[int f(const int a){return a;}],,eval "ac_cv_ccworks=yes",eval "ac_cv_ccworks=no")
AC_MSG_RESULT($ac_cv_ccworks)
if test $ac_cv_ccworks = "yes" ; then
    ifelse([$1],,:,[$1])
else
    ifelse([$2],,:,[$2])
fi
])dnl
dnl
dnl Check that the compile accepts ANSI CPP concatenation.  Perform first 
dnl arg if yes, second if false
dnl PAC_CHECK_CPP_CONCAT(true-action, false-action)
dnl
dnl *** DO NOT CHANGE WITHOUT CHANGING sowing/confdb VERSION ***
define(PAC_CHECK_CPP_CONCAT,[
ac_pound="#"
AC_MSG_CHECKING([whether the compiler $CC accepts $ac_pound$ac_pound for concatenation in cpp])
AC_COMPILE_CHECK(,[
#define concat(a,b) a##b],[int concat(a,b);return ab;],
eval "ac_cv_ccworks=yes",eval "ac_cv_ccworks=no")
AC_MSG_RESULT($ac_cv_ccworks)
if test $ac_cv_ccworks = "yes" ; then
    ifelse([$1],,:,[$1])
else
    ifelse([$2],,:,[$2])
fi
])dnl
dnl
dnl
dnl Test the compiler to see if it actually works.  First, check to see
dnl if the compiler works at all
dnl Uses TESTCC, not CC
dnl 
dnl The test directory is ccbugs by default, but can be overridded with 
dnl CCBUGS
dnl
define(PAC_CORRECT_COMPILER,[
AC_REQUIRE([AC_CROSS_CHECK])
if test -z "$CCBUGS" ; then CCBUGS=ccbugs ; fi
if test ! -d $CCBUGS -a -d ${top_srcdir}/$CCBUGS ; then
    CCBUGS=${top_srcdir}/$CCBUGS
fi
if test -d $CCBUGS ; then 
    # Use "LTESTCC" as "local Test CC"
    if test -z "$TESTCC" ; then LTESTCC="$CC" ; else LTESTCC="$TESTCC" ; fi
    for file in $CCBUGS/ccfail*.c ; do
        CFILE=`basename $file .c`
        AC_MSG_CHECKING(`cat $CCBUGS/$CFILE.title`)
        cp $file conftest.c
        broken=1
        rm -f conftest.out conftest.rout
        if eval $LTESTCC $CFLAGS $OPTFLAGS -o conftest conftest.c $LDFLAGS $LIBS >conftest.out 2>&1 ; then
	    if test -s conftest ; then
                ./conftest 2>&1 1>conftest.rout
                if test $? = 0 ; then
  	            broken=0
                fi
	    fi
        fi
        if test $broken = 1 ; then 
	    AC_MSG_RESULT(no)
	    cat $CCBUGS/$CFILE.txt | sed 's/^/\*\#/g' 
	    if test -s conftest.out ; then
	        echo "Output from compile step was:"
		cat conftest.out
	    fi
	    if test -s conftest.rout ; then
	        echo "Output from run step was:"
		cat conftest.rout
	    fi
	else
	    AC_MSG_RESULT(yes)
        fi
	rm -f conftest conftest.c conftest.o conftest.out conftest.rout
    done
    #
    # These are non-fatal, but must be run
    for file in $CCBUGS/ccnfail*.c ; do
        CFILE=`basename $file .c`
        AC_MSG_CHECKING(`cat $CCBUGS/$CFILE.title`)
        cp $file conftest.c
        nbroken=1
	rm -f conftest.out conftest.rout
        if eval $LTESTCC $CFLAGS $OPTFLAGS -o conftest conftest.c $LDFLAGS $LIBS >conftest.out 2>&1 ; then
	    if test -s conftest ; then
                ./conftest 2>&1 1>conftest.rout
                if test $? = 0 ; then
  	            nbroken=0
                fi
	    fi
        fi
        if test $nbroken = 1 ; then 
	    AC_MSG_RESULT(no)
	    cat $CCBUGS/$CFILE.txt | sed 's/^/\*\#/g' 
	    if test -s conftest.out ; then
	        echo "Output from compile step was:"
		cat conftest.out
	    fi
	    if test -s conftest.rout ; then
	        echo "Output from run step was:"
		cat conftest.rout
	    fi
	else
	    AC_MSG_RESULT(yes)
        fi
	rm -f conftest conftest.c conftest.o conftest.out conftest.rout
    done

    # 
    # Now, try the warnings.  Note that this just does compiles, not runs
    for file in $CCBUGS/ccwarn*.c ; do
        CFILE=`basename $file .c`
        AC_MSG_CHECKING(`cat $CCBUGS/$CFILE.title`)
        cp $file conftest.c
	echo "${CC-cc} $CFLAGS ... test for quotes in defn" >>config.log
        if eval ${CC-cc} $CFLAGS $OPTFLAGS \
	    -DCONFIGURE_ARGS_CLEAN="'"'"'-A -B'"'"'" -c \
	    conftest.c $LIBS >> config.log 2>&1 ; then
	    AC_MSG_RESULT(yes)
	    true 
	else
	    AC_MSG_RESULT(no)
	    cat $CCBUGS/$CFILE.txt | sed 's/^/\*\#/g' 
	    if test "$CFILE" = "ccwarn1" ; then
	       CONFIGURE_ARGS_CLEAN="`echo $CONFIGURE_ARGS_CLEAN | tr ' ' '_'`"
            fi
        fi
	# set +x
	rm -f conftest conftest.[co]
    done
    # 
    # After everything, see if there are any problems
    if test $broken = 1 ; then 
        if test -z "$FAILMSG" ; then
	    echo "Compiler $CC appears broken; aborting configure..."
        else
	    eval echo "$FAILMSG"
        fi
        exit 1
    fi
fi
])dnl
dnl
dnl Check that the Fortran compiler works.  We needed this first for LINUX
dnl Perform first arg is yes, second if false
dnl PAC_CHECK_F77_COMPILER_OK(true-action, false-action)
dnl The name of the compiler is F77
dnl
define(PAC_CHECK_F77_COMPILER_OK,[
AC_MSG_CHECKING(whether the compiler $F77 runs)
cat >conftest.f <<EOF
          program main
          end
EOF
rm -f conftest.out
$F77 $FFLAGS $OPTFLAGSF -c conftest.f > conftest.out 2>&1
if test $? != 0 ; then
    AC_MSG_RESULT(no)
    echo "Fortran compiler returned non-zero return code"
    if test -s conftest.out ; then
	echo "Output from test was"
        cat conftest.out
    fi
    ifelse([$2],,:,[$2])
elif test ! -s conftest.o ; then
    AC_MSG_RESULT(no)
    echo "Fortran compiler did not produce object file"
    if test -s conftest.out ; then
	echo "Output from test was"
        cat conftest.out
    fi
    ifelse([$2],,:,[$2])
else    
    AC_MSG_RESULT(yes)
    ifelse([$1],,:,[$1])
fi
rm -f conftest* 
])dnl
dnl
dnl
dnl Check that the Fortran 90 compiler works.  
dnl Perform first arg is yes, second if false
dnl PAC_CHECK_F90_COMPILER_OK(true-action, false-action)
dnl The name of the compiler is F90
dnl Also defines F90_SUFFIX as f or f90 (xlf90 accepts only f; Solaris
dnl uses suffix to decide on free versus fixed format.
dnl
define(PAC_CHECK_F90_COMPILER_OK,[
AC_MSG_CHECKING(whether the compiler $F90 runs)
cat >conftest.f <<EOF
          program main
          end
EOF
rm -f conftest.out
$F90 $F90FLAGS -c conftest.f > conftest.out 2>&1
if test $? != 0 ; then
    AC_MSG_RESULT(no)
    echo "Fortran 90 compiler returned non-zero return code"
    echo "******If Fortran 77 compiler is being used, make sure you configure"
    echo        "with the correct options for folding external names to"
    echo        "either all upper- or lower-case.   The failure of this test"
    echo        "could mean that your Fortran 90 compiler could not accept"
    echo        "a compile flag chosen for your F77 compiler."
    echo        "The configuration process for the Fortran 90 compiler will"
    echo        "NOT continue.******"
    do_f90modules="no"
    NO_f90=1
    if test -s conftest.out ; then
	echo "Output from test was"
        cat conftest.out
    fi
    ifelse([$2],,:,[$2])
elif test ! -s conftest.o ; then
    AC_MSG_RESULT(no)
    echo "Fortran 90 compiler did not produce object file"
    if test -s conftest.out ; then
	echo "Output from test was"
        cat conftest.out
    fi
    ifelse([$2],,:,[$2])
else    
    AC_MSG_RESULT(yes)
    ifelse([$1],,:,[$1])
    /bin/mv -f conftest.f conftest.f90
    AC_MSG_CHECKING(whether the compiler $F90 accepts .f90 suffix)
    $F90 $F90FLAGS -c conftest.f90 > conftest.out 2>&1
    if test $? != 0 ; then
        F90_SUFFIX="f"
	AC_MSG_RESULT(no)
    else
        F90_SUFFIX="f90"
	AC_MSG_RESULT(yes)
    fi
fi
rm -f conftest* 
])dnl
dnl
dnl
dnl PAC_PROGRAM_CHECK(VARIABLE, PROG-TO-CHECK-FOR, VALUE-IF-FOUND
dnl               [, VALUE-IF-NOT-FOUND [,FULL-PATH-IF-FOUND])
dnl
dnl The variable named by FULL-PATH-IF-FOUND will be set to the
dnl full path for the program
dnl
dnl A fault with the routine in autoconf is that if PROG-TO-CHECK-FOR
dnl already has a path, it will FAIL!
dnl
dnl An UNDOCUMENTED FEATURE is that if VARIABLE is already set, this
dnl routine DOES NOTHING!
dnl
dnl An evil feature of the configure commands is that the 'variables' seem
dnl to get added to the SUBST list automatically.  I've disabled this 
dnl by NOT using ac_cv_prog_$1 which the configure code seems to use to
dnl do this.
dnl 
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO *** (sort of)
dnl
dnl 11/30/02 - Fix to handle the case where the variable value contains
dnl an argument (e.g., /usr/bin/f90 -n32).  The original code handled
dnl "f90 -n32" but not "/usr/bin/f90 -n32" (!)
dnl
define(PAC_PROGRAM_CHECK,
[# Extract the first word of "$2", so it can be a program name with args.
set dummy $2; ac_word=[$]2
AC_MSG_CHECKING([for $ac_word])
ac_prog_where=""
if test -n "[$]$1"; then
  ac_pg_$1="[$]$1" # Let the user override the test.
else
  ac_first_char=`expr "$2" : "\(.\)"`
  # Use ac_word instead of $2 in case the command has options on it.
  if test "$ac_first_char" = "/" -a -x "$ac_word" ; then
       ac_pg_$1="$3"
       ac_prog_where=$2
  else
      IFS="${IFS= 	}"; ac_save_ifs="$IFS"; IFS="${IFS}:"
      for ac_dir in $PATH; do
        test -z "$ac_dir" && ac_dir=.
        if test -f $ac_dir/$ac_word; then
          ac_pg_$1="$3"
          ac_prog_where=$ac_dir/$ac_word
          break
        fi
      done
      IFS="$ac_save_ifs"
  fi
dnl If no 4th arg is given, leave the cache variable unset,
dnl so AC_CHECK_PROGS will keep looking.
ifelse([$4], , , [  test -z "[$]ac_pg_$1" && ac_pg_$1="$4"
])dnl
fi;dnl
$1="$ac_pg_$1"
if test -n "$ac_prog_where" ; then
  AC_MSG_RESULT(found $ac_prog_where ([$]$1))
  ifelse([$5], , , [ $5=$ac_prog_where ] )
else
  AC_MSG_RESULT(no)
fi
dnl AC_SUBST($1)dnl
])dnl
dnl
dnl PAC_PROGRAMS_CHECK is like PAC_PROGRAM_CHECK, but with
dnl a list of programs.
dnl
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
define(PAC_PROGRAMS_CHECK,
[for p in $2
do
PAC_PROGRAM_CHECK($1, [$]p, [$]p,,$5)
test -n "[$]$1" && break
done
if test -z "[$]$1" ; then
    # We have to set this outside of the loop lest the first failure in 
    # PROGRAM_CHECK set the value (which then terminates the effect of the
    # loop, since autoconf macros only set values that are null, they 
    # don't override them
    $1="$4"
fi
ifelse([$3],,, [test -n "[$]$1" || $1="$3"
])])dnl
dnl
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
dnl PAC_CHECK_SIZEOF(TYPE)
dnl (switch to testcc now done by PAC_TEST_PROGRAM)
define(PAC_CHECK_SIZEOF,
[changequote(<<, >>)dnl
dnl The name to #define.
define(<<AC_TYPE_NAME>>, translit(sizeof_$1, [a-z *], [A-Z_P]))dnl
dnl The cache variable name.
define(<<AC_CV_NAME>>, translit(ac_cv_sizeof_$1, [ *], [_p]))dnl
changequote([, ])dnl
dnl Can only do this test if not cross-compiling (and TESTCC not defined)
if test "$cross_compiling" = 1 -a -z "$TESTCC" ; then
    # Check for preset size
    ac_name="AC_TYPE_NAME"
    eval testsize=\$"CROSS_SIZEOF_$ac_name"
    if test -z "$testsize" ; then
        echo "Cannot check for size of $1 when cross-compiling"
        AC_CV_NAME=0
    else
        AC_CV_NAME=$testsize
    fi
else
AC_MSG_CHECKING(for size of $1)
PAC_TEST_PROGRAM([#include <stdio.h>
main()
{
  FILE *f=fopen("cftestval", "w");
  if (!f) exit(1);
  fprintf(f, "%d\n", sizeof($1));
  exit(0);
}], AC_CV_NAME=`cat cftestval`,AC_CV_NAME=0)
rm -f cftestval

if test "$AC_CV_NAME" = 0 ; then
AC_MSG_RESULT($1 unsupported)
else
AC_MSG_RESULT($AC_CV_NAME)
fi
$2=$AC_CV_NAME
dnl AC_DEFINE_UNQUOTED(AC_TYPE_NAME, $AC_CV_NAME)
fi
undefine([AC_TYPE_NAME])dnl
undefine([AC_CV_NAME])dnl
])dnl
dnl
dnl We need routines to check that make works.  Possible problems with
dnl make include
dnl
dnl It is really gnumake, and contrary to the documentation on gnumake,
dnl it insists on screaming everytime a directory is changed.  The fix
dnl is to add the argument --no-print-directory to the make
dnl
dnl It is really BSD 4.4 make, and can't handle 'include'.  For some
dnl systems, this can be fatal; there is no fix (other than removing this
dnl aleged make).
dnl
dnl It is the OSF V3 make, and can't handle a comment in a block of targe
dnl code.  There is no acceptable fix.
dnl
dnl This assumes that "MAKE" holds the name of the make program.  If it
dnl determines that it is an improperly built gnumake, it adds
dnl --no-print-directorytries to the symbol MAKE.
dnl *** DO NOT CHANGE WITHOUT CHANGING sowing/confdb VERSION ***
define(PAC_MAKE_IS_GNUMAKE,[
AC_MSG_CHECKING(for gnumake)
rm -f conftest
cat > conftest <<.
SHELL=/bin/sh
ALL:
	@(dir=`pwd` ; cd .. ; \$(MAKE) -f \$\$dir/conftest SUB)
SUB:
	@echo "success"
.
str=`$MAKE -f conftest 2>&1`
if test "$str" != "success" ; then
    str=`$MAKE --no-print-directory -f conftest 2>&1`
    if test "$str" = "success" ; then
        MAKE="$MAKE --no-print-directory"
	AC_MSG_RESULT(yes using --no-print-directory)
    else
	AC_MSG_RESULT(no)
	echo "Unexpected output from make with program" >>config.log
	cat conftest >>config.log
	echo "str" >> config.log
    fi
else
    AC_MSG_RESULT(no)
fi
rm -f conftest
str=""
])dnl
dnl
dnl This make does not support "include filename"
dnl PAC_MAKE_IS_BSD44([true text])
dnl
dnl *** DO NOT CHANGE WITHOUT CHANGING sowing/confdb VERSION ***
define(PAC_MAKE_IS_BSD44,[
AC_MSG_CHECKING(whether make supports include)
rm -f conftest
cat > conftest <<.
ALL:
	@echo "success"
.
cat > conftest1 <<.
include conftest
.
str=`$MAKE -f conftest1 2>&1`
rm -f conftest conftest1
if test "$str" != "success" ; then
    AC_MSG_RESULT(no)
dnl    echo "The BSD 4.4 make is INCOMPATIBLE with all other makes."
dnl    echo "Using this so-called make may cause problems when building programs."
dnl    echo "You should consider using gnumake instead."
    ifelse([$1],,[$1])
else
    AC_MSG_RESULT(yes)
fi
str=""
])dnl
dnl
dnl PAC_MAKE_IS_OSF([true text])
dnl
dnl *** DO NOT CHANGE WITHOUT CHANGING sowing/confdb VERSION ***
define(PAC_MAKE_IS_OSF,[
AC_MSG_CHECKING(for OSF V3 make)
rm -f conftest
cat > conftest <<.
SHELL=/bin/sh
ALL:
	@# This is a valid comment!
	@echo "success"
.
str=`$MAKE -f conftest 2>&1`
rm -f conftest 
if test "$str" != "success" ; then
    AC_MSG_RESULT(Found OSF V3 make)
    echo "The OSF V3 make does not allow comments in target code."
    echo "Using this make may cause problems when building programs."
    echo "You should consider using gnumake instead."
    ifelse([$1],,[$1])
else
    AC_MSG_RESULT(no)
fi
str=""
])dnl
dnl
dnl Look for a style of VPATH.  Known forms are
dnl VPATH = .:dir
dnl .PATH: . dir
dnl
dnl Defines VPATH or .PATH with . $(srcdir)
dnl Requires that vpath work with implicit targets
dnl NEED TO DO: Check that $< works on explicit targets.
dnl
dnl *** DO NOT CHANGE WITHOUT CHANGING sowing/confdb VERSION ***
define(PAC_MAKE_VPATH,[
AC_SUBST(VPATH)
AC_MSG_CHECKING(for virtual path format)
rm -rf conftest*
mkdir conftestdir
cat >conftestdir/a.c <<EOF
A sample file
EOF
cat > conftest <<EOF
all: a.o
VPATH=.:conftestdir
.c.o:
	@echo \$<
EOF
ac_out=`$MAKE -f conftest 2>&1 | grep 'conftestdir/a.c'`
if test -n "$ac_out" ; then 
    AC_MSG_RESULT(VPATH)
    VPATH='VPATH=.:$(srcdir)'
else
    rm -f conftest
    cat > conftest <<EOF
all: a.o
.PATH: . conftestdir
.c.o:
	@echo \$<
EOF
    ac_out=`$MAKE -f conftest 2>&1 | grep 'conftestdir/a.c'`
    if test -n "$ac_out" ; then 
        AC_MSG_RESULT(.PATH)
        VPATH='.PATH: . $(srcdir)'
    else
	AC_MSG_RESULT(neither VPATH nor .PATH works)
    fi
fi
rm -rf conftest*
])dnl
dnl
dnl Here begins macros for setting defaults for specific systems.
dnl These handle things like C compilers with funny names and special
dnl options.
dnl
dnl
dnl These make use of the GLOBALS; see each definition for which ones
dnl are used:
dnl
dnl (Not yet present)
dnl
dnl PAC_GET_CC(arch)
dnl Uses USERCC, CC, USERCLINKER, CLINKER, LIB_LIST.  
dnl Looks for special versions
dnl of C compilers, particularly cross compilers.  May also set some
dnl compile flags.  Clears GCC if it sets CC.  Calls "print_error" for
dnl error messages
dnl 
dnl Also checks for special linker flags
dnl
define(PAC_GET_CC,[
AC_PROVIDE([AC_PROG_CC])
if test -z "$USERCC" ; then
case $1 in 
   cenju3) if test $ARCH = abi
           then
              CCC=CC
              CCLINKER=cjCC
              CC=cc
              F77=f77
              ASM=as
              if test -z "$USERFLINKER" ; then
                  FLINKER="cjabif77 $LDFLAGS"
              fi
              if test -z "$USERCLINKER" ; then
                  CLINKER="cjabicc $LDFLAGS"
              fi
           else
              CCC=CC
              CCLINKER=cjCC
              CC=/usr/necccs/bin/cc
              F77=/usr/necccs/bin/f77
              ASM=/usr/necccs/bin/as
              if test ! -x $CC ; then
                 CC=cc
                 F77=f77
                 ASM=as
              fi
#
              if test -z "$USERFLINKER" ; then
                  FLINKER="cjf77 $LDFLAGS"
              fi
              if test -z "$USERCLINKER" ; then
                  CLINKER="cjcc $LDFLAGS"
              fi
           fi
#
           DEVCFLAGS="$DEVCFLAGS -O -Kmips2"
           MPILIBNAME=mpich
           ASMFILES_O=get_stack.o
          ;;
   intelnx|paragon) CC=icc ; GCC="" 
	  # If this version of the intel compiler accepts the -nx flag, use it.
  	  if icc -nx > /dev/null 2>&1 ; then
	    # For some reason the lib list was commented out; we need
	    # it to link with if we use the NX routines
	    LIB_LIST="$LIB_LIST -nx"
	    CFLAGS="$CFLAGS -nx"
	  fi
	;;
   cm5) CC=cc ; GCC="" ;   
	if test -z "$USERCLINKER" ; then
	     CLINKER="cmmd-ld -comp $CC"
	fi ;;
   cray_t3d)        
	# Some Cray's require -Ccray-t3d instead of -Tcray-t3d.  
        # We have no diagnostic for this behavior yet.
	if test -x /mpp/bin/cc ; then 
            CC=/mpp/bin/cc ; CFLAGS="$CFLAGS -Tcray-t3d -DT3D" ; GCC="" 
	fi
        if test -z "$USERCLINKER" ; then 
	       CLINKER="$CC -Tcray-t3d" ; fi ;;
   cray_t3e)        
	# Some Cray's require -Ccray-t3e instead of -Tcray-t3e.  
        # We have no diagnostic for this behavior yet.
	if test -x /mpp/bin/cc ; then 
            CC=/mpp/bin/cc ; CFLAGS="$CFLAGS -Tcray-t3e -DT3E" ; GCC="" 
	fi
        if test -z "$USERCLINKER" ; then 
	    CLINKER="$CC -Tcray-t3e" ; fi ;;
   hpux)
	# For some systems, the (linker) option 
        #  -Wl,+vnocompatwarnings 
	# will suppress warning messages about versions of object files.
	if test "`which ${CC-cc}`" = "/usr/convex/bin/cc" ; then 
        CFLAGS="$CFLAGS -or none -U_REENTRANT -D_POSIX_SOURCE -D_HPUX_SOURCE -DMPI_cspp"
         elif test "$CC" != "gcc" ; then
	    # If cflags includes -Ae or -Aa, we don't need to add -Aa
            # In a perfect world, we might want to try -Ae
            # There is also -Aportable...
	    hasarg=`echo A$CFLAGS | sed -n -e '/-A/p'`
	    if test -z "$hasarg" ; then
		# Try to Add -Ae; add -Aa if that doesn't work
	        PAC_CHECK_COMPILER_OPTION(-Ae,hasarg=1)
		if test -n "$hasarg" ; then
                    CFLAGS="$CFLAGS -Ae"
                else
 	            PAC_CHECK_COMPILER_OPTION(-Aa,hasarg=1)
		    if test -n "$hasarg" ; then
			CFLAGS="$CFLAGS -Aa"
			AaOPT=1
		    else
			AaOPT=0
		    fi
                fi
	    fi
            # We need these flags to get the correct system include
            # files.
            CFLAGS="$CFLAGS -D_POSIX_SOURCE -D_HPUX_SOURCE"
	    # P4 needs these to get the correct system includes
            P4_CFLAGS="-D_POSIX_SOURCE -D_HPUX_SOURCE"
	    # We MUST have an ANSI compiler for HPUX, even for USER code
	    # If the regular cpp worked, we would not need to do this.
	    if test -z "$AaOPT" ; then
   	        PAC_CHECK_COMPILER_OPTION(-Aa,AaOPT=1,AaOPT=0)
            fi
	    if test "$AaOPT" = 1 ; then
	       USER_CFLAGS="$USER_CFLAGS -Aa"
	    fi
	    # Alternate...
	    # -Ae is extended -Aa (only on some PA RISC systems)
	    #CFLAGS="$CFLAGS -Ae +Olibcalls"
	    #CLINKER="cc -Ae"
	    # fort77 is the POSIX-compliant version of f77; fort77 can use 
	    # -Ldirectory
	    #F77=fort77
	    #FLINKER=fort77
	    #FFLAGS="$FFLAGS +ppu"
	    # Try and see that this works
	    AC_COMPILE_CHECK([Checking whether HP compiler has ANSI option...],,,
		hpux_ansi=1,hpux_ansi=0)
	    if test $hpux_ansi = 0 ; then
	       print_error "HPUX C compiler does not support ANSI mode!"
	       print_error "This mode is required because severe bugs in HPUX CPP"
	       print_error "cause problems.  Configuration continuing BUT	"
	       print_error "if you have trouble, consider using the GNU C"
	       print_error "compiler gcc instead."
	    else
              print_error "HPUX C compiler is being forced into ANSI mode so that"
              print_error "severe bugs in HPUX CPP do not cause problems"
	    fi
         fi 
	 ;;
    alpha)   ;;
    convex_spp)  CC="/usr/convex/bin/cc" ;;
    ibmpoe)
         dnl This is intended for the Standard POE/MPL version
         dnl This version REQUIRES you to have either mpCC or mpcc.
         dnl ??? is this safe ??
	 dnl An additional problem is that some sites will install mpCC
         dnl even though xlC is not available (!).  This forces us
         dnl to test for both mpCC and xlC, then mpcc and xlc.
	 CCval=
         PAC_PROGRAM_CHECK(CCval,xlC,xlC)
         if test -n "$CCval" ; then
             TESTCC="$CCval"
	     CCval=""
             PAC_PROGRAM_CHECK(CCval,mpCC,mpCC)
         else
	     PAC_PROGRAM_CHECK(CCval,xlc,xlc)
             if test -n "$CCval" ; then
                 TESTCC="$CCval"
	         CCval=""
                 PAC_PROGRAM_CHECK(CCval,mpcc,mpcc)
             fi
         fi
         if test -z "$CCval" ; then
            print_error "Could not find mpCC or mpcc!"
            print_error "Make sure that you path is set correctly."
	    exit 1
         fi
         CC="$CCval"
    ;;
    meiko) 
      dnl /opt/SUNWspro/bin/cc,/opt/apogee/bin/apcc,/opt/PGI/bin/cc,
      dnl /opt/gcc/bin/gcc
      CCval=''
      PAC_PROGRAMS_CHECK(CCval,cc apcc pgcc gcc)
      if test -z "$CCval" ; then
          print_error "Could not find a C compiler"
	  exit 1
      elif test "$CCVal" = "cc" ; then
          CC="cc -g -xcg92"
      else
	  CC=$CCval
      fi
	;;
    ncube)   CC=ncc ;;
    rs6000)
      CCval=""
      PAC_PROGRAMS_CHECK(CCval,xlC xlc cc)
      if test -n "$CCval" ; then
	 CC=$CCval
         GCC=""
      fi
      ;;
    *)
      # If no CC has yet been set:
      if test -z "$CC" ; then
          CCval=""
          # Pick the vendor's cc ahead of gcc.
	  # ecc, icc are various Intel compilers
	  # pgcc is the Portland group compiler
          PAC_PROGRAMS_CHECK(CCval,cc gcc pgcc ecc icc)
          # For pgcc, we might want to add -Muchar (char == unsigned char)
          if test -n "$CCval" ; then
	      CC=$CCval
	  fi
      fi
      ;;
esac
fi
dnl if test -n "$CC" ; then
dnl    PAC_PROGRAM_CHECK(HAS_CC,$CC,1,0)
dnl fi
if test -z "$USERCLINKER" -a -z "$CLINKER" ; then
    CLINKER="$CC"
fi
dnl
dnl Special check for Intel linker
pac_msg=`$CC -V 2>&1 | grep Itanium`
if test "$CC" = "ecc" -o -n "$pac_msg" ; then
    AC_MSG_CHECKING([whether -i_dynamic is required for Itanium C compiler])
    pac_result=no
    cat > conftest.c <<EOF
int main( int argc, char **argv ) { return 0; }	
EOF
    pac_msg=`$CLINKER -o conftest conftest.c 2>&1 | grep 'bfd assertion fail'`
    if test -n "$pac_msg" ; then
        pac_msg=`$CLINKER -o conftest conftest.c -i_dynamic 2>&1 | grep 'bfd assertion fail'`
	if test -x conftest -a -z "$pac_msg" ; then 
	    pac_result=yes
	    CLINKER="$CLINKER -i_dynamic"
        fi
    fi
    AC_MSG_RESULT($pac_result)
    rm -f conftest*
fi
])dnl
dnl
dnl PAC_GET_ANSI_CC(...) is like PAC_GET_CC, but it checks that CC
dnl accepts ANSI-style prototypes.  If not, it then tries to use gcc
dnl
define(PAC_GET_ANSI_CC,[
PAC_GET_CC($1)
PAC_CHECK_CC_PROTOTYPES(,noproto=1)
if test -n "$noproto" ; then
    print_error "The compiler $CC does not accept ANSI prototypes"
    CC=""
    PAC_PROGRAM_CHECK(CC,gcc,gcc)
    if test -n "$CC" ; then
	PAC_CHECK_COMPILER_OK(,exit 1)
	PAC_CHECK_CC_PROTOTYPES(,exit 1)
        print_error "Using $CC as the C compiler instead"
	CLINKER=$CC
    else
	CC=""
        CLINKER=""
	print_error "Could not find a working C compiler!"
	exit 1
    fi
fi
])dnl
dnl
dnl
dnl May also set F77GETARG (routine to get commandline arguments)
dnl Also sets HAS_F77 (makes sure that the chosen compiler is present!)
dnl
define(PAC_GET_F77,[
if test -z "$USERF77" ; then
case $1 in 
   intelnx|paragon|i860) F77=if77 ;;
   cm5) # TMC Told us this should be f77
        F77=f77 ; if test -z "$USERFLINKER" ; then
		      FLINKER="cmmd-ld -comp $F77 $LDFLAGS"
		  fi ;;
   CRAY)
   # The Fortran compiler might be cf77 or f77
   # This wierd function uses the VALUE of the first argument before
   # the second!
   F77=	
   PAC_PROGRAMS_CHECK(F77,cf77 f77 f95 f90)
   ;;
   cray_t3d)        
# The dp switch on the following lines allows compilation of "double precision"
# at the cost of violating the Fortran standard
   print_error Setting double precision to be the same as real, in violation 
   print_error of the Fortran standard.  This is needed because Cray Fortran 
   print_error for the T3D does not support double precision and hence is 
   print_error not actually Fortran.
   F77="/mpp/bin/cf77"
   FFLAGS="$FFLAGS -Ccray-t3d -dp"
   F77GETARG="call pxfgetarg(i,s,len(s),ierr)"
   if test -z "$USERFLINKER" ; then
       FLINKER="$F77 -Ccray-t3d $LDFLAGS"
   fi
   ;;
#
#  SX4 Floating Point formats
#
#  float = float0 : Conforms IEEE 754 standard.
#                   C:       sizeof (int)     = 4; sizeof (float) = 4
#                   FORTRAN: sizeof (INTEGER) = 4; sizeof (REAL)  = 4
#
#  float = float1 : IBM  floating point format
#                   C:       sizeof (int)     = 4; sizeof (float) = 4
#                   FORTRAN: sizeof (INTEGER) = 4; sizeof (REAL)  = 4
#
#  float = float2 : CRAY floating point format
#                   C:       sizeof (int)     = 4; sizeof (float) = 8
#                   FORTRAN: sizeof (INTEGER) = 8; sizeof (REAL)  = 8
#                   !!! Will not work for FORTRAN and MPICH !!!
#
#  float = float2_int64 : CRAY floating point format
#                   C:       sizeof (int)     = 8; sizeof (float) = 8
#                   FORTRAN: sizeof (INTEGER) = 8; sizeof (REAL)  = 8
#
   SX_4_float0 | SX_4_float1 | SX_4_float2 | SX_4_float2_int64)
         arch_SX_4=1
#
         float=`expr $CARCH : 'SX_4_\(float.\).*'`
         w8=`expr $CARCH : "SX_4_float._\(ew\).*" \| "dw"`
         sx4int=`expr $CARCH : ".*_\(int64\).*" \| "int32"`
#
         echo "Generating MPICH for floating point format \`\`$float''"
#
         if test "$w8" = "ew" ; then
            echo "                 and extended numeric storage \`\`$w8''"
         else
            if test "$sx4int" = "int64" ; then
               echo "                 and sizeof (int) = 64"
            elif test "$float" = "float2" ; then
               cat << EOF
***********************************************************************
*
*   WARNING:
*
*   MPICH is generated for floating point format \`\`float2'' without
*         expanding C int's to 64 bits.
*         This version will not run together with FORTRAN codes.
*
***********************************************************************
EOF
            fi
         fi
#
         CCC="CC"
         CC="cc"; ASM="as -m -h $float"
         OPTFLAGSC="-h2 -hsetmain -hnoflunf -pvctl,nomsg"
         CFLAGS="-h$float -DCHAR_PTR_IS_ADDRESS"
         FFLAGS="-$float"
         LIB_LIST="$LIB_LIST -li77sx"
#
         if test -z "$USERCLINKER" ; then
            CLINKER="cc -h$float"; fi
         if test -z "$USERCCLINKER" ; then
            CCLINKER="CC -h$float"; fi
         if test -z "$USERFLINKER" ; then
            FLINKER="f77 -$float $LDFLAGS"; fi
#
         if test "$w8" = "ew" ; then
            FLINKER="$FLINKER -ew $LDFLAGS"
            CFLAGS="$CFLAGS -D_W8"
            FFLAGS="$FFLAGS -ew"
         fi
#
         if test "$sx4int" = "int64" ; then
            CFLAGS="$CFLAGS -hint64"
            CLINKER="$CLINKER -hint64 $LDFLAGS"
            FLINKER="$FLINKER -Wl'-int64' $LDFLAGS"
         fi
#
         CCFLAGS="$CFLAGS"
    ;;
#
   hpux) 
    # This may eliminate the need for +U77 ....
    if test "`which $F77`" != "/usr/convex/bin/fc" ; then 
        F77GETARG=["call igetarg(i,s,len(s))"]
	# mgates reports that the +T option makes MPICH work on some HPUX
	# platforms.  The documentation for +T is truely scary; it seems
	# to imply that without it, a Fortran program will NOT run correctly.
        # The hpux F90 compiler doesn't accept +T.  Since we currently set
	# F90FLAGS to FFLAGS by default, 
        if test -z "$USER_SET_F90FLAGS" -a -z "$F90FLAGS" ; then
	    F90FLAGS="$FFLAGS"
	    USER_SET_F90FLAGS=1
	fi
	FFLAGS="$FFLAGS +T"
    else
        # The Convex compiler needs to have optimization messages suppressed
        FFLAGS="$FFLAGS -or none"
    fi
    # There are reports that version 10 Fortran requires +U77
    ;;
    convex_spp)  F77="/usr/convex/bin/fc" ;;
    ibmpoe)
         dnl This is intended for the Standard POE/MPL version
	 dnl We may want to use the _r versions 
	 F77=mpxlf
	 TESTF77=xlf
	 dnl This is needed for the parts of the configure that
	 dnl try to find Fortran 90
         if test -z "$F90" ; then 
	     F90="mpxlf -qlanglvl=90ext -qfree=f90"
         fi
	 if test -z "$TESTF90" ; then
   	     TESTF90=xlf90
         fi
    ;;
    meiko) 
      PAC_PROGRAMS_CHECK(FCval,f77 apf77 pgf77)
      if test -n "$FCval" -a "$FCval" = f77 ; then
	F77="f77 -g -cg92"
      else
        F77="$FCval"
      fi
      ;;
    ncube)   F77=nf77 ;;
    rs6000)  F77=xlf ;;
    LINUX|linux) 
      PAC_PROGRAMS_CHECK(FCval,f77 fort77 g77 pgf77 f95 f90 pgf90)
      if test "$FCval" != "$F77" -a "$USERF77" != 1 ; then 
  	  F77="$FCval"
      fi
      # For pgf77, some people use fflags="-tp p6 -Msignextend"
      ;;
    *)
      # Fujitsu Fortran is frt
      # We must use an FCval (undefined variable) to ensure
      # that we make the tests.  Also, fall back on f90 or f95 
      PAC_PROGRAMS_CHECK(FCval,f77 g77 fort77 frt pgf77 f95 f90,,,F77FULL)
      if test "$FCval" != "$F77" -a "$USERF77" != 1 ; then 
  	  F77="$FCval"
      fi
      ;;
esac
fi
if test -z "$F77" -a -n "$F90" ; then
    # If no Fortran 77 compiler has been selected but there is a Fortran 90
    # compiler, choose that.
    F77="$F90"
fi

if test -z "$USERFLINKER" -a -z "$FLINKER" ; then
    FLINKER="$F77 $LDFLAGS"
fi
#
# Check that the Fortran compiler is actually available:
HAS_F77=
if test -n "$F77" ; then
    if test -z "$F77FULL" ; then
        PAC_PROGRAM_CHECK(HAS_F77,$F77,1,0,F77FULL)
    else
	if test -x $F77FULL ; then
	    HAS_F77=1
	fi
    fi
    # if test -n "$F77FULL" ; then
	# Really should replace with full path....
        # but would need to capture arguments as well...
	dnl F77=$F77FULL
    # fi
else
    HAS_F77=0
fi
dnl
dnl Special check for Intel linker
pac_msg=`$F77 -V 2>&1 | grep Itanium`
if test "$F77" = "efc" -o -n "$pac_msg" ; then
    AC_MSG_CHECKING([whether -i_dynamic is required for Itanium Fortran compiler])
    pac_result=no
    cat > conftest.f <<EOF
        program main
        end
EOF
    pac_msg=`$FLINKER -o conftest conftest.f 2>&1 | grep 'bfd assertion fail'`
    if test -n "$pac_msg" ; then
        pac_msg=`$FLINKER -o conftest conftest.f -i_dynamic 2>&1 | grep 'bfd assertion fail'`
	if test -x conftest -a -z "$pac_msg" ; then 
	    pac_result=yes
	    FLINKER="$FLINKER -i_dynamic"
        fi
    fi
    AC_MSG_RESULT($pac_result)
    rm -f conftest*
fi
])dnl
dnl
dnl Fortran runtime for Fortran/C linking
dnl On suns, try
dnl FC_LIB          =/usr/local/lang/SC2.0.1/libM77.a \ 
dnl              /usr/local/lang/SC2.0.1/libF77.a -lm \
dnl              /usr/local/lang/SC2.0.1/libm.a \
dnl              /usr/local/lang/SC2.0.1/libansi.a
dnl
dnl AIX requires -bI:/usr/lpp/xlf/lib/lowsys.exp
dnl ------------------------------------------------------------------------
dnl
dnl Get the format of Fortran names.  Uses F77, FFLAGS, and sets WDEF.
dnl If the test fails, sets NOF77 to 1, HAS_FORTRAN to 0
dnl
dnl *** DO NOT CHANGE WITHOUT CHANGING sowing/confdb VERSION ***
define(PAC_GET_FORTNAMES,[
   # Check for strange behavior of Fortran.  For example, some FreeBSD
   # systems use f2c to implement f77, and the version of f2c that they 
   # use generates TWO (!!!) trailing underscores
   # Currently, WDEF is not used but could be...
   #
   # Eventually, we want to be able to override the choices here and
   # force a particular form.  This is particularly useful in systems
   # where a Fortran compiler option is used to force a particular
   # external name format (rs6000 xlf, for example).
   cat > confftest.f <<EOF
       subroutine mpir_init_fop( a )
       integer a
       a = 1
       return
       end
EOF
   echo "$F77 $FFLAGS -c confftest.f" >> config.log
   $F77 $FFLAGS -c confftest.f >> config.log 2>&1
   if test ! -s confftest.o ; then
        print_error "Unable to test Fortran compiler"
        print_error "(compiling a test program failed to produce an "
        print_error "object file)."
	NOF77=1
        HAS_FORTRAN=0
   elif test -z "$FORTRANNAMES" ; then
    # We have to be careful here, since the name may occur in several
    # forms.  We try to handle this by testing for several forms
    # directly.
    if test $arch_CRAY ; then
     # Cray doesn't accept -a ...
     nameform1=`strings confftest.o | grep mpir_init_fop_  | sed -n -e '1p'`
     nameform2=`strings confftest.o | grep MPIR_INIT_FOP   | sed -n -e '1p'`
     nameform3=`strings confftest.o | grep mpir_init_fop   | sed -n -e '1p'`
     nameform4=`strings confftest.o | grep mpir_init_fop__ | sed -n -e '1p'`
    else
     nameform1=`strings -a confftest.o | grep mpir_init_fop_  | sed -n -e '1p'`
     nameform2=`strings -a confftest.o | grep MPIR_INIT_FOP   | sed -n -e '1p'`
     nameform3=`strings -a confftest.o | grep mpir_init_fop   | sed -n -e '1p'`
     nameform4=`strings -a confftest.o | grep mpir_init_fop__ | sed -n -e '1p'`
    fi
    rm -f confftest.f confftest.o
    if test -n "$nameform4" ; then
	echo "Fortran externals are lower case and have 1 or 2 trailing underscores"
	FORTRANNAMES="FORTRANDOUBLEUNDERSCORE"
    elif test -n "$nameform1" ; then
        # We don't set this in CFLAGS; it is a default case
        echo "Fortran externals have a trailing underscore and are lowercase"
	FORTRANNAMES="FORTRANUNDERSCORE"
    elif test -n "$nameform2" ; then
	echo "Fortran externals are uppercase"     
	FORTRANNAMES="FORTRANCAPS" 
    elif test -n "$nameform3" ; then
	echo "Fortran externals are lower case"
	FORTRANNAMES="FORTRANNOUNDERSCORE"
    else
	print_error "Unable to determine the form of Fortran external names"
	print_error "Make sure that the compiler $F77 can be run on this system"
#	print_error "If you have problems linking, try using the -nof77 option"
#        print_error "to configure and rebuild MPICH."
	print_error "Turning off Fortran (-nof77 being assumed)."
	NOF77=1
        HAS_FORTRAN=0
    fi
#   case $nameform in 
#       MPIR_INIT_FOP | _MPIR_INIT_FOP)
#	echo "Fortran externals are uppercase"     
#	WDEF=-DFORTRANCAPS 
#	;;
#       mpir_init_fop_ | _mpir_init_fop_)   
#	 # We don't set this in CFLAGS; it is a default case
#        echo "Fortran externals have a trailing underscore and are lowercase"
#	WDEF=-DFORTRANUNDERSCORE ;;
#
#       mpir_init_fop | _mpir_init_fop)     
#	echo "Fortran externals are lower case"
#	WDEF=-DFORTRANNOUNDERSCORE 
#	;;
#
#           # Fortran no underscore is the "default" case for the wrappers; 
#	   # having this defined allows us to have an explicit test, 
#	   # particularly since the most common UNIX case is FORTRANUNDERSCORE
#       mpir_init_fop__ | _mpir_init_fop__)  
#	echo "Fortran externals are lower case and have 1 or 2 trailing underscores"
#	WDEF=-DFORTRANDOUBLEUNDERSCORE
#        ;;
#
#       *)
#	print_error "Unable to determine the form of Fortran external names"
#	print_error "If you have problems linking, try using the -nof77 option"
#        print_error "to configure and rebuild MPICH."
#	NOF77=1
#        HAS_FORTRAN=0
#	;;
#   esac
    fi
    if test -n "$FORTRANNAMES" ; then
        WDEF="-D$FORTRANNAMES"
    fi
    ])dnl
dnl
dnl ------------------------------------------------------------------------
dnl
dnl AR = name with args to create/add to archive
dnl As input, ARLOCAL is the arg that should be use for using the local 
dnl directory
dnl ARNAME = just name
dnl ARARGS = just args
define(PAC_GET_AR,[
if test -z "$USERAR" ; then
case $1 in 
   intelnx|paragon|i860) AR="ar860 crl" ; ARNAME="ar860" ; ARARGS="crl" ;;
   cm5) AR="ar" ; ARNAME="ar" ; ARARGS="cr"
   ;;
   meiko|solaris) AR="ar" ; ARNAME="ar" ; ARARGS="cr" 
   ;;
   ncube) AR="nar cr" ; ARNAME="nar" ; ARARGS="cr" ;;
   *)
   PAC_PROGRAMS_CHECK(ARNAME,ar)
esac
fi
if test -z "$ARNAME" ; then
    ARNAME="ar"
fi
if test -z "$AR" ; then 
    AR="$ARNAME" ; ARARGS="cr$ARLOCAL" 
fi
])dnl
dnl --------------------------------------------------------
dnl Find X11.  This is more careful than the AC version
dnl Uses USERXLIB; sets x_includes, X11INC, x_libraries, X11LIB, no_x
dnl
define(PAC_FIND_X11,[
   # FIND_X doesn't always work correctly when cross compiling, so we
   # try to be more careful and conservative
   if test -z "$USERXLIB" ; then 
    # The user has specified the libraries/include paths; pick them up 
    # below....
    if test -z "$cross_compiling" -o "$cross_compiling" = 0 ; then 
       AC_FIND_X()
       if test -n "$no_x" ; then
	  print_error "Did not find X11 libraries and/or include files"
       fi
    else
       # Try to compile a program with an include file.
       # I didn't use HEADER_CHECK because I want to insist that the 
       # code try to compile with the header
       no_x=true
       AC_COMPILE_CHECK([X11 headers],[#include <X11/Xlib.h>],,no_x="")
       if test -z "$no_x" ; then 
          # Try to link a simple X program
          AC_HAVE_LIBRARY(X11,no_x="",no_x="true")
       fi
       if test -n "$no_x" ; then
         print_error " " 
         print_error "X11 is not used when cross compiling (because of the"
         print_error "difficulties in finding the correct libraries)"
         print_error " "
       fi
     fi
   else
	# Pick up the paths from the user if possible
	if test -z "$x_includes" -a -n "$X11INC" ; then 
	    x_includes="$X11INC"
            XINCLUDES="-Ix_includes"
	fi
	if test -z "$x_libraries" -a -n "$X11LIB" ; then 
	    x_libraries="$X11LIB"
	fi
   fi
   if test -n "$x_includes" ; then
       XINCLUDES="-I$x_includes"
   fi
   if test -z "$no_x" ; then
       # may STILL not have x if the include files aren't around
       if test -n "$x_includes" ; then
           f_test=$x_includes/X11/Xlib.h
       else
           f_test=/usr/include/X11/Xlib.h
       fi
       if test ! -f $f_test ; then
           no_x=true
	   print_error "X11 include files were not found in $f_test!"
       fi
   fi
])dnl
dnl
dnl --------------------------------------------------------
dnl Internal subroutine of AC_FIND_X.  We had to update this because it
dnl did not know about R6.
define([AC_FIND_X_DIRECT],
[echo checking for X include and library files directly
AC_TEST_CPP([#include <X11/Intrinsic.h>], no_x=,
  for dir in \
    /usr/local/include \
    /usr/unsupported/include \
    /usr/x386/include \
    /usr/local/X11R6/include \
    /usr/local/X11R5/include \
    /usr/local/x11r6/include \
    /usr/local/x11r5/include \
    /usr/include/X11R6 \
    /usr/include/X11R5 \
    /usr/include/X11R4 \
    /usr/X11R6/include \
    /usr/X11R5/include \
    /usr/X11/include \
    /usr/openwin/include \
    /usr/openwin/share/include \
    /usr/lpp/Xamples/include \
    ; \
  do
    if test -r $dir/X11/Intrinsic.h; then
      x_includes=$dir; no_x=
      break
    fi
  done)

# Check for the libraries.  First see if replacing the `include' by
# `lib' works.
AC_HAVE_LIBRARY(Xt, no_x=,
for dir in `echo "$x_includes" | sed s/include/lib/` \
  /usr/local/lib \
  /usr/unsupported/lib \
  /usr/x386/lib \
  /usr/local/X11R6/lib \
  /usr/local/X11R5/lib \
  /usr/local/x11r6/lib \
  /usr/local/x11r5/lib \
  /usr/lib/X11 \
  /usr/lib/X11R4 \
  /usr/X11R6/lib \
  /usr/X11R5/lib \
  /usr/X11/lib \
  /usr/openwin/lib \
  /usr/lpp/Xamples/lib \
  ; \
do
  for extension in a so sl; do
    if test -r $dir/libXt.$extension; then
      x_libraries=$dir; no_x=
      break 2
    fi
  done
done)])dnl
dnl
dnl --------------------------------------------------------
dnl Test for the VERSION of tk.  There are major changes between 3.6 and 4.0
dnl (in particular, the type Tk_ColorModel disappeared
dnl  Put result into TK_VERSION (as, e.g., 3.6 or 4.0).  Should test version
dnl as STRING, since we don't control the changes between versions, and 
dnl only versions that we know should be tested.
dnl Note that this may be important ONLY if you include tk.h .
dnl TKINCDIR may also be defined if the include files are not where the
dnl architecture-dependant library files are
dnl
dnl TK_LIB and XINCLUDES must be defined (and no_x must NOT be true)
dnl
define(PAC_TK_VERSION,[
AC_MSG_CHECKING(for version of TK)
rm -f conftestval
#
# Some systems have a separate tcl dir; since we need both tcl and tk
# we include both directories
# Tk is going to load X11; if no X11, skip this step
if test -z "$no_x" -a -n "$TK_DIR" -a -n "$TCL_DIR" ; then
  CFLAGSsave="$CFLAGS"
  CFLAGS="$CFLAGS -I$TK_DIR/include -I$TCL_DIR/include $XINCLUDES"
  if test -n "$TKINCDIR" ; then
      CFLAGS="$CFLAGS -I$TKINCDIR/include"
  fi
  PAC_TEST_PROGRAM([#include "tk.h"
#include <stdio.h>
main() { FILE *fp = fopen( "conftestval", "w" ); 
fprintf( fp, "%d.%d", TK_MAJOR_VERSION, TK_MINOR_VERSION );
return 0; }],
  TK_VERSION=`cat conftestval`,TK_VERSION="unavailable")
  CFLAGS="$CFLAGSsave"
elif test -n "$wishloc" ; then
  # It is possible to use a wish program with
  # set tk_version [ string range $tk_patchLevel 0 2 ]
  # puts stdout $tk_version
  TK_VERSION="unavailable"
else
  TK_VERSION="unavailable"
fi
AC_MSG_RESULT($TK_VERSION)
])dnl
dnl
dnl Redefine these to use msg_checking/result
dnl Also, test for broken LINUX shells
dnl
define([AC_COMPILE_CHECK],
[AC_PROVIDE([$0])dnl
ifelse([$1], , , [AC_MSG_CHECKING(for $1)]
)dnl
if test ! -f confdefs.h ; then
    AC_MSG_RESULT("!! SHELL ERROR !!")
    echo "The file confdefs.h created by configure has been removed"
    echo "This may be a problem with your shell; some versions of LINUX"
    echo "have this problem.  See the Installation guide for more"
    echo "information."
    exit 1
fi
cat > conftest.c <<EOF
#include "confdefs.h"
[$2]
int main() { exit(0); }
int t() { [$3] }
EOF
dnl Don't try to run the program, which would prevent cross-configuring.
if eval $compile; then
  ifelse([$1], , , [AC_MSG_RESULT(yes)])
  ifelse([$4], , :, [
  $4
])
ifelse([$5], , , [else
  expandcompile=`echo "$compile" | sed -e 's/>.*//'`
  eval echo "configure:$expandcompile" >> config.log
  echo "configure: failed program was:" >>config.log
  cat confdefs.h >> config.log
  cat conftest.c >> config.log
  if test -s conftest.out ; then cat conftest.out >> config.log 
  else
      ${CC-cc} $CFLAGS conftest.c -o conftest $LDFLAGS $LIBS >> config.log 2>&1
  fi
  $5
])dnl
   ifelse([$1], , , ifelse([$5], ,else) [AC_MSG_RESULT(no)]
    expandcompile=`echo "$compile" | sed -e 's/>.*//'`
    eval echo "configure:$expandcompile" >> config.log
    echo "configure: failed program was:" >>config.log
    cat confdefs.h >> config.log
    cat conftest.c >> config.log
    if test -s conftest.out ; then cat conftest.out >> config.log 
    else
      ${CC-cc} $CFLAGS conftest.c -o conftest $LDFLAGS $LIBS >> config.log 2>&1
    fi
)
fi
rm -f conftest*]
)dnl
dnl
dnl This version compiles an entire function; used to check for
dnl things like varargs done correctly
dnl
dnl PAC_COMPILE_CHECK_FUNC(msg,function,if_true,if_false)
dnl
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
dnl (put function definition in the "header" slot)
define(PAC_COMPILE_CHECK_FUNC,
[AC_PROVIDE([$0])dnl
ifelse([$1], , , [AC_MSG_CHECKING(for $1)]
)dnl
if test ! -f confdefs.h ; then
    AC_MSG_RESULT("!! SHELL ERROR !!")
    echo "The file confdefs.h created by configure has been removed"
    echo "This may be a problem with your shell; some versions of LINUX"
    echo "have this problem.  See the Installation guide for more"
    echo "information."
    exit 1
fi
cat > conftest.c <<EOF
#include "confdefs.h"
[$2]
EOF
dnl Don't try to run the program, which would prevent cross-configuring.
if test -z "$ac_compile_link" ; then 
    ac_compile_link='${CC-cc} $CFLAGS conftest.c -o conftest $LDFLAGS $LIBS >>config.log 2>&1'
fi
echo "$ac_compile_link" >>config.log
cat conftest.c >>config.log
if eval $ac_compile_link; then
  ifelse([$1], , , [AC_MSG_RESULT(yes)])
  ifelse([$3], , :, [rm -rf conftest*
  $3
])
else
    rm -rf conftest*
    ifelse([$4], , , $4)
    ifelse([$1], , , [AC_MSG_RESULT(no)])
fi
rm -f conftest*]
)dnl
dnl
dnl
dnl checks for compiler characteristics
dnl
dnl AC_CROSS_CHECK is used to determine if we can run programs to 
dnl determine system characteristics.  The basic GNU autoconf test
dnl makes the assumption that return codes are properly handled.  In 
dnl some cases, the system will pretend to run a program and return a 
dnl 0 return code, but actually will fail.  In others, it can run the
dnl program, but because it doesn't properly handle return codes, might
dnl as well not.  
define([AC_CROSS_CHECK],
[AC_PROVIDE([$0])
if test -n "$pac_cv_cross_compiling" ; then
    if test "$pac_cv_cross_compiling" = "yes" ; then
	cross_compiling=1
    fi
else
    AC_MSG_CHECKING(whether cross-compiling)
    # If we cannot run a trivial program, we must be cross compiling.
    dnl This must use AC_TEST_PROGRAM, not PAC_TEST_PROGRAM, because
    dnl PAC_TEST_PROGRAM tries to use the TESTCC program
    AC_TEST_PROGRAM([main(){exit(0);}], pac_ok=1, pac_ok=0)
    if test $pac_ok = 1 ; then
        AC_TEST_PROGRAM([main(){exit(1);}], pac_ok=0 )
        if test $pac_ok = 1 ; then
	    pac_cv_cross_compiling="no"
            AC_MSG_RESULT(no)
        else
            cross_compiling=1
	    pac_cv_cross_compiling="yes"
            AC_MSG_RESULT(yes, because return codes handled incorrectly)
        fi
    else
        cross_compiling=1
        pac_cv_cross_compiling="yes"
        AC_MSG_RESULT(yes)
    fi
fi
])dnl
dnl
dnl Append SH style definitions to a file
dnl To generate a site file (for MAKE), use PAC_APPEND_FILE.  This allows
dnl you to use configure to create a likely site file.
dnl
dnl PAC_APPEND_FILE(varname,varvalue,file)
dnl Example: PAC_APPEND_FILE("CC",$CC,"make.site")
dnl
define(PAC_APPEND_FILE,[
if test "$3" = "-" ; then echo "$1=$2" ; else echo "$1=$2" >> $3 ; fi
])dnl
dnl
dnl See if Fortran compiler accepts -Idirectory flag
dnl 
dnl PAC_FORTRAN_HAS_INCDIR(directory,true-action,false-action)
dnl
dnl Fortran compiler is F77 and is passed FFLAGS
dnl
define(PAC_FORTRAN_HAS_INCDIR,[
AC_MSG_CHECKING([for Fortran include argument])
cat > $1/conftestf.h <<EOF
       call sub()
EOF
cat > conftest.f <<EOF
       program main
       include 'conftestf.h'
       end
EOF
echo "$F77 $FFLAGS -c -I$1 conftest.f" >> config.log
if $F77 $FFLAGS -c -I$1 conftest.f >> config.log 2>&1 ; then
    FINCARG="-I"
    ifelse($2,,true,$2)
    AC_MSG_RESULT([supports -I for include])
elif $F77 $FFLAGS -c -Wf,-I$1 conftest.f >> config.log 2>&1 ; then
    FINCARG="-Wf,-I"
    ifelse($2,,true,$2)
    AC_MSG_RESULT([supports -Wf,-I for include])
else
    ifelse($3,,true,$3)
    AC_MSG_RESULT([does NOT support -I or -Wf,-I for include])
fi
rm -f conftest.f $1/conftestf.h
])dnl
dnl
dnl PAC_FORTRAN_GET_INTEGER_SIZE(var_for_size)
dnl
dnl sets var_for_size to the size.  Ignores if the size cannot be determined
dnl
define(PAC_FORTRAN_GET_INTEGER_SIZE,
[AC_MSG_CHECKING([for size of Fortran INTEGER])
rm -f conftestval
rm -f conftestf.f conftestf.o
cat <<EOF > conftestf.f
      subroutine isize( )
      integer i(2)
      call cisize( i(1), i(2) )
      end
EOF
echo "$F77 $FFLAGS -c conftestf.f" >>config.log
if $F77 $FFLAGS -c conftestf.f >>config.log 2>&1 ; then 
    SaveLIBS="$LIBS"
    LIBS="conftestf.o $LIBS"
    PAC_TEST_PROGRAM([#include <stdio.h>
#ifdef FORTRANCAPS
#define cisize_ CISIZE
#define isize_  ISIZE
#elif defined(FORTRANNOUNDERSCORE)
#define cisize_ cisize
#define isize_  isize
#endif
static int isize_val;
void cisize_( i1p, i2p )
char *i1p, *i2p;
{
	isize_val = (i2p - i1p) * sizeof(char);
}
main() { 
  FILE *f=fopen("conftestval","w");
  
  if (!f) exit(1);
  isize_();
  fprintf( f, "%d\n", isize_val);
  exit(0);
}],Pac_CV_NAME=`cat conftestval`,Pac_CV_NAME="")
LIBS="$SaveLIBS"
else
   :
fi
if test -z "$Pac_CV_NAME" ; then
    # Try to compile/link with the Fortran compiler instead.  This
    # worked for the NEC SX-4
    compile_f='${CC-cc} $CFLAGS -c conftest.c; ${F77-f77} $FFLAGS -o conftest conftest.o $LDFLAGS $LIBS >config.log 2>&1'
    echo "$compile_f" >> config.log
    eval $compile_f
    if test ! -s conftest ; then 
	echo "Could not build executable program:"
	echo "${F77-f77} $FFLAGS -o conftest conftest.o $LDFLAGS $LIBS"
    else
	rm -f conftestout
	if test -s conftest && (./conftest;exit) 2>conftestout ; then
	    Pac_CV_NAME=`cat conftestval`
        fi
    fi

fi
rm -f conftestf.f conftestf.o
if test -n "$Pac_CV_NAME" -a "$Pac_CV_NAME" != 0 ; then
    AC_MSG_RESULT($Pac_CV_NAME)
else
    AC_MSG_RESULT(unavailable)
fi
$1=$Pac_CV_NAME
])dnl
dnl
dnl PAC_FORTRAN_GET_REAL_SIZE(var_for_size)
dnl
dnl sets var_for_size to the size.  Ignores if the size cannot be determined
dnl
dnl *** DO NOT CHANGE WITHOUT CHANGING sowing/confdb VERSION ***
dnl *** (new is PAC_PROG_F77_CHECK_SIZEOF)
define(PAC_FORTRAN_GET_REAL_SIZE,
[AC_MSG_CHECKING([for size of Fortran REAL])
rm -f conftestval
rm -f conftestf.f conftestf.o
cat <<EOF > conftestf.f
      subroutine isize( )
      real i(2)
      call cisize( i(1), i(2) )
      end
EOF
echo "$F77 $FFLAGS -c conftestf.f" >>config.log
if $F77 $FFLAGS -c conftestf.f >>config.log 2>&1 ; then 
    SaveLIBS="$LIBS"
    LIBS="conftestf.o $LIBS"
    PAC_TEST_PROGRAM([#include <stdio.h>
#ifdef FORTRANCAPS
#define cisize_ CISIZE
#define isize_  ISIZE
#elif defined(FORTRANNOUNDERSCORE)
#define cisize_ cisize
#define isize_  isize
#endif
static int isize_val;
void cisize_( i1p, i2p )
char *i1p, *i2p;
{
	isize_val = (i2p - i1p) * sizeof(char);
}
main() { 
  FILE *f=fopen("conftestval","w");
  
  if (!f) exit(1);
  isize_();
  fprintf( f, "%d\n", isize_val);
  exit(0);
}],Pac_CV_NAME=`cat conftestval`,Pac_CV_NAME="")
LIBS="$SaveLIBS"
else
   :
fi
rm -f conftestf.f conftestf.o
if test -n "$Pac_CV_NAME" -a "$Pac_CV_NAME" != 0 ; then
    AC_MSG_RESULT($Pac_CV_NAME)
else
    AC_MSG_RESULT(unavailable)
fi
$1=$Pac_CV_NAME
])dnl
dnl
dnl See if Fortran accepts ! for comments
dnl
dnl PAC_FORTRAN_HAS_EXCLAM_COMMENTS(action-if-true,action-if-false)
dnl
dnl *** DO NOT CHANGE WITHOUT CHANGING sowing/confdb VERSION ***
dnl *** (new is PAC_PROG_F77_EXCLAIM_COMMENTS)
define(PAC_FORTRAN_HAS_EXCLAM_COMMENTS,[
AC_MSG_CHECKING([for Fortran accepts ! for comments])
cat > conftest.f <<EOF
       program main
!      This is a comment
       end
EOF
echo "$F77 $FFLAGS -c conftest.f" >>config.log
if $F77 $FFLAGS -c conftest.f >> config.log 2>&1 ; then
    ifelse($1,,true,$1)
    AC_MSG_RESULT([yes])
else
    ifelse($2,,true,$2)
    AC_MSG_RESULT([no])
fi
rm -f conftest.f
])dnl
dnl
dnl tries to determine the Fortran 90 kind parameter for 8-byte integers
dnl
dnl Set decimal digits to 2 for int*1, 4 for int*2, 8 (or 9) for int*4, and
dnl 16 to 18 for int*8.  If not set, it assumes 16.
dnl PAC_FORTRAN_INT_KIND([variable to set to kind value],[decimal digits])
dnl The value is -1 if it is not available
dnl The second arg is the number of BYTES
dnl *** DO NOT CHANGE WITHOUT CHANGING sowing/confdb VERSION ***
define(PAC_FORTRAN_INT_KIND,[
AC_MSG_CHECKING([for Fortran 90 KIND parameter for ifelse($2,,8-byte,$2-byte) integers])
# We need to evaluate the second arg, which may be a runtime value
sellen="$2"
if test -z "$sellen" ; then 
    sellen=16
else 
    # Convert bytes to digits
    case $sellen in 
	1) sellen=2 ;;
	2) sellen=4 ;;
	4) sellen=8 ;;
	8) sellen=16 ;;
	16) sellen=30 ;;
        *) sellen=8 ;;
    esac
fi
rm -f conftest*
cat <<EOF > conftest.f
      program main
      integer i
      i = selected_int_kind($sellen)
      open(8, file="conftest1.out", form="formatted")
      write (8,*) i
      close(8)
      stop
      end
EOF
if test -z "$F90" ; then
   F90=f90
fi
KINDVAL=""
# We must be careful in case the F90LINKER isn't the same as F90
# (e.g., it has extra options or is a different program)
if $F90 -c -o conftest.o conftest.f $LDFLAGS >conftest.out 2>&1 ; then
    # Use F90 if we can (in case the linker prepares programs
    # for a parallel environment).
    echo "$F90 -o conftest conftest.o $LDFLAGS" >> config.log
    if $F90 -o conftest conftest.o $LDFLAGS >>config.log 2>&1 ; then
	F90LINKERTEST="$F90" 
    elif test -z "$F90LINKER" ; then 
	F90LINKERTEST="$F90"
    else
	F90LINKERTEST="$F90LINKER"
    fi
    if $F90LINKERTEST -o conftest conftest.o $LDFLAGS >conftest.out 2>&1 ; then
        ./conftest >>conftest.out 2>&1
        if test -s conftest1.out ; then
	    # Because of write, there may be a leading blank.
            KINDVAL=`cat conftest1.out | sed 's/ //g'`
        else
	    if test -s conftest.out ; then cat conftest.out >> config.log ; fi
	    KINDVAL=-1
        fi
    else
        echo "Failure to link program to test for INTEGER kind" >>config.log
        $F90LINKER -o conftest conftest.f $LDFLAGS >>config.log 2>&1
    fi
else 
   echo "Failure to build program to test for INTEGER kind" >>config.log
   $F90 -o conftest conftest.f $LDFLAGS >>config.log 2>&1
fi
rm -f conftest*
if test -n "$KINDVAL" -a "$KINDVAL" != "-1" ; then
   AC_MSG_RESULT($KINDVAL)
   ifelse($1,,,$1=$KINDVAL)
else
   AC_MSG_RESULT(unavailable)
   ifelse($1,,,$1="-1")
fi
])dnl
dnl
dnl
dnl Check that signal semantics work correctly
dnl
define(PAC_SIGNALS_WORK,[
AC_REQUIRE([AC_CROSS_CHECK])
AC_MSG_CHECKING([whether signals work correctly])
cat >conftest.c <<EOF
#include <signal.h>
static int rc = 0, irc = 1, maxcnt=5;
void handler( sig )
int sig;
{
void (*oldsig)();
oldsig = signal( SIGUSR1, handler );
if (oldsig != handler) rc = 1;
irc = 0;
}
int main(argc, argv)
int argc;
char **argv;
{
(void)signal( SIGUSR1, handler );
kill( getpid(), SIGUSR1 );
while (irc && maxcnt) { sleep(1); maxcnt--;}
return rc;
}
EOF
rm -f conftest.out
if eval ${CC-cc} $CFLAGS -o conftest conftest.c $LDFLAGS > conftest.out 2>&1 ; then
    if ./conftest ; then
	AC_MSG_RESULT(yes)
    else
	if test -s conftest.out ; then cat conftest.out >> config.log ; fi
        cat conftest.c >>config.log
	AC_MSG_RESULT(Signals reset when used!)
	AC_DEFINE(SIGNALS_RESET_WHEN_USED)
    fi
else
    if test -s conftest.out ; then cat conftest.out >> config.log ; fi
    cat conftest.c >>config.log
    AC_MSG_RESULT(Could not compile test program!)
fi
rm -f conftest conftest.c conftest.o conftest.out
])dnl
dnl
dnl
dnl record top-level directory (this one)
dnl A problem.  Some systems use an NFS automounter.  This can generate
dnl paths of the form /tmp_mnt/... . On SOME systems, that path is
dnl not recognized, and you need to strip off the /tmp_mnt. On others, 
dnl it IS recognized, so you need to leave it in.  Grumble.
dnl The real problem is that OTHER nodes on the same NFS system may not
dnl be able to find a directory based on a /tmp_mnt/... name.
dnl
dnl It is WRONG to use $PWD, since that is maintained only by the C shell,
dnl and if we use it, we may find the 'wrong' directory.  To test this, we
dnl try writing a file to the directory and then looking for it in the 
dnl current directory.  Life would be so much easier if the NFS automounter
dnl worked correctly.
dnl
dnl PAC_GETWD(varname [, filename ] )
dnl 
dnl Set varname to current directory.  Use filename (relative to current
dnl directory) if provided to double check.
dnl
dnl Need a way to use "automounter fix" for this.
dnl
define(PAC_GETWD,[
AC_MSG_CHECKING(for current directory name)
$1=$PWD
if test "${$1}" != "" -a -d "${$1}" ; then 
    if test -r ${$1}/.foo$$ ; then
        rm -f ${$1}/.foo$$
	rm -f .foo$$
    fi
    if test -r ${$1}/.foo$$ -o -r .foo$$ ; then
	$1=
    else
	echo "test" > ${$1}/.foo$$
	if test ! -r .foo$$ ; then
            rm -f ${$1}/.foo$$
	    $1=
        else
 	    rm -f ${$1}/.foo$$
	fi
    fi
fi
if test "${$1}" = "" ; then
    $1=`pwd | sed -e 's%/tmp_mnt/%/%g'`
fi
dnl
dnl First, test the PWD is sensible
ifelse($2,,,
if test ! -r ${$1}/$2 ; then
    dnl PWD must be messed up
    $1=`pwd`
    if test ! -r ${$1}/$2 ; then
	print_error "Cannot determine the root directory!" 
        exit 1
    fi
    $1=`pwd | sed -e 's%/tmp_mnt/%/%g'`
    if test ! -d ${$1} ; then 
        print_error "Warning: your default path uses the automounter; this may"
        print_error "cause some problems if you use other NFS-connected systems."
        $1=`pwd`
    fi
fi)
if test -z "${$1}" ; then
    $1=`pwd | sed -e 's%/tmp_mnt/%/%g'`
    if test ! -d ${$1} ; then 
        print_error "Warning: your default path uses the automounter; this may"
        print_error "cause some problems if you use other NFS-connected systems."
        $1=`pwd`
    fi
fi
AC_MSG_RESULT(${$1})
])dnl
dnl
dnl
dnl 
dnl This is *VERY* out of date.
define(PAC_GET_SPECIAL_SYSTEM_INFO,[
#
# We should provide a way to specify a particular IRIX version, rather 
# than requiring the this code to figure everything out.
# In particular, there are IRIX-like systems that do not have the 'hinv'
# command.
#
if test -n "$arch_IRIX" ; then
   AC_MSG_CHECKING(for IRIX OS version)
   dnl Every version and machine under IRIX is incompatible with every other
   dnl version.  This block of code replaces a generic "IRIX" arch value 
   dnl with 
   dnl  IRIX_<version>_<chip>
   dnl  For example
   dnl  IRIX_5_4400 (IRIX 5.x, using MIPS 4400)
   osversion=`uname -r | sed 's/\..*//'`
   dnl Note that we need to allow brackets here, so we briefly turn off 
   dnl the macro quotes
   changequote(,)dnl
   dnl Get the second field (looking for 6.1)
   dnl this handles 6.1.27
   dnl   osvminor=`uname -r | sed 's/[0-9]\.\([0-9]*\)\..*/\1/'`
   osvminor=`uname -r | sed 's/[0-9]\.\([0-9]*\).*/\1/'`
   AC_MSG_RESULT($osversion)
   dnl Get SGI processor count by quick hack
   dnl 7/13/95, bri@sgi.com
   AC_MSG_CHECKING(for IRIX cpucount)
   cpucount=`hinv | grep '[0-9]* [0-9]* MHZ IP[0-9]* Proc' | cut -f 1 -d' '`
   if test "$cpucount" = "" ; then
     cpucount=`hinv | grep 'Processor [0-9]*:' | wc -l | sed -e 's/ //g'`
   fi
   changequote([,])dnl
   if test "$cpucount" = "" ; then
     print_error "Could not determine cpucount."
     print_error "Please send "
     hinv
     print_error "to mpi-bugs@mcs.anl.gov"
     exit 1
   fi
   AC_MSG_RESULT($cpucount)
   if test -z "$PROCESSOR_COUNT" ; then PROCESSOR_COUNT=$cpucount ; fi
   AC_DEFINE_UNQUOTED(PROCESSOR_COUNT, $PROCESSOR_COUNT)
   dnl
   dnl 
   dnl Check for fast SGI device
   if test -d mpid/sgi -a "$osversion" -ge 6 -a "$osvminor" -ge 1 -a \
	`uname -s` = "IRIX64" ; then
	if test -z "$device_sgi" ; then
	    echo "Consider using -device=sgi for SGI arrays"
	fi
   elif test -n "$device_sgi" ; then
	print_error "The sgi device requires IRIX64 and version 6.1 or later"
        exit 1
   fi
   dnl
   dnl Set -comm=shared if IRIX MP & COMM=ch_p4 & COMM not explicitly set
   dnl 7/13/95 bri@sgi.com
   if test $cpucount -gt 1 ; then
     if test "$COMM" = "ch_p4" ; then
       if test "$default_comm" = "1" ; then
         echo "IRIX multiprocessor & p4, setting -comm=shared"
         echo "  (configure with -comm=ch_p4 to disable shared memory)"
         COMM="shared"
       fi
     fi
   fi

   AC_MSG_CHECKING(for IRIX cpumodel)
   dnl The tail -1 is necessary for multiple processor SGI boxes
   dnl We might use this to detect SGI multiprocessors and recommend
   dnl -comm=shared
   cputype=`hinv -t cpu | tail -1 | cut -f 3 -d' '`
   if test -z "$cputype" ; then
	print_error "Could not get cputype from hinv -t cpu command."
	print_error "Please send "
	hinv -t cpu 2>&1
	hinv -t cpu | cut -f 3 -d' ' 2>&1
	print_error "to mpi-bugs@mcs.anl.gov" 
	exit 1
   fi
   AC_MSG_RESULT($cputype)
   dnl echo "checking for osversion and cputype"
   dnl cputype may contain R4400, R2000A/R3000, or something else.  
   dnl We may eventually need to look at it.
   if test -z "$osversion" ; then
	print_error "Could not determine OS version.  Please send" 
        print_error " " 
	uname -a
	print_error "to mpi-bugs@mcs.anl.gov" 
        exit 1
   elif test $osversion = 4 ; then
	dnl Nathan told us that things worked for IRIX 4 as well; 
	dnl however, we need 'ar ts libname' (ranlib) on version 4 but 
	dnl not the others
        true
   elif test $osversion = 5 ; then
	true
   elif test $osversion = 6 ; then
	true
   else 
       print_error "Could not recognize the version of IRIX (got $osversion)"
       print_error "MPICH knows about versions 4, 5 and 6; the version being"
       print_error "returned from uname -r is $osversion."
       print_error "Please send"
       uname -a 2>&1
       hinv 2>&1
       print_error "to mpi-bugs@mcs.anl.gov"
       exit 1
   fi
   AC_MSG_CHECKING(for cputype)
   OLD_ARCH=IRIX
   IRIXARCH="$ARCH_$osversion"
   dnl Now, handle the chip set
   changequote(,)dnl
   cputype=`echo $cputype | sed -e 's%.*/%%' -e 's/R//' | tr -d "[A-Z]"`
   changequote([,])dnl
   case $cputype in 
	3000) ;;
	4000) ;;
	4400) ;;
	4600) ;;
	5000) ;;
	8000) ;;
	10000);;
	12000);;
	14000);;
        *)
	print_error "Unexpected IRIX/MIPS chipset $cputype.  Please send the output"
	print_error " "
        uname -a 2>&1
        hinv 2>&1 
 	print_error " " 
        print_error "to mpi-bugs@mcs.anl.gov" 
	print_error "MPICH will continue and assume that the cputype is"
        print_error "compatible with a MIPS 4400 processor."
 	print_error " " 
        cputype=4400
	;;
   esac
   AC_MSG_RESULT($cputype)
   IRIXARCH="$IRIXARCH_$cputype"
   echo "IRIX-specific architecture is $IRIXARCH"
fi
])dnl
dnl
dnl Check that ranlib works, and is not just a noisy stub
dnl We do this by creating a small object file
dnl and a trial library, and then ranlib the result.
dnl Finally, we try to link with the library (the IRIX Ranlib exists, but
dnl destroys the archive.  User-friendly, it isn't).
dnl
dnl Requires that CC, AR, and RANLIB already be defined.
dnl
define(PAC_RANLIB_WORKS,[
AC_MSG_CHECKING(whether ranlib works)
broken=0
cat <<EOF >conftest.c
int a(){return 1;}
EOF
rm -f conftest.out
compileonly='${CC-cc} -c $CFLAGS conftest.c >conftest.out 2>&1'
if eval $compileonly ; then 
    :
else
    if test -s conftest.out ; then cat conftest.out >> config.log ; fi
    broken=1;
fi
rm conftest.out
if test $broken = 1 ; then
    AC_MSG_RESULT(no)
    print_error "Error in creating test object for ranlib!"
else
    arcmd='$AR cr foo.a conftest.o >conftest.out 2>&1'
    eval $arcmd
    ranlibtest='$RANLIB foo.a >>conftest.out 2>&1'
    if eval $ranlibtest ; then
        : 
    else
	if test -s conftest.out ; then cat conftest.out >> config.log ; fi
        broken=1
    fi
    rm conftest.out
    cat <<EOF >conftest.c
int a(); int main(argc,argv)int argc; char **argv;{ return a();}
EOF
    compileonly='${CC-cc} -c $CFLAGS conftest.c >conftest.out 2>&1'
    if eval $compileonly ; then 
        : 
    else
        broken=1
	if test -s conftest.out ; then cat conftest.out >> config.log ; fi
    fi
    rm conftest.out
    if test $broken = 1 ; then
        AC_MSG_RESULT(no)
        print_error "Error in creating test program for ranlib test!"
    else
	# Check that we can link the program
	echo $CLINKER $CFLAGS $LDFLAGS conftest.o -o conftest foo.a $LIBS >> \
		config.log
        if eval $CLINKER $CFLAGS $LDFLAGS conftest.o -o conftest foo.a $LIBS \
		>>config.log 2>&1 ; then
	    AC_MSG_RESULT(yes)
	else
	    AC_MSG_RESULT(no)
	    print_error "Error linking with ranlibed library"
	    broken=1
        fi
    fi
    rm -f foo.a
    if test $broken = 1 ; then
        print_error "RANLIB ($RANLIB) failed!"
        print_error "Assuming that ranlib is a stub returning non-zero"
        print_error "condition code"
        RANLIB=':'
    fi
fi
rm -f conftest.o conftest.c
dnl 
dnl Check that ranlib doesn't need -c option (Mac OSX needs this; FreeBSD
dnl probably does too)
AC_MSG_CHECKING([for broken handling of common symbols])
result="could not test"
cat > conftest.c <<EOF
extern int foo;
int main( int argc, char **argv ) { return foo; }
EOF
cat > conftest1.c <<EOF
extern int foo;
int foo;
EOF
dnl
dnl Compile the main program
compileonly='${CC-cc} -c $CFLAGS conftest.c >conftest.out 2>&1'
if eval $compileonly ; then 
    :
else
    if test -s conftest.out ; then cat conftest.out >> config.log ; fi
    broken=1;
fi
dnl
dnl Build the library
compileonly='${CC-cc} -c $CFLAGS conftest1.c >conftest.out 2>&1'
if eval $compileonly ; then 
    arcmd='$AR cr libfoo.a conftest1.o >conftest.out 2>&1'
    eval $arcmd
    ranlibtest='$RANLIB libfoo.a >>conftest.out 2>&1'
    if eval $ranlibtest ; then
        if ${CLINKER} -o conftest conftest.o -L. -lfoo >>conftest.out 2>&1 ; then
	    result=no
	else
	    # try ranlib -c
	    ranlibtest='$RANLIB -c libfoo.a >>conftest.out 2>&1'
	    if eval $ranlibtest ; then
	        if ${CLINKER} -o conftest conftest.o -L. -lfoo >>conftest.out 2>&1 ; then
		    result="yes! using -c to fix"
		    RANLIB="$RANLIB -c"
                fi
            fi
	fi
    else
	if test -s conftest.out ; then cat conftest.out >> config.log ; fi
        broken=1
    fi
    rm -f libfoo.a conftest*
else
    if test -s conftest.out ; then cat conftest.out >> config.log ; fi
    broken=1;
fi
AC_MSG_RESULT($result)
])dnl
dnl
dnl PAC_OUTPUT_EXEC(files[,mode]) - takes files (as shell script or others),
dnl and applies configure to the them.  Basically, this is what AC_OUTPUT
dnl should do, but without adding a comment line at the top.
dnl Must be used ONLY after AC_OUTPUT (it needs config.status, which 
dnl AC_OUTPUT creates).
dnl Optionally, set the mode (+x, a+x, etc)
dnl
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
dnl (acoutput allows this now)
define(PAC_OUTPUT_EXEC,[
CONFIG_FILES="$1"
export CONFIG_FILES
./config.status
CONFIG_FILES=""
for pac_file in $1 ; do 
    rm -f .pactmp
    sed -e '1d' $pac_file > .pactmp
    rm -f $pac_file
    mv .pactmp $pac_file
    ifelse($2,,,chmod $2 $pac_file)
done
])dnl
dnl
dnl PAC_FIND_FCLIB( [carch] )
dnl
dnl Find the libraries needed to link Fortran routines with C main programs
dnl This is ONLY an approximation but DOES handle some simple cases.
dnl Sets FCLIB if it can.  Fortran compiler FULL PATH would help.
dnl
dnl *** DO NOT CHANGE WITHOUT CHANGING sowing/confdb VERSION ***
dnl *** (superceeded by autoconf-2 AC_F77_LIBRARY_LDFLAGS)
define(PAC_FIND_FCLIB,[
if test -n "$F77" ; then
PAC_PROGRAM_CHECK(FCVal,$F77,,,FCFULLPATH)
AC_MSG_CHECKING([for Fortran libraries to link C programs with])
case $1 in 
    sun4)
    if test "$FCFULLPATH" = /usr/lang/f77 ; then
	# Look for /usr/lang/SC... .   This is tricky, because 
	# we want to find the LATEST versions first
	for dir in /usr/lang/SC2*.*.* /usr/lang/SC2.* /usr/lang/SC2* \
	 	 /usr/lang/SC1.*.* /usr/lang/SC1.* /usr/lang/SC1* ; do
	    if test -d $dir ; then
		if test -s $dir/libF77.a ; then
		    FCLIB="$dir/libF77.a -lm"
		    if test -s $dir/libm.a ; then
			FCLIB="$FCLIB $dir/libm.a"
		    fi
	            break
	        fi
            fi
        done
    fi
    ;;
    solaris)
	# /opt/SUNWspro/SC*/lib/name
	for file in libF77.a libM77.a libsunmath.a ; do
	    for dir in /opt/SUNWspro/SC4.*/lib /opt/SUNWspro/SC4*/lib \
		       /opt/SUNWspro/SC3.* /opt/SUNWspro/SC3* \
		       /opt/SUNWspro/SC2.* /opt/SUNWspro/SC2* ; do
  	        if test -d $dir ; then
		    if test -s $dir/$file ; then
			FCLIB="$FCLIB $dir/$file"
			break
		    fi
                fi
            done
        done
    ;;

    rs6000)
	for file in /usr/lib/libxlf.a /usr/lib/libxlf90.a ; do
	    if test -s $file ; then
		FCLIB="$FCLIB $file"
            fi
	done
	if test -s /usr/lpp/xlf/lib/lowsys.exp ; then
	    FCLIB="$FCLIB /usr/lpp/xlf/lib/lowsys.exp"
	fi
	;;
    IRIX64|IRIX)
	AC_MSG_RESULT()
        SaveDEFS="$DEFS"
        SaveLIBS="$LIBS"
	for lib in fpe sun F77 U77 I77 isam ; do
	    AC_HAVE_LIBRARY($lib,FCLIB="$FCLIB -l$lib")
        done
        DEFS="$SaveDEFS"
	LIBS="$SaveLIBS"
    ;;
    alpha)
	for file in libfor.a libutil.a libFutil.a libots.a ; do
	    if test -s /usr/lib/$file ; then
		FCLIB="$FCLIB /usr/lib/$file"
            fi
	done
    ;;
    freebsd|linux)
	AC_MSG_RESULT()
        SaveDEFS="$DEFS"
        SaveLIBS="$LIBS"
	for lib in f2c m ; do
	    AC_HAVE_LIBRARY($lib,FCLIB="$FCLIB -l$lib")
        done
        DEFS="$SaveDEFS"
	LIBS="$SaveLIBS"
    ;;
    hpux)
	for file in libf.a libf.sl ; do
	    if test -s /usr/lib/$file ; then
		FCLIB="$FCLIB /usr/lib/$file"
            fi
	done
    ;;
    paragon)
	AC_MSG_RESULT()
        SaveDEFS="$DEFS"
        SaveLIBS="$LIBS"
	for lib in f ; do
	    AC_HAVE_LIBRARY($lib,FCLIB="$FCLIB -l$lib")
        done
        DEFS="$SaveDEFS"
	LIBS="$SaveLIBS"
    ;;
    *)
    :
    ;;
esac
if test -n "$FCLIB" ; then
    AC_MSG_RESULT(found $FCLIB)
else
    AC_MSG_RESULT(none)
fi
fi
])dnl
dnl
dnl This is drawn from version 2; it understands g++ as well as gcc
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
dnl define(AC_FD_CC, 5)dnl
define(AC_FD_CC, 2)dnl
dnl [#] AC_FD_CC compiler messages saved in config.log
dnl if test "$silent" = yes; then
dnl  exec AC_FD_MSG>/dev/null
dnl else
dnl   exec AC_FD_MSG>&1
dnl fi
dnl exec AC_FD_CC>./config.log
dnl
dnl
dnl AC_TRY_LINK(INCLUDES, FUNCTION-BODY,
dnl             ACTION-IF-FOUND [, ACTION-IF-NOT-FOUND])
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
define(AC_TRY_LINK,[
if test -z "$ac_ext" ; then 
    ac_ext=c
fi
cat > conftest.$ac_ext <<EOF
dnl This sometimes fails to find confdefs.h, for some reason.
dnl [#]line __oline__ "[$]0"
dnl [#]line __oline__ "configure"
#include "confdefs.h"
[$1]
int main() { return 0; }
int t() {
[$2]
; return 0; }
EOF
rm -f conftest.out
if test -z "$ac_link" ; then
ac_link='${CC-cc} -o conftest $CFLAGS $CPPFLAGS $LDFLAGS conftest.$ac_ext $LIBS >conftest.out 2>&1'
fi
if eval $ac_link; then
  ifelse([$3], , :, [rm -rf conftest*
  $3])
else
  echo "$ac_link" >>config.log
  echo "Failed program was" >>config.log
  cat conftest.$ac_ext >>config.log
  if test -s conftest.out ; then cat conftest.out >> config.log ; fi
ifelse([$4], , , [rm -rf conftest*
  $4
])dnl
fi
rm -f conftest*]
)dnl
dnl
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
define([AC_CHECK_FUNC],
[AC_MSG_CHECKING([for $1])
dnl AC_CACHE_VAL(ac_cv_func_$1,[
AC_TRY_LINK(
[#include <ctype.h> /* Arbitrary system header to define __stub macros. */
#ifdef __cplusplus
extern "C" { char $1(); };
#else
char $1();
#endif],
/* The GNU C library defines this for functions which it implements
    to always fail with ENOSYS.  Some functions are actually named
    something starting with __ and the normal name is an alias.  */
#if defined (__stub_$1) || defined (__stub___$1)
choke me
#else
/* Override any gcc2 internal prototype to avoid an error.  */
[$1();
#endif
], eval "ac_cv_func_$1=yes", eval "ac_cv_func_$1=no")dnl        ])dnl

if eval "test \"`echo '$ac_cv_func_'$1`\" = yes"; then
  AC_MSG_RESULT(yes)
  ifelse([$2], , :, [$2])
else
  AC_MSG_RESULT(no)
ifelse([$3], , , [$3
])dnl
fi
])dnl
dnl PAC_LANG_C()
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
define(PAC_LANG_C,
[define([AC_LANG], [C])dnl
ac_ext=c
# CFLAGS is not in ac_cpp because -g, -O, etc. are not valid cpp options.
ac_cpp='$CPP $CPPFLAGS'
ac_compile='${CC-cc} $CFLAGS $CPPFLAGS conftest.$ac_ext -c 1>&AC_FD_CC 2>&AC_FD_CC'
ac_link='${CC-cc} $CFLAGS $CPPFLAGS $LDFLAGS conftest.$ac_ext -o conftest $LIBS 1>&AC_FD_CC 2>&AC_FD_CC'
])dnl
dnl
dnl PAC_LANG_CPLUSPLUS()
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
define(PAC_LANG_CPLUSPLUS,
[define([AC_LANG], [CPLUSPLUS])dnl
ac_ext=C
# CXXFLAGS is not in ac_cpp because -g, -O, etc. are not valid cpp options.
ac_cpp='$CXXCPP $CPPFLAGS'
ac_compile='${CXX-gcc} $CXXFLAGS $CPPFLAGS conftest.$ac_ext -c 1>&AC_FD_CC 2>&AC_FD_CC'
ac_link='${CXX-gcc} $CXXFLAGS $CPPFLAGS $LDFLAGS conftest.$ac_ext -o conftest $LIBS 1>&AC_FD_CC 2>&AC_FD_CC'
])dnl
dnl
dnl *** THIS IS SUPERCEEDED BY AN AUTOCONF 2 MACRO ***
dnl Fortran extensions
dnl
dnl PAC_FORTRAN_HAS_POINTER(action-if-true,action-if-false)
dnl
dnl Checks that you can do
dnl
dnl integer M
dnl pointer (MPTR,M)
dnl data MPTR/0/
dnl 
dnl if F77_VERBOSE defined, prints why it failed to find
dnl pointer
dnl
dnl *** DO NOT CHANGE WITHOUT CHANGING sowing/confdb VERSION ***
define(PAC_FORTRAN_HAS_POINTER,[
AC_MSG_CHECKING([whether Fortran has pointer declaration])
cat > conftest.f <<EOF
        program main
        integer M
        pointer (MPTR,M)
        data MPTR/0/
        end
EOF
rm -f conftest.out
$F77 $FFLAGS -c conftest.f > conftest.out 2>&1
if test $? != 0 ; then
    AC_MSG_RESULT(no)
    if test -n "$F77_VERBOSE" ; then
        echo "Fortran compiler returned non-zero return code"
        if test -s conftest.out ; then
	    echo "Output from test was"
            cat conftest.out
        fi
    fi
    ifelse([$2],,:,[$2])
elif test ! -s conftest.o ; then
    AC_MSG_RESULT(no)
    if test -n "$F77_VERBOSE" ; then
        echo "Fortran compiler did not produce object file"
        if test -s conftest.out ; then
	    echo "Output from test was"
            cat conftest.out
        fi
    fi
    ifelse([$2],,:,[$2])
else    
    AC_MSG_RESULT(yes)
    ifelse([$1],,:,[$1])
fi
rm -f conftest* 
])dnl
dnl
dnl There is a bug in AC_PREPARE that sets the srcdir incorrectly (it
dnl is correct in configure, but it puts an absolute path into config.status,
dnl which is a big problem for scripts like mpireconfig that are wrappers
dnl around config.status).  The bug is in not recognizing that ./ and .//
dnl are the same  directory as . (in fact, ./[/]* is the same).
dnl
define(PAC_FIXUP_SRCDIR,[
# Find the source files, if location was not specified.
if test "$srcdirdefaulted" = "yes" ; then
  srcdir=""
  # Try the directory containing this script, then `..'.
  prog=[$]0
changequote(,)dnl
  confdir=`echo $prog|sed 's%/[^/][^/]*$%%'`
  # Remove all trailing /'s 
  confdir=`echo $confdir|sed 's%[/*]$%%'`
changequote([,])dnl
  test "X$confdir" = "X$prog" && confdir=.
  srcdir=$confdir
  if test ! -r $srcdir/$unique_file; then
    srcdir=..
  fi
fi
if test ! -r $srcdir/$unique_file; then
  if test x$srcdirdefaulted = xyes; then
    echo "configure: Cannot find sources in \`${confdir}' or \`..'." 1>&2
  else
    echo "configure: Cannot find sources in \`${srcdir}'." 1>&2
  fi
  exit 1
fi
# Preserve a srcdir of `.' to avoid automounter screwups with pwd.
# (and preserve ./ and .//)
# But we can't avoid them for `..', to make subdirectories work.
case $srcdir in
  .|./|.//|/*|~*) ;;
  *) srcdir=`cd $srcdir; pwd` ;; # Make relative path absolute.
esac
])
dnl
dnl Solaris blew the declarations for gettimeofday...
dnl
dnl PAC_IS_GETTIMEOFDAY_OK(ok_action,failure_action)
dnl
dnl *** DO NOT CHANGE WITHOUT CHANGING sowing/confdb VERSION ***
define(PAC_IS_GETTIMEOFDAY_OK,[
AC_MSG_CHECKING(for how many arguments gettimeofday takes)
# Test sets "wierd" only for FAILURE to accept 2
PAC_TEST_PROGRAM([#include <sys/time.h>
main() {struct timeval tp;
gettimeofday(&tp,(void*)0);return 0;}],AC_MSG_RESULT(two - whew)
$1,
AC_MSG_RESULT(one!)
$2)
])
dnl
dnl Set USE_STDARG if stdargs work correctly.  Sets var to 1 if it is, 
dnl 0 otherwise
dnl
dnl PAC_STDARG_CORRECT(var)
define([PAC_STDARG_CORRECT],[
PAC_COMPILE_CHECK_FUNC(stdarg is correct,[
#include <stdio.h>
#include <stdarg.h>
int func( int a, ... ){
int b;
va_list ap;
va_start( ap, a );
b = va_arg(ap, int);
printf( "%d-%d\n", a, b );
va_end(ap);
fflush(stdout);
return 0;
}
int main() { func( 1, 2 ); return 0;}],$1=1,$1=0)])
dnl
dnl From aclocal.m4 in MPITEST (configure for Intel Test Suite)
dnl Added INTEGER*8
dnl
dnl PAC_TEST_FORTTYPES tests to see if the following fortran datatypes are
dnl supported: INTEGER1, INTEGER2, INTEGER4, REAL4, REAL8, DOUBLE_COMPLEX
dnl
dnl *** DO NOT CHANGE WITHOUT CHANGING sowing/confdb VERSION ***
define(PAC_TEST_FORTTYPES,dnl
   [
FIX_FILE=0
FORT_INT1=1
FORT_INT2=1
FORT_INT4=1
FORT_INT8=1
FORT_REAL4=1
FORT_REAL8=1
FORT_DOUBLE_COMPLEX=1
COUNT=13
AC_MSG_CHECKING(for integer * 1)
cat > testfort.f <<EOF
        subroutine forttype( a )
        integer*1 a
        return
        end
EOF
   echo "$F77 $FFLAGS -c testfort.f" >>config.log
   $F77 $FFLAGS -c testfort.f >> config.log 2>&1
   if test ! -s testfort.o ; then
       AC_MSG_RESULT(no)
       FORT_INT1=0
       FIX_FILE=1
       COUNT=`expr ${COUNT} - 1`
   else
       AC_MSG_RESULT(yes)
   fi
   rm -f testfort.f testfort.o
dnl
AC_MSG_CHECKING(for integer * 2)
    cat > testfort.f <<EOF
        subroutine forttype( a )
        integer*2 a
	return
        end
EOF
   echo "$F77 $FFLAGS -c testfort.f" >>config.log
   $F77 $FFLAGS -c testfort.f >> config.log 2>&1
   if test ! -s testfort.o ; then
       AC_MSG_RESULT(no)
       FORT_INT2=0
       FIX_FILE=1
       COUNT=`expr ${COUNT} - 1`
   else
       AC_MSG_RESULT(yes)
   fi
   rm -f testfort.f testfort.o
dnl
AC_MSG_CHECKING(for integer * 4)
    cat > testfort.f <<EOF
        subroutine forttype( a )
        integer*4 a
	return
        end
EOF
   echo "$F77 $FFLAGS -c testfort.f" >>config.log
   $F77 $FFLAGS -c testfort.f >> config.log 2>&1
   if test ! -s testfort.o ; then
       AC_MSG_RESULT(no)
       FORT_INT4=0
       FIX_FILE=1
       COUNT=`expr ${COUNT} - 1`
   else
       AC_MSG_RESULT(yes)
   fi
   rm -f testfort.f testfort.o
dnl
AC_MSG_CHECKING(for integer * 8)
    cat > testfort.f <<EOF
        subroutine forttype( a )
        integer*8 a
	return
        end
EOF
   echo "$F77 $FFLAGS -c testfort.f" >>config.log
   $F77 $FFLAGS -c testfort.f >>config.log 2>&1
   if test ! -s testfort.o ; then
       AC_MSG_RESULT(no)
       FORT_INT8=0
       FIX_FILE=1
       COUNT=`expr ${COUNT} - 1`
   else
       AC_MSG_RESULT(yes)
   fi
   rm -f testfort.f testfort.o
dnl
AC_MSG_CHECKING(for integer * 16)
    cat > testfort.f <<EOF
        subroutine forttype( a )
        integer*16 a
	return
        end
EOF
   echo "$F77 $FFLAGS -c testfort.f" >>config.log
   $F77 $FFLAGS -c testfort.f >> config.log 2>&1
   if test ! -s testfort.o ; then
       AC_MSG_RESULT(no)
       FORT_INT16=0
       FIX_FILE=1
       COUNT=`expr ${COUNT} - 1`
   else
       AC_MSG_RESULT(yes)
   fi
   rm -f testfort.f testfort.o
dnl
AC_MSG_CHECKING(for real * 4)
    cat > testfort.f <<EOF
        subroutine forttype( a )
        real*4 a
	return
        end
EOF
   echo "$F77 $FFLAGS -c testfort.f" >>config.log
   $F77 $FFLAGS -c testfort.f >> config.log 2>&1
   if test ! -s testfort.o ; then
       AC_MSG_RESULT(no)
       FORT_REAL4=0
       FIX_FILE=1
       COUNT=`expr ${COUNT} - 1`
   else
       AC_MSG_RESULT(yes)
   fi
   rm -f testfort.f testfort.o
dnl
AC_MSG_CHECKING(for real * 8)
    cat > testfort.f <<EOF
        subroutine forttype( a )
        real*8 a
	return
        end
EOF
   echo "$F77 $FFLAGS -c testfort.f" >>config.log
   $F77 $FFLAGS -c testfort.f >> config.log 2>&1
   if test ! -s testfort.o ; then
       AC_MSG_RESULT(no)
       FORT_REAL8=0
       FIX_FILE=1
       COUNT=`expr ${COUNT} - 1`
   else
       AC_MSG_RESULT(yes)
   fi
   rm -f testfort.f testfort.o
dnl
AC_MSG_CHECKING(for real * 16)
    cat > testfort.f <<EOF
        subroutine forttype( a )
        real*16 a
	return
        end
EOF
   echo "$F77 $FFLAGS -c testfort.f" >>config.log
   $F77 $FFLAGS -c testfort.f >> config.log 2>&1
   if test ! -s testfort.o ; then
       AC_MSG_RESULT(no)
       FORT_REAL16=0
       FIX_FILE=1
       COUNT=`expr ${COUNT} - 1`
   else
       AC_MSG_RESULT(yes)
   fi
   rm -f testfort.f testfort.o
dnl
AC_MSG_CHECKING(for double complex)
    cat > testfort.f <<EOF
        subroutine forttype( a )
        double complex a
	return
        end
EOF
   echo "$F77 $FFLAGS -c testfort.f" >>config.log
   $F77 $FFLAGS -c testfort.f >> config.log 2>&1
   if test ! -s testfort.o ; then
       AC_MSG_RESULT(no)
       FORT_DOUBLE_COMPLEX=0
       FIX_FILE=1
       COUNT=`expr ${COUNT} - 1`
   else
       AC_MSG_RESULT(yes)
   fi
   rm -f testfort.f testfort.o
dnl
AC_MSG_CHECKING(for complex * 8)
    cat > testfort.f <<EOF
        subroutine forttype( a )
        complex*8 a
	return
        end
EOF
   echo "$F77 $FFLAGS -c testfort.f" >>config.log
   $F77 $FFLAGS -c testfort.f >> config.log 2>&1
   if test ! -s testfort.o ; then
       AC_MSG_RESULT(no)
       FORT_COMPLEX8=0
       FIX_FILE=1
       COUNT=`expr ${COUNT} - 1`
   else
       AC_MSG_RESULT(yes)
   fi
   rm -f testfort.f testfort.o
dnl
AC_MSG_CHECKING(for complex * 16)
    cat > testfort.f <<EOF
        subroutine forttype( a )
        complex*16 a
	return
        end
EOF
   echo "$F77 $FFLAGS -c testfort.f" >>config.log
   $F77 $FFLAGS -c testfort.f >> config.log 2>&1
   if test ! -s testfort.o ; then
       AC_MSG_RESULT(no)
       FORT_COMPLEX16=0
       FIX_FILE=1
       COUNT=`expr ${COUNT} - 1`
   else
       AC_MSG_RESULT(yes)
   fi
   rm -f testfort.f testfort.o
dnl
AC_MSG_CHECKING(for complex * 32)
    cat > testfort.f <<EOF
        subroutine forttype( a )
        complex*32 a
	return
        end
EOF
   echo "$F77 $FFLAGS -c testfort.f" >>config.log
   $F77 $FFLAGS -c testfort.f >> config.log 2>&1
   if test ! -s testfort.o ; then
       AC_MSG_RESULT(no)
       FORT_COMPLEX32=0
       FIX_FILE=1
       COUNT=`expr ${COUNT} - 1`
   else
       AC_MSG_RESULT(yes)
   fi
   rm -f testfort.f testfort.o
dnl
   ])dnl
dnl
dnl
dnl PAC_CHECK_COMPILER_OPTION(optionname,action-if-ok,action-if-fail)
dnl This is now careful to check that the output is different, since 
dnl some compilers are noisy.
dnl 
dnl We are extra careful to prototype the functions incase compiler options
dnl that complain about poor code are in effect
dnl
dnl Side effect: If compiler option works, it is added to CFLAGS
dnl
dnl *** DO NOT CHANGE WITHOUT CHANGING sowing/confdb VERSION ***
define([PAC_CHECK_COMPILER_OPTION],[
AC_MSG_CHECKING([whether C compiler accepts option $1])
CFLAGSSAV="$CFLAGS"
CFLAGS="$1 $CFLAGS"
rm -f conftest.out
echo 'int try(void);int try(void){return 0;}' > conftest2.c
echo 'int main(void);int main(void){return 0;}' > conftest.c
if ${CC-cc} $CFLAGSSAV -o conftest conftest.c $LDFLAGS >conftest.bas 2>&1 ; then
   if ${CC-cc} $CFLAGS -o conftest conftest.c $LDFLAGS >conftest.out 2>&1 ; then
      if diff -b conftest.out conftest.bas >/dev/null 2>&1 ; then
         AC_MSG_RESULT(yes)
         AC_MSG_CHECKING([whether routines compiled with $1 can be linked with ones compiled  without $1])       
         rm -f conftest.out
         rm -f conftest.bas
         if ${CC-cc} -c $CFLAGSSAV conftest2.c >conftest2.out 2>&1 ; then
            if ${CC-cc} $CFLAGS -o conftest conftest2.o conftest.c $LDFLAGS >conftest.bas 2>&1 ; then
               if ${CC-cc} $CFLAGS -o conftest conftest2.o conftest.c $LDFLAGS >conftest.out 2>&1 ; then
                  if diff -b conftest.out conftest.bas >/dev/null 2>&1 ; then
	             AC_MSG_RESULT(yes)	  
                     $2
                  elif test -s conftest.out ; then
	             cat conftest.out >> config.log
	             AC_MSG_RESULT(no)
                     print_error "Will not add $1 to CFLAGS"
                     CFLAGS="$CFLAGSSAV"
	             $3
                  else
                     AC_MSG_RESULT(no)
                     print_error "Will not add $1 to CFLAGS"
                     CFLAGS="$CFLAGSSAV"
	             $3
                  fi  
               else
	          if test -s conftest.out ; then
	             cat conftest.out >> config.log
	          fi
                  AC_MSG_RESULT(no)
                  print_error "Will not add $1 to CFLAGS"
                  CFLAGS="$CFLAGSSAV"
                  $3
               fi
	    else
               # Could not link with the option!
               AC_MSG_RESULT(no)
            fi
         else
            if test -s conftest2.out ; then
               cat conftest.out >> config.log
            fi
	    AC_MSG_RESULT(no)
            print_error "Will not add $1 to CFLAGS"
            CFLAGS="$CFLAGSSAV"
	    $3
         fi
      else
         cat conftest.out >> config.log
         AC_MSG_RESULT(no)
         $3
         CFLAGS="$CFLAGSSAV"         
      fi
   else
      AC_MSG_RESULT(no)
      $3
      if test -s conftest.out ; then cat conftest.out >> config.log ; fi    
      CFLAGS="$CFLAGSSAV"
   fi
else
    # Could not compile without the option!
    AC_MSG_RESULT(no)
fi
rm -f conftest*
])
dnl
dnl PAC_CHECK_FC_COMPILER_OPTION is like PAC_CHECK_COMPILER_OPTION,
dnl except for Fortran 
dnl It is harder to do a test here since Fortran compilers tend to be very 
dnl noisy.
dnl
dnl Side effect: If compiler option works, it is added to FFLAGS
dnl
dnl *** DO NOT CHANGE WITHOUT CHANGING sowing/confdb VERSION ***
define([PAC_CHECK_FC_COMPILER_OPTION],[
AC_MSG_CHECKING([whether Fortran compiler accepts option $1])
FFLAGSSAV="$FFLAGS"
FFLAGS="$1 $FFLAGS"
cat >conftest.f <<EOF
        program main
        end
EOF
cat >conftest3.f <<EOF
       subroutine try( )
       return
       end
EOF
rm -f conftest1.out conftest2.out
rm -f conftest3.out
if $F77 $FFLAGS -o conftest conftest.f $LDFLAGS > conftest1.out 2>&1 ; then
    if $F77 $FFLAGSSAV -o conftest conftest.f $LDFLAGS > conftest2.out 2>&1 ; then
        if diff conftest2.out conftest1.out > /dev/null 2>&1 ; then
            AC_MSG_RESULT(yes)
            AC_MSG_CHECKING([whether routines compiled with $1 can be linked with ones compiled without $1])
            rm -f conftest1.out 
            if $F77 -c $FFLAGSSAVE conftest3.f >conftest3.out 2>&1 ;then
                if $F77 $FFLAGS -o conftest conftest3.o conftest.f $LDFLAGS >conftest1.out 2>&1 ; then
                    if diff conftest2.out conftest1.out > /dev/null 2>&1 ; then
                        AC_MSG_RESULT(yes)  
			$2
	            else
                        AC_MSG_RESULT(no)
			FFLAGS="$FFLAGSSAV"
                        cat conftest1.out >> config.log
                        $3
	            fi
                else
                    AC_MSG_RESULT(no)
		    FFLAGS="$FFLAGSSAV"
                    cat conftest1.out >> config.log
                    $3
                fi
            else
                 AC_MSG_RESULT(no)
	         FFLAGS="$FFLAGSSAV"
                 cat conftest3.out >> config.log
                 $3
            fi
       else
           AC_MSG_RESULT(no)
	   FFLAGS="$FFLAGSSAV"
	   $3
       fi
    else
        AC_MSG_RESULT(no)
        FFLAGS="$FFLAGSSAV"
	$3
    fi
else
    AC_MSG_RESULT(no)
    FFLAGS="$FFLAGSSAV"
    $3
fi
rm -f conftest*

])
dnl
dnl
dnl Check that shared library support actually works
dnl
dnl Extra symbols used:
dnl    CC_SHARED_OPT
dnl    SHARED_LIB_UTIL (mpich/util/makesharedlib -kind=@SHAREDKIND@ -local)
dnl    SHARED_LIB_PATH (how to get linker to look in current directory
dnl                     for a shared library).  
dnl    SHARED_LIB_SEARCH_PATH (How to specify that the PROGRAM should look
dnl                     in a particular directory for a shared library.
dnl                     It should include `pwd`
dnl
dnl    Also checks to see if a program remembers where the shared library
dnl    was (!).  Some systems require LD_LIBRARY_PATH be set(!)
dnl    Set SHARED_LIB_NEEDS_PATH to yes if LD_LIBRARY_PATH needed
dnl PAC_SHARED_LIBS_OK([action-if-ok],[action-if-failed])
dnl
define(PAC_SHARED_LIBS_OK,[
    if test -z "$SHARED_LIB_UTIL" ; then
	echo "Error in configure; SHARED_LIB_UTIL not set"
	exit 1
    fi
    AC_MSG_CHECKING([whether shared libraries can be built])
    rm -f conftest* libconftest*
    cat >conftest.c <<EOF
int foo(int a);
int foo(int a) {return a+1;}
EOF
    cat >conftest1.c <<EOF
int main(void);
int foo(int);
int main(void){ return foo(-1); }
EOF
    if ${CC-cc} $CFLAGS $CC_SHARED_OPT -c conftest.c >conftest.out 2>&1 ; then
	if $SHARED_LIB_UTIL -obj=conftest.o -lib=libconftest.a >conftest.out 2>&1 ; then
	    :
	else
	    echo "Failed to build shared library" >> config.log
	    cat conftest.out >>config.log
	    # The next statement will fail because there is no library,
	    # which is ok because it does so relatively silently.
	    rm -f conftest.out
	    echo "Steps taken to build the shared library:" >>config.log
	    $SHARED_LIB_UTIL -echo -obj=conftest.o -lib=libconftest.a >>config.log 2>&1 
	fi
	if ${CC-cc} $CFLAGS -o conftest conftest1.c $SHARED_LIB_PATH \
		$SHARED_LIB_SEARCH_PATH -lconftest >conftest.out 2>&1 ; then
	   ifelse([$1],,,[$1])
	   AC_MSG_RESULT(yes)
	   AC_MSG_CHECKING(whether programs remember where the shared lib is)
           mkdir .tmp
           cp conftest .tmp
	   cd .tmp 
	      if ./conftest >conftest.out 2>&1 ; then
		  AC_MSG_RESULT(yes)
	      else
		  echo "Could not run program built with shared libs" >>../config.log
		  if test -s conftest.out ; then 
			echo "Error message was" >> ../config.log
			cat conftest.out >>../config.log
		  fi
		  # Try with LD_LIBRARY_PATH
		  saveLD="$LD_LIBRARY_PATH"
		  if test -z "$LD_LIBRARY_PATH" ; then LD_LIBRARY_PATH="." ; fi
	          LD_LIBRARY_PATH="${LD_LIBRARY_PATH}:`pwd`/.."
		  export LD_LIBRARY_PATH
		  rm -f conftest.out
		  if ./conftest >conftest.out 2>&1 ; then
		      AC_MSG_RESULT(no: needs path in LD_LIBRARY_PATH!)
		      SHARED_LIB_NEEDS_PATH="yes"
		  else
		      AC_MSG_RESULT(no: LD_LIBRARY_PATH does not work!)
		      if test -s conftest.out ; then
			echo "Error messages when running program build with shared libs" >> ../config.log
			cat conftest.out >>../config.log
		      fi
		  fi
		  LD_LIBRARY_PATH="$saveLD"
	      fi
           cd ..
	   rm -rf .tmp
	else
           ifelse([$2],,,[$2])
	   if test -s conftest.out ; then cat conftest.out >> config.log ; fi
	   AC_MSG_RESULT(no)
	fi
    else
        ifelse([$2],,,[$2])
	if test -s conftest.out ; then cat conftest.out >> config.log ; fi
	AC_MSG_RESULT(no)
    fi
rm -f conftest* libconftest*
])
dnl
dnl Test that the C compiler allows #define a(b) a(b,__LINE__,__FILE__)
dnl PAC_MACRO_NAME_IN_MACRO([action if ok],[action if failed])
dnl
dnl Note that we can't put a pound sign into the msg_checking macro because
dnl it confuses autoconf
define(PAC_MACRO_NAME_IN_MACRO,
[AC_REQUIRE([AC_PROG_CC])dnl
AC_MSG_CHECKING([whether compiler allows define a(b) a(b,__LINE__)])
AC_COMPILE_CHECK(,
[
void a(i,j)int i,j;{}
#define a(b) a(b,__LINE__)],
a(0);return 0;,ac_cv_cpprworks="yes",ac_cv_cpprworks="no")
if test $ac_cv_cpprworks = "yes" ; then
    AC_MSG_RESULT(yes)
    ifelse([$1],,,[$1])
else
    AC_MSG_RESULT(no)
    ifelse([$2],,,[$2])
fi
])
dnl
dnl Check that anonymous mmap works
dnl
dnl PAC_HAVE_ANON_MMAP([action-if-success],[action-if-failure])
define([PAC_HAVE_ANON_MMAP],[
AC_HAVE_FUNCS(mmap)
# Check that MMAP works!
AC_MSG_CHECKING([whether shared, anonymous mmap works with -1 filedes])
PAC_TEST_PROGRAM([
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
int main(){
int memsize;
caddr_t shmptr;
memsize = getpagesize();
shmptr = mmap((caddr_t) 0, memsize, 
PROT_READ|PROT_WRITE|PROT_EXEC, 
MAP_SHARED
#ifdef MAP_ANON
|MAP_ANON
#endif
,-1, (off_t) 0);
if (shmptr == (caddr_t) -1) {
 return 1;}
return 0;
}
],ac_cv_mmap=1,ac_cv_mmap=0)
if test $ac_cv_mmap = 0 ; then 
    AC_MSG_RESULT(no!)
    ifelse($1,,,$1)
else
    AC_MSG_RESULT(yes)
    ifelse($2,,,$2)
fi
])
dnl
dnl Check that semget works correctly (sometimes no enabled, sometimes only
dnl root can use it).  
dnl
dnl PAC_SEMGET_WORKS([action-if-success],[action-if-failure],[errmsg])
dnl errmsg prints a more detailed error message.  Default is echo; use true
dnl to suppress output.
define([PAC_SEMGET_WORKS],[
# We need to check that semctl is ok.
# Both of these need to go into aclocal.m4
AC_MSG_CHECKING([whether semget works])
###
### Still need to check for SEMUN_UNDEFINED - see mpid/ch_p4/p4/configure.in
### 
cat > conftest.c <<EOF
#include <sys/types.h>
#include <sys/ipc.h>
#include <errno.h>
#include <sys/sem.h>
int main () {
#ifdef NEEDS_UNION_SEMUN
#if defined(SEMUN_UNDEFINED)    
union semun { int val } arg;
#else
union semun arg;
arg.val = 0;
#endif
#else
int arg = 0;
#endif
key_t key;
int semset_id;
key=ftok(".", 'a');
errno=0;
if ((semset_id=semget(key,10,IPC_CREAT|IPC_EXCL|0666)) == -1) 
printf("%d\n", errno);
else {
printf("%d\n", errno);
semctl(semset_id,0,IPC_RMID,arg); }
return 0; 
}
EOF
echo "${CC-cc} $CFLAGS -o conftest conftest.c $LDFLAGS $LIBS" >> config.log
if ${CC-cc} $CFLAGS -o conftest conftest.c $LDFLAGS $LIBS >> config.log 2>&1 ; then
    if test -x conftest ; then
	rm -f conftest.out
	./conftest > conftest.out
	errno=`cat conftest.out`

	# these values are specific to a particular unix.
        # we need to convert number to ERRNO based on the local 
        # system, and then match, if we can
	if test $errno -eq 0 ; then
	    AC_MSG_RESULT(yes)
	    ifelse($1,,,$1)
	elif test $errno -eq 13 ; then  
	    AC_MSG_RESULT(no)
	    ifelse($2,,,$2)
            ifelse($3,,echo,$3) "No access permission rights to the semaphore set"
	    ifelse($3,,echo,$3) "created with this key!"
            ifelse($3,,echo,$3) "Configure could have chosen to create a"
            ifelse($3,,echo,$3) "semaphore set using an unacceptable key value"
        elif test $errno -eq 17 ; then
	    AC_MSG_RESULT(no)
	    ifelse($2,,,$2)
	    ifelse($3,,echo,$3) "The semaphore set created with this key"
	    ifelse($3,,echo,$3) "already exists!"
	    ifelse($3,,echo,$3) "Try running util/cleanipcs and then reconfiguring"
	    ifelse($3,,echo,$3) "This may or may not help."
	elif test $errno -eq 43 ; then
	    AC_MSG_RESULT(no)
	    ifelse($2,,,$2)
	    ifelse($3,,echo,$3) "The semaphore set created with this key"
	    ifelse($3,,echo,$3) "is marked to be deleted!"
	    ifelse($3,,echo,$3) "Try running util/cleanipcs and then"
	    ifelse($3,,echo,$3) "reconfiguring.  This may or may not help."
	elif test $errno -eq 2 ; then
	    AC_MSG_RESULT(no)
	    ifelse($2,,,$2)
	    ifelse($3,,echo,$3) "No semaphore set exists for this key!"
	    ifelse($3,,echo,$3) "Configure could have chosen to create a" 
	    ifelse($3,,echo,$3) "semaphore set using an unacceptable key value."
	elif test $errno -eq 12 ; then
	    AC_MSG_RESULT(undetermined)
	    ifelse($2,,,$2)
	    ifelse($3,,echo,$3) "Not enough memory to create a semaphore set!"
	    ifelse($3,,echo,$3) "Try running util/cleanipcs and then" 
	    ifelse($3,,echo,$3) "reconfiguring.  This may or may not help."
	elif test $errno -eq 28 ; then
	    AC_MSG_RESULT(no)
	    ifelse($2,,,$2)
	    ifelse($3,,echo,$3) "The system limit for the maximum number of"
	    ifelse($3,,echo,$3) "semaphore sets (SEMMNI), or the system wide"
            ifelse($3,,echo,$3) "maximum number of semaphores (SEMMNS)"
	    ifelse($3,,echo,$3) "has been reached."
        fi
    else
	AC_MSG_RESULT(undetermined)
        ifelse($2,,,$2)
	ifelse($3,,echo,$3) "Could not build executable"
    fi
else
    AC_MSG_RESULT(undetermined)
    ifelse($2,,,$2)
    ifelse($3,,echo,$3) "Could not compile program"
fi
rm -f conftest*
])
dnl
dnl
dnl The following trys a program and sets a flag to indicate whether
dnl the compilation is clean (no extra messages), successful (messages but
dnl status is zero), or failed.  This handles the case where the compiler
dnl complains about a type mismatch, but allows it anyway, and it can be
dnl used to find the appropriate type.
dnl
dnl PAC_TRY_COMPILE_CLEAN(includes,function,flagvar)
dnl flagvar is set to 0 (clean), 1 (dirty but status ok), 2 (failed)
dnl Note that an entire function is needed as the second argument,
dnl not just a function body.  This allows us to check for more 
dnl complex situations (such as handling ... in an arg list)
dnl
define([PAC_TRY_COMPILE_CLEAN],[
$3=2
rm -f conftest*
dnl 
dnl Get the compiler output to test against
if test -z "$TRY_COMPLILE_CLEAN" ; then
    echo 'int try(void);int try(void){return 0;}' > conftest.c
    if ${CC-cc} $CFLAGS -c conftest.c >conftest.bas 2>&1 ; then
	if test -s conftest.bas ; then 
	    TRY_COMPILE_CLEAN_OUT=`cat conftest.bas`
        fi
        TRY_COMPILE_CLEAN=1
    else
	AC_MSG_WARN([Could not compile simple test program!])
	if test -s conftest.bas ; then 	cat conftest.bas >> config.log ; fi
    fi
fi
dnl
dnl Create the program that we need to test with
rm -f conftest*
cat >conftest.c <<EOF
#include "confdefs.h"
[$1]
[$2]
EOF
dnl
dnl Compile it and test
if ${CC-cc} $CFLAGS -c conftest.c >conftest.bas 2>&1 ; then
    dnl Success.  Is the output the same?
    if test "$TRY_COMPILE_CLEAN_OUT" = "`cat conftest.bas`" ; then
	$3=0
    else
        cat conftest.c >>config.log
	if test -s conftest.bas ; then 	cat conftest.bas >> config.log ; fi
        $3=1
    fi
else
    dnl Failure.  Set flag to 2
    cat conftest.c >>config.log
    if test -s conftest.bas ; then cat conftest.bas >> config.log ; fi
    $3=2
fi
rm -f conftest*
])
dnl
dnl
dnl PAC_STDARG(action if works, action if oldstyle, action if fails)
dnl
dnl defines have_stdarg_h if the header exists
dnl defines 
define([PAC_STDARG],[
dnl First, check for stdarg header
PAC_CHECK_HEADER(stdarg.h,[AC_DEFINE(HAVE_STDARG_H)
havestdarg=1])
#
# It isn't enough to check for stdarg.  Even gcc doesn't get it right;
# on some systems, the gcc version of stdio.h loads stdarg.h WITH THE WRONG
# OPTIONS (causing it to choose the OLD STYLE va_start etc).
#
# The original test tried the two-arg version first; the old-style
# va_start took only a single arg.
# This turns out to be VERY tricky, because some compilers (e.g., Solaris) 
# are quite happy to accept the *wrong* number of arguments to a macro!
# Instead, we try to find a clean compile version, using our special
# TRY_COMPILE_CLEAN command
#
# This approach fails for compilers that generate noise.  What we need to 
# do is to run a program that makes use of stdarg and see if it works 
# correctly.  We can fall back on this approach when cross-compiling
#
USE_STDARG=0
AC_SUBST(USE_STDARG)
#
AC_MSG_CHECKING([whether stdarg is oldstyle])
PAC_TRY_COMPILE_CLEAN([#include <stdio.h>
#include <stdarg.h>],
[int func( int a, ... ){
int b;
va_list ap;
va_start( ap );
b = va_arg(ap, int);
printf( "%d-%d\n", a, b );
va_end(ap);
fflush(stdout);
return 0;
}
int main() { func( 1, 2 ); return 0;}],check_compile)
pac_old_warning=no
case $check_compile in 
    0)  AC_MSG_RESULT(yes)
        AC_DEFINE(USE_STDARG)
	AC_DEFINE(USE_OLDSTYLE_STDARG)
	ifelse([$2],,,[$2])
	USE_STDARG=1
	;;
    1)  AC_MSG_RESULT([hmm, warnings from compiler.  Trying newstyle])
	pac_old_warning=yes
	;;
    2)  AC_MSG_RESULT(no)
	;;
esac

if test -n "$havestdarg" -a "$USE_STDARG" != 1 ; then
    AC_MSG_CHECKING([whether stdarg is correct])
    PAC_TRY_COMPILE_CLEAN([
#include <stdio.h>
#include <stdarg.h>],[
int func( int a, ... ){
int b;
va_list ap;
va_start( ap, a );
b = va_arg(ap, int);
printf( "%d-%d\n", a, b );
va_end(ap);
fflush(stdout);
return 0;
}
int main() { func( 1, 2 ); return 0;}],check_compile)
dnl Generate the result message
case $check_compile in 
    0)  AC_MSG_RESULT(yes)
	;;
    1)  AC_MSG_RESULT([yes with warnings])
	;;
    2)  AC_MSG_RESULT(no)
	;;
esac
dnl Compiling is not enough.  Linking may also be a problem.
dnl For example, the gcc stdarg may be used with a non-gcc compiler
dnl In this case, compilation will succeed but linking will fail
dnl because gcc builtins such as __builtin_next_arg are not found
AC_MSG_CHECKING([whether programs using stdarg link])
AC_TRY_LINK([#include <stdarg.h>
int foo( int a, ... )
{
    va_list Argp;
    int     b;
    va_start( Argp, a );
    b = va_arg(Argp,int);
    va_end(Argp);
    return 2*a-b;
}
],[return foo(1,2)],check_link=yes,check_link=no)
AC_MSG_RESULT($check_link)
if test $check_link = no ; then
    check_compile=2
fi
case $check_compile in 
    0)  AC_DEFINE(USE_STDARG)
        USE_STDARG=1
	ifelse([$1],,,[$1])
	;;
    1)  AC_DEFINE(USE_STDARG)
        USE_STDARG=1
	if test $pac_old_warning = "yes" ; then
	    # A Risky move: if both gave warnings, choose old-style
    	    ifelse([$2],,:,[$2])
	else
	    ifelse([$1],,:,[$1])
        fi
	;;
    2)  ifelse([$3],,,[$3])
	;;
esac
fi])
dnl
dnl Check that the Fortran and C++ compilers can link the
dnl specified functions
dnl
dnl PAC_FUNC_INTER_LANG( functions, [ fortworks ], [ c++works] )
dnl the second and third args are set to yes/no depending
dnl
dnl F77_LIBS and CXX_LIBS can be defined
dnl
define([PAC_FUNC_INTER_LANG],[
ifelse([$2],,,[$2=no])
ifelse([$3],,,[$3=no])
rm -f conftest*
cat >> conftest1.c <<EOF
#include "confdefs.h"
void f()
{
EOF
for func in $1 ; do
    echo "$func();" >>conftest1.c
done
echo '}' >>conftest1.c
if ${CC-cc} -c $CFLAGS conftest1.c >>config.log 2>&1 ; then
    if test -n "$F77" ; then
	AC_MSG_CHECKING([whether Fortran programs can link with needed C functions])
        cat > conftest.f <<EOF
        program main
        end
EOF
        if test -z "$FLINKER" ; then FLINKER="$F77 $LDFLAGS" ; fi
	if $FLINKER -o conftest conftest.f conftest1.o $LIBS $F77_LIBS >>config.log 2>&1 ; then
	    AC_MSG_RESULT(yes)
	    ifelse([$2],,,[$2=yes])
	else
	    AC_MSG_RESULT(no)
	    ifelse([$2],,,[$2=no])
	    echo "$FLINKER -o conftest conftest.f conftest1.o $LIBS $F77_LIBS" >>config.log
	    echo "Error linking" >>config.log
	    cat conftest.f >>config.log
	fi
	rm -f conftest.f conftest conftest.o
    fi
    if test -n "$CCC" -a -z "$CXX" ; then CXX="$CCC" ; fi
    if test -z "$CXX" -a -n "$CPP_COMPILER" ; then CXX="$CPP_COMPILER" ; fi
    if test -n "$CXX" ; then
	AC_MSG_CHECKING([whether C++ programs can link with needed C functions])
	cat > conftest.cc <<EOF
	int main() { return 0; }
EOF
	if $CXX -o conftest conftest.cc conftest1.o $LDFLAGS $LIBS $CXX_LIBS >>config.log 2>&1 ; then
	    AC_MSG_RESULT(yes)
	    ifelse([$3],,,[$3=yes])
	else
	    AC_MSG_RESULT(no)
	    ifelse([$3],,,[$3=no])
	    echo "$CXX -o conftest conftest.cc conftest1.o $LDFLAGS $LIBS $CXX_LIBS" >>config.log 
	    echo "Error linking" >>config.log
	    cat conftest.cc >>config.log
	fi
    fi
else
    echo "Could not compile " >> config.log
    cat conftest.c >>config.log
fi
rm -f conftest*
])
dnl
dnl Include other definitions
builtin(include,aclocal_tcl.m4)
