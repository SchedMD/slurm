/*****************************************************************************\
 *  node_mgr.c - manage the node records of slurm
 *	Note: there is a global node table (node_record_table_ptr), its
 *	hash table (node_hash_table), time stamp (last_node_update) and
 *	configuration list (config_list)
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2010 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov>, et. al.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
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
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#include "config.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#include "src/common/bitstring.h"
#include "src/common/fd.h"
#include "src/common/fetch_config.h"
#include "src/common/gres.h"
#include "src/common/hostlist.h"
#include "src/common/macros.h"
#include "src/common/node_features.h"
#include "src/common/pack.h"
#include "src/common/parse_time.h"
#include "src/common/power.h"
#include "src/common/read_config.h"
#include "src/common/select.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_ext_sensors.h"
#include "src/common/slurm_resource_info.h"
#include "src/common/slurm_mcs.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"

#include "src/slurmctld/agent.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/ping_nodes.h"
#include "src/slurmctld/power_save.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/read_config.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmctld_plugstack.h"
#include "src/slurmctld/state_save.h"
#include "src/common/timers.h"
#include "src/slurmctld/trigger_mgr.h"

/* No need to change we always pack SLURM_PROTOCOL_VERSION */
#define NODE_STATE_VERSION        "PROTOCOL_VERSION"

#define DEFAULT_NODE_REG_MEM_PERCENT 100.0
#define DEFAULT_CLOUD_REG_MEM_PERCENT 90.0

typedef struct {
	uid_t uid;
	part_record_t **visible_parts;
} pack_node_info_t;

/* Global variables */
bitstr_t *avail_node_bitmap = NULL;	/* bitmap of available nodes */
bitstr_t *bf_ignore_node_bitmap = NULL; /* bitmap of nodes to ignore during a
					 * backfill cycle */
bitstr_t *booting_node_bitmap = NULL;	/* bitmap of booting nodes */
bitstr_t *cg_node_bitmap    = NULL;	/* bitmap of completing nodes */
bitstr_t *future_node_bitmap = NULL;	/* bitmap of FUTURE nodes */
bitstr_t *idle_node_bitmap  = NULL;	/* bitmap of idle nodes */
bitstr_t *power_node_bitmap = NULL;	/* bitmap of powered down nodes */
bitstr_t *share_node_bitmap = NULL;  	/* bitmap of sharable nodes */
bitstr_t *up_node_bitmap    = NULL;  	/* bitmap of non-down nodes */
bitstr_t *rs_node_bitmap    = NULL; 	/* bitmap of resuming nodes */

static void 	_dump_node_state(node_record_t *dump_node_ptr, buf_t *buffer);
static void	_drain_node(node_record_t *node_ptr, char *reason,
			    uint32_t reason_uid);
static front_end_record_t * _front_end_reg(
				slurm_node_registration_status_msg_t *reg_msg);
static bool	_is_cloud_hidden(node_record_t *node_ptr);
static void    _make_node_unavail(node_record_t *node_ptr);
static void 	_make_node_down(node_record_t *node_ptr,
				time_t event_time);
static bool	_node_is_hidden(node_record_t *node_ptr,
				pack_node_info_t *pack_info);
static buf_t *_open_node_state_file(char **state_file);
static void 	_pack_node(node_record_t *dump_node_ptr, buf_t *buffer,
			   uint16_t protocol_version, uint16_t show_flags);
static void	_sync_bitmaps(node_record_t *node_ptr, int job_count);
static void	_update_config_ptr(bitstr_t *bitmap,
				   config_record_t *config_ptr);
static int	_update_node_gres(char *node_names, char *gres);
static int	_update_node_weight(char *node_names, uint32_t weight);
static bool 	_valid_node_state_change(uint32_t old, uint32_t new);

/* dump_all_node_state - save the state of all nodes to file */
int dump_all_node_state ( void )
{
	/* Save high-water mark to avoid buffer growth with copies */
	static int high_buffer_size = (1024 * 1024);
	int error_code = 0, inx, log_fd;
	char *old_file, *new_file, *reg_file;
	node_record_t *node_ptr;
	/* Locks: Read config and node */
	slurmctld_lock_t node_read_lock = { READ_LOCK, NO_LOCK, READ_LOCK,
					    NO_LOCK, NO_LOCK };
	buf_t *buffer = init_buf(high_buffer_size);
	DEF_TIMERS;

	START_TIMER;
	/* write header: version, time */
	packstr(NODE_STATE_VERSION, buffer);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time (NULL), buffer);

	/* write node records to buffer */
	lock_slurmctld (node_read_lock);
	for (inx = 0; (node_ptr = next_node(&inx)); inx++) {
		xassert (node_ptr->magic == NODE_MAGIC);
		xassert (node_ptr->config_ptr->magic == CONFIG_MAGIC);
		_dump_node_state (node_ptr, buffer);
	}

	old_file = xstrdup(slurm_conf.state_save_location);
	xstrcat (old_file, "/node_state.old");
	reg_file = xstrdup(slurm_conf.state_save_location);
	xstrcat (reg_file, "/node_state");
	new_file = xstrdup(slurm_conf.state_save_location);
	xstrcat (new_file, "/node_state.new");
	unlock_slurmctld (node_read_lock);

	/* write the buffer to file */
	lock_state_files();
	log_fd = creat (new_file, 0600);
	if (log_fd < 0) {
		error ("Can't save state, error creating file %s %m", new_file);
		error_code = errno;
	} else {
		int pos = 0, nwrite = get_buf_offset(buffer), amount, rc;
		char *data = (char *)get_buf_data(buffer);
		high_buffer_size = MAX(nwrite, high_buffer_size);
		while (nwrite > 0) {
			amount = write(log_fd, &data[pos], nwrite);
			if ((amount < 0) && (errno != EINTR)) {
				error("Error writing file %s, %m", new_file);
				error_code = errno;
				break;
			}
			nwrite -= amount;
			pos    += amount;
		}

		rc = fsync_and_close(log_fd, "node");
		if (rc && !error_code)
			error_code = rc;
	}
	if (error_code)
		(void) unlink (new_file);
	else {	/* file shuffle */
		(void) unlink (old_file);
		if (link(reg_file, old_file))
			debug4("unable to create link for %s -> %s: %m",
			       reg_file, old_file);
		(void) unlink (reg_file);
		if (link(new_file, reg_file))
			debug4("unable to create link for %s -> %s: %m",
			       new_file, reg_file);
		(void) unlink (new_file);
	}
	xfree (old_file);
	xfree (reg_file);
	xfree (new_file);
	unlock_state_files ();

	free_buf (buffer);
	END_TIMER2("dump_all_node_state");
	return error_code;
}

/*
 * _dump_node_state - dump the state of a specific node to a buffer
 * IN dump_node_ptr - pointer to node for which information is requested
 * IN/OUT buffer - location to store data, pointers automatically advanced
 */
static void _dump_node_state(node_record_t *dump_node_ptr, buf_t *buffer)
{
	packstr (dump_node_ptr->comm_name, buffer);
	packstr (dump_node_ptr->name, buffer);
	packstr (dump_node_ptr->node_hostname, buffer);
	packstr (dump_node_ptr->comment, buffer);
	packstr (dump_node_ptr->extra, buffer);
	packstr (dump_node_ptr->reason, buffer);
	packstr (dump_node_ptr->features, buffer);
	packstr (dump_node_ptr->features_act, buffer);
	packstr (dump_node_ptr->gres, buffer);
	packstr (dump_node_ptr->cpu_spec_list, buffer);
	pack32  (dump_node_ptr->next_state, buffer);
	pack32  (dump_node_ptr->node_state, buffer);
	pack32  (dump_node_ptr->cpu_bind, buffer);
	pack16  (dump_node_ptr->cpus, buffer);
	pack16  (dump_node_ptr->boards, buffer);
	pack16  (dump_node_ptr->tot_sockets, buffer);
	pack16  (dump_node_ptr->cores, buffer);
	pack16  (dump_node_ptr->core_spec_cnt, buffer);
	pack16  (dump_node_ptr->threads, buffer);
	pack64  (dump_node_ptr->real_memory, buffer);
	pack32  (dump_node_ptr->tmp_disk, buffer);
	pack32  (dump_node_ptr->reason_uid, buffer);
	pack_time(dump_node_ptr->reason_time, buffer);
	pack_time(dump_node_ptr->boot_req_time, buffer);
	pack_time(dump_node_ptr->power_save_req_time, buffer);
	pack_time(dump_node_ptr->last_response, buffer);
	pack16  (dump_node_ptr->protocol_version, buffer);
	packstr (dump_node_ptr->mcs_label, buffer);
	(void) gres_node_state_pack(dump_node_ptr->gres_list, buffer,
				    dump_node_ptr->name);
	pack32(dump_node_ptr->weight, buffer);
}


/* Open the node state save file, or backup if necessary.
 * state_file IN - the name of the state save file used
 * RET the file description to read from or error code
 */
static buf_t *_open_node_state_file(char **state_file)
{
	buf_t *buf;

	*state_file = xstrdup(slurm_conf.state_save_location);
	xstrcat(*state_file, "/node_state");

	if (!(buf = create_mmap_buf(*state_file)))
		error("Could not open node state file %s: %m", *state_file);
	else
		return buf;

	error("NOTE: Trying backup state save file. Information may be lost!");
	xstrcat(*state_file, ".old");
	return create_mmap_buf(*state_file);
}

/*
 * load_all_node_state - Load the node state from file, recover on slurmctld
 *	restart. Execute this after loading the configuration file data.
 *	Data goes into common storage.
 * IN state_only - if true, overwrite only node state and reason
 *	Use this to overwrite the "UNKNOWN state typically used in slurm.conf
 * RET 0 or error code
 */
extern int load_all_node_state ( bool state_only )
{
	char *comm_name = NULL, *node_hostname = NULL;
	char *node_name = NULL, *comment = NULL, *reason = NULL, *state_file;
	char *features = NULL, *features_act = NULL;
	char *gres = NULL, *cpu_spec_list = NULL, *extra = NULL;
	char *mcs_label = NULL;
	int error_code = 0, node_cnt = 0;
	uint16_t core_spec_cnt = 0;
	uint32_t node_state, cpu_bind = 0, next_state = NO_VAL;
	uint16_t cpus = 1, boards = 1, sockets = 1, cores = 1, threads = 1;
	uint64_t real_memory;
	uint32_t tmp_disk, name_len, weight = 0;
	uint32_t reason_uid = NO_VAL;
	time_t boot_req_time = 0, reason_time = 0, last_response = 0;
	time_t power_save_req_time = 0;

	List gres_list = NULL;
	node_record_t *node_ptr;
	time_t time_stamp, now = time(NULL);
	buf_t *buffer;
	char *ver_str = NULL;
	hostset_t hs = NULL;
	hostlist_t down_nodes = NULL;
	bool power_save_mode = false;
	uint16_t protocol_version = NO_VAL16;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));

	if (slurm_conf.suspend_program && slurm_conf.resume_program)
		power_save_mode = true;

	/* read the file */
	lock_state_files ();
	buffer = _open_node_state_file(&state_file);
	if (!buffer) {
		info("No node state file (%s) to recover", state_file);
		xfree(state_file);
		unlock_state_files();
		return ENOENT;
	}
	xfree(state_file);
	unlock_state_files();

	safe_unpackstr_xmalloc( &ver_str, &name_len, buffer);
	debug3("Version string in node_state header is %s", ver_str);
	if (ver_str && !xstrcmp(ver_str, NODE_STATE_VERSION))
		safe_unpack16(&protocol_version, buffer);

	if (!protocol_version || (protocol_version == NO_VAL16)) {
		if (!ignore_state_errors)
			fatal("Can not recover node state, data version incompatible, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
		error("*****************************************************");
		error("Can not recover node state, data version incompatible");
		error("*****************************************************");
		xfree(ver_str);
		free_buf(buffer);
		return EFAULT;
	}
	xfree(ver_str);

	safe_unpack_time (&time_stamp, buffer);

	while (remaining_buf (buffer) > 0) {
		uint32_t base_state;
		uint16_t obj_protocol_version = NO_VAL16;
		if (protocol_version >= SLURM_22_05_PROTOCOL_VERSION) {
			uint32_t len;
			safe_unpackstr_xmalloc(&comm_name, &len, buffer);
			safe_unpackstr_xmalloc(&node_name, &len, buffer);
			safe_unpackstr_xmalloc(&node_hostname, &len, buffer);
			safe_unpackstr_xmalloc(&comment, &len, buffer);
			safe_unpackstr_xmalloc(&extra, &len, buffer);
			safe_unpackstr_xmalloc(&reason, &len, buffer);
			safe_unpackstr_xmalloc(&features, &len, buffer);
			safe_unpackstr_xmalloc(&features_act, &len,buffer);
			safe_unpackstr_xmalloc(&gres, &len, buffer);
			safe_unpackstr_xmalloc(&cpu_spec_list, &len, buffer);
			safe_unpack32(&next_state, buffer);
			safe_unpack32(&node_state, buffer);
			safe_unpack32(&cpu_bind, buffer);
			safe_unpack16(&cpus, buffer);
			safe_unpack16(&boards, buffer);
			safe_unpack16(&sockets, buffer);
			safe_unpack16(&cores, buffer);
			safe_unpack16(&core_spec_cnt, buffer);
			safe_unpack16(&threads, buffer);
			safe_unpack64(&real_memory, buffer);
			safe_unpack32(&tmp_disk, buffer);
			safe_unpack32(&reason_uid, buffer);
			safe_unpack_time(&reason_time, buffer);
			safe_unpack_time(&boot_req_time, buffer);
			safe_unpack_time(&power_save_req_time, buffer);
			safe_unpack_time(&last_response, buffer);
			safe_unpack16(&obj_protocol_version, buffer);
			safe_unpackstr_xmalloc(&mcs_label, &name_len, buffer);
			if (gres_node_state_unpack(&gres_list, buffer,
						   node_name,
						   protocol_version) !=
			    SLURM_SUCCESS)
				goto unpack_error;
			safe_unpack32(&weight, buffer);
			base_state = node_state & NODE_STATE_BASE;
		} else if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
			uint32_t len;
			safe_unpackstr_xmalloc(&comm_name, &len, buffer);
			safe_unpackstr_xmalloc(&node_name, &len, buffer);
			safe_unpackstr_xmalloc(&node_hostname, &len, buffer);
			safe_unpackstr_xmalloc(&comment, &len, buffer);
			safe_unpackstr_xmalloc(&extra, &len, buffer);
			safe_unpackstr_xmalloc(&reason, &len, buffer);
			safe_unpackstr_xmalloc(&features, &len, buffer);
			safe_unpackstr_xmalloc(&features_act, &len,buffer);
			safe_unpackstr_xmalloc(&gres, &len, buffer);
			safe_unpackstr_xmalloc(&cpu_spec_list, &len, buffer);
			safe_unpack32(&next_state, buffer);
			safe_unpack32(&node_state, buffer);
			safe_unpack32(&cpu_bind, buffer);
			safe_unpack16(&cpus, buffer);
			safe_unpack16(&boards, buffer);
			safe_unpack16(&sockets, buffer);
			safe_unpack16(&cores, buffer);
			safe_unpack16(&core_spec_cnt, buffer);
			safe_unpack16(&threads, buffer);
			safe_unpack64(&real_memory, buffer);
			safe_unpack32(&tmp_disk, buffer);
			safe_unpack32(&reason_uid, buffer);
			safe_unpack_time(&reason_time, buffer);
			safe_unpack_time(&boot_req_time, buffer);
			safe_unpack_time(&power_save_req_time, buffer);
			safe_unpack_time(&last_response, buffer);
			safe_unpack16(&obj_protocol_version, buffer);
			safe_unpackstr_xmalloc(&mcs_label, &name_len, buffer);
			if (gres_node_state_unpack(&gres_list, buffer,
						   node_name,
						   protocol_version) !=
			    SLURM_SUCCESS)
				goto unpack_error;
			base_state = node_state & NODE_STATE_BASE;
		} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
			uint32_t len;
			safe_unpackstr_xmalloc(&comm_name, &len, buffer);
			safe_unpackstr_xmalloc(&node_name, &len, buffer);
			safe_unpackstr_xmalloc(&node_hostname, &len, buffer);
			safe_unpackstr_xmalloc(&comment, &len, buffer);
			safe_unpackstr_xmalloc(&reason, &len, buffer);
			safe_unpackstr_xmalloc(&features, &len, buffer);
			safe_unpackstr_xmalloc(&features_act, &len,buffer);
			safe_unpackstr_xmalloc(&gres, &len, buffer);
			safe_unpackstr_xmalloc(&cpu_spec_list, &len, buffer);
			safe_unpack32(&next_state, buffer);
			safe_unpack32(&node_state, buffer);
			safe_unpack32(&cpu_bind, buffer);
			safe_unpack16(&cpus, buffer);
			safe_unpack16(&boards, buffer);
			safe_unpack16(&sockets, buffer);
			safe_unpack16(&cores, buffer);
			safe_unpack16(&core_spec_cnt, buffer);
			safe_unpack16(&threads, buffer);
			safe_unpack64(&real_memory, buffer);
			safe_unpack32(&tmp_disk, buffer);
			safe_unpack32(&reason_uid, buffer);
			safe_unpack_time(&reason_time, buffer);
			safe_unpack_time(&boot_req_time, buffer);
			safe_unpack_time(&power_save_req_time, buffer);
			safe_unpack_time(&last_response, buffer);
			safe_unpack16(&obj_protocol_version, buffer);
			safe_unpackstr_xmalloc(&mcs_label, &name_len, buffer);
			if (gres_node_state_unpack(&gres_list, buffer,
						   node_name,
						   protocol_version) !=
			    SLURM_SUCCESS)
				goto unpack_error;
			base_state = node_state & NODE_STATE_BASE;
		} else {
			error("%s: protocol_version %hu not supported",
			      __func__, protocol_version);
			goto unpack_error;
		}

		/* validity test as possible */
		if ((cpus == 0) ||
		    (boards == 0) ||
		    (sockets == 0) ||
		    (cores == 0) ||
		    (threads == 0) ||
		    (base_state  >= NODE_STATE_END)) {
			error("Invalid data for node %s: procs=%u, boards=%u, "
			       "sockets=%u, cores=%u, threads=%u, state=%u",
				node_name, cpus, boards,
				sockets, cores, threads, node_state);
			error("No more node data will be processed from the checkpoint file");
			goto unpack_error;

		}

		if (node_state & NODE_STATE_DYNAMIC_NORM) {
			/* Create node record to restore node into. */
			config_record_t *config_ptr;
			config_ptr = create_config_record();
			config_ptr->boards = boards;
			config_ptr->cores = cores;
			config_ptr->cpus = cpus;
			config_ptr->feature = xstrdup(features);
			config_ptr->gres = xstrdup(gres);
			config_ptr->node_bitmap = bit_alloc(node_record_count);
			config_ptr->nodes = xstrdup(node_name);
			config_ptr->real_memory = real_memory;
			config_ptr->threads = threads;
			config_ptr->tmp_disk = tmp_disk;
			config_ptr->tot_sockets = sockets;
			config_ptr->weight = weight;

			if (!(node_ptr = add_node_record(node_name,
							 config_ptr))) {
				list_delete_ptr(config_list, config_ptr);
			} else {
				/*
				 * add_node_record() populates gres_list but we
				 * want to use the gres_list from state.
				 */
				FREE_NULL_LIST(node_ptr->gres_list);
			}
		}

		/* find record and perform update */
		node_ptr = find_node_record (node_name);
		if (node_ptr == NULL) {
			error ("Node %s has vanished from configuration",
			       node_name);
		} else if (state_only &&
			   !(node_state & NODE_STATE_DYNAMIC_NORM)) {
			uint32_t orig_flags;
			if ((IS_NODE_CLOUD(node_ptr) ||
			    (node_state & NODE_STATE_DYNAMIC_FUTURE)) &&
			    comm_name && node_hostname) {
				/* Recover NodeAddr and NodeHostName */
				set_node_comm_name(node_ptr,
						   comm_name,
						   node_hostname);
			}
			if (IS_NODE_FUTURE(node_ptr) &&
			    (node_state & NODE_STATE_DYNAMIC_FUTURE)) {
				/* Preserve active dynamic future node state */
				node_ptr->node_state    = node_state;

			} else if (IS_NODE_CLOUD(node_ptr)) {
				if ((!power_save_mode) &&
				    ((node_state & NODE_STATE_POWERED_DOWN) ||
				     (node_state & NODE_STATE_POWERING_DOWN) ||
	 			     (node_state & NODE_STATE_POWERING_UP))) {
					node_state &= (~NODE_STATE_POWERED_DOWN);
					node_state &= (~NODE_STATE_POWERING_UP);
					node_state &= (~NODE_STATE_POWERING_DOWN);
					if (hs)
						hostset_insert(hs, node_name);
					else
						hs = hostset_create(node_name);
				}
				/*
				 * Replace FUTURE state with new state (idle),
				 * but preserve recovered state flags
				 * (e.g. POWER*).
				 */
				if ((node_state & NODE_STATE_BASE) ==
				    NODE_STATE_FUTURE) {
					node_state =
						((node_ptr->node_state &
						  NODE_STATE_BASE) |
						 (node_state &
						  NODE_STATE_FLAGS));

					/*
					 * If node was FUTURE, then it wasn't up
					 * so mark it as powered down.
					 */
					if (power_save_mode)
						node_state |=
							NODE_STATE_POWERED_DOWN;
				}

				node_ptr->node_state =
					node_state | NODE_STATE_CLOUD;

			} else if (IS_NODE_UNKNOWN(node_ptr)) {
				if (base_state == NODE_STATE_DOWN) {
					orig_flags = node_ptr->node_state &
						     NODE_STATE_FLAGS;
					node_ptr->node_state = NODE_STATE_DOWN
						| orig_flags;
				}
				if (node_state & NODE_STATE_DRAIN)
					 node_ptr->node_state |=
						 NODE_STATE_DRAIN;
				if (node_state & NODE_STATE_FAIL)
					node_ptr->node_state |=
						NODE_STATE_FAIL;
				if ((node_state & NODE_STATE_POWERED_DOWN) ||
				    (node_state & NODE_STATE_POWERING_DOWN)) {
					uint32_t power_flag =
						node_state &
						(NODE_STATE_POWERED_DOWN |
						 NODE_STATE_POWERING_DOWN);
					if (power_save_mode &&
					    IS_NODE_UNKNOWN(node_ptr)) {
						orig_flags = node_ptr->
							node_state &
							     NODE_STATE_FLAGS;
						node_ptr->node_state =
							NODE_STATE_IDLE |
							orig_flags |
							power_flag;
					} else if (power_save_mode) {
						node_ptr->node_state |=
							power_flag;
					} else if (hs)
						hostset_insert(hs, node_name);
					else
						hs = hostset_create(node_name);
					/* Recover hardware state for powered
					 * down nodes */
					node_ptr->cpus          = cpus;
					node_ptr->boards        = boards;
					node_ptr->tot_sockets = sockets;
					node_ptr->cores         = cores;
					node_ptr->tot_cores = sockets * cores;
					node_ptr->core_spec_cnt =
						core_spec_cnt;
					xfree(node_ptr->cpu_spec_list);
					node_ptr->cpu_spec_list =
						cpu_spec_list;
					cpu_spec_list = NULL;/* Nothing */
							     /* to free */
					node_ptr->threads       = threads;
					node_ptr->real_memory   = real_memory;
					node_ptr->tmp_disk      = tmp_disk;
				}
				if (node_state & NODE_STATE_MAINT)
					node_ptr->node_state |= NODE_STATE_MAINT;
				if (node_state & NODE_STATE_REBOOT_REQUESTED)
					node_ptr->node_state |=
						NODE_STATE_REBOOT_REQUESTED;
				if (node_state & NODE_STATE_REBOOT_ISSUED)
					node_ptr->node_state |=
						NODE_STATE_REBOOT_ISSUED;
				if (node_state & NODE_STATE_POWERING_UP) {
					if (power_save_mode) {
						node_ptr->node_state |=
							NODE_STATE_POWERING_UP;
					} else if (hs)
						hostset_insert(hs, node_name);
					else
						hs = hostset_create(node_name);
				}
			}

			if (!node_ptr->extra) {
				node_ptr->extra = extra;
				extra = NULL;
			}

			if (!node_ptr->comment) {
				node_ptr->comment = comment;
				comment = NULL;
			}

			if (node_ptr->reason == NULL) {
				node_ptr->reason = reason;
				reason = NULL;	/* Nothing to free */
				node_ptr->reason_time = reason_time;
				node_ptr->reason_uid = reason_uid;
			}

			xfree(node_ptr->features_act);
			node_ptr->features_act	= features_act;
			features_act		= NULL;	/* Nothing to free */
			node_ptr->gres_list	= gres_list;
			gres_list		= NULL;	/* Nothing to free */
		} else {
			if ((!power_save_mode) &&
			    ((node_state & NODE_STATE_POWERED_DOWN) ||
			     (node_state & NODE_STATE_POWERING_DOWN) ||
 			     (node_state & NODE_STATE_POWERING_UP))) {
				node_state &= (~NODE_STATE_POWERED_DOWN);
				node_state &= (~NODE_STATE_POWERING_DOWN);
				node_state &= (~NODE_STATE_POWERING_UP);
				if (hs)
					hostset_insert(hs, node_name);
				else
					hs = hostset_create(node_name);
			}
			if ((IS_NODE_CLOUD(node_ptr) ||
			    (node_state & NODE_STATE_DYNAMIC_FUTURE) ||
			    (node_state & NODE_STATE_DYNAMIC_NORM)) &&
			    comm_name && node_hostname) {
				/* Recover NodeAddr and NodeHostName */
				set_node_comm_name(node_ptr,
						   comm_name,
						   node_hostname);
			}
			node_ptr->node_state    = node_state;
			xfree(node_ptr->extra);
			node_ptr->extra = extra;
			extra = NULL; /* Nothing to free */
			xfree(node_ptr->comment);
			node_ptr->comment = comment;
			comment = NULL; /* Nothing to free */
			xfree(node_ptr->reason);
			node_ptr->reason	= reason;
			reason			= NULL;	/* Nothing to free */
			node_ptr->reason_time	= reason_time;
			node_ptr->reason_uid	= reason_uid;
			xfree(node_ptr->features);
			node_ptr->features	= features;
			features		= NULL;	/* Nothing to free */
			xfree(node_ptr->features_act);
			node_ptr->features_act	= features_act;
			features_act		= NULL;	/* Nothing to free */
			xfree(node_ptr->gres);
			node_ptr->gres 		= gres;
			gres			= NULL;	/* Nothing to free */
			node_ptr->gres_list	= gres_list;
			gres_list		= NULL;	/* Nothing to free */
			xfree(node_ptr->cpu_spec_list);
			node_ptr->cpu_spec_list = cpu_spec_list;
			cpu_spec_list 		= NULL; /* Nothing to free */
			node_ptr->part_cnt      = 0;
			xfree(node_ptr->part_pptr);
			node_ptr->cpu_bind      = cpu_bind;
			node_ptr->cpus          = cpus;
			node_ptr->boards        = boards;
			node_ptr->tot_sockets = sockets;
			node_ptr->cores         = cores;
			node_ptr->tot_cores = sockets * cores;
			node_ptr->core_spec_cnt = core_spec_cnt;
			node_ptr->threads       = threads;
			node_ptr->real_memory   = real_memory;
			node_ptr->tmp_disk      = tmp_disk;
			xfree(node_ptr->mcs_label);
			node_ptr->mcs_label	= mcs_label;
			mcs_label		= NULL; /* Nothing to free */
		}

		if (node_ptr) {
			node_cnt++;

			node_ptr->next_state = next_state;

			if (IS_NODE_DOWN(node_ptr)) {
				if (down_nodes)
					hostlist_push(down_nodes, node_name);
				else
					down_nodes = hostlist_create(
							node_name);
			}

			node_ptr->last_response = last_response;
			node_ptr->boot_req_time = boot_req_time;
			node_ptr->power_save_req_time = power_save_req_time;

			if (obj_protocol_version &&
			    (obj_protocol_version != NO_VAL16))
				node_ptr->protocol_version =
					obj_protocol_version;
			else
				node_ptr->protocol_version = protocol_version;

			/* Sanity check to make sure we can take a version we
			 * actually understand.
			 */
			if (node_ptr->protocol_version <
			    SLURM_MIN_PROTOCOL_VERSION)
				node_ptr->protocol_version =
					SLURM_MIN_PROTOCOL_VERSION;

			if (!IS_NODE_POWERED_DOWN(node_ptr))
				node_ptr->last_busy = now;
		}

		xfree(features);
		xfree(features_act);
		xfree(gres);
		FREE_NULL_LIST(gres_list);
		xfree (comm_name);
		xfree (node_hostname);
		xfree (node_name);
		xfree(comment);
		xfree(extra);
		xfree(reason);
		xfree(cpu_spec_list);
	}

