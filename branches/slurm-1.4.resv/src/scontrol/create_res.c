/*****************************************************************************\
 *  create_res.c - reservation creation function for scontrol.
 *****************************************************************************
 *  Copyright (C) 2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by David Bremer <dbremer@llnl.gov>
 *  LLNL-CODE-402394.
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
\*****************************************************************************/

#include "src/scontrol/scontrol.h"
#include "src/slurmctld/reservation.h"


extern int
scontrol_create_res(int argc, char *argv[])
{
	reserve_request_msg_t   resv_msg;
	int i;
	int duration = -3;   /* -1 == INFINITE, -2 == error, -3 == not set */
	char *new_res_name = NULL;

	slurm_init_resv_desc_msg (&resv_msg);

	for (i=0; i<argc; i++) {
		if        (strncasecmp(argv[i], "ReservationName=", 16) == 0) {
			resv_msg.name = &argv[i][16];

		} else if (strncasecmp(argv[i], "StartTime=", 10) == 0) {
			time_t  t = parse_time(&argv[i][10], 0);
			if (t == 0) {
				//TODO:  Set errno here instead of exit_code?
				exit_code = 1;
				error("Invalid input %s", argv[i]);
				return 0;
			}
			resv_msg.start_time = t;

		} else if (strncasecmp(argv[i], "EndTime=", 8) == 0) {
			time_t  t = parse_time(&argv[i][8], 0);
			if (t == 0) {
				//TODO:  Set errno here instead of exit_code?
				exit_code = 1;
				error("Invalid input %s", argv[i]);
				return 0;
			}
			resv_msg.end_time = t;

		} else if (strncasecmp(argv[i], "Duration=", 9) == 0) {
			/* -1 == INFINITE, -2 == error, -3 == not set */
			duration = time_str2mins(&argv[i][9]);
			if (duration == -2) {
				//TODO:  Set errno here instead of exit_code?
				exit_code = 1;
				error("Invalid input %s", argv[i]);
				return 0;
			}
		} else if (strncasecmp(argv[i], "Type=", 5) == 0) {
			char *typestr = &argv[i][5];
			if (strncasecmp(typestr, "Maintenance", 5) == 0) {
				resv_msg.type = RESERVE_TYPE_MAINT;
			} else {
				exit_code = 1;
				error("Unknown reservation type %s", &argv[i][5]);
				return 0;
			}
		} else if (strncasecmp(argv[i], "NodeCnt=", 8) == 0) {
			resv_msg.node_cnt = strtol(&argv[i][8], (char **)NULL, 10);
		} else if (strncasecmp(argv[i], "Nodes=", 6) == 0) {
			resv_msg.node_list = &argv[i][6];
		} else if (strncasecmp(argv[i], "Features=", 9) == 0) {
			resv_msg.features = &argv[i][9];
		} else if (strncasecmp(argv[i], "PartitionName=", 14) == 0) {
			resv_msg.partition = &argv[i][14];
		} else if (strncasecmp(argv[i], "Users=", 6) == 0) {
			resv_msg.users = &argv[i][6];
		} else if (strncasecmp(argv[i], "Accounts=", 9) == 0) {
			resv_msg.accounts = &argv[i][9];
		}
	}
	if (duration != -3) {
		if (duration == INFINITE)
			resv_msg.end_time = INFINITE;
		else
			resv_msg.end_time = resv_msg.start_time + duration*60;
	}

	new_res_name = slurm_create_reservation(&resv_msg);
	if (!new_res_name) {
		exit_code = 1;
		return slurm_get_errno();
	} else {
		free(new_res_name);
	}
	return 0;
}
