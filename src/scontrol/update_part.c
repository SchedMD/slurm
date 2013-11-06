/*****************************************************************************\
 *  update_part.c - partition update function for scontrol.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Portions Copyright (C) 2010 SchedMD <http://www.schedmd.com>.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
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

#include "src/common/proc_args.h"
#include "src/scontrol/scontrol.h"

extern int
scontrol_parse_part_options (int argc, char *argv[], int *update_cnt_ptr,
			     update_part_msg_t *part_msg_ptr)
{
	int i, min, max;
	char *tag, *val;
	int taglen, vallen;

	if (!update_cnt_ptr) {
		error("scontrol_parse_part_options internal error, "
		      "update_cnt_ptr == NULL");
		exit_code = 1;
		return -1;
	}
	if (!part_msg_ptr) {
		error("scontrol_parse_part_options internal error, "
		      "part_msg_ptr == NULL");
		exit_code = 1;
		return -1;
	}

	for (i=0; i<argc; i++) {
		tag = argv[i];
		val = strchr(argv[i], '=');
		if (val) {
			taglen = val - argv[i];
			val++;
			vallen = strlen(val);
		} else {
			exit_code = 1;
			error("Invalid input: %s  Request aborted", argv[i]);
			return -1;
		}

		if (strncasecmp(tag, "PartitionName", MAX(taglen, 2)) == 0) {
			part_msg_ptr->name = val;
			(*update_cnt_ptr)++;
		} else if (strncasecmp(tag, "MaxTime", MAX(taglen, 4)) == 0) {
			int max_time = time_str2mins(val);
			if ((max_time < 0) && (max_time != INFINITE)) {
				exit_code = 1;
				error("Invalid input %s", argv[i]);
				return -1;
			}
			part_msg_ptr->max_time = max_time;
			(*update_cnt_ptr)++;
		}
		else if (strncasecmp(tag, "DefaultTime", MAX(taglen, 8)) == 0){
			int default_time = time_str2mins(val);
			if ((default_time < 0) && (default_time != INFINITE)) {
				exit_code = 1;
				error("Invalid input %s", argv[i]);
				return -1;
			}
			part_msg_ptr->default_time = default_time;
			(*update_cnt_ptr)++;
		}
		else if (strncasecmp(tag, "MaxCPUsPerNode", MAX(taglen, 4))
			  == 0) {
			if ((strcasecmp(val,"UNLIMITED") == 0) ||
			    (strcasecmp(val,"INFINITE") == 0)) {
				part_msg_ptr->max_cpus_per_node =
					(uint32_t) INFINITE;
			} else if (parse_uint32(val, &part_msg_ptr->
						      max_cpus_per_node)) {
				error("Invalid MaxCPUsPerNode value: %s", val);
				return -1;
			}
			(*update_cnt_ptr)++;
		}
		else if (strncasecmp(tag, "MaxNodes", MAX(taglen, 4)) == 0) {
			if ((strcasecmp(val,"UNLIMITED") == 0) ||
			    (strcasecmp(val,"INFINITE") == 0))
				part_msg_ptr->max_nodes = (uint32_t) INFINITE;
			else {
				min = 1;
				get_resource_arg_range(val,
					"MaxNodes", &min, &max, true);
				part_msg_ptr->max_nodes = min;
			}
			(*update_cnt_ptr)++;
		}
		else if (strncasecmp(tag, "MinNodes", MAX(taglen, 2)) == 0) {
			min = 1;
			verify_node_count(val, &min, &max);
			part_msg_ptr->min_nodes = min;
			(*update_cnt_ptr)++;
		}
		else if (strncasecmp(tag, "Default", MAX(taglen, 7)) == 0) {
			if (strncasecmp(val, "NO", MAX(vallen, 1)) == 0)
				part_msg_ptr->flags |= PART_FLAG_DEFAULT_CLR;
			else if (strncasecmp(val, "YES", MAX(vallen, 1)) == 0)
				part_msg_ptr->flags |= PART_FLAG_DEFAULT;
			else {
				exit_code = 1;
				error("Invalid input: %s", argv[i]);
				error("Acceptable Default values "
					"are YES and NO");
				return -1;
			}
			(*update_cnt_ptr)++;
		}
		else if (!strncasecmp(tag, "DisableRootJobs", MAX(taglen, 1))) {
			if (strncasecmp(val, "NO", MAX(vallen, 1)) == 0)
				part_msg_ptr->flags |= PART_FLAG_NO_ROOT_CLR;
			else if (strncasecmp(val, "YES", MAX(vallen, 1)) == 0)
				part_msg_ptr->flags |= PART_FLAG_NO_ROOT;
			else {
				exit_code = 1;
				error("Invalid input: %s", argv[i]);
				error("Acceptable DisableRootJobs values "
					"are YES and NO");
				return -1;
			}
			(*update_cnt_ptr)++;
		}
		else if (strncasecmp(tag, "Hidden", MAX(taglen, 1)) == 0) {
			if (strncasecmp(val, "NO", MAX(vallen, 1)) == 0)
				part_msg_ptr->flags |= PART_FLAG_HIDDEN_CLR;
			else if (strncasecmp(val, "YES", MAX(vallen, 1)) == 0)
				part_msg_ptr->flags |= PART_FLAG_HIDDEN;
			else {
				exit_code = 1;
				error("Invalid input: %s", argv[i]);
				error("Acceptable Hidden values "
					"are YES and NO");
				return -1;
			}
			(*update_cnt_ptr)++;
		}
		else if (strncasecmp(tag, "LLN", MAX(taglen, 1)) == 0) {
			if (strncasecmp(val, "NO", MAX(vallen, 1)) == 0)
				part_msg_ptr->flags |= PART_FLAG_LLN_CLR;
			else if (strncasecmp(val, "YES", MAX(vallen, 1)) == 0)
				part_msg_ptr->flags |= PART_FLAG_LLN;
			else {
				exit_code = 1;
				error("Invalid input: %s", argv[i]);
				error("Acceptable LLN values "
					"are YES and NO");
				return -1;
			}
			(*update_cnt_ptr)++;
		}
		else if (strncasecmp(tag, "RootOnly", MAX(taglen, 3)) == 0) {
			if (strncasecmp(val, "NO", MAX(vallen, 1)) == 0)
				part_msg_ptr->flags |= PART_FLAG_ROOT_ONLY_CLR;
			else if (strncasecmp(val, "YES", MAX(vallen, 1)) == 0)
				part_msg_ptr->flags |= PART_FLAG_ROOT_ONLY;
			else {
				exit_code = 1;
				error("Invalid input: %s", argv[i]);
				error("Acceptable RootOnly values "
					"are YES and NO");
				return -1;
			}
			(*update_cnt_ptr)++;
		}
		else if (strncasecmp(tag, "ReqResv", MAX(taglen, 3)) == 0) {
			if (strncasecmp(val, "NO", MAX(vallen, 1)) == 0)
				part_msg_ptr->flags |= PART_FLAG_REQ_RESV_CLR;
			else if (strncasecmp(val, "YES", MAX(vallen, 1)) == 0)
				part_msg_ptr->flags |= PART_FLAG_REQ_RESV;
			else {
				exit_code = 1;
				error("Invalid input: %s", argv[i]);
				error("Acceptable ReqResv values "
					"are YES and NO");
				return -1;
			}
			(*update_cnt_ptr)++;
		}
		else if (strncasecmp(tag, "Shared", MAX(taglen, 2)) == 0) {
			char *colon_pos = strchr(val, ':');
			if (colon_pos) {
				*colon_pos = '\0';
				vallen = strlen(val);
			}
			if (strncasecmp(val, "NO", MAX(vallen, 1)) == 0) {
				part_msg_ptr->max_share = 1;

			} else if (strncasecmp(val, "EXCLUSIVE",
				   MAX(vallen, 1)) == 0) {
				part_msg_ptr->max_share = 0;

			} else if (strncasecmp(val, "YES", MAX(vallen, 1))
				   == 0) {
				if (colon_pos) {
					part_msg_ptr->max_share =
						(uint16_t) strtol(colon_pos+1,
							(char **) NULL, 10);
				} else {
					part_msg_ptr->max_share = (uint16_t) 4;
				}
			} else if (strncasecmp(val, "FORCE", MAX(vallen, 1))
				   == 0) {
				if (colon_pos) {
					part_msg_ptr->max_share =
						(uint16_t) strtol(colon_pos+1,
							(char **) NULL, 10) |
							SHARED_FORCE;
				} else {
					part_msg_ptr->max_share =
						(uint16_t) 4 |SHARED_FORCE;
				}
			} else {
				exit_code = 1;
				error("Invalid input: %s", argv[i]);
				error("Acceptable Shared values are "
					"NO, EXCLUSIVE, YES:#, and FORCE:#");
				return -1;
			}
			(*update_cnt_ptr)++;
		}
		else if (strncasecmp(tag, "PreemptMode", MAX(taglen, 3)) == 0) {
			uint16_t new_mode = preempt_mode_num(val);
			if (new_mode != (uint16_t) NO_VAL)
				part_msg_ptr->preempt_mode = new_mode;
			else {
				error("Invalid input: %s", argv[i]);
				return -1;
			}
			(*update_cnt_ptr)++;
		}
		else if (strncasecmp(tag, "Priority", MAX(taglen, 3)) == 0) {
			if (parse_uint16(val, &part_msg_ptr->priority)) {
				error ("Invalid Priority value: %s", val);
				return -1;
			}
			(*update_cnt_ptr)++;
		}
		else if (strncasecmp(tag, "State", MAX(taglen, 2)) == 0) {
			if (strncasecmp(val, "INACTIVE", MAX(vallen, 1)) == 0)
				part_msg_ptr->state_up = PARTITION_INACTIVE;
			else if (strncasecmp(val, "DOWN", MAX(vallen, 1)) == 0)
				part_msg_ptr->state_up = PARTITION_DOWN;
			else if (strncasecmp(val, "UP", MAX(vallen, 1)) == 0)
				part_msg_ptr->state_up = PARTITION_UP;
			else if (strncasecmp(val, "DRAIN", MAX(vallen, 1)) == 0)
				part_msg_ptr->state_up = PARTITION_DRAIN;
			else {
				exit_code = 1;
				error("Invalid input: %s", argv[i]);
				error("Acceptable State values "
					"are UP, DOWN, DRAIN and INACTIVE");
				return -1;
			}
			(*update_cnt_ptr)++;
		}
		else if (!strncasecmp(tag, "Nodes", MAX(taglen, 1))) {
			part_msg_ptr->nodes = val;
			(*update_cnt_ptr)++;
		}
		else if (!strncasecmp(tag, "AllowGroups", MAX(taglen, 6))) {
			part_msg_ptr->allow_groups = val;
			(*update_cnt_ptr)++;
		}
		else if (!strncasecmp(tag, "AllowAccounts", MAX(taglen, 6))) {
			part_msg_ptr->allow_accounts = val;
			(*update_cnt_ptr)++;
		}
		else if (!strncasecmp(tag, "AllowQos", MAX(taglen, 6))) {
			part_msg_ptr->allow_qos = val;
			(*update_cnt_ptr)++;
		}
		else if (!strncasecmp(tag, "DenyAccounts", MAX(taglen, 5))) {
			part_msg_ptr->deny_accounts = val;
			(*update_cnt_ptr)++;
		}
		else if (!strncasecmp(tag, "DenyQos", MAX(taglen, 5))) {
			part_msg_ptr->deny_qos = val;
			(*update_cnt_ptr)++;
		}
		else if (!strncasecmp(tag, "AllocNodes", MAX(taglen, 6))) {
			part_msg_ptr->allow_alloc_nodes = val;
			(*update_cnt_ptr)++;
		}
		else if (!strncasecmp(tag, "Alternate", MAX(taglen, 3))) {
			part_msg_ptr->alternate = val;
			(*update_cnt_ptr)++;
		}
		else if (!strncasecmp(tag, "GraceTime", MAX(taglen, 5))) {
			if (parse_uint32(val, &part_msg_ptr->grace_time)) {
				error ("Invalid GraceTime value: %s", val);
				return -1;
			}
			(*update_cnt_ptr)++;
		}
		else if (!strncasecmp(tag, "DefMemPerCPU", MAX(taglen, 10))) {
			if (parse_uint32(val, &part_msg_ptr->def_mem_per_cpu)) {
				error ("Invalid DefMemPerCPU value: %s", val);
				return -1;
			}
			part_msg_ptr->def_mem_per_cpu |= MEM_PER_CPU;
			(*update_cnt_ptr)++;
		}
		else if (!strncasecmp(tag, "DefMemPerNode", MAX(taglen, 10))) {
			if (parse_uint32(val, &part_msg_ptr->def_mem_per_cpu)) {
				error ("Invalid DefMemPerNode value: %s", val);
				return -1;
			}
			(*update_cnt_ptr)++;
		}
		else if (!strncasecmp(tag, "MaxMemPerCPU", MAX(taglen, 10))) {
			if (parse_uint32(val, &part_msg_ptr->max_mem_per_cpu)) {
				error ("Invalid MaxMemPerCPU value: %s", val);
				return -1;
			}
			part_msg_ptr->max_mem_per_cpu |= MEM_PER_CPU;
			(*update_cnt_ptr)++;
		}
		else if (!strncasecmp(tag, "MaxMemPerNode", MAX(taglen, 10))) {
			if (parse_uint32(val, &part_msg_ptr->max_mem_per_cpu)) {
				error ("Invalid MaxMemPerNode value: %s", val);
				return -1;
			}
			(*update_cnt_ptr)++;
		}
		else {
			exit_code = 1;
			error("Update of this parameter is not "
			      "supported: %s\n", argv[i]);
			error("Request aborted");
			return -1;
		}
	}
	return 0;
}



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
	int update_cnt = 0;
	update_part_msg_t part_msg;

	slurm_init_part_desc_msg ( &part_msg );
	scontrol_parse_part_options (argc, argv, &update_cnt, &part_msg);

	if (part_msg.name == NULL) {
		exit_code = 1;
		error("PartitionName must be given.");
		return 0;
	}
	if (update_cnt <= 1) {
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



/*
 * scontrol_create_part - create a slurm partition configuration per the
 *	supplied arguments
 * IN argc - count of arguments
 * IN argv - list of arguments
 * RET 0 if no slurm error, errno otherwise. parsing error prints
 *			error message and returns 0
 */
extern int
scontrol_create_part (int argc, char *argv[])
{
	int update_cnt = 0;
	update_part_msg_t part_msg;

	slurm_init_part_desc_msg ( &part_msg );
	scontrol_parse_part_options (argc, argv, &update_cnt, &part_msg);

	if (part_msg.name == NULL) {
		exit_code = 1;
		error("PartitionName must be given.");
		return 0;
	}
	if (update_cnt == 0) {
		exit_code = 1;
		error("No parameters specified");
		return 0;
	}

	if (slurm_create_partition(&part_msg)) {
		exit_code = 1;
		slurm_perror("Error creating the partition");
		return slurm_get_errno ();
	} else
		return 0;
}








