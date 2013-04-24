/*****************************************************************************\
 *  bg_switch_connections.c - Blue Gene switch management functions,
 *  establish switch connections
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov> and Danny Auble <da@llnl.gov>
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

#include "bridge_switch_connections.h"

#if defined HAVE_BG_FILES
static int _get_mp_by_location(my_bluegene_t* my_bg,
			       uint16_t* curr_coord,
			       rm_BP_t** mp);
static int _get_switches_by_mpid(my_bluegene_t* my_bg, const char *mpid,
				 rm_switch_t **curr_switch);

//static int _set_switch(rm_switch_t* curr_switch, ba_connection_t *int_wire);
static int _add_switch_conns(rm_switch_t* curr_switch,
			     ba_switch_t *ba_switch);
#endif

static int _used_switches(ba_mp_t *ba_node);

/**
 * this is just stupid.  there are some implicit rules for where
 * "NextMP" goes to, but we don't know, so we have to do this.
 */
#if defined HAVE_BG_FILES
static int _get_mp_by_location(my_bluegene_t* my_bg, uint16_t* curr_coord,
			       rm_BP_t** mp)
{
	static int mp_num = 0;
	int i, rc;
	rm_location_t loc;

	if (!mp_num) {
		if ((rc = bridge_get_data(my_bg, RM_BPNum, &mp_num))
		    != SLURM_SUCCESS) {
			fatal("bridge_get_data: RM_BPNum: %s",
			      bg_err_str(rc));
			return SLURM_ERROR;
		}
	}

	for (i=0; i<mp_num; i++) {
		if (i) {
			if ((rc = bridge_get_data(my_bg, RM_NextBP, mp))
			    != SLURM_SUCCESS) {
				fatal("bridge_get_data: RM_NextBP: %s",
				      bg_err_str(rc));
				return SLURM_ERROR;
			}
		} else {
			if ((rc = bridge_get_data(my_bg, RM_FirstBP, mp))
			    != SLURM_SUCCESS) {
				fatal("bridge_get_data: RM_FirstBP: %s",
				      bg_err_str(rc));
				return SLURM_ERROR;
			}
		}
		if ((rc = bridge_get_data(*mp, RM_BPLoc, &loc))
		    != SLURM_SUCCESS) {
			fatal("bridge_get_data: RM_BPLoc: %s",
			      bg_err_str(rc));
			return SLURM_ERROR;
		}

		if ((loc.X == curr_coord[X])
		    &&  (loc.Y == curr_coord[Y])
		    &&  (loc.Z == curr_coord[Z])) {
			return SLURM_SUCCESS;
		}
	}

	// error("_get_mp_by_location: could not find specified mp.");
	return SLURM_ERROR;
}

static int _get_switches_by_mpid(
	my_bluegene_t* my_bg, const char *mpid,
	rm_switch_t *coord_switch[SYSTEM_DIMENSIONS])
{
	static int switch_num = 0;
	rm_switch_t *curr_switch = NULL;
	int i, rc;
	int found_mpid = 0;
	char *curr_mpid = NULL;

	if (!switch_num) {
		if ((rc = bridge_get_data(my_bg, RM_SwitchNum, &switch_num))
		    != SLURM_SUCCESS) {
			fatal("bridge_get_data: RM_SwitchNum: %s",
			      bg_err_str(rc));
			return SLURM_ERROR;
		}
	}

	for (i=0; i<switch_num; i++) {
		if (i) {
			if ((rc = bridge_get_data(my_bg, RM_NextSwitch,
						  &curr_switch))
			    != SLURM_SUCCESS) {
				fatal("bridge_get_data"
				      "(RM_NextSwitch): %s",
				      bg_err_str(rc));
			}
		} else {
			if ((rc = bridge_get_data(my_bg, RM_FirstSwitch,
						  &curr_switch))
			    != SLURM_SUCCESS) {
				fatal("bridge_get_data"
				      "(RM_FirstSwitch): %s",
				      bg_err_str(rc));
			}
		}
		if ((rc = bridge_get_data(curr_switch, RM_SwitchBPID,
					  &curr_mpid)) != SLURM_SUCCESS) {
			fatal("bridge_get_data: RM_SwitchBPID: %s",
			      bg_err_str(rc));
		}

		if (!curr_mpid) {
			error("No BP ID was returned from database");
			continue;
		}

		if (!strcasecmp((char *)mpid, (char *)curr_mpid)) {
			coord_switch[found_mpid] = curr_switch;
			found_mpid++;
			if (found_mpid==SYSTEM_DIMENSIONS) {
				free(curr_mpid);
				return SLURM_SUCCESS;
			}
		}
		free(curr_mpid);
	}
	return SLURM_ERROR;
}

