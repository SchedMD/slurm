dnl
dnl   (C) 2001 by Argonne National Laboratory
dnl       See COPYRIGHT in top-level directory.
dnl
dnl       @author  Anthony Chan
dnl
dnl-------------------------------------------------------------------------
dnl JAC_TRY_COMPILE - test the compilation of java program
dnl
dnl JAC_TRY_COMPILE( JC, JFLAGS, IMPORTS, PROGRAM-BODY
dnl                  [, ACTION-IF-WORKING [ , ACTION-IF-NOT-WORKING ] ] )
dnl JC            - java compiler
dnl JFLAGS        - java compiler flags, like options: -d and -classpath, ...
dnl IMPORTS       - java import statements, besides top level "class" statement
dnl PROGRAM_BODY  - java program body
dnl
AC_DEFUN(JAC_TRY_COMPILE,[
dnl - set internal JC and JFLAGS variables
jac_JC="$1"
jac_JFLAGS="$2"
dnl - set the testing java program
changequote(,)
    rm -f conftest*
    cat > conftest.java <<EOF
$3
class conftest {
$4
}
EOF
changequote([,])
dnl
    jac_compile='${jac_JC} ${jac_JFLAGS} conftest.java 1>&AC_FD_CC'
    if AC_TRY_EVAL(jac_compile) && test -s conftest.class ; then
        ifelse([$5],,:,[rm -rf conftest* ; $5])
    else
        ifelse([$6],,:,[rm -rf conftest* ; $6])
    fi
])dnl
dnl
dnl JAC_TRY_RMIC - test the rmic program
dnl
dnl JAC_TRY_RMIC( RMIC, JRFLAGS, JC, JFLAGS
dnl               [, ACTION-IF-WORKING [ , ACTION-IF-NOT-WORKING ] ] )
dnl RMIC          - rmic compiler
dnl JRFLAGS       - rmic compiler flags, like options: -d and -classpath, ...
dnl JC            - java compiler
dnl JFLAGS        - java compiler flags, like options: -d and -classpath, ...
dnl
AC_DEFUN(JAC_TRY_RMIC,[
dnl - set internal RMIC and JRFLAGS variables
jac_RMIC="$1"
jac_JRFLAGS="$2"
dnl - set internal JC and JFLAGS variables
jac_JC="$3"
jac_JFLAGS="$4"
dnl - set the testing java program
changequote(,)
    rm -f conftest*
dnl
    cat > conftest_remote.java <<EOF
import java.rmi.*;
public interface conftest_remote extends Remote
{
    public void remote_interface() throws RemoteException;
}
EOF
dnl
    cat > conftest_rmic.java <<EOF
import java.rmi.*;
import java.rmi.server.*;
public class conftest_rmic extends UnicastRemoteObject
                           implements conftest_remote
{
    public conftest_rmic() throws RemoteException
    { super(); }
    public void remote_interface() throws RemoteException
    {}
}
EOF
changequote([,])
dnl
    jac_compile='${jac_JC} ${jac_JFLAGS} conftest_remote.java conftest_rmic.java 1>&AC_FD_CC'
    if AC_TRY_EVAL(jac_compile) && test -s conftest_rmic.class ; then
        jac_rmic='${jac_RMIC} ${jac_JRFLAGS} conftest_rmic 1>&AC_FD_CC'
        if AC_TRY_EVAL(jac_rmic) && test -s conftest_rmic_Stub.class ; then
            ifelse([$5],,:,[rm -rf conftest* ; $5])
        else
            ifelse([$6],,:,[rm -rf conftest* ; $6])
        fi
    else
        ifelse([$6],,:,[rm -rf conftest* ; $6])
    fi
])dnl
dnl
dnl JAC_FIND_PROG_IN_KNOWNS - locate Java program in standard known locations
dnl
dnl JAC_FIND_PROG_IN_KNOWNS( PROG_VAR, PROG-TO-CHECK-FOR
dnl                          [, TEST-ACTION-IF-FOUND] )
dnl
dnl PROG_VAR              - returned variable name of PROG-TO-CHECK-FOR
dnl PROG-TO-CHECK-FOR     - java program to check for, e.g. javac or java
dnl TEST-ACTION-IF-FOUND  - testing program for PROG-TO-CHECK-FOR.
dnl                         if TRUE,  it must return jac_prog_working=yes
dnl                         if FALSE, it must return jac_prog_working=no
dnl
AC_DEFUN([JAC_FIND_PROG_IN_KNOWNS],[
$1=""
# Determine the system type
AC_REQUIRE([AC_CANONICAL_HOST])
subdir=""
case "$host" in
    mips-sgi-irix*)
        if test -d "/software/irix" ; then
            subdir="irix"
        elif test -d "/software/irix-6" ; then
            subdir="irix-6"
        fi
        ;;
    *linux*)
        if test -d "/software/linux" ; then
            subdir="linux"
        fi
        ;;
    *solaris*)
        if test -d "/software/solaris" ; then
            subdir="solaris"
        elif test -d "/software/solaris-2" ; then
            subdir="solaris-2"
        fi
        ;;
    *sun4*)
        if test -d "/software/sun4" ; then
            subdir="sun4"
        fi
        ;;
    *aix*)
        if test -d "/software/aix-4" ; then
            subdir="aix-4"
        fi
        ;;
    *rs6000*)
        if test -d "/software/aix-4" ; then
            subdir="aix-4"
        fi
        ;;
    *freebsd*)
        if test -d "/software/freebsd" ; then
            subdir="freebsd"
   	    fi
