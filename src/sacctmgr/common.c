/*****************************************************************************\
 *  common.c - definitions for functions common to all modules in sacctmgr.
 *****************************************************************************
 *  Copyright (C) 2008 Lawrence Livermore National Security.
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Danny Auble <da@llnl.gov>
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

#include "src/sacctmgr/sacctmgr.h"
#include "src/common/slurmdbd_defs.h"
#include "src/common/slurm_auth.h"

#include <unistd.h>
#include <termios.h>

#define FORMAT_STRING_SIZE 32

static pthread_t lock_warning_thread;

static void *_print_lock_warn(void *no_data)
{
	sleep(5);
	printf(" Database is busy or waiting for lock from other user.\n");

	return NULL;
}

static void _nonblock(int state)
{
	struct termios ttystate;

	//get the terminal state
	tcgetattr(STDIN_FILENO, &ttystate);

	switch(state) {
	case 1:
		//turn off canonical mode
		ttystate.c_lflag &= ~ICANON;
		//minimum of number input read.
		ttystate.c_cc[VMIN] = 1;
		break;
	default:
		//turn on canonical mode
		ttystate.c_lflag |= ICANON;
	}
	//set the terminal attributes.
	tcsetattr(STDIN_FILENO, TCSANOW, &ttystate);

}

extern int parse_option_end(char *option)
{
	int end = 0;

	if (!option)
		return 0;

	while(option[end]) {
		if ((option[end] == '=')
		   || (option[end] == '+' && option[end+1] == '=')
		   || (option[end] == '-' && option[end+1] == '='))
			break;
		end++;
	}

	if (!option[end])
		return 0;

	end++;
	return end;
}

/* you need to xfree whatever is sent from here */
extern char *strip_quotes(char *option, int *increased, bool make_lower)
{
	int end = 0;
	int i=0, start=0;
	char *meat = NULL;
	char quote_c = '\0';
	int quote = 0;

	if (!option)
		return NULL;

	/* first strip off the ("|')'s */
	if (option[i] == '\"' || option[i] == '\'') {
		quote_c = option[i];
		quote = 1;
		i++;
	}
	start = i;

	while(option[i]) {
		if (quote && option[i] == quote_c) {
			end++;
			break;
		} else if (option[i] == '\"' || option[i] == '\'')
			option[i] = '`';
		else if (make_lower) {
			char lower = tolower(option[i]);
			if (lower != option[i])
				option[i] = lower;
		}

		i++;
	}
	end += i;

	meat = xmalloc((i-start)+1);
	memcpy(meat, option+start, (i-start));

	if (increased)
		(*increased) += end;

	return meat;
}

