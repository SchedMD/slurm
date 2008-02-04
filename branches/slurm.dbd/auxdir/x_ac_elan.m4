##*****************************************************************************
## $Id$
##*****************************************************************************
#  AUTHOR:
#    Mark A. Grondona <mgrondona@llnl.gov>
#
#  SYNOPSIS:
#    AC_ELAN
#
#  DESCRIPTION:
#    Checks for whether Elan MPI may be supported either via libelan3
#     or libelanctrl. ELAN_LIBS is set to the libraries needed for
#     Elan modules.
#
#  WARNINGS:
#    This macro must be placed after AC_PROG_CC or equivalent.
##*****************************************************************************


AC_DEFUN([X_AC_ELAN],
[
   AC_CHECK_LIB([rmscall],  
	        [rms_prgcreate], 
	        [ac_elan_have_rmscall=yes; ELAN_LIBS="-lrmscall"])

   if test "$ac_elan_have_rmscall" != "yes" ; then
       AC_MSG_NOTICE([Cannot support QsNet without librmscall])        
   fi

   AC_CHECK_LIB([elan3], [elan3_create],  
	        [ac_elan_have_elan3=yes],
	        [ac_elan_noelan3=1])

   AC_CHECK_LIB([elanctrl], [elanctrl_open], 
	        [ac_elan_have_elanctrl=yes], 
	        [ac_elan_noelanctrl=1])

   if test "$ac_elan_have_elan3" = "yes"; then
      AC_DEFINE(HAVE_LIBELAN3, 1, [define if you have libelan3.])
      ELAN_LIBS="$ELAN_LIBS -lelan3"
      test "$ac_elan_have_rmscall" = "yes" && ac_have_elan="yes"
   elif test "$ac_elan_have_elanctrl" = "yes"; then
      AC_DEFINE(HAVE_LIBELANCTRL, 1, [define if you have libelanctrl.])
      ELAN_LIBS="$ELAN_LIBS -lelanctrl"
      test "$ac_elan_have_rmscall" = "yes" && ac_have_elan="yes"
   else
      AC_MSG_NOTICE([Cannot support QsNet without libelan3 or libelanctrl!])
   fi

   if test "$ac_have_elan" = yes; then
     AC_CHECK_LIB([elanhosts], [elanhost_config_create],
                  [ac_elan_have_elanhosts=yes], [])

     if test "$ac_elan_have_elanhosts" = "yes"; then
        AC_DEFINE(HAVE_LIBELANHOSTS, 1, [define if you have libelanhosts.])
        ELAN_LIBS="$ELAN_LIBS -lelanhosts"
     else
        ac_have_elan="no"
        AC_MSG_NOTICE([Cannot build QsNet modules without libelanhosts])
     fi
   fi

   AC_SUBST(ELAN_LIBS)
])
