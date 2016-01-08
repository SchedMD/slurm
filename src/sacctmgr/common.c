/*****************************************************************************\
 *  common.c - definitions for functions common to all modules in sacctmgr.
 *****************************************************************************
 *  Copyright (C) 2010-2015 SchedMD LLC.
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
	} else if (!strncasecmp("Allowed", object, MAX(command_len, 2))) {
		field->type = PRINT_ALLOWED;
		field->name = xstrdup("% Allowed");
		field->len = 10;
		field->print_routine = print_fields_uint16;
	} else if (!strncasecmp("Associations", object, MAX(command_len, 2))) {
		field->type = PRINT_ASSOC_NAME;
		field->name = xstrdup("Assocs");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("TRES", object, MAX(command_len, 2))) {
		field->type = PRINT_TRES;
		field->name = xstrdup("TRES");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("Classification", object,
				MAX(command_len, 3))) {
		field->type = PRINT_CLASS;
		field->name = xstrdup("Class");
		field->len = 9;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("ClusterNodes", object, MAX(command_len, 8))
		   || !strncasecmp("NodeNames", object, MAX(command_len, 8))) {
		field->type = PRINT_CLUSTER_NODES;
		field->name = xstrdup("Cluster Nodes");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("Clusters", object, MAX(command_len, 2))) {
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
	} else if (!strncasecmp("Count", object, MAX(command_len, 3))) {
		field->type = PRINT_COUNT;
		field->name = xstrdup("Count");
		field->len = 6;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("CountAllowed", object, MAX(command_len, 6))) {
		field->type = PRINT_CALLOWED;
		field->name = xstrdup("# Allowed");
		field->len = 10;
		field->print_routine = print_fields_uint32;
	} else if (!strncasecmp("CountUsed", object, MAX(command_len, 6))) {
		field->type = PRINT_CALLOWED;
		field->name = xstrdup("# Used");
		field->len = 10;
		field->print_routine = print_fields_uint32;
	} else if (!strncasecmp("CPUCount", object,
				MAX(command_len, 2))) {
		field->type = PRINT_CPUS;
		field->name = xstrdup("CPU Cnt");
		field->len = 7;
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
	} else if (!strncasecmp("Federation", object, MAX(command_len, 2))) {
		field->type = PRINT_FEDERATION;
		field->name = xstrdup("Federation");
		field->len = 10;
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
	} else if (!strncasecmp("GrpCPUs", object, MAX(command_len, 6))) {
		field->type = PRINT_GRPC;
		field->name = xstrdup("GrpCPUs");
		field->len = 8;
		field->print_routine = print_fields_uint64;
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
	} else if (!strncasecmp("GrpTRES", object, MAX(command_len, 7))) {
		field->type = PRINT_GRPT;
		field->name = xstrdup("GrpTRES");
		field->len = 13;
		field->print_routine = sacctmgr_print_tres;
	} else if (!strncasecmp("GrpTRESMins", object, MAX(command_len, 7))) {
		field->type = PRINT_GRPTM;
		field->name = xstrdup("GrpTRESMins");
		field->len = 13;
		field->print_routine = sacctmgr_print_tres;
	} else if (!strncasecmp("GrpTRESRunMins",
				object, MAX(command_len, 7))) {
		field->type = PRINT_GRPTRM;
		field->name = xstrdup("GrpTRESRunMins");
		field->len = 13;
		field->print_routine = sacctmgr_print_tres;
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
	} else if (!strncasecmp("servertype", object, MAX(command_len, 10))) {
		field->type = PRINT_SERVERTYPE;
		field->name = xstrdup("ServerType");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("MaxCPUMinsPerJob", object,
				MAX(command_len, 7))) {
		field->type = PRINT_MAXCM;
		field->name = xstrdup("MaxCPUMins");
		field->len = 11;
		field->print_routine = print_fields_uint64;
	} else if (!strncasecmp("MaxCPURunMinsPerUser", object,
				MAX(command_len, 7)) ||
		   !strncasecmp("MaxCPURunMinsPU", object,
				MAX(command_len, 7))) {
		field->type = PRINT_MAXCRM;
		field->name = xstrdup("MaxCPURunMinsPU");
		field->len = 15;
		field->print_routine = print_fields_uint64;
	} else if (!strncasecmp("MaxCPUsPerJob", object, MAX(command_len, 7))) {
		field->type = PRINT_MAXC;
		field->name = xstrdup("MaxCPUs");
		field->len = 8;
		field->print_routine = print_fields_uint64;
	} else if (!strncasecmp("MaxCPUsPerUser", object,
				MAX(command_len, 11)) ||
		   !strncasecmp("MaxCPUsPU", object,
				MAX(command_len, 9))) {
		field->type = PRINT_MAXCU;
		field->name = xstrdup("MaxCPUsPU");
		field->len = 9;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("MaxTRESPerJob",
				object, MAX(command_len, 7))) {
		field->type = PRINT_MAXT;
		field->name = xstrdup("MaxTRES");
		field->len = 13;
		field->print_routine = sacctmgr_print_tres;
	} else if (!strncasecmp("MaxTRESPerNode",
				object, MAX(command_len, 11))) {
		field->type = PRINT_MAXTN;
		field->name = xstrdup("MaxTRESPerNode");
		field->len = 14;
		field->print_routine = sacctmgr_print_tres;
	} else if (!strncasecmp("MaxTRESMinsPerJob", object,
				MAX(command_len, 8))) {
		field->type = PRINT_MAXTM;
		field->name = xstrdup("MaxTRESMins");
		field->len = 13;
		field->print_routine = sacctmgr_print_tres;
	} else if (!strncasecmp("MaxTRESRunMinsPerAccount", object,
				MAX(command_len, 18)) ||
		   !strncasecmp("MaxTRESRunMinsPerAcct", object,
				MAX(command_len, 18)) ||
		   !strncasecmp("MaxTRESRunMinsPA", object,
				MAX(command_len, 15))) {
		field->type = PRINT_MAXTRMA;
		field->name = xstrdup("MaxTRESRunMinsPA");
		field->len = 15;
		field->print_routine = sacctmgr_print_tres;
	} else if (!strncasecmp("MaxTRESRunMinsPerUser", object,
				MAX(command_len, 8)) ||
		   !strncasecmp("MaxTRESRunMinsPU", object,
				MAX(command_len, 8))) {
		field->type = PRINT_MAXTRM;
		field->name = xstrdup("MaxTRESRunMinsPU");
		field->len = 15;
		field->print_routine = sacctmgr_print_tres;
	} else if (!strncasecmp("MaxTRESPerAccount", object,
				MAX(command_len, 11)) ||
		   !strncasecmp("MaxTRESPerAcct", object,
				MAX(command_len, 11)) ||
		   !strncasecmp("MaxTRESPA", object,
				MAX(command_len, 9))) {
		field->type = PRINT_MAXTA;
		field->name = xstrdup("MaxTRESPA");
		field->len = 13;
		field->print_routine = sacctmgr_print_tres;
	} else if (!strncasecmp("MaxTRESPerUser", object,
				MAX(command_len, 11))) {
		field->type = PRINT_MAXTU;
		field->name = xstrdup("MaxTRESPU");
		field->len = 13;
		field->print_routine = sacctmgr_print_tres;
	} else if (!strncasecmp("MaxJobs", object, MAX(command_len, 4))) {
		field->type = PRINT_MAXJ;
		field->name = xstrdup("MaxJobs");
		field->len = 7;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("MaxJobsPerAccount", object,
				MAX(command_len, 11)) ||
		   !strncasecmp("MaxJobsPerAcct", object,
				MAX(command_len, 11)) ||
		   !strncasecmp("MaxJobsPA", object,
				MAX(command_len, 9))) {
		field->type = PRINT_MAXJA;
		field->name = xstrdup("MaxJobsPA");
		field->len = 9;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("MaxJobsPerUser", object,
				MAX(command_len, 8)) ||
		   !strncasecmp("MaxJobsPU", object,
				MAX(command_len, 8))) {
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
				MAX(command_len, 12)) ||
		   !strncasecmp("MaxNodesPU", object,
				MAX(command_len, 10))) {
		field->type = PRINT_MAXNU;
		field->name = xstrdup("MaxNodesPU");
		field->len = 10;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("MaxSubmitJobs", object, MAX(command_len, 4))) {
		field->type = PRINT_MAXS;
		field->name = xstrdup("MaxSubmit");
		field->len = 9;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("MaxSubmitJobsPerAccount", object,
				MAX(command_len, 17)) ||
		   !strncasecmp("MaxSubmitJobsPerAcct", object,
				MAX(command_len, 17)) ||
		   !strncasecmp("MaxSubmitJobsPA", object,
				MAX(command_len, 15))) {
		field->type = PRINT_MAXSA;
		field->name = xstrdup("MaxSubmitPA");
		field->len = 11;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("MaxSubmitJobsPerUser", object,
				MAX(command_len, 10)) ||
		   !strncasecmp("MaxSubmitJobsPU", object,
				MAX(command_len, 10))) {
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
	} else if (!strncasecmp("MinCPUsPerJob", object, MAX(command_len, 7))) {
		field->type = PRINT_MINC;
		field->name = xstrdup("MinCPUs");
		field->len = 8;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("MinTRESPerJob", object, MAX(command_len, 7))) {
		field->type = PRINT_MINT;
		field->name = xstrdup("MinTRES");
		field->len = 13;
		field->print_routine = sacctmgr_print_tres;
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
	} else if (!strncasecmp("Reason", object,
				MAX(command_len, 1))) {
		field->type = PRINT_REASON;
		field->name = xstrdup("Reason");
		field->len = 30;
		field->print_routine = print_fields_str;
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
	} else if (!strncasecmp("Server", object, MAX(command_len, 3))) {
		field->type = PRINT_SERVER;
		field->name = xstrdup("Server");
		field->len = 10;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("Share", object, MAX(command_len, 1))
		   || !strncasecmp("FairShare", object, MAX(command_len, 2))) {
		field->type = PRINT_FAIRSHARE;
		field->name = xstrdup("Share");
		field->len = 9;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("StateRaw", object,
				MAX(command_len, 6))) {
		field->type = PRINT_STATERAW;
		field->name = xstrdup("StateRaw");
		field->len = 8;
		field->print_routine = print_fields_uint;
	} else if (!strncasecmp("State", object, MAX(command_len, 1))) {
		field->type = PRINT_STATE;
		field->name = xstrdup("State");
		field->len = 6;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("TimeStamp", object, MAX(command_len, 2))) {
		field->type = PRINT_TS;
		field->name = xstrdup("Time");
		field->len = 19;
		field->print_routine = print_fields_date;
	} else if (!strncasecmp("TimeStart", object, MAX(command_len, 7)) ||
		   !strncasecmp("Start", object, MAX(command_len, 3))) {
		field->type = PRINT_TIMESTART;
		field->name = xstrdup("TimeStart");
		field->len = 19;
		field->print_routine = print_fields_date;
	} else if (!strncasecmp("TimeEnd", object, MAX(command_len, 5)) ||
		   !strncasecmp("End", object, MAX(command_len, 2))) {
		field->type = PRINT_TIMEEND;
		field->name = xstrdup("TimeEnd");
		field->len = 19;
		field->print_routine = print_fields_date;
	} else if (!strncasecmp("TRES", object,
				MAX(command_len, 2))) {
		field->type = PRINT_TRES;
		field->name = xstrdup("TRES");
		field->len = 20;
		field->print_routine = print_fields_str;
	} else if (!strncasecmp("Type", object, MAX(command_len, 2))) {
		field->type = PRINT_TYPE;
		field->name = xstrdup("Type");
		field->len = 8;
		field->print_routine = print_fields_str;
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
	} else if (!strncasecmp("Allocated", object, MAX(command_len, 7))) {
		field->type = PRINT_ALLOCATED;
		field->name = xstrdup("% Allocated");
		field->len = 11;
		field->print_routine = print_fields_uint16;
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

		c = (char) getchar();
		printf("\n");
	}
	_nonblock(0);
	if (ans <= 0)
		printf("timeout\n");
	else if (c == 'Y' || c == 'y')
		return 1;

	return 0;
}

extern int sacctmgr_remove_assoc_usage(slurmdb_assoc_cond_t *assoc_cond)
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
	slurmdb_assoc_rec_t* rec = NULL;
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

	local_assoc_list = acct_storage_g_get_assocs(
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
					rec = sacctmgr_find_assoc_from_list(
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
				rec = sacctmgr_find_assoc_from_list(
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
		FREE_NULL_LIST(update_list);
	}
end_it:
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);
	if (itr3)
		list_iterator_destroy(itr3);

	FREE_NULL_LIST(local_assoc_list);
	FREE_NULL_LIST(local_cluster_list);

	return rc;
}

extern int sacctmgr_remove_qos_usage(slurmdb_qos_cond_t *qos_cond)
{
	List update_list = NULL;
	List cluster_list;
	List local_qos_list = NULL;
	List local_cluster_list = NULL;
	ListIterator itr = NULL, itr2 = NULL;
	char *qos_name = NULL, *cluster_name = NULL;
	slurmdb_qos_rec_t* rec = NULL;
	slurmdb_cluster_rec_t* cluster_rec = NULL;
	slurmdb_update_object_t* update_obj = NULL;
	slurmdb_cluster_cond_t cluster_cond;
	int rc = SLURM_SUCCESS;
	bool free_cluster_name = 0;

	cluster_list = qos_cond->description_list;
	qos_cond->description_list = NULL;

	if (!cluster_list)
		cluster_list = list_create(slurm_destroy_char);

	if (!list_count(cluster_list)) {
		char *temp = slurm_get_cluster_name();
		if (temp) {
			printf("No cluster specified, resetting "
			       "on local cluster %s\n", temp);
			list_append(cluster_list, temp);
		}
		if (!list_count(cluster_list)) {
			error("A cluster name is required to remove usage");
			return SLURM_ERROR;
		}
	}

	if (!commit_check("Would you like to reset usage?")) {
		printf(" Changes Discarded\n");
		return rc;
	}

	local_qos_list = acct_storage_g_get_qos(db_conn, my_uid, qos_cond);

	slurmdb_init_cluster_cond(&cluster_cond, 0);
	cluster_cond.cluster_list = cluster_list;
	local_cluster_list = acct_storage_g_get_clusters(
		db_conn, my_uid, &cluster_cond);

	itr = list_iterator_create(cluster_list);
	itr2 = list_iterator_create(qos_cond->name_list);
	while ((cluster_name = list_next(itr))) {
		cluster_rec = sacctmgr_find_cluster_from_list(
			local_cluster_list, cluster_name);
		if (!cluster_rec) {
			error("Failed to find cluster %s in database",
			      cluster_name);
			rc = SLURM_ERROR;
			goto end_it;
		}

		update_list = list_create(slurmdb_destroy_update_object);
		update_obj = xmalloc(sizeof(slurmdb_update_object_t));
		update_obj->type = SLURMDB_REMOVE_QOS_USAGE;
		update_obj->objects = list_create(NULL);

		while ((qos_name = list_next(itr2))) {
			rec = sacctmgr_find_qos_from_list(
				local_qos_list, qos_name);
			if (!rec) {
				error("Failed to find QOS %s", qos_name);
				rc = SLURM_ERROR;
				goto end_it;
			}
			list_append(update_obj->objects, rec);
		}
		list_iterator_reset(itr2);

		if (list_count(update_obj->objects)) {
			list_append(update_list, update_obj);
			rc = slurmdb_send_accounting_update(
				update_list, cluster_name,
				cluster_rec->control_host,
				cluster_rec->control_port,
				cluster_rec->rpc_version);
		}
		FREE_NULL_LIST(update_list);
	}
end_it:
	list_iterator_destroy(itr);
	list_iterator_destroy(itr2);

	FREE_NULL_LIST(update_list);
	FREE_NULL_LIST(local_qos_list);

	if (free_cluster_name)
		xfree(cluster_name);

	return rc;
}

extern slurmdb_assoc_rec_t *sacctmgr_find_account_base_assoc(
	char *account, char *cluster)
{
	slurmdb_assoc_rec_t *assoc = NULL;
	char *temp = "root";
	slurmdb_assoc_cond_t assoc_cond;
	List assoc_list = NULL;

	if (!cluster)
		return NULL;

	if (account)
		temp = account;

	memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));
	assoc_cond.acct_list = list_create(NULL);
	list_append(assoc_cond.cluster_list, temp);
	assoc_cond.cluster_list = list_create(NULL);
	list_append(assoc_cond.cluster_list, cluster);
	assoc_cond.user_list = list_create(NULL);
	list_append(assoc_cond.user_list, "");

	assoc_list = acct_storage_g_get_assocs(db_conn, my_uid,
						     &assoc_cond);

	FREE_NULL_LIST(assoc_cond.acct_list);
	FREE_NULL_LIST(assoc_cond.cluster_list);
	FREE_NULL_LIST(assoc_cond.user_list);

	if (assoc_list)
		assoc = list_pop(assoc_list);

	FREE_NULL_LIST(assoc_list);

	return assoc;
}

extern slurmdb_assoc_rec_t *sacctmgr_find_root_assoc(char *cluster)
{
	return sacctmgr_find_account_base_assoc(NULL, cluster);
}

extern slurmdb_user_rec_t *sacctmgr_find_user(char *name)
{
	slurmdb_user_rec_t *user = NULL;
	slurmdb_user_cond_t user_cond;
	slurmdb_assoc_cond_t assoc_cond;
	List user_list = NULL;

	if (!name)
		return NULL;

	memset(&user_cond, 0, sizeof(slurmdb_user_cond_t));
	memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));
	assoc_cond.user_list = list_create(NULL);
	list_append(assoc_cond.user_list, name);
	user_cond.assoc_cond = &assoc_cond;

	user_list = acct_storage_g_get_users(db_conn, my_uid,
					     &user_cond);

	FREE_NULL_LIST(assoc_cond.user_list);

	if (user_list)
		user = list_pop(user_list);

	FREE_NULL_LIST(user_list);

	return user;
}

extern slurmdb_account_rec_t *sacctmgr_find_account(char *name)
{
	slurmdb_account_rec_t *account = NULL;
	slurmdb_account_cond_t account_cond;
	slurmdb_assoc_cond_t assoc_cond;
	List account_list = NULL;

	if (!name)
		return NULL;

	memset(&account_cond, 0, sizeof(slurmdb_account_cond_t));
	memset(&assoc_cond, 0, sizeof(slurmdb_assoc_cond_t));
	assoc_cond.acct_list = list_create(NULL);
	list_append(assoc_cond.acct_list, name);
	account_cond.assoc_cond = &assoc_cond;

	account_list = acct_storage_g_get_accounts(db_conn, my_uid,
						   &account_cond);

	FREE_NULL_LIST(assoc_cond.acct_list);

	if (account_list)
		account = list_pop(account_list);

	FREE_NULL_LIST(account_list);

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

	FREE_NULL_LIST(cluster_cond.cluster_list);

	if (cluster_list)
		cluster = list_pop(cluster_list);

	FREE_NULL_LIST(cluster_list);

	return cluster;
}

extern slurmdb_assoc_rec_t *sacctmgr_find_assoc_from_list(
	List assoc_list, char *user, char *account,
	char *cluster, char *partition)
{
	ListIterator itr = NULL;
	slurmdb_assoc_rec_t * assoc = NULL;

	if (!assoc_list)
		return NULL;

	itr = list_iterator_create(assoc_list);
	while((assoc = list_next(itr))) {
		if (((!user && assoc->user)
		     || (user && (!assoc->user
				  || xstrcasecmp(user, assoc->user))))
		    || (account && (!assoc->acct
				    || xstrcasecmp(account, assoc->acct)))
		    || ((!cluster && assoc->cluster)
			|| (cluster && (!assoc->cluster
					|| xstrcasecmp(cluster,
						       assoc->cluster)))))
			continue;
		else if (partition) {
			if (partition[0] != '*'
			    && (!assoc->partition
				|| xstrcasecmp(partition, assoc->partition)))
				continue;
		} else if (assoc->partition)
			continue;

		break;
	}
	list_iterator_destroy(itr);

	return assoc;
}

extern slurmdb_assoc_rec_t *sacctmgr_find_account_base_assoc_from_list(
	List assoc_list, char *account, char *cluster)
{
	ListIterator itr = NULL;
	slurmdb_assoc_rec_t *assoc = NULL;
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
		    || xstrcasecmp(temp, assoc->acct)
		    || xstrcasecmp(cluster, assoc->cluster))
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
		if (!xstrcasecmp(working_name, qos->name))
			break;
	}
	list_iterator_destroy(itr);

	return qos;

}

extern slurmdb_res_rec_t *sacctmgr_find_res_from_list(
	List res_list, uint32_t id, char *name, char *server)
{
	ListIterator itr = NULL;
	slurmdb_res_rec_t *res = NULL;

	if ((id == NO_VAL) && (!name || !res_list))
		return NULL;

	itr = list_iterator_create(res_list);
	while ((res = list_next(itr))) {
		if ((id == res->id)
		    || (name && server
			&& !xstrcasecmp(server, res->server)
			&& !xstrcasecmp(name, res->name)))
			break;
	}
	list_iterator_destroy(itr);

	return res;
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
		if (!xstrcasecmp(name, user->name))
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
		if (!xstrcasecmp(name, account->name))
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
		if (!xstrcasecmp(name, cluster->name))
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
				  || xstrcasecmp(user, wckey->user))))
		    || (name && (!wckey->name
				 || xstrcasecmp(name, wckey->name)))
		    || ((!cluster && wckey->cluster)
			|| (cluster && (!wckey->cluster
					|| xstrcasecmp(cluster,
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
		*out_value = INFINITE64; /* flag to clear */
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
						if (!xstrcasecmp(tmp_char,
								 name))
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
				if (!xstrcasecmp(tmp_char, name))
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

