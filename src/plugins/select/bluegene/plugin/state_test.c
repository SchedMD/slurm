/*****************************************************************************\
 *  state_test.c - Test state of Bluegene base partitions and switches. 
 *  DRAIN nodes in SLURM that are not usable. 
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004-2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov> and Morris Jette <jette1@llnl.gov>
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <slurm/slurm.h>

#include "src/common/log.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/slurmctld.h"
#include "bluegene.h"

#define BUFSIZE 4096

#ifdef HAVE_BG_FILES

/* Find the specified BlueGene node ID and drain it from SLURM */
static void _configure_node_down(rm_bp_id_t bp_id, my_bluegene_t *bg)
{
	int bp_num, i, rc;
	rm_bp_id_t bpid;
	rm_BP_t *my_bp;
	rm_location_t bp_loc;
	rm_BP_state_t bp_state;
	char bg_down_node[128], reason[128], time_str[32];
	time_t now = time(NULL);

	if ((rc = bridge_get_data(bg, RM_BPNum, &bp_num)) != STATUS_OK) {
		error("bridge_get_data(RM_BPNum): %s", bg_err_str(rc));
		bp_num = 0;
	}

	for (i=0; i<bp_num; i++) {
		if (i) {
			if ((rc = bridge_get_data(bg, RM_NextBP, &my_bp)) 
			    != STATUS_OK) {
				error("bridge_get_data(RM_NextBP): %s", 
				      bg_err_str(rc));
				continue;
			}
		} else {
			if ((rc = bridge_get_data(bg, RM_FirstBP, &my_bp)) 
			    != 
			    STATUS_OK) {
				error("bridge_get_data(RM_FirstBP): %s", 
				      bg_err_str(rc));
				continue;
			}
		}

		if ((rc = bridge_get_data(my_bp, RM_BPID, &bpid)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_BPID): %s", bg_err_str(rc));
			continue;
		}

		if(!bpid) {
			error("No BPID was returned from database");
			continue;
		}

		if (strcmp(bp_id, bpid) != 0) {	/* different base partition */
			free(bpid);
			continue;
		}
		free(bpid);

		if ((rc = bridge_get_data(my_bp, RM_BPState, &bp_state)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_BPState): %s", 
			      bg_err_str(rc));
			continue;
		}
		if  (bp_state != RM_BP_UP) 		/* already down */
			continue;

		if ((rc = bridge_get_data(my_bp, RM_BPLoc, &bp_loc)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_BPLoc): %s", bg_err_str(rc));
			continue;
		}
		slurm_conf_lock();
		snprintf(bg_down_node, sizeof(bg_down_node), "%s%c%c%c", 
			 slurmctld_conf.node_prefix,
			 alpha_num[bp_loc.X], alpha_num[bp_loc.Y],
			 alpha_num[bp_loc.Z]);
		slurm_conf_unlock();
	
		if (node_already_down(bg_down_node))
			break;

		error("switch for node %s is bad", bg_down_node);
		slurm_make_time_str(&now, time_str, sizeof(time_str));
		snprintf(reason, sizeof(reason),
			 "select_bluegene: MMCS switch not UP [SLURM@%s]", 
			 time_str);
		slurm_drain_nodes(bg_down_node, reason);
		break;
	}
}

/* Convert base partition state value to a string */
static char *_convert_bp_state(rm_BP_state_t state)
{
	switch(state) { 
	case RM_BP_UP:
		return "RM_BP_UP";
		break;
	case RM_BP_DOWN:
		return "RM_BP_DOWN";
		break;
	case RM_BP_MISSING:
		return "RM_BP_MISSING";
		break;
	case RM_BP_ERROR:
		return "RM_BP_ERROR";
		break;
	case RM_BP_NAV:
		return "RM_BP_NAV";
	}
	return "BP_STATE_UNIDENTIFIED!";
}