static int _add_switch_conns(rm_switch_t* curr_switch,
			     ba_switch_t *ba_switch)
{
	int firstconnect=1;

	/* max number of connections in a switch */
	int num_connections = 3;
	ba_connection_t *ba_conn = NULL;
	rm_connection_t conn[num_connections];
	rm_connection_t *conn_ptr = NULL;
	int i, rc;
	int source = 0;
	List conn_list = list_create(NULL);
	/* we have to figure out how may connections we have and then
	   go through the loop again to actually add them */

	for(i=0; i<num_connections; i++) {
		/* set the source port(-) to check */
		switch(i) {
		case 0:
			source = 1;
			conn[i].p1 = RM_PORT_S1;
			break;
		case 1:
			source = 2;
			conn[i].p1 = RM_PORT_S2;
			break;
		case 2:
			source = 4;
			conn[i].p1 = RM_PORT_S4;
			break;
		default:
			error("we are to far into the switch connections");
			break;
		}
		ba_conn = &ba_switch->int_wire[source];
		if (ba_conn->used && ba_conn->port_tar != source) {
			switch(ba_conn->port_tar) {
			case 0:
				conn[i].p2 = RM_PORT_S0;
				break;
			case 3:
				conn[i].p2 = RM_PORT_S3;
				break;
			case 5:
				conn[i].p2 = RM_PORT_S5;
				break;
			default:
				error("we are trying to connection %d -> %d "
				      "which can't happen",
				      source, ba_conn->port_tar);
				break;
			}
			conn[i].part_state = RM_PARTITION_READY;
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_WIRES)
				info("adding %d -> %d", source,
				     ba_conn->port_tar);
			list_push(conn_list, &conn[i]);
		}
	}

	i = list_count(conn_list);
	if (i) {
		if ((rc = bridge_set_data(curr_switch, RM_SwitchConnNum, &i))
		    != SLURM_SUCCESS) {
			fatal("bridge_set_data: RM_SwitchConnNum: %s",
			      bg_err_str(rc));

			return SLURM_ERROR;
		}
	} else {
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_WIRES)
			info("we got a switch with no connections");
		list_destroy(conn_list);
                return SLURM_ERROR;
	}

	/* Now we can add them to the mix */
	while ((conn_ptr = list_pop(conn_list))) {
		if (firstconnect) {
			if ((rc = bridge_set_data(
				     curr_switch,
				     RM_SwitchFirstConnection,
				     conn_ptr))
			    != SLURM_SUCCESS) {
				fatal("bridge_set_data"
				      "(RM_SwitchFirstConnection): "
				      "%s",
				      bg_err_str(rc));
				list_destroy(conn_list);
				return SLURM_ERROR;
			}
			firstconnect=0;
		} else {
			if ((rc = bridge_set_data(
				     curr_switch,
				     RM_SwitchNextConnection,
				     conn_ptr))
			    != SLURM_SUCCESS) {
				fatal("bridge_set_data"
				      "(RM_SwitchNextConnection): %s",
				      bg_err_str(rc));
				list_destroy(conn_list);
				return SLURM_ERROR;
			}
		}
	}

	list_destroy(conn_list);

	return SLURM_SUCCESS;
}
#endif

