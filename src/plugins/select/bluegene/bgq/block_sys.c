/*****************************************************************************\
 *  block_sys.c - component used for wiring up the blocks
 *
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov> and Danny Auble <da@llnl.gov>
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#include "bluegene.h"
#include "src/common/uid.h"
#include <fcntl.h>

/** these are used in the dynamic partitioning algorithm */

/* global system = list of free blocks */
List bg_sys_free = NULL;
/* global system = list of allocated blocks */
List bg_sys_allocated = NULL;


#define MAX_ADD_RETRY 2

#if 0
/* Vestigial
 * print out a list
 */
static void _print_list(List list)
{
	int* stuff = NULL, i = 0;
	ListIterator itr;

	if (list == NULL)
		return;

	debug("trying to get the list iterator");
	itr = list_iterator_create(list);
	debug("done");

	debug("printing list");
	while ((stuff = (int*) list_next(itr))) {
		debug("stuff %d", stuff);
		if (stuff == NULL){
			break;
		}

		debug("[ %d", stuff[0]);
		for (i=1; i<SYSTEM_DIMENSIONS; i++){
			debug(" x %d", stuff[i]);
		}
		debug(" ]");
	}
	list_iterator_destroy(itr);
}
#endif

#if defined HAVE_BG_FILES && defined HAVE_BG_Q

static int _set_ionodes(bg_record_t *bg_record, int io_start, int io_nodes)
{
	char bitstring[BITSIZE];

	if (!bg_record)
		return SLURM_ERROR;

	bg_record->ionode_bitmap = bit_alloc(bg_conf->numpsets);
	/* Set the correct ionodes being used in this block */
	bit_nset(bg_record->ionode_bitmap, io_start, io_start+io_nodes);
	bit_fmt(bitstring, BITSIZE, bg_record->ionode_bitmap);
	bg_record->ionodes = xstrdup(bitstring);
	return SLURM_SUCCESS;
}


#ifdef HAVE_BGL
extern int find_nodecard_num(rm_partition_t *block_ptr, rm_nodecard_t *ncard,
			     int *nc_id)
{
	char *my_card_name = NULL;
	char *card_name = NULL;
	rm_bp_id_t bp_id = NULL;
	int num = 0;
	int i=0;
	int rc;
	rm_nodecard_list_t *ncard_list = NULL;
	rm_BP_t *curr_mp = NULL;
	rm_nodecard_t *ncard2;

	xassert(block_ptr);
	xassert(nc_id);

	if ((rc = bridge_get_data(ncard,
				  RM_NodeCardID,
				  &my_card_name))
	    != STATUS_OK) {
		error("bridge_get_data(RM_NodeCardID): %s",
		      bridge_err_str(rc));
	}

	if ((rc = bridge_get_data(block_ptr,
				  RM_PartitionFirstBP,
				  &curr_mp))
	    != STATUS_OK) {
		error("bridge_get_data(RM_PartitionFirstBP): %s",
		      bridge_err_str(rc));
	}
	if ((rc = bridge_get_data(curr_mp, RM_BPID, &mp_id))
	    != STATUS_OK) {
		error("bridge_get_data(RM_BPID): %d", rc);
		return SLURM_ERROR;
	}

	if ((rc = bridge_get_nodecards(mp_id, &ncard_list))
	    != STATUS_OK) {
		error("bridge_get_nodecards(%s): %d",
		      mp_id, rc);
		free(mp_id);
		return SLURM_ERROR;
	}
	free(mp_id);
	if ((rc = bridge_get_data(ncard_list, RM_NodeCardListSize, &num))
	    != STATUS_OK) {
		error("bridge_get_data(RM_NodeCardListSize): %s",
		      bridge_err_str(rc));
		return SLURM_ERROR;
	}

	for(i=0; i<num; i++) {
		if (i) {
			if ((rc =
			     bridge_get_data(ncard_list,
					     RM_NodeCardListNext,
					     &ncard2)) != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_NodeCardListNext): %s",
				      bridge_err_str(rc));
				rc = SLURM_ERROR;
				goto cleanup;
			}
		} else {
			if ((rc = bridge_get_data(ncard_list,
						  RM_NodeCardListFirst,
						  &ncard2)) != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_NodeCardListFirst: %s",
				      bridge_err_str(rc));
				rc = SLURM_ERROR;
				goto cleanup;
			}
		}
		if ((rc = bridge_get_data(ncard2,
					  RM_NodeCardID,
					  &card_name)) != STATUS_OK) {
			error("bridge_get_data(RM_NodeCardID: %s",
			      bridge_err_str(rc));
			rc = SLURM_ERROR;
			goto cleanup;
		}
		if (strcmp(my_card_name, card_name)) {
			free(card_name);
			continue;
		}
		free(card_name);
		(*nc_id) = i;
		break;
	}
