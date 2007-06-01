dnl
dnl NOT YET READY
dnl 
dnl This contains some helper macros to help handle complex enable values
dnl such as
dnl  --enable-g=g,mem=trace
dnl and
dnl  --enable-g=all 
dnl
dnl for --enable-name, use 
dnl PAC_ARG_ENABLE_PARSE(name) 
dnl
dnl Not done:
dnl Provide a list of allowed values (to allow checking for missed values)
dnl Provide an option to automatically define a value.  We currently
dnl set HAVE_ENABLE_NAME_SUBNAME=value.  For example
dnl --enable-g=g,mem=trace 
dnl would set the variables
dnl HAVE_ENABLE_G_G=yes
dnl HAVE_ENABLE_G_MEM=trace
dnl
AC_DEFUN(PAC_ARG_ENABLE_PARSE,[
    eval ac_e_parse=${enable}_$1
    save_IFS="$IFS"
    IFS=","
    for ac_opt in $ac_e_parse ; do
	dnl handle ac_opt has a value
	case "$ac_opt" in
        *=*) ac_optname=`echo "$ac_opt" | sed 's/=.*//'` 
             ac_optarg=`echo "$ac_opt" | sed 's/[-_a-zA-Z0-9]*=//'` ;;
	*)   ac_optname=$ac_opt
	     ac_optarg=yes ;;
	esac
        dnl Still need to upcase the name
	changequote(<<,>>)dnl
	define(<<AC_VAR_NAME>>, translit($1,[a-z],[A-Z]))dnl
	ac_opt_name_caps=`echo $ac_opt_name | tr a-z A-Z`
	changequote([,])dnl
	ac_opt_name=HAVE_ENABLE_[]AC_VAR_NAME[]_${ac_opt_name_caps}
	eval $ac_opt_name=$ac_optarg
    done
])