static print_field_t *_get_print_field(char *object)
{
	/* This should be kept in alpha order to avoid picking the
	   wrong field name.
	*/
	print_field_t *field = xmalloc(sizeof(print_field_t));
	char *tmp_char = NULL;
	int command_len, field_len = 0;

	if ((tmp_char = strstr(object, "\%"))) {
		field_len = atoi(tmp_char+1);
		tmp_char[0] = '\0';
	}
	command_len = strlen(object);

	if (!strncasecmp("Account", object, MAX(command_len, 3))
	    || !strncasecmp("Acct", object, MAX(command_len, 4))) {
		field->type = PRINT_ACCT;
		field->name = xstrdup("Account");
		if (tree_display)
			field->len = -20;
		else
			field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("ActionRaw", object, MAX(command_len, 7))) {
		field->type = PRINT_ACTIONRAW;
		field->name = xstrdup("ActionRaw");
		field->len = 10;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("Action", object, MAX(command_len, 4))) {
		field->type = PRINT_ACTION;
		field->name = xstrdup("Action");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("Actor", object, MAX(command_len, 4))) {
		field->type = PRINT_ACTOR;
		field->name = xstrdup("Actor");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("AdminLevel", object, MAX(command_len, 2))) {
		field->type = PRINT_ADMIN;
		field->name = xstrdup("Admin");
		field->len = 9;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("Classification", object,
				MAX(command_len, 3))) {
		field->type = PRINT_CPUS;
		field->name = xstrdup("Class");
		field->len = 9;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("ClusterNodes", object, MAX(command_len, 8))
		   || !strncasecmp("NodeNames", object, MAX(command_len, 8))) {
		field->type = PRINT_CLUSTER_NODES;
		field->name = xstrdup("Cluster Nodes");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("Cluster", object, MAX(command_len, 2))) {
		field->type = PRINT_CLUSTER;
		field->name = xstrdup("Cluster");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("Coordinators", object, MAX(command_len, 2))) {
		field->type = PRINT_COORDS;
		field->name = xstrdup("Coord Accounts");
		field->len = 20;
		field->print_routine = sacctmgr_print_coord_list;
	} else if (!strncasecmp("ControlHost", object, MAX(command_len, 8))) {
		field->type = PRINT_CHOST;
		field->name = xstrdup("ControlHost");
		field->len = 15;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("ControlPort", object, MAX(command_len, 8))) {
		field->type = PRINT_CPORT;
		field->name = xstrdup("ControlPort");
		field->len = 12;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("CPUCount", object, MAX(command_len, 2))) {
		field->type = PRINT_CPUS;
		field->name = xstrdup("CPUCount");
		field->len = 9;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("DefaultAccount", object,
				MAX(command_len, 8))) {
		field->type = PRINT_DACCT;
		field->name = xstrdup("Def Acct");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("DefaultQOS", object, MAX(command_len, 8))) {
		field->type = PRINT_DQOS;
		field->name = xstrdup("Def QOS");
		field->len = 9;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("DefaultWCKey", object, MAX(command_len, 8))) {
		field->type = PRINT_DWCKEY;
		field->name = xstrdup("Def WCKey");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("Description", object, MAX(command_len, 3))) {
		field->type = PRINT_DESC;
		field->name = xstrdup("Descr");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("Duration", object, MAX(command_len, 2))) {
		field->type = PRINT_DURATION;
		field->name = xstrdup("Duration");
		field->len = 13;
		field->print_routine = print_fields_time_from_secs;
	} else if (!strncasecmp("End", object, MAX(command_len, 2))) {
		field->type = PRINT_END;
		field->name = xstrdup("End");
		field->len = 19;
		field->print_routine = print_fields_date;
	} else if (!strncasecmp("EventRaw", object, MAX(command_len, 6))) {
		field->type = PRINT_EVENTRAW;
		field->name = xstrdup("EventRaw");
		field->len = 8;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("Event", object, MAX(command_len, 2))) {
		field->type = PRINT_EVENT;
		field->name = xstrdup("Event");
		field->len = 7;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("Flags", object, MAX(command_len, 2))) {
		field->type = PRINT_FLAGS;
		field->name = xstrdup("Flags");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("GraceTime", object, MAX(command_len, 3))) {
		field->type = PRINT_GRACE;
		field->name = xstrdup("GraceTime");
		field->len = 10;
		field->print_routine = print_fields_time_from_secs;
	} else if (!strncasecmp("GrpCPUMins", object, MAX(command_len, 7))) {
		field->type = PRINT_GRPCM;
		field->name = xstrdup("GrpCPUMins");
		field->len = 11;
		field->print_routine = print_fields_uint64;
	} else if (!strncasecmp("GrpCPURunMins", object, MAX(command_len, 7))) {
		field->type = PRINT_GRPCRM;
		field->name = xstrdup("GrpCPURunMins");
		field->len = 13;
		field->print_routine = print_fields_uint64;
	} else if (!strncasecmp("GrpCPUs", object, MAX(command_len, 7))) {
		field->type = PRINT_GRPC;
		field->name = xstrdup("GrpCPUs");
		field->len = 8;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("GrpJobs", object, MAX(command_len, 4))) {
		field->type = PRINT_GRPJ;
		field->name = xstrdup("GrpJobs");
		field->len = 7;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("GrpMemory", object, MAX(command_len, 4))) {
		field->type = PRINT_GRPMEM;
		field->name = xstrdup("GrpMem");
		field->len = 7;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("GrpNodes", object, MAX(command_len, 4))) {
		field->type = PRINT_GRPN;
		field->name = xstrdup("GrpNodes");
		field->len = 8;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("GrpSubmitJobs", object, MAX(command_len, 4))) {
		field->type = PRINT_GRPS;
		field->name = xstrdup("GrpSubmit");
		field->len = 9;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("GrpWall", object, MAX(command_len, 4))) {
		field->type = PRINT_GRPW;
		field->name = xstrdup("GrpWall");
		field->len = 11;
		field->print_routine = print_fields_time;
	} else if (!strncasecmp("ID", object, MAX(command_len, 2))) {
		field->type = PRINT_ID;
		field->name = xstrdup("ID");
		field->len = 6;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("Info", object, MAX(command_len, 2))) {
		field->type = PRINT_INFO;
		field->name = xstrdup("Info");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("LFT", object, MAX(command_len, 1))) {
		field->type = PRINT_LFT;
		field->name = xstrdup("LFT");
		field->len = 6;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("MaxCPUMinsPerJob", object,
				MAX(command_len, 7))) {
		field->type = PRINT_MAXCM;
		field->name = xstrdup("MaxCPUMins");
		field->len = 11;
		field->print_routine = print_fields_uint64;
	} else if (!strncasecmp("MaxCPURunMinsPerUser",
				object, MAX(command_len, 7))) {
		field->type = PRINT_MAXCRM;
		field->name = xstrdup("MaxCPURunMinsPU");
		field->len = 15;
		field->print_routine = print_fields_uint64;
	} else if (!strncasecmp("MaxCPUsPerJob", object, MAX(command_len, 7))) {
		field->type = PRINT_MAXC;
		field->name = xstrdup("MaxCPUs");
		field->len = 8;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("MaxCPUsPerUser", object,
				MAX(command_len, 11))) {
		field->type = PRINT_MAXCU;
		field->name = xstrdup("MaxCPUsPU");
		field->len = 9;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("MaxJobs", object, MAX(command_len, 4))) {
		field->type = PRINT_MAXJ;
		field->name = xstrdup("MaxJobs");
		field->len = 7;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("MaxJobsPerUser",
				object, MAX(command_len, 8))) {
		field->type = PRINT_MAXJ; /* used same as MaxJobs */
		field->name = xstrdup("MaxJobsPU");
		field->len = 9;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("MaxNodesPerJob", object,
				MAX(command_len, 4))) {
		field->type = PRINT_MAXN;
		field->name = xstrdup("MaxNodes");
		field->len = 8;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("MaxNodesPerUser", object,
				MAX(command_len, 12))) {
		field->type = PRINT_MAXNU;
		field->name = xstrdup("MaxNodesPU");
		field->len = 10;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("MaxSubmitJobs", object, MAX(command_len, 4))) {
		field->type = PRINT_MAXS;
		field->name = xstrdup("MaxSubmit");
		field->len = 9;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("MaxSubmitJobsPerUser",
				object, MAX(command_len, 10))) {
		field->type = PRINT_MAXS; /* used same as MaxSubmitJobs */
		field->name = xstrdup("MaxSubmitPU");
		field->len = 11;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("MaxWallDurationPerJob", object,
				MAX(command_len, 4))) {
		field->type = PRINT_MAXW;
		field->name = xstrdup("MaxWall");
		field->len = 11;
		field->print_routine = print_fields_time;
	} else if (!strncasecmp("Name", object, MAX(command_len, 2))) {
		field->type = PRINT_NAME;
		field->name = xstrdup("Name");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("NodeCount", object, MAX(command_len, 5))) {
		field->type = PRINT_NODECNT;
		field->name = xstrdup("NodeCount");
		field->len = 9;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("NodeName", object, MAX(command_len, 5))) {
		field->type = PRINT_NODENAME;
		field->name = xstrdup("NodeName");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("Organization", object, MAX(command_len, 1))) {
		field->type = PRINT_ORG;
		field->name = xstrdup("Org");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("ParentID", object, MAX(command_len, 7))) {
		field->type = PRINT_PID;
		field->name = xstrdup("Par ID");
		field->len = 6;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("ParentName", object, MAX(command_len, 7))) {
		field->type = PRINT_PNAME;
		field->name = xstrdup("Par Name");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("Partition", object, MAX(command_len, 4))) {
		field->type = PRINT_PART;
		field->name = xstrdup("Partition");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("PluginIDSelect", object,
				MAX(command_len, 2))) {
		field->type = PRINT_SELECT;
		field->name = xstrdup("PluginIDSelect");
		field->len = 14;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("PreemptMode", object, MAX(command_len, 8))) {
		field->type = PRINT_PREEM;
		field->name = xstrdup("PreemptMode");
		field->len = 11;
		field->print_routine = print_fields_str;
	/* Preempt needs to follow PreemptMode */
	} else if (!strncasecmp("Preempt", object, MAX(command_len, 7))) {
		field->type = PRINT_PREE;
		field->name = xstrdup("Preempt");
		field->len = 10;
		field->print_routine = sacctmgr_print_qos_bitstr;
	} else if (!strncasecmp("Priority", object, MAX(command_len, 3))) {
		field->type = PRINT_PRIO;
		field->name = xstrdup("Priority");
		field->len = 10;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("Problem", object, MAX(command_len, 1))) {
		field->type = PRINT_PROBLEM;
		field->name = xstrdup("Problem");
		field->len = 40;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("QOSLevel", object, MAX(command_len, 3))) {
		field->type = PRINT_QOS;
		field->name = xstrdup("QOS");
		field->len = 20;
		field->print_routine = sacctmgr_print_qos_list;
	} else if (!strncasecmp("QOSRAWLevel", object, MAX(command_len, 4))) {
		field->type = PRINT_QOS_RAW;
		field->name = xstrdup("QOS_RAW");
		field->len = 10;
		field->print_routine = print_fields_char_list;
	} else if (!strncasecmp("RGT", object, MAX(command_len, 1))) {
		field->type = PRINT_RGT;
		field->name = xstrdup("RGT");
		field->len = 6;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("RPC", object, MAX(command_len, 1))) {
		field->type = PRINT_RPC_VERSION;
		field->name = xstrdup("RPC");
		field->len = 5;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("Share", object, MAX(command_len, 1))
		   || !strncasecmp("FairShare", object, MAX(command_len, 2))) {
		field->type = PRINT_FAIRSHARE;
		field->name = xstrdup("Share");
		field->len = 9;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("TimeStamp", object, MAX(command_len, 1))) {
		field->type = PRINT_TS;
		field->name = xstrdup("Time");
		field->len = 19;
		field->print_routine = print_fields_date;
	} else if (!strncasecmp("UsageFactor", object, MAX(command_len, 6))) {
		field->type = PRINT_UF;
		field->name = xstrdup("UsageFactor");
		field->len = 11;
		field->print_routine = print_fields_double;
	} else if (!strncasecmp("UsageThreshold",
				object, MAX(command_len, 6))) {
		field->type = PRINT_UT;
		field->name = xstrdup("UsageThres");
		field->len = 10;
		field->print_routine = print_fields_double;
	} else if (!strncasecmp("User", object, MAX(command_len, 1))) {
		field->type = PRINT_USER;
		field->name = xstrdup("User");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("WCKeys", object, MAX(command_len, 2))) {
		field->type = PRINT_WCKEYS;
		field->name = xstrdup("WCKeys");
		field->len = 20;
		field->print_routine = print_fields_char_list;
	} else if (!strncasecmp("Where", object, MAX(command_len, 2))) {
		field->type = PRINT_WHERE;
		field->name = xstrdup("Where");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else {
		exit_code=1;
		fprintf(stderr, "Unknown field '%s'\n", object);
		exit(1);
	}

	if (field_len)
		field->len = field_len;
	return field;
}

extern int notice_thread_init()
{
	pthread_attr_t attr;

	slurm_attr_init(&attr);
	if (pthread_create(&lock_warning_thread, &attr,
			   &_print_lock_warn, NULL))
		error ("pthread_create error %m");
	slurm_attr_destroy(&attr);
	return SLURM_SUCCESS;
}

extern int notice_thread_fini()
{
	return pthread_cancel(lock_warning_thread);
}

extern int commit_check(char *warning)
{
	int ans = 0;
	char c = '\0';
	int fd = fileno(stdin);
	fd_set rfds;
	struct timeval tv;

	if (!rollback_flag)
		return 1;

	printf("%s (You have 30 seconds to decide)\n", warning);
	_nonblock(1);
	while(c != 'Y' && c != 'y'
	      && c != 'N' && c != 'n'
	      && c != '\n') {
		if (c) {
			printf("Y or N please\n");
		}
		printf("(N/y): ");
		fflush(stdout);
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		/* Wait up to 30 seconds. */
		tv.tv_sec = 30;
		tv.tv_usec = 0;
		if ((ans = select(fd+1, &rfds, NULL, NULL, &tv)) <= 0)
			break;

		c = getchar();
		printf("\n");
	}
	_nonblock(0);
	if (ans <= 0)
		printf("timeout\n");
	else if (c == 'Y' || c == 'y')
		return 1;

	return 0;
}

extern int sacctmgr_remove_assoc_usage(slurmdb_association_cond_t *assoc_cond)
{
	List update_list = NULL;
	List local_assoc_list = NULL;
	List local_cluster_list = NULL;
	ListIterator itr = NULL;
	ListIterator itr2 = NULL;
	ListIterator itr3 = NULL;
	char *account = NULL;
	char *cluster = NULL;
	char *user = NULL;
	slurmdb_association_rec_t* rec = NULL;
	slurmdb_cluster_rec_t* cluster_rec = NULL;
	slurmdb_update_object_t* update_obj = NULL;
	slurmdb_cluster_cond_t cluster_cond;
	int rc = SLURM_SUCCESS;

	if (!assoc_cond->cluster_list)
		assoc_cond->cluster_list = list_create(slurm_destroy_char);

	if (!list_count(assoc_cond->cluster_list)) {
		char *temp = slurm_get_cluster_name();
		if (temp) {
			printf("No cluster specified, resetting "
			       "on local cluster %s\n", temp);
			list_append(assoc_cond->cluster_list, temp);
		}
		if (!list_count(assoc_cond->cluster_list)) {
			error("A cluster name is required to remove usage");
			return SLURM_ERROR;
		}
	}

	if (!commit_check("Would you like to reset usage?")) {
		printf(" Changes Discarded\n");
		return rc;
	}

	local_assoc_list = acct_storage_g_get_associations(
		db_conn, my_uid, assoc_cond);

	slurmdb_init_cluster_cond(&cluster_cond, 0);
	cluster_cond.cluster_list = assoc_cond->cluster_list;
	local_cluster_list = acct_storage_g_get_clusters(
		db_conn, my_uid, &cluster_cond);

	itr = list_iterator_create(assoc_cond->cluster_list);
	itr2 = list_iterator_create(assoc_cond->acct_list);
	if (assoc_cond->user_list && list_count(assoc_cond->user_list))
		itr3 = list_iterator_create(assoc_cond->user_list);
	while((cluster = list_next(itr))) {
		cluster_rec = sacctmgr_find_cluster_from_list(
			local_cluster_list, cluster);
		if (!cluster_rec) {
			error("Failed to find cluster %s in database",
			      cluster);
			rc = SLURM_ERROR;
			goto end_it;
		}

		update_list = list_create(slurmdb_destroy_update_object);
		update_obj = xmalloc(sizeof(slurmdb_update_object_t));
		update_obj->type = SLURMDB_REMOVE_ASSOC_USAGE;
		update_obj->objects = list_create(NULL);

		if (itr3) {
			while ((user = list_next(itr3))) {
				while ((account = list_next(itr2))) {
					rec = sacctmgr_find_association_from_list(
						local_assoc_list,
						user, account, cluster, "*");
					if (!rec) {
						error("Failed to find "
						      "cluster %s "
						      "account %s user "
						      "%s association "
						      "in database",
						      cluster, account,
						      user);
						rc = SLURM_ERROR;
						goto end_it;
					}
					list_append(update_obj->objects, rec);
				}
				list_iterator_reset(itr2);
			}
			list_iterator_reset(itr3);
		} else {
			while ((account = list_next(itr2))) {
				rec = sacctmgr_find_association_from_list(
					local_assoc_list,
					NULL, account, cluster, "*");
				if (!rec) {
					error("Failed to find cluster %s "
					      "account %s association in "
					      "database",
					      cluster, account);
					rc = SLURM_ERROR;
					goto end_it;
				}
				list_append(update_obj->objects, rec);
			}
			list_iterator_reset(itr2);
		}

		if (list_count(update_obj->objects)) {
			list_append(update_list, update_obj);
			rc = slurmdb_send_accounting_update(
				update_list, cluster,
				cluster_rec->control_host,
				cluster_rec->control_port,
				cluster_rec->rpc_version);
		}
		list_destroy(update_list);
	}
end_it:
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);
	if (itr3)
		list_iterator_destroy(itr3);

	list_destroy(local_assoc_list);
	list_destroy(local_cluster_list);

	return rc;
}

