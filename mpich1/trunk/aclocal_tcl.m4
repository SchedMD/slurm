dnl
dnl Define the test to look for wish (the tcl/tk windowing shell)
dnl This is under development and is derived from the FINDX command
dnl FIND_WISH looks in the path and in places that we've found wish.
dnl It sets "wishloc" to the location that it found, or to the
dnl empty string if it can't find wish.
dnl Note that we need tk version 3.3 or later, so we don't check for the 
dnl earlier versions
dnl
dnl Some systems are now (probably wisely, given the number of 
dnl incompatibilities) using names like "wish4.2" or "wish-3.6" and the like.
define([PAC_FIND_WISH],[wishloc=""
AC_MSG_CHECKING([for wish])
# Look for wish in the path
if test -n "$TCL73TK36_DIR" ; then
    if test -d $TCL73TK36_DIR/bin
	wishalt_dir=$TCL73TK36_DIR
    fi
fi
IFS="${IFS= 	}"; saveifs="$IFS"; IFS="${IFS}:"
for dir in $wishalt_dir $PATH ; do 
    if test -x $dir/wish ; then
	wishloc=$dir/wish
        break
    elif test -x $dir/tcl7.3-tk3.6/bin/wish ; then
	wishloc=$dir/tcl7.3-tk3.6/bin/wish
	break
    elif test -x $dir/tcl7.4-tk4.0/bin/wish ; then
        wishloc=$dir/tcl7.4-tk4.0/bin/wish
	break
    else
	for file in $dir/wish3.? $dir/wish-3.? $dir/wish4.? $dir/wish* ; do
	    if test -x $file ; then
		wishloc=$file
		break
	    fi
	if test -n "$wishloc" ; then break ; fi
	done
    fi
done
IFS="$saveifs"
# Look for wish elsewhere
if test -z "$wishloc" ; then
for dir in \
    /usr/local/bin \
    /usr/local/tk-3.3/bin \
    /usr/local/tcl7.3-tk3.6/bin \
    /usr/local/tcl7.0/bin \
    /usr/local/tcl7.0-tk3.3/bin \
    /usr/contrib/bin \
    /usr/contrib/tk3.6/bin \
    /usr/contrib/tcl7.3-tk3.6/bin \
    /usr/contrib/tk3.3/bin \
    /usr/contrib/tcl7.0-tk3.3/bin \
    $HOME/tcl/bin \
    $HOME/tcl7.3/bin \
    /opt/Tcl/bin \
    /opt/bin \
    /usr/unsupported \
    /usr/unsupported/bin \
    /usr/bin \
    /bin \
    /usr/sgitcl \
    /usr/pkg \
    /local/encap/tcl-7.1/bin ; do
    if test -x $dir/wish ; then
	wishloc=$dir/wish
        break
    fi
done
fi
if test -n "$wishloc" ; then 
  AC_MSG_RESULT(found $wishloc)
else
  AC_MSG_RESULT(no)
fi])dnl
dnl
dnl We can use wish to find tcl and tk libraries with
dnl puts stdout $tk_library
dnl tclsh can be used with 
dnl puts stdout $tcl_library
dnl
dnl Also sets FOUND_TK36 to 1 if found, sets FOUND_TK to version if a version
dnl after 3 is found
dnl
define([PAC_FIND_TCL],[
# Look for Tcl.  Prefer the TCL73TK36_DIR version if it exists
if test -z "$TCL_DIR" ; then
# See if tclsh is in the path
# If there is a tclsh, it MAY provide tk.
PAC_PROGRAM_CHECK(TCLSH,tclsh,1,,tclshloc)
AC_MSG_CHECKING([for Tcl])
if test -n "$tclshloc" ; then
    cat >conftest <<EOF
puts stdout [\$]tcl_library
EOF
    tcllibloc=`$tclshloc conftest 2>/dev/null`
    # The tcllibloc is the directory containing the .tcl files.  
    # The .a files may be one directory up
    if test -n "$tcllibloc" ; then
        tcllibloc=`dirname $tcllibloc`
        # and the lib directory one above that
        tcllibs="$tcllibloc `dirname $tcllibloc`"
    fi
    rm -f conftest   
fi
foundversion=""
# At ANL, the software is in a common tree; we need to pick the
# correct architecture
# ? is host correct?
if test "$host" = "irix" ; then
    archdir="irix-6"
elif test "$host" = "linux" ; then
    archdir="linux"
elif test "$host" = "solaris" ; then
    archdir="solaris-2" 
elif test "$host" = "sun4" ; then
    archdir="sun4"
else
    archdir="."
fi
#
for dir in $TCL73TK36_DIR $tcllibs \
    /usr \
    /usr/local \
    /usr/local/tcl7.5 \
    /usr/local/tcl7.3 \
    /usr/local/tcl7.3-tk3.6 \
    /usr/local/tcl7.0 \
    /usr/local/tcl7.0-tk3.3 \
    /usr/local/tcl7.* \
    /usr/contrib \
    /usr/contrib/tk3.6 \
    /usr/contrib/tcl7.3-tk3.6 \
    /usr/contrib/tk3.3 \
    /usr/contrib/tcl7.0-tk3.3 \
    $HOME/tcl \
    $HOME/tcl7.3 \
    $HOME/tcl7.5 \
    /opt/Tcl \
    /opt/local \
    /opt/local/tcl7.5 \
    /opt/local/tcl7.* \
    /usr/bin \
    /Tools/tcl \
    /usr/sgitcl \
    /usr/pgk \
    /software/$archdir/apps/packages/tcl-7* \
    /local/encap/tcl-7.1 ; do
    # In some cases, the tck/tk name comes *after* the include.
    for fileloc in $dir/include $dir/include/tcl* ; do
       if test -r $fileloc/tcl.h ; then 
	    # Check for correct version
   	    changequote(,)
 	    tclversion=`grep 'TCL_MAJOR_VERSION' $fileloc/tcl.h | \
		    sed -e 's/^.*TCL_MAJOR_VERSION[^0-9]*\([0-9]*\).*$/\1/'`
	    changequote([,])
	    if test "$tclversion" != "7" ; then
	        # Skip if it is the wrong version
	        foundversion=$tclversion
	        continue
	    fi
            if test -r $dir/lib/libtcl.a -o -r $dir/lib/libtcl.so ; then
	        TCL_DIR=$dir
	        break
            fi
	    for libdir in $dir/lib $dir/lib/tcl* ; do
                if test -r $libdir/libtcl.a -o -r $libdir/libtcl.so ; then
	            # Not used yet
 	            TCL_LIB_DIR=$libdir
	            break
                fi
            done
	    for file in $dir/lib/libtcl*.a $dir/lib/tcl*/libtcl*.a ; do
	        if test -r $file ; then 
                    TCL_DIR_W="$TCL_DIR_W $file"
	        fi
	    done
        fi
	if test -n "$TCL_DIR" ; then break ; fi
    done
    if test -n "$TCL_DIR" ; then break ; fi
done
fi
if test -n "$TCL_DIR" ; then 
  AC_MSG_RESULT(found $TCL_DIR/include/tcl.h and $TCL_DIR/lib/libtcl)
else
  if test -n "$TCL_DIR_W" ; then
    AC_MSG_RESULT(found $TCL_DIR_W but need libtcl.a)
  else
    if test -z "$foundversion" ; then
        AC_MSG_RESULT(no)
    else
	AC_MSG_RESULT(no: found version $foundversion but need version 7)
    fi
  fi
fi
# Look for Tk (look in tcl dir if the code is nowhere else)
if test -z "$TK_DIR" ; then
AC_MSG_CHECKING([for Tk])
if test -n "$wishloc" ; then
    # Originally, we tried to run wish and get the tkversion from it
    # unfortunately, this sometimes hung, probably waiting to get a display
#    cat >conftest <<EOF
#puts stdout [\$]tk_library
#exit
#EOF
#    tklibloc=`$wishloc -file conftest 2>/dev/null`
    tklibloc=`strings $wishloc | grep 'lib/tk'`
    # The tklibloc is the directory containing the .tclk files.  
    # The .a files may be one directory up
    # There may be multiple lines in tklibloc now.  Make sure that we only
    # test actual directories
    if test -n "$tklibloc" ; then
	for tkdirname in $tklibloc ; do
    	    if test -d $tkdirname ; then
                tkdirname=`dirname $tkdirname`
                # and the lib directory one above that
                tklibs="$tkdirname `dirname $tkdirname`"
	    fi
	done
    fi
    rm -f conftest   
fi
foundversion=""
TK_UPDIR=""
TK_UPVERSION=""
for dir in $TCL73TK36_DIR $tklibs \
    /usr \
    /usr/local \
    /usr/local/tk3.6 \
    /usr/local/tcl7.3-tk3.6 \
    /usr/local/tk3.3 \
    /usr/local/tcl7.0-tk3.3 \
    /usr/contrib \
    /usr/contrib/tk3.6 \
    /usr/contrib/tcl7.3-tk3.6 \
    /usr/contrib/tk3.3 \
    /usr/contrib/tcl7.0-tk3.3 \
    $HOME/tcl \
    $HOME/tcl7.3 \
    /opt/Tcl \
    /opt/local \
    /opt/local/tk3.6 \
    /usr/bin \
    /Tools/tk \
    /usr/sgitcl \
    /usr/pkg \
    /software/$archdir/apps/packages/tcl* \
    /local/encap/tk-3.4 $TCL_DIR ; do
    if test -r $dir/include/tk.h ; then 
	# Check for correct version
	changequote(,)
	tkversion=`grep 'TK_MAJOR_VERSION' $dir/include/tk.h | \
		sed -e 's/^.*TK_MAJOR_VERSION[^0-9]*\([0-9]*\).*$/\1/'`
	tk2version=`grep 'TK_MINOR_VERSION' $dir/include/tk.h | \
		sed -e 's/^.*TK_MINOR_VERSION[^0-9]*\([0-9]*\).*$/\1/'`
	changequote([,])
	tkupversion="$tkversion.$tk2version"
	if test "$tkversion" != "3" ; then
            # Try for later versions for UPSHOT
            if test -z "$TK_UPDIR" -a $tkversion -ge 3 ; then
                TK_UPDIR=$dir
	        TK_UPVERSION=$tkversion
                if test -r $TK_UPDIR/lib/libtk.a -o \
		        -r $TK_UPDIR/lib/libtk.so ; then
	            continue
	        elif test -r $TK_UPDIR/lib/libtk$tkupversion.a -o \
			  -r $TK_UPDIR/lib/libtk$tkupversion.so ; then
                    continue
	        else
	            TK_UPDIR=""
	            TK_UPVERSION=""
                fi
            fi
	    # Skip if it is the wrong version
	    foundversion=$tkversion
	    continue
	fi
        if test -r $dir/lib/libtk.a -o -r $dir/lib/libtk.so ; then
	    TK_DIR=$dir
	    break
	fi
	for file in $dir/lib/libtk*.a ; do
	    if test -r $file ; then 
                TK_DIR_W="$TK_DIR_W $file"
	    fi
	done
    fi
done
fi
if test -n "$TK_DIR" ; then 
  AC_MSG_RESULT(found $TK_DIR/include/tk.h and $TK_DIR/lib/libtk)
  FOUND_TK36=1
else
  FOUND_TK36=0
  if test -n "$TK_DIR_W" ; then
    AC_MSG_RESULT(found $TK_DIR_W but need libtk.a (and version 3.6) )
  else
    if test -z "$foundversion" ; then
        AC_MSG_RESULT(no)
    else
	AC_MSG_RESULT(no: found version $foundversion but need 3.6)
        FOUND_TK=$TK_UPVERSION
    fi
  fi
fi
])dnl