fini:	info("Recovered state of %d nodes", node_cnt);
	if (hs) {
		char node_names[128];
		hostset_ranged_string(hs, sizeof(node_names), node_names);
		info("Cleared POWER_SAVE flag from nodes %s", node_names);
		hostset_destroy(hs);
	}

	if (down_nodes) {
		char *down_host_str = NULL;
		down_host_str = hostlist_ranged_string_xmalloc(down_nodes);
		info("Down nodes: %s", down_host_str);
		xfree(down_host_str);
		hostlist_destroy(down_nodes);
	}

	free_buf (buffer);
	return error_code;

unpack_error:
	if (!ignore_state_errors)
		fatal("Incomplete node data checkpoint file, start with '-i' to ignore this. Warning: using -i will lose the data that can't be recovered.");
	error("Incomplete node data checkpoint file");
	error_code = EFAULT;
	xfree(features);
	xfree(gres);
	FREE_NULL_LIST(gres_list);
	xfree(comm_name);
	xfree(node_hostname);
	xfree(node_name);
	xfree(comment);
	xfree(extra);
	xfree(reason);
	goto fini;
}


/* list_compare_config - compare two entry from the config list based upon
 *	weight, see common/list.h for documentation */
int list_compare_config (void *config_entry1, void *config_entry2)
{
	int weight1, weight2;
	config_record_t *c1 = *(config_record_t **) config_entry1;
	config_record_t *c2 = *(config_record_t **) config_entry2;

	weight1 = c1->weight;
	weight2 = c2->weight;

	return (weight1 - weight2);
}

/* Return true if the node should be hidden by virtue of being powered down
 * and in the cloud. */
static bool _is_cloud_hidden(node_record_t *node_ptr)
{
	if (((slurm_conf.private_data & PRIVATE_CLOUD_NODES) == 0) &&
	    IS_NODE_CLOUD(node_ptr) && IS_NODE_POWERED_DOWN(node_ptr))
		return true;
	return false;
}

static bool _node_is_hidden(node_record_t *node_ptr,
			    pack_node_info_t *pack_info)
{
	int i;

	if ((slurm_conf.private_data & PRIVATE_DATA_NODES) &&
	    (slurm_mcs_get_privatedata() == 1) &&
	    (mcs_g_check_mcs_label(pack_info->uid, node_ptr->mcs_label,
				   false) != 0))
		return true;

	if (!node_ptr->part_cnt)
		return false;

	for (i = 0; i < node_ptr->part_cnt; i++) {
		part_record_t *part_ptr = node_ptr->part_pptr[i];
		/* return false if the node belongs to any visible partition */
		for (int j = 0; pack_info->visible_parts[j]; j++)
			if (pack_info->visible_parts[j] == part_ptr)
				return false;
	}

	return true;
}

static void _free_pack_node_info_members(pack_node_info_t *pack_info)
{
	xfree(pack_info->visible_parts);
}

/*
 * pack_all_node - dump all configuration and node information for all nodes
 *	in machine independent form (for network transmission)
 * OUT buffer_ptr - pointer to the stored data
 * OUT buffer_size - set to size of the buffer in bytes
 * IN show_flags - node filtering options
 * IN uid - uid of user making request (for partition filtering)
 * IN protocol_version - slurm protocol version of client
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: the caller must xfree the buffer at *buffer_ptr
 * NOTE: change slurm_load_node() in api/node_info.c when data format changes
 */
extern void pack_all_node(char **buffer_ptr, int *buffer_size,
			  uint16_t show_flags, uid_t uid,
			  uint16_t protocol_version)
{
	int inx;
	uint32_t nodes_packed, tmp_offset;
	buf_t *buffer;
	time_t now = time(NULL);
	node_record_t *node_ptr;
	bool hidden, privileged = validate_operator(uid);
	static bool inited = false;
	static config_record_t blank_config = {0};
	static node_record_t blank_node = {0};
	pack_node_info_t pack_info = {
		.uid = uid,
		.visible_parts = build_visible_parts(uid, privileged)
	};

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(PART_LOCK, READ_LOCK));

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	buffer = init_buf (BUF_SIZE*16);
	nodes_packed = 0;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		/* write header: count and time */
		pack32(nodes_packed, buffer);
		pack_time(now, buffer);

		/* write node records */
		for (inx = 0; inx < node_record_count; inx++) {
			if (!node_record_table_ptr[inx])
				goto pack_empty;
			node_ptr = node_record_table_ptr[inx];
			xassert(node_ptr->magic == NODE_MAGIC);
			xassert(node_ptr->config_ptr->magic == CONFIG_MAGIC);

			/*
			 * We can't avoid packing node records without breaking
			 * the node index pointers. So pack a node with a name
			 * of NULL and let the caller deal with it.
			 */
			hidden = false;
			if (((show_flags & SHOW_ALL) == 0) &&
			    !privileged &&
			    (_node_is_hidden(node_ptr, &pack_info)))
				hidden = true;
			else if (IS_NODE_FUTURE(node_ptr) &&
				 (!(show_flags & SHOW_FUTURE)))
				hidden = true;
			else if (_is_cloud_hidden(node_ptr))
				hidden = true;
			else if ((node_ptr->name == NULL) ||
				 (node_ptr->name[0] == '\0'))
				hidden = true;

			if (hidden) {
pack_empty:
				if (!inited) {
					blank_node.config_ptr = &blank_config;
					blank_node.select_nodeinfo =
						select_g_select_nodeinfo_alloc();
					inited = true;
				}

				_pack_node(&blank_node, buffer, protocol_version,
					   show_flags);
			} else {
				_pack_node(node_ptr, buffer, protocol_version,
					   show_flags);
			}
			nodes_packed++;
		}
	} else {
		error("select_g_select_jobinfo_pack: protocol_version "
		      "%hu not supported", protocol_version);
	}

	tmp_offset = get_buf_offset (buffer);
	set_buf_offset (buffer, 0);
	pack32  (nodes_packed, buffer);
	set_buf_offset (buffer, tmp_offset);

	*buffer_size = get_buf_offset (buffer);
	buffer_ptr[0] = xfer_buf_data (buffer);
	_free_pack_node_info_members(&pack_info);
}

/*
 * pack_one_node - dump all configuration and node information for one node
 *	in machine independent form (for network transmission)
 * OUT buffer_ptr - pointer to the stored data
 * OUT buffer_size - set to size of the buffer in bytes
 * IN show_flags - node filtering options
 * IN uid - uid of user making request (for partition filtering)
 * IN node_name - name of node for which information is desired,
 *		  use first node if name is NULL
 * IN protocol_version - slurm protocol version of client
 * global: node_record_table_ptr - pointer to global node table
 * NOTE: the caller must xfree the buffer at *buffer_ptr
 * NOTE: change slurm_load_node() in api/node_info.c when data format changes
 */
extern void pack_one_node (char **buffer_ptr, int *buffer_size,
			   uint16_t show_flags, uid_t uid, char *node_name,
			   uint16_t protocol_version)
{
	uint32_t nodes_packed, tmp_offset;
	buf_t *buffer;
	time_t now = time(NULL);
	node_record_t *node_ptr;
	bool hidden, privileged = validate_operator(uid);
	pack_node_info_t pack_info = {
		.uid = uid,
		.visible_parts = build_visible_parts(uid, privileged)
	};

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(PART_LOCK, READ_LOCK));

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	buffer = init_buf (BUF_SIZE);
	nodes_packed = 0;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		/* write header: count and time */
		pack32(nodes_packed, buffer);
		pack_time(now, buffer);

		/* write node records */
		if (node_name)
			node_ptr = find_node_record(node_name);
		else
			node_ptr = node_record_table_ptr[0];
		if (node_ptr) {
			hidden = false;
			if (((show_flags & SHOW_ALL) == 0) &&
			    !privileged &&
			    (_node_is_hidden(node_ptr, &pack_info)))
				hidden = true;
			else if (IS_NODE_FUTURE(node_ptr) &&
				 (!(show_flags & SHOW_FUTURE)))
				hidden = true;
//			Don't hide the node if explicitly requested by name
//			else if (_is_cloud_hidden(node_ptr))
//				hidden = true;
			else if ((node_ptr->name == NULL) ||
				 (node_ptr->name[0] == '\0'))
				hidden = true;

			if (!hidden) {
				_pack_node(node_ptr, buffer, protocol_version,
					   show_flags);
				nodes_packed++;
			}
		}
	} else {
		error("select_g_select_jobinfo_pack: protocol_version "
		      "%hu not supported", protocol_version);
	}

	tmp_offset = get_buf_offset (buffer);
	set_buf_offset (buffer, 0);
	pack32  (nodes_packed, buffer);
	set_buf_offset (buffer, tmp_offset);

	*buffer_size = get_buf_offset (buffer);
	buffer_ptr[0] = xfer_buf_data (buffer);
	_free_pack_node_info_members(&pack_info);
}

/*
 * _pack_node - dump all configuration information about a specific node in
 *	machine independent form (for network transmission)
 * IN dump_node_ptr - pointer to node for which information is requested
 * IN/OUT buffer - buffer where data is placed, pointers automatically updated
 * IN protocol_version - slurm protocol version of client
 * IN show_flags -
 * NOTE: if you make any changes here be sure to make the corresponding changes
 * 	to _unpack_node_info_members() in common/slurm_protocol_pack.c
 */
static void _pack_node(node_record_t *dump_node_ptr, buf_t *buffer,
		       uint16_t protocol_version, uint16_t show_flags)
{
	char *gres_drain = NULL, *gres_used = NULL;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));

	if (protocol_version >= SLURM_22_05_PROTOCOL_VERSION) {
		packstr(dump_node_ptr->name, buffer);
		packstr(dump_node_ptr->node_hostname, buffer);
		packstr(dump_node_ptr->comm_name, buffer);
		packstr(dump_node_ptr->bcast_address, buffer);
		pack16(dump_node_ptr->port, buffer);
		pack32(dump_node_ptr->next_state, buffer);
		pack32(dump_node_ptr->node_state, buffer);
		packstr(dump_node_ptr->version, buffer);

		/* Only data from config_record used for scheduling */
		pack16(dump_node_ptr->config_ptr->cpus, buffer);
		pack16(dump_node_ptr->config_ptr->boards, buffer);
		pack16(dump_node_ptr->config_ptr->tot_sockets, buffer);
		pack16(dump_node_ptr->config_ptr->cores, buffer);
		pack16(dump_node_ptr->config_ptr->threads, buffer);
		pack64(dump_node_ptr->config_ptr->real_memory, buffer);
		pack32(dump_node_ptr->config_ptr->tmp_disk, buffer);

		packstr(dump_node_ptr->mcs_label, buffer);
		pack32(dump_node_ptr->owner, buffer);
		pack16(dump_node_ptr->core_spec_cnt, buffer);
		pack32(dump_node_ptr->cpu_bind, buffer);
		pack64(dump_node_ptr->mem_spec_limit, buffer);
		packstr(dump_node_ptr->cpu_spec_list, buffer);
		pack16(dump_node_ptr->cpus_efctv, buffer);

		pack32(dump_node_ptr->cpu_load, buffer);
		pack64(dump_node_ptr->free_mem, buffer);
		pack32(dump_node_ptr->config_ptr->weight, buffer);
		pack32(dump_node_ptr->reason_uid, buffer);

		pack_time(dump_node_ptr->boot_time, buffer);
		pack_time(dump_node_ptr->last_busy, buffer);
		pack_time(dump_node_ptr->reason_time, buffer);
		pack_time(dump_node_ptr->slurmd_start_time, buffer);

		select_g_select_nodeinfo_pack(dump_node_ptr->select_nodeinfo,
					      buffer, protocol_version);

		packstr(dump_node_ptr->arch, buffer);
		packstr(dump_node_ptr->features, buffer);
		packstr(dump_node_ptr->features_act, buffer);
		if (dump_node_ptr->gres)
			packstr(dump_node_ptr->gres, buffer);
		else
			packstr(dump_node_ptr->config_ptr->gres, buffer);

		/* Gathering GRES details is slow, so don't by default */
		if (show_flags & SHOW_DETAIL) {
			gres_drain =
				gres_get_node_drain(dump_node_ptr->gres_list);
			gres_used  =
				gres_get_node_used(dump_node_ptr->gres_list);
		}
		packstr(gres_drain, buffer);
		packstr(gres_used, buffer);
		xfree(gres_drain);
		xfree(gres_used);

		packstr(dump_node_ptr->os, buffer);
		packstr(dump_node_ptr->comment, buffer);
		packstr(dump_node_ptr->extra, buffer);
		packstr(dump_node_ptr->reason, buffer);
		acct_gather_energy_pack(dump_node_ptr->energy, buffer,
					protocol_version);
		ext_sensors_data_pack(dump_node_ptr->ext_sensors, buffer,
				      protocol_version);
		power_mgmt_data_pack(dump_node_ptr->power, buffer,
				     protocol_version);

		packstr(dump_node_ptr->tres_fmt_str, buffer);
	} else if (protocol_version >= SLURM_21_08_PROTOCOL_VERSION) {
		packstr(dump_node_ptr->name, buffer);
		packstr(dump_node_ptr->node_hostname, buffer);
		packstr(dump_node_ptr->comm_name, buffer);
		packstr(dump_node_ptr->bcast_address, buffer);
		pack16(dump_node_ptr->port, buffer);
		pack32(dump_node_ptr->next_state, buffer);
		pack32(dump_node_ptr->node_state, buffer);
		packstr(dump_node_ptr->version, buffer);

		/* Only data from config_record used for scheduling */
		pack16(dump_node_ptr->config_ptr->cpus, buffer);
		pack16(dump_node_ptr->config_ptr->boards, buffer);
		pack16(dump_node_ptr->config_ptr->tot_sockets, buffer);
		pack16(dump_node_ptr->config_ptr->cores, buffer);
		pack16(dump_node_ptr->config_ptr->threads, buffer);
		pack64(dump_node_ptr->config_ptr->real_memory, buffer);
		pack32(dump_node_ptr->config_ptr->tmp_disk, buffer);

		packstr(dump_node_ptr->mcs_label, buffer);
		pack32(dump_node_ptr->owner, buffer);
		pack16(dump_node_ptr->core_spec_cnt, buffer);
		pack32(dump_node_ptr->cpu_bind, buffer);
		pack64(dump_node_ptr->mem_spec_limit, buffer);
		packstr(dump_node_ptr->cpu_spec_list, buffer);

		pack32(dump_node_ptr->cpu_load, buffer);
		pack64(dump_node_ptr->free_mem, buffer);
		pack32(dump_node_ptr->config_ptr->weight, buffer);
		pack32(dump_node_ptr->reason_uid, buffer);

		pack_time(dump_node_ptr->boot_time, buffer);
		pack_time(dump_node_ptr->last_busy, buffer);
		pack_time(dump_node_ptr->reason_time, buffer);
		pack_time(dump_node_ptr->slurmd_start_time, buffer);

		select_g_select_nodeinfo_pack(dump_node_ptr->select_nodeinfo,
					      buffer, protocol_version);

		packstr(dump_node_ptr->arch, buffer);
		packstr(dump_node_ptr->features, buffer);
		packstr(dump_node_ptr->features_act, buffer);
		if (dump_node_ptr->gres)
			packstr(dump_node_ptr->gres, buffer);
		else
			packstr(dump_node_ptr->config_ptr->gres, buffer);

		/* Gathering GRES details is slow, so don't by default */
		if (show_flags & SHOW_DETAIL) {
			gres_drain =
				gres_get_node_drain(dump_node_ptr->gres_list);
			gres_used  =
				gres_get_node_used(dump_node_ptr->gres_list);
		}
		packstr(gres_drain, buffer);
		packstr(gres_used, buffer);
		xfree(gres_drain);
		xfree(gres_used);

		packstr(dump_node_ptr->os, buffer);
		packstr(dump_node_ptr->comment, buffer);
		packstr(dump_node_ptr->extra, buffer);
		packstr(dump_node_ptr->reason, buffer);
		acct_gather_energy_pack(dump_node_ptr->energy, buffer,
					protocol_version);
		ext_sensors_data_pack(dump_node_ptr->ext_sensors, buffer,
				      protocol_version);
		power_mgmt_data_pack(dump_node_ptr->power, buffer,
				     protocol_version);

		packstr(dump_node_ptr->tres_fmt_str, buffer);
	} else if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		packstr(dump_node_ptr->name, buffer);
		packstr(dump_node_ptr->node_hostname, buffer);
		packstr(dump_node_ptr->comm_name, buffer);
		packstr(dump_node_ptr->bcast_address, buffer);
		pack16(dump_node_ptr->port, buffer);
		pack32(dump_node_ptr->next_state, buffer);
		pack32(dump_node_ptr->node_state, buffer);
		packstr(dump_node_ptr->version, buffer);

		/* Only data from config_record used for scheduling */
		pack16(dump_node_ptr->config_ptr->cpus, buffer);
		pack16(dump_node_ptr->config_ptr->boards, buffer);
		pack16(dump_node_ptr->config_ptr->tot_sockets, buffer);
		pack16(dump_node_ptr->config_ptr->cores, buffer);
		pack16(dump_node_ptr->config_ptr->threads, buffer);
		pack64(dump_node_ptr->config_ptr->real_memory, buffer);
		pack32(dump_node_ptr->config_ptr->tmp_disk, buffer);

		packstr(dump_node_ptr->mcs_label, buffer);
		pack32(dump_node_ptr->owner, buffer);
		pack16(dump_node_ptr->core_spec_cnt, buffer);
		pack32(dump_node_ptr->cpu_bind, buffer);
		pack64(dump_node_ptr->mem_spec_limit, buffer);
		packstr(dump_node_ptr->cpu_spec_list, buffer);

		pack32(dump_node_ptr->cpu_load, buffer);
		pack64(dump_node_ptr->free_mem, buffer);
		pack32(dump_node_ptr->config_ptr->weight, buffer);
		pack32(dump_node_ptr->reason_uid, buffer);

		pack_time(dump_node_ptr->boot_time, buffer);
		pack_time(dump_node_ptr->reason_time, buffer);
		pack_time(dump_node_ptr->slurmd_start_time, buffer);

		select_g_select_nodeinfo_pack(dump_node_ptr->select_nodeinfo,
					      buffer, protocol_version);

		packstr(dump_node_ptr->arch, buffer);
		packstr(dump_node_ptr->features, buffer);
		packstr(dump_node_ptr->features_act, buffer);
		if (dump_node_ptr->gres)
			packstr(dump_node_ptr->gres, buffer);
		else
			packstr(dump_node_ptr->config_ptr->gres, buffer);

		/* Gathering GRES details is slow, so don't by default */
		if (show_flags & SHOW_DETAIL) {
			gres_drain =
				gres_get_node_drain(dump_node_ptr->gres_list);
			gres_used  =
				gres_get_node_used(dump_node_ptr->gres_list);
		}
		packstr(gres_drain, buffer);
		packstr(gres_used, buffer);
		xfree(gres_drain);
		xfree(gres_used);

		packstr(dump_node_ptr->os, buffer);
		packstr(dump_node_ptr->comment, buffer);
		packstr(dump_node_ptr->reason, buffer);
		acct_gather_energy_pack(dump_node_ptr->energy, buffer,
					protocol_version);
		ext_sensors_data_pack(dump_node_ptr->ext_sensors, buffer,
				      protocol_version);
		power_mgmt_data_pack(dump_node_ptr->power, buffer,
				     protocol_version);

		packstr(dump_node_ptr->tres_fmt_str, buffer);
	} else {
		error("_pack_node: protocol_version "
		      "%hu not supported", protocol_version);
	}
}

