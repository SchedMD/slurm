/*****************************************************************************\
 *  scancel.h - definitions for scancel data structures and functions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette<jette1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
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
\*****************************************************************************/

#ifndef _HAVE_SCANCEL_H
#define _HAVE_SCANCEL_H

#if HAVE_CONFIG_H
#  include "config.h"
#endif

typedef struct scancel_options {
	char *account;		/* --account=n, -a		*/
	bool batch;		/* --batch, -b			*/
	bool ctld;		/* --ctld			*/
	bool interactive;	/* --interactive, -i		*/
	char *job_name;		/* --name=n, -nn		*/
	char *partition;	/* --partition=n, -pn		*/
	char *qos;		/* --qos=n, -qn			*/
	uint16_t signal;	/* --signal=n, -sn		*/
	uint16_t state;		/* --state=n, -tn		*/
	uid_t user_id;		/* derived from user_name	*/
	char *user_name;	/* --user=n, -un		*/
	int verbose;		/* --verbose, -v		*/

	uint16_t job_cnt;	/* count of job_id's specified	*/
	uint32_t *job_id;	/* list of job_id's		*/
	uint32_t *step_id;	/* list of job step id's	*/
	char *wckey;		/* --wckey			*/
	char *nodelist;		/* --nodelist, -w		*/
} opt_t;

opt_t opt;

/* process options:
 * 1. set defaults
 * 2. update options with env vars
 * 3. update options with commandline args
 * 4. perform some verification that options are reasonable
 */
int initialize_and_process_args(int argc, char *argv[]);

#endif	/* _HAVE_SCANCEL_H */
