/*****************************************************************************\
 *  info_burst_buffer.c - Burst buffer information functions for scontrol.
 *****************************************************************************
 *  Copyright (C) 2014-2017 SchedMD LLC <https://www.schedmd.com/>.
 *  Written by Morris Jette <jette@schedmd.com>
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

#include "scontrol.h"

/*
 * scontrol_print_bbstat - Print burst buffer status information to stdout
 */
extern void scontrol_print_bbstat(int argc, char **argv)
{
	char *stat_resp = NULL;
	int error_code;

	error_code = slurm_load_burst_buffer_stat(argc, argv, &stat_resp);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror("slurm_load_burst_buffer_stat error");
		return;
	}
	fprintf(stdout, "%s", stat_resp);
	xfree(stat_resp);
}

/*
 * scontrol_print_burst_buffer - print all burst_buffer information to stdout
 */
extern void scontrol_print_burst_buffer(void)
{
	int error_code, i, verbosity = 0;
	burst_buffer_info_msg_t *burst_buffer_info_ptr = NULL;
	burst_buffer_info_t *burst_buffer_ptr = NULL;

	error_code = slurm_load_burst_buffer_info(&burst_buffer_info_ptr);
	if (error_code) {
		exit_code = 1;
		if (quiet_flag != 1)
			slurm_perror ("slurm_load_burst_buffer_info error");
		return;
	}

	if (quiet_flag == -1)
		verbosity = 1;
	burst_buffer_ptr = burst_buffer_info_ptr->burst_buffer_array;
	for (i = 0; i < burst_buffer_info_ptr->record_count; i++) {
		slurm_print_burst_buffer_record(stdout, &burst_buffer_ptr[i],
						one_liner, verbosity);
	}

	slurm_free_burst_buffer_info_msg(burst_buffer_info_ptr);
}
