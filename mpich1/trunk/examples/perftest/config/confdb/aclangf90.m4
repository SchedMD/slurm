# This file is part of Autoconf.                       -*- Autoconf -*-
# Programming languages support.
# Copyright 2000, 2001
# Free Software Foundation, Inc.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
# 02111-1307, USA.
#
# As a special exception, the Free Software Foundation gives unlimited
# permission to copy, distribute and modify the configure scripts that
# are the output of Autoconf.  You need not follow the terms of the GNU
# General Public License when using or distributing such scripts, even
# though portions of the text of Autoconf appear in them.  The GNU
# General Public License (GPL) does govern all other use of the material
# that constitutes the Autoconf program.
#
# Certain portions of the Autoconf source text are designed to be copied
# (in certain cases, depending on the input) into the output of
# Autoconf.  We call these the "data" portions.  The rest of the Autoconf
# source text consists of comments plus executable code that decides which
# of the data portions to output in any given case.  We call these
# comments and executable code the "non-data" portions.  Autoconf never
# copies any of the non-data portions into its output.
#
# This special exception to the GPL applies to versions of Autoconf
# released by the Free Software Foundation.  When you make and
# distribute a modified version of Autoconf, you may extend this special
# exception to the GPL to apply to your modified version as well, *unless*
# your modified version has the potential to copy into its output some
# of the text that was the non-data portion of the version that you started
# with.  (In other words, unless your change moves or copies text from
# the non-data portions to the data portions.)  If your modification has
# such potential, you must delete any notice of this special exception
# to the GPL from your modified version.
#
# Written by David MacKenzie, with help from
# Franc,ois Pinard, Karl Berry, Richard Pixley, Ian Lance Taylor,
# Roland McGrath, Noah Friedman, david d zuhn, and many others.
#
#
# This file was created from aclang.m4, from the autoconf 2.52 distribution
# It contains modifications that are specific to Fortran 90 support,
# needed to support systems that wish to use separate Fortran compilers 
# for Fortran 77 and Fortran 90 programs.
# Fortran 90 part written by William Gropp.  
#


## ----------------------- ##
## 1. Language selection.  ##
## ----------------------- ##




# ----------------------------- #
# 1e. The Fortran 90 language.  #
# ----------------------------- #


# AC_LANG(Fortran 90)
# -------------------
# There is a problem here.  There is no standard extension for Fortran 90,
# and some compiler will fail to accept f90; others fail to accept f (the
# various IBM xlf/f90 compilers are very picky, and there is no extension
# that both IBM compilers will accept without additional options).
m4_define([AC_LANG(Fortran 90)],
[ac_ext=${ac_f90ext-f}
ac_compile='$F90 -c $F90FLAGS conftest.$ac_ext >&AS_MESSAGE_LOG_FD'
ac_link='$F90 -o conftest$ac_exeext $F90FLAGS $LDFLAGS conftest.$ac_ext $LIBS >&AS_MESSAGE_LOG_FD'
ac_compiler_gnu=$ac_cv_f90_compiler_gnu
])


# AC_LANG_FORTRAN90
# -----------------
AU_DEFUN([AC_LANG_FORTRAN90], [AC_LANG(Fortran 90)])


# _AC_LANG_ABBREV(Fortran 90)
# ---------------------------
m4_define([_AC_LANG_ABBREV(Fortran 90)], [f90])



## ---------------------- ##
## 2.Producing programs.  ##
## ---------------------- ##



# ------------------------ #
# 2e. Fortran 90 sources.  #
# ------------------------ #

# AC_LANG_SOURCE(Fortran 90)(BODY)
# --------------------------------
# FIXME: Apparently, according to former AC_TRY_COMPILER, the CPP
# directives must not be included.  But AC_TRY_RUN_NATIVE was not
# avoiding them, so?
m4_define([AC_LANG_SOURCE(Fortran 90)],
[$1])


# AC_LANG_PROGRAM(Fortran 90)([PROLOGUE], [BODY])
# -----------------------------------------------
# Yes, we discard the PROLOGUE.
m4_define([AC_LANG_PROGRAM(Fortran 90)],
[m4_ifval([$1],
       [m4_warn([syntax], [$0: ignoring PROLOGUE: $1])])dnl
      program main
$2
      end])


# AC_LANG_CALL(Fortran 90)(PROLOGUE, FUNCTION)
# --------------------------------------------
# FIXME: This is a guess, help!
m4_define([AC_LANG_CALL(Fortran 90)],
[AC_LANG_PROGRAM([$1],
[      call $2])])


## -------------------------------------------- ##
## 3. Looking for Compilers and Preprocessors.  ##
## -------------------------------------------- ##


# ----------------------------- #
# 3e. The Fortran 90 compiler.  #
# ----------------------------- #


# AC_LANG_PREPROC(Fortran 90)
# ---------------------------
# Find the Fortran 90 preprocessor.  Must be AC_DEFUN'd to be AC_REQUIRE'able.
AC_DEFUN([AC_LANG_PREPROC(Fortran 90)],
[m4_warn([syntax],
         [$0: No preprocessor defined for ]_AC_LANG)])


# AC_LANG_COMPILER(Fortran 90)
# ----------------------------
# Find the Fortran 90 compiler.  Must be AC_DEFUN'd to be
# AC_REQUIRE'able.
AC_DEFUN([AC_LANG_COMPILER(Fortran 90)],
[AC_REQUIRE([AC_PROG_F90])])


# ac_cv_prog_g90
# --------------
# We used to name the cache variable this way.
AU_DEFUN([ac_cv_prog_g90],
[ac_cv_f90_compiler_gnu])