cleanup:
	free(my_card_name);
	return SLURM_SUCCESS;
}
#endif
#endif

#if defined HAVE_BG_FILES && defined HAVE_BG_Q
/*
 * Download from MMCS the initial BG block information
 */
int read_bg_blocks(List curr_block_list)
{
	int rc = SLURM_SUCCESS;

	int mp_cnt, i, nc_cnt, io_cnt;
	rm_element_t *mp_ptr = NULL;
	rm_bp_id_t mpid;
	rm_partition_t *block_ptr = NULL;
	char node_name_tmp[255], *user_name = NULL;
	bg_record_t *bg_record = NULL;
	uid_t my_uid;

	uint16_t *coord = NULL;
	int block_number, block_count;
	char *tmp_char = NULL;

	rm_partition_list_t *block_list = NULL;
	rm_partition_state_flag_t state = PARTITION_ALL_FLAG;
	rm_nodecard_t *ncard = NULL;
	int nc_id, io_start;

	bool small = false;
	hostlist_t hostlist;		/* expanded form of hosts */

	set_bp_map();

	if (bg_recover) {
		if ((rc = bridge_get_blocks(state, &block_list))
		    != STATUS_OK) {
			error("2 rm_get_blocks(): %s", bridge_err_str(rc));
			return SLURM_ERROR;
		}
	} else {
		if ((rc = bridge_get_blocks_info(state, &block_list))
		    != STATUS_OK) {
			error("2 rm_get_blocks_info(): %s", bridge_err_str(rc));
			return SLURM_ERROR;
		}
	}

	if ((rc = bridge_get_data(block_list, RM_PartListSize, &block_count))
	    != STATUS_OK) {
		error("bridge_get_data(RM_PartListSize): %s",
		      bridge_err_str(rc));
		block_count = 0;
	}

	info("querying the system for existing blocks");
	for(block_number=0; block_number<block_count; block_number++) {
		if (block_number) {
			if ((rc = bridge_get_data(block_list,
						  RM_PartListNextPart,
						  &block_ptr)) != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_PartListNextPart): %s",
				      bridge_err_str(rc));
				break;
			}
		} else {
			if ((rc = bridge_get_data(block_list,
						  RM_PartListFirstPart,
						  &block_ptr)) != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_PartListFirstPart): %s",
				      bridge_err_str(rc));
				break;
			}
		}

		if ((rc = bridge_get_data(block_ptr, RM_PartitionID,
					  &tmp_char))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionID): %s",
			      bridge_err_str(rc));
			continue;
		}

		if (!tmp_char) {
			error("No Block ID was returned from database");
			continue;
		}

		if (strncmp("RMP", tmp_char, 3)) {
			free(tmp_char);
			continue;
		}

		/* New BG Block record */

		bg_record = xmalloc(sizeof(bg_record_t));
		bg_record->magic = BLOCK_MAGIC;
		list_push(curr_block_list, bg_record);

		bg_record->bg_block_id = xstrdup(tmp_char);
		free(tmp_char);

		bg_record->state = NO_VAL;