extern slurmdb_association_rec_t *sacctmgr_find_account_base_assoc(
	char *account, char *cluster)
{
	slurmdb_association_rec_t *assoc = NULL;
	char *temp = "root";
	slurmdb_association_cond_t assoc_cond;
	List assoc_list = NULL;

	if (!cluster)
		return NULL;

	if (account)
		temp = account;

	memset(&assoc_cond, 0, sizeof(slurmdb_association_cond_t));
	assoc_cond.acct_list = list_create(NULL);
	list_append(assoc_cond.cluster_list, temp);
	assoc_cond.cluster_list = list_create(NULL);
	list_append(assoc_cond.cluster_list, cluster);
	assoc_cond.user_list = list_create(NULL);
	list_append(assoc_cond.user_list, "");

	assoc_list = acct_storage_g_get_associations(db_conn, my_uid,
						     &assoc_cond);

	list_destroy(assoc_cond.acct_list);
	list_destroy(assoc_cond.cluster_list);
	list_destroy(assoc_cond.user_list);

	if (assoc_list)
		assoc = list_pop(assoc_list);

	list_destroy(assoc_list);

	return assoc;
}

extern slurmdb_association_rec_t *sacctmgr_find_root_assoc(char *cluster)
{
	return sacctmgr_find_account_base_assoc(NULL, cluster);
}

