dnl
dnl An initial attempt at macros for C#.  
dnl Mostly contains information on where C# may be located and 
dnl the suffix
dnl
dnl The C# compiler under Windows is called csc.exe and the
dnl suffix is cs
dnl On my laptop, csc.exe is in 
dnl c:/WINDOWS/Microsoft.NET/Framework/v1.1.4322/csc.exe
dnl in general, in Framework/vx.x.x/csc.exe . 
dnl How can we get the most recent version?
dnl Important options:
dnl   /target:exe (console executable (default))
dnl   /target:winexe (Windows executable)
dnl   /target:library
dnl   /target:module
dnl   /debug
dnl   /optimize
dnl   /unsafe   (allow unsafe code)
AC_DEFUN(PAC_LANG_CSHARP,
[AC_REQUIRE([PAC_PROG_CSHARP])
define([AC_LANG], [CSHARP])dnl
ac_ext=$pac_cv_csharp_ext
ac_compile='${CSHARP-csc} -c $CSFLAGS conftest.$ac_ext 1>&AC_FD_CC'
ac_link='${CSHARP-csc} -o conftest${ac_exeext} $CSFLAGS $LDFLAGS conftest.$ac_ext $LIBS 1>&AC_FD_CC'
dnl cross_compiling no longer maintained by autoconf as part of the
dnl AC_LANG changes.  If we set it here, a later AC_LANG may not 
dnl restore it (in the case where one compiler claims to be a cross compiler
dnl and another does not)
dnl cross_compiling=$pac_cv_prog_csharp_cross
])
AC_DEFUN(PAC_PROG_CSHARP,[
if test -z "$CSHARP" ; then
    AC_CHECK_PROGS(CSHARP,csc)
    test -z "$CSHARP" && AC_MSG_WARN([no acceptable C# compiler found in \$PATH])
fi
dnl if test -n "$CSHARP" ; then
dnl      PAC_PROG_CSHARP_WORKS
dnl fi
dnl Cache these so we don't need to change in and out of csharp mode
ac_csharpext=$pac_cv_csharp_ext
ac_csharpcompile='${CSHARP-csc} -c $CSFLAGS conftest.$ac_csharpext 1>&AC_FD_CC'
ac_csharplink='${CSHARP-csc} -o conftest${ac_exeext} $CSFLAGS $LDFLAGS conftest.$ac_csharpext $LIBS 1>&AC_FD_CC'
# Check for problems with Intel efc compiler, if the compiler works
if test "$pac_cv_prog_csharp_works" = yes ; then
    cat > conftest.$ac_csharpext <<EOF
        program main
        end
EOF
    pac_msg=`$CSHARP -o conftest $CSFLAGS $LDFLAGS conftest.$ac_csharpext $LIBS 2>&1 | grep 'bfd assertion fail'`
    if test -n "$pac_msg" ; then
        pac_msg=`$CSHARP -o conftest $CSFLAGS $LDFLAGS conftest.$ac_csharpext -i_dynamic $LIBS 2>&1 | grep 'bfd assertion fail'`
        if test -z "$pac_msg" ; then LDFLAGS="-i_dynamic" ; fi
        # There should really be csharplinker flags rather than generic ldflags.
    fi
fi
])