esac
#
if test -z "$subdir" ; then
    if test -d "/software/common" ; then
       subdir="common"
    fi
fi
#
AC_MSG_CHECKING([for $2 in known locations])
reverse_dirs=""
for dir in \
    /usr \
    /usr/jdk* \
    /usr/j2sdk* \
    /usr/java* \
    /usr/java/jdk* \
    /usr/java/j2sdk* \
    /usr/local \
    /usr/local/java* \
    /usr/local/jdk* \
    /usr/local/j2sdk* \
    /usr/share \
    /usr/share/java* \
    /usr/share/jdk* \
    /usr/share/j2sdk* \
    /usr/contrib \
    /usr/contrib/java* \
    /usr/contrib/jdk* \
    /usr/contrib/j2sdk* \
    $HOME/java* \
    $HOME/jdk* \
    $HOME/j2sdk* \
    /opt/jdk* \
    /opt/j2sdk* \
    /opt/java* \
    /opt/local \
    /opt/local/jdk* \
    /opt/local/j2sdk* \
    /opt/local/java* \
    /Tools/jdk* \
    /Tools/j2sdk* \
    /software/$subdir/apps/packages/java* \
    /software/$subdir/apps/packages/jdk* \
    /software/$subdir/apps/packages/j2sdk* \
    /software/$subdir/com/packages/java* \
    /software/$subdir/com/packages/jdk* \
    /software/$subdir/com/packages/j2sdk* \
    /soft/apps/packages/java* \
    /soft/apps/packages/jdk* \
    /soft/apps/packages/j2sdk* \
    /soft/com/packages/java* \
    /soft/com/packages/jdk* \
    /soft/com/packages/j2sdk* \
    /local/encap/java* \
    /local/encap/j2sdk* \
    /local/encap/jdk* ; do
    if test -d $dir ; then
        reverse_dirs="$dir $reverse_dirs"
    fi
done
dnl
for dir in $reverse_dirs ; do
    if test -d $dir ; then
        case "$dir" in
            *java-workshop* )
                if test -d "$dir/JDK/bin" ; then
                    if test -x "$dir/JDK/bin/$2" ; then
                        $1="$dir/JDK/bin/$2"
                    fi
                fi
                ;;
            *java* | *jdk* | *j2sdk* )
                if test -x "$dir/bin/$2" ; then
                    $1="$dir/bin/$2"
                fi
                ;;
        esac