extern slurmdb_user_rec_t *sacctmgr_find_user(char *name)
{
	slurmdb_user_rec_t *user = NULL;
	slurmdb_user_cond_t user_cond;
	slurmdb_association_cond_t assoc_cond;
	List user_list = NULL;

	if (!name)
		return NULL;

	memset(&user_cond, 0, sizeof(slurmdb_user_cond_t));
	memset(&assoc_cond, 0, sizeof(slurmdb_association_cond_t));
	assoc_cond.user_list = list_create(NULL);
	list_append(assoc_cond.user_list, name);
	user_cond.assoc_cond = &assoc_cond;

	user_list = acct_storage_g_get_users(db_conn, my_uid,
					     &user_cond);

	list_destroy(assoc_cond.user_list);

	if (user_list)
		user = list_pop(user_list);

	list_destroy(user_list);

	return user;
}

extern slurmdb_account_rec_t *sacctmgr_find_account(char *name)
{
	slurmdb_account_rec_t *account = NULL;
	slurmdb_account_cond_t account_cond;
	slurmdb_association_cond_t assoc_cond;
	List account_list = NULL;

	if (!name)
		return NULL;

	memset(&account_cond, 0, sizeof(slurmdb_account_cond_t));
	memset(&assoc_cond, 0, sizeof(slurmdb_association_cond_t));
	assoc_cond.acct_list = list_create(NULL);
	list_append(assoc_cond.acct_list, name);
	account_cond.assoc_cond = &assoc_cond;

	account_list = acct_storage_g_get_accounts(db_conn, my_uid,
						   &account_cond);

	list_destroy(assoc_cond.acct_list);

	if (account_list)
		account = list_pop(account_list);

	list_destroy(account_list);

	return account;
}