/* Return "true" if a node's state is already "new_state". This is more
 * complex than simply comparing the state values due to flags (e.g.
 * A node might be DOWN + NO_RESPOND or IDLE + DRAIN) */
static bool _equivalent_node_state(node_record_t *node_ptr, uint32_t new_state)
{
	if (new_state == NO_VAL)	/* No change */
		return true;
	if ((new_state == NODE_STATE_DOWN)  && IS_NODE_DOWN(node_ptr))
		return true;
	if ((new_state == NODE_STATE_DRAIN) && IS_NODE_DRAIN(node_ptr))
		return true;
	if ((new_state == NODE_STATE_FAIL)  && IS_NODE_FAIL(node_ptr))
		return true;
	/* Other states might be added here */
	return false;
}

/* Confirm that the selected ActiveFeatures are a subset of AvailableFeatures */
static bool _valid_features_act(char *features_act, char *features)
{
	bool valid_subset = true;
	char *tmp_act, *last_act = NULL, *tok_act;
	char *tmp_avail, *last_avail = NULL, *tok_avail;

	if (!features_act || (features_act[0] == '\0'))
		return true;
	if (!features || (features[0] == '\0'))
		return false;

	tmp_act = xstrdup(features_act);
        tok_act = strtok_r(tmp_act, ",", &last_act);
        while (tok_act) {
		last_avail = NULL;
		tmp_avail = xstrdup(features);
		tok_avail = strtok_r(tmp_avail, ",", &last_avail);
		while (tok_avail) {
			if (!xstrcmp(tok_act, tok_avail))
				break;
		        tok_avail = strtok_r(NULL, ",", &last_avail);
		}
		xfree(tmp_avail);
		if (!tok_avail) {	/* No match found */
			valid_subset = false;
			break;
		}
                tok_act = strtok_r(NULL, ",", &last_act);
	}
	xfree(tmp_act);

	return valid_subset;
}

static void _undo_reboot_asap(node_record_t *node_ptr)
{
	node_ptr->node_state &= (~NODE_STATE_DRAIN);
	xfree(node_ptr->reason);
}

static void _require_node_reg(node_record_t *node_ptr)
{
#ifndef HAVE_FRONT_END
	node_ptr->node_state |= NODE_STATE_NO_RESPOND;
#endif
	node_ptr->last_response = time(NULL);
	node_ptr->boot_time = 0;
	ping_nodes_now = true;
}

int update_node(update_node_msg_t *update_node_msg, uid_t auth_uid)
{
	int error_code = 0, node_cnt;
	node_record_t *node_ptr = NULL;
	char *this_node_name = NULL, *tmp_feature, *orig_features_act = NULL;
	hostlist_t host_list, hostaddr_list = NULL, hostname_list = NULL;
	uint32_t base_state = 0, node_flags, state_val;
	time_t now = time(NULL);

	if (update_node_msg->node_names == NULL ) {
		info("%s: invalid node name", __func__);
		return ESLURM_INVALID_NODE_NAME;
	}

	if (!(host_list = nodespec_to_hostlist(update_node_msg->node_names,
					       NULL)))
		return ESLURM_INVALID_NODE_NAME;

	if (!(node_cnt = hostlist_count(host_list))) {
		info("%s: expansion of node specification '%s' resulted in zero nodes",
		     __func__, update_node_msg->node_names);
		FREE_NULL_HOSTLIST(host_list);
		return ESLURM_INVALID_NODE_NAME;
	}

	if (update_node_msg->node_addr) {
		hostaddr_list = hostlist_create(update_node_msg->node_addr);
		if (hostaddr_list == NULL) {
			info("update_node: hostlist_create error on %s: %m",
			     update_node_msg->node_addr);
			FREE_NULL_HOSTLIST(host_list);
			return ESLURM_INVALID_NODE_NAME;
		}
		if (node_cnt != hostlist_count(hostaddr_list)) {
			info("update_node: nodecount mismatch");
			FREE_NULL_HOSTLIST(host_list);
			FREE_NULL_HOSTLIST(hostaddr_list);
			return ESLURM_INVALID_NODE_NAME;
		}
	}

	if (update_node_msg->node_hostname) {
		hostname_list = hostlist_create(update_node_msg->node_hostname);
		if (hostname_list == NULL) {
			info("update_node: hostlist_create error on %s: %m",
			     update_node_msg->node_hostname);
			FREE_NULL_HOSTLIST(host_list);
			FREE_NULL_HOSTLIST(hostaddr_list);
			return ESLURM_INVALID_NODE_NAME;
		}
		if (node_cnt != hostlist_count(hostname_list)) {
			info("update_node: nodecount mismatch");
			FREE_NULL_HOSTLIST(host_list);
			FREE_NULL_HOSTLIST(hostaddr_list);
			FREE_NULL_HOSTLIST(hostname_list);
			return ESLURM_INVALID_NODE_NAME;
		}
	}

	while ( (this_node_name = hostlist_shift (host_list)) ) {
		int err_code = 0;
		bool acct_updated = false;

		node_ptr = find_node_record (this_node_name);
		if (node_ptr == NULL) {
			error ("update_node: node %s does not exist",
				this_node_name);
			error_code = ESLURM_INVALID_NODE_NAME;
			free (this_node_name);
			break;
		}

		if (hostaddr_list) {
			char *this_addr = hostlist_shift(hostaddr_list);
			xfree(node_ptr->comm_name);
			node_ptr->comm_name = xstrdup(this_addr);
			free(this_addr);
		}
		if (hostname_list) {
			char *this_hostname = hostlist_shift(hostname_list);
			xfree(node_ptr->node_hostname);
			node_ptr->node_hostname = xstrdup(this_hostname);
			free(this_hostname);
		}
		if (hostaddr_list || hostname_list) {
			/* This updates the lookup table addresses */
			slurm_reset_alias(node_ptr->name, node_ptr->comm_name,
					  node_ptr->node_hostname);
		}

		if (update_node_msg->cpu_bind) {
			char tmp_str[128];
			slurm_sprint_cpu_bind_type(tmp_str,
						   update_node_msg->cpu_bind);
			info("update_node: setting CpuBind to %s for node %s",
			     tmp_str, this_node_name);
			if (update_node_msg->cpu_bind == CPU_BIND_OFF)
				node_ptr->cpu_bind = 0;
			else
				node_ptr->cpu_bind = update_node_msg->cpu_bind;
		}

		if (update_node_msg->features || update_node_msg->features_act) {
			char *features_act = NULL, *features_avail = NULL;
			if (!node_features_g_node_update_valid(node_ptr,
							 update_node_msg)) {
				error_code = ESLURM_INVALID_FEATURE;
				xfree(update_node_msg->features);
				xfree(update_node_msg->features_act);
			}
			if (update_node_msg->features_act)
				features_act = update_node_msg->features_act;
			else
				features_act = node_ptr->features_act;

			if (update_node_msg->features)
				features_avail = update_node_msg->features;
			else
				features_avail = node_ptr->features;
			if (!_valid_features_act(features_act, features_avail)){
				info("%s: Invalid ActiveFeatures (\'%s\' not subset of \'%s\' on node %s)",
				     __func__, features_act, features_avail,
				     node_ptr->name);
				error_code = ESLURM_ACTIVE_FEATURE_NOT_SUBSET;
				xfree(update_node_msg->features);
				xfree(update_node_msg->features_act);
			}
		}

		if (update_node_msg->features_act) {
			if (node_ptr->features_act)
				orig_features_act =
					xstrdup(node_ptr->features_act);
			else
				orig_features_act = xstrdup(node_ptr->features);
		}
		if (update_node_msg->features) {
			if (!update_node_msg->features_act &&
			    (node_features_g_count() == 0)) {
				/*
				 * If no NodeFeatures plugin and no explicit
				 * active features, then make active and
				 * available feature values match
				 */
				update_node_msg->features_act =
					xstrdup(update_node_msg->features);
			}
			xfree(node_ptr->features);
			if (update_node_msg->features[0]) {
				node_ptr->features =
					node_features_g_node_xlate2(
						update_node_msg->features);
			}
			/*
			 * update_node_avail_features() logs and updates
			 * avail_feature_list below
			 */
		}

		if (update_node_msg->features_act) {
			tmp_feature = node_features_g_node_xlate(
					update_node_msg->features_act,
					orig_features_act, node_ptr->features,
					node_ptr->index);
			xfree(node_ptr->features_act);
			node_ptr->features_act = tmp_feature;
			error_code = update_node_active_features(
						node_ptr->name,
						node_ptr->features_act,
						FEATURE_MODE_COMB);
			xfree(orig_features_act);
		}

		if (update_node_msg->gres) {
			xfree(node_ptr->gres);
			if (update_node_msg->gres[0])
				node_ptr->gres = xstrdup(update_node_msg->gres);
			/* _update_node_gres() logs and updates config */
		}

		if (update_node_msg->extra) {
			xfree(node_ptr->extra);
			if (update_node_msg->extra[0])
				node_ptr->extra = xstrdup(
					update_node_msg->extra);
		}

		if (update_node_msg->comment) {
			xfree(node_ptr->comment);
			if (update_node_msg->comment[0])
				node_ptr->comment =
					xstrdup(update_node_msg->comment);
		}

		/* No accounting update if node state and reason are unchange */
		state_val = update_node_msg->node_state;
		if (_equivalent_node_state(node_ptr, state_val) &&
		    !xstrcmp(node_ptr->reason, update_node_msg->reason)) {
			free(this_node_name);
			continue;
		}

		if ((update_node_msg -> reason) &&
		    (update_node_msg -> reason[0])) {
			xfree(node_ptr->reason);
			node_ptr->reason = xstrdup(update_node_msg->reason);
			node_ptr->reason_time = now;
			node_ptr->reason_uid = auth_uid;
			info ("update_node: node %s reason set to: %s",
				this_node_name, node_ptr->reason);
		}

		if (state_val != NO_VAL) {
			base_state = node_ptr->node_state;
			if (!_valid_node_state_change(base_state, state_val)) {
				info("Invalid node state transition requested "
				     "for node %s from=%s to=%s",
				     this_node_name,
				     node_state_string(base_state),
				     node_state_string(state_val));
				state_val = NO_VAL;
				error_code = ESLURM_INVALID_NODE_STATE;
			}
			base_state &= NODE_STATE_BASE;
		}

		if (state_val != NO_VAL) {
			node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
			if (state_val == NODE_RESUME) {
				if (IS_NODE_IDLE(node_ptr) &&
				    (IS_NODE_DRAIN(node_ptr) ||
				     IS_NODE_FAIL(node_ptr))) {
					clusteracct_storage_g_node_up(
						acct_db_conn,
						node_ptr,
						now);
					acct_updated = true;
				}
				node_ptr->node_state &= (~NODE_STATE_DRAIN);
				node_ptr->node_state &= (~NODE_STATE_FAIL);
				node_ptr->node_state &=
					(~NODE_STATE_REBOOT_REQUESTED);
				node_ptr->node_state &=
					(~NODE_STATE_REBOOT_ISSUED);

				if (IS_NODE_POWERING_DOWN(node_ptr)) {
					node_ptr->node_state &=
						(~NODE_STATE_INVALID_REG);
					node_ptr->node_state &=
						(~NODE_STATE_POWERING_DOWN);
					node_ptr->node_state |=
						NODE_STATE_POWERED_DOWN;

					if (IS_NODE_CLOUD(node_ptr) &&
					    cloud_reg_addrs)
						set_node_comm_name(
							node_ptr,
							node_ptr->name,
							node_ptr->name);

					node_ptr->power_save_req_time = 0;

					clusteracct_storage_g_node_down(
						acct_db_conn,
						node_ptr, now,
						"Powered down after resume",
						node_ptr->reason_uid);
				}

				if (IS_NODE_DOWN(node_ptr)) {
					state_val = NODE_STATE_IDLE;
					_require_node_reg(node_ptr);
				} else if (IS_NODE_FUTURE(node_ptr)) {
					if (node_ptr->port == 0) {
						node_ptr->port =
							slurm_conf.slurmd_port;
					}
					state_val = NODE_STATE_IDLE;
					bit_clear(future_node_bitmap,
						  node_ptr->index);

					_require_node_reg(node_ptr);
				} else if (node_flags & NODE_STATE_DRAIN) {
					state_val = base_state;
					_require_node_reg(node_ptr);
				} else
					state_val = base_state;
			} else if (state_val == NODE_STATE_UNDRAIN) {
				if (IS_NODE_IDLE(node_ptr) &&
				    IS_NODE_DRAIN(node_ptr)) {
					clusteracct_storage_g_node_up(
						acct_db_conn,
						node_ptr,
						now);
					acct_updated = true;
				}
				node_ptr->node_state &= (~NODE_STATE_DRAIN);
				_require_node_reg(node_ptr);
				state_val = base_state;
			}

			if ((state_val == NODE_STATE_DOWN) ||
			    (state_val == NODE_STATE_FUTURE)) {
				/* We must set node DOWN before killing
				 * its jobs */
				_make_node_down(node_ptr, now);
				kill_running_job_by_node_name (this_node_name);
				if (state_val == NODE_STATE_FUTURE) {
					if (IS_NODE_DYNAMIC_FUTURE(node_ptr)) {
						/* Reset comm and hostname */
						set_node_comm_name(
							node_ptr,
							node_ptr->name,
							node_ptr->name);
					}
					node_ptr->node_state =
						NODE_STATE_FUTURE;
					bit_set(future_node_bitmap,
						node_ptr->index);
					clusteracct_storage_g_node_down(
						acct_db_conn,
						node_ptr, now,
						"Set to State=FUTURE",
						node_ptr->reason_uid);
				}
			} else if (state_val == NODE_STATE_IDLE) {
				/* assume they want to clear DRAIN and
				 * FAIL flags too */
				if (IS_NODE_DOWN(node_ptr)) {
					trigger_node_up(node_ptr);
					clusteracct_storage_g_node_up(
						acct_db_conn,
						node_ptr,
						now);
					acct_updated = true;
				} else if (IS_NODE_IDLE(node_ptr)   &&
					   (IS_NODE_DRAIN(node_ptr) ||
					    IS_NODE_FAIL(node_ptr))) {
					clusteracct_storage_g_node_up(
						acct_db_conn,
						node_ptr,
						now);
					acct_updated = true;
				}	/* else already fully available */
				node_ptr->node_state &= (~NODE_STATE_DRAIN);
				node_ptr->node_state &= (~NODE_STATE_FAIL);
				if (!IS_NODE_NO_RESPOND(node_ptr) ||
				     IS_NODE_POWERED_DOWN(node_ptr))
					make_node_avail(node_ptr);
				bit_set (idle_node_bitmap, node_ptr->index);
				bit_set (up_node_bitmap, node_ptr->index);
				if (IS_NODE_POWERED_DOWN(node_ptr))
					node_ptr->last_busy = 0;
				else
					node_ptr->last_busy = now;
			} else if (state_val == NODE_STATE_ALLOCATED) {
				if (!IS_NODE_DRAIN(node_ptr) &&
				    !IS_NODE_FAIL(node_ptr)  &&
				    !IS_NODE_NO_RESPOND(node_ptr))
					make_node_avail(node_ptr);
				bit_set (up_node_bitmap, node_ptr->index);
				bit_clear (idle_node_bitmap, node_ptr->index);
			} else if ((state_val == NODE_STATE_DRAIN) ||
				   (state_val == NODE_STATE_FAIL)) {
				uint32_t new_state = state_val;
				if ((IS_NODE_ALLOCATED(node_ptr) ||
				     IS_NODE_MIXED(node_ptr)) &&
				    (IS_NODE_POWERED_DOWN(node_ptr) ||
				     IS_NODE_POWERING_UP(node_ptr))) {
					info("%s: DRAIN/FAIL request for node %s which is allocated and being powered up. Requeuing jobs",
					     __func__, this_node_name);
					kill_running_job_by_node_name(
								this_node_name);
				}
				bit_clear (avail_node_bitmap, node_ptr->index);
				node_ptr->node_state &= (~NODE_STATE_DRAIN);
				node_ptr->node_state &= (~NODE_STATE_FAIL);
				state_val = node_ptr->node_state |= state_val;
				if ((node_ptr->run_job_cnt  == 0) &&
				    (node_ptr->comp_job_cnt == 0)) {
					trigger_node_drained(node_ptr);
					clusteracct_storage_g_node_down(
						acct_db_conn,
						node_ptr, now, NULL,
						node_ptr->reason_uid);
				}
				if ((new_state == NODE_STATE_FAIL) &&
				    (nonstop_ops.node_fail))
					(nonstop_ops.node_fail)(NULL, node_ptr);
			} else if (state_val & NODE_STATE_POWER_DOWN) {
				if ((state_val & NODE_STATE_POWER_UP) &&
				    (IS_NODE_POWERING_UP(node_ptr))) {
					/* Clear any reboot op in progress */
					node_ptr->node_state &=
						(~NODE_STATE_POWERING_UP);
					node_ptr->last_response = now;
					free(this_node_name);
					continue;
				}

				/* Abort reboot request */
				if (IS_NODE_REBOOT_REQUESTED(node_ptr) ||
				    IS_NODE_REBOOT_ISSUED(node_ptr)) {
					/*
					 * A node is DOWN+REBOOT_ISSUED when the
					 * reboot has been issued. Set node
					 * state back to idle only if the reboot
					 * has been issued. Node will remain
					 * cleared in the avail_node_bitmap
					 * until the node is powered down.
					 */
					if (IS_NODE_REBOOT_ISSUED(node_ptr) &&
					    IS_NODE_DOWN(node_ptr)) {
						node_ptr->node_state =
							NODE_STATE_IDLE |
							(node_ptr->node_state &
							 NODE_STATE_FLAGS);
					}

					node_ptr->node_state &=
						(~NODE_STATE_REBOOT_REQUESTED);
					node_ptr->node_state &=
						(~NODE_STATE_REBOOT_ISSUED);
					xfree(node_ptr->reason);

					info("Canceling REBOOT on node %s",
					     this_node_name);
				}

				if (IS_NODE_POWERED_DOWN(node_ptr)) {
					node_ptr->node_state &=
						(~NODE_STATE_POWERED_DOWN);
					info("power down request repeating "
					     "for node %s", this_node_name);
				} else if (IS_NODE_POWERING_DOWN(node_ptr)) {
					info("ignoring power down request for node %s, already powering down",
					     this_node_name);
					node_ptr->next_state = NO_VAL;
					free(this_node_name);
					continue;
				} else
					info("powering down node %s",
					     this_node_name);

				if (state_val & NODE_STATE_POWERED_DOWN) {
					/* Force power down */
					_make_node_unavail(node_ptr);
					/*
					 * Kill any running jobs and requeue if
					 * possible.
					 */
					kill_running_job_by_node_name(
						this_node_name);
					node_ptr->node_state &=
						(~NODE_STATE_POWERING_UP);
				} else if (state_val & NODE_STATE_POWER_DRAIN) {
					/* power down asap -- drain */
					_drain_node(node_ptr, "POWER_DOWN_ASAP",
						    node_ptr->reason_uid);
				}
				if (IS_NODE_DOWN(node_ptr)) {
					/* Abort any power up request */
					node_ptr->node_state &=
						(~NODE_STATE_POWERING_UP);
				}

				node_ptr->node_state |=
					NODE_STATE_POWER_DOWN;

				if (IS_NODE_IDLE(node_ptr)) {
					/*
					 * remove node from avail_node_bitmap so
					 * that it will power_down before jobs
					 * get on it.
					 */
					bit_clear(avail_node_bitmap,
						  node_ptr->index);
				}

				node_ptr->next_state = NO_VAL;
				bit_clear(rs_node_bitmap, node_ptr->index);
				free(this_node_name);
				continue;
			} else if (state_val == NODE_STATE_POWER_UP) {
				if (!IS_NODE_POWERED_DOWN(node_ptr)) {
					if (IS_NODE_POWERING_UP(node_ptr)) {
						node_ptr->node_state |=
							NODE_STATE_POWERED_DOWN;
						node_ptr->node_state |=
							NODE_STATE_POWER_UP;
						info("power up request "
						     "repeating for node %s",
						     this_node_name);
					} else {
						verbose("node %s is already "
							"powered up",
							this_node_name);
					}
				} else {
					node_ptr->node_state |=
						NODE_STATE_POWER_UP;
					info("powering up node %s",
					     this_node_name);
				}
				node_ptr->next_state = NO_VAL;
				bit_clear(rs_node_bitmap, node_ptr->index);
				free(this_node_name);
				continue;
			} else if (state_val == NODE_STATE_NO_RESPOND) {
				node_ptr->node_state |= NODE_STATE_NO_RESPOND;
				state_val = base_state;
				bit_clear(avail_node_bitmap, node_ptr->index);
			} else if (state_val == NODE_STATE_REBOOT_CANCEL) {
				if (!IS_NODE_REBOOT_ISSUED(node_ptr)) {
					node_ptr->node_state &=
						(~NODE_STATE_REBOOT_REQUESTED);
					state_val = base_state;
					if ((node_ptr->next_state &
					     NODE_STATE_FLAGS) &
					    NODE_STATE_UNDRAIN)
						_undo_reboot_asap(node_ptr);
				} else {
					info("REBOOT on node %s already in progress -- unable to cancel",
					     this_node_name);
					err_code = error_code =
						ESLURM_REBOOT_IN_PROGRESS;
				}
			} else {
				info("Invalid node state specified %u",
				     state_val);
				err_code = 1;
				error_code = ESLURM_INVALID_NODE_STATE;
			}

			if (err_code == 0) {
				node_ptr->node_state = state_val |
						(node_ptr->node_state &
						 NODE_STATE_FLAGS);

				if (!IS_NODE_REBOOT_REQUESTED(node_ptr) &&
				    !IS_NODE_REBOOT_ISSUED(node_ptr))
					node_ptr->next_state = NO_VAL;
				bit_clear(rs_node_bitmap, node_ptr->index);

				info ("update_node: node %s state set to %s",
					this_node_name,
					node_state_string(state_val));
			}
		}

		if (!acct_updated && !IS_NODE_DOWN(node_ptr) &&
		    !IS_NODE_DRAIN(node_ptr) && !IS_NODE_FAIL(node_ptr)) {
			/* reason information is handled in
			   clusteracct_storage_g_node_up()
			*/
			clusteracct_storage_g_node_up(
				acct_db_conn, node_ptr, now);
		}

		free (this_node_name);
	}

	/* Write/clear log */
	(void)update_node_active_features(NULL, NULL, FEATURE_MODE_PEND);

	FREE_NULL_HOSTLIST(host_list);
	FREE_NULL_HOSTLIST(hostaddr_list);
	FREE_NULL_HOSTLIST(hostname_list);
	last_node_update = now;

	if ((error_code == SLURM_SUCCESS) && (update_node_msg->features)) {
		error_code = update_node_avail_features(
					update_node_msg->node_names,
					update_node_msg->features,
					FEATURE_MODE_IND);
	}
	if ((error_code == SLURM_SUCCESS) && (update_node_msg->gres)) {
		error_code = _update_node_gres(update_node_msg->node_names,
					       update_node_msg->gres);
	}

	/*
	 * Update weight. Weight is part of config_ptr,
	 * hence split config records if required
	 */
	if ((error_code == SLURM_SUCCESS) &&
	    (update_node_msg->weight != NO_VAL))	{
		error_code = _update_node_weight(update_node_msg->node_names,
						 update_node_msg->weight);
		if (error_code == SLURM_SUCCESS) {
			/* sort config_list by weight for scheduling */
			list_sort(config_list, &list_compare_config);
		}
	}

	return error_code;
}