dnl
        # Not all releases work.  Try a simple program
        if test -n "[$]$1" ; then
            AC_MSG_RESULT([found [$]$1])
            ifelse([$3],, break, [
                AC_MSG_CHECKING([if [$]$1 works])
                $3
                if test "$jac_prog_working" = "yes" ; then
                    AC_MSG_RESULT(yes)
                    break
                else
                    AC_MSG_RESULT(no)
                    AC_MSG_CHECKING([for working $2 in known locations])
                    $1=""
                fi
            ])
        fi
dnl
    fi
done
if test -z "[$]$1" ; then
    AC_MSG_RESULT(not found)
fi
])dnl
dnl
dnl JAC_FIND_PROG_IN_PATH - locate Java program in user's $PATH
dnl
dnl JAC_FIND_PROG_IN_PATH( PROG_VAR, PROG-TO-CHECK-FOR
dnl                        [, CHECKING-ACTION-IF-FOUND] )
dnl
dnl PROG_VAR              - returned variable name of PROG-TO-CHECK-FOR
dnl PROG-TO-CHECK-FOR     - java program to check for, e.g. javac or java
dnl TEST-ACTION-IF-FOUND  - testing program for PROG-TO-CHECK-FOR.
dnl                         if TRUE,  it must return jac_prog_working=yes
dnl                         if FALSE, it must return jac_prog_working=no
dnl
AC_DEFUN([JAC_FIND_PROG_IN_PATH], [
AC_MSG_CHECKING([for $2 in user's PATH])
if test -n "$PATH" ; then
    $1=""
    dnl It is safer to create jac_PATH than modify IFS, because the potential
    dnl 3rd argument, TEST-ACTION, may contain code in modifying IFS
    jac_PATH=`echo $PATH | sed 's/:/ /g'`
    for dir in ${jac_PATH} ; do
        if test -d $dir -a -x "$dir/$2" ; then
            $1="$dir/$2"
            # Not all releases work.  Try a simple program
            if test -n "[$]$1" ; then
                AC_MSG_RESULT([found [$]$1])
                ifelse([$3],, break, [
                    AC_MSG_CHECKING([if [$]$1 works])
                    $3
                    if test "$jac_prog_working" = "yes" ; then
                        AC_MSG_RESULT(yes)
                        break
                    else
                        AC_MSG_RESULT(no)
                        AC_MSG_CHECKING([for working $2 in user's PATH])
                        $1=""
                    fi
                ])
            fi
        fi
    done
fi
if test -z "[$]$1" ; then
    AC_MSG_RESULT(not found)
fi
])dnl
dnl
dnl JAC_PATH_PROG - locate Java program in user's $PATH then known locations
dnl
dnl JAC_PATH_PROG( PROG_VAR, PROG-TO-CHECK-FOR [, CHECKING-ACTION-IF-FOUND] )
dnl
dnl PROG_VAR              - returned variable name of PROG-TO-CHECK-FOR
dnl PROG-TO-CHECK-FOR     - java program to check for, e.g. javac or java
dnl TEST-ACTION-IF-FOUND  - testing program for PROG-TO-CHECK-FOR.
dnl                         if TRUE,  it must return jac_prog_working=yes
dnl                         if FALSE, it must return jac_prog_working=no
dnl
AC_DEFUN([JAC_PATH_PROG], [
ifelse([$3],,
    [JAC_FIND_PROG_IN_PATH($1, $2)],
    [JAC_FIND_PROG_IN_PATH($1, $2, [$3])]
)
if test "x[$]$1" = "x" ; then
    ifelse([$3],,
        [JAC_FIND_PROG_IN_KNOWNS($1, $2)],
        [JAC_FIND_PROG_IN_KNOWNS($1, $2, [$3])]
    )
fi
])
dnl
dnl JAC_JNI_HEADERS - locate Java Native Interface header files
dnl
dnl JAC_JNI_HEADER( JNI_INC [, JDK_TOPDIR] )
dnl
dnl JNI_INC    - returned JNI include flag
dnl JDK_TOPDIR - optional Java SDK directory.  If supplied, it will be updated
dnl              to reflect the JDK_TOPDIR used in JNI_INC
dnl
AC_DEFUN([JAC_JNI_HEADERS], [
AC_REQUIRE([AC_CANONICAL_SYSTEM])dnl
AC_REQUIRE([AC_PROG_CPP])dnl
is_jni_working=no
ifelse([$2],, :, [
    if test "x[$]$2" != "x" ; then
        jac_JDK_TOPDIR="[$]$2"
        AC_MSG_CHECKING([if $jac_JDK_TOPDIR exists])
        if test -d "$jac_JDK_TOPDIR" ; then
            AC_MSG_RESULT(yes)
            jac_jni_working=yes
        else
            AC_MSG_RESULT(no)
            jac_jni_working=no
        fi
 
        if test "$jac_jni_working" = "yes" ; then
            AC_MSG_CHECKING([for <jni.h> include flag])
            jac_JDK_INCDIR="$jac_JDK_TOPDIR/include"
            if test -d "$jac_JDK_INCDIR" -a -f "$jac_JDK_INCDIR/jni.h" ; then
                jac_JNI_INC="-I$jac_JDK_INCDIR"
                if test "$build_os" = "cygwin" ; then
                    jac_JAVA_ARCH=win32
                else
                    changequote(,)dnl
                    jac_JAVA_ARCH="`echo $build_os | sed -e 's%[-0-9].*%%'`"
                    changequote([,])dnl
                fi
                if test -d "$jac_JDK_INCDIR/$jac_JAVA_ARCH" ; then
                    jac_JNI_INC="$jac_JNI_INC -I$jac_JDK_INCDIR/$jac_JAVA_ARCH"
dnl             these 2 lines handle blackdown's JDK 117_v3
dnl             elif test -d "$jac_JDK_INCDIR/genunix" ; then
dnl                 jac_JNI_INC="$jac_JNI_INC -I$jac_JDK_INCDIR/genunix"
                fi
                AC_MSG_RESULT([found $jac_JNI_INC])
                jac_jni_working=yes
            else
                AC_MSG_RESULT([not found])
                jac_jni_working=no
            fi
        fi

        if test "$jac_jni_working" = "yes" ; then
            AC_MSG_CHECKING([for <jni.h> usability])
            jac_save_CPPFLAGS="$CPPFLAGS"
            CPPFLAGS="$jac_save_CPPFLAGS $jac_JNI_INC"
dnl Explicitly test for JNIEnv and jobject.
dnl <stdio.h> and <stdlib.h> are here to make sure include path like
dnl -I/usr/include/linux dnl won't be accepted.
            AC_TRY_COMPILE([
#include <jni.h>
#if defined( STDC_HEADERS ) || defined( HAVE_STDIO_H )
#include <stdio.h>
#endif
#if defined( STDC_HEADERS ) || defined( HAVE_STDLIB_H )
#include <stdlib.h>
#endif
                           ], [
    JNIEnv  *env;
    jobject  obj;
                           ], [jac_jni_working=yes], [jac_jni_working=no])
            CPPFLAGS="$jac_save_CPPFLAGS"
            if test "$jac_jni_working" = "yes" ; then
                $1="$jac_JNI_INC"
                ifelse($2,, :, [$2="$jac_JDK_TOPDIR"])
                AC_MSG_RESULT(yes)
            else
                $1=""
                AC_MSG_RESULT(no)
            fi
        fi

        if test "$jac_jni_working" = "yes" ; then
            is_jni_working=yes
        else
            is_jni_working=no
        fi
    fi
])

if test "$is_jni_working" = "no" ; then
    JAC_PATH_PROG(jac_JH, javah, [
        AC_MSG_RESULT([unknown!])
        changequote(,)dnl
        jac_JDK_TOPDIR="`echo $jac_JH | sed -e 's%\(.*\)/[^/]*/[^/]*$%\1%'`"
        changequote([,])dnl
        AC_MSG_CHECKING([if $jac_JDK_TOPDIR exists])
        if test "X$jac_JDK_TOPDIR" != "X" -a -d "$jac_JDK_TOPDIR" ; then
            jac_jni_working=yes
        else
            jac_jni_working=no
        fi
        if test "$jac_jni_working" = "yes" ; then
            AC_MSG_RESULT(yes)
            AC_MSG_CHECKING([for <jni.h> include flag])
            jac_JDK_INCDIR="$jac_JDK_TOPDIR/include"
            if test -d "$jac_JDK_INCDIR" -a -f "$jac_JDK_INCDIR/jni.h" ; then
                jac_JNI_INC="-I$jac_JDK_INCDIR"
                if test "$build_os" = "cygwin" ; then
                    jac_JAVA_ARCH=win32
                else
                    changequote(,)dnl
                    jac_JAVA_ARCH="`echo $build_os | sed -e 's%[-0-9].*%%'`"
                    changequote([,])dnl
                fi
                if test -d "$jac_JDK_INCDIR/$jac_JAVA_ARCH" ; then
                    jac_JNI_INC="$jac_JNI_INC -I$jac_JDK_INCDIR/$jac_JAVA_ARCH"
dnl             these 2 lines handle blackdown's JDK 117_v3
dnl             elif test -d "$jac_JDK_INCDIR/genunix" ; then
dnl                 jac_JNI_INC="$jac_JNI_INC -I$jac_JDK_INCDIR/genunix"
                fi
                jac_jni_working=yes
            else
                jac_jni_working=no
            fi
        fi
        if test "$jac_jni_working" = "yes" ; then
            AC_MSG_RESULT([found $jac_JNI_INC])
            AC_MSG_CHECKING([for <jni.h> usability])
            jac_save_CPPFLAGS="$CPPFLAGS"
            CPPFLAGS="$jac_save_CPPFLAGS $jac_JNI_INC"
dnl Explicitly test for JNIEnv and jobject.
dnl <stdio.h> and <stdlib.h> are here to make sure include path like
dnl -I/usr/include/linux dnl won't be accepted.
            AC_TRY_COMPILE([
#include <jni.h>
#if defined( STDC_HEADERS ) || defined( HAVE_STDIO_H )
#include <stdio.h>
#endif
#if defined( STDC_HEADERS ) || defined( HAVE_STDLIB_H )
#include <stdlib.h>
#endif
                           ], [
    JNIEnv  *env;
    jobject  obj;
                           ], [jac_jni_working=yes], [jac_jni_working=no])
            CPPFLAGS="$jac_save_CPPFLAGS"
        fi
        if test "$jac_jni_working" = "yes" ; then
            $1="$jac_JNI_INC"
            ifelse($2,, :, [$2="$jac_JDK_TOPDIR"])
            jac_prog_working=yes
        else
            $1=""
            jac_prog_working=no
        fi
    ])
fi
])dnl
dnl
dnl JAC_TRY_RUN - test the execution of a java class file
dnl
dnl JAC_TRY_RUN( JVM, JVMFLAGS, CLASS-FILE
dnl              [, ACTION-IF-WORKING [ , ACTION-IF-NOT-WORKING ] ] )
dnl JVM           - java virtual machine
dnl JVMFLAGS      - jVM flags, like options: -d and -classpath, ...
dnl CLASS-FILE    - java byte code, .class file, is assumed located at $srcdir
dnl                 i.e. relative path name from $srcdir
dnl
AC_DEFUN(JAC_TRY_RUN, [
dnl - set internal JVM and JVMFLAGS variables
jac_CPRP=cp
jac_JVM="$1"
jac_JVMFLAGS="$2"
dnl - set the testing java program
changequote(,)dnl
jac_basename="`echo $3 | sed -e 's%.*/\([^/]*\)$%\1%'`"
changequote([,])dnl
jac_baseclass="`echo $jac_basename | sed -e 's%.class$%%'`"
if test ! -f "$jac_basename" ; then
    if test -f "$srcdir/$3" ; then
        $jac_CPRP $srcdir/$3 .
    else
        AC_MSG_ERROR([$srcdir/$3 does NOT exist!])
    fi
fi
dnl
    jac_command='${jac_JVM} ${jac_JVMFLAGS} ${jac_baseclass} 1>&AC_FD_CC'
    if AC_TRY_EVAL(jac_command) ; then
        ifelse([$4],, :,[$4])
    else
        ifelse([$5],, :,[$5])
    fi
])dnl
dnl
dnl JAC_TRY_RUNJAR - test the execution of a java jar file
dnl
dnl JAC_TRY_RUNJAR( JVM, JVMFLAGS, JAR-FILE
dnl                 [, ACTION-IF-WORKING [ , ACTION-IF-NOT-WORKING ] ] )
dnl JVM           - java virtual machine
dnl JVMFLAGS      - jVM flags, like options: -d and -classpath, ...
dnl JAR-FILE      - java executable jar file, is assumed located at $srcdir
dnl                 i.e. relative path name from $srcdir
dnl
AC_DEFUN(JAC_TRY_RUNJAR, [
dnl - set internal JVM and JVMFLAGS variables
jac_CPRP=cp
jac_JVM="$1"
jac_JVMFLAGS="$2"
dnl - set the testing java program
changequote(,)dnl
jac_basename="`echo $3 | sed -e 's%.*/\([^/]*\)$%\1%'`"
changequote([,])dnl
if test ! -f "$jac_basename" ; then
    if test -f "$srcdir/$3" ; then
        $jac_CPRP $srcdir/$3 .
    else
        AC_MSG_ERROR([$srcdir/$3 does NOT exist!])
    fi
fi
dnl
    jac_command='${jac_JVM} ${jac_JVMFLAGS} -jar ${jac_basename} 1>&AC_FD_CC'
    if AC_TRY_EVAL(jac_command) ; then
        ifelse([$4],, :,[$4])
    else
        ifelse([$5],, :,[$5])
    fi
])dnl
dnl
dnl JAC_CHECK_CLASSPATH - check and fix the classpath
dnl
AC_DEFUN(JAC_CHECK_CLASSPATH, [
AC_MSG_CHECKING([if CLASSPATH is set])
if test "x$CLASSPATH" != "x" ; then
    AC_MSG_RESULT([yes])
    AC_MSG_CHECKING([if CLASSPATH contains current path])
    IFS="${IFS=   }"; jac_saved_ifs="$IFS"; IFS=":"
    jac_hasCurrPath=no
    for path_elem in $CLASSPATH ; do
        if test "X$path_elem" = "X." ; then
            jac_hasCurrPath=yes
        fi
    done
    IFS="$jac_saved_ifs"
    if test "$jac_hasCurrPath" = "no" ; then
        AC_MSG_RESULT([no, prepend . to CLASSPATH])
        CLASSPATH=".:$CLASSPATH"
        export CLASSPATH
    else
        AC_MSG_RESULT(yes)
    fi
else
    AC_MSG_RESULT([no, good to go])
fi
])
dnl
dnl JAC_CHECK_CYGPATH - check and set the cygpath
dnl
AC_DEFUN(JAC_CHECK_CYGPATH, [
AC_MSG_CHECKING([for cygpath])
jac_hasProg=no
IFS="${IFS=   }"; pac_saved_ifs="$IFS"; IFS=":"
for path_elem in $PATH ; do
    if test -d $path_elem -a -x "$path_elem/cygpath" ; then
        jac_hasProg=yes
        break
    fi
done
IFS="$pac_saved_ifs"
if test "$jac_hasProg" = "yes" ; then
    $1="\`cygpath -w "
    $2="\`"
    AC_MSG_RESULT(yes)
else
    $1=""
    $2=""
    AC_MSG_RESULT(no)
fi
])