# AC_PROG_F90([COMPILERS...])
# ---------------------------
# COMPILERS is a space separated list of Fortran 90 compilers to search
# for.
#
# Compilers are ordered by
#  1. F95, F90
#  2. Good/tested native compilers, bad/untested native compilers
#
# frt is the Fujitsu Fortran compiler.
# pgf90 is the Portland Group F90 compilers.
# xlf90/xlf95 are IBM (AIX) F90/F95 compilers.
# lf95 is the Lahey-Fujitsu compiler.
# fl32 is the Microsoft Fortran "PowerStation" compiler.
# epcf90 is the "Edinburgh Portable Compiler" F90.
# pathf90 is the Pathscale Fortran 90 compiler
# fort is the Compaq Fortran 90 (now 95) compiler for Tru64 and Linux/Alpha.
# ifort is another name for the Inten f90 compiler
# efc - An older Intel compiler (?)
# ifc - An older Intel compiler
AC_DEFUN([AC_PROG_F90],
[# This is the aclangf90 version of f90 language support
AC_LANG_PUSH(Fortran 90)dnl
AC_ARG_VAR([F90],    [Fortran 90 compiler command])dnl
AC_ARG_VAR([F90FLAGS], [Fortran 90 compiler flags])dnl
_AC_ARG_VAR_LDFLAGS()dnl
AC_CHECK_TOOLS(F90,
      [m4_default([$1],
                  [f95 f90 pgf90 ifort epcf90 fort xlf95 xlf90 xlf lf95 pathf90 g95 gfortran ifc efc])])

# Once we find the compiler, confirm the extension 
AC_MSG_CHECKING([that $ac_ext works as the extension for Fortran 90 program])
cat > conftest.$ac_ext <<EOF
      program conftest
      end
EOF
if AC_TRY_EVAL(ac_compile) ; then
    AC_MSG_RESULT(yes)
else
    AC_MSG_RESULT(no)
    AC_MSG_CHECKING([for extension for Fortran 90 programs])
    ac_ext="f90"
    cat > conftest.$ac_ext <<EOF
      program conftest
      end
EOF
    if AC_TRY_EVAL(ac_compile) ; then
        AC_MSG_RESULT([f90])
    else
        rm -f conftest*
        ac_ext="f"
        cat > conftest.$ac_ext <<EOF
      program conftest
      end
EOF
        if AC_TRY_EVAL(ac_compile) ; then
            AC_MSG_RESULT([f])
        else
            AC_MSG_RESULT([unknown!])
        fi
    fi
    ac_f90ext=$ac_ext
    if test "$ac_ext" = "f90" ; then
        pac_cv_f90_ext_f90=yes
    else 
        pac_cv_f90_ext_f90=no
    fi
    pac_cv_f90_ext=$ac_ext
    rm -f conftest*
fi

# Provide some information about the compiler.
echo "$as_me:__oline__:" \
     "checking for _AC_LANG compiler version" >&AS_MESSAGE_LOG_FD
ac_compiler=`set X $ac_compile; echo $[2]`
_AC_EVAL([$ac_compiler --version </dev/null >&AS_MESSAGE_LOG_FD])
_AC_EVAL([$ac_compiler -v </dev/null >&AS_MESSAGE_LOG_FD])
_AC_EVAL([$ac_compiler -V </dev/null >&AS_MESSAGE_LOG_FD])

m4_expand_once([_AC_COMPILER_EXEEXT])[]dnl
m4_expand_once([_AC_COMPILER_OBJEXT])[]dnl
# If we don't use `.F' as extension, the preprocessor is not run on the
# input file.
ac_save_ext=$ac_ext
ac_ext=F
_AC_LANG_COMPILER_GNU
ac_ext=$ac_save_ext
G90=`test $ac_compiler_gnu = yes && echo yes`
_AC_PROG_F90_G
AC_LANG_POP(Fortran 90)dnl
])# AC_PROG_F90


# _AC_PROG_F90_G
# --------------
# Check whether -g works, even if F90FLAGS is set, in case the package
# plays around with F90FLAGS (such as to build both debugging and normal
# versions of a library), tasteless as that idea is.
m4_define([_AC_PROG_F90_G],
[ac_test_F90FLAGS=${F90FLAGS+set}
ac_save_F90FLAGS=$F90FLAGS
F90FLAGS=
AC_CACHE_CHECK(whether $F90 accepts -g, ac_cv_prog_f90_g,
[F90FLAGS=-g
_AC_COMPILE_IFELSE([AC_LANG_PROGRAM()],
[ac_cv_prog_f90_g=yes],
[ac_cv_prog_f90_g=no])
])
if test "$ac_test_F90FLAGS" = set; then
  F90FLAGS=$ac_save_F90FLAGS
elif test $ac_cv_prog_f90_g = yes; then
  if test "$G95" = yes; then
    F90FLAGS="-g -O2"
  else
    F90FLAGS="-g"
  fi
else
  if test "$G95" = yes; then
    F90FLAGS="-O2"
  else
    F90FLAGS=
  fi
fi[]dnl
])# _AC_PROG_F90_G