#ifndef HAVE_BGL
		if ((rc = bridge_get_data(block_ptr,
					  RM_PartitionSize,
					  &mp_cnt))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionSize): %s",
			      bridge_err_str(rc));
			continue;
		}

		if (mp_cnt==0)
			continue;

		bg_record->node_cnt = mp_cnt;
		bg_record->cpu_cnt = bg_conf->cpu_ratio * bg_record->node_cnt;
#endif
		bg_record->job_running = NO_JOB_RUNNING;

		if ((rc = bridge_get_data(block_ptr,
					  RM_PartitionBPNum,
					  &mp_cnt))
		    != STATUS_OK) {
			error("bridge_get_data(RM_BPNum): %s",
			      bridge_err_str(rc));
			continue;
		}

		if (mp_cnt==0)
			continue;
		bg_record->mp_count = mp_cnt;

		debug3("has %d MPs", bg_record->mp_count);

		if ((rc = bridge_get_data(block_ptr, RM_PartitionSwitchNum,
					  &bg_record->switch_count))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionSwitchNum): %s",
			      bridge_err_str(rc));
			continue;
		}

		if ((rc = bridge_get_data(block_ptr, RM_PartitionSmall,
					  &small))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionSmall): %s",
			      bridge_err_str(rc));
			continue;
		}

		if (small) {
			if ((rc = bridge_get_data(block_ptr,
						  RM_PartitionOptions,
						  &tmp_char))
			    != STATUS_OK) {
				error("bridge_get_data(RM_PartitionOptions): "
				      "%s", bridge_err_str(rc));
				continue;
			} else if (tmp_char) {
				switch(tmp_char[0]) {
				case 's':
					bg_record->conn_type = SELECT_HTC_S;
					break;
				case 'd':
					bg_record->conn_type = SELECT_HTC_D;
					break;
				case 'v':
					bg_record->conn_type = SELECT_HTC_V;
					break;
				case 'l':
					bg_record->conn_type = SELECT_HTC_L;
					break;
				default:
					bg_record->conn_type = SELECT_SMALL;
					break;
				}

				free(tmp_char);
			} else
				bg_record->conn_type = SELECT_SMALL;

			if ((rc = bridge_get_data(block_ptr,
						  RM_PartitionFirstNodeCard,
						  &ncard))
			    != STATUS_OK) {
				error("bridge_get_data("
				      "RM_PartitionFirstNodeCard): %s",
				      bridge_err_str(rc));
				continue;
			}

			if ((rc = bridge_get_data(block_ptr,
						  RM_PartitionNodeCardNum,
						  &nc_cnt))
			    != STATUS_OK) {
				error("bridge_get_data("
				      "RM_PartitionNodeCardNum): %s",
				      bridge_err_str(rc));
				continue;
			}
#ifdef HAVE_BGL
			/* Translate nodecard count to ionode count */
			if ((io_cnt = nc_cnt * bg_conf->io_ratio))
				io_cnt--;

			nc_id = 0;
			if (nc_cnt == 1)
				find_nodecard_num(block_ptr, ncard, &nc_id);

			bg_record->node_cnt =
				nc_cnt * bg_conf->nodecard_node_cnt;
			bg_record->cpu_cnt =
				bg_conf->cpu_ratio * bg_record->node_cnt;

			if ((rc = bridge_get_data(ncard,
						  RM_NodeCardQuarter,
						  &io_start)) != STATUS_OK) {
				error("bridge_get_data(CardQuarter): %d",rc);
				continue;
			}
			io_start *= bg_conf->quarter_ionode_cnt;
			io_start += bg_conf->nodecard_ionode_cnt * (nc_id%4);
#else
			/* Translate nodecard count to ionode count */
			if ((io_cnt = nc_cnt * bg_conf->io_ratio))
				io_cnt--;

			if ((rc = bridge_get_data(ncard,
						  RM_NodeCardID,
						  &tmp_char)) != STATUS_OK) {
				error("bridge_get_data(RM_NodeCardID): %d",rc);
				continue;
			}

			if (!tmp_char)
				continue;

			/* From the first nodecard id we can figure
			   out where to start from with the alloc of ionodes.
			*/
			nc_id = atoi((char*)tmp_char+1);
			free(tmp_char);
			io_start = nc_id * bg_conf->io_ratio;
			if (bg_record->node_cnt < bg_conf->nodecard_node_cnt) {
				rm_ionode_t *ionode;

				/* figure out the ionode we are using */
				if ((rc = bridge_get_data(
					     ncard,
					     RM_NodeCardFirstIONode,
					     &ionode)) != STATUS_OK) {
					error("bridge_get_data("
					      "RM_NodeCardFirstIONode): %d",
					      rc);
					continue;
				}
				if ((rc = bridge_get_data(ionode,
							  RM_IONodeID,
							  &tmp_char))
				    != STATUS_OK) {
					error("bridge_get_data("
					      "RM_NodeCardIONodeNum): %s",
					      bridge_err_str(rc));
					rc = SLURM_ERROR;
					continue;
				}

				if (!tmp_char)
					continue;
				/* just add the ionode num to the
				 * io_start */
				io_start += atoi((char*)tmp_char+1);
				free(tmp_char);
				/* make sure i is 0 since we are only using
				 * 1 ionode */
				io_cnt = 0;
			}
#endif
			if (_set_ionodes(bg_record, io_start, io_cnt)
			    == SLURM_ERROR)
				error("couldn't create ionode_bitmap "
				      "for ionodes %d to %d",
				      io_start, io_start+io_cnt);
			debug3("%s uses ionodes %s",
			       bg_record->bg_block_id,
			       bg_record->ionodes);
		} else {
#ifdef HAVE_BGL
			bg_record->cpu_cnt = bg_conf->cpus_per_mp
				* bg_record->mp_count;
			bg_record->node_cnt =  bg_conf->mp_node_cnt
				* bg_record->mp_count;
#endif
			if ((rc = bridge_get_data(block_ptr,
						  RM_PartitionConnection,
						  &bg_record->conn_type))
			    != STATUS_OK) {
				error("bridge_get_data"
				      "(RM_PartitionConnection): %s",
				      bridge_err_str(rc));
				continue;
			}
			/* Set the bitmap blank here if it is a full
			   node we don't want anything set we also
			   don't want the bg_record->ionodes set.
			*/
			bg_record->ionode_bitmap = bit_alloc(bg_conf->numpsets);
		}

		bg_record->ba_mp_list = get_and_set_block_wiring(
			bg_record->bg_block_id, block_ptr);
		if (!bg_record->ba_mp_list)
			fatal("couldn't get the wiring info for block %s",
			      bg_record->bg_block_id);

		hostlist = hostlist_create(NULL);

		for (i=0; i<mp_cnt; i++) {
			if (i) {
				if ((rc = bridge_get_data(block_ptr,
							  RM_PartitionNextBP,
							  &mp_ptr))
				    != STATUS_OK) {
					error("bridge_get_data(RM_NextBP): %s",
					      bridge_err_str(rc));
					rc = SLURM_ERROR;
					break;
				}
			} else {
				if ((rc = bridge_get_data(block_ptr,
							  RM_PartitionFirstBP,
							  &mp_ptr))
				    != STATUS_OK) {
					error("bridge_get_data"
					      "(RM_FirstBP): %s",
					      bridge_err_str(rc));
					rc = SLURM_ERROR;
					break;
				}
			}
			if ((rc = bridge_get_data(mp_ptr, RM_BPID, &mpid))
			    != STATUS_OK) {
				error("bridge_get_data(RM_BPID): %s",
				      bridge_err_str(rc));
				rc = SLURM_ERROR;
				break;
			}

			if (!mpid) {
				error("No MP ID was returned from database");
				continue;
			}

			coord = find_bp_loc(mpid);

			if (!coord) {
				fatal("Could not find coordinates for "
				      "MP ID %s", (char *) mpid);
			}
			free(mpid);


			snprintf(node_name_tmp,
				 sizeof(node_name_tmp),
				 "%s%c%c%c",
				 bg_conf->slurm_node_prefix,
				 alpha_num[coord[X]], alpha_num[coord[Y]],
				 alpha_num[coord[Z]]);


			hostlist_push(hostlist, node_name_tmp);
		}
		bg_record->nodes = hostlist_ranged_string_xmalloc(hostlist);
		hostlist_destroy(hostlist);
		debug3("got nodes of %s", bg_record->nodes);
		// need to get the 000x000 range for nodes
		// also need to get coords