extern slurmdb_cluster_rec_t *sacctmgr_find_cluster(char *name)
{
	slurmdb_cluster_rec_t *cluster = NULL;
	slurmdb_cluster_cond_t cluster_cond;
	List cluster_list = NULL;

	if (!name)
		return NULL;

	slurmdb_init_cluster_cond(&cluster_cond, 0);
	cluster_cond.cluster_list = list_create(NULL);
	list_append(cluster_cond.cluster_list, name);

	cluster_list = acct_storage_g_get_clusters(db_conn, my_uid,
						   &cluster_cond);

	list_destroy(cluster_cond.cluster_list);

	if (cluster_list)
		cluster = list_pop(cluster_list);

	list_destroy(cluster_list);

	return cluster;
}

extern slurmdb_association_rec_t *sacctmgr_find_association_from_list(
	List assoc_list, char *user, char *account,
	char *cluster, char *partition)
{
	ListIterator itr = NULL;
	slurmdb_association_rec_t * assoc = NULL;

	if (!assoc_list)
		return NULL;

	itr = list_iterator_create(assoc_list);
	while((assoc = list_next(itr))) {
		if (((!user && assoc->user)
		     || (user && (!assoc->user
				  || strcasecmp(user, assoc->user))))
		    || (account && (!assoc->acct
				    || strcasecmp(account, assoc->acct)))
		    || ((!cluster && assoc->cluster)
			|| (cluster && (!assoc->cluster
					|| strcasecmp(cluster,
						      assoc->cluster)))))
			continue;
		else if (partition) {
			if (partition[0] != '*'
			    && (!assoc->partition
				|| strcasecmp(partition, assoc->partition)))
				continue;
		} else if (assoc->partition)
			continue;

		break;
	}
	list_iterator_destroy(itr);

	return assoc;
}

extern slurmdb_association_rec_t *sacctmgr_find_account_base_assoc_from_list(
	List assoc_list, char *account, char *cluster)
{
	ListIterator itr = NULL;
	slurmdb_association_rec_t *assoc = NULL;
	char *temp = "root";

	if (!cluster || !assoc_list)
		return NULL;

	if (account)
		temp = account;
	/* info("looking for %s %s in %d", account, cluster, */
/* 	     list_count(assoc_list)); */
	itr = list_iterator_create(assoc_list);
	while((assoc = list_next(itr))) {
		/* info("is it %s %s %s", assoc->user, assoc->acct, assoc->cluster); */
		if (assoc->user
		    || strcasecmp(temp, assoc->acct)
		    || strcasecmp(cluster, assoc->cluster))
			continue;
		/* 	info("found it"); */
		break;
	}
	list_iterator_destroy(itr);

	return assoc;
}

extern slurmdb_qos_rec_t *sacctmgr_find_qos_from_list(
	List qos_list, char *name)
{
	ListIterator itr = NULL;
	slurmdb_qos_rec_t *qos = NULL;
	char *working_name = NULL;

	if (!name || !qos_list)
		return NULL;

	if (name[0] == '+' || name[0] == '-')
		working_name = name+1;
	else
		working_name = name;

	itr = list_iterator_create(qos_list);
	while((qos = list_next(itr))) {
		if (!strcasecmp(working_name, qos->name))
			break;
	}
	list_iterator_destroy(itr);

	return qos;

}

extern slurmdb_user_rec_t *sacctmgr_find_user_from_list(
	List user_list, char *name)
{
	ListIterator itr = NULL;
	slurmdb_user_rec_t *user = NULL;

	if (!name || !user_list)
		return NULL;

	itr = list_iterator_create(user_list);
	while((user = list_next(itr))) {
		if (!strcasecmp(name, user->name))
			break;
	}
	list_iterator_destroy(itr);

	return user;

}

extern slurmdb_account_rec_t *sacctmgr_find_account_from_list(
	List acct_list, char *name)
{
	ListIterator itr = NULL;
	slurmdb_account_rec_t *account = NULL;

	if (!name || !acct_list)
		return NULL;

	itr = list_iterator_create(acct_list);
	while((account = list_next(itr))) {
		if (!strcasecmp(name, account->name))
			break;
	}
	list_iterator_destroy(itr);

	return account;

}

extern slurmdb_cluster_rec_t *sacctmgr_find_cluster_from_list(
	List cluster_list, char *name)
{
	ListIterator itr = NULL;
	slurmdb_cluster_rec_t *cluster = NULL;

	if (!name || !cluster_list)
		return NULL;

	itr = list_iterator_create(cluster_list);
	while((cluster = list_next(itr))) {
		if (!strcasecmp(name, cluster->name))
			break;
	}
	list_iterator_destroy(itr);

	return cluster;
}

extern slurmdb_wckey_rec_t *sacctmgr_find_wckey_from_list(
	List wckey_list, char *user, char *name, char *cluster)
{
	ListIterator itr = NULL;
	slurmdb_wckey_rec_t * wckey = NULL;

	if (!wckey_list)
		return NULL;

	itr = list_iterator_create(wckey_list);
	while((wckey = list_next(itr))) {
		if (((!user && wckey->user)
		     || (user && (!wckey->user
				  || strcasecmp(user, wckey->user))))
		    || (name && (!wckey->name
				 || strcasecmp(name, wckey->name)))
		    || ((!cluster && wckey->cluster)
			|| (cluster && (!wckey->cluster
					|| strcasecmp(cluster,
						      wckey->cluster)))))
			continue;
		break;
	}
	list_iterator_destroy(itr);

	return wckey;
}

extern int get_uint(char *in_value, uint32_t *out_value, char *type)
{
	char *ptr = NULL, *meat = NULL;
	long num;

	if (!(meat = strip_quotes(in_value, NULL, 1))) {
		error("Problem with strip_quotes");
		return SLURM_ERROR;
	}
	num = strtol(meat, &ptr, 10);
	if ((num == 0) && ptr && ptr[0]) {
		error("Invalid value for %s (%s)", type, meat);
		xfree(meat);
		return SLURM_ERROR;
	}
	xfree(meat);

	if (num < 0)
		*out_value = INFINITE;		/* flag to clear */
	else
		*out_value = (uint32_t) num;
	return SLURM_SUCCESS;
}