/*
 * restore_node_features - Make node and config (from slurm.conf) fields
 *	consistent for Features, Gres and Weight
 * IN recover -
 *              0, 1 - use data from config record, built using slurm.conf
 *              2 = use data from node record, built from saved state
 */
extern void restore_node_features(int recover)
{
	int i, node_features_plugin_cnt;
	node_record_t *node_ptr;

	node_features_plugin_cnt = node_features_g_count();
	for (i = 0; (node_ptr = next_node(&i)); i++) {
		if (node_ptr->weight != node_ptr->config_ptr->weight) {
			error("Node %s Weight(%u) differ from slurm.conf",
			      node_ptr->name, node_ptr->weight);
			if (recover == 2) {
				_update_node_weight(node_ptr->name,
						    node_ptr->weight);
			} else {
				node_ptr->weight = node_ptr->config_ptr->
						   weight;
			}
		}
		if (xstrcmp(node_ptr->config_ptr->feature, node_ptr->features)){
			if (node_features_plugin_cnt == 0) {
				error("Node %s Features(%s) differ from slurm.conf",
				      node_ptr->name, node_ptr->features);
			}
			if (recover == 2) {
				update_node_avail_features(node_ptr->name,
							   node_ptr->features,
							   FEATURE_MODE_COMB);
			}
		}

		/*
		 * We lose the GRES information updated manually and always
		 * use the information from slurm.conf
		 */
		(void) gres_node_reconfig(
			node_ptr->name,
			node_ptr->config_ptr->gres,
			&node_ptr->gres,
			&node_ptr->gres_list,
			slurm_conf.conf_flags & CTL_CONF_OR,
			node_ptr->cores,
			node_ptr->tot_sockets);
		gres_node_state_log(node_ptr->gres_list, node_ptr->name);
	}
	update_node_avail_features(NULL, NULL, FEATURE_MODE_PEND);
}

/* Duplicate a configuration record except for the node names & bitmap */
config_record_t *_dup_config(config_record_t *config_ptr)
{
	config_record_t *new_config_ptr;

	new_config_ptr = create_config_record();
	new_config_ptr->magic       = config_ptr->magic;
	new_config_ptr->cpus        = config_ptr->cpus;
	new_config_ptr->cpu_spec_list = xstrdup(config_ptr->cpu_spec_list);
	new_config_ptr->boards      = config_ptr->boards;
	new_config_ptr->tot_sockets     = config_ptr->tot_sockets;
	new_config_ptr->cores       = config_ptr->cores;
	new_config_ptr->core_spec_cnt = config_ptr->core_spec_cnt;
	new_config_ptr->threads     = config_ptr->threads;
	new_config_ptr->real_memory = config_ptr->real_memory;
	new_config_ptr->mem_spec_limit = config_ptr->mem_spec_limit;
	new_config_ptr->tmp_disk    = config_ptr->tmp_disk;
	new_config_ptr->weight      = config_ptr->weight;
	new_config_ptr->feature     = xstrdup(config_ptr->feature);
	new_config_ptr->gres        = xstrdup(config_ptr->gres);

	return new_config_ptr;
}

/*
 * _update_node_weight - Update weight associated with nodes
 *	build new config list records as needed
 * IN node_names - List of nodes to update
 * IN weight - New weight value
 * RET: SLURM_SUCCESS or error code
 */
static int _update_node_weight(char *node_names, uint32_t weight)
{
	bitstr_t *node_bitmap = NULL, *tmp_bitmap;
	ListIterator config_iterator;
	config_record_t *config_ptr, *new_config_ptr, *first_new = NULL;
	int rc, config_cnt, tmp_cnt;

	rc = node_name2bitmap(node_names, false, &node_bitmap);
	if (rc) {
		info("_update_node_weight: invalid node_name");
		return rc;
	}

	/* For each config_record with one of these nodes,
	 * update it (if all nodes updated) or split it into
	 * a new entry */
	config_iterator = list_iterator_create(config_list);
	while ((config_ptr = list_next(config_iterator))) {
		if (config_ptr == first_new)
			break;	/* done with all original records */

		tmp_bitmap = bit_copy(node_bitmap);
		bit_and(tmp_bitmap, config_ptr->node_bitmap);
		config_cnt = bit_set_count(config_ptr->node_bitmap);
		tmp_cnt = bit_set_count(tmp_bitmap);
		if (tmp_cnt == 0) {
			/* no overlap, leave alone */
		} else if (tmp_cnt == config_cnt) {
			/* all nodes changed, update in situ */
			config_ptr->weight = weight;
		} else {
			/* partial update, split config_record */
			new_config_ptr = _dup_config(config_ptr);
			if (first_new == NULL)
				first_new = new_config_ptr;
			/* Change weight for the given nodes */
			new_config_ptr->weight      = weight;
			new_config_ptr->node_bitmap = bit_copy(tmp_bitmap);
			new_config_ptr->nodes = bitmap2node_name(tmp_bitmap);
			_update_config_ptr(tmp_bitmap, new_config_ptr);

			/* Update remaining records */
			bit_and_not(config_ptr->node_bitmap, tmp_bitmap);
			xfree(config_ptr->nodes);
			config_ptr->nodes = bitmap2node_name(
						config_ptr->node_bitmap);
		}
		FREE_NULL_BITMAP(tmp_bitmap);
	}
	list_iterator_destroy(config_iterator);
	FREE_NULL_BITMAP(node_bitmap);

	info("_update_node_weight: nodes %s weight set to: %u",
		node_names, weight);
	return SLURM_SUCCESS;
}

static inline void _update_node_features_post(
	char *node_names,
	char **last_features, char *features,
	bitstr_t **last_node_bitmap, bitstr_t **node_bitmap,
	int mode, const char *type)
{

	xassert(last_features);
	xassert(last_node_bitmap);
	xassert(node_bitmap);

	if (mode == FEATURE_MODE_IND) {
		debug2("%s: nodes %s %s features set to: %s",
		       __func__, node_names, type, features);
	} else if (*last_features && *last_node_bitmap &&
		   ((mode == FEATURE_MODE_PEND) ||
		    xstrcmp(features, *last_features))) {
		char *last_node_names = bitmap2node_name(*last_node_bitmap);
		debug2("%s: nodes %s %s features set to: %s",
		       __func__, last_node_names, type, *last_features);
		xfree(last_node_names);
		xfree(*last_features);
		FREE_NULL_BITMAP(*last_node_bitmap);
	}

	if (mode == FEATURE_MODE_COMB) {
		if (!*last_features) {
			/* Start combining records */
			*last_features = xstrdup(features);
			*last_node_bitmap = *node_bitmap;
			*node_bitmap = NULL;
		} else {
			/* Add this node to existing log info */
			bit_or(*last_node_bitmap, *node_bitmap);
		}
	}
}

extern int update_node_active_features(char *node_names, char *active_features,
				       int mode)
{
	static char *last_active_features = NULL;
	static bitstr_t *last_node_bitmap = NULL;
	bitstr_t *node_bitmap = NULL;
	int rc;

	if (mode < FEATURE_MODE_PEND) {
		/* Perform update of node active features */
		rc = node_name2bitmap(node_names, false, &node_bitmap);
		if (rc) {
			info("%s: invalid node_name (%s)", __func__,
			     node_names);
			return rc;
		}
		update_feature_list(active_feature_list, active_features,
				    node_bitmap);
		(void) node_features_g_node_update(active_features,
						   node_bitmap);
	}

	_update_node_features_post(node_names,
				   &last_active_features, active_features,
				   &last_node_bitmap, &node_bitmap,
				   mode, "active");
	FREE_NULL_BITMAP(node_bitmap);

	return SLURM_SUCCESS;
}

extern int update_node_avail_features(char *node_names, char *avail_features,
				      int mode)
{
	static char *last_avail_features = NULL;
	static bitstr_t *last_node_bitmap = NULL;
	bitstr_t *node_bitmap = NULL, *tmp_bitmap;
	ListIterator config_iterator;
	config_record_t *config_ptr, *new_config_ptr, *first_new = NULL;
	int rc, config_cnt, tmp_cnt;

	if (mode < FEATURE_MODE_PEND) {
		rc = node_name2bitmap(node_names, false, &node_bitmap);
		if (rc) {
			info("%s: invalid node_name (%s)",
			     __func__, node_names);
			return rc;
		}

		/*
		 * For each config_record with one of these nodes, update it
		 * (if all nodes updated) or split it into a new entry
		 */
		config_iterator = list_iterator_create(config_list);
		while ((config_ptr = list_next(config_iterator))) {
			if (config_ptr == first_new)
				break;	/* done with all original records */

			tmp_bitmap = bit_copy(node_bitmap);
			bit_and(tmp_bitmap, config_ptr->node_bitmap);
			config_cnt = bit_set_count(config_ptr->node_bitmap);
			tmp_cnt = bit_set_count(tmp_bitmap);
			if (tmp_cnt == 0) {
				/* no overlap, leave alone */
			} else if (tmp_cnt == config_cnt) {
				/* all nodes changed, update in situ */
				xfree(config_ptr->feature);
				if (avail_features && avail_features[0]) {
					config_ptr->feature =
						xstrdup(avail_features);
				}
			} else {
				/* partial update, split config_record */
				new_config_ptr = _dup_config(config_ptr);
				if (first_new == NULL)
					first_new = new_config_ptr;
				xfree(new_config_ptr->feature);
				if (avail_features && avail_features[0]) {
					new_config_ptr->feature =
						xstrdup(avail_features);
				}
				new_config_ptr->node_bitmap =
						bit_copy(tmp_bitmap);
				new_config_ptr->nodes =
						bitmap2node_name(tmp_bitmap);
				_update_config_ptr(tmp_bitmap, new_config_ptr);

				/* Update remaining records */
				bit_and_not(config_ptr->node_bitmap, tmp_bitmap);
				xfree(config_ptr->nodes);
				config_ptr->nodes = bitmap2node_name(
						    config_ptr->node_bitmap);
			}
			FREE_NULL_BITMAP(tmp_bitmap);
		}
		list_iterator_destroy(config_iterator);
		if (avail_feature_list) {	/* List not set at startup */
			update_feature_list(avail_feature_list, avail_features,
					    node_bitmap);
		}
	}

	_update_node_features_post(node_names,
				   &last_avail_features, avail_features,
				   &last_node_bitmap, &node_bitmap,
				   mode, "available");
	FREE_NULL_BITMAP(node_bitmap);

	return SLURM_SUCCESS;
}

/*
 * _update_node_gres - Update generic resources associated with nodes
 *	build new config list records as needed
 * IN node_names - List of nodes to update
 * IN gres - New gres value
 * RET: SLURM_SUCCESS or error code
 */
static int _update_node_gres(char *node_names, char *gres)
{
	bitstr_t *changed_node_bitmap = NULL, *node_bitmap = NULL, *tmp_bitmap;
	ListIterator config_iterator;
	config_record_t *config_ptr, *new_config_ptr, *first_new = NULL;
	node_record_t *node_ptr;
	int rc, rc2, overlap1, overlap2;
	int i, i_first, i_last;

	rc = node_name2bitmap(node_names, false, &node_bitmap);
	if (rc) {
		info("%s: invalid node_name: %s", __func__, node_names);
		return rc;
	}

	/*
	 * For each config_record with one of these nodes,
	 * update it (if all nodes updated) or split it into a new entry
	 */
	config_iterator = list_iterator_create(config_list);
	while ((config_ptr = list_next(config_iterator))) {
		if (config_ptr == first_new)
			break;	/* done with all original records */

		overlap1 = bit_overlap(node_bitmap, config_ptr->node_bitmap);
		if (overlap1 == 0)
			continue;  /* No changes to this config_record */

		/* At least some nodes in this config need to change */
		tmp_bitmap = bit_copy(node_bitmap);
		bit_and(tmp_bitmap, config_ptr->node_bitmap);
		i_first = bit_ffs(tmp_bitmap);
		if (i_first >= 0)
			i_last = bit_fls(tmp_bitmap);
		else
			i_last = i_first - 1;
		for (i = i_first; i <= i_last; i++) {
			if (!bit_test(tmp_bitmap, i))
				continue;	/* Not this node */
			node_ptr = node_record_table_ptr[i];
			rc2 = gres_node_reconfig(
				node_ptr->name,
				gres, &node_ptr->gres,
				&node_ptr->gres_list,
				slurm_conf.conf_flags & CTL_CONF_OR,
				node_ptr->cores,
				node_ptr->tot_sockets);
			if (rc2 != SLURM_SUCCESS) {
				bit_clear(tmp_bitmap, i);
				overlap1--;
				if (rc == SLURM_SUCCESS)
					rc = rc2;
			}
			gres_node_state_log(node_ptr->gres_list,
					    node_ptr->name);
		}

		overlap2 = bit_set_count(config_ptr->node_bitmap);
		if (overlap1 == 0) {
			/* No nodes actually changed in this configuration */
			FREE_NULL_BITMAP(tmp_bitmap);
		} else if (overlap1 == overlap2) {
			/* All nodes changes in this configuration */
			xfree(config_ptr->gres);
			if (gres && gres[0])
				config_ptr->gres = xstrdup(gres);
			if (changed_node_bitmap) {
				bit_or(changed_node_bitmap, tmp_bitmap);
				FREE_NULL_BITMAP(tmp_bitmap);
			} else {
				changed_node_bitmap = tmp_bitmap;
				tmp_bitmap = NULL;
			}
		} else {
			/*
			 * Some nodes changes in this configuration.
			 * Split config_record in two.
			 */
			new_config_ptr = _dup_config(config_ptr);
			if (!first_new)
				first_new = new_config_ptr;
			xfree(new_config_ptr->gres);
			if (gres && gres[0])
				new_config_ptr->gres = xstrdup(gres);
			new_config_ptr->node_bitmap = tmp_bitmap;
			new_config_ptr->nodes = bitmap2node_name(tmp_bitmap);
			_update_config_ptr(tmp_bitmap, new_config_ptr);
			if (changed_node_bitmap) {
				bit_or(changed_node_bitmap, tmp_bitmap);
			} else {
				changed_node_bitmap = bit_copy(tmp_bitmap);
			}

			/* Update remaining config_record */
			bit_and_not(config_ptr->node_bitmap, tmp_bitmap);
			xfree(config_ptr->nodes);
			config_ptr->nodes = bitmap2node_name(
						config_ptr->node_bitmap);
			tmp_bitmap = NULL;	/* Nothing left to free */
		}
	}
	list_iterator_destroy(config_iterator);
	FREE_NULL_BITMAP(node_bitmap);

	/* Report changes nodes, may be subset of requested nodes */
	if (changed_node_bitmap) {
		char *change_node_str = bitmap2node_name(changed_node_bitmap);
		info("%s: nodes %s gres set to: %s", __func__,
		     change_node_str, gres);
		FREE_NULL_BITMAP(changed_node_bitmap);
		xfree(change_node_str);
	}

	return rc;
}

/* Reset the config pointer for updated jobs */
static void _update_config_ptr(bitstr_t *bitmap, config_record_t *config_ptr)
{
	int i;

	for (i = 0; i < node_record_count; i++) {
		if (!bit_test(bitmap, i))
			continue;
		node_record_table_ptr[i]->config_ptr = config_ptr;
	}
}

static void _drain_node(node_record_t *node_ptr, char *reason,
			uint32_t reason_uid)
{
	time_t now = time(NULL);

	xassert(node_ptr);

	if (IS_NODE_DRAIN(node_ptr)) {
		/* state already changed, nothing to do */
		return;
	}

	node_ptr->node_state |= NODE_STATE_DRAIN;
	bit_clear(avail_node_bitmap, node_ptr->index);
	info("drain_nodes: node %s state set to DRAIN",
	     node_ptr->name);
	if ((node_ptr->reason == NULL) ||
	    (xstrncmp(node_ptr->reason, "Not responding", 14) == 0)) {
		xfree(node_ptr->reason);
		node_ptr->reason = xstrdup(reason);
		node_ptr->reason_time = now;
		node_ptr->reason_uid = reason_uid;
	}
	if ((node_ptr->run_job_cnt  == 0) &&
	    (node_ptr->comp_job_cnt == 0)) {
		/* no jobs, node is drained */
		trigger_node_drained(node_ptr);
		clusteracct_storage_g_node_down(acct_db_conn,
						node_ptr, now, NULL,
						reason_uid);
	}
}

/*
 * drain_nodes - drain one or more nodes,
 *  no-op for nodes already drained or draining
 * IN nodes - nodes to drain
 * IN reason - reason to drain the nodes
 * RET SLURM_SUCCESS or error code
 * global: node_record_table_ptr - pointer to global node table
 */