extern void sacctmgr_print_tres(print_field_t *field, char *tres_simple_str,
				int last)
{
	int abs_len = abs(field->len);
	char *print_this;

	sacctmgr_initialize_g_tres_list();

	print_this = slurmdb_make_tres_string_from_simple(
		tres_simple_str, g_tres_list, NO_VAL, CONVERT_NUM_UNIT_EXACT);


	if (!print_this)
		print_this = xstrdup("");

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

extern void sacctmgr_print_assoc_limits(slurmdb_assoc_rec_t *assoc)
{
	char *tmp_char;

	if (!assoc)
		return;

	if (assoc->shares_raw == INFINITE)
		printf("  Fairshare     = NONE\n");
	else if (assoc->shares_raw == SLURMDB_FS_USE_PARENT)
		printf("  Fairshare     = parent\n");
	else if (assoc->shares_raw != NO_VAL)
		printf("  Fairshare     = %u\n", assoc->shares_raw);

	if (assoc->grp_jobs == INFINITE)
		printf("  GrpJobs       = NONE\n");
	else if (assoc->grp_jobs != NO_VAL)
		printf("  GrpJobs       = %u\n", assoc->grp_jobs);

	if (assoc->grp_submit_jobs == INFINITE)
		printf("  GrpSubmitJobs = NONE\n");
	else if (assoc->grp_submit_jobs != NO_VAL)
		printf("  GrpSubmitJobs = %u\n",
		       assoc->grp_submit_jobs);

	if (assoc->grp_tres) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->grp_tres, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT);
		printf("  GrpTRES       = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (assoc->grp_tres_mins) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->grp_tres_mins, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT);;
		printf("  GrpTRESMins   = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (assoc->grp_tres_run_mins) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->grp_tres_run_mins, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT);
		printf("  GrpTRESRunMins= %s\n", tmp_char);
		xfree(tmp_char);
	}

	if (assoc->grp_wall == INFINITE)
		printf("  GrpWall       = NONE\n");
	else if (assoc->grp_wall != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) assoc->grp_wall,
			      time_buf, sizeof(time_buf));
		printf("  GrpWall       = %s\n", time_buf);
	}

	if (assoc->max_jobs == INFINITE)
		printf("  MaxJobs       = NONE\n");
	else if (assoc->max_jobs != NO_VAL)
		printf("  MaxJobs       = %u\n", assoc->max_jobs);

	if (assoc->max_submit_jobs == INFINITE)
		printf("  MaxSubmitJobs = NONE\n");
	else if (assoc->max_submit_jobs != NO_VAL)
		printf("  MaxSubmitJobs = %u\n",
		       assoc->max_submit_jobs);

	if (assoc->max_tres_pj) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->max_tres_pj, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT);
		printf("  MaxTRES       = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (assoc->max_tres_pn) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->max_tres_pn, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT);
		printf("  MaxTRESPerNode= %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (assoc->max_tres_mins_pj) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->max_tres_mins_pj, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT);
		printf("  MaxTRESMins   = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (assoc->max_tres_run_mins) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			assoc->max_tres_run_mins, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT);
		printf("  MaxTRESRUNMins= %s\n", tmp_char);
		xfree(tmp_char);
	}

	if (assoc->max_wall_pj == INFINITE)
		printf("  MaxWall       = NONE\n");
	else if (assoc->max_wall_pj != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) assoc->max_wall_pj,
			      time_buf, sizeof(time_buf));
		printf("  MaxWall       = %s\n", time_buf);
	}

	if (assoc->parent_acct)
		printf("  Parent        = %s\n", assoc->parent_acct);

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

	if (assoc->def_qos_id != NO_VAL) {
		if (!g_qos_list)
			g_qos_list =
				acct_storage_g_get_qos(db_conn, my_uid, NULL);
		printf("  DefQOS        = %s\n",
		       slurmdb_qos_str(g_qos_list, assoc->def_qos_id));
	}

}