/* Test for nodes that are not UP in MMCS and DRAIN them in SLURM */ 
static void _test_down_nodes(my_bluegene_t *bg)
{
	int bp_num, i, rc;
	rm_BP_t *my_bp;
	rm_BP_state_t bp_state;
	rm_location_t bp_loc;
	char down_node_list[BUFSIZE];
	char bg_down_node[128];
	char reason[128], time_str[32];
	time_t now = time(NULL);
		
	debug2("Running _test_down_nodes");
	down_node_list[0] = '\0';
	if ((rc = bridge_get_data(bg, RM_BPNum, &bp_num)) != STATUS_OK) {
		error("bridge_get_data(RM_BPNum): %s", bg_err_str(rc));
		bp_num = 0;
	}
	for (i=0; i<bp_num; i++) {
		if (i) {
			if ((rc = bridge_get_data(bg, RM_NextBP, &my_bp)) 
			    != STATUS_OK) {
				error("bridge_get_data(RM_NextBP): %s", 
				      bg_err_str(rc));
				continue;
			}
		} else {
			if ((rc = bridge_get_data(bg, RM_FirstBP, &my_bp)) 
			    != STATUS_OK) {
				error("bridge_get_data(RM_FirstBP): %s", 
				      bg_err_str(rc));
				continue;
			}
		}

		if ((rc = bridge_get_data(my_bp, RM_BPState, &bp_state)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_BPState): %s", 
			      bg_err_str(rc));
			continue;
		}
		
		if  (bp_state == RM_BP_UP)
			continue;
		
		if ((rc = bridge_get_data(my_bp, RM_BPLoc, &bp_loc)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_BPLoc): %s", bg_err_str(rc));
			continue;
		}

		slurm_conf_lock();
		snprintf(bg_down_node, sizeof(bg_down_node), "%s%c%c%c", 
			 slurmctld_conf.node_prefix,
			 alpha_num[bp_loc.X], alpha_num[bp_loc.Y],
			 alpha_num[bp_loc.Z]);
		slurm_conf_unlock();
	
		if (node_already_down(bg_down_node))
			continue;

		debug("_test_down_nodes: %s in state %s", 
		      bg_down_node, _convert_bp_state(bp_state));
		
		if ((strlen(down_node_list) + strlen(bg_down_node) 
		     + 2) 
		    < BUFSIZE) {
			if (down_node_list[0] != '\0')
				strcat(down_node_list,",");
			strcat(down_node_list, bg_down_node);
		} else
			error("down_node_list overflow");
	}
	if (down_node_list[0]) {
		slurm_make_time_str(&now, time_str, sizeof(time_str));
		snprintf(reason, sizeof(reason), 
			 "select_bluegene: MMCS state not UP [SLURM@%s]", 
			 time_str); 
		slurm_drain_nodes(down_node_list, reason);
	}
	
}

/* Test for switches that are not UP in MMCS, 
 * when found DRAIN them in SLURM and configure their base partition DOWN */
static void _test_down_switches(my_bluegene_t *bg)
{
	int switch_num, i, rc;
	rm_switch_t *my_switch;
	rm_bp_id_t bp_id;
	rm_switch_state_t switch_state;

	debug2("Running _test_down_switches");
	if ((rc = bridge_get_data(bg, RM_SwitchNum, &switch_num)) 
	    != STATUS_OK) {
		error("bridge_get_data(RM_SwitchNum): %s", bg_err_str(rc));
		switch_num = 0;
	}
	for (i=0; i<switch_num; i++) {
		if (i) {
			if ((rc = bridge_get_data(bg, RM_NextSwitch, 
						  &my_switch))
			    != STATUS_OK) {
				error("bridge_get_data(RM_NextSwitch): %s", 
				      bg_err_str(rc));
				continue;
			}
		} else {
			if ((rc = bridge_get_data(bg, RM_FirstSwitch, 
						  &my_switch))
			    != STATUS_OK) {
				error("bridge_get_data(RM_FirstSwitch): %s",
				      bg_err_str(rc));
				continue;
			}
		}

		if ((rc = bridge_get_data(my_switch, RM_SwitchState, 
					  &switch_state)) != STATUS_OK) {
			error("bridge_get_data(RM_SwitchState): %s",
			      bg_err_str(rc));
			continue;
		}
		if (switch_state == RM_SWITCH_UP)
			continue;
		if ((rc = bridge_get_data(my_switch, RM_SwitchBPID, &bp_id)) 
		    != STATUS_OK) {
			error("bridge_get_data(RM_SwitchBPID): %s",
			      bg_err_str(rc));
			continue;
		}

		if(!bp_id) {
			error("No BPID was returned from database");
			continue;
		}

		_configure_node_down(bp_id, bg);
		free(bp_id);
	}
}
#endif

/* Determine if specific slurm node is already in DOWN or DRAIN state */
extern bool node_already_down(char *node_name)
{
	uint16_t base_state;
	struct node_record *node_ptr = find_node_record(node_name);
	
	if (node_ptr) {
		base_state = node_ptr->node_state & 
			(~NODE_STATE_NO_RESPOND);
		if ((base_state == NODE_STATE_DOWN)
		    ||  (base_state == NODE_STATE_DRAIN))
			return true;
		else
			return false;
	}

	return false;
}