#ifdef HAVE_BGL
		if ((rc = bridge_get_data(block_ptr, RM_PartitionMode,
					  &bg_record->node_use))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionMode): %s",
			      bridge_err_str(rc));
		}
#endif
		if ((rc = bridge_get_data(block_ptr, RM_PartitionState,
					  &bg_record->state)) != STATUS_OK) {
			error("bridge_get_data(RM_PartitionState): %s",
			      bridge_err_str(rc));
			continue;
		} else if (bg_record->state == RM_PARTITION_CONFIGURING)
			bg_record->boot_state = 1;

		debug3("Block %s is in state %d",
		       bg_record->bg_block_id,
		       bg_record->state);

		process_nodes(bg_record, false);

		/* We can stop processing information now since we
		   don't need to rest of the information to decide if
		   this is the correct block. */
		if (bg_conf->layout_mode == LAYOUT_DYNAMIC) {
			bg_record_t *tmp_record = xmalloc(sizeof(bg_record_t));
			copy_bg_record(bg_record, tmp_record);
			list_push(bg_lists->main, tmp_record);
		}

		if ((rc = bridge_get_data(block_ptr, RM_PartitionUsersNum,
					  &mp_cnt)) != STATUS_OK) {
			error("bridge_get_data(RM_PartitionUsersNum): %s",
			      bridge_err_str(rc));
			continue;
		} else {
			if (mp_cnt==0) {

				bg_record->user_name =
					xstrdup(bg_conf->slurm_user_name);
				bg_record->target_name =
					xstrdup(bg_conf->slurm_user_name);

			} else {
				user_name = NULL;
				if ((rc = bridge_get_data(
					     block_ptr,
					     RM_PartitionFirstUser,
					     &user_name))
				    != STATUS_OK) {
					error("bridge_get_data"
					      "(RM_PartitionFirstUser): %s",
					      bridge_err_str(rc));
					continue;
				}
				if (!user_name) {
					error("No user name was "
					      "returned from database");
					continue;
				}
				bg_record->user_name = xstrdup(user_name);

				if (!bg_record->boot_state)
					bg_record->target_name =
						xstrdup(bg_conf->
							slurm_user_name);
				else
					bg_record->target_name =
						xstrdup(user_name);

				free(user_name);

			}
			if (uid_from_string (bg_record->user_name, &my_uid)<0){
				error("uid_from_string(%s): %m",
				      bg_record->user_name);
			} else {
				bg_record->user_uid = my_uid;
			}
		}

