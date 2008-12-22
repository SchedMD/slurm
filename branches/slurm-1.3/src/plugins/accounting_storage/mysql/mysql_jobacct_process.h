/*****************************************************************************\
 *  mysql_jobacct_process.h - functions the processing of
 *                               information from the mysql jobacct
 *                               storage.
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

#ifndef _HAVE_MYSQL_JOBACCT_PROCESS_H
#define _HAVE_MYSQL_JOBACCT_PROCESS_H

#include <sys/types.h>
#include <pwd.h>
#include <stdlib.h>
#include "src/common/assoc_mgr.h"
#include "src/common/jobacct_common.h"
#include "src/slurmdbd/read_config.h"
#include "src/slurmctld/slurmctld.h"
#include "src/database/mysql_common.h"
#include "src/common/slurm_accounting_storage.h"

#ifdef HAVE_MYSQL

//extern int acct_db_init;

extern char *assoc_table;
extern char *job_table;
extern char *step_table;
extern char *suspend_table;

extern int setup_job_cond_limits(acct_job_cond_t *job_cond, char **extra);

extern List mysql_jobacct_process_get_jobs(mysql_conn_t *mysql_conn, uid_t uid,
					   acct_job_cond_t *job_cond);

extern int mysql_jobacct_process_archive(mysql_conn_t *mysql_conn,
					 acct_archive_cond_t *arch_cond);

extern int mysql_jobacct_process_archive_load(mysql_conn_t *mysql_conn,
					      acct_archive_rec_t *arch_rec);

#endif

#endif
