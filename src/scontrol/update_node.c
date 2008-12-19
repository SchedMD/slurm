/*****************************************************************************\
 *  update_node.c - node update function for scontrol.
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
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

#include "scontrol.h"

/* 
 * scontrol_update_node - update the slurm node configuration per the supplied
 *	arguments 
 * IN argc - count of arguments
 * IN argv - list of arguments
 * RET 0 if no slurm error, errno otherwise. parsing error prints 
 *			error message and returns 0
 */
extern int
scontrol_update_node (int argc, char *argv[]) 
{
	int i, j, k, rc = 0, update_cnt = 0;
	uint16_t state_val;
	update_node_msg_t node_msg;
	char *reason_str = NULL;
	char *user_name;

	node_msg.node_names = NULL;
	node_msg.features = NULL;
	node_msg.reason = NULL;
	node_msg.node_state = (uint16_t) NO_VAL;
	for (i=0; i<argc; i++) {
		if (strncasecmp(argv[i], "NodeName=", 9) == 0)
			node_msg.node_names = &argv[i][9];
		else if (strncasecmp(argv[i], "Features=", 9) == 0) {
			node_msg.features = &argv[i][9];
			update_cnt++;
		} else if (strncasecmp(argv[i], "Reason=", 7) == 0) {
			char time_buf[64], time_str[32];
			time_t now;
			int len = strlen(&argv[i][7]);
			reason_str = xmalloc(len+1);
			if (argv[i][7] == '"')
				strcpy(reason_str, &argv[i][8]);
			else
				strcpy(reason_str, &argv[i][7]);

			len = strlen(reason_str) - 1;
			if ((len >= 0) && (reason_str[len] == '"'))
				reason_str[len] = '\0';

			/* Append user, date and time */
			xstrcat(reason_str, " [");
			user_name = getlogin();
			if (user_name)
				xstrcat(reason_str, user_name);
			else {
				sprintf(time_buf, "%d", getuid());
				xstrcat(reason_str, time_buf);
			}
			now = time(NULL);
			slurm_make_time_str(&now, time_str, sizeof(time_str));
			snprintf(time_buf, sizeof(time_buf), "@%s]", time_str);
			xstrcat(reason_str, time_buf);
				
			node_msg.reason = reason_str;
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "State=NoResp", 12) == 0) {
			node_msg.node_state = NODE_STATE_NO_RESPOND;
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "State=DRAIN", 11) == 0) {
			node_msg.node_state = NODE_STATE_DRAIN;
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "State=FAIL", 10) == 0) {
			node_msg.node_state = NODE_STATE_FAIL;
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "State=RES", 9) == 0) {
			node_msg.node_state = NODE_RESUME;
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "State=POWER_DOWN", 13) == 0) {
			node_msg.node_state = NODE_STATE_POWER_SAVE;
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "State=POWER_UP", 13) == 0) {
			node_msg.node_state = NODE_STATE_POWER_UP;
			update_cnt++;
		}
		else if (strncasecmp(argv[i], "State=", 6) == 0) {
			state_val = (uint16_t) NO_VAL;
			for (j = 0; j <= NODE_STATE_END; j++) {
				if (strcasecmp (node_state_string(j), 
				                &argv[i][6]) == 0) {
					state_val = (uint16_t) j;
					break;
				}
				if (j == NODE_STATE_END) {
					exit_code = 1;
					fprintf(stderr, "Invalid input: %s\n", 
						argv[i]);
					fprintf (stderr, "Request aborted\n");
					fprintf (stderr, "Valid states are: ");
					fprintf (stderr,
						 "NoResp DRAIN FAIL RESUME "
						 "POWER_DOWN POWER_UP");
					for (k = 0; k < NODE_STATE_END; k++) {
						fprintf (stderr, "%s ", 
						         node_state_string(k));
					}
					fprintf (stderr, "\n");
					fprintf (stderr, 
						 "Not all states are valid "
						 "given a node's prior "
						 "state\n");
					goto done;
				}
			}
			node_msg.node_state = state_val;
			update_cnt++;
		}
		else {
			exit_code = 1;
			fprintf (stderr, "Invalid input: %s\n", argv[i]);
			fprintf (stderr, "Request aborted\n");
			goto done;
		}
	}

	if (((node_msg.node_state == NODE_STATE_DRAIN)
	||   (node_msg.node_state == NODE_STATE_FAIL))
	&&  (node_msg.reason == NULL)) {
		fprintf (stderr, "You must specify a reason when DRAINING a "
			"node\nRequest aborted\n");
		goto done;
	}

	if (update_cnt == 0) {
		exit_code = 1;
		fprintf (stderr, "No changes specified\n");
		return 0;
	}

	rc = slurm_update_node(&node_msg);

done:	xfree(reason_str);
	if (rc) {
		exit_code = 1;
		return slurm_get_errno ();
	} else
		return 0;
}