static int _used_switches(ba_mp_t* ba_node)
{
	/* max number of connections in a switch */
	int num_connections = 3;
	ba_connection_t *ba_conn = NULL;
	ba_switch_t *ba_switch = NULL;
	int i = 0, j = 0, switch_count = 0;
	int source = 0;

	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_WIRES)
		info("checking node %c%c%c",
		     alpha_num[ba_node->coord[X]],
		     alpha_num[ba_node->coord[Y]],
		     alpha_num[ba_node->coord[Z]]);
	for(i=0; i<SYSTEM_DIMENSIONS; i++) {
		if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_WIRES)
			info("dim %d", i);
		ba_switch = &ba_node->axis_switch[i];
		for(j=0; j<num_connections; j++) {
			/* set the source port(-) to check */
			switch(j) {
			case 0:
				source = 1;
				break;
			case 1:
				source = 2;
				break;
			case 2:
				source = 4;
				break;
			default:
				error("we are to far into the "
				      "switch connections");
				break;
			}
			ba_conn = &ba_switch->int_wire[source];
			if (ba_conn->used && ba_conn->port_tar != source) {
				switch_count++;
				if (bg_conf->slurm_debug_flags
				    & DEBUG_FLAG_BG_WIRES)
					info("used");
				break;
			}
		}
	}
	return switch_count;
}