#ifdef HAVE_BGL
		/* get the images of the block */
		if ((rc = bridge_get_data(block_ptr,
					  RM_PartitionBlrtsImg,
					  &user_name))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionBlrtsImg): %s",
			      bridge_err_str(rc));
			continue;
		}
		if (!user_name) {
			error("No BlrtsImg was returned from database");
			continue;
		}
		bg_record->blrtsimage = xstrdup(user_name);

		if ((rc = bridge_get_data(block_ptr,
					  RM_PartitionLinuxImg,
					  &user_name))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionLinuxImg): %s",
			      bridge_err_str(rc));
			continue;
		}
		if (!user_name) {
			error("No LinuxImg was returned from database");
			continue;
		}
		bg_record->linuximage = xstrdup(user_name);

		if ((rc = bridge_get_data(block_ptr,
					  RM_PartitionRamdiskImg,
					  &user_name))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionRamdiskImg): %s",
			      bridge_err_str(rc));
			continue;
		}
		if (!user_name) {
			error("No RamdiskImg was returned from database");
			continue;
		}
		bg_record->ramdiskimage = xstrdup(user_name);

#else
		if ((rc = bridge_get_data(block_ptr,
					  RM_PartitionCnloadImg,
					  &user_name))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionCnloadImg): %s",
			      bridge_err_str(rc));
			continue;
		}
		if (!user_name) {
			error("No CnloadImg was returned from database");
			continue;
		}
		bg_record->linuximage = xstrdup(user_name);

		if ((rc = bridge_get_data(block_ptr,
					  RM_PartitionIoloadImg,
					  &user_name))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionIoloadImg): %s",
			      bridge_err_str(rc));
			continue;
		}
		if (!user_name) {
			error("No IoloadImg was returned from database");
			continue;
		}
		bg_record->ramdiskimage = xstrdup(user_name);