/* 
 * Search MMCS for failed switches and nodes. Failed resources are DRAINED in 
 * SLURM. This relies upon rm_get_BG(), which is slow (10+ seconds) so run 
 * this test infrequently.
 */
extern void test_mmcs_failures(void)
{
#ifdef HAVE_BG_FILES
	my_bluegene_t *bg;
	int rc;

	if ((rc = bridge_get_bg(&bg)) != STATUS_OK) {
		
		error("bridge_get_BG(): %s", bg_err_str(rc));
		return;
	}
	
			
	_test_down_switches(bg);
	_test_down_nodes(bg);
	if ((rc = bridge_free_bg(bg)) != STATUS_OK)
		error("bridge_free_BG(): %s", bg_err_str(rc));
#endif
}

extern int check_block_bp_states(char *bg_block_id)
{
	int rc = SLURM_SUCCESS;
#ifdef HAVE_BG_FILES
	rm_partition_t *block_ptr = NULL;
	rm_BP_t *bp_ptr = NULL;
	char *bpid = NULL;
	int bp_cnt = 0;
	int i = 0;
	int *coord = NULL;
	rm_BP_state_t bp_state;
	char bg_down_node[128], reason[128], time_str[32];
	char down_node_list[BUFSIZE];
	time_t now = time(NULL);
	
	down_node_list[0] = '\0';
	
	if ((rc = bridge_get_block(bg_block_id, &block_ptr)) != STATUS_OK) {
		error("Block %s doesn't exist.", bg_block_id);
		rc = SLURM_ERROR;
		
		goto done;
	}
	
	
	if ((rc = bridge_get_data(block_ptr, RM_PartitionBPNum, &bp_cnt)) 
	    != STATUS_OK) {
		error("bridge_get_data(RM_BPNum): %s", bg_err_str(rc));
		rc = SLURM_ERROR;
		goto cleanup;
	}
	
	for(i=0; i<bp_cnt; i++) {
		if(i) {
			if ((rc = bridge_get_data(block_ptr, 
						  RM_PartitionNextBP, 
						  &bp_ptr))
			    != STATUS_OK) {
				error("bridge_get_data(RM_NextBP): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				break;
			}
		} else {
			if ((rc = bridge_get_data(block_ptr, 
						  RM_PartitionFirstBP, 
						  &bp_ptr))
			    != STATUS_OK) {
				error("bridge_get_data(RM_FirstBP): %s", 
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				break;
			}	
		}
		if ((rc = bridge_get_data(bp_ptr, RM_BPState, &bp_state))
		    != STATUS_OK) {
			error("bridge_get_data(RM_BPLoc): %s",
			      bg_err_str(rc));
			rc = SLURM_ERROR;
			break;
		}
		if(bp_state == RM_BP_UP)
			continue;
		rc = SLURM_ERROR;
		if ((rc = bridge_get_data(bp_ptr, RM_BPID, &bpid))
		    != STATUS_OK) {
			error("bridge_get_data(RM_BPID): %s",
			      bg_err_str(rc));
			break;
		}
		coord = find_bp_loc(bpid);
		
		if(!coord) {
			fatal("Could not find coordinates for "
			      "BP ID %s", (char *) bpid);
		}
		free(bpid);
		slurm_conf_lock();
		snprintf(bg_down_node, sizeof(bg_down_node), "%s%c%c%c", 
			 slurmctld_conf.node_prefix,
			 alpha_num[coord[X]], alpha_num[coord[Y]],
			 alpha_num[coord[Z]]);
		slurm_conf_unlock();
	
		if (node_already_down(bg_down_node))
			continue;

		debug("check_block_bp_states: %s in state %s", 
		      bg_down_node, _convert_bp_state(bp_state));
		if ((strlen(down_node_list) + strlen(bg_down_node) + 2) 
		    < BUFSIZE) {
			if (down_node_list[0] != '\0')
				strcat(down_node_list,",");
			strcat(down_node_list, bg_down_node);
		} else
			error("down_node_list overflow");
	}
	
cleanup:
	bridge_free_block(block_ptr);
done:
	if (down_node_list[0]) {
		slurm_make_time_str(&now, time_str, sizeof(time_str));
		snprintf(reason, sizeof(reason), 
			 "select_bluegene: MMCS state not UP [SLURM@%s]", 
			 time_str); 
		slurm_drain_nodes(down_node_list, reason);
	}
#endif
	return rc;

}

