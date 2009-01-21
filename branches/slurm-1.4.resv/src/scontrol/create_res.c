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


extern void
scontrol_parse_res_options(int argc, char *argv[], 
			   reserve_request_msg_t  *resv_msg_ptr, 
			   const char *msg)
{
	int i;
	int duration = -3;   /* -1 == INFINITE, -2 == error, -3 == not set */

	for (i=0; i<argc; i++) {
		if        (strncasecmp(argv[i], "ReservationName=", 16) == 0) {
			resv_msg_ptr->name = &argv[i][16];

		} else if (strncasecmp(argv[i], "StartTime=", 10) == 0) {
			time_t  t = parse_time(&argv[i][10], 0);
			if (t == 0) {
				//TODO:  Set errno here instead of exit_code?
				exit_code = 1;
				error("Invalid start time %s.  %s", argv[i], msg);
				return;
			}
			resv_msg_ptr->start_time = t;

		} else if (strncasecmp(argv[i], "EndTime=", 8) == 0) {
			time_t  t = parse_time(&argv[i][8], 0);
			if (t == 0) {
				//TODO:  Set errno here instead of exit_code?
				exit_code = 1;
				error("Invalid end time %s.  %s", argv[i], msg);
				return;
			}
			resv_msg_ptr->end_time = t;

		} else if (strncasecmp(argv[i], "Duration=", 9) == 0) {
			/* -1 == INFINITE, -2 == error, -3 == not set */
			duration = time_str2mins(&argv[i][9]);
			if (duration < 0 && duration != INFINITE) {
				//TODO:  Set errno here instead of exit_code?
				exit_code = 1;
				error("Invalid duration %s.  %s", argv[i], msg);
				return;
			}
			resv_msg_ptr->duration = (uint32_t)duration;

		} else if (strncasecmp(argv[i], "Type=", 5) == 0) {
			char *typestr = &argv[i][5];
			if (strncasecmp(typestr, "Maintenance", 5) == 0) {
				resv_msg_ptr->type = RESERVE_TYPE_MAINT;
			} else {
				exit_code = 1;
				error("Invalid type %s.  %s", argv[i], msg);
				return;
			}
		} else if (strncasecmp(argv[i], "NodeCnt=", 8) == 0) {
			char *endptr = NULL;
			resv_msg_ptr->node_cnt = strtol(&argv[i][8], &endptr, 10);

			if (endptr == NULL || *endptr != '\0' || 
                            argv[i][8] == '\0') {
				exit_code = 1;
				error("Invalid node count %s.  %s", argv[i], msg);
				return;
			}
		} else if (strncasecmp(argv[i], "Nodes=", 6) == 0) {
			resv_msg_ptr->node_list = &argv[i][6];
		} else if (strncasecmp(argv[i], "Features=", 9) == 0) {
			resv_msg_ptr->features = &argv[i][9];
		} else if (strncasecmp(argv[i], "PartitionName=", 14) == 0) {
			resv_msg_ptr->partition = &argv[i][14];
		} else if (strncasecmp(argv[i], "Users=", 6) == 0) {
			resv_msg_ptr->users = &argv[i][6];
		} else if (strncasecmp(argv[i], "Accounts=", 9) == 0) {
			resv_msg_ptr->accounts = &argv[i][9];
		} else if (strncasecmp(argv[i], "res", 3) == 0) {
			continue;
		} else {
			exit_code = 1;
			error("Unknown parameter %s.  %s", argv[i], msg);
			return;
		}
	}
}



extern int
scontrol_update_res(int argc, char *argv[])
{
	reserve_request_msg_t   resv_msg;
	char *new_res_name = NULL;
	int err;

	slurm_init_resv_desc_msg (&resv_msg);
	scontrol_parse_res_options(argc, argv, &resv_msg, "No reservation update.");
	if (exit_code == 1)
		return 0;

	if (resv_msg.name == NULL) {
		exit_code = 1;
		error("ReservationName must be given.  No reservation update.");
		return 0;
	}


	err = slurm_update_reservation(&resv_msg);
	if (err) {
		exit_code = 1;
		slurm_perror("Error updating the reservation.");
		return slurm_get_errno();
	} else {
		printf("Reservation updated.\n");
		free(new_res_name);
	}
	return 0;
}



extern int
scontrol_create_res(int argc, char *argv[])
{
	reserve_request_msg_t   resv_msg;
	char *new_res_name = NULL;

	slurm_init_resv_desc_msg (&resv_msg);
	scontrol_parse_res_options(argc, argv, &resv_msg, "No reservation created.");
	if (exit_code == 1)
		return 0;


	if (resv_msg.start_time == (time_t)NO_VAL) {
		exit_code = 1;
		error("A start time must be given.  No reservation created.");
		return 0;
	}
	if (resv_msg.end_time == (time_t)NO_VAL && 
	    resv_msg.duration == (uint32_t)NO_VAL) {
		exit_code = 1;
		error("An end time or duration must be given.  "
		      "No reservation created.");
		return 0;
	}
	if (resv_msg.end_time != (time_t)NO_VAL && 
	    resv_msg.duration != (uint32_t)NO_VAL && 
            resv_msg.start_time + resv_msg.duration*60 != resv_msg.end_time) {
		exit_code = 1;
		error("StartTime + Duration does not equal EndTime.  "
		      "No reservation created.");
		return 0;
	}
	if (resv_msg.start_time > resv_msg.end_time && 
	    resv_msg.end_time != (time_t)NO_VAL) {
		exit_code = 1;
		error("Start time cannot be after end time.  "
		      "No reservation created.");
		return 0;
	}
	if (resv_msg.node_cnt == NO_VAL && resv_msg.node_list == NULL) {
		exit_code = 1;
		error("Either Nodes or NodeCnt must be specified.  "
		      "No reservation created.");
		return 0;
	}
	if (resv_msg.users == NULL && resv_msg.accounts == NULL) {
		exit_code = 1;
		error("Either Users or Accounts must be specified.  "
		      "No reservation created.");
		return 0;
	}

	new_res_name = slurm_create_reservation(&resv_msg);
	if (!new_res_name) {
		exit_code = 1;
		slurm_perror("Error creating the reservation");
		return slurm_get_errno();
	} else {
		printf("Reservation created: %s\n", new_res_name);
		free(new_res_name);
	}
	return 0;
}