#endif
		if ((rc = bridge_get_data(block_ptr,
					  RM_PartitionMloaderImg,
					  &user_name))
		    != STATUS_OK) {
			error("bridge_get_data(RM_PartitionMloaderImg): %s",
			      bridge_err_str(rc));
			continue;
		}
		if (!user_name) {
			error("No MloaderImg was returned from database");
			continue;
		}
		bg_record->mloaderimage = xstrdup(user_name);
	}
	bridge_free_block_list(block_list);

	return rc;
}

#endif

extern int load_state_file(List curr_block_list, char *dir_name)
{
	int state_fd, i, j=0;
	char *state_file = NULL;
	Buf buffer = NULL;
	char *data = NULL;
	int data_size = 0;
	block_info_msg_t *block_ptr = NULL;
	bg_record_t *bg_record = NULL;
	block_info_t *block_info = NULL;
	bitstr_t *node_bitmap = NULL, *ionode_bitmap = NULL;
	uint16_t geo[SYSTEM_DIMENSIONS];
	char temp[256];
	int data_allocated, data_read = 0;
	char *ver_str = NULL;
	uint32_t ver_str_len;
	int blocks = 0;
	uid_t my_uid;
	int ionodes = 0;
	char *name = NULL;
	struct part_record *part_ptr = NULL;
	char *non_usable_nodes = NULL;
	bitstr_t *bitmap = NULL;
	ListIterator itr = NULL;
	uint16_t protocol_version = (uint16_t)NO_VAL;

	if (!dir_name) {
		debug2("Starting bluegene with clean slate");
		return SLURM_SUCCESS;
	}

	xassert(curr_block_list);

	state_file = xstrdup(dir_name);
	xstrcat(state_file, "/block_state");
	state_fd = open(state_file, O_RDONLY);
	if (state_fd < 0) {
		error("No block state file (%s) to recover", state_file);
		xfree(state_file);
		return SLURM_SUCCESS;
	} else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size],
					 BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error("Read error on %s: %m",
					      state_file);
					break;
				}
			} else if (data_read == 0)	/* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close(state_fd);
	}
	xfree(state_file);

	buffer = create_buf(data, data_size);
	safe_unpackstr_xmalloc(&ver_str, &ver_str_len, buffer);
	debug3("Version string in block_state header is %s", ver_str);
	if (ver_str) {
		if (!strcmp(ver_str, BLOCK_STATE_VERSION)) {
			protocol_version = SLURM_PROTOCOL_VERSION;
		} else if (!strcmp(ver_str, BLOCK_2_1_STATE_VERSION)) {
			protocol_version = SLURM_2_2_PROTOCOL_VERSION;
		} else if (!strcmp(ver_str, BLOCK_2_1_STATE_VERSION)) {
			protocol_version = SLURM_2_1_PROTOCOL_VERSION;
		}
	}

	if (protocol_version == (uint16_t)NO_VAL) {
		error("***********************************************");
		error("Can not recover block state, "
		      "data version incompatible");
		error("***********************************************");
		xfree(ver_str);
		free_buf(buffer);
		return EFAULT;
	}
	xfree(ver_str);
	if (slurm_unpack_block_info_msg(&block_ptr, buffer, protocol_version)
	    == SLURM_ERROR) {
		error("select_p_state_restore: problem unpacking block_info");
		goto unpack_error;
	}

#if defined HAVE_BG_FILES && defined HAVE_BGQ
	for (i=0; i<block_ptr->record_count; i++) {
		block_info = &(block_ptr->block_array[i]);

		/* we only care about the states we need here
		 * everthing else should have been set up already */
		if (block_info->state == BG_BLOCK_ERROR) {
			slurm_mutex_lock(&block_state_mutex);
			if ((bg_record = find_bg_record_in_list(
				     curr_block_list,
				     block_info->bg_block_id)))
				/* put_block_in_error_state should be
				   called after the bg_lists->main has been
				   made.  We can't call it here since
				   this record isn't the record kept
				   around in bg_lists->main.
				*/
				bg_record->state = block_info->state;
			slurm_mutex_unlock(&block_state_mutex);
		}
	}

	slurm_free_block_info_msg(block_ptr);
	free_buf(buffer);
	return SLURM_SUCCESS;
