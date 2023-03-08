/*****************************************************************************\
 *  scancel.h - definitions for scancel data structures and functions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Copyright (C) 2010-2015 SchedMD LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette<jette1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef _HAVE_SCANCEL_H
#define _HAVE_SCANCEL_H

#include "src/common/slurmdb_defs.h"

typedef struct scancel_options {
	char *account;		/* --account=n, -a		*/
	bool batch;		/* --batch, -b			*/
	char *sibling;		/* --sibling=<sib_name>		*/
	bool ctld;		/* --ctld			*/
	List clusters;          /* --cluster=cluster_name -Mcluster-name */
	bool cron;		/* --cron */
	bool full;		/* --full, -f			*/
	bool hurry;		/* --hurry, -H			*/
	bool interactive;	/* --interactive, -i		*/
	char *job_name;		/* --name=n, -nn		*/
	char *partition;	/* --partition=n, -pn		*/
	char *qos;		/* --qos=n, -qn			*/
	char *reservation;	/* --reservation=n, -Rn		*/
	uint16_t signal;	/* --signal=n, -sn		*/
	uint32_t state;		/* --state=n, -tn		*/
	uid_t user_id;		/* derived from user_name	*/
	char *user_name;	/* --user=n, -un		*/
	int verbose;		/* --verbose, -v		*/
	char *wckey;		/* --wckey			*/
	char *nodelist;		/* --nodelist, -w		*/

	char **job_list;        /* job ID input, NULL termated
				 * Expanded in to arrays below	*/

	uint16_t job_cnt;	/* count of job_id's specified	*/
	uint32_t *job_id;	/* list of job ID's		*/
	uint32_t *array_id;	/* list of job array task IDs	*/
	uint32_t *step_id;	/* list of job step ID's	*/
	bool *job_found;	/* Set if the job record is found */
	bool *job_pend;		/* Set fi job is pending	*/
} opt_t;

extern opt_t opt;

/* process options:
 * 1. set defaults
 * 2. update options with env vars
 * 3. update options with commandline args
 * 4. perform some verification that options are reasonable
 */
extern int initialize_and_process_args(int argc, char **argv);

/*
 * No job filtering options were specified (e.g. by user or state), only the
 * job ids is on the command line.
 */
extern bool has_default_opt(void);
extern bool has_job_steps(void);
#endif	/* _HAVE_SCANCEL_H */
