# $NetBSD$

AC_DEFUN([X_AC_DLFCN], [
  AC_MSG_CHECKING([library containing dlopen])
  AC_CHECK_LIB([], [dlopen], [ac_have_dlopen=yes; DL_LIBS=""],
    [AC_CHECK_LIB([dl], [dlopen], [ac_have_dlopen=yes; DL_LIBS="-ldl"],
      [AC_CHECK_LIB([svdl], [dlopen], [ac_have_dlopen=yes; DL_LIBS="-lsvdl"])])])

  AC_SUBST(DL_LIBS)
])