#endif

	slurm_mutex_lock(&block_state_mutex);
	reset_ba_system(true);

	/* Locks are already in place to protect part_list here */
	bitmap = bit_alloc(node_record_count);
	itr = list_iterator_create(part_list);
	while ((part_ptr = list_next(itr))) {
		/* we only want to use mps that are in partitions */
		if (!part_ptr->node_bitmap) {
			debug4("Partition %s doesn't have any nodes in it.",
			       part_ptr->name);
			continue;
		}
		bit_or(bitmap, part_ptr->node_bitmap);
	}
	list_iterator_destroy(itr);

	bit_not(bitmap);
	if (bit_ffs(bitmap) != -1) {
		fatal("We don't have any nodes in any partitions.  "
		      "Can't create blocks.  "
		      "Please check your slurm.conf.");
	}

	non_usable_nodes = bitmap2node_name(bitmap);
	FREE_NULL_BITMAP(bitmap);

	node_bitmap = bit_alloc(node_record_count);
	ionode_bitmap = bit_alloc(bg_conf->numpsets);
	for (i=0; i<block_ptr->record_count; i++) {
		block_info = &(block_ptr->block_array[i]);

		bit_nclear(node_bitmap, 0, bit_size(node_bitmap) - 1);
		bit_nclear(ionode_bitmap, 0, bit_size(ionode_bitmap) - 1);

		j = 0;
		while (block_info->mp_inx[j] >= 0) {
			if (block_info->mp_inx[j+1]
			    >= node_record_count) {
				fatal("Job state recovered incompatible with "
				      "bluegene.conf. mp=%u state=%d",
				      node_record_count,
				      block_info->mp_inx[j+1]);
			}
			bit_nset(node_bitmap,
				 block_info->mp_inx[j],
				 block_info->mp_inx[j+1]);
			j += 2;
		}

		j = 0;
		while (block_info->ionode_inx[j] >= 0) {
			if (block_info->ionode_inx[j+1]
			    >= bg_conf->numpsets) {
				fatal("Job state recovered incompatible with "
				      "bluegene.conf. ionodes=%u state=%d",
				      bg_conf->numpsets,
				      block_info->ionode_inx[j+1]);
			}
			bit_nset(ionode_bitmap,
				 block_info->ionode_inx[j],
				 block_info->ionode_inx[j+1]);
			j += 2;
		}

		bg_record = xmalloc(sizeof(bg_record_t));
		bg_record->magic = BLOCK_MAGIC;
		bg_record->bg_block_id =
			xstrdup(block_info->bg_block_id);
		bg_record->nodes =
			xstrdup(block_info->nodes);
		bg_record->ionodes =
			xstrdup(block_info->ionodes);
		bg_record->ionode_bitmap = bit_copy(ionode_bitmap);
		/* put_block_in_error_state should be
		   called after the bg_lists->main has been
		   made.  We can't call it here since
		   this record isn't the record kept
		   around in bg_lists->main.
		*/
		bg_record->state = block_info->state;
		bg_record->job_running = NO_JOB_RUNNING;

		bg_record->mp_count = bit_set_count(node_bitmap);
		bg_record->node_cnt = block_info->node_cnt;
		if (bg_conf->mp_node_cnt > bg_record->node_cnt) {
			ionodes = bg_conf->mp_node_cnt
				/ bg_record->node_cnt;
			bg_record->cpu_cnt = bg_conf->cpus_per_mp / ionodes;
		} else {
			bg_record->cpu_cnt = bg_conf->cpus_per_mp
				* bg_record->mp_count;
		}
#ifdef HAVE_BGL
		bg_record->node_use = block_info->node_use;
#endif
		memcpy(bg_record->conn_type, block_info->conn_type,
		       sizeof(bg_record->conn_type));

		process_nodes(bg_record, true);

		bg_record->target_name = xstrdup(bg_conf->slurm_user_name);
		bg_record->user_name = xstrdup(bg_conf->slurm_user_name);

		if (uid_from_string (bg_record->user_name, &my_uid) < 0) {
			error("uid_from_strin(%s): %m",
			      bg_record->user_name);
		} else {
			bg_record->user_uid = my_uid;
		}

#ifdef HAVE_BGL
		bg_record->blrtsimage =
			xstrdup(block_info->blrtsimage);
#endif
		bg_record->linuximage =
			xstrdup(block_info->linuximage);
		bg_record->mloaderimage =
			xstrdup(block_info->mloaderimage);
		bg_record->ramdiskimage =
			xstrdup(block_info->ramdiskimage);

		for(j=0; j<SYSTEM_DIMENSIONS; j++)
			geo[j] = bg_record->geo[j];

		if ((bg_conf->layout_mode == LAYOUT_OVERLAP)
		    || bg_record->full_block) {
			reset_ba_system(false);
		}

		removable_set_mps(non_usable_nodes);
		/* we want the mps that aren't
		 * in this record to mark them as used
		 */
		if (set_all_mps_except(bg_record->nodes)
		    != SLURM_SUCCESS)
			fatal("something happened in "
			      "the load of %s.  "
			      "Did you use smap to "
			      "make the "
			      "bluegene.conf file?",
			      bg_record->bg_block_id);
		if (bg_record->ba_mp_list)
			list_flush(bg_record->ba_mp_list);
		else
			bg_record->ba_mp_list =	list_create(destroy_ba_mp);
		name = set_bg_block(bg_record->ba_mp_list,
				    bg_record->start,
				    geo,
				    bg_record->conn_type);
		reset_all_removed_mps();

		if (!name) {
			error("I was unable to "
			      "make the "
			      "requested block.");
			destroy_bg_record(bg_record);
			continue;
		}


		snprintf(temp, sizeof(temp), "%s%s",
			 bg_conf->slurm_node_prefix,
			 name);

		xfree(name);
		if (strcmp(temp, bg_record->nodes)) {
			fatal("bad wiring in preserved state "
			      "(found %s, but allocated %s) "
			      "YOU MUST COLDSTART",
			      bg_record->nodes, temp);
		}

		bridge_block_create(bg_record);
		blocks++;
		list_push(curr_block_list, bg_record);
		if (bg_conf->layout_mode == LAYOUT_DYNAMIC) {
			bg_record_t *tmp_record = xmalloc(sizeof(bg_record_t));
			copy_bg_record(bg_record, tmp_record);
			list_push(bg_lists->main, tmp_record);
		}
	}

	xfree(non_usable_nodes);
	FREE_NULL_BITMAP(ionode_bitmap);
	FREE_NULL_BITMAP(node_bitmap);

	sort_bg_record_inc_size(curr_block_list);
	slurm_mutex_unlock(&block_state_mutex);

	info("Recovered %d blocks", blocks);
	slurm_free_block_info_msg(block_ptr);
	free_buf(buffer);

	return SLURM_SUCCESS;

unpack_error:
	error("Incomplete block data checkpoint file");
	free_buf(buffer);
	return SLURM_FAILURE;
}
