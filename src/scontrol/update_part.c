/*****************************************************************************\
 *  update_part.c - partition update function for scontrol.
 *****************************************************************************
 *  Copyright (C) 2002-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
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

#include "src/common/proc_args.h"
#include "src/scontrol/scontrol.h"


/* 
 * scontrol_update_part - update the slurm partition configuration per the 
 *	supplied arguments 
 * IN argc - count of arguments
 * IN argv - list of arguments
 * RET 0 if no slurm error, errno otherwise. parsing error prints 
 *			error message and returns 0
 */
extern int
scontrol_update_part (int argc, char *argv[]) 
{
	int i, min, max, update_cnt = 0;
	update_part_msg_t part_msg;

	slurm_init_part_desc_msg ( &part_msg );
	for (i=0; i<argc; i++) {
		if (strncasecmp(argv[i], "PartitionName=", 14) == 0)
			part_msg.name = &argv[i][14];
		else if (strncasecmp(argv[i], "MaxTime=", 8) == 0) {
			int max_time = time_str2mins(&argv[i][8]);
			if ((max_time < 0) && (max_time != INFINITE)) {
				exit_code = 1;
				error("Invalid input %s", argv[i]);
				return 0;
			}
			part_msg.max_time = max_time;
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "MaxNodes=", 9) == 0) {
			if ((strcasecmp(&argv[i][9],"UNLIMITED") == 0) ||
			    (strcasecmp(&argv[i][8],"INFINITE") == 0))
				part_msg.max_nodes = (uint32_t) INFINITE;
			else {
				min = 1;
				get_resource_arg_range(&argv[i][9],
					"MaxNodes", &min, &max, true);
				part_msg.max_nodes = min;
			}
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "MinNodes=", 9) == 0) {
			min = 1;
			get_resource_arg_range(&argv[i][9],
				"MinNodes", &min, &max, true);
			part_msg.min_nodes = min;
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Default=", 8) == 0) {
			if (strcasecmp(&argv[i][8], "NO") == 0)
				part_msg.default_part = 0;
			else if (strcasecmp(&argv[i][8], "YES") == 0)
				part_msg.default_part = 1;
			else {
				exit_code = 1;
				error("Invalid input: %s", argv[i]);
				error("Acceptable Default values "
					"are YES and NO");
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Hidden=", 4) == 0) {
			if (strcasecmp(&argv[i][7], "NO") == 0)
				part_msg.hidden = 0;
			else if (strcasecmp(&argv[i][7], "YES") == 0)
				part_msg.hidden = 1;
			else {
				exit_code = 1;
				error("Invalid input: %s", argv[i]);
				error("Acceptable Hidden values "
					"are YES and NO");
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "RootOnly=", 4) == 0) {
			if (strcasecmp(&argv[i][9], "NO") == 0)
				part_msg.root_only = 0;
			else if (strcasecmp(&argv[i][9], "YES") == 0)
				part_msg.root_only = 1;
			else {
				exit_code = 1;
				error("Invalid input: %s", argv[i]);
				error("Acceptable RootOnly values "
					"are YES and NO");
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Shared=", 7) == 0) {
			if (strncasecmp(&argv[i][7], "NO", 2) == 0) {
				part_msg.max_share = 1;
			} else if (strncasecmp(&argv[i][7], "EXCLUSIVE", 9) == 0) {
				part_msg.max_share = 0;
			} else if (strncasecmp(&argv[i][7], "YES:", 4) == 0) {
				part_msg.max_share = (uint16_t) strtol(&argv[i][11], 
					(char **) NULL, 10);
			} else if (strncasecmp(&argv[i][7], "YES", 3) == 0) {
				part_msg.max_share = (uint16_t) 4;
			} else if (strncasecmp(&argv[i][7], "FORCE:", 6) == 0) {
				part_msg.max_share = (uint16_t) strtol(&argv[i][13],
					(char **) NULL, 10) | SHARED_FORCE;
			} else if (strncasecmp(&argv[i][7], "FORCE", 5) == 0) {
				part_msg.max_share = (uint16_t) 4 |
					SHARED_FORCE;
			} else {
				exit_code = 1;
				error("Invalid input: %s", argv[i]);
				error("Acceptable Shared values are "
					"NO, EXCLUSIVE, YES:#, and FORCE:#");
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Priority=", 9) == 0) {
			part_msg.priority = (uint16_t) strtol(&argv[i][9], 
					(char **) NULL, 10);
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "State=", 6) == 0) {
			if (strcasecmp(&argv[i][6], "DOWN") == 0)
				part_msg.state_up = 0;
			else if (strcasecmp(&argv[i][6], "UP") == 0)
				part_msg.state_up = 1;
			else {
				exit_code = 1;
				error("Invalid input: %s", argv[i]);
				error("Acceptable State values "
					"are UP and DOWN");
				return 0;
			}
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "Nodes=", 6) == 0) {
			part_msg.nodes = &argv[i][6];
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "AllowGroups=", 12) == 0) {
			part_msg.allow_groups = &argv[i][12];
			update_cnt++;
		}
		else {
			exit_code = 1;
			error("Invalid input: %s", argv[i]);
			error("Request aborted");
			return 0;
		}
	}

	if (update_cnt == 0) {
		exit_code = 1;
		error("No changes specified");
		return 0;
	}

	if (slurm_update_partition(&part_msg)) {
		exit_code = 1;
		return slurm_get_errno ();
	} else
		return 0;
}