extern int get_uint16(char *in_value, uint16_t *out_value, char *type)
{
	char *ptr = NULL, *meat = NULL;
	long num;

	if (!(meat = strip_quotes(in_value, NULL, 1))) {
		error("Problem with strip_quotes");
		return SLURM_ERROR;
	}

	num = strtol(meat, &ptr, 10);
	if ((num == 0) && ptr && ptr[0]) {
		error("Invalid value for %s (%s)", type, meat);
		xfree(meat);
		return SLURM_ERROR;
	}
	xfree(meat);

	if (num < 0)
		*out_value = (uint16_t) INFINITE; /* flag to clear */
	else
		*out_value = (uint16_t) num;
	return SLURM_SUCCESS;
}

extern int get_uint64(char *in_value, uint64_t *out_value, char *type)
{
	char *ptr = NULL, *meat = NULL;
	long long num;

	if (!(meat = strip_quotes(in_value, NULL, 1))) {
		error("Problem with strip_quotes");
		return SLURM_ERROR;
	}

	num = strtoll(meat, &ptr, 10);
	if ((num == 0) && ptr && ptr[0]) {
		error("Invalid value for %s (%s)", type, meat);
		xfree(meat);
		return SLURM_ERROR;
	}
	xfree(meat);

	if (num < 0)
		*out_value = INFINITE;		/* flag to clear */
	else
		*out_value = (uint64_t) num;
	return SLURM_SUCCESS;
}

extern int get_double(char *in_value, double *out_value, char *type)
{
	char *ptr = NULL, *meat = NULL;
	double num;

	if (!(meat = strip_quotes(in_value, NULL, 1))) {
		error("Problem with strip_quotes");
		return SLURM_ERROR;
	}
	num = strtod(meat, &ptr);
	if ((num == 0) && ptr && ptr[0]) {
		error("Invalid value for %s (%s)", type, meat);
		xfree(meat);
		return SLURM_ERROR;
	}
	xfree(meat);

	if (num < 0)
		*out_value = (double) INFINITE;		/* flag to clear */
	else
		*out_value = (double) num;
	return SLURM_SUCCESS;
}

extern int addto_action_char_list(List char_list, char *names)
{
	int i=0, start=0;
	char *name = NULL, *tmp_char = NULL;
	ListIterator itr = NULL;
	char quote_c = '\0';
	int quote = 0;
	uint32_t id=0;
	int count = 0;

	if (!char_list) {
		error("No list was given to fill in");
		return 0;
	}

	itr = list_iterator_create(char_list);
	if (names) {
		if (names[i] == '\"' || names[i] == '\'') {
			quote_c = names[i];
			quote = 1;
			i++;
		}
		start = i;
		while(names[i]) {
			if (quote && names[i] == quote_c)
				break;
			else if (names[i] == '\"' || names[i] == '\'')
				names[i] = '`';
			else if (names[i] == ',') {
				if ((i-start) > 0) {
					name = xmalloc((i-start+1));
					memcpy(name, names+start, (i-start));

					id = str_2_slurmdbd_msg_type(name);
					if (id == NO_VAL) {
						error("You gave a bad action "
						      "'%s'.", name);
						xfree(name);
						break;
					}
					xfree(name);

					name = xstrdup_printf("%u", id);
					while((tmp_char = list_next(itr))) {
						if (!strcasecmp(tmp_char, name))
							break;
					}
					list_iterator_reset(itr);

					if (!tmp_char) {
						list_append(char_list, name);
						count++;
					} else
						xfree(name);
				}

				i++;
				start = i;
				if (!names[i]) {
					error("There is a problem with "
					      "your request.  It appears you "
					      "have spaces inside your list.");
					break;
				}
			}
			i++;
		}
		if ((i-start) > 0) {
			name = xmalloc((i-start)+1);
			memcpy(name, names+start, (i-start));

			id = str_2_slurmdbd_msg_type(name);
			if (id == NO_VAL)  {
				error("You gave a bad action '%s'.",
				      name);
				xfree(name);
				goto end_it;
			}
			xfree(name);

			name = xstrdup_printf("%u", id);
			while((tmp_char = list_next(itr))) {
				if (!strcasecmp(tmp_char, name))
					break;
			}

			if (!tmp_char) {
				list_append(char_list, name);
				count++;
			} else
				xfree(name);
		}
	}
end_it:
	list_iterator_destroy(itr);
	return count;
}

extern List copy_char_list(List char_list)
{
	List ret_list = NULL;
	char *tmp_char = NULL;
	ListIterator itr = NULL;

	if (!char_list || !list_count(char_list))
		return NULL;

	itr = list_iterator_create(char_list);
	ret_list = list_create(slurm_destroy_char);

	while((tmp_char = list_next(itr)))
		list_append(ret_list, xstrdup(tmp_char));

	list_iterator_destroy(itr);

	return ret_list;
}

extern void sacctmgr_print_coord_list(
	print_field_t *field, List value, int last)
{
	int abs_len = abs(field->len);
	ListIterator itr = NULL;
	char *print_this = NULL;
	slurmdb_coord_rec_t *object = NULL;

	if (!value || !list_count(value)) {
		if (print_fields_parsable_print)
			print_this = xstrdup("");
		else
			print_this = xstrdup(" ");
	} else {
		list_sort(value, (ListCmpF)sort_coord_list);
		itr = list_iterator_create(value);
		while((object = list_next(itr))) {
			if (print_this)
				xstrfmtcat(print_this, ",%s",
					   object->name);
			else
				print_this = xstrdup(object->name);
		}
		list_iterator_destroy(itr);
	}

	if (print_fields_parsable_print == PRINT_FIELDS_PARSABLE_NO_ENDING
	    && last)
		printf("%s", print_this);
	else if (print_fields_parsable_print)
		printf("%s|", print_this);
	else if (print_this) {
		if (strlen(print_this) > abs_len)
			print_this[abs_len-1] = '+';

		if (field->len == abs_len)
			printf("%*.*s ", abs_len, abs_len, print_this);
		else
			printf("%-*.*s ", abs_len, abs_len, print_this);
	}
	xfree(print_this);
}

