/*****************************************************************************\
 *  iaa.c - Library for managing a switch on a Cray system.
 *****************************************************************************
 *  Copyright (C) 2014 SchedMD LLC
 *  Copyright 2014 Cray Inc. All Rights Reserved.
 *  Written by David Gloe
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://slurm.schedmd.com/>.
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

#include "switch_cray.h"

#if defined(HAVE_NATIVE_CRAY_GA) || defined(HAVE_CRAY_NETWORK)

#include "src/common/xstring.h"

// IAA file format
#define CRAY_IAA_FILE "/tmp/cray_iaa_info.%"PRIu64

/*
 * Write the IAA file and set the filename in the job's environment
 */
int write_iaa_file(stepd_step_rec_t *job, slurm_cray_jobinfo_t *sw_job,
		   int *ptags, int num_ptags, alpsc_peInfo_t *alpsc_pe_info)
{
	char *fname = xstrdup_printf(CRAY_IAA_FILE, sw_job->apid);
	int rc, ret = SLURM_ERROR;
	char *err_msg = NULL;

	do {
		// Write the file
		rc = alpsc_write_iaa_info(&err_msg, fname, sw_job->num_cookies,
					  (const char **)sw_job->cookies,
					  num_ptags, ptags, alpsc_pe_info);
		ALPSC_CN_DEBUG("alpsc_write_iaa_info");
		if (rc != 1) {
			break;
		}

		// chown the file to the job user
		rc = chown(fname, job->uid, job->gid);
		if (rc == -1) {
			CRAY_ERR("chown(%s, %d, %d) failed: %m",
				 fname, (int)job->uid, (int)job->gid);
			break;
		}

		// Write the environment variable
		rc = env_array_overwrite(&job->env, CRAY_IAA_INFO_FILE_ENV,
					 fname);
		if (rc == 0) {
			CRAY_ERR("Failed to set env variable %s",
				 CRAY_IAA_INFO_FILE_ENV);
			break;
		}
		ret = SLURM_SUCCESS;
	} while(0);

	xfree(fname);
	return ret;
}

/*
 * Unlink the IAA file
 */
void unlink_iaa_file(slurm_cray_jobinfo_t *job)
{
	char *fname = xstrdup_printf(CRAY_IAA_FILE, job->apid);
	unlink(fname);
	xfree(fname);
}

#endif