extern int configure_small_block(bg_record_t *bg_record)
{
	int rc = SLURM_SUCCESS;
#if defined HAVE_BG_FILES
	bool small = true;
	ba_mp_t* ba_node = NULL;
	rm_BP_t *curr_mp = NULL;
	rm_bp_id_t mp_id = NULL;
#ifndef HAVE_BGL
	rm_nodecard_id_t nc_char = NULL;
#endif
	int nc_id = 0;
	int num_ncards = 0, sub_nodecard = 0, ionode_card = 0, nc_count = 0;
	rm_nodecard_t *ncard;
	rm_nodecard_list_t *ncard_list = NULL;
	int num, i;
	int use_nc[bg_conf->mp_nodecard_cnt];
	double nc_pos = 0;
#endif
	xassert(bg_record->ionode_bitmap);
	if (bg_record->mp_count != 1) {
		error("Requesting small block with %d mps, needs to be 1.",
		      bg_record->mp_count);
		return SLURM_ERROR;
	}
/* 	info("configuring small block on ionodes %s out of %d ncs",  */
/* 	     bg_record->ionodes, bg_conf->mp_nodecard_cnt); */
#if defined HAVE_BG_FILES
	/* set that we are doing a small block */
	if ((rc = bridge_set_data(bg_record->bg_block, RM_PartitionSmall,
				  &small)) != SLURM_SUCCESS) {

		fatal("bridge_set_data(RM_PartitionPsetsPerBP): %s",
		      bg_err_str(rc));
	}

	num_ncards = bg_record->cnode_cnt/bg_conf->nodecard_cnode_cnt;
	if (num_ncards < 1) {
		num_ncards = 1;
		sub_nodecard = 1;
	}
	memset(use_nc, 0, sizeof(use_nc));

	/* find out how many nodecards to get for each ionode */

	for(i = 0; i<bg_conf->ionodes_per_mp; i++) {
		if (bit_test(bg_record->ionode_bitmap, i)) {
			if (bg_conf->nc_ratio > 1) {
				int j=0;
				for(j=0; j<bg_conf->nc_ratio; j++)
					use_nc[(int)nc_pos+j] = 1;
			} else {
				use_nc[(int)nc_pos] = 1;
				if (i%2)
					ionode_card = 1;
			}
		}
		nc_pos += bg_conf->nc_ratio;
	}

	if ((rc = bridge_set_data(bg_record->bg_block,
				  RM_PartitionNodeCardNum,
				  &num_ncards))
	    != SLURM_SUCCESS) {

		fatal("bridge_set_data: RM_PartitionBPNum: %s",
		      bg_err_str(rc));
	}

	ba_node = list_peek(bg_record->ba_mp_list);

	if (_get_mp_by_location(bg, ba_node->coord, &curr_mp)
	    == SLURM_ERROR) {
		fatal("_get_mp_by_location()");
	}

	/* Set the one MP */

	if ((rc = bridge_set_data(bg_record->bg_block,
				  RM_PartitionBPNum,
				  &bg_record->mp_count))
	    != SLURM_SUCCESS) {

		fatal("bridge_set_data: RM_PartitionBPNum: %s",
		      bg_err_str(rc));
		return SLURM_ERROR;
	}
	if ((rc = bridge_set_data(bg_record->bg_block,
				  RM_PartitionFirstBP,
				  curr_mp))
	    != SLURM_SUCCESS) {

		fatal("bridge_set_data("
		      "BRIDGE_PartitionFirstBP): %s",
		      bg_err_str(rc));
		return SLURM_ERROR;
	}


	/* find the mp_id of the mp to get the small32 */
	if ((rc = bridge_get_data(curr_mp, RM_BPID, &mp_id))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(): %d", rc);
		return SLURM_ERROR;
	}


	if (!mp_id) {
		error("No MP ID was returned from database");
		return SLURM_ERROR;
	}

	if ((rc = bridge_get_nodecards(mp_id, &ncard_list))
	    != SLURM_SUCCESS) {
		error("bridge_get_nodecards(%s): %d",
		      mp_id, rc);
		free(mp_id);
		return SLURM_ERROR;
	}
	free(mp_id);


	if ((rc = bridge_get_data(ncard_list, RM_NodeCardListSize, &num))
	    != SLURM_SUCCESS) {
		error("bridge_get_data(RM_NodeCardListSize): %s",
		      bg_err_str(rc));
		return SLURM_ERROR;
	}
	if (num_ncards > num) {
		error("You requested more (%d > %d) nodecards "
		      "than are available on this block %s",
		      num_ncards, num, bg_record->mp_str);
	}

	for(i=0; i<num; i++) {
		if (i) {
			if ((rc = bridge_get_data(ncard_list,
						  RM_NodeCardListNext,
						  &ncard)) != SLURM_SUCCESS) {
				error("bridge_get_data"
				      "(RM_NodeCardListNext): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				goto cleanup;
			}
		} else {
			if ((rc = bridge_get_data(ncard_list,
						  RM_NodeCardListFirst,
						  &ncard)) != SLURM_SUCCESS) {
				error("bridge_get_data"
				      "(RM_NodeCardListFirst): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				goto cleanup;
			}
		}

#ifdef HAVE_BGL
		/* on BG/L we assume the order never changes when the
		   system is up.  This could change when a reboot of
		   the system happens, but that should be rare.
		*/
		nc_id = i;
		if (!use_nc[i])
			continue;
#else
		if ((rc = bridge_get_data(ncard,
					  RM_NodeCardID,
					  &nc_char)) != SLURM_SUCCESS) {
			error("bridge_get_data(RM_NodeCardID): %s",
			      bg_err_str(rc));
			rc = SLURM_ERROR;
			goto cleanup;
		}

		if (!nc_char) {
			error("No NodeCard ID was returned from database");
			rc = SLURM_ERROR;
			goto cleanup;
		}

		nc_id = atoi((char*)nc_char+1);

		if (!use_nc[nc_id]) {
			free(nc_char);
			continue;
		}

		if (sub_nodecard) {
			rm_ionode_t *ionode;
			char *ionode_id = "J00";

			if ((rc = bridge_new_nodecard(&ncard))
			    != SLURM_SUCCESS) {
				error("bridge_new_nodecard(): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				goto cleanup;
			}

			if ((rc = bridge_set_data(ncard,
						  RM_NodeCardID,
						  nc_char))
			    != SLURM_SUCCESS) {
				error("bridge_set_data("
				      "RM_NodeCardID): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				goto cleanup;
			}

			if ((rc = bridge_set_data(ncard,
						  RM_NodeCardIONodeNum,
						  &sub_nodecard))
			    != SLURM_SUCCESS) {
				error("bridge_set_data("
				      "RM_NodeCardIONodeNum): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				goto cleanup;
			}

			if ((rc = bridge_new_ionode(&ionode))
			    != SLURM_SUCCESS) {
				error("bridge_new_ionode(): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				goto cleanup;
			}

			if (ionode_card)
				ionode_id = "J01";

			if ((rc = bridge_set_data(ionode,
						  RM_IONodeID,
						  ionode_id))
			    != SLURM_SUCCESS) {
				error("bridge_set_data("
				      "RM_NodeCardIONodeNum): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				goto cleanup;
			}

			if ((rc = bridge_set_data(ncard,
						  RM_NodeCardFirstIONode,
						  ionode))
			    != SLURM_SUCCESS) {
				error("bridge_set_data("
				      "RM_NodeCardFirstIONode): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				goto cleanup;
			}

			if ((rc = bridge_free_ionode(ionode))
			    != SLURM_SUCCESS) {
				error("bridge_free_ionode(): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				goto cleanup;
			}
		}
		free(nc_char);
#endif

		if (nc_count) {
			if ((rc = bridge_set_data(bg_record->bg_block,
						  RM_PartitionNextNodeCard,
						  ncard))
			    != SLURM_SUCCESS) {

				error("bridge_set_data("
				      "RM_PartitionNextNodeCard): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				goto cleanup;
			}
		} else {
			if ((rc = bridge_set_data(bg_record->bg_block,
						  RM_PartitionFirstNodeCard,
						  ncard))
			    != SLURM_SUCCESS) {

				error("bridge_set_data("
				      "RM_PartitionFirstNodeCard): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				goto cleanup;
			}
		}

		nc_count++;
#ifndef HAVE_BGL
		if (sub_nodecard) {
			if ((rc = bridge_free_nodecard(ncard))
			    != SLURM_SUCCESS) {
				error("bridge_free_nodecard(): %s",
				      bg_err_str(rc));
				rc = SLURM_ERROR;
				goto cleanup;
			}
		}
#endif
		if (nc_count == num_ncards)
			break;
	}
cleanup:
	if ((rc = bridge_free_nodecard_list(ncard_list)) != SLURM_SUCCESS) {
		error("bridge_free_nodecard_list(): %s", bg_err_str(rc));
		return SLURM_ERROR;
	}

#endif
	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_WIRES)
		info("making the small block");
	if (rc != SLURM_ERROR)
		rc = SLURM_SUCCESS;
	return rc;
}

/**
 * connect the given switch up with the given connections
 */
extern int configure_block_switches(bg_record_t * bg_record)
{
	int rc = SLURM_SUCCESS;
	ListIterator itr;
	ba_mp_t *ba_node = NULL;
#if defined HAVE_BG_FILES
	char *mpid = NULL;
	int first_mp=1;
	int first_switch=1;
	int i = 0;
	rm_BP_t *curr_mp = NULL;
	rm_switch_t *coord_switch[SYSTEM_DIMENSIONS];
#endif
	if (!bg_record->ba_mp_list) {
		error("There was no block_list given, can't create block");
		return SLURM_ERROR;
	}

	bg_record->switch_count = 0;
	bg_record->mp_count = 0;

	itr = list_iterator_create(bg_record->ba_mp_list);
	while ((ba_node = list_next(itr))) {
		if (ba_node->used) {
			bg_record->mp_count++;
		}
		bg_record->switch_count += _used_switches(ba_node);
	}
#if defined HAVE_BG_FILES
	if ((rc = bridge_set_data(bg_record->bg_block,
				  RM_PartitionBPNum,
				  &bg_record->mp_count))
	    != SLURM_SUCCESS) {
		fatal("bridge_set_data: RM_PartitionBPNum: %s",
		      bg_err_str(rc));
		rc = SLURM_ERROR;

		goto cleanup;
	}
	if ((rc = bridge_set_data(bg_record->bg_block,
				  RM_PartitionSwitchNum,
				  &bg_record->switch_count))
	    != SLURM_SUCCESS) {
		fatal("bridge_set_data: RM_PartitionSwitchNum: %s",
		      bg_err_str(rc));
		rc = SLURM_ERROR;

		goto cleanup;
	}
#endif
	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_WIRES)
		info("MP count %d", bg_record->mp_count);
	if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_WIRES)
		info("switch count %d", bg_record->switch_count);

	list_iterator_reset(itr);
	while ((ba_node = list_next(itr))) {
#if defined HAVE_BG_FILES
		if (_get_mp_by_location(bg, ba_node->coord, &curr_mp)
		    == SLURM_ERROR) {
			rc = SLURM_ERROR;
			goto cleanup;
		}
#endif
		if (!ba_node->used) {
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_WIRES)
				info("%c%c%c is a passthrough, "
				     "not including in request",
				     alpha_num[ba_node->coord[X]],
				     alpha_num[ba_node->coord[Y]],
				     alpha_num[ba_node->coord[Z]]);
		} else {
			if (bg_conf->slurm_debug_flags & DEBUG_FLAG_BG_WIRES)
				info("using node %c%c%c",
				     alpha_num[ba_node->coord[X]],
				     alpha_num[ba_node->coord[Y]],
				     alpha_num[ba_node->coord[Z]]);
#if defined HAVE_BG_FILES
			if (first_mp){
				if ((rc = bridge_set_data(bg_record->bg_block,
							  RM_PartitionFirstBP,
							  curr_mp))
				    != SLURM_SUCCESS) {
					list_iterator_destroy(itr);
					fatal("bridge_set_data("
					      "RM_PartitionFirstBP): %s",
					      bg_err_str(rc));
				}
				first_mp = 0;
			} else {
				if ((rc = bridge_set_data(bg_record->bg_block,
							  RM_PartitionNextBP,
							  curr_mp))
				    != SLURM_SUCCESS) {
					list_iterator_destroy(itr);
					fatal("bridge_set_data"
					      "(RM_PartitionNextBP): %s",
					      bg_err_str(rc));
				}
			}
#endif
		}
#if defined HAVE_BG_FILES
		if ((rc = bridge_get_data(curr_mp, RM_BPID, &mpid))
		    != SLURM_SUCCESS) {
			list_iterator_destroy(itr);
			fatal("bridge_get_data: RM_BPID: %s",
			      bg_err_str(rc));
		}

		if (!mpid) {
			error("No BP ID was returned from database");
			continue;
		}
		if (_get_switches_by_mpid(bg, mpid, coord_switch)
		    != SLURM_SUCCESS) {
			error("Didn't get all the switches for mp %s", mpid);
			free(mpid);
			continue;
		}
		free(mpid);
		for(i=0; i<SYSTEM_DIMENSIONS; i++) {
			if (_add_switch_conns(coord_switch[i],
					      &ba_node->axis_switch[i])
			    == SLURM_SUCCESS) {
				if (bg_conf->slurm_debug_flags
				    & DEBUG_FLAG_BG_WIRES)
					info("adding switch dim %d", i);
				if (first_switch){
					if ((rc = bridge_set_data(
						     bg_record->bg_block,
						     RM_PartitionFirstSwitch,
						     coord_switch[i]))
					    != SLURM_SUCCESS) {
						fatal("bridge_set_data("
						      "RM_PartitionFirst"
						      "Switch): %s",
						      bg_err_str(rc));
					}

					first_switch = 0;
				} else {
					if ((rc = bridge_set_data(
						     bg_record->bg_block,
						     RM_PartitionNextSwitch,
						     coord_switch[i]))
					    != SLURM_SUCCESS) {
						fatal("bridge_set_data("
						      "RM_PartitionNext"
						      "Switch): %s",
						      bg_err_str(rc));
					}
				}
			}
		}
#endif
	}
	rc = SLURM_SUCCESS;
#if defined HAVE_BG_FILES
cleanup:
#endif
	return rc;
}