extern void sacctmgr_print_qos_list(print_field_t *field, List qos_list,
				    List value, int last)
{
	int abs_len = abs(field->len);
	char *print_this = NULL;

	print_this = get_qos_complete_str(qos_list, value);

	if (print_fields_parsable_print == PRINT_FIELDS_PARSABLE_NO_ENDING
	    && last)
		printf("%s", print_this);
	else if (print_fields_parsable_print)
		printf("%s|", print_this);
	else {
		if (strlen(print_this) > abs_len)
			print_this[abs_len-1] = '+';

		if (field->len == abs_len)
			printf("%*.*s ", abs_len, abs_len, print_this);
		else
			printf("%-*.*s ", abs_len, abs_len, print_this);
	}
	xfree(print_this);
}

extern void sacctmgr_print_qos_bitstr(print_field_t *field, List qos_list,
				      bitstr_t *value, int last)
{
	int abs_len = abs(field->len);
	char *print_this = NULL;

	print_this = get_qos_complete_str_bitstr(qos_list, value);

	if (print_fields_parsable_print == PRINT_FIELDS_PARSABLE_NO_ENDING
	    && last)
		printf("%s", print_this);
	else if (print_fields_parsable_print)
		printf("%s|", print_this);
	else {
		if (strlen(print_this) > abs_len)
			print_this[abs_len-1] = '+';

		if (field->len == abs_len)
			printf("%*.*s ", abs_len, abs_len, print_this);
		else
			printf("%-*.*s ", abs_len, abs_len, print_this);
	}
	xfree(print_this);
}

extern void sacctmgr_print_assoc_limits(slurmdb_association_rec_t *assoc)
{
	if (!assoc)
		return;

	if (assoc->shares_raw == INFINITE)
		printf("  Fairshare     = NONE\n");
	else if (assoc->shares_raw != NO_VAL)
		printf("  Fairshare     = %u\n", assoc->shares_raw);

	if (assoc->grp_cpu_mins == INFINITE)
		printf("  GrpCPUMins    = NONE\n");
	else if (assoc->grp_cpu_mins != NO_VAL)
		printf("  GrpCPUMins    = %"PRIu64"\n",
		       assoc->grp_cpu_mins);

	if (assoc->grp_cpus == INFINITE)
		printf("  GrpCPUs       = NONE\n");
	else if (assoc->grp_cpus != NO_VAL)
		printf("  GrpCPUs       = %u\n", assoc->grp_cpus);

	if (assoc->grp_jobs == INFINITE)
		printf("  GrpJobs       = NONE\n");
	else if (assoc->grp_jobs != NO_VAL)
		printf("  GrpJobs       = %u\n", assoc->grp_jobs);

	if (assoc->grp_mem == INFINITE)
		printf("  GrpMemory     = NONE\n");
	else if (assoc->grp_mem != NO_VAL)
		printf("  GrpMemory     = %u\n", assoc->grp_mem);

	if (assoc->grp_nodes == INFINITE)
		printf("  GrpNodes      = NONE\n");
	else if (assoc->grp_nodes != NO_VAL)
		printf("  GrpNodes      = %u\n", assoc->grp_nodes);

	if (assoc->grp_submit_jobs == INFINITE)
		printf("  GrpSubmitJobs = NONE\n");
	else if (assoc->grp_submit_jobs != NO_VAL)
		printf("  GrpSubmitJobs = %u\n",
		       assoc->grp_submit_jobs);

	if (assoc->grp_wall == INFINITE)
		printf("  GrpWall       = NONE\n");
	else if (assoc->grp_wall != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) assoc->grp_wall,
			      time_buf, sizeof(time_buf));
		printf("  GrpWall       = %s\n", time_buf);
	}

	if (assoc->max_cpu_mins_pj == (uint64_t)INFINITE)
		printf("  MaxCPUMins    = NONE\n");
	else if (assoc->max_cpu_mins_pj != (uint64_t)NO_VAL)
		printf("  MaxCPUMins    = %"PRIu64"\n",
		       assoc->max_cpu_mins_pj);

	if (assoc->max_cpus_pj == INFINITE)
		printf("  MaxCPUs       = NONE\n");
	else if (assoc->max_cpus_pj != NO_VAL)
		printf("  MaxCPUs       = %u\n", assoc->max_cpus_pj);

	if (assoc->max_jobs == INFINITE)
		printf("  MaxJobs       = NONE\n");
	else if (assoc->max_jobs != NO_VAL)
		printf("  MaxJobs       = %u\n", assoc->max_jobs);

	if (assoc->max_nodes_pj == INFINITE)
		printf("  MaxNodes      = NONE\n");
	else if (assoc->max_nodes_pj != NO_VAL)
		printf("  MaxNodes      = %u\n", assoc->max_nodes_pj);

	if (assoc->max_submit_jobs == INFINITE)
		printf("  MaxSubmitJobs = NONE\n");
	else if (assoc->max_submit_jobs != NO_VAL)
		printf("  MaxSubmitJobs = %u\n",
		       assoc->max_submit_jobs);

	if (assoc->max_wall_pj == INFINITE)
		printf("  MaxWall       = NONE\n");
	else if (assoc->max_wall_pj != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) assoc->max_wall_pj,
			      time_buf, sizeof(time_buf));
		printf("  MaxWall       = %s\n", time_buf);
	}

	if (assoc->qos_list) {
		if (!g_qos_list)
			g_qos_list =
				acct_storage_g_get_qos(db_conn, my_uid, NULL);
		char *temp_char = get_qos_complete_str(g_qos_list,
						       assoc->qos_list);
		if (temp_char) {
			printf("  QOS           = %s\n", temp_char);
			xfree(temp_char);
		}
	}
}