# AC_PROG_F90_C_O
# ---------------
# Test if the Fortran 90 compiler accepts the options `-c' and `-o'
# simultaneously, and define `F90_NO_MINUS_C_MINUS_O' if it does not.
#
# The usefulness of this macro is questionable, as I can't really see
# why anyone would use it.  The only reason I include it is for
# completeness, since a similar test exists for the C compiler.
AC_DEFUN([AC_PROG_F90_C_O],
[AC_REQUIRE([AC_PROG_F90])dnl
AC_CACHE_CHECK([whether $F90 understand -c and -o together],
               [ac_cv_prog_f90_c_o],
[AC_LANG_CONFTEST([AC_LANG_PROGRAM([])])
# We test twice because some compilers refuse to overwrite an existing
# `.o' file with `-o', although they will create one.
ac_try='$F90 $F90FLAGS -c conftest.$ac_ext -o conftest.$ac_objext >&AS_MESSAGE_LOG_FD'
if AC_TRY_EVAL(ac_try) &&
     test -f conftest.$ac_objext &&
     AC_TRY_EVAL(ac_try); then
  ac_cv_prog_f90_c_o=yes
else
  ac_cv_prog_f90_c_o=no
fi
rm -f conftest*])
if test $ac_cv_prog_f90_c_o = no; then
  AC_DEFINE(F90_NO_MINUS_C_MINUS_O, 1,
            [Define if your Fortran 90 compiler doesn't accept -c and -o together.])
fi
])# AC_PROG_F90_C_O

## ------------------------------- ##
## 4. Compilers' characteristics.  ##
## ------------------------------- ##

# ---------------------------------------- #
# 4d. Fortan 90 compiler characteristics.  #
# ---------------------------------------- #


# _AC_PROG_F90_V_OUTPUT([FLAG = $ac_cv_prog_f90_v])
# -------------------------------------------------
# Link a trivial Fortran program, compiling with a verbose output FLAG
# (which default value, $ac_cv_prog_f90_v, is computed by
# _AC_PROG_F90_V), and return the output in $ac_f90_v_output.  This
# output is processed in the way expected by AC_F90_LIBRARY_LDFLAGS,
# so that any link flags that are echoed by the compiler appear as
# space-separated items.
AC_DEFUN([_AC_PROG_F90_V_OUTPUT],
[AC_REQUIRE([AC_PROG_F90])dnl
AC_LANG_PUSH(Fortran 90)dnl

AC_LANG_CONFTEST([AC_LANG_PROGRAM([])])

# Compile and link our simple test program by passing a flag (argument
# 1 to this macro) to the Fortran 90 compiler in order to get
# "verbose" output that we can then parse for the Fortran 90 linker
# flags.
ac_save_F90FLAGS=$F90FLAGS
F90FLAGS="$F90FLAGS m4_default([$1], [$ac_cv_prog_f90_v])"
(eval echo $as_me:__oline__: \"$ac_link\") >&AS_MESSAGE_LOG_FD
ac_f90_v_output=`eval $ac_link AS_MESSAGE_LOG_FD>&1 2>&1 | grep -v 'Driving:'`
echo "$ac_f90_v_output" >&AS_MESSAGE_LOG_FD
F90FLAGS=$ac_save_F90FLAGS

rm -f conftest*
AC_LANG_POP(Fortran 90)dnl

# If we are using xlf then replace all the commas with spaces.
if echo $ac_f90_v_output | grep xlfentry >/dev/null 2>&1; then
  ac_f90_v_output=`echo $ac_f90_v_output | sed 's/,/ /g'`
fi

# If we are using Cray Fortran then delete quotes.
# Use "\"" instead of '"' for font-lock-mode.
# FIXME: a more general fix for quoted arguments with spaces?
if echo $ac_f90_v_output | grep cft90 >/dev/null 2>&1; then
  ac_f90_v_output=`echo $ac_f90_v_output | sed "s/\"//g"`
fi[]dnl
])# _AC_PROG_F90_V_OUTPUT


# _AC_PROG_F90_V
# --------------
#
# Determine the flag that causes the Fortran 90 compiler to print
# information of library and object files (normally -v)
# Needed for AC_F90_LIBRARY_FLAGS
# Some compilers don't accept -v (Lahey: -verbose, xlf: -V, Fujitsu: -###)
AC_DEFUN([_AC_PROG_F90_V],
[AC_CACHE_CHECK([how to get verbose linking output from $F90],
                [ac_cv_prog_f90_v],
[AC_LANG_ASSERT(Fortran 90)
AC_COMPILE_IFELSE([AC_LANG_PROGRAM()],
[ac_cv_prog_f90_v=
# Try some options frequently used verbose output
for ac_verb in -v -verbose --verbose -V -\#\#\#; do
  _AC_PROG_F90_V_OUTPUT($ac_verb)
  # look for -l* and *.a constructs in the output
  for ac_arg in $ac_f90_v_output; do
     case $ac_arg in
        [[\\/]]*.a | ?:[[\\/]]*.a | -[[lLRu]]*)
          ac_cv_prog_f90_v=$ac_verb
          break 2 ;;
     esac
  done
done
if test -z "$ac_cv_prog_f90_v"; then
   AC_MSG_WARN([cannot determine how to obtain linking information from $F90])
fi],
                  [AC_MSG_WARN([compilation failed])])
])])# _AC_PROG_F90_V


# AC_F90_LIBRARY_LDFLAGS
# ----------------------
#
# Determine the linker flags (e.g. "-L" and "-l") for the Fortran 90
# intrinsic and run-time libraries that are required to successfully
# link a Fortran 90 program or shared library.  The output variable
# F90LIBS is set to these flags.
#
# This macro is intended to be used in those situations when it is
# necessary to mix, e.g. C++ and Fortran 90, source code into a single
# program or shared library.
#
# For example, if object files from a C++ and Fortran 90 compiler must
# be linked together, then the C++ compiler/linker must be used for
# linking (since special C++-ish things need to happen at link time
# like calling global constructors, instantiating templates, enabling
# exception support, etc.).
#
# However, the Fortran 90 intrinsic and run-time libraries must be
# linked in as well, but the C++ compiler/linker doesn't know how to
# add these Fortran 90 libraries.  Hence, the macro
# "AC_F90_LIBRARY_LDFLAGS" was created to determine these Fortran 90
# libraries.
#
# This macro was packaged in its current form by Matthew D. Langston.
# However, nearly all of this macro came from the "OCTAVE_FLIBS" macro
# in "octave-2.0.13/aclocal.m4", and full credit should go to John
# W. Eaton for writing this extremely useful macro.  Thank you John.
AC_DEFUN([AC_F90_LIBRARY_LDFLAGS],
[AC_LANG_PUSH(Fortran 90)dnl
_AC_PROG_F90_V
AC_CACHE_CHECK([for Fortran 90 libraries], ac_cv_f90libs,
[if test "x$F90LIBS" != "x"; then
  ac_cv_f90libs="$F90LIBS" # Let the user override the test.
else

_AC_PROG_F90_V_OUTPUT

ac_cv_f90libs=

# Save positional arguments (if any)
ac_save_positional="$[@]"

set X $ac_f90_v_output
while test $[@%:@] != 1; do
  shift
  ac_arg=$[1]
  case $ac_arg in
        [[\\/]]*.a | ?:[[\\/]]*.a)
          AC_LIST_MEMBER_OF($ac_arg, $ac_cv_f90libs, ,
              ac_cv_f90libs="$ac_cv_f90libs $ac_arg")
          ;;
        -bI:*)
          AC_LIST_MEMBER_OF($ac_arg, $ac_cv_f90libs, ,
             [AC_LINKER_OPTION([$ac_arg], ac_cv_f90libs)])
          ;;
          # Ignore these flags.
        -lang* | -lcrt0.o | -lc | -lgcc | -LANG:=*)
          ;;
        -lkernel32)
          test x"$CYGWIN" != xyes && ac_cv_f90libs="$ac_cv_f90libs $ac_arg"
          ;;
        -[[LRuY]])
          # These flags, when seen by themselves, take an argument.
          # We remove the space between option and argument and re-iterate
          # unless we find an empty arg or a new option (starting with -)
	  case $[2] in
             "" | -*);;
             *)
		ac_arg="$ac_arg$[2]"
		shift; shift
		set X $ac_arg "$[@]"
		;;
	  esac
          ;;
        -YP,*)
          for ac_j in `echo $ac_arg | sed -e 's/-YP,/-L/;s/:/ -L/g'`; do
            AC_LIST_MEMBER_OF($ac_j, $ac_cv_f90libs, ,
                            [ac_arg="$ac_arg $ac_j"
                             ac_cv_f90libs="$ac_cv_f90libs $ac_j"])
          done
          ;;
        -[[lLR]]*)
          AC_LIST_MEMBER_OF($ac_arg, $ac_cv_f90libs, ,
                          ac_cv_f90libs="$ac_cv_f90libs $ac_arg")
          ;;
          # Ignore everything else.
  esac
