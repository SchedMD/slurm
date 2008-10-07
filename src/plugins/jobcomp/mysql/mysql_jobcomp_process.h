/*****************************************************************************\
 *  mysql_jobcomp_process.h - functions the processing of
 *                               information from the mysql jobcomp
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

#ifndef _HAVE_MYSQL_JOBCOMP_PROCESS_H
#define _HAVE_MYSQL_JOBCOMP_PROCESS_H

#include "src/database/mysql_common.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_jobcomp.h"

#ifdef HAVE_MYSQL
extern MYSQL *jobcomp_mysql_db;
extern int jobcomp_db_init;

extern char *jobcomp_table;
/* This variable and the following enum are related so if you change
   the jobcomp_table_fields defined in mysql_jobcomp.c you must update
   this enum accordingly.
*/
extern storage_field_t jobcomp_table_fields[];
enum {
	JOBCOMP_REQ_JOBID,
	JOBCOMP_REQ_UID,
	JOBCOMP_REQ_USER_NAME,
	JOBCOMP_REQ_GID,
	JOBCOMP_REQ_GROUP_NAME,
	JOBCOMP_REQ_NAME,
	JOBCOMP_REQ_STATE,
	JOBCOMP_REQ_PARTITION,
	JOBCOMP_REQ_TIMELIMIT,
	JOBCOMP_REQ_STARTTIME,
	JOBCOMP_REQ_ENDTIME,
	JOBCOMP_REQ_NODELIST,
	JOBCOMP_REQ_NODECNT,
	JOBCOMP_REQ_CONNECTION,
	JOBCOMP_REQ_REBOOT,
	JOBCOMP_REQ_ROTATE,
	JOBCOMP_REQ_MAXPROCS,
	JOBCOMP_REQ_GEOMETRY,
	JOBCOMP_REQ_START,
	JOBCOMP_REQ_BLOCKID,
	JOBCOMP_REQ_COUNT		
};

extern List mysql_jobcomp_process_get_jobs(List selected_steps,
					   List selected_parts,
					   sacct_parameters_t *params);

extern void mysql_jobcomp_process_archive(List selected_parts,
					  sacct_parameters_t *params);
#endif

#endif