extern int drain_nodes(char *nodes, char *reason, uint32_t reason_uid)
{
	int error_code = SLURM_SUCCESS;
	node_record_t *node_ptr;
	char  *this_node_name ;
	hostlist_t host_list;

	if ((nodes == NULL) || (nodes[0] == '\0')) {
		error ("drain_nodes: invalid node name  %s", nodes);
		return ESLURM_INVALID_NODE_NAME;
	}

	if ( (host_list = hostlist_create (nodes)) == NULL) {
		error ("hostlist_create error on %s: %m", nodes);
		return ESLURM_INVALID_NODE_NAME;
	}

	while ( (this_node_name = hostlist_shift (host_list)) ) {
		if (!(node_ptr = find_node_record(this_node_name))) {
			error_code = ESLURM_INVALID_NODE_NAME;
			error("drain_nodes: node %s does not exist",
			      this_node_name);
			xfree(this_node_name);
			break;
		}
		free (this_node_name);
		_drain_node(node_ptr, reason, reason_uid);
	}
	last_node_update = time (NULL);

	hostlist_destroy (host_list);

	/*
	 * check all reservations since nodes may have been in a reservation with
	 * floating count of nodes that needs to be updated
	 */
	validate_all_reservations(false);

	return error_code;
}
/* Return true if admin request to change node state from old to new is valid */
static bool _valid_node_state_change(uint32_t old, uint32_t new)
{
	uint32_t base_state, node_flags;
	static bool power_save_on = false;
	static time_t sched_update = 0;

	if (old == new)
		return true;

	base_state = old & NODE_STATE_BASE;
	node_flags = old & NODE_STATE_FLAGS;

	if (sched_update != slurm_conf.last_update) {
		power_save_on = power_save_test();
		sched_update = slurm_conf.last_update;
	}

	switch (new) {
		case NODE_STATE_DOWN:
		case NODE_STATE_DRAIN:
		case NODE_STATE_FAIL:
		case NODE_STATE_NO_RESPOND:
			return true;

		case NODE_STATE_UNDRAIN:
			if (!(node_flags & NODE_STATE_INVALID_REG))
				return true;
			break;

		case NODE_STATE_POWER_DOWN:
		case NODE_STATE_POWER_UP:
		case (NODE_STATE_POWER_DOWN | NODE_STATE_POWER_UP):
		case (NODE_STATE_POWER_DOWN | NODE_STATE_POWERED_DOWN):
		case (NODE_STATE_POWER_DOWN | NODE_STATE_POWER_DRAIN):
			if (power_save_on)
				return true;
			info("attempt to do power work on node but PowerSave is disabled");
			break;

		case NODE_RESUME:
			if (node_flags & NODE_STATE_POWERING_DOWN)
				return true;
			if (node_flags & NODE_STATE_INVALID_REG)
				return false;
			if ((base_state == NODE_STATE_DOWN)   ||
			    (base_state == NODE_STATE_FUTURE) ||
			    (node_flags & NODE_STATE_DRAIN)   ||
			    (node_flags & NODE_STATE_FAIL)    ||
			    (node_flags & NODE_STATE_REBOOT_REQUESTED))
				return true;
			break;

		case NODE_STATE_REBOOT_CANCEL:
			if (node_flags & NODE_STATE_REBOOT_REQUESTED)
				return true;
			break;

		case NODE_STATE_FUTURE:
			if ((base_state == NODE_STATE_DOWN) ||
			    (base_state == NODE_STATE_IDLE))
				return true;
			break;

		case NODE_STATE_IDLE:
			if (!(node_flags & NODE_STATE_INVALID_REG) &&
			    ((base_state == NODE_STATE_DOWN) ||
			     (base_state == NODE_STATE_IDLE)))
				return true;
			break;

		case NODE_STATE_ALLOCATED:
			if (base_state == NODE_STATE_ALLOCATED)
				return true;
			break;

		default:	/* All others invalid */
			break;
	}

	return false;
}

static int _build_node_spec_bitmap(node_record_t *node_ptr)
{
	uint32_t size;
	int *cpu_spec_array;
	int i;

	if (node_ptr->threads == 0) {
		error("Node %s has invalid thread per core count (%u)",
		      node_ptr->name, node_ptr->threads);
		return SLURM_ERROR;
	}

	if (!node_ptr->cpu_spec_list)
		return SLURM_SUCCESS;
	size = node_ptr->tot_cores;
	FREE_NULL_BITMAP(node_ptr->node_spec_bitmap);
	node_ptr->node_spec_bitmap = bit_alloc(size);
	bit_nset(node_ptr->node_spec_bitmap, 0, size-1);

	/* remove node's specialized cpus now */
	cpu_spec_array = bitfmt2int(node_ptr->cpu_spec_list);
	i = 0;
	while (cpu_spec_array[i] != -1) {
		int start = (cpu_spec_array[i] / node_ptr->threads);
		int end = (cpu_spec_array[i + 1] / node_ptr->threads);
		if (start > size) {
			error("%s: Specialized CPUs id start above the configured limit.",
			      __func__);
			break;
		}

		if (end > size) {
			error("%s: Specialized CPUs id end above the configured limit",
			      __func__);
			end = size;
		}
		/*
		 * We need to test to make sure we have these bits in this map.
		 * If the node goes from 12 cpus to 6 like scenario.
		 */
		bit_nclear(node_ptr->node_spec_bitmap, start, end);
		i += 2;
	}
	node_ptr->core_spec_cnt = bit_clear_count(node_ptr->node_spec_bitmap);
	xfree(cpu_spec_array);
	return SLURM_SUCCESS;
}

extern int update_node_record_acct_gather_data(
	acct_gather_node_resp_msg_t *msg)
{
	node_record_t *node_ptr;

	node_ptr = find_node_record(msg->node_name);
	if (node_ptr == NULL)
		return ENOENT;

	memcpy(node_ptr->energy, msg->energy, sizeof(acct_gather_energy_t));

	return SLURM_SUCCESS;
}

/* A node's socket/core configuration has changed could be due to KNL NUMA
 * mode change and reboot. Update this node's config record, splitting an
 * existing record if needed. */
static void _split_node_config(node_record_t *node_ptr,
			       slurm_node_registration_status_msg_t *reg_msg)
{
	config_record_t *config_ptr, *new_config_ptr;

	if (!node_ptr)
		return;
	config_ptr = node_ptr->config_ptr;
	if (!config_ptr)
		return;

	if ((bit_set_count(config_ptr->node_bitmap) > 1) &&
	    bit_test(config_ptr->node_bitmap, node_ptr->index)) {
		new_config_ptr = create_config_record();
		memcpy(new_config_ptr, config_ptr, sizeof(config_record_t));
		new_config_ptr->cpu_spec_list =
			xstrdup(config_ptr->cpu_spec_list);
		new_config_ptr->feature = xstrdup(config_ptr->feature);
		new_config_ptr->gres = xstrdup(config_ptr->gres);
		bit_clear(config_ptr->node_bitmap, node_ptr->index);
		xfree(config_ptr->nodes);
		config_ptr->nodes = bitmap2node_name(config_ptr->node_bitmap);
		new_config_ptr->node_bitmap = bit_alloc(node_record_count);
		bit_set(new_config_ptr->node_bitmap, node_ptr->index);
		new_config_ptr->nodes = xstrdup(node_ptr->name);
		node_ptr->config_ptr = new_config_ptr;
		config_ptr = new_config_ptr;
	}
	config_ptr->cores = reg_msg->cores;
	config_ptr->tot_sockets = reg_msg->sockets;
}

/*
 * validate_node_specs - validate the node's specifications as valid,
 *	if not set state to down, in any case update last_response
 * IN slurm_msg - get node registration message it
 * OUT newly_up - set if node newly brought into service
 * RET 0 if no error, ENOENT if no such node, EINVAL if values too low
 */
extern int validate_node_specs(slurm_msg_t *slurm_msg, bool *newly_up)
{
	int error_code;
	config_record_t *config_ptr;
	node_record_t *node_ptr;
	char *reason_down = NULL;
	char *orig_features = NULL, *orig_features_act = NULL;
	uint32_t node_flags;
	time_t now = time(NULL);
	bool orig_node_avail, was_invalid_reg, was_powering_up = false;
	static uint32_t cr_flag = NO_VAL;
	static int node_features_cnt = 0;
	int sockets1, sockets2;	/* total sockets on node */
	int cores1, cores2;	/* total cores on node */
	int threads1, threads2;	/* total threads on node */
	static time_t sched_update = 0;
	static double conf_node_reg_mem_percent = -1;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));

	slurm_node_registration_status_msg_t *reg_msg =
		(slurm_node_registration_status_msg_t *)slurm_msg->data;

	node_ptr = find_node_record(reg_msg->node_name);
	if (node_ptr == NULL)
		return ENOENT;

	debug3("%s: validating nodes %s in state: %s",
	       __func__, reg_msg->node_name,
	       node_state_string(node_ptr->node_state));

	if (sched_update != slurm_conf.last_update) {
		char *tmp_ptr;
		if ((tmp_ptr = xstrcasestr(slurm_conf.slurmctld_params,
					   "node_reg_mem_percent="))) {
			conf_node_reg_mem_percent = strtod(tmp_ptr + 21, NULL);
			if (errno) {
				conf_node_reg_mem_percent = -1;
				error("%s: Unable to convert %s value to double",
				      __func__, tmp_ptr);
			}
			sched_update = slurm_conf.last_update;
		}
	}

	orig_node_avail = bit_test(avail_node_bitmap, node_ptr->index);

	config_ptr = node_ptr->config_ptr;
	error_code = SLURM_SUCCESS;

	node_ptr->protocol_version = slurm_msg->protocol_version;
	xfree(node_ptr->version);
	node_ptr->version = reg_msg->version;
	reg_msg->version = NULL;

	if (waiting_for_node_boot(node_ptr) ||
	    waiting_for_node_power_down(node_ptr))
		return SLURM_SUCCESS;
	bit_clear(booting_node_bitmap, node_ptr->index);

	if (cr_flag == NO_VAL) {
		cr_flag = 0;  /* call is no-op for select/linear and others */
		if (select_g_get_info_from_plugin(SELECT_CR_PLUGIN,
						  NULL, &cr_flag)) {
			cr_flag = NO_VAL;	/* error */
		}
		if (cr_flag == SELECT_TYPE_CONS_TRES)
			cr_flag = SELECT_TYPE_CONS_RES;
		node_features_cnt = node_features_g_count();
	}

	if (reg_msg->features_avail || reg_msg->features_active) {
		char *sep = "";
		orig_features = xstrdup(node_ptr->features);
		if (orig_features && orig_features[0])
			sep = ",";
		if (reg_msg->features_avail) {
			xstrfmtcat(orig_features, "%s%s", sep,
				   reg_msg->features_avail);
		}
		if (node_ptr->features_act)
			orig_features_act = xstrdup(node_ptr->features_act);
		else
			orig_features_act = xstrdup(node_ptr->features);
	}
	if (reg_msg->features_avail) {
		if (reg_msg->features_active && !node_ptr->features_act) {
			node_ptr->features_act = node_ptr->features;
			node_ptr->features = NULL;
		} else {
			xfree(node_ptr->features);
		}
		node_ptr->features = node_features_g_node_xlate(
					reg_msg->features_avail,
					orig_features, orig_features,
					node_ptr->index);
		(void) update_node_avail_features(node_ptr->name,
						  node_ptr->features,
						  FEATURE_MODE_IND);
	}
	if (reg_msg->features_active) {
		char *tmp_feature;
		tmp_feature = node_features_g_node_xlate(
						reg_msg->features_active,
						orig_features_act,
						orig_features,
						node_ptr->index);
		xfree(node_ptr->features_act);
		node_ptr->features_act = tmp_feature;
		(void) update_node_active_features(node_ptr->name,
						   node_ptr->features_act,
						   FEATURE_MODE_IND);
	}
	xfree(orig_features);
	xfree(orig_features_act);

	sockets1 = reg_msg->sockets;
	cores1   = sockets1 * reg_msg->cores;
	threads1 = cores1   * reg_msg->threads;
	if (gres_node_config_unpack(reg_msg->gres_info,
				    node_ptr->name) != SLURM_SUCCESS) {
		error_code = SLURM_ERROR;
		xstrcat(reason_down, "Could not unpack gres data");
	} else if (gres_node_config_validate(
				node_ptr->name, config_ptr->gres,
				&node_ptr->gres, &node_ptr->gres_list,
				reg_msg->threads, reg_msg->cores,
				reg_msg->sockets,
				slurm_conf.conf_flags & CTL_CONF_OR,
				&reason_down)
		   != SLURM_SUCCESS) {
		error_code = EINVAL;
		/* reason_down set in function above */
	}
	gres_node_state_log(node_ptr->gres_list, node_ptr->name);

	if (!(slurm_conf.conf_flags & CTL_CONF_OR)) {
		/* sockets1, cores1, and threads1 are set above */
		sockets2 = config_ptr->tot_sockets;
		cores2   = sockets2 * config_ptr->cores;
		threads2 = cores2   * config_ptr->threads;

		if (threads1 < threads2) {
			debug("Node %s has low socket*core*thread count "
			      "(%d < %d)",
			      reg_msg->node_name, threads1, threads2);
			error_code = EINVAL;
			if (reason_down)
				xstrcat(reason_down, ", ");
			xstrcat(reason_down, "Low socket*core*thread count");
		}

		if (reg_msg->cpus < config_ptr->cpus) {
			debug("Node %s has low cpu count (%u < %u)",
			      reg_msg->node_name, reg_msg->cpus,
			      config_ptr->cpus);
			error_code  = EINVAL;
			if (reason_down)
				xstrcat(reason_down, ", ");
			xstrcat(reason_down, "Low CPUs");
		}

		if ((error_code == SLURM_SUCCESS) &&
		    (cr_flag == SELECT_TYPE_CONS_RES) &&
		    (node_features_cnt > 0) &&
		    (reg_msg->sockets != config_ptr->tot_sockets) &&
		    (reg_msg->cores   != config_ptr->cores) &&
		    ((reg_msg->sockets * reg_msg->cores) ==
		     (config_ptr->tot_sockets * config_ptr->cores))) {
			_split_node_config(node_ptr, reg_msg);
		}
	}
	if (reg_msg->boards > reg_msg->sockets) {
		error("Node %s has more boards than sockets (%u > %u), setting board count to 1",
		      reg_msg->node_name, reg_msg->boards, reg_msg->sockets);
		reg_msg->boards = 1;
	}

	if (!(slurm_conf.conf_flags & CTL_CONF_OR)) {
		double node_reg_mem_percent;
		if (conf_node_reg_mem_percent == -1) {
			if (IS_NODE_CLOUD(node_ptr))
				node_reg_mem_percent =
					DEFAULT_CLOUD_REG_MEM_PERCENT;
			else
				node_reg_mem_percent =
					DEFAULT_NODE_REG_MEM_PERCENT;
		} else
			node_reg_mem_percent = conf_node_reg_mem_percent;

		if (config_ptr->real_memory &&
		    ((((double)reg_msg->real_memory /
		       config_ptr->real_memory) * 100) <
		     node_reg_mem_percent)) {
			debug("Node %s has low real_memory size (%"PRIu64" / %"PRIu64") < %.2f%%",
			      reg_msg->node_name, reg_msg->real_memory,
			      config_ptr->real_memory, node_reg_mem_percent);
			error_code  = EINVAL;
			if (reason_down)
				xstrcat(reason_down, ", ");
			xstrfmtcat(reason_down, "Low RealMemory (reported:%"PRIu64" < %.2f%% of configured:%"PRIu64")",
				   reg_msg->real_memory, node_reg_mem_percent,
				   config_ptr->real_memory);
		}

		if (reg_msg->tmp_disk < config_ptr->tmp_disk) {
			debug("Node %s has low tmp_disk size (%u < %u)",
			      reg_msg->node_name, reg_msg->tmp_disk,
			      config_ptr->tmp_disk);
			error_code = EINVAL;
			if (reason_down)
				xstrcat(reason_down, ", ");
			xstrcat(reason_down, "Low TmpDisk");
		}
	}

	if (reg_msg->cpu_spec_list && !node_ptr->cpu_spec_list) {
		xfree(node_ptr->cpu_spec_list);
		node_ptr->cpu_spec_list = reg_msg->cpu_spec_list;
		reg_msg->cpu_spec_list = NULL;	/* Nothing left to free */

		if (_build_node_spec_bitmap(node_ptr) != SLURM_SUCCESS)
			error_code = EINVAL;
	}

	xfree(node_ptr->arch);
	node_ptr->arch = reg_msg->arch;
	reg_msg->arch = NULL;	/* Nothing left to free */

	xfree(node_ptr->os);
	node_ptr->os = reg_msg->os;
	reg_msg->os = NULL;	/* Nothing left to free */

	if (node_ptr->cpu_load != reg_msg->cpu_load) {
		node_ptr->cpu_load = reg_msg->cpu_load;
		node_ptr->cpu_load_time = now;
		last_node_update = now;
	}
	if (node_ptr->free_mem != reg_msg->free_mem) {
		node_ptr->free_mem = reg_msg->free_mem;
		node_ptr->free_mem_time = now;
		last_node_update = now;
	}

	if (node_ptr->last_response &&
	    (node_ptr->boot_time > node_ptr->last_response) &&
	    !IS_NODE_UNKNOWN(node_ptr)) {	/* Node just rebooted */
		(void) node_features_g_get_node(node_ptr->name);
	}

	if (IS_NODE_NO_RESPOND(node_ptr) ||
	    IS_NODE_POWERING_UP(node_ptr) ||
	    IS_NODE_POWERING_DOWN(node_ptr) ||
	    IS_NODE_POWERED_DOWN(node_ptr)) {
		bool was_powered_down = IS_NODE_POWERED_DOWN(node_ptr);

		info("Node %s now responding", node_ptr->name);

		/*
		 * Set last_busy in case that the node came up out of band or
		 * came up after ResumeTimeout so that it can be suspended at a
		 * later point.
		 */
		if (IS_NODE_POWERING_UP(node_ptr) ||
		    IS_NODE_POWERED_DOWN(node_ptr))
			node_ptr->last_busy = now;

		/*
		 * Set last_response if it's expected. Otherwise let it get
		 * marked at "unexpectedly rebooted". Not checked with
		 * IS_NODE_POWERED_DOWN() above to allow ReturnToService !=2
		 * catch nodes [re]booting unexpectedly.
		 */
		if (IS_NODE_POWERING_UP(node_ptr)) {
			node_ptr->last_response = now;
			was_powering_up = true;
		}

		node_ptr->node_state &= (~NODE_STATE_NO_RESPOND);
		node_ptr->node_state &= (~NODE_STATE_POWERING_UP);
		node_ptr->node_state &= (~NODE_STATE_POWERED_DOWN);
		node_ptr->node_state &= (~NODE_STATE_POWERING_DOWN);
		if (!is_node_in_maint_reservation(node_ptr->index))
			node_ptr->node_state &= (~NODE_STATE_MAINT);

		bit_clear(power_node_bitmap, node_ptr->index);

		last_node_update = now;

		if (was_powered_down)
			clusteracct_storage_g_node_up(acct_db_conn, node_ptr,
						      now);
	}

	was_invalid_reg = IS_NODE_INVALID_REG(node_ptr);
	node_ptr->node_state &= ~NODE_STATE_INVALID_REG;
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;

	if (error_code) {
		node_ptr->node_state |= NODE_STATE_INVALID_REG;
		if (!was_invalid_reg) {
			error("Setting node %s state to INVAL with reason:%s",
			       reg_msg->node_name, reason_down);

			if (was_powering_up)
				kill_running_job_by_node_name(node_ptr->name);
		}

		if (!IS_NODE_DOWN(node_ptr)
			&& !IS_NODE_DRAIN(node_ptr)
			&& ! IS_NODE_FAIL(node_ptr)) {
			drain_nodes(reg_msg->node_name, reason_down,
			            slurm_conf.slurm_user_id);
		}
		last_node_update = time (NULL);
	} else if (reg_msg->status == ESLURMD_PROLOG_FAILED
		   || reg_msg->status == ESLURMD_SETUP_ENVIRONMENT_ERROR) {
		if (!IS_NODE_DRAIN(node_ptr) && !IS_NODE_FAIL(node_ptr)) {
			char *reason;
			error("%s: Prolog or job env setup failure on node %s, "
			      "draining the node",
			      __func__, reg_msg->node_name);
			if (reg_msg->status == ESLURMD_PROLOG_FAILED)
				reason = "Prolog error";
			else
				reason = "Job env setup error";
			drain_nodes(reg_msg->node_name, reason,
			            slurm_conf.slurm_user_id);
			last_node_update = time (NULL);
		}
	} else {
		if (IS_NODE_UNKNOWN(node_ptr) || IS_NODE_FUTURE(node_ptr)) {
			bool was_future = IS_NODE_FUTURE(node_ptr);
			debug("validate_node_specs: node %s registered with "
			      "%u jobs",
			      reg_msg->node_name,reg_msg->job_count);
			if (IS_NODE_FUTURE(node_ptr)) {
				if (IS_NODE_MAINT(node_ptr) &&
				    !is_node_in_maint_reservation(
					    node_ptr->index))
					node_flags &= (~NODE_STATE_MAINT);
				node_flags &= (~NODE_STATE_REBOOT_REQUESTED);
				node_flags &= (~NODE_STATE_REBOOT_ISSUED);
			}
			if (reg_msg->job_count) {
				node_ptr->node_state = NODE_STATE_ALLOCATED |
					node_flags;
			} else {
				node_ptr->node_state = NODE_STATE_IDLE |
					node_flags;
				node_ptr->last_busy = now;
			}
			last_node_update = now;

			/* don't send this on a slurmctld unless needed */
			if (was_future || /* always send FUTURE checkins */
			    (slurmctld_init_db &&
			     !IS_NODE_DRAIN(node_ptr) &&
			     !IS_NODE_FAIL(node_ptr))) {
				/* reason information is handled in
				   clusteracct_storage_g_node_up()
				*/
				clusteracct_storage_g_node_up(
					acct_db_conn, node_ptr, now);
			}
		} else if (IS_NODE_DOWN(node_ptr) &&
			   ((slurm_conf.ret2service == 2) ||
			    IS_NODE_REBOOT_ISSUED(node_ptr) ||
			    ((slurm_conf.ret2service == 1) &&
			     !xstrcmp(node_ptr->reason, "Not responding") &&
			     (node_ptr->boot_time <
			      node_ptr->last_response)))) {
			node_flags &= (~NODE_STATE_REBOOT_ISSUED);
			if (node_ptr->next_state != NO_VAL)
				node_flags &= (~NODE_STATE_DRAIN);

			if ((node_ptr->next_state & NODE_STATE_BASE) ==
			    NODE_STATE_DOWN) {
				node_ptr->node_state = NODE_STATE_DOWN |
						       node_flags;
				set_node_reboot_reason(node_ptr,
						       "reboot complete");
			} else if (reg_msg->job_count) {
				node_ptr->node_state = NODE_STATE_ALLOCATED |
						       node_flags;
			} else {
				node_ptr->node_state = NODE_STATE_IDLE |
						       node_flags;
				node_ptr->last_busy = now;
			}
			node_ptr->next_state = NO_VAL;
			bit_clear(rs_node_bitmap, node_ptr->index);

			info("node %s returned to service",
			     reg_msg->node_name);
			trigger_node_up(node_ptr);
			last_node_update = now;
			if (!IS_NODE_DRAIN(node_ptr)
			    && !IS_NODE_DOWN(node_ptr)
			    && !IS_NODE_FAIL(node_ptr)) {
				/* reason information is handled in
				 * clusteracct_storage_g_node_up() */
				clusteracct_storage_g_node_up(
					acct_db_conn, node_ptr, now);
			}
		} else if (node_ptr->last_response &&
			   (node_ptr->boot_time > node_ptr->last_response) &&
			   (slurm_conf.ret2service != 2)) {
			if (!node_ptr->reason ||
			    (node_ptr->reason &&
			     !xstrcmp(node_ptr->reason, "Not responding"))) {
				if (node_ptr->reason)
					xfree(node_ptr->reason);
				node_ptr->reason_time = now;
				node_ptr->reason_uid = slurm_conf.slurm_user_id;
				node_ptr->reason = xstrdup(
					"Node unexpectedly rebooted");
			}
			info("%s: Node %s unexpectedly rebooted boot_time=%u last response=%u",
			     __func__, reg_msg->node_name,
			     (uint32_t)node_ptr->boot_time,
			     (uint32_t)node_ptr->last_response);
			_make_node_down(node_ptr, now);
			kill_running_job_by_node_name(reg_msg->node_name);
			last_node_update = now;
			reg_msg->job_count = 0;
		} else if (IS_NODE_ALLOCATED(node_ptr) &&
			   (reg_msg->job_count == 0)) {	/* job vanished */
			node_ptr->node_state = NODE_STATE_IDLE | node_flags;
			node_ptr->last_busy = now;
			last_node_update = now;
		} else if (IS_NODE_COMPLETING(node_ptr) &&
			   (reg_msg->job_count == 0)) {	/* job already done */
			node_ptr->node_state &= (~NODE_STATE_COMPLETING);
			last_node_update = now;
			bit_clear(cg_node_bitmap, node_ptr->index);
		} else if (IS_NODE_IDLE(node_ptr) &&
			   (reg_msg->job_count != 0)) {
			if (node_ptr->run_job_cnt != 0) {
				node_ptr->node_state = NODE_STATE_ALLOCATED |
						       node_flags;
				error("Invalid state for node %s, was IDLE "
			      	      "with %u running jobs",
			      	      node_ptr->name, reg_msg->job_count);
			}
			/*
			 * there must be completing job(s) on this node since
			 * reg_msg->job_count was set (run_job_cnt +
			 * comp_job_cnt) in validate_jobs_on_node()
			 */
			if (node_ptr->comp_job_cnt != 0) {
				node_ptr->node_state |= NODE_STATE_COMPLETING;
				bit_set(cg_node_bitmap, node_ptr->index);
			}
			last_node_update = now;
		}
		if (IS_NODE_IDLE(node_ptr)) {
			node_ptr->owner = NO_VAL;
			xfree(node_ptr->mcs_label);
		}

		select_g_update_node_config(node_ptr->index);
		_sync_bitmaps(node_ptr, reg_msg->job_count);
	}

	xfree(reason_down);
	if (reg_msg->energy)
		memcpy(node_ptr->energy, reg_msg->energy,
		       sizeof(acct_gather_energy_t));

	node_ptr->last_response = now;
	node_ptr->boot_req_time = (time_t) 0;
	node_ptr->power_save_req_time = (time_t) 0;

	*newly_up = (!orig_node_avail &&
		     bit_test(avail_node_bitmap, node_ptr->index));

	if (!error_code && IS_NODE_CLOUD(node_ptr) && cloud_reg_addrs) {
		slurm_addr_t addr;
		char *comm_name = NULL, *hostname = NULL;

		/* Get IP of slurmd */
		if (slurm_msg->conn_fd >= 0 &&
		    !slurm_get_peer_addr(slurm_msg->conn_fd, &addr)) {
			comm_name = xmalloc(INET6_ADDRSTRLEN);
			slurm_get_ip_str(&addr, comm_name, INET6_ADDRSTRLEN);
		}

		if (slurm_msg->protocol_version <= SLURM_21_08_PROTOCOL_VERSION)
			hostname = auth_g_get_host(slurm_msg->auth_cred);
		else
			hostname = xstrdup(reg_msg->hostname);

		set_node_comm_name(
			node_ptr,
			comm_name ? comm_name : hostname,
			hostname);

		xfree(comm_name);
		xfree(hostname);
	}

	return error_code;
}