extern void sacctmgr_print_qos_limits(slurmdb_qos_rec_t *qos)
{
	char *tmp_char;

	if (!qos)
		return;

	if (qos->preempt_list && !g_qos_list)
		g_qos_list = acct_storage_g_get_qos(db_conn, my_uid, NULL);

	if (qos->flags && (qos->flags != QOS_FLAG_NOTSET)) {
		char *tmp_char = slurmdb_qos_flags_str(qos->flags);
		printf("  Flags                    = %s\n", tmp_char);
		xfree(tmp_char);
	}

	if (qos->grace_time == INFINITE)
		printf("  GraceTime                = NONE\n");
	else if (qos->grace_time != NO_VAL)
		printf("  GraceTime                = %d\n", qos->grace_time);

	if (qos->grp_jobs == INFINITE)
		printf("  GrpJobs                  = NONE\n");
	else if (qos->grp_jobs != NO_VAL)
		printf("  GrpJobs                  = %u\n", qos->grp_jobs);

	if (qos->grp_submit_jobs == INFINITE)
		printf("  GrpSubmitJobs            = NONE\n");
	else if (qos->grp_submit_jobs != NO_VAL)
		printf("  GrpSubmitJobs            = %u\n",
		       qos->grp_submit_jobs);

	if (qos->grp_tres) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->grp_tres, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT);
		printf("  GrpTRES                  = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (qos->grp_tres_mins) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->grp_tres_mins, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT);
		printf("  GrpTRESMins              = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (qos->grp_tres_run_mins) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->grp_tres_run_mins, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT);
		printf("  GrpTRESRunMins           = %s\n", tmp_char);
		xfree(tmp_char);
	}

	if (qos->grp_wall == INFINITE)
		printf("  GrpWall                  = NONE\n");
	else if (qos->grp_wall != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) qos->grp_wall,
			      time_buf, sizeof(time_buf));
		printf("  GrpWall                  = %s\n", time_buf);
	}

	if (qos->max_jobs_pa == INFINITE)
		printf("  MaxJobsPerAccount        = NONE\n");
	else if (qos->max_jobs_pa != NO_VAL)
		printf("  MaxJobsPerAccount        = %u\n",
		       qos->max_jobs_pa);
	if (qos->max_jobs_pu == INFINITE)
		printf("  MaxJobsPerUser = NONE\n");
	else if (qos->max_jobs_pu != NO_VAL)
		printf("  MaxJobsPerUser = %u\n",
		       qos->max_jobs_pu);

	if (qos->max_submit_jobs_pa == INFINITE)
		printf("  MaxSubmitJobsPerAccount  = NONE\n");
	else if (qos->max_submit_jobs_pa != NO_VAL)
		printf("  MaxSubmitJobsPerAccount  = %u\n",
		       qos->max_submit_jobs_pa);

	if (qos->max_submit_jobs_pu == INFINITE)
		printf("  MaxSubmitJobsPerUser     = NONE\n");
	else if (qos->max_submit_jobs_pu != NO_VAL)
		printf("  MaxSubmitJobsPerUser     = %u\n",
		       qos->max_submit_jobs_pu);

	if (qos->max_tres_pa) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->max_tres_pa, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT);
		printf("  MaxTRESPerAccount        = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (qos->max_tres_pj) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->max_tres_pj, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT);
		printf("  MaxTRESPerJob            = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (qos->max_tres_pn) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->max_tres_pn, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT);
		printf("  MaxTRESPerNode           = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (qos->max_tres_pu) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->max_tres_pu, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT);
		printf("  MaxTRESPerUser           = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (qos->max_tres_mins_pj) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->max_tres_mins_pj, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT);
		printf("  MaxTRESMins              = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (qos->max_tres_run_mins_pa) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->max_tres_run_mins_pa, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT);
		printf("  MaxTRESRUNMinsPerAccount = %s\n", tmp_char);
		xfree(tmp_char);
	}
	if (qos->max_tres_run_mins_pu) {
		sacctmgr_initialize_g_tres_list();
		tmp_char = slurmdb_make_tres_string_from_simple(
			qos->max_tres_run_mins_pu, g_tres_list, NO_VAL,
			CONVERT_NUM_UNIT_EXACT);
		printf("  MaxTRESRUNMinsPerUser    = %s\n", tmp_char);
		xfree(tmp_char);
	}

	if (qos->max_wall_pj == INFINITE)
		printf("  MaxWall                  = NONE\n");
	else if (qos->max_wall_pj != NO_VAL) {
		char time_buf[32];
		mins2time_str((time_t) qos->max_wall_pj,
			      time_buf, sizeof(time_buf));
		printf("  MaxWall                  = %s\n", time_buf);
	}

	if (qos->preempt_list) {
		char *temp_char = get_qos_complete_str(g_qos_list,
						       qos->preempt_list);
		if (temp_char) {
			printf("  Preempt          = %s\n", temp_char);
			xfree(temp_char);
		}
	}

	if (qos->preempt_mode && (qos->preempt_mode != (uint16_t)NO_VAL)) {
		printf("  PreemptMode              = %s\n",
		       preempt_mode_string(qos->preempt_mode));
	}

	if (qos->priority == INFINITE)
		printf("  Priority                 = NONE\n");
	else if (qos->priority != NO_VAL)
		printf("  Priority                 = %d\n", qos->priority);

}

extern int sort_coord_list(void *a, void *b)
{
	slurmdb_coord_rec_t *coord_a = *(slurmdb_coord_rec_t **)a;
	slurmdb_coord_rec_t *coord_b = *(slurmdb_coord_rec_t **)b;
	int diff;

	diff = xstrcmp(coord_a->name, coord_b->name);

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

extern int sacctmgr_validate_cluster_list(List cluster_list)
{
	List temp_list = NULL;
	char *cluster = NULL;
	int rc = SLURM_SUCCESS;
	ListIterator itr = NULL, itr_c = NULL;

	if (cluster_list) {
		slurmdb_cluster_cond_t cluster_cond;
		slurmdb_init_cluster_cond(&cluster_cond, 0);
		cluster_cond.cluster_list = cluster_list;

		temp_list = acct_storage_g_get_clusters(db_conn, my_uid,
							&cluster_cond);
	} else
		temp_list = acct_storage_g_get_clusters(db_conn, my_uid,
							NULL);


	itr_c = list_iterator_create(cluster_list);
	itr = list_iterator_create(temp_list);
	while ((cluster = list_next(itr_c))) {
		slurmdb_cluster_rec_t *cluster_rec = NULL;

		list_iterator_reset(itr);
		while ((cluster_rec = list_next(itr))) {
			if (!xstrcasecmp(cluster_rec->name, cluster))
				break;
		}
		if (!cluster_rec) {
			exit_code=1;
			fprintf(stderr, " This cluster '%s' "
				"doesn't exist.\n"
				"        Contact your admin "
				"to add it to accounting.\n",
				cluster);
			list_delete_item(itr_c);
		}
	}
	list_iterator_destroy(itr);
	list_iterator_destroy(itr_c);
	FREE_NULL_LIST(temp_list);

	if (!list_count(cluster_list))
		rc = SLURM_ERROR;

	return rc;
}


extern void sacctmgr_initialize_g_tres_list(void)
{
	if (!g_tres_list) {
		slurmdb_tres_cond_t tres_cond;
		memset(&tres_cond, 0, sizeof(slurmdb_tres_cond_t));
		tres_cond.with_deleted = 1;
		g_tres_list = slurmdb_tres_get(db_conn, &tres_cond);
	}
}
