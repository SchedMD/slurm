dnl
dnl/*D
dnl PAC_LANG_PUSH_COMPILERS - Replace all compilers with test versions 
dnl
dnl Synopsis:
dnl PAC_LANG_PUSH_COMPILERS
dnl
dnl Output Effects:
dnl The values of 'CC', 'CXX', 'F77', 'F90', and 'CPP' are replaced with
dnl the values of 'TESTCC' etc.  The old values are saved (see 
dnl 'PAC_LANG_POP_COMPILERS').
dnl 
dnl Calls to this macro may be nested, but only the outer-most calls have
dnl any effect.
dnl
dnl See also:
dnl PAC_LANG_POP_COMPILERS
dnl D*/
dnl
dnl These two name allow you to use TESTCC for CC, etc, in all of the 
dnl autoconf compilation tests.  This is useful, for example, when the
dnl compiler needed at the end cannot be used to build programs that can 
dnl be run, for example, as required by some parallel computing systems.
dnl Instead, define TESTCC, TESTCXX, TESTF77, and TESTF90 as the "local"
dnl compilers.  Because autoconf insists on calling cpp for the header 
dnl checks, we use TESTCPP for the CPP test as well.  And if no TESTCPP 
dnl is defined, we create one using TESTCC.
dnl
dnl 2.52 does not have try_compiler, which is like try_compile, but 
dnl it doesn't force a main program 
dnl Not quite correct, but adequate for here
ifdef([AC_TRY_COMPILER],,[AC_DEFUN([AC_TRY_COMPILER],
[cat > conftest.$ac_ext <<EOF
ifelse(_AC_LANG, [Fortran 77], ,
[
[#]line __oline__ "configure"
#include "confdefs.h"
])
[$1]
EOF
if AC_TRY_EVAL(ac_link) && test -s conftest${ac_exeext}; then
  [$2]=yes
  # If we can't run a trivial program, we are probably using a cross compiler.
  if (./conftest; exit) 2>/dev/null; then
    [$3]=no
  else
    [$3]=yes
  fi
else
  echo "configure: failed program was:" >&AC_FD_CC
  cat conftest.$ac_ext >&AC_FD_CC
  [$2]=no
fi
rm -fr conftest*])
])
dnl
dnl pac_cross_compiling overrides all tests if set to yes.  This allows
dnl us to test the cross-compilation branches of the code, and to use
dnl compilers that can both cross-compile and build code for the current
dnl platform
dnl 
AC_DEFUN(PAC_LANG_PUSH_COMPILERS,[
if test "X$pac_save_level" = "X" ; then
    pac_save_CC="$CC"
    pac_save_CXX="$CXX"
    pac_save_F77="$F77"
    pac_save_F90="$F90"
    pac_save_prog_cc_cross="$ac_cv_prog_cc_cross"
    pac_save_prog_f77_cross="$ac_cv_prog_f77_cross"
    pac_save_prog_cxx_cross="$ac_cv_prog_cxx_cross"
    pac_save_prog_f90_cross="$pac_cv_prog_f90_cross"
    if test "X$CPP" = "X" ; then
	AC_PROG_CPP
    fi
    pac_save_CPP="$CPP"
    CC="${TESTCC:=$CC}"
    CXX="${TESTCXX:=$CXX}"
    F77="${TESTF77:=$F77}"
    F90="${TESTF90:=$F90}"
    if test -z "$TESTCPP" ; then
        PAC_PROG_TESTCPP
    fi
    CPP="${TESTCPP:=$CPP}"
    pac_save_level="0"
    # Recompute cross_compiling values and set for the current language
    # This is just:
    AC_LANG_SAVE
    AC_LANG_C
    if test "$pac_cross_compiling" = "yes" ; then
        ac_cv_prog_cc_cross=yes
	ac_cv_prog_cc_works=yes
    else
        AC_TRY_COMPILER([main(){return(0);}], ac_cv_prog_cc_works, ac_cv_prog_cc_cross)
    fi
    AC_LANG_RESTORE
    # Ignore Fortran if we aren't using it.
    if test -n "$F77" ; then
        AC_LANG_SAVE
        AC_LANG_FORTRAN77
	if test "$pac_cross_compiling" = "yes" ; then
	    ac_cv_prog_f77_cross=yes
	    ac_cv_prog_f77_works=yes
	else
            AC_TRY_COMPILER(dnl
[      program conftest
      end
], ac_cv_prog_f77_works, ac_cv_prog_f77_cross)
	fi
        AC_LANG_RESTORE
    fi
    # Ignore C++ if we aren't using it.
    if test -n "$CXX" ; then
        AC_LANG_SAVE
        AC_LANG_CPLUSPLUS
        AC_TRY_COMPILER([int main(){return(0);}], ac_cv_prog_cxx_works, ac_cv_prog_cxx_cross)
        AC_LANG_RESTORE
    fi
    # Ignore Fortran 90 if we aren't using it.
    if test -n "$F90" ; then
        AC_LANG_SAVE
        PAC_LANG_FORTRAN90
	dnl We can't use AC_TRY_COMPILER because it doesn't know about 
        dnl Fortran 90
	if test "$pac_cross_compiling" = "yes" ; then
	    ac_cv_prog_f90_cross=yes
	    ac_cv_prog_f90_works=yes
	else
            cat > conftest.$ac_ext << EOF
      program conftest
      end
EOF
            if { (eval echo configure:2324: \"$ac_link\") 1>&5; (eval $ac_link) 2>&5; } && test -s conftest${ac_exeext}; then
              ac_cv_prog_f90_works=yes
              # If we can't run a trivial program, we are probably using a cross compiler.
              if (./conftest; exit) 2>/dev/null; then
                  ac_cv_prog_f90_cross=no
              else
                  ac_cv_prog_f90_cross=yes
              fi
            else
              echo "configure: failed program was:" >&5
              cat conftest.$ac_ext >&5
              ac_cv_prog_f90_works=no
            fi
	fi
	pac_cv_prog_f90_cross="$ac_cv_prog_f90_cross"
	pac_cv_prog_f90_works="$ac_cv_prog_f90_works"
        rm -fr conftest*
        AC_LANG_RESTORE
    fi
fi
pac_save_level=`expr $pac_save_level + 1`
])
dnl/*D
dnl PAC_LANG_POP_COMPILERS - Restore compilers that were displaced by
dnl PAC_LANG_PUSH_COMPILERS
dnl
dnl Synopsis:
dnl PAC_LANG_POP_COMPILERS
dnl
dnl Output Effects:
dnl The values of 'CC', 'CXX', 'F77', 'F90', and 'CPP' are replaced with
dnl their original values from the outermost call to 'PAC_LANG_PUSH_COMPILERS'.
dnl 
dnl Calls to this macro may be nested, but only the outer-most calls have
dnl any effect.
dnl
dnl See also:
dnl PAC_LANG_PUSH_COMPILERS
dnl D*/
AC_DEFUN(PAC_LANG_POP_COMPILERS,[
pac_save_level=`expr $pac_save_level - 1`
if test "X$pac_save_level" = "X0" ; then
    CC="$pac_save_CC"
    CXX="$pac_save_CXX"
    F77="$pac_save_F77"
    F90="$pac_save_F90"
    CPP="$pac_save_CPP"
    ac_cv_prog_cc_cross="$pac_save_prog_cc_cross"
    ac_cv_prog_f77_cross="$pac_save_prog_f77_cross"
    ac_cv_prog_cxx_cross="$pac_save_prog_cxx_cross"
    pac_cv_prog_f90_cross="$pac_save_prog_f90_cross"
    pac_save_level=""
fi
])
AC_DEFUN(PAC_PROG_TESTCPP,[
if test -z "$TESTCPP"; then
  AC_CACHE_VAL(pac_cv_prog_TESTCPP,[
  rm -f conftest.*
  cat > conftest.c <<EOF
  #include <assert.h>
  Syntax Error
EOF
  # On the NeXT, cc -E runs the code through the compiler's parser,
  # not just through cpp.
  TESTCPP="${TESTCC-cc} -E"
  ac_try="$TESTCPP conftest.c >/dev/null 2>conftest.out"
  if AC_TRY_EVAL(ac_try) ; then
      pac_cv_prog_TESTCPP="$TESTCPP"
  fi
  if test "X$pac_cv_prog_TESTCPP" = "X" ; then
      TESTCPP="${TESTCC-cc} -E -traditional-cpp"
      ac_try="$TESTCPP conftest.c >/dev/null 2>conftest.out"
      if AC_TRY_EVAL(ac_try) ; then
          pac_cv_prog_TESTCPP="$TESTCPP"
      fi
  fi
  if test "X$pac_cv_prog_TESTCPP" = "X" ; then
      TESTCPP="${TESTCC-cc} -nologo -E"
      ac_try="$TESTCPP conftest.c >/dev/null 2>conftest.out"
      if AC_TRY_EVAL(ac_try) ; then
          pac_cv_prog_TESTCPP="$TESTCPP"
      fi
  fi
  if test "X$pac_cv_prog_TESTCPP" = "X" ; then
      AC_PATH_PROG(TESTCPP,cpp)
  fi
  rm -f conftest.*
  ])
else
  pac_cv_prog_TESTCPP="$TESTCPP"
fi
])