static front_end_record_t * _front_end_reg(
		slurm_node_registration_status_msg_t *reg_msg)
{
	front_end_record_t *front_end_ptr;
	uint32_t state_base, state_flags;
	time_t now = time(NULL);

	debug2("name:%s boot_time:%u up_time:%u",
	       reg_msg->node_name, (unsigned int) reg_msg->slurmd_start_time,
	       reg_msg->up_time);

	front_end_ptr = find_front_end_record(reg_msg->node_name);
	if (front_end_ptr == NULL) {
		error("Registration message from unknown node %s",
		      reg_msg->node_name);
		return NULL;
	}

	front_end_ptr->boot_time = now - reg_msg->up_time;
	if (front_end_ptr->last_response &&
	    (front_end_ptr->boot_time > front_end_ptr->last_response)) {
		info("front end %s unexpectedly rebooted, "
		     "killing all previously running jobs running on it.",
		     reg_msg->node_name);
		(void) kill_job_by_front_end_name(front_end_ptr->name);
		reg_msg->job_count = 0;
	}

	front_end_ptr->last_response = now;
	front_end_ptr->slurmd_start_time = reg_msg->slurmd_start_time;
	state_base  = front_end_ptr->node_state & JOB_STATE_BASE;
	state_flags = front_end_ptr->node_state & JOB_STATE_FLAGS;
	if ((state_base == NODE_STATE_DOWN) && (front_end_ptr->reason) &&
	    (!xstrncmp(front_end_ptr->reason, "Not responding", 14))) {
		error("front end node %s returned to service",
		      reg_msg->node_name);
		state_base = NODE_STATE_IDLE;
		xfree(front_end_ptr->reason);
		front_end_ptr->reason_time = (time_t) 0;
		front_end_ptr->reason_uid = 0;
	}
	if (state_base == NODE_STATE_UNKNOWN)
		state_base = NODE_STATE_IDLE;

	state_flags &= (~NODE_STATE_NO_RESPOND);

	front_end_ptr->node_state = state_base | state_flags;
	last_front_end_update = now;
	return front_end_ptr;
}

/*
 * validate_nodes_via_front_end - validate all nodes on a cluster as having
 *	a valid configuration as soon as the front-end registers. Individual
 *	nodes will not register with this configuration
 * IN reg_msg - node registration message
 * IN protocol_version - Version of Slurm on this node
 * OUT newly_up - set if node newly brought into service
 * RET 0 if no error, Slurm error code otherwise
 */
extern int validate_nodes_via_front_end(
		slurm_node_registration_status_msg_t *reg_msg,
		uint16_t protocol_version, bool *newly_up)
{
	int error_code = 0, i, j, rc;
	bool update_node_state = false;
	job_record_t *job_ptr;
	config_record_t *config_ptr;
	node_record_t *node_ptr = NULL;
	time_t now = time(NULL);
	ListIterator job_iterator;
	hostlist_t reg_hostlist = NULL;
	char *host_str = NULL, *reason_down = NULL;
	uint32_t node_flags;
	front_end_record_t *front_end_ptr;

	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(FED_LOCK, READ_LOCK));

	if (reg_msg->up_time > now) {
		error("Node up_time on %s is invalid: %u>%u",
		      reg_msg->node_name, reg_msg->up_time, (uint32_t) now);
		reg_msg->up_time = 0;
	}

	front_end_ptr = _front_end_reg(reg_msg);
	if (front_end_ptr == NULL)
		return ESLURM_INVALID_NODE_NAME;

	front_end_ptr->protocol_version = protocol_version;
	xfree(front_end_ptr->version);
	front_end_ptr->version = reg_msg->version;
	reg_msg->version = NULL;
	*newly_up = false;

	if (reg_msg->status == ESLURMD_PROLOG_FAILED) {
		error("Prolog failed on node %s", reg_msg->node_name);
		/* Do NOT set the node DOWN here. Unlike non-front-end systems,
		 * this failure is likely due to some problem in the underlying
		 * infrastructure (e.g. the block failed to boot). */
		/* set_front_end_down(front_end_ptr, "Prolog failed"); */
	}

	/* First validate the job info */
	for (i = 0; i < reg_msg->job_count; i++) {
		if ( (reg_msg->step_id[i].job_id >= MIN_NOALLOC_JOBID) &&
		     (reg_msg->step_id[i].job_id <= MAX_NOALLOC_JOBID) ) {
			info("NoAllocate %ps reported",
			     &reg_msg->step_id[i]);
			continue;
		}

		job_ptr = find_job_record(reg_msg->step_id[i].job_id);
		if (job_ptr && job_ptr->node_bitmap &&
		    ((j = bit_ffs(job_ptr->node_bitmap)) >= 0))
			node_ptr = node_record_table_ptr[j];

		if (job_ptr == NULL) {
			error("Orphan %ps reported on node %s",
			      &reg_msg->step_id[i],
			      front_end_ptr->name);
			abort_job_on_node(reg_msg->step_id[i].job_id,
					  job_ptr, front_end_ptr->name);
			continue;
		} else if (job_ptr->batch_host == NULL) {
			error("Resetting NULL batch_host of JobId=%u to %s",
			      reg_msg->step_id[i].job_id, front_end_ptr->name);
			job_ptr->batch_host = xstrdup(front_end_ptr->name);
		}


		if (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr)) {
			debug3("Registered %pJ %ps on %s",
			       job_ptr,
			       &reg_msg->step_id[i],
			       front_end_ptr->name);
			if (job_ptr->batch_flag) {
				/* NOTE: Used for purging defunct batch jobs */
				job_ptr->time_last_active = now;
			}
		}

		else if (IS_JOB_COMPLETING(job_ptr)) {
			/*
			 * Re-send kill request as needed,
			 * not necessarily an error
			 */
			kill_job_on_node(job_ptr, node_ptr);
		}

		else if (IS_JOB_PENDING(job_ptr)) {
			/* Typically indicates a job requeue and the hung
			 * slurmd that went DOWN is now responding */
			error("Registered PENDING %pJ %ps on %s",
			      job_ptr,
			      &reg_msg->step_id[i],
			      front_end_ptr->name);
			abort_job_on_node(reg_msg->step_id[i].job_id, job_ptr,
					  front_end_ptr->name);
		} else if (difftime(now, job_ptr->end_time) <
		           slurm_conf.msg_timeout) {
			/* Race condition */
			debug("Registered newly completed %pJ %ps on %s",
			      job_ptr,
			      &reg_msg->step_id[i],
			      front_end_ptr->name);
		}

		else {		/* else job is supposed to be done */
			error("Registered %pJ %ps in state %s on %s",
			      job_ptr,
			      &reg_msg->step_id[i],
			      job_state_string(job_ptr->job_state),
			      front_end_ptr->name);
			kill_job_on_node(job_ptr, node_ptr);
		}
	}


	/* purge orphan batch jobs */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = list_next(job_iterator))) {
		if (!IS_JOB_RUNNING(job_ptr) ||
		    IS_JOB_CONFIGURING(job_ptr) ||
		    (job_ptr->batch_flag == 0))
			continue;
		if (job_ptr->front_end_ptr != front_end_ptr)
			continue;
		if (difftime(now, job_ptr->time_last_active) <= 5)
			continue;
		info("Killing orphan batch %pJ", job_ptr);
		job_complete(job_ptr->job_id, slurm_conf.slurm_user_id,
		             false, false, 0);
	}
	list_iterator_destroy(job_iterator);

	(void) gres_node_config_unpack(reg_msg->gres_info,
				       node_record_table_ptr[i]->name);
	for (i = 0; (node_ptr = next_node(&i)); i++) {
		bool acct_updated = false;

		config_ptr = node_ptr->config_ptr;
		node_ptr->last_response = now;

		rc = gres_node_config_validate(
			node_ptr->name,
			config_ptr->gres,
			&node_ptr->gres,
			&node_ptr->gres_list,
			reg_msg->threads,
			reg_msg->cores,
			reg_msg->sockets,
			slurm_conf.conf_flags & CTL_CONF_OR,
			&reason_down);
		if (rc) {
			if (!IS_NODE_DOWN(node_ptr)) {
				error("Setting node %s state to DOWN",
				      node_ptr->name);
			}
			set_node_down(node_ptr->name, reason_down);
			last_node_update = now;
		}
		xfree(reason_down);
		gres_node_state_log(node_ptr->gres_list, node_ptr->name);

		if (reg_msg->up_time) {
			node_ptr->up_time = reg_msg->up_time;
			node_ptr->boot_time = now - reg_msg->up_time;
		}
		node_ptr->slurmd_start_time = reg_msg->slurmd_start_time;

		if (IS_NODE_NO_RESPOND(node_ptr)) {
			update_node_state = true;
			/* This is handled by the select/cray plugin */
			node_ptr->node_state &= (~NODE_STATE_NO_RESPOND);
			node_ptr->node_state &= (~NODE_STATE_POWERING_UP);
		}

		if (reg_msg->status != ESLURMD_PROLOG_FAILED) {
			if (reg_hostlist)
				(void) hostlist_push_host(reg_hostlist,
							  node_ptr->name);
			else
				reg_hostlist = hostlist_create(node_ptr->name);

			node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
			if (IS_NODE_UNKNOWN(node_ptr)) {
				update_node_state = true;
				*newly_up = true;
				if (node_ptr->run_job_cnt) {
					node_ptr->node_state =
						NODE_STATE_ALLOCATED |
						node_flags;
				} else {
					node_ptr->node_state =
						NODE_STATE_IDLE |
						node_flags;
					node_ptr->last_busy = now;
				}
				if (!IS_NODE_DRAIN(node_ptr) &&
				    !IS_NODE_FAIL(node_ptr)) {
					/* reason information is handled in
					 * clusteracct_storage_g_node_up() */
					clusteracct_storage_g_node_up(
						acct_db_conn,
						node_ptr, now);
					acct_updated = true;
				}
			} else if (IS_NODE_DOWN(node_ptr) &&
				   ((slurm_conf.ret2service == 2) ||
				    (node_ptr->boot_req_time != 0)    ||
				    ((slurm_conf.ret2service == 1) &&
				     !xstrcmp(node_ptr->reason,
					      "Not responding")))) {
				update_node_state = true;
				*newly_up = true;
				if (node_ptr->run_job_cnt) {
					node_ptr->node_state =
						NODE_STATE_ALLOCATED |
						node_flags;
				} else {
					node_ptr->node_state =
						NODE_STATE_IDLE |
						node_flags;
					node_ptr->last_busy = now;
				}
				trigger_node_up(node_ptr);
				if (!IS_NODE_DRAIN(node_ptr) &&
				    !IS_NODE_FAIL(node_ptr)) {
					/* reason information is handled in
					 * clusteracct_storage_g_node_up() */
					clusteracct_storage_g_node_up(
						acct_db_conn,
						node_ptr, now);
					acct_updated = true;
				}
			} else if (IS_NODE_ALLOCATED(node_ptr) &&
				   (node_ptr->run_job_cnt == 0)) {
				/* job vanished */
				update_node_state = true;
				node_ptr->node_state = NODE_STATE_IDLE |
					node_flags;
				node_ptr->last_busy = now;
			} else if (IS_NODE_COMPLETING(node_ptr) &&
				   (node_ptr->comp_job_cnt == 0)) {
				/* job already done */
				update_node_state = true;
				node_ptr->node_state &=
					(~NODE_STATE_COMPLETING);
				bit_clear(cg_node_bitmap, i);
			} else if (IS_NODE_IDLE(node_ptr) &&
				   (node_ptr->run_job_cnt != 0)) {
				update_node_state = true;
				node_ptr->node_state = NODE_STATE_ALLOCATED |
						       node_flags;
				error("Invalid state for node %s, was IDLE "
				      "with %u running jobs",
				      node_ptr->name, reg_msg->job_count);
			}
			if (IS_NODE_IDLE(node_ptr)) {
				node_ptr->owner = NO_VAL;
				xfree(node_ptr->mcs_label);
			}

			select_g_update_node_config(i);
			_sync_bitmaps(node_ptr,
				      (node_ptr->run_job_cnt +
				       node_ptr->comp_job_cnt));
		}
		if (reg_msg->energy)
			memcpy(node_ptr->energy, reg_msg->energy,
			       sizeof(acct_gather_energy_t));

		if (!acct_updated && slurmctld_init_db &&
		    !IS_NODE_DOWN(node_ptr) &&
		    !IS_NODE_DRAIN(node_ptr) && !IS_NODE_FAIL(node_ptr)) {
			/* reason information is handled in
			   clusteracct_storage_g_node_up()
			*/
			clusteracct_storage_g_node_up(
				acct_db_conn, node_ptr, now);
		}

	}

	if (reg_hostlist) {
		hostlist_uniq(reg_hostlist);
		host_str = hostlist_ranged_string_xmalloc(reg_hostlist);
		debug("Nodes %s have registered", host_str);
		xfree(host_str);
		hostlist_destroy(reg_hostlist);
	}

	if (update_node_state)
		last_node_update = time (NULL);
	return error_code;
}

/* Sync idle, share, and avail_node_bitmaps for a given node */
static void _sync_bitmaps(node_record_t *node_ptr, int job_count)
{
	if (job_count == 0) {
		bit_set (idle_node_bitmap, node_ptr->index);
		bit_set (share_node_bitmap, node_ptr->index);
	}
	if (IS_NODE_DOWN(node_ptr) || IS_NODE_DRAIN(node_ptr) ||
	    IS_NODE_FAIL(node_ptr) || IS_NODE_NO_RESPOND(node_ptr))
		bit_clear (avail_node_bitmap, node_ptr->index);
	else
		make_node_avail(node_ptr);
	if (IS_NODE_DOWN(node_ptr))
		bit_clear (up_node_bitmap, node_ptr->index);
	else
		bit_set   (up_node_bitmap, node_ptr->index);
}

#ifdef HAVE_FRONT_END
static void _node_did_resp(front_end_record_t *fe_ptr)
{
	uint32_t node_flags;
	time_t now = time(NULL);

	fe_ptr->last_response = now;

	if (IS_NODE_NO_RESPOND(fe_ptr)) {
		info("Node %s now responding", fe_ptr->name);
		last_front_end_update = now;
		fe_ptr->node_state &= (~NODE_STATE_NO_RESPOND);
	}

	node_flags = fe_ptr->node_state & NODE_STATE_FLAGS;
	if (IS_NODE_UNKNOWN(fe_ptr)) {
		last_front_end_update = now;
		fe_ptr->node_state = NODE_STATE_IDLE | node_flags;
	}
	if (IS_NODE_DOWN(fe_ptr) &&
	    ((slurm_conf.ret2service == 2) ||
	     ((slurm_conf.ret2service == 1) &&
	      !xstrcmp(fe_ptr->reason, "Not responding")))) {
		last_front_end_update = now;
		fe_ptr->node_state = NODE_STATE_IDLE | node_flags;
		info("node_did_resp: node %s returned to service",
		     fe_ptr->name);
		trigger_front_end_up(fe_ptr);
		if (!IS_NODE_DRAIN(fe_ptr) && !IS_NODE_FAIL(fe_ptr)) {
			xfree(fe_ptr->reason);
			fe_ptr->reason_time = 0;
			fe_ptr->reason_uid = NO_VAL;
		}
	}
	return;
}
#else
static void _node_did_resp(node_record_t *node_ptr)
{
	uint32_t node_flags;
	time_t now = time(NULL);

	if (waiting_for_node_boot(node_ptr) ||
	    waiting_for_node_power_down(node_ptr))
		return;
	node_ptr->last_response = now;
	if (IS_NODE_NO_RESPOND(node_ptr) || IS_NODE_POWERING_UP(node_ptr)) {
		info("Node %s now responding", node_ptr->name);
		node_ptr->node_state &= (~NODE_STATE_NO_RESPOND);
		node_ptr->node_state &= (~NODE_STATE_POWERING_UP);
		if (!is_node_in_maint_reservation(node_ptr->index))
			node_ptr->node_state &= (~NODE_STATE_MAINT);
		last_node_update = now;
	}
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	if (IS_NODE_UNKNOWN(node_ptr)) {
		node_ptr->last_busy = now;
		if (node_ptr->run_job_cnt) {
			node_ptr->node_state = NODE_STATE_ALLOCATED |
					       node_flags;
		} else
			node_ptr->node_state = NODE_STATE_IDLE | node_flags;
		last_node_update = now;
		if (!IS_NODE_DRAIN(node_ptr) && !IS_NODE_FAIL(node_ptr)) {
			clusteracct_storage_g_node_up(acct_db_conn,
						      node_ptr, now);
		}
	}
	if (IS_NODE_DOWN(node_ptr) &&
	    ((slurm_conf.ret2service == 2) ||
	     (node_ptr->boot_req_time != 0)    ||
	     ((slurm_conf.ret2service == 1) &&
	      !xstrcmp(node_ptr->reason, "Not responding")))) {
		node_ptr->last_busy = now;
		node_ptr->node_state = NODE_STATE_IDLE | node_flags;
		info("node_did_resp: node %s returned to service",
		     node_ptr->name);
		trigger_node_up(node_ptr);
		last_node_update = now;
		if (!IS_NODE_DRAIN(node_ptr) && !IS_NODE_FAIL(node_ptr)) {
			/* reason information is handled in
			   clusteracct_storage_g_node_up()
			*/
			clusteracct_storage_g_node_up(acct_db_conn,
						      node_ptr, now);
		}
	}
	if (IS_NODE_IDLE(node_ptr) && !IS_NODE_COMPLETING(node_ptr)) {
		bit_set (idle_node_bitmap, node_ptr->index);
		bit_set (share_node_bitmap, node_ptr->index);
	}
	if (IS_NODE_DOWN(node_ptr) ||
	    IS_NODE_DRAIN(node_ptr) ||
	    IS_NODE_FAIL(node_ptr) ||
	    (IS_NODE_POWER_DOWN(node_ptr) && !IS_NODE_ALLOCATED(node_ptr))) {
		bit_clear (avail_node_bitmap, node_ptr->index);
	} else
		bit_set   (avail_node_bitmap, node_ptr->index);
	if (IS_NODE_DOWN(node_ptr))
		bit_clear (up_node_bitmap, node_ptr->index);
	else
		bit_set   (up_node_bitmap, node_ptr->index);
	return;
}
#endif

/*
 * node_did_resp - record that the specified node is responding
 * IN name - name of the node
 */
