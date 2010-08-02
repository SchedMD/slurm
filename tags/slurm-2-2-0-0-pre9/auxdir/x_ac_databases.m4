##*****************************************************************************
## $Id: x_ac_databases.m4 5401 2005-09-22 01:56:49Z da $
##*****************************************************************************
#  AUTHOR:
#    Danny Auble  <da@llnl.gov>
#
#  SYNOPSIS:
#    X_AC_DATABASES
#
#  DESCRIPTION:
#    Test for Different Database apis. If found define appropriate ENVs. 
##*****************************************************************************

AC_DEFUN([X_AC_DATABASES],
[
	#Check for MySQL
	ac_have_mysql="no"
	_x_ac_mysql_bin="no"
	### Check for mysql_config program
	AC_ARG_WITH(
		[mysql_config],
		AS_HELP_STRING(--with-mysql_config=PATH, 
			Specify path to mysql_config binary),
		[_x_ac_mysql_bin="$withval"])
	
	if test x$_x_ac_mysql_bin = xno; then
    		AC_PATH_PROG(HAVEMYSQLCONFIG, mysql_config, no)
	else
   		AC_PATH_PROG(HAVEMYSQLCONFIG, mysql_config, no, $_x_ac_mysql_bin)
	fi

	if test x$HAVEMYSQLCONFIG = xno; then
        	AC_MSG_WARN([*** mysql_config not found. Evidently no MySQL install on system.])
	else
		# check for mysql-5.0.0+
		mysql_config_major_version=`$HAVEMYSQLCONFIG --version | \
			sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[a-zA-Z0-9]]*\)/\1/'`
    		mysql_config_minor_version=`$HAVEMYSQLCONFIG --version | \
			sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[a-zA-Z0-9]]*\)/\2/'`
    		mysql_config_micro_version=`$HAVEMYSQLCONFIG --version | \
			sed 's/\([[0-9]]*\).\([[0-9]]*\).\([[a-zA-Z0-9]]*\)/\3/'`

		if test $mysql_config_major_version -lt 5; then
	   		AC_MSG_WARN([*** mysql-$mysql_config_major_version.$mysql_config_minor_version.$mysql_config_micro_version available, we need >= mysql-5.0.0 installed for the mysql interface.])
            		ac_have_mysql="no"
		else 
		# mysql_config puts -I on the front of the dir.  We don't 
		# want that so we remove it.
			MYSQL_CFLAGS=`$HAVEMYSQLCONFIG --cflags`
			MYSQL_LIBS=`$HAVEMYSQLCONFIG --libs_r`
			save_CFLAGS="$CFLAGS"
			save_LIBS="$LIBS"
       			CFLAGS="$MYSQL_CFLAGS $save_CFLAGS"
			LIBS="$MYSQL_LIBS $save_LIBS"
			AC_TRY_LINK([#include <mysql.h>],[
          				int main()
          				{
						MYSQL mysql;
            					(void) mysql_init(&mysql);
						(void) mysql_close(&mysql);
            				}
        				],
				[ac_have_mysql="yes"],
				[ac_have_mysql="no"])
			CFLAGS="$save_CFLAGS"
			LIBS="$save_LIBS"
       			if test "$ac_have_mysql" == "yes"; then
            			AC_MSG_RESULT([MySQL test program built properly.])
            			AC_SUBST(MYSQL_LIBS)
				AC_SUBST(MYSQL_CFLAGS)
				AC_DEFINE(HAVE_MYSQL, 1, [Define to 1 if using MySQL libaries])
			else
				MYSQL_CFLAGS=`$HAVEMYSQLCONFIG --cflags`
				MYSQL_LIBS=`$HAVEMYSQLCONFIG --libs`
				save_CFLAGS="$CFLAGS"
				save_LIBS="$LIBS"
       				CFLAGS="$MYSQL_CFLAGS $save_CFLAGS"
				LIBS="$MYSQL_LIBS $save_LIBS"
				AC_TRY_LINK([#include <mysql.h>],[
          					int main()
          					{
							MYSQL mysql;
            						(void) mysql_init(&mysql);
							(void) mysql_close(&mysql);
            					}
        					],
					[ac_have_mysql="yes"],
					[ac_have_mysql="no"])
				CFLAGS="$save_CFLAGS"
				LIBS="$save_LIBS"
				
    				if test "$ac_have_mysql" == "yes"; then
            				AC_MSG_RESULT([MySQL (non-threaded) test program built properly.])
            				AC_SUBST(MYSQL_LIBS)
					AC_SUBST(MYSQL_CFLAGS)
					AC_DEFINE(MYSQL_NOT_THREAD_SAFE, 1, [Define to 1 if with non thread-safe code])
					AC_DEFINE(HAVE_MYSQL, 1, [Define to 1 if using MySQL libaries])
				else
					MYSQL_CFLAGS=""
					MYSQL_LIBS=""
          				AC_MSG_WARN([*** MySQL test program execution failed.])
				fi        	
			fi
		fi
      	fi
	AM_CONDITIONAL(WITH_MYSQL, test x"$ac_have_mysql" == x"yes")

	#Check for PostgreSQL
	ac_have_postgres="no"
	_x_ac_pgsql_bin="no"
	### Check for pg_config program
 	AC_ARG_WITH(
		[pg_config],
		AS_HELP_STRING(--with-pg_config=PATH, 
			Specify path to pg_config binary),
		[_x_ac_pgsql_bin="$withval"])

	if test x$_x_ac_pgsql_bin = xno; then
    		AC_PATH_PROG(HAVEPGCONFIG, pg_config, no)
	else
  		AC_PATH_PROG(HAVEPGCONFIG, pg_config, no, $_x_ac_pgsql_bin)
	fi
  
	if test x$HAVEPGCONFIG = xno; then
        	AC_MSG_WARN([*** pg_config not found. Evidently no PostgreSQL install on system.])
	else
		PGSQL_INCLUDEDIR=`$HAVEPGCONFIG --includedir`
		PGSQL_LIBDIR=`$HAVEPGCONFIG --libdir`
		PGSQL_CFLAGS="-I$PGSQL_INCLUDEDIR -L$PGSQL_LIBDIR" 
		save_CFLAGS="$CFLAGS"
        	CFLAGS="$PGSQL_CFLAGS $save_CFLAGS"
                
		PGSQL_LIBS=" -lpq"
	       	save_LIBS="$LIBS"
        	LIBS="$PGSQL_LIBS $save_LIBS"
       		AC_TRY_LINK([#include <libpq-fe.h>],[
          			int main()
       	  			{
					PGconn     *conn;
					conn = PQconnectdb("dbname = postgres");
       					(void) PQfinish(conn);
            			}
        			],
			[ac_have_pgsql="yes"],
			[ac_have_pgsql="no"])
		LIBS="$save_LIBS"
       		CFLAGS="$save_CFLAGS"
		if test "$ac_have_pgsql" == "yes"; then
    			AC_MSG_RESULT([PostgreSQL test program built properly.])
            		AC_SUBST(PGSQL_LIBS)
			AC_SUBST(PGSQL_CFLAGS)
			AC_DEFINE(HAVE_PGSQL, 1, [Define to 1 if using PostgreSQL libaries])
		else	
			PGSQL_CFLAGS=""
			PGSQL_LIBS=""
       			AC_MSG_WARN([*** PostgreSQL test program execution failed.])
		fi        	
      	fi
	AM_CONDITIONAL(WITH_PGSQL, test x"$ac_have_pgsql" == x"yes")

])