extern void sacctmgr_print_qos_limits(slurmdb_qos_rec_t *qos)
{
	if (!qos)
		return;

	if (qos->preempt_list && !g_qos_list)
		g_qos_list = acct_storage_g_get_qos(db_conn, my_uid, NULL);

	if (qos->flags && (qos->flags != QOS_FLAG_NOTSET)) {
		char *tmp_char = slurmdb_qos_flags_str(qos->flags);
		printf("  Flags          = %s\n", tmp_char);
		xfree(tmp_char);
	}

	if (qos->grace_time == INFINITE)
		printf("  GraceTime      = NONE\n");
	else if (qos->grace_time != NO_VAL)
		printf("  GraceTime      = %d\n", qos->grace_time);

	if (qos->grp_cpu_mins == INFINITE)
		printf("  GrpCPUMins     = NONE\n");
	else if (qos->grp_cpu_mins != NO_VAL)
		printf("  GrpCPUMins     = %"PRIu64"\n",
		       qos->grp_cpu_mins);

	if (qos->grp_cpus == INFINITE)
		printf("  GrpCPUs        = NONE\n");
	else if (qos->grp_cpus != NO_VAL)
		printf("  GrpCPUs        = %u\n", qos->grp_cpus);

	if (qos->grp_jobs == INFINITE)
		printf("  GrpJobs        = NONE\n");
	else if (qos->grp_jobs != NO_VAL)
		printf("  GrpJobs        = %u\n", qos->grp_jobs);

	if (qos->grp_mem == INFINITE)
		printf("  GrpMemory      = NONE\n");
	else if (qos->grp_mem != NO_VAL)
		printf("  GrpMemory      = %u\n", qos->grp_mem);

	if (qos->grp_nodes == INFINITE)
		printf("  GrpNodes       = NONE\n");
	else if (qos->grp_nodes != NO_VAL)
		printf("  GrpNodes       = %u\n", qos->grp_nodes);

	if (qos->grp_submit_jobs == INFINITE)
		printf("  GrpSubmitJobs  = NONE\n");
	else if (qos->grp_submit_jobs != NO_VAL)
		printf("  GrpSubmitJobs  = %u\n",
		       qos->grp_submit_jobs);

	if (qos->grp_wall == INFINITE)
		printf("  GrpWall        = NONE\n");
	else if (qos->grp_wall != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) qos->grp_wall,
			      time_buf, sizeof(time_buf));
		printf("  GrpWall        = %s\n", time_buf);
	}

	if (qos->max_cpu_mins_pj == (uint64_t)INFINITE)
		printf("  MaxCPUMins     = NONE\n");
	else if (qos->max_cpu_mins_pj != (uint64_t)NO_VAL)
		printf("  MaxCPUMins     = %"PRIu64"\n",
		       qos->max_cpu_mins_pj);

	if (qos->max_cpus_pj == INFINITE)
		printf("  MaxCPUs        = NONE\n");
	else if (qos->max_cpus_pj != NO_VAL)
		printf("  MaxCPUs        = %u\n", qos->max_cpus_pj);

	if (qos->max_cpus_pu == INFINITE)
		printf("  MaxCPUsPerUser        = NONE\n");
	else if (qos->max_cpus_pu != NO_VAL)
		printf("  MaxCPUsPerUser        = %u\n", qos->max_cpus_pu);

	if (qos->max_jobs_pu == INFINITE)
		printf("  MaxJobs        = NONE\n");
	else if (qos->max_jobs_pu != NO_VAL)
		printf("  MaxJobs        = %u\n", qos->max_jobs_pu);

	if (qos->max_nodes_pj == INFINITE)
		printf("  MaxNodes       = NONE\n");
	else if (qos->max_nodes_pj != NO_VAL)
		printf("  MaxNodes       = %u\n", qos->max_nodes_pj);

	if (qos->max_nodes_pu == INFINITE)
		printf("  MaxNodesPerUser       = NONE\n");
	else if (qos->max_nodes_pu != NO_VAL)
		printf("  MaxNodesPerUser       = %u\n", qos->max_nodes_pu);

	if (qos->max_submit_jobs_pu == INFINITE)
		printf("  MaxSubmitJobs  = NONE\n");
	else if (qos->max_submit_jobs_pu != NO_VAL)
		printf("  MaxSubmitJobs  = %u\n",
		       qos->max_submit_jobs_pu);

	if (qos->max_wall_pj == INFINITE)
		printf("  MaxWall        = NONE\n");
	else if (qos->max_wall_pj != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) qos->max_wall_pj,
			      time_buf, sizeof(time_buf));
		printf("  MaxWall        = %s\n", time_buf);
	}

	if (qos->preempt_list) {
		char *temp_char = get_qos_complete_str(g_qos_list,
						       qos->preempt_list);
		if (temp_char) {
			printf("  Preempt        = %s\n", temp_char);
			xfree(temp_char);
		}
	}

	if (qos->preempt_mode && (qos->preempt_mode != (uint16_t)NO_VAL)) {
		printf("  PreemptMode    = %s\n",
		       preempt_mode_string(qos->preempt_mode));
	}

	if (qos->priority == INFINITE)
		printf("  Priority       = NONE\n");
	else if (qos->priority != NO_VAL)
		printf("  Priority       = %d\n", qos->priority);

}

extern int sort_coord_list(void *a, void *b)
{
	slurmdb_coord_rec_t *coord_a = *(slurmdb_coord_rec_t **)a;
	slurmdb_coord_rec_t *coord_b = *(slurmdb_coord_rec_t **)b;
	int diff;

	diff = strcmp(coord_a->name, coord_b->name);

	if (diff < 0)
		return -1;
	else if (diff > 0)
		return 1;

	return 0;
}

extern List sacctmgr_process_format_list(List format_list)
{
	List print_fields_list = list_create(destroy_print_field);
	ListIterator itr = list_iterator_create(format_list);
	print_field_t *field = NULL;
	char *object = NULL;

	while((object = list_next(itr))) {
		if (!(field = _get_print_field(object)))
			exit(1);

		list_append(print_fields_list, field);
	}
	list_iterator_destroy(itr);

	return print_fields_list;
}