void node_did_resp (char *name)
{
#ifdef HAVE_FRONT_END
	front_end_record_t *node_ptr;
	node_ptr = find_front_end_record (name);
#else
	node_record_t *node_ptr;
	node_ptr = find_node_record (name);
#endif

	xassert(verify_lock(CONF_LOCK, READ_LOCK));

	if (node_ptr == NULL) {
		error ("node_did_resp unable to find node %s", name);
		return;
	}
	_node_did_resp(node_ptr);
	debug2("node_did_resp %s",name);
}

/*
 * node_not_resp - record that the specified node is not responding
 * IN name - name of the node
 * IN msg_time - time message was sent
 */
void node_not_resp (char *name, time_t msg_time, slurm_msg_type_t resp_type)
{
#ifdef HAVE_FRONT_END
	front_end_record_t *node_ptr;

	node_ptr = find_front_end_record (name);
#else
	node_record_t *node_ptr;

	node_ptr = find_node_record (name);
#endif
	if (node_ptr == NULL) {
		error ("node_not_resp unable to find node %s", name);
		return;
	}

	/* If the slurmd on the node responded with something we don't
	 * want to ever set the node down, so mark that the node
	 * responded, but for whatever reason there was a
	 * communication error.  This makes it so we don't mark the
	 * node down if the slurmd really is there (Wrong protocol
	 * version or munge issue or whatever) so we don't kill
	 * any running jobs.  RESPONSE_FORWARD_FAILED means we
	 * couldn't contact the slurmd.
	 * last_response could be in the future if boot in progress.
	 */
	if (resp_type != RESPONSE_FORWARD_FAILED) {
		node_ptr->last_response = MAX(msg_time - 1,
					      node_ptr->last_response);
	}

	if (!IS_NODE_DOWN(node_ptr)) {
		/* Logged by node_no_resp_msg() on periodic basis */
		node_ptr->not_responding = true;
	}

	if (IS_NODE_NO_RESPOND(node_ptr) ||
	    IS_NODE_POWERING_DOWN(node_ptr) ||
	    IS_NODE_POWERED_DOWN(node_ptr))
		return;		/* Already known to be not responding */

	if (node_ptr->last_response >= msg_time) {
		debug("node_not_resp: node %s responded since msg sent",
		      node_ptr->name);
		return;
	}

	node_ptr->node_state |= NODE_STATE_NO_RESPOND;
#ifdef HAVE_FRONT_END
	last_front_end_update = time(NULL);
#else
	last_node_update = time(NULL);
	bit_clear (avail_node_bitmap, node_ptr->index);
#endif

	return;
}

/* For every node with the "not_responding" flag set, clear the flag
 * and log that the node is not responding using a hostlist expression */
extern void node_no_resp_msg(void)
{
	int i;
	node_record_t *node_ptr;
	char *host_str = NULL;
	hostlist_t no_resp_hostlist = NULL;

	for (i = 0; (node_ptr = next_node(&i)); i++) {
		if (!node_ptr->not_responding ||
		    IS_NODE_POWERED_DOWN(node_ptr) ||
		    IS_NODE_POWERING_DOWN(node_ptr) ||
		    IS_NODE_POWERING_UP(node_ptr))
			continue;
		if (no_resp_hostlist) {
			(void) hostlist_push_host(no_resp_hostlist,
						  node_ptr->name);
		} else
			no_resp_hostlist = hostlist_create(node_ptr->name);
		node_ptr->not_responding = false;
 	}
	if (no_resp_hostlist) {
		hostlist_uniq(no_resp_hostlist);
		host_str = hostlist_ranged_string_xmalloc(no_resp_hostlist);
		error("Nodes %s not responding", host_str);
		xfree(host_str);
		hostlist_destroy(no_resp_hostlist);
	}
}

/*
 * set_node_down - make the specified compute node's state DOWN and
 *	kill jobs as needed
 * IN name - name of the node
 * IN reason - why the node is DOWN
 */
void set_node_down (char *name, char *reason)
{
	node_record_t *node_ptr;

	node_ptr = find_node_record (name);
	if (node_ptr == NULL) {
		error ("set_node_down unable to find node %s", name);
		return;
	}
	set_node_down_ptr (node_ptr, reason);

	return;
}

/*
 * set_node_down_ptr - make the specified compute node's state DOWN and
 *	kill jobs as needed
 * IN node_ptr - node_ptr to the node
 * IN reason - why the node is DOWN
 */
void set_node_down_ptr(node_record_t *node_ptr, char *reason)
{
	time_t now = time(NULL);

	if ((node_ptr->reason == NULL) ||
	    (xstrncmp(node_ptr->reason, "Not responding", 14) == 0)) {
		xfree(node_ptr->reason);
		if (reason) {
			node_ptr->reason = xstrdup(reason);
			node_ptr->reason_time = now;
			node_ptr->reason_uid = slurm_conf.slurm_user_id;
		} else {
			node_ptr->reason_time = 0;
			node_ptr->reason_uid = NO_VAL;
		}
	}
	_make_node_down(node_ptr, now);
	(void) kill_running_job_by_node_name(node_ptr->name);
	_sync_bitmaps(node_ptr, 0);

	return;
}

/*
 * is_node_down - determine if the specified node's state is DOWN
 * IN name - name of the node
 * RET true if node exists and is down, otherwise false
 */
bool is_node_down (char *name)
{
	node_record_t *node_ptr;

	node_ptr = find_node_record (name);
	if (node_ptr == NULL) {
		error ("is_node_down unable to find node %s", name);
		return false;
	}

	if (IS_NODE_DOWN(node_ptr))
		return true;
	return false;
}

/*
 * is_node_resp - determine if the specified node's state is responding
 * IN name - name of the node
 * RET true if node exists and is responding, otherwise false
 */
bool is_node_resp (char *name)
{
#ifdef HAVE_FRONT_END
	front_end_record_t *node_ptr;

	node_ptr = find_front_end_record (name);
#else
	node_record_t *node_ptr;

	node_ptr = find_node_record (name);
#endif
	if (node_ptr == NULL) {
		error ("is_node_resp unable to find node %s", name);
		return false;
	}

	if (IS_NODE_NO_RESPOND(node_ptr))
		return false;
	return true;
}

/*
 * find_first_node_record - find a record for first node in the bitmap
 * IN node_bitmap
 */
node_record_t *find_first_node_record(bitstr_t *node_bitmap)
{
	int inx;

	if (node_bitmap == NULL) {
		error ("find_first_node_record passed null bitstring");
		return NULL;
	}

	inx = bit_ffs (node_bitmap);
	if (inx < 0)
		return NULL;
	else
		return node_record_table_ptr[inx];
}

/*
 * msg_to_slurmd - send given msg_type (REQUEST_RECONFIGURE or REQUEST_SHUTDOWN)
 * to every slurmd
 */
void msg_to_slurmd (slurm_msg_type_t msg_type)
{
	int i;
	shutdown_msg_t *shutdown_req;
	agent_arg_t *kill_agent_args;
#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr;
#else
	node_record_t *node_ptr;
#endif

	kill_agent_args = xmalloc (sizeof (agent_arg_t));
	kill_agent_args->msg_type = msg_type;
	kill_agent_args->retry = 0;
	kill_agent_args->hostlist = hostlist_create(NULL);
	if (msg_type == REQUEST_SHUTDOWN) {
 		shutdown_req = xmalloc(sizeof(shutdown_msg_t));
		shutdown_req->options = 0;
		kill_agent_args->msg_args = shutdown_req;
	}

	kill_agent_args->protocol_version = SLURM_PROTOCOL_VERSION;

#ifdef HAVE_FRONT_END
	for (i = 0, front_end_ptr = front_end_nodes;
	     i < front_end_node_cnt; i++, front_end_ptr++) {
		if (kill_agent_args->protocol_version >
		    front_end_ptr->protocol_version)
			kill_agent_args->protocol_version =
				front_end_ptr->protocol_version;

		hostlist_push_host(kill_agent_args->hostlist,
				   front_end_ptr->name);
		kill_agent_args->node_count++;
	}
#else
	for (i = 0; (node_ptr = next_node(&i)); i++) {
		if (IS_NODE_FUTURE(node_ptr))
			continue;
		if (IS_NODE_CLOUD(node_ptr) &&
		    (IS_NODE_POWERED_DOWN(node_ptr) ||
		     IS_NODE_POWERING_DOWN(node_ptr)))
			continue;
		if (kill_agent_args->protocol_version >
		    node_record_table_ptr[node_ptr->index]->protocol_version)
			kill_agent_args->protocol_version =
				node_record_table_ptr[node_ptr->index]->
				protocol_version;
		hostlist_push_host(kill_agent_args->hostlist, node_ptr->name);
		kill_agent_args->node_count++;
	}
#endif

	if (kill_agent_args->node_count == 0) {
		hostlist_destroy(kill_agent_args->hostlist);
		xfree (kill_agent_args);
	} else {
		debug ("Spawning agent msg_type=%d", msg_type);
		set_agent_arg_r_uid(kill_agent_args, SLURM_AUTH_UID_ANY);
		agent_queue_request(kill_agent_args);
	}
}

/*
 * Specialized version of msg_to_slurmd that handles cross-version issues
 * when running configless.
 *
 * Since the REQUEST_RECONFIGURE message had no body, you could get away with
 * sending under the oldest format of any slurmd attached to the system.
 *
 * For configless, this would mean nothing gets sent to anyone, and those
 * older slurmds get REQUEST_RECONFIGURE_WITH_CONFIG and ignore it.
 *
 * So explicitly split the pool into three groups.
 * Note: DOES NOT SUPPORT FRONTEND.
 */
void push_reconfig_to_slurmd(char **slurmd_config_files)
{
#ifndef HAVE_FRONT_END
	agent_arg_t *curr_args, *prev_args, *old_args;
	node_record_t *node_ptr;
	config_response_msg_t *curr_config, *prev_config, *old_config;

	/*
	 * The 'curr_args' is when we pivoted to a List holding configs.
	 * As long as that same pack code is maintained, this is fine
	 * going forward.
	 */
	curr_args = xmalloc(sizeof(*curr_args));
	curr_args->msg_type = REQUEST_RECONFIGURE_WITH_CONFIG;
	curr_args->retry = 0;
	curr_args->hostlist = hostlist_create(NULL);
	curr_args->protocol_version = SLURM_PROTOCOL_VERSION;
	curr_config = xmalloc(sizeof(*curr_config));
	load_config_response_list(curr_config, slurmd_config_files);
	curr_args->msg_args = curr_config;

	prev_args = xmalloc(sizeof(*prev_args));
	prev_args->msg_type = REQUEST_RECONFIGURE_WITH_CONFIG;
	prev_args->retry = 0;
	prev_args->hostlist = hostlist_create(NULL);
	prev_args->protocol_version = SLURM_ONE_BACK_PROTOCOL_VERSION;
	prev_config = xmalloc(sizeof(*prev_config));
	load_config_response_msg(prev_config, CONFIG_REQUEST_SLURMD);
	prev_args->msg_args = prev_config;

	old_args = xmalloc(sizeof(*old_args));
	old_args->msg_type = REQUEST_RECONFIGURE_WITH_CONFIG;
	old_args->retry = 0;
	old_args->hostlist = hostlist_create(NULL);
	old_args->protocol_version = SLURM_MIN_PROTOCOL_VERSION;
	old_config = xmalloc(sizeof(*old_config));
	load_config_response_msg(old_config, CONFIG_REQUEST_SLURMD);
	old_args->msg_args = old_config;

	for (int i = 0; (node_ptr = next_node(&i)); i++) {
		if (IS_NODE_FUTURE(node_ptr))
			continue;
		if (IS_NODE_CLOUD(node_ptr) &&
		    (IS_NODE_POWERED_DOWN(node_ptr) ||
		     IS_NODE_POWERING_DOWN(node_ptr)))
			continue;

		if (node_ptr->protocol_version >= SLURM_PROTOCOL_VERSION) {
			hostlist_push_host(curr_args->hostlist, node_ptr->name);
			curr_args->node_count++;
		} else if (node_ptr->protocol_version ==
			   SLURM_ONE_BACK_PROTOCOL_VERSION) {
			hostlist_push_host(prev_args->hostlist, node_ptr->name);
			prev_args->node_count++;
		} else if (node_ptr->protocol_version ==
			   SLURM_MIN_PROTOCOL_VERSION) {
			hostlist_push_host(old_args->hostlist, node_ptr->name);
			old_args->node_count++;
		}
	}

	if (curr_args->node_count == 0) {
		hostlist_destroy(curr_args->hostlist);
		slurm_free_config_response_msg(curr_config);
		xfree(curr_args);
	} else {
		debug("Spawning agent msg_type=%d", curr_args->msg_type);
		set_agent_arg_r_uid(curr_args, SLURM_AUTH_UID_ANY);
		agent_queue_request(curr_args);
	}

	if (prev_args->node_count == 0) {
		hostlist_destroy(prev_args->hostlist);
		slurm_free_config_response_msg(prev_config);
		xfree(prev_args);
	} else {
		debug("Spawning agent msg_type=%d", prev_args->msg_type);
		set_agent_arg_r_uid(prev_args, SLURM_AUTH_UID_ANY);
		agent_queue_request(prev_args);
	}

	if (old_args->node_count == 0) {
		hostlist_destroy(old_args->hostlist);
		slurm_free_config_response_msg(old_config);
		xfree(old_args);
	} else {
		debug("Spawning agent msg_type=%d", old_args->msg_type);
		set_agent_arg_r_uid(old_args, SLURM_AUTH_UID_ANY);
		agent_queue_request(old_args);
	}
#else
	error("%s: Cannot use configless with FrontEnd mode! Sending normal reconfigure request.",
	      __func__);
	msg_to_slurmd(REQUEST_RECONFIGURE);
#endif
}


/*
 * make_node_alloc - flag specified node as allocated to a job
 * IN node_ptr - pointer to node being allocated
 * IN job_ptr  - pointer to job that is starting
 */
extern void make_node_alloc(node_record_t *node_ptr, job_record_t *job_ptr)
{
	uint32_t node_flags;

	(node_ptr->run_job_cnt)++;
	bit_clear(idle_node_bitmap, node_ptr->index);
	if (job_ptr->details && (job_ptr->details->share_res == 0)) {
		bit_clear(share_node_bitmap, node_ptr->index);
		(node_ptr->no_share_job_cnt)++;
	}

	if ((job_ptr->details &&
	     (job_ptr->details->whole_node == WHOLE_NODE_USER)) ||
	    (job_ptr->part_ptr &&
	     (job_ptr->part_ptr->flags & PART_FLAG_EXCLUSIVE_USER))) {
		node_ptr->owner_job_cnt++;
		node_ptr->owner = job_ptr->user_id;
	}

	if (slurm_mcs_get_select(job_ptr) == 1) {
		xfree(node_ptr->mcs_label);
		node_ptr->mcs_label = xstrdup(job_ptr->mcs_label);
	}

	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	node_ptr->node_state = NODE_STATE_ALLOCATED | node_flags;
	xfree(node_ptr->reason);
	node_ptr->reason_time = 0;
	node_ptr->reason_uid = NO_VAL;

	last_node_update = time(NULL);
}

/* make_node_avail - flag specified node as available */
extern void make_node_avail(node_record_t *node_ptr)
{
	if (IS_NODE_POWER_DOWN(node_ptr) || IS_NODE_POWERING_DOWN(node_ptr))
		return;
	bit_set(avail_node_bitmap, node_ptr->index);

	/*
	 * If we are in the middle of a backfill cycle, this bitmap is
	 * used (when bf_continue is enabled) to avoid scheduling lower
	 * priority jobs on to newly available resources.
	 */
	bit_set(bf_ignore_node_bitmap, node_ptr->index);
}

/* make_node_comp - flag specified node as completing a job
 * IN node_ptr - pointer to node marked for completion of job
 * IN job_ptr - pointer to job that is completing
 * IN suspended - true if job was previously suspended
 */
extern void make_node_comp(node_record_t *node_ptr, job_record_t *job_ptr,
			   bool suspended)
{
	uint32_t node_flags;
	time_t now = time(NULL);

	xassert(node_ptr);
	if (suspended) {
		if (node_ptr->sus_job_cnt) {
			(node_ptr->sus_job_cnt)--;
		} else {
			error("%s: %pJ node %s sus_job_cnt underflow", __func__,
			      job_ptr, node_ptr->name);
		}
	} else {
		if (node_ptr->run_job_cnt) {
			(node_ptr->run_job_cnt)--;
		} else {
			error("%s: %pJ node %s run_job_cnt underflow", __func__,
			      job_ptr, node_ptr->name);
		}
		if (job_ptr->details && (job_ptr->details->share_res == 0)) {
			if (node_ptr->no_share_job_cnt) {
				(node_ptr->no_share_job_cnt)--;
			} else {
				error("%s: %pJ node %s no_share_job_cnt underflow",
				      __func__, job_ptr, node_ptr->name);
			}
			if (node_ptr->no_share_job_cnt == 0)
				bit_set(share_node_bitmap, node_ptr->index);
		}
	}

	/* Sync up conditionals with deallocate_nodes() */
	if (!IS_NODE_DOWN(node_ptr) &&
	    !IS_NODE_POWERED_DOWN(node_ptr) &&
	    !IS_NODE_POWERING_UP(node_ptr)) {
		/* Don't verify RPC if node in DOWN or POWER_UP state */
		(node_ptr->comp_job_cnt)++;
		node_ptr->node_state |= NODE_STATE_COMPLETING;
		bit_set(cg_node_bitmap, node_ptr->index);
	}
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;

	if ((node_ptr->run_job_cnt  == 0) &&
	    (node_ptr->comp_job_cnt == 0)) {
		node_ptr->last_busy = now;
		bit_set(idle_node_bitmap, node_ptr->index);
		if (IS_NODE_DRAIN(node_ptr) || IS_NODE_FAIL(node_ptr)) {
			trigger_node_drained(node_ptr);
			clusteracct_storage_g_node_down(
				acct_db_conn,
				node_ptr, now, NULL,
				slurm_conf.slurm_user_id);
		}
	}

	if (IS_NODE_DOWN(node_ptr)) {
		debug3("%s: Node %s being left DOWN", __func__, node_ptr->name);
	} else if (node_ptr->run_job_cnt)
		node_ptr->node_state = NODE_STATE_ALLOCATED | node_flags;
	else {
		node_ptr->node_state = NODE_STATE_IDLE | node_flags;
		node_ptr->last_busy = now;
	}
	last_node_update = now;
}

/*
 * Subset of _make_node_down() except for marking node down, trigger and
 * accounting update.
 */
static void _make_node_unavail(node_record_t *node_ptr)
{
	xassert(node_ptr);

	node_ptr->node_state &= (~NODE_STATE_COMPLETING);
	bit_clear(avail_node_bitmap, node_ptr->index);
	bit_clear(cg_node_bitmap, node_ptr->index);
	bit_set(idle_node_bitmap, node_ptr->index);
	bit_set(share_node_bitmap, node_ptr->index);
	bit_clear(up_node_bitmap, node_ptr->index);
}

/* _make_node_down - flag specified node as down */
static void _make_node_down(node_record_t *node_ptr, time_t event_time)
{
	uint32_t node_flags;

	xassert(node_ptr);

	_make_node_unavail(node_ptr);
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	node_ptr->node_state = NODE_STATE_DOWN | node_flags;
	node_ptr->owner = NO_VAL;
	xfree(node_ptr->mcs_label);
	trigger_node_down(node_ptr);
	last_node_update = time (NULL);
	clusteracct_storage_g_node_down(acct_db_conn,
					node_ptr, event_time, NULL,
					node_ptr->reason_uid);
	/*
	 * check all reservations since node may have been in a reservation with
	 * floating count of nodes that needs to be updated
	 */
	validate_all_reservations(false);
}

/*
 * make_node_idle - flag specified node as having finished with a job
 * IN node_ptr - pointer to node reporting job completion
 * IN job_ptr - pointer to job that just completed or NULL if not applicable
 */
