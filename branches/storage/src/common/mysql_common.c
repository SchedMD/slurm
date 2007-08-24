/*****************************************************************************\
 *  mysql_common.c - common functions for the the mysql storage plugin.
 *****************************************************************************
 *
 *  Copyright (C) 2004-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission 
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and 
 *  distribute linked combinations including the two. You must obey the GNU 
 *  General Public License in all respects for all of the code used other than 
 *  OpenSSL. If you modify file(s) with this exception, you may extend this 
 *  exception to your version of the file(s), but you are not obligated to do 
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in 
 *  the program, then also delete it here.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 *  This file is patterned after jobcomp_linux.c, written by Morris Jette and
 *  Copyright (C) 2002 The Regents of the University of California.
\*****************************************************************************/

#include "mysql_common.h"

bool thread_safe = true;
pthread_mutex_t mysql_lock = PTHREAD_MUTEX_INITIALIZER;

#ifdef HAVE_MYSQL

static int _mysql_make_table_current(MYSQL *mysql_db, int storage_init, 
				     char *table_name,
				     storage_field_t *fields)
{
	char *query = NULL;
	int i = 0;

	while(fields[i].name) {
		query = xstrdup_printf("alter table %s modify %s %s",
				       table_name, fields[i].name,
				       fields[i].options);
		if(mysql_db_query(mysql_db, storage_init, query)) {
			info("adding column %s after %s", fields[i].name,
			     fields[i-1].name);
			xfree(query);
			query = xstrdup_printf(
				"alter table %s add %s %s after %s",
				table_name, fields[i].name,
				fields[i].options,
				fields[i-1].name);
			if(mysql_db_query(mysql_db, storage_init, query)) {
				xfree(query);
				return SLURM_ERROR;
			}

		}
		xfree(query);
		i++;
	}
	
	return SLURM_SUCCESS;
}

extern mysql_db_info_t *create_mysql_db_info()
{
	mysql_db_info_t *db_info = xmalloc(sizeof(mysql_db_info_t));
	db_info->port = slurm_get_jobacct_storage_port();
	if(!db_info->port) 
		db_info->port = 3306;
	db_info->host = slurm_get_jobacct_storage_host();	
	db_info->user = slurm_get_jobacct_storage_user();	
	db_info->pass = slurm_get_jobacct_storage_pass();	
	return db_info;
}

extern int *destroy_mysql_db_info(mysql_db_info_t *db_info)
{
	if(db_info) {
		xfree(db_info->host);
		xfree(db_info->user);
		xfree(db_info->pass);
		xfree(db_info);
	}
	return SLURM_SUCCESS;
}

extern int mysql_create_db(MYSQL *mysql_db, char *db_name,
			   mysql_db_info_t *db_info)
{
	char create_line[50];

	if(mysql_real_connect(mysql_db, db_info->host, db_info->user,
			      db_info->pass, NULL, db_info->port, NULL, 0)) {
		snprintf(create_line, sizeof(create_line),
			 "create database %s", db_name);
		if(mysql_query(mysql_db, create_line)) {
			fatal("mysql_real_query failed: %d %s\n%s",
			      mysql_errno(mysql_db),
			      mysql_error(mysql_db), create_line);
		}
	} else {
		info("Connection failed to host = %s "
		     "user = %s pass = %s port = %u",
		     db_info->host, db_info->user,
		     db_info->pass, db_info->port);
		fatal("mysql_real_connect failed: %d %s",
		      mysql_errno(mysql_db),
		      mysql_error(mysql_db));
	}
	return SLURM_SUCCESS;
}

extern int mysql_get_db_connection(MYSQL **mysql_db, char *db_name,
				   mysql_db_info_t *db_info,
				   int *storage_init)
{
	int rc = SLURM_SUCCESS;
	
	if(!(*mysql_db = mysql_init(*mysql_db)))
		fatal("mysql_init failed: %s", mysql_error(*mysql_db));
	else {
		while(!*storage_init) {
			if(!mysql_real_connect(*mysql_db, db_info->host,
					       db_info->user, db_info->pass,
					       db_name, db_info->port,
					       NULL, 0)) {
				if(mysql_errno(*mysql_db) == ER_BAD_DB_ERROR) {
					debug("Database %s not created.  "
					      "Creating", db_name);
					mysql_create_db(*mysql_db, db_name,
							db_info);
				} else {
					fatal("mysql_real_connect failed: "
					      "%d %s",
					      mysql_errno(*mysql_db),
					      mysql_error(*mysql_db));
				}
			} else {
				*storage_init = true;
			}
		}
	}
	return rc;
}

extern int mysql_db_query(MYSQL *mysql_db, int storage_init,  char *query)
{
	if(!storage_init)
		fatal("You haven't inited this storage yet.");

	if(mysql_query(mysql_db, query)) {
		error("mysql_query failed: %d %s\n%s",
		      mysql_errno(mysql_db),
		      mysql_error(mysql_db), query);
		return SLURM_ERROR;
	}

	return SLURM_SUCCESS;
}

extern MYSQL_RES *mysql_db_query_ret(MYSQL *mysql_db, int storage_init,
				     char *query)
{
	MYSQL_RES *result = NULL;
	
	if(mysql_db_query(mysql_db, storage_init, query) != SLURM_ERROR)  {
		result = mysql_store_result(mysql_db);
		if(!result && mysql_field_count(mysql_db)) {
			/* should have returned data */
			error("We should have gotten a result: %s", 
			      mysql_error(mysql_db));
		}
	}

	return result;
}

extern int mysql_insert_ret_id(MYSQL *mysql_db, int storage_init, char *query)
{
	int new_id = 0;
	
	if(mysql_db_query(mysql_db, storage_init, query) != SLURM_ERROR)  {
		new_id = mysql_insert_id(mysql_db);
		if(!new_id) {
			/* should have new id */
			error("We should have gotten a new id: %s", 
			      mysql_error(mysql_db));
		}
	}

	return new_id;
	
}

extern int mysql_db_create_table(MYSQL *mysql_db, int storage_init, 
				 char *table_name, storage_field_t *fields,
				 char *ending)
{
	char *query = NULL;
	char *tmp = NULL;
	char *next = NULL;
	int i = 0;
	storage_field_t *first_field = fields;
	
	query = xstrdup_printf("create table if not exists %s (", table_name);
	i=0;
	while(fields && fields->name) {
		next = xstrdup_printf(" %s %s",
				      fields->name, 
				      fields->options);
		if(i) 
			xstrcat(tmp, ",");
		xstrcat(tmp, next);
		xfree(next);
		fields++;
		i++;
	}
	xstrcat(query, tmp);
	xfree(tmp);
	xstrcat(query, ending);

	if(mysql_db_query(mysql_db, storage_init, query)
	   == SLURM_ERROR) {
		xfree(query);
		return SLURM_ERROR;
	}
	xfree(query);	
	
	return _mysql_make_table_current(mysql_db, storage_init, 
					 table_name, first_field);
}

#endif

