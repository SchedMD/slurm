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
	AC_CHECK_HEADERS(mysql/mysql.h,
			 [has_mysql_header="true"],
			 [has_mysql_header="false"])
   	if test "$has_mysql_header" = "true"; then	
      		AC_CHECK_LIB(mysqlclient, mysql_init, [has_mysql_lib="true"], [has_mysql_lib="false"])
      		if test "$has_mysql_lib" = "true"; then
               		MYSQL_LIBS=" -lmysqlclient -lz"
        		save_LIBS="$LIBS"
        		LIBS="$MYSQL_LIBS $save_LIBS"
        		AC_TRY_LINK([
          			#include <mysql/mysql.h>
        			],[
          			int main()
          			{
					MYSQL mysql;
            				(void) mysql_init(&mysql);
					(void) mysql_close(&mysql);
            		        }
        		], [ac_have_mysql="yes"], [ac_have_mysql="no"])
			LIBS="$save_LIBS"
        		if test "$ac_have_mysql" == "yes"; then
            			AC_MSG_RESULT([MySQL test program built properly.])
            			AC_SUBST(MYSQL_LIBS)
				AC_DEFINE(HAVE_MYSQL, 1, [Define to 1 if using MySQL libaries])
	        	else
        			AC_MSG_WARN([*** MySQL test program execution failed.])
			fi        	
      		else
        		AC_MSG_WARN(mysqlclient lib not found: mysql support not available)
      		fi
   	else
      		AC_MSG_WARN(mysql/mysql.h header not found: mysql support not available)
   	fi 

	#Check for PostgreSQL
	ac_have_pgsql="no"
	### Check for pkg-config program
    	AC_PATH_PROG(HAVEPGCONFIG, pg_config, $PATH)
	if test -z "$HAVEPGCONFIG"; then
        	AC_MSG_WARN([*** pg_config not found. Evidently no PostgreSQL install on system.])
	else
		PGSQL_INCLUDEDIR=`$HAVEPGCONFIG --includedir`
		PGSQL_LIBDIR=`$HAVEPGCONFIG --libdir`
		PGSQL_CFLAGS="-I$PGSQL_INCLUDEDIR -L$PGSQL_LIBDIR" 
		save_CFLAGS="$CFLAGS"
        	CFLAGS="$PGSQL_CFLAGS $save_CFLAGS"
                
		AC_CHECK_HEADERS($PGSQL_INCLUDEDIR/libpq-fe.h,
			 [has_pgsql_header="true"],
			 [has_pgsql_header="false"])
	   	if test "$has_pgsql_header" = "true"; then	
			AC_CHECK_LIB(pq, PQconnectdb, [has_pgsql_lib="true"], [has_pgsql_lib="false"])
			if test "$has_pgsql_lib" = "true"; then
       				PGSQL_LIBS=" -lpq"
	       			save_LIBS="$LIBS"
        			LIBS="$PGSQL_LIBS $save_LIBS"
       				AC_TRY_LINK([
      					#include <libpq-fe.h>
      					],[
          				int main()
       	  				{
						PGconn     *conn;
						conn = PQconnectdb("dbname = postgres");
       						(void) PQfinish(conn);
            			        }
        			], [ac_have_pgsql="yes"], [ac_have_pgsql="no"])
				LIBS="$save_LIBS"
       				if test "$ac_have_pgsql" == "yes"; then
    					AC_MSG_RESULT([PostgreSQL test program built properly.])
            				AC_SUBST(PGSQL_LIBS)
					AC_SUBST(PGSQL_CFLAGS)
					AC_DEFINE(HAVE_PGSQL, 1, [Define to 1 if using PostgreSQL libaries])
		        	else	
       					AC_MSG_WARN([*** PostgreSQL test program execution failed.])
				fi        	
      			else
        			AC_MSG_WARN(libpq not found: PostgreSQL support not available)
			fi			
   		else	
			AC_MSG_WARN(libpq-fe.h header not found: PostgreSQL support not available)
  		fi 
		CFLAGS="$save_CFLAGS"
       	fi
])