void make_node_idle(node_record_t *node_ptr, job_record_t *job_ptr)
{
	uint32_t node_flags;
	time_t now = time(NULL);
	bitstr_t *node_bitmap = NULL;

	if (job_ptr) {
		if (job_ptr->node_bitmap_cg)
			node_bitmap = job_ptr->node_bitmap_cg;
		else
			node_bitmap = job_ptr->node_bitmap;
	}

	log_flag(TRACE_JOBS, "%s: enter %pJ", __func__, job_ptr);

	xassert(node_ptr);
	if (node_bitmap && (bit_test(node_bitmap, node_ptr->index))) {
		/* Not a replay */
		last_job_update = now;
		bit_clear(node_bitmap, node_ptr->index);

		if (!IS_JOB_FINISHED(job_ptr))
			job_update_tres_cnt(job_ptr, node_ptr->index);

		if (job_ptr->node_cnt) {
			/*
			 * Clean up the JOB_COMPLETING flag
			 * only if there is not the slurmctld
			 * epilog running, otherwise wait
			 * when it terminates then this
			 * function will be invoked.
			 */
			job_ptr->node_cnt--;
			if ((job_ptr->node_cnt == 0) &&
			    !job_ptr->epilog_running)
				cleanup_completing(job_ptr);
		} else if ((job_ptr->total_cpus == 0) &&
			   (job_ptr->total_nodes == 0)) {
			/* Job resized to zero nodes (expanded another job) */
		} else {
			error("%s: %pJ node_cnt underflow", __func__, job_ptr);
		}

		if (IS_JOB_SUSPENDED(job_ptr)) {
			/* Remove node from suspended job */
			if (node_ptr->sus_job_cnt)
				(node_ptr->sus_job_cnt)--;
			else
				error("%s: %pJ node %s sus_job_cnt underflow",
				      __func__, job_ptr, node_ptr->name);
		} else if (IS_JOB_RUNNING(job_ptr)) {
			/* Remove node from running job */
			if (node_ptr->run_job_cnt)
				(node_ptr->run_job_cnt)--;
			else
				error("%s: %pJ node %s run_job_cnt underflow",
				      __func__, job_ptr, node_ptr->name);
		} else {
			if (node_ptr->comp_job_cnt) {
				(node_ptr->comp_job_cnt)--;
			} else if (IS_NODE_DOWN(node_ptr)) {
				/* We were not expecting this response,
				 * ignore it */
			} else {
				error("%s: %pJ node %s comp_job_cnt underflow",
				      __func__, job_ptr, node_ptr->name);
			}
			if (node_ptr->comp_job_cnt > 0)
				goto fini;	/* More jobs completing */
		}
	}

	if (node_ptr->comp_job_cnt == 0) {
		node_ptr->node_state &= (~NODE_STATE_COMPLETING);
		bit_clear(cg_node_bitmap, node_ptr->index);
		if (IS_NODE_IDLE(node_ptr)) {
			node_ptr->owner = NO_VAL;
			xfree(node_ptr->mcs_label);
		}
	}

	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	if (IS_NODE_DOWN(node_ptr) || IS_NODE_FUTURE(node_ptr)) {
		debug3("%s: %pJ node %s being left %s",
		       __func__, job_ptr, node_ptr->name,
		       node_state_base_string(node_ptr->node_state));
		goto fini;
	}
	bit_set(up_node_bitmap, node_ptr->index);

	if (IS_NODE_DRAIN(node_ptr) || IS_NODE_FAIL(node_ptr) ||
	    IS_NODE_NO_RESPOND(node_ptr))
		bit_clear(avail_node_bitmap, node_ptr->index);
	else
		make_node_avail(node_ptr);

	if ((IS_NODE_DRAIN(node_ptr) || IS_NODE_FAIL(node_ptr)) &&
	    (node_ptr->run_job_cnt == 0) && (node_ptr->comp_job_cnt == 0)) {
		node_ptr->node_state = NODE_STATE_IDLE | node_flags;
		bit_set(idle_node_bitmap, node_ptr->index);
		debug3("%s: %pJ node %s is DRAINED",
		       __func__, job_ptr, node_ptr->name);
		node_ptr->last_busy = now;
		trigger_node_drained(node_ptr);
		if (!IS_NODE_REBOOT_REQUESTED(node_ptr) &&
		    !IS_NODE_REBOOT_ISSUED(node_ptr))
			clusteracct_storage_g_node_down(acct_db_conn,
			                                node_ptr, now, NULL,
			                                slurm_conf.slurm_user_id);
	} else if (node_ptr->run_job_cnt) {
		node_ptr->node_state = NODE_STATE_ALLOCATED | node_flags;
		if (!IS_NODE_NO_RESPOND(node_ptr) &&
		     !IS_NODE_FAIL(node_ptr) && !IS_NODE_DRAIN(node_ptr))
			make_node_avail(node_ptr);
	} else {
		node_ptr->node_state = NODE_STATE_IDLE | node_flags;
		if (!IS_NODE_NO_RESPOND(node_ptr) &&
		     !IS_NODE_FAIL(node_ptr) && !IS_NODE_DRAIN(node_ptr))
			make_node_avail(node_ptr);
		if (!IS_NODE_NO_RESPOND(node_ptr) &&
		    !IS_NODE_COMPLETING(node_ptr))
			bit_set(idle_node_bitmap, node_ptr->index);
		node_ptr->last_busy = now;
	}

	if (IS_NODE_IDLE(node_ptr) && IS_NODE_POWER_DOWN(node_ptr)) {
		/*
		 * Now that the node is idle and is to be powered off, remove
		 * from the avail_node_bitmap to prevent jobs being scheduled on
		 * the node before it power's off.
		 */
		bit_clear(avail_node_bitmap, node_ptr->index);
	}

fini:
	if (job_ptr &&
	    ((job_ptr->details &&
	      (job_ptr->details->whole_node == WHOLE_NODE_USER)) ||
	     (job_ptr->part_ptr &&
	      (job_ptr->part_ptr->flags & PART_FLAG_EXCLUSIVE_USER)))) {
		if (node_ptr->owner_job_cnt == 0) {
			error("%s: node_ptr->owner_job_cnt underflow",
			      __func__);
		} else if (--node_ptr->owner_job_cnt == 0) {
			node_ptr->owner = NO_VAL;
			xfree(node_ptr->mcs_label);
		}
	}
	last_node_update = now;
}

extern int send_nodes_to_accounting(time_t event_time)
{
	int rc = SLURM_SUCCESS, i = 0;
	node_record_t *node_ptr = NULL;
	char *reason = NULL;
	slurmctld_lock_t node_read_lock = {
		.node = READ_LOCK,
	};

 	lock_slurmctld(node_read_lock);
	/* send nodes not in 'up' state */
	for (i = 0; (node_ptr = next_node(&i)); i++) {
		if (!node_ptr->name)
			continue;
		if (node_ptr->reason)
			reason = node_ptr->reason;
		else
			reason = "First Registration";
		if (IS_NODE_DRAIN(node_ptr) ||
		    IS_NODE_FAIL(node_ptr) ||
		    IS_NODE_DOWN(node_ptr) ||
		    IS_NODE_FUTURE(node_ptr) ||
		    (IS_NODE_CLOUD(node_ptr) &&
		     IS_NODE_POWERED_DOWN(node_ptr)))
			rc = clusteracct_storage_g_node_down(
				acct_db_conn,
				node_ptr, event_time,
				reason,
				slurm_conf.slurm_user_id);
		if (rc == SLURM_ERROR)
			break;
	}
	unlock_slurmctld(node_read_lock);
	return rc;
}

/* node_fini - free all memory associated with node records */
extern void node_fini (void)
{
	FREE_NULL_LIST(active_feature_list);
	FREE_NULL_LIST(avail_feature_list);
	FREE_NULL_BITMAP(avail_node_bitmap);
	FREE_NULL_BITMAP(bf_ignore_node_bitmap);
	FREE_NULL_BITMAP(booting_node_bitmap);
	FREE_NULL_BITMAP(cg_node_bitmap);
	FREE_NULL_BITMAP(future_node_bitmap);
	FREE_NULL_BITMAP(idle_node_bitmap);
	FREE_NULL_BITMAP(power_node_bitmap);
	FREE_NULL_BITMAP(share_node_bitmap);
	FREE_NULL_BITMAP(up_node_bitmap);
	FREE_NULL_BITMAP(rs_node_bitmap);
	node_fini2();
}

/* Reset a node's CPU load value */
extern void reset_node_load(char *node_name, uint32_t cpu_load)
{
#ifdef HAVE_FRONT_END
	return;
#else
	node_record_t *node_ptr;

	node_ptr = find_node_record(node_name);
	if (node_ptr) {
		time_t now = time(NULL);
		node_ptr->cpu_load = cpu_load;
		node_ptr->cpu_load_time = now;
		last_node_update = now;
	} else
		error("reset_node_load unable to find node %s", node_name);
#endif
}

/* Reset a node's free memory value */
extern void reset_node_free_mem(char *node_name, uint64_t free_mem)
{
#ifdef HAVE_FRONT_END
	return;
#else
	node_record_t *node_ptr;

	node_ptr = find_node_record(node_name);
	if (node_ptr) {
		time_t now = time(NULL);
		node_ptr->free_mem = free_mem;
		node_ptr->free_mem_time = now;
		last_node_update = now;
	} else
		error("reset_node_free_mem unable to find node %s", node_name);
#endif
}


/*
 * Check for nodes that haven't rebooted yet.
 *
 * If the node hasn't booted by ResumeTimeout, mark the node as down.
 */
extern void check_reboot_nodes()
{
	int i;
	node_record_t *node_ptr;
	time_t now = time(NULL);
	uint16_t resume_timeout = slurm_conf.resume_timeout;
	static bool power_save_on = false;
	static time_t sched_update = 0;

	if (sched_update != slurm_conf.last_update) {
		power_save_on = power_save_test();
		sched_update = slurm_conf.last_update;
	}

	for (i = 0; (node_ptr = next_node(&i)); i++) {

		if ((IS_NODE_REBOOT_ISSUED(node_ptr) ||
		     (!power_save_on && IS_NODE_POWERING_UP(node_ptr))) &&
		    node_ptr->boot_req_time &&
		    (node_ptr->boot_req_time + resume_timeout < now)) {
			set_node_reboot_reason(node_ptr,
					       "reboot timed out");

			/*
			 * Remove states now so that event state shows as DOWN.
			 */
			node_ptr->node_state &= (~NODE_STATE_POWERING_UP);
			node_ptr->node_state &= (~NODE_STATE_REBOOT_ISSUED);
			node_ptr->node_state &= (~NODE_STATE_DRAIN);
			node_ptr->boot_req_time = 0;
			set_node_down_ptr(node_ptr, NULL);

			bit_clear(rs_node_bitmap, node_ptr->index);
		}
	}
}

extern bool waiting_for_node_boot(node_record_t *node_ptr)
{
	xassert(node_ptr);

	if ((IS_NODE_POWERING_UP(node_ptr) ||
	     IS_NODE_REBOOT_ISSUED(node_ptr)) &&
	    (node_ptr->boot_time < node_ptr->boot_req_time)) {
		debug("Still waiting for boot of node %s", node_ptr->name);
		return true;
	}

	return false;
}

extern bool waiting_for_node_power_down(node_record_t *node_ptr)
{
	xassert(node_ptr);

	if (IS_NODE_POWERING_DOWN(node_ptr) &&
	    node_ptr->power_save_req_time &&
	    (node_ptr->boot_time <
	     (node_ptr->power_save_req_time + slurm_conf.suspend_timeout))) {
		debug("Still waiting for node '%s' to power off",
		      node_ptr->name);
		return true;
	}

	return false;
}

extern void set_node_comm_name(node_record_t *node_ptr, char *comm_name,
			       char *hostname)
{
	char *new_comm_name = xstrdup(comm_name);
	char *new_hostname = xstrdup(hostname);

	xfree(node_ptr->comm_name);
	node_ptr->comm_name = new_comm_name;

	xfree(node_ptr->node_hostname);
	node_ptr->node_hostname = new_hostname;

	slurm_reset_alias(node_ptr->name,
			  node_ptr->comm_name,
			  node_ptr->node_hostname);
}

static int _foreach_build_part_bitmap(void *x, void *arg)
{
	build_part_bitmap(x);
	return SLURM_SUCCESS;
}

static void _update_parts()
{
	/* scan partition table and identify nodes in each */
	list_for_each(part_list, _foreach_build_part_bitmap, NULL);
	set_partition_tres();
}

static void _build_node_callback(char *alias, char *hostname, char *address,
				 char *bcast_address, uint16_t port,
				 int state_val, slurm_conf_node_t *conf_node,
				 config_record_t *config_ptr)
{
	node_record_t *node_ptr;

	if (!(node_ptr = add_node_record(alias, config_ptr)))
		return;

	if ((state_val != NO_VAL) &&
	    (state_val != NODE_STATE_UNKNOWN))
		node_ptr->node_state = state_val;
	node_ptr->last_response = (time_t) 0;
	node_ptr->comm_name = xstrdup(address);
	node_ptr->cpu_bind  = conf_node->cpu_bind;
	node_ptr->node_hostname = xstrdup(hostname);
	node_ptr->bcast_address = xstrdup(bcast_address);
	node_ptr->port = port;
	node_ptr->reason = xstrdup(conf_node->reason);

	node_ptr->node_state |= NODE_STATE_DYNAMIC_NORM;

	slurm_reset_alias(node_ptr->name,
			  node_ptr->comm_name,
			  node_ptr->node_hostname);

	if (config_ptr->feature) {
		node_ptr->features = xstrdup(config_ptr->feature);
		node_ptr->features_act = xstrdup(config_ptr->feature);
	}

	if (IS_NODE_FUTURE(node_ptr)) {
		bit_set(future_node_bitmap, node_ptr->index);
	} else if (IS_NODE_CLOUD(node_ptr)) {
		make_node_idle(node_ptr, NULL);
		bit_set(power_node_bitmap, node_ptr->index);

		gres_g_node_config_load(
			node_ptr->config_ptr->cpus, node_ptr->name,
			node_ptr->gres_list, NULL, NULL);
		gres_node_config_validate(
			node_ptr->name, node_ptr->config_ptr->gres,
			&node_ptr->gres, &node_ptr->gres_list,
			node_ptr->config_ptr->threads,
			node_ptr->config_ptr->cores,
			node_ptr->config_ptr->tot_sockets,
			slurm_conf.conf_flags & CTL_CONF_OR, NULL);
	}
}

extern int create_nodes(char *nodeline, char **err_msg)
{
	int state_val, rc = SLURM_SUCCESS;
	slurm_conf_node_t *conf_node;
	config_record_t *config_ptr;
	s_p_hashtbl_t *node_hashtbl = NULL;
	slurmctld_lock_t write_lock = {
		.conf = READ_LOCK,
		.job = WRITE_LOCK,
		.node = WRITE_LOCK,
		.part = WRITE_LOCK
	};

	xassert(nodeline);
	xassert(err_msg);

	if (!xstrstr(slurm_conf.select_type, "cons_tres")) {
		*err_msg = xstrdup("Node creation only compatible with select/cons_tres");
		error("%s", *err_msg);
		return ESLURM_ACCESS_DENIED;
	}

	lock_slurmctld(write_lock);

	if (!(conf_node = slurm_conf_parse_nodeline(nodeline, &node_hashtbl))) {
		*err_msg = xstrdup_printf("Failed to parse nodeline '%s'",
					  nodeline);
		error("%s", *err_msg);
		rc = SLURM_ERROR;
		goto fini;
	}

	state_val = state_str2int(conf_node->state, conf_node->nodenames);
	if ((state_val == NO_VAL) ||
	    ((state_val != NODE_STATE_FUTURE) &&
	     !(state_val & NODE_STATE_CLOUD))) {
		*err_msg = xstrdup("Only State=FUTURE and State=CLOUD allowed for nodes created by scontrol");
		error("%s", *err_msg);
		rc = ESLURM_INVALID_NODE_STATE;
		goto fini;
	}

	config_ptr = config_record_from_conf_node(conf_node,
						  slurmctld_tres_cnt);
	config_ptr->node_bitmap = bit_alloc(node_record_count);

	expand_nodeline_info(conf_node, config_ptr, _build_node_callback);
	s_p_hashtbl_destroy(node_hashtbl);

	if (config_ptr->feature) {
		update_feature_list(avail_feature_list, config_ptr->feature,
				    config_ptr->node_bitmap);
		update_feature_list(active_feature_list, config_ptr->feature,
				    config_ptr->node_bitmap);
	}

	set_cluster_tres(false);
	_update_parts();
	power_save_set_timeouts(NULL);
	select_g_reconfigure();

fini:
	unlock_slurmctld(write_lock);

	if (rc == SLURM_SUCCESS) {
		/* Must be called outside of locks */
		clusteracct_storage_g_cluster_tres(
			acct_db_conn, NULL, NULL, 0, SLURM_PROTOCOL_VERSION);
	}

	return rc;
}

extern int create_dynamic_reg_node(slurm_msg_t *msg)
{
	config_record_t *config_ptr;
	node_record_t *node_ptr;
	slurm_addr_t addr;
	char *comm_name = NULL;
	int state_val = NODE_STATE_UNKNOWN;
	slurm_node_registration_status_msg_t *reg_msg = msg->data;

	xassert(verify_lock(JOB_LOCK, WRITE_LOCK));
	xassert(verify_lock(NODE_LOCK, WRITE_LOCK));
	xassert(verify_lock(PART_LOCK, WRITE_LOCK));

	if (!xstrstr(slurm_conf.select_type, "cons_tres")) {
		error("Node creation only compatible with select/cons_tres");
		return ESLURM_ACCESS_DENIED;
	}

	if (find_node_record2(reg_msg->node_name))
		return SLURM_SUCCESS;

	if (reg_msg->dynamic_conf) {
		slurm_conf_node_t *conf_node;
		s_p_hashtbl_t *node_hashtbl = NULL;

		if (!(conf_node =
		      slurm_conf_parse_nodeline(reg_msg->dynamic_conf,
						&node_hashtbl))) {
			s_p_hashtbl_destroy(node_hashtbl);
			error("Failed to parse dynamic nodeline '%s'",
			      reg_msg->dynamic_conf);
			return SLURM_ERROR;
		}
		config_ptr = config_record_from_conf_node(conf_node,
							  slurmctld_tres_cnt);
		if (conf_node->state)
			state_val = state_str2int(conf_node->state,
						  conf_node->nodenames);

		s_p_hashtbl_destroy(node_hashtbl);
	} else {
		config_ptr = create_config_record();
		config_ptr->boards = reg_msg->boards;
		config_ptr->cores = reg_msg->cores;
		config_ptr->cpus = reg_msg->cpus;
		config_ptr->nodes = xstrdup(reg_msg->node_name);
		config_ptr->real_memory = reg_msg->real_memory;
		config_ptr->threads = reg_msg->threads;
		config_ptr->tmp_disk = reg_msg->tmp_disk;
		config_ptr->tot_sockets = reg_msg->sockets;
	}

	config_ptr->node_bitmap = bit_alloc(node_record_count);

	if (!(node_ptr = add_node_record(reg_msg->node_name, config_ptr))) {
		list_delete_ptr(config_list, config_ptr);
		return SLURM_ERROR;
	}

	/* Get IP of slurmd */
	if (msg->conn_fd >= 0 &&
	    !slurm_get_peer_addr(msg->conn_fd, &addr)) {
		comm_name = xmalloc(INET6_ADDRSTRLEN);
		slurm_get_ip_str(&addr, comm_name,
				 INET6_ADDRSTRLEN);
	}

	set_node_comm_name(node_ptr,
			   comm_name ? comm_name : reg_msg->hostname,
			   reg_msg->hostname);
	xfree(comm_name);

	node_ptr->features = xstrdup(node_ptr->config_ptr->feature);
	update_feature_list(avail_feature_list, node_ptr->features,
			    config_ptr->node_bitmap);
	node_ptr->features_act = xstrdup(node_ptr->config_ptr->feature);
	update_feature_list(active_feature_list, node_ptr->features_act,
			    config_ptr->node_bitmap);

	/* Handle DOWN and DRAIN, otherwise make the node idle */
	if ((state_val == NODE_STATE_DOWN) ||
	    (state_val & NODE_STATE_DRAIN)) {
		_make_node_down(node_ptr, time(NULL));
		node_ptr->node_state = state_val;
	} else
		make_node_idle(node_ptr, NULL);

	node_ptr->node_state |= NODE_STATE_DYNAMIC_NORM;

	set_cluster_tres(false);
	_update_parts();
	power_save_set_timeouts(NULL);
	select_g_reconfigure();

	return SLURM_SUCCESS;
}

static void _remove_node_from_features(node_record_t *node_ptr)
{
	bitstr_t *node_bitmap = bit_alloc(node_record_count);
	bit_set(node_bitmap, node_ptr->index);
	update_feature_list(avail_feature_list, NULL, node_bitmap);
	update_feature_list(active_feature_list, NULL, node_bitmap);
	bit_free(node_bitmap);
}

/*
 * Has to be in slurmctld code for locking.
 */
static int _delete_node(char *name)
{
	node_record_t *node_ptr;

	node_ptr = find_node_record(name);
	if (!node_ptr) {
		error("Unable to find node %s to delete", name);
		return ESLURM_INVALID_NODE_NAME;
	}
	if (IS_NODE_ALLOCATED(node_ptr) ||
	    IS_NODE_COMPLETING(node_ptr)) {
		error("Node '%s' can't be delete because it's still in use.",
		      name);
		return ESLURM_NODES_BUSY;
	}
	if (node_ptr->node_state & NODE_STATE_RES) {
		error("Node '%s' can't be delete because it's in a reservation.",
		      name);
		return ESLURM_NODES_BUSY;
	}

	bit_clear(idle_node_bitmap, node_ptr->index);
	bit_clear(avail_node_bitmap, node_ptr->index);

	_remove_node_from_features(node_ptr);

	delete_node_record(node_ptr);
	slurm_conf_remove_node(name);

	return SLURM_SUCCESS;
}

extern int delete_nodes(char *names, char **err_msg)
{
	char *node_name;
	hostlist_t to_delete;
	bool one_success = false;
	int ret_rc = SLURM_SUCCESS;
	hostlist_t error_hostlist = NULL;

	xassert(err_msg);

	slurmctld_lock_t write_lock = {
		.job = WRITE_LOCK, .node = WRITE_LOCK, .part = WRITE_LOCK};

	if (!xstrstr(slurm_conf.select_type, "cons_tres")) {
		*err_msg = xstrdup("Node deletion only compatible with select/cons_tres");
		error("%s", *err_msg);
		return ESLURM_ACCESS_DENIED;
	}

	lock_slurmctld(write_lock);

	if (!(to_delete = nodespec_to_hostlist(names, NULL))) {
		ret_rc = ESLURM_INVALID_NODE_NAME;
		goto cleanup;
	}
	if (!hostlist_count(to_delete)) {
		info("%s: expansion of node specification '%s' resulted in zero nodes",
		     __func__, names);
		ret_rc = ESLURM_INVALID_NODE_NAME;
		goto cleanup;
	}

	while ((node_name = hostlist_shift(to_delete))) {
		int rc;
		if ((rc = _delete_node(node_name))) {
			error("failed to delete node '%s'", node_name);
			if (!error_hostlist)
				error_hostlist = hostlist_create(node_name);
			else
				hostlist_push_host(error_hostlist, node_name);
		} else
			one_success = true;
		ret_rc |= rc;
		free(node_name);
	}

	if (one_success) {
		rehash_node();
		_update_parts();
		select_g_reconfigure();
	}
	if (error_hostlist) {
		char *nodes = hostlist_ranged_string_xmalloc(error_hostlist);
		*err_msg = xstrdup_printf("failed to delete nodes %s", nodes);
		xfree(nodes);
		FREE_NULL_HOSTLIST(error_hostlist);
	}

cleanup:
	unlock_slurmctld(write_lock);
	if (one_success) {
		/* Must be called outside of locks */
		clusteracct_storage_g_cluster_tres(
			acct_db_conn, NULL, NULL, 0, SLURM_PROTOCOL_VERSION);
	}

	FREE_NULL_HOSTLIST(to_delete);

	return ret_rc;
}

extern void set_node_reboot_reason(node_record_t *node_ptr, char *message)
{
	xassert(verify_lock(CONF_LOCK, READ_LOCK));
	xassert(node_ptr);

	if (message == NULL) {
		xfree(node_ptr->reason);
		node_ptr->reason_time = 0;
		node_ptr->reason_uid = NO_VAL;
	} else {
		if (node_ptr->reason &&
		    !xstrstr(node_ptr->reason, message)) {
			xstrfmtcat(node_ptr->reason, " : %s", message);
		} else {
			xfree(node_ptr->reason);
			node_ptr->reason = xstrdup(message);
		}
		node_ptr->reason_time = time(NULL);
		node_ptr->reason_uid = slurm_conf.slurm_user_id;
	}
}