done
# restore positional arguments
set X $ac_save_positional; shift

# We only consider "LD_RUN_PATH" on Solaris systems.  If this is seen,
# then we insist that the "run path" must be an absolute path (i.e. it
# must begin with a "/").
case `(uname -sr) 2>/dev/null` in
   "SunOS 5"*)
      ac_ld_run_path=`echo $ac_f90_v_output |
                        sed -n 's,^.*LD_RUN_PATH *= *\(/[[^ ]]*\).*$,-R\1,p'`
      test "x$ac_ld_run_path" != x &&
        AC_LINKER_OPTION([$ac_ld_run_path], ac_cv_f90libs)
      ;;
esac
fi # test "x$F90LIBS" = "x"
])
F90LIBS="$ac_cv_f90libs"
AC_SUBST(F90LIBS)
AC_LANG_POP(Fortran 90)dnl
])# AC_F90_LIBRARY_LDFLAGS


# AC_F90_DUMMY_MAIN([ACTION-IF-FOUND], [ACTION-IF-NOT-FOUND])
# -----------------------------------------------------------
#
# Detect name of dummy main routine required by the Fortran libraries,
# (if any) and define F90_DUMMY_MAIN to this name (which should be
# used for a dummy declaration, if it is defined).  On some systems,
# linking a C program to the Fortran library does not work unless you
# supply a dummy function called something like MAIN__.
#
# Execute ACTION-IF-NOT-FOUND if no way of successfully linking a C
# program with the F90 libs is found; default to exiting with an error
# message.  Execute ACTION-IF-FOUND if a dummy routine name is needed
# and found or if it is not needed (default to defining F90_DUMMY_MAIN
# when needed).
#
# What is technically happening is that the Fortran libraries provide
# their own main() function, which usually initializes Fortran I/O and
# similar stuff, and then calls MAIN__, which is the entry point of
# your program.  Usually, a C program will override this with its own
# main() routine, but the linker sometimes complain if you don't
# provide a dummy (never-called) MAIN__ routine anyway.
#
# Of course, programs that want to allow Fortran subroutines to do
# I/O, etcetera, should call their main routine MAIN__() (or whatever)
# instead of main().  A separate autoconf test (AC_F90_MAIN) checks
# for the routine to use in this case (since the semantics of the test
# are slightly different).  To link to e.g. purely numerical
# libraries, this is normally not necessary, however, and most C/C++
# programs are reluctant to turn over so much control to Fortran.  =)
#
# The name variants we check for are (in order):
#   MAIN__ (g90, MAIN__ required on some systems; IRIX, MAIN__ optional)
#   MAIN_, __main (SunOS)
#   MAIN _MAIN __MAIN main_ main__ _main (we follow DDD and try these too)
AC_DEFUN([AC_F90_DUMMY_MAIN],
[AC_REQUIRE([AC_F90_LIBRARY_LDFLAGS])dnl
m4_define([_AC_LANG_PROGRAM_C_F90_HOOKS],
[#ifdef F90_DUMMY_MAIN
#  ifdef __cplusplus
     extern "C"
#  endif
   int F90_DUMMY_MAIN() { return 1; }
#endif
])
AC_CACHE_CHECK([for dummy main to link with Fortran 90 libraries],
               ac_cv_f90_dummy_main,
[AC_LANG_PUSH(C)dnl
 ac_f90_dm_save_LIBS=$LIBS
 LIBS="$LIBS $F90LIBS"

 # First, try linking without a dummy main:
 AC_TRY_LINK([], [],
             ac_cv_f90_dummy_main=none,
             ac_cv_f90_dummy_main=unknown)

 if test $ac_cv_f90_dummy_main = unknown; then
   for ac_func in MAIN__ MAIN_ __main MAIN _MAIN __MAIN main_ main__ _main; do
     AC_TRY_LINK([@%:@define F90_DUMMY_MAIN $ac_func],
                 [], [ac_cv_f90_dummy_main=$ac_func; break])
   done
 fi
 rm -f conftest*
 LIBS=$ac_f90_dm_save_LIBS
 AC_LANG_POP(C)dnl
])
F90_DUMMY_MAIN=$ac_cv_f90_dummy_main
AS_IF([test "$F90_DUMMY_MAIN" != unknown],
      [m4_default([$1],
[if test $F90_DUMMY_MAIN != none; then
  AC_DEFINE_UNQUOTED([F90_DUMMY_MAIN], $F90_DUMMY_MAIN,
                     [Define to dummy `main' function (if any) required to
                      link to the Fortran 90 libraries.])
fi])],
      [m4_default([$2],
                [AC_MSG_ERROR([Linking to Fortran libraries from C fails.])])])
])# AC_F90_DUMMY_MAIN


# AC_F90_MAIN
# -----------
# Define F90_MAIN to name of alternate main() function for use with
# the Fortran libraries.  (Typically, the libraries may define their
# own main() to initialize I/O, etcetera, that then call your own
# routine called MAIN__ or whatever.)  See AC_F90_DUMMY_MAIN, above.
# If no such alternate name is found, just define F90_MAIN to main.
#
AC_DEFUN([AC_F90_MAIN],
[AC_REQUIRE([AC_F90_LIBRARY_LDFLAGS])dnl
AC_CACHE_CHECK([for alternate main to link with Fortran 90 libraries],
               ac_cv_f90_main,
[AC_LANG_PUSH(C)dnl
 ac_f90_m_save_LIBS=$LIBS
 LIBS="$LIBS $F90LIBS"
 ac_cv_f90_main="main" # default entry point name

 for ac_func in MAIN__ MAIN_ __main MAIN _MAIN __MAIN main_ main__ _main; do
   AC_TRY_LINK([#undef F90_DUMMY_MAIN
@%:@define main $ac_func], [], [ac_cv_f90_main=$ac_func; break])
 done
 rm -f conftest*
 LIBS=$ac_f90_m_save_LIBS
 AC_LANG_POP(C)dnl
])
AC_DEFINE_UNQUOTED([F90_MAIN], $ac_cv_f90_main,
                   [Define to alternate name for `main' routine that is
                    called from a `main' in the Fortran libraries.])
])# AC_F90_MAIN


# _AC_F90_NAME_MANGLING
# ---------------------
# Test for the name mangling scheme used by the Fortran 90 compiler.
#
# Sets ac_cv_f90_mangling. The value contains three fields, separated
# by commas:
#
# lower case / upper case:
#    case translation of the Fortan 90 symbols
# underscore / no underscore:
#    whether the compiler appends "_" to symbol names
# extra underscore / no extra underscore:
#    whether the compiler appends an extra "_" to symbol names already
#    containing at least one underscore
#
AC_DEFUN([_AC_F90_NAME_MANGLING],
[AC_REQUIRE([AC_F90_LIBRARY_LDFLAGS])dnl
AC_REQUIRE([AC_F90_DUMMY_MAIN])dnl
AC_CACHE_CHECK([for Fortran 90 name-mangling scheme],
               ac_cv_f90_mangling,
[AC_LANG_PUSH(Fortran 90)dnl
AC_COMPILE_IFELSE(
[      subroutine foobar()
      return
      end
      subroutine foo_bar()
      return
      end],
[mv conftest.$ac_objext cf90_test.$ac_objext

  AC_LANG_PUSH(C)dnl

  ac_save_LIBS=$LIBS
  LIBS="cf90_test.$ac_objext $LIBS $F90LIBS"

  ac_success=no
  for ac_foobar in foobar FOOBAR; do
    for ac_underscore in "" "_"; do
      ac_func="$ac_foobar$ac_underscore"
      AC_TRY_LINK_FUNC($ac_func,
         [ac_success=yes; break 2])
    done
  done

  if test "$ac_success" = "yes"; then
     case $ac_foobar in
        foobar)
           ac_case=lower
           ac_foo_bar=foo_bar
           ;;
        FOOBAR)
           ac_case=upper
           ac_foo_bar=FOO_BAR
           ;;
     esac

     ac_success_extra=no
     for ac_extra in "" "_"; do
        ac_func="$ac_foo_bar$ac_underscore$ac_extra"
        AC_TRY_LINK_FUNC($ac_func,
        [ac_success_extra=yes; break])
     done

     if test "$ac_success_extra" = "yes"; then
	ac_cv_f90_mangling="$ac_case case"
        if test -z "$ac_underscore"; then
           ac_cv_f90_mangling="$ac_cv_f90_mangling, no underscore"
	else
           ac_cv_f90_mangling="$ac_cv_f90_mangling, underscore"
        fi
        if test -z "$ac_extra"; then
           ac_cv_f90_mangling="$ac_cv_f90_mangling, no extra underscore"
	else
           ac_cv_f90_mangling="$ac_cv_f90_mangling, extra underscore"
        fi
      else
	ac_cv_f90_mangling="unknown"
      fi
  else
     ac_cv_f90_mangling="unknown"
  fi

  LIBS=$ac_save_LIBS
  AC_LANG_POP(C)dnl
  rm -f cf90_test* conftest*])
AC_LANG_POP(Fortran 90)dnl
])
])# _AC_F90_NAME_MANGLING

# The replacement is empty.
AU_DEFUN([AC_F90_NAME_MANGLING], [])


# AC_F90_WRAPPERS
# ---------------
# Defines C macros F90_FUNC(name,NAME) and F90_FUNC_(name,NAME) to
# properly mangle the names of C identifiers, and C identifiers with
# underscores, respectively, so that they match the name mangling
# scheme used by the Fortran 90 compiler.
AC_DEFUN([AC_F90_WRAPPERS],
[AC_REQUIRE([_AC_F90_NAME_MANGLING])dnl
AH_TEMPLATE([F90_FUNC],
    [Define to a macro mangling the given C identifier (in lower and upper
     case), which must not contain underscores, for linking with Fortran.])dnl
AH_TEMPLATE([F90_FUNC_],
    [As F90_FUNC, but for C identifiers containing underscores.])dnl
case $ac_cv_f90_mangling in
  "lower case, no underscore, no extra underscore")
          AC_DEFINE([F90_FUNC(name,NAME)],  [name])
          AC_DEFINE([F90_FUNC_(name,NAME)], [name]) ;;
  "lower case, no underscore, extra underscore")
          AC_DEFINE([F90_FUNC(name,NAME)],  [name])
          AC_DEFINE([F90_FUNC_(name,NAME)], [name ## _]) ;;
  "lower case, underscore, no extra underscore")
          AC_DEFINE([F90_FUNC(name,NAME)],  [name ## _])
          AC_DEFINE([F90_FUNC_(name,NAME)], [name ## _]) ;;
  "lower case, underscore, extra underscore")
          AC_DEFINE([F90_FUNC(name,NAME)],  [name ## _])
          AC_DEFINE([F90_FUNC_(name,NAME)], [name ## __]) ;;
  "upper case, no underscore, no extra underscore")
          AC_DEFINE([F90_FUNC(name,NAME)],  [NAME])
          AC_DEFINE([F90_FUNC_(name,NAME)], [NAME]) ;;
  "upper case, no underscore, extra underscore")
          AC_DEFINE([F90_FUNC(name,NAME)],  [NAME])
          AC_DEFINE([F90_FUNC_(name,NAME)], [NAME ## _]) ;;
  "upper case, underscore, no extra underscore")
          AC_DEFINE([F90_FUNC(name,NAME)],  [NAME ## _])
          AC_DEFINE([F90_FUNC_(name,NAME)], [NAME ## _]) ;;
  "upper case, underscore, extra underscore")
          AC_DEFINE([F90_FUNC(name,NAME)],  [NAME ## _])
          AC_DEFINE([F90_FUNC_(name,NAME)], [NAME ## __]) ;;
  *)
          AC_MSG_WARN([unknown Fortran 90 name-mangling scheme])
          ;;
esac
])# AC_F90_WRAPPERS


# AC_F90_FUNC(NAME, [SHELLVAR = NAME])
# ------------------------------------
# For a Fortran subroutine of given NAME, define a shell variable
# $SHELLVAR to the Fortran-90 mangled name.  If the SHELLVAR
# argument is not supplied, it defaults to NAME.
AC_DEFUN([AC_F90_FUNC],
[AC_REQUIRE([_AC_F90_NAME_MANGLING])dnl
case $ac_cv_f90_mangling in
  upper*) ac_val="m4_toupper([$1])" ;;
  lower*) ac_val="m4_tolower([$1])" ;;
  *)      ac_val="unknown" ;;
esac
case $ac_cv_f90_mangling in *," underscore"*) ac_val="$ac_val"_ ;; esac
m4_if(m4_index([$1],[_]),-1,[],
[case $ac_cv_f90_mangling in *," extra underscore"*) ac_val="$ac_val"_ ;; esac
])
m4_default([$2],[$1])="$ac_val"
])# AC_F90_FUNC

#
# ------------------------------------------------------------------------
# Special characteristics that have no autoconf counterpart but that
# we need as part of the Fortran 90 support.  To distinquish these, they
# have a [PAC] prefix.
#
dnl
dnl PAC_F90_MODULE_EXT(action if found,action if not found)
dnl
AC_DEFUN([PAC_F90_MODULE_EXT],
[AC_CACHE_CHECK([for Fortran 90 module extension],
pac_cv_f90_module_ext,[
# aclang90.m4 version
pac_cv_f90_module_case="unknown"
AC_LANG_PUSH(Fortran 90)
cat >conftest.$ac_ext <<EOF
        module conftest
        integer n
        parameter (n=1)
        end module conftest
EOF
if AC_TRY_EVAL(ac_compile) ; then
   dnl Look for module name
   dnl First, try to find known names.  This avoids confusion caused by
   dnl additional files (like <name>.stb created by some versions of pgf90)
   for name in conftest CONFTEST ; do
       for ext in mod MOD ; do
           if test -s $name.$ext ; then
               if test $name = conftest ; then
                   pac_cv_f90_module_case=lower
               else
                   pac_cv_f90_module_case=upper
               fi
               pac_cv_f90_module_ext=$ext
               pac_MOD=$ext
               break
           fi
       done
       if test -n "$pac_cv_f90_module_ext" ; then break ; fi
   done
   if test -z "$pac_MOD" ; then
      pac_MOD=`ls conftest* AS_MESSAGE_LOG_FD>&1 2>&1 | grep -v conftest.$ac_ext | grep -v conftest.o`
      pac_MOD=`echo $pac_MOD | sed -e 's/conftest\.//g'`
      pac_cv_f90_module_case="lower"
      if test "X$pac_MOD" = "X" ; then
	   for file in CONFTEST* ; do
	       if test "x$file" = "xCONFTEST.$ac_ext" ; then continue ; fi
	       if test "x$file" = "xCONFTEST.o" ; then continue ; fi
	       if test -s "$file" ; then 
	           pac_MOD=$file
	           break
	       fi
	   done
           pac_MOD=`echo $pac_MOD | sed -e 's/CONFTEST\.//g'`
	   if test -n "$pac_MOD" ; then
	       testname="CONFTEST"
	       pac_cv_f90_module_case="upper"
	   fi
       fi
       if test -z "$pac_MOD" ; then 
	   pac_cv_f90_module_ext="unknown"
       else
	   pac_cv_f90_module_ext=$pac_MOD
       fi
    fi
else
    echo "configure: failed program was:" >&AC_FD_CC
    cat conftest.$ac_ext >&AC_FD_CC
    pac_cv_f90_module_ext="unknown"
fi
AC_LANG_POP
rm -f conftest*
])
AC_SUBST(F90MODEXT)
if test "$pac_cv_f90_module_ext" = "unknown" ; then
    ifelse($2,,:,[$2])
else
    ifelse($1,,F90MODEXT=$pac_MOD,[$1])
fi
])
dnl
dnl PAC_F90_MODULE_INCFLAG
AC_DEFUN([PAC_F90_MODULE_INCFLAG],[
AC_CACHE_CHECK([for Fortran 90 module include flag],
pac_cv_f90_module_incflag,[
AC_REQUIRE([PAC_F90_MODULE_EXT])
AC_LANG_PUSH(Fortran 90)
#
# Note that the name of the file and the name of the module must be
# the same (some compilers use the module, some the file name)
rm -f work.pc work.pcl conftest.$ac_ext
cat >conftest.$ac_ext <<EOF
        module conf
        integer n
        parameter (n=1)
        end module conf
EOF
pac_madedir="no"
if test ! -d conftestdir ; then mkdir conftestdir ; pac_madedir="yes"; fi
if test "$pac_cv_f90_module_case" = "upper" ; then
    pac_module="CONF.$pac_cv_f90_module_ext"
else
    pac_module="conf.$pac_cv_f90_module_ext"
fi
if AC_TRY_EVAL(ac_compile) ; then
    if test -s "$pac_module" ; then
        mv $pac_module conftestdir
	# Remove any temporary files, and hide the work.pc file (if
	# the compiler generates them)
	if test -f work.pc ; then 
	    mv -f work.pc conftest.pc
        fi
	rm -f work.pcl
    else
	AC_MSG_WARN([Unable to build a simple F90 module])
        echo "configure: failed program was:" >&AC_FD_CC
        cat conftest.$ac_f90ext >&AC_FD_CC
    fi
else
    echo "configure: failed program was:" >&AC_FD_CC
    cat conftest.$ac_ext >&AC_FD_CC
fi
rm -f conftest.$ac_ext
cat >conftest.$ac_ext <<EOF
        program main
        use conf
        end
EOF
if ${F90-f90} -c $F90FLAGS -Iconftestdir conftest.$ac_ext 1>&AC_FD_CC 2>&1 && \
	test -s conftest.o ; then
    pac_cv_f90_module_incflag="-I"
elif ${F90-f90} -c $F90FLAGS -Mconftestdir conftest.$ac_ext 1>&AC_FD_CC 2>&1 && \
	test-s conftest.o ; then
    pac_cv_f90_module_incflag="-M"
elif ${F90-f90} -c $F90FLAGS -pconftestdir conftest.$ac_ext 1>&AC_FD_CC 2>&1 && \
	test -s conftest.o ; then
    pac_cv_f90_module_incflag="-p"
elif test -s work.pc ; then 
     mv conftest.pc conftestdir/mpimod.pc
     echo "mpimod.pc" > conftestdir/mpimod.pcl
     echo "`pwd`/conftestdir/mpimod.pc" >> conftestdir/mpimod.pcl
     if ${F90-f90} -c $F90FLAGS -cl,conftestdir/mpimod.pcl conftest.$ac_ext 1>&AC_FD_CC 2>&1 && test -s conftest.o ; then
         pac_cv_f90_module_incflag='-cl,'
	# Not quite right; see the comments that follow
         AC_MSG_RESULT([-cl,filename where filename contains a list of files and directories])
	 F90_WORK_FILES_ARG="-cl,mpimod.pcl"
         F90MODINCSPEC="-cl,<dir>/<file>mod.pcl"
	 AC_SUBST(F90_WORK_FILES_ARG)
     else 
         # The version of the Intel compiler that I have refuses to let
	 # you put the "work catalog" list anywhere but the current directory.
         pac_cv_f90_module_incflag="Unavailable!"
     fi
else
    # Early versions of the Intel ifc compiler required a *file*
    # containing the names of files that contained the names of the
    # 
    # -cl,filename.pcl
    #   filename.pcl contains
    #     fullpathname.pc
    # The "fullpathname.pc" is generated, I believe, when a module is 
    # compiled.  
    # Intel compilers use a wierd system: -cl,filename.pcl .  If no file is
    # specified, work.pcl and work.pc are created.  However, if you specify
    # a file, it must contain a the name of a file ending in .pc .  Ugh!
    pac_cv_f90_module_incflag="unknown"
fi
if test "$pac_madedir" = "yes" ; then rm -rf conftestdir ; fi
rm -f conftest*
AC_LANG_POP
])
AC_SUBST(F90MODINCFLAG)
F90MODINCFLAG=$pac_cv_f90_module_incflag
])
AC_DEFUN([PAC_F90_MODULE],[
PAC_F90_MODULE_EXT
PAC_F90_MODULE_INCFLAG
])
AC_DEFUN([PAC_F90_EXT],[
AC_CACHE_CHECK([whether Fortran 90 accepts f90 suffix],
pac_cv_f90_ext_f90,[
save_ac_f90ext=$ac_f90ext
ac_f90ext="f90"
AC_LANG_PUSH(Fortran 90)
AC_TRY_COMPILE(,,pac_cv_f90_ext_f90="yes",pac_cv_f90_ext_f90="no")
AC_LANG_POP
])
if test "$pac_cv_f90_ext_f90" = "yes" ; then
    ac_f90ext=f90
else
    ac_f90ext=f
fi
])
dnl
dnl/*D 
dnl PAC_PROG_F90_INT_KIND - Determine kind parameter for an integer with
dnl the specified number of bytes.
dnl
dnl Synopsis:
dnl  PAC_PROG_F90_INT_KIND(variable-to-set,number-of-bytes,[cross-size])
dnl
dnl D*/
AC_DEFUN(PAC_PROG_F90_INT_KIND,[
# Set the default
$1=-1
if test "$pac_cv_prog_f90_cross" = "yes" ; then
    $1="$3"
else
AC_LANG_PUSH(Fortran 90)
if test -n "$ac_compile" ; then
    AC_MSG_CHECKING([for Fortran 90 integer kind for $2-byte integers])
    # Convert bytes to digits
    case $2 in 
	1) sellen=2 ;;
	2) sellen=4 ;;
	4) sellen=8 ;;
	8) sellen=16 ;;
       16) sellen=30 ;;
	*) sellen=8 ;;
    esac
    # Check for cached value
    eval testval=\$"pac_cv_prog_f90_int_kind_$sellen"
    if test -n "$testval" ; then 
        AC_MSG_RESULT([$testval (cached)])
	$1=$testval
    else
        # must compute
        rm -f conftest*
        cat <<EOF > conftest.$ac_ext
      program main
      integer i
      i = selected_int_kind($sellen)
      open(8, file="conftest1.out", form="formatted")
      write (8,*) i
      close(8)
      stop
      end
EOF
        KINDVAL="unavailable"
        eval "pac_cv_prog_f90_int_kind_$sellen"=-1
        if AC_TRY_EVAL(ac_link) && test -s conftest ; then
            ./conftest >>config.log 2>&1
            if test -s conftest1.out ; then
	        # Because of write, there may be a leading blank.
                KINDVAL=`cat conftest1.out | sed 's/ //g'`
 	        eval "pac_cv_prog_f90_int_kind_$sellen"=$KINDVAL
	        $1=$KINDVAL
            fi
        fi
        rm -f conftest*
	AC_MSG_RESULT($KINDVAL)
    fi # not cached
fi # Has Fortran 90
AC_LANG_POP
fi # is not cross compiling
])dnl
dnl
dnl PAC_F90_AND_F77_COMPATIBLE([action-if-true],[action-if-false])
dnl
AC_DEFUN([PAC_F90_AND_F77_COMPATIBLE],[
AC_REQUIRE([PAC_PROG_F90_WORKS])
AC_CACHE_CHECK([whether Fortran 90 works with Fortran 77],
pac_cv_f90_and_f77,[
pac_cv_f90_and_f77="unknown"
rm -f conftest*
if test -z "$ac_ext_f90" -a -n "$pac_cv_f90_ext" ; then ac_ext_f90=$pac_cv_f90_ext ; fi
# Define the two language-specific steps
link_f90='${F90-f90} -o conftest${ac_exeext} $F90FLAGS $LDFLAGS conftest1.$ac_ext_f90 conftest2.o $LIBS 1>&AC_FD_CC'
compile_f77='${F77-f77} -c $FFLAGS conftest2.f 1>&AC_FD_CC'
# Create test programs
cat > conftest1.$ac_ext_f90 <<EOF
       program main
       integer a
       a = 1
       call t1_2(a)
       end
EOF
cat > conftest2.f <<EOF
       subroutine t1_2(b)
       integer b
       b = b + 1
       end
EOF
# compile the f77 program and link with the f90 program
# The reverse may not work because the Fortran 90 environment may
# expect to be in control (and to provide library files unknown to any other
# environment, even Fortran 77!)
if AC_TRY_EVAL(compile_f77) ; then
    if AC_TRY_EVAL(link_f90) && test -x conftest ; then
        pac_cv_f90_and_f77="yes"
    else 
        pac_cv_f90_and_f77="no"
    fi
    # Some versions of the Intel compiler produce these two files
    rm -f work.pc work.pcl
else
    pac_cv_f90_and_f77="yes"
fi])
# Perform the requested action based on whether the test succeeded
if test "$pac_cv_f90_and_f77" = yes ; then
    ifelse($1,,:,[$1])
else
    ifelse($2,,:,[$2])
fi
])
dnl
dnl Backwards compatibility features
dnl
AC_DEFUN([PAC_PROG_F90],[AC_PROG_F90])
AC_DEFUN([PAC_LANG_FORTRAN90],[AC_LANG_PUSH(Fortran 90)])
dnl Internal routine for testing F90
dnl PAC_PROG_F90_WORKS()
AC_DEFUN(PAC_PROG_F90_WORKS,
[AC_MSG_CHECKING([for extension for Fortran 90 programs])
pac_cv_f90_ext="f90"
cat > conftest.$pac_cv_f90_ext <<EOF
      program conftest
      end
EOF
ac_compile='${F90-f90} -c $F90FLAGS conftest.$pac_cv_f90_ext 1>&AC_FD_CC'
if AC_TRY_EVAL(ac_compile) ; then
    AC_MSG_RESULT([f90])
else
    rm -f conftest*
    pac_cv_f90_ext="f"
    cat > conftest.$pac_cv_f90_ext <<EOF
      program conftest
      end
EOF
    if AC_TRY_EVAL(ac_compile) ; then
	AC_MSG_RESULT([f])
    else
        AC_MSG_RESULT([unknown!])
    fi
fi
AC_MSG_CHECKING([whether the Fortran 90 compiler ($F90 $F90FLAGS $LDFLAGS) works])
AC_LANG_SAVE
# We cannot use _LANG_FORTRAN90 here because we will usually be executing this
# test in the context of _PROG_F90, which is a require on _LANG_FORTRAN90.
# Instead, we insert the necessary code from _LANG_FORTRAN90 here
dnl PAC_LANG_FORTRAN90
dnl define(ifdef([_AC_LANG],[_AC_LANG],[AC_LANG]), [FORTRAN90])dnl
ifdef([_AC_LANG],[define([_AC_LANG],FORTRAN90)],[define([AC_LANG],FORTRAN90)])
dnl define([AC_LANG], [FORTRAN90])dnl
ac_ext=$pac_cv_f90_ext
ac_compile='${F90-f90} -c $F90FLAGS conftest.$ac_ext 1>&AC_FD_CC'
ac_link='${F90-f90} -o conftest${ac_exeext} $F90FLAGS $LDFLAGS conftest.$ac_ext $LIBS 1>&AC_FD_CC'
dnl cross_compiling no longer maintained by autoconf as part of the
dnl AC_LANG changes.  If we set it here, a later AC_LANG may not 
dnl restore it (in the case where one compiler claims to be a cross compiler
dnl and another does not)
dnl cross_compiling=$pac_cv_prog_f90_cross
# Include a Fortran 90 construction to distinguish between Fortran 77 
# and Fortran 90 compilers.
cat >conftest.$ac_ext <<EOF
      program conftest
      integer, dimension(10) :: n
      end
EOF
if AC_TRY_EVAL(ac_link) && test -s conftest${ac_exeect} ; then
    pac_cv_prog_f90_works="yes"
    if (./conftest; exit) 2>/dev/null ; then
        pac_cv_prog_f90_cross="no"
    else
        pac_cv_prog_f90_cross="yes"
    fi
else
  echo "configure: failed program was:" >&AC_FD_CC
  cat conftest.$ac_ext >&AC_FD_CC
  pac_cv_prog_f90_works="no"
fi
rm -f conftest*
# The intel compiler sometimes generates these work.pc and .pcl files
rm -f work.pc work.pcl
AC_LANG_RESTORE
AC_MSG_RESULT($pac_cv_prog_f90_works)
if test "$pac_cv_prog_f90_works" = no; then
  AC_MSG_WARN([installation or configuration problem: Fortran 90 compiler cannot create executables.])
fi
AC_MSG_CHECKING([whether the Fortran 90 compiler ($F90 $F90FLAGS $LDFLAGS) is a cross-compiler])
AC_MSG_RESULT($pac_cv_prog_f90_cross)
dnl cross_compiling no longer maintained by autoconf as part of the
dnl AC_LANG changes.  If we set it here, a later AC_LANG may not 
dnl restore it (in the case where one compiler claims to be a cross compiler
dnl and another does not)
dnl cross_compiling=$pac_cv_prog_f90_cross
])
dnl
dnl PAC_F90_AND_F77_COMPATIBLE([action-if-true],[action-if-false])
dnl
AC_DEFUN([PAC_F90_AND_F77_COMPATIBLE],[
AC_REQUIRE([PAC_PROG_F90_WORKS])
AC_CACHE_CHECK([whether Fortran 90 works with Fortran 77],
pac_cv_f90_and_f77,[
pac_cv_f90_and_f77="unknown"
rm -f conftest*
if test -z "$ac_ext_f90" ; then ac_ext_f90=$pac_cv_f90_ext ; fi
# Define the two language-specific steps
link_f90='${F90-f90} -o conftest${ac_exeext} $F90FLAGS $LDFLAGS conftest1.$ac_ext_f90 conftest2.o $LIBS 1>&AC_FD_CC'
compile_f77='${F77-f77} -c $FFLAGS conftest2.f 1>&AC_FD_CC'
# Create test programs
cat > conftest1.$ac_ext_f90 <<EOF
       program main
       integer a
       a = 1
       call t1(a)
       end
EOF
cat > conftest2.f <<EOF
       subroutine t1(b)
       integer b
       b = b + 1
       end
EOF
# compile the f77 program and link with the f90 program
# The reverse may not work because the Fortran 90 environment may
# expect to be in control (and to provide library files unknown to any other
# environment, even Fortran 77!)
if AC_TRY_EVAL(compile_f77) ; then
    if AC_TRY_EVAL(link_f90) && test -x conftest ; then
        pac_cv_f90_and_f77="yes"
    else 
        pac_cv_f90_and_f77="no"
    fi
    # Some versions of the Intel compiler produce these two files
    rm -f work.pc work.pcl
else
    pac_cv_f90_and_f77="yes"
fi])
# Perform the requested action based on whether the test succeeded
if test "$pac_cv_f90_and_f77" = yes ; then
    ifelse($1,,:,[$1])
else
    ifelse($2,,:,[$2])
fi
])
