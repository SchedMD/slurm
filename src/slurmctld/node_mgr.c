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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif


#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "src/common/gres.h"
#include "src/common/hostlist.h"
#include "src/common/macros.h"
#include "src/common/pack.h"
#include "src/common/parse_time.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/common/node_select.h"
#include "src/common/read_config.h"
#include "src/common/slurm_accounting_storage.h"
#include "src/common/slurm_acct_gather_energy.h"
#include "src/common/slurm_ext_sensors.h"
#include "src/slurmctld/agent.h"
#include "src/slurmctld/front_end.h"
#include "src/slurmctld/locks.h"
#include "src/slurmctld/ping_nodes.h"
#include "src/slurmctld/proc_req.h"
#include "src/slurmctld/reservation.h"
#include "src/slurmctld/sched_plugin.h"
#include "src/slurmctld/slurmctld.h"
#include "src/slurmctld/slurmctld_plugstack.h"
#include "src/slurmctld/state_save.h"
#include "src/common/timers.h"
#include "src/slurmctld/trigger_mgr.h"
#include "src/plugins/select/bluegene/bg_enums.h"

#define _DEBUG		0
#define MAX_RETRIES	10

/* Change NODE_STATE_VERSION value when changing the state save format */
#define NODE_STATE_VERSION        "PROTOCOL_VERSION"
#define NODE_2_6_STATE_VERSION    "VER006"	/* SLURM version 2.6 */

/* Global variables */
bitstr_t *avail_node_bitmap = NULL;	/* bitmap of available nodes */
bitstr_t *cg_node_bitmap    = NULL;	/* bitmap of completing nodes */
bitstr_t *idle_node_bitmap  = NULL;	/* bitmap of idle nodes */
bitstr_t *power_node_bitmap = NULL;	/* bitmap of powered down nodes */
bitstr_t *share_node_bitmap = NULL;  	/* bitmap of sharable nodes */
bitstr_t *up_node_bitmap    = NULL;  	/* bitmap of non-down nodes */

static void 	_dump_node_state (struct node_record *dump_node_ptr,
				  Buf buffer);
static front_end_record_t * _front_end_reg(
				slurm_node_registration_status_msg_t *reg_msg);
static bool	_is_cloud_hidden(struct node_record *node_ptr);
static void 	_make_node_down(struct node_record *node_ptr,
				time_t event_time);
static bool	_node_is_hidden(struct node_record *node_ptr);
static int	_open_node_state_file(char **state_file);
static void 	_pack_node(struct node_record *dump_node_ptr, Buf buffer,
			   uint16_t protocol_version, uint16_t show_flags);
static void	_sync_bitmaps(struct node_record *node_ptr, int job_count);
static void	_update_config_ptr(bitstr_t *bitmap,
				struct config_record *config_ptr);
static int	_update_node_features(char *node_names, char *features);
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
	struct node_record *node_ptr;
	/* Locks: Read config and node */
	slurmctld_lock_t node_read_lock = { READ_LOCK, NO_LOCK, READ_LOCK,
					    NO_LOCK };
	Buf buffer = init_buf(high_buffer_size);
	DEF_TIMERS;

	START_TIMER;
	/* write header: version, time */
	packstr(NODE_STATE_VERSION, buffer);
	pack16(SLURM_PROTOCOL_VERSION, buffer);
	pack_time(time (NULL), buffer);

	/* write node records to buffer */
	lock_slurmctld (node_read_lock);
	for (inx = 0, node_ptr = node_record_table_ptr; inx < node_record_count;
	     inx++, node_ptr++) {
		xassert (node_ptr->magic == NODE_MAGIC);
		xassert (node_ptr->config_ptr->magic == CONFIG_MAGIC);
		_dump_node_state (node_ptr, buffer);
	}

	old_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (old_file, "/node_state.old");
	reg_file = xstrdup (slurmctld_conf.state_save_location);
	xstrcat (reg_file, "/node_state");
	new_file = xstrdup (slurmctld_conf.state_save_location);
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
static void
_dump_node_state (struct node_record *dump_node_ptr, Buf buffer)
{
	packstr (dump_node_ptr->comm_name, buffer);
	packstr (dump_node_ptr->name, buffer);
	packstr (dump_node_ptr->node_hostname, buffer);
	packstr (dump_node_ptr->reason, buffer);
	packstr (dump_node_ptr->features, buffer);
	packstr (dump_node_ptr->gres, buffer);
	packstr (dump_node_ptr->cpu_spec_list, buffer);
	pack32  (dump_node_ptr->node_state, buffer);
	pack16  (dump_node_ptr->cpus, buffer);
	pack16  (dump_node_ptr->boards, buffer);
	pack16  (dump_node_ptr->sockets, buffer);
	pack16  (dump_node_ptr->cores, buffer);
	pack16  (dump_node_ptr->core_spec_cnt, buffer);
	pack16  (dump_node_ptr->threads, buffer);
	pack32  (dump_node_ptr->real_memory, buffer);
	pack32  (dump_node_ptr->mem_spec_limit, buffer);
	pack32  (dump_node_ptr->tmp_disk, buffer);
	pack32  (dump_node_ptr->reason_uid, buffer);
	pack_time(dump_node_ptr->reason_time, buffer);
	pack16  (dump_node_ptr->protocol_version, buffer);
	(void) gres_plugin_node_state_pack(dump_node_ptr->gres_list, buffer,
					   dump_node_ptr->name);
}


/* Open the node state save file, or backup if necessary.
 * state_file IN - the name of the state save file used
 * RET the file description to read from or error code
 */
static int _open_node_state_file(char **state_file)
{
	int state_fd;
	struct stat stat_buf;

	*state_file = xstrdup(slurmctld_conf.state_save_location);
	xstrcat(*state_file, "/node_state");
	state_fd = open(*state_file, O_RDONLY);
	if (state_fd < 0) {
		error("Could not open node state file %s: %m", *state_file);
	} else if (fstat(state_fd, &stat_buf) < 0) {
		error("Could not stat node state file %s: %m", *state_file);
		(void) close(state_fd);
	} else if (stat_buf.st_size < 10) {
		error("Node state file %s too small", *state_file);
		(void) close(state_fd);
	} else 	/* Success */
		return state_fd;

	error("NOTE: Trying backup state save file. Information may be lost!");
	xstrcat(*state_file, ".old");
	state_fd = open(*state_file, O_RDONLY);
	return state_fd;
}

/*
 * load_all_node_state - Load the node state from file, recover on slurmctld
 *	restart. Execute this after loading the configuration file data.
 *	Data goes into common storage.
 * IN state_only - if true, overwrite only node state and reason
 *	Use this to overwrite the "UNKNOWN state typically used in slurm.conf
 * RET 0 or error code
 * NOTE: READ lock_slurmctld config before entry
 */
extern int load_all_node_state ( bool state_only )
{
	char *comm_name = NULL, *node_hostname = NULL;
	char *node_name = NULL, *reason = NULL, *data = NULL, *state_file;
	char *features = NULL, *gres = NULL, *cpu_spec_list = NULL;
	int data_allocated, data_read = 0, error_code = 0, node_cnt = 0;
	uint16_t node_state2, core_spec_cnt = 0;
	uint32_t node_state;
	uint16_t cpus = 1, boards = 1, sockets = 1, cores = 1, threads = 1;
	uint32_t real_memory, tmp_disk, data_size = 0, name_len;
	uint32_t reason_uid = NO_VAL, mem_spec_limit = 0;
	time_t reason_time = 0;
	List gres_list = NULL;
	struct node_record *node_ptr;
	int state_fd;
	time_t time_stamp, now = time(NULL);
	Buf buffer;
	char *ver_str = NULL;
	hostset_t hs = NULL;
	bool power_save_mode = false;
	uint16_t protocol_version = (uint16_t)NO_VAL;

	if (slurmctld_conf.suspend_program && slurmctld_conf.resume_program)
		power_save_mode = true;

	/* read the file */
	lock_state_files ();
	state_fd = _open_node_state_file(&state_file);
	if (state_fd < 0) {
		info ("No node state file (%s) to recover", state_file);
		error_code = ENOENT;
	}
	else {
		data_allocated = BUF_SIZE;
		data = xmalloc(data_allocated);
		while (1) {
			data_read = read(state_fd, &data[data_size], BUF_SIZE);
			if (data_read < 0) {
				if (errno == EINTR)
					continue;
				else {
					error ("Read error on %s: %m",
						state_file);
					break;
				}
			} else if (data_read == 0)     /* eof */
				break;
			data_size      += data_read;
			data_allocated += data_read;
			xrealloc(data, data_allocated);
		}
		close (state_fd);
	}
	xfree (state_file);
	unlock_state_files ();

	buffer = create_buf (data, data_size);

	safe_unpackstr_xmalloc( &ver_str, &name_len, buffer);
	debug3("Version string in node_state header is %s", ver_str);
	if (ver_str) {
		if (!strcmp(ver_str, NODE_STATE_VERSION))
			safe_unpack16(&protocol_version, buffer);
		else if (!strcmp(ver_str, NODE_2_6_STATE_VERSION))
			protocol_version = SLURM_2_6_PROTOCOL_VERSION;
	}

	if (protocol_version == (uint16_t)NO_VAL) {
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
		uint16_t base_state2;
		uint16_t obj_protocol_version = (uint16_t)NO_VAL;
		if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
			safe_unpackstr_xmalloc (&comm_name, &name_len, buffer);
			safe_unpackstr_xmalloc (&node_name, &name_len, buffer);
			safe_unpackstr_xmalloc (&node_hostname,
							    &name_len, buffer);
			safe_unpackstr_xmalloc (&reason,    &name_len, buffer);
			safe_unpackstr_xmalloc (&features,  &name_len, buffer);
			safe_unpackstr_xmalloc (&gres,      &name_len, buffer);
			safe_unpackstr_xmalloc (&cpu_spec_list,
							    &name_len, buffer);
			safe_unpack32 (&node_state,  buffer);
			safe_unpack16 (&cpus,        buffer);
			safe_unpack16 (&boards,     buffer);
			safe_unpack16 (&sockets,     buffer);
			safe_unpack16 (&cores,       buffer);
			safe_unpack16 (&core_spec_cnt, buffer);
			safe_unpack16 (&threads,     buffer);
			safe_unpack32 (&real_memory, buffer);
			safe_unpack32 (&mem_spec_limit, buffer);
			safe_unpack32 (&tmp_disk,    buffer);
			safe_unpack32 (&reason_uid,  buffer);
			safe_unpack_time (&reason_time, buffer);
			safe_unpack16 (&obj_protocol_version, buffer);
			if (gres_plugin_node_state_unpack(
				    &gres_list, buffer, node_name,
				    protocol_version) != SLURM_SUCCESS)
				goto unpack_error;
			base_state = node_state & NODE_STATE_BASE;
		} else if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
			safe_unpackstr_xmalloc (&comm_name, &name_len, buffer);
			safe_unpackstr_xmalloc (&node_name, &name_len, buffer);
			safe_unpackstr_xmalloc (&node_hostname,
							    &name_len, buffer);
			safe_unpackstr_xmalloc (&reason,    &name_len, buffer);
			safe_unpackstr_xmalloc (&features,  &name_len, buffer);
			safe_unpackstr_xmalloc (&gres,      &name_len, buffer);
			safe_unpack16 (&node_state2,  buffer);
			safe_unpack16 (&cpus,        buffer);
			safe_unpack16 (&boards,     buffer);
			safe_unpack16 (&sockets,     buffer);
			safe_unpack16 (&cores,       buffer);
			safe_unpack16 (&threads,     buffer);
			safe_unpack32 (&real_memory, buffer);
			safe_unpack32 (&tmp_disk,    buffer);
			safe_unpack32 (&reason_uid,  buffer);
			safe_unpack_time (&reason_time, buffer);
			safe_unpack16 (&obj_protocol_version, buffer);
			if (gres_plugin_node_state_unpack(
				    &gres_list, buffer, node_name,
				    protocol_version) != SLURM_SUCCESS)
				goto unpack_error;
			base_state2 = node_state2 & NODE_STATE_BASE;
			/* First decode the quantities as 16 bit
			 * then assign to 32 bit.
			 */
			node_state = node_state2;
			base_state = base_state2;
		} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
			safe_unpackstr_xmalloc (&comm_name, &name_len, buffer);
			safe_unpackstr_xmalloc (&node_name, &name_len, buffer);
			safe_unpackstr_xmalloc (&node_hostname,
							    &name_len, buffer);
			safe_unpackstr_xmalloc (&reason,    &name_len, buffer);
			safe_unpackstr_xmalloc (&features,  &name_len, buffer);
			safe_unpackstr_xmalloc (&gres,      &name_len, buffer);
			safe_unpack16 (&node_state2,  buffer);
			safe_unpack16 (&cpus,        buffer);
			safe_unpack16 (&boards,     buffer);
			safe_unpack16 (&sockets,     buffer);
			safe_unpack16 (&cores,       buffer);
			safe_unpack16 (&threads,     buffer);
			safe_unpack32 (&real_memory, buffer);
			safe_unpack32 (&tmp_disk,    buffer);
			safe_unpack32 (&reason_uid,  buffer);
			safe_unpack_time (&reason_time, buffer);
			if (gres_plugin_node_state_unpack(
				    &gres_list, buffer, node_name,
				    protocol_version) != SLURM_SUCCESS)
				goto unpack_error;
			base_state2 = node_state2 & NODE_STATE_BASE;
			/* First decode the quantities as 16 bit
			 * then assign to 32 bit.
			 */
			node_state = node_state2;
			base_state = base_state2;
		} else {
			error("load_all_node_state: protocol_version "
			      "%hu not supported", protocol_version);
			goto unpack_error;
		}

		/* validity test as possible */
		if ((cpus == 0) ||
		    (boards == 0) ||
		    (sockets == 0) ||
		    (cores == 0) ||
		    (threads == 0) ||
		    (base_state  >= NODE_STATE_END)) {
			error ("Invalid data for node %s: procs=%u, boards=%u,"
			       " sockets=%u, cores=%u, threads=%u, state=%u",
				node_name, cpus, boards,
				sockets, cores, threads, node_state);
			error ("No more node data will be processed from the "
				"checkpoint file");
			goto unpack_error;

		}

		/* find record and perform update */
		node_ptr = find_node_record (node_name);
		if (node_ptr == NULL) {
			error ("Node %s has vanished from configuration",
			       node_name);
		} else if (state_only) {
			uint32_t orig_flags;
			if (IS_NODE_CLOUD(node_ptr)) {
				if ((!power_save_mode) &&
				    ((node_state & NODE_STATE_POWER_SAVE) ||
	 			     (node_state & NODE_STATE_POWER_UP))) {
					node_state &= (~NODE_STATE_POWER_SAVE);
					node_state &= (~NODE_STATE_POWER_UP);
					if (hs)
						hostset_insert(hs, node_name);
					else
						hs = hostset_create(node_name);
				}
				if (comm_name && node_hostname) {
					/* Recover NodeAddr and NodeHostName */
					xfree(node_ptr->comm_name);
					node_ptr->comm_name = comm_name;
					comm_name = NULL;  /* Nothing to free */
					xfree(node_ptr->node_hostname);
					node_ptr->node_hostname = node_hostname;
					node_hostname = NULL;  /* Nothing to free */
					slurm_reset_alias(node_ptr->name,
							  node_ptr->comm_name,
							  node_ptr->node_hostname);
				}
				node_ptr->node_state    = node_state;
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
				if (node_state & NODE_STATE_POWER_SAVE) {
					if (power_save_mode &&
					    IS_NODE_UNKNOWN(node_ptr)) {
						orig_flags = node_ptr->
							node_state &
							     NODE_STATE_FLAGS;
						node_ptr->node_state =
							NODE_STATE_IDLE |
							orig_flags |
							NODE_STATE_POWER_SAVE;
					} else if (power_save_mode) {
						node_ptr->node_state |=
							NODE_STATE_POWER_SAVE;
					} else if (hs)
						hostset_insert(hs, node_name);
					else
						hs = hostset_create(node_name);
					/* Recover hardware state for powered
					 * down nodes */
					node_ptr->cpus          = cpus;
					node_ptr->boards        = boards;
					node_ptr->sockets       = sockets;
					node_ptr->cores         = cores;
					node_ptr->core_spec_cnt =
						core_spec_cnt;
					xfree(node_ptr->cpu_spec_list);
					node_ptr->cpu_spec_list =
						cpu_spec_list;
					cpu_spec_list = NULL;/* Nothing */
							     /*to free */
					node_ptr->threads       = threads;
					node_ptr->real_memory   = real_memory;
					node_ptr->mem_spec_limit =
						mem_spec_limit;
					node_ptr->tmp_disk      = tmp_disk;
				}
				if (node_state & NODE_STATE_MAINT)
					node_ptr->node_state |= NODE_STATE_MAINT;
				if (node_state & NODE_STATE_POWER_UP) {
					if (power_save_mode) {
						node_ptr->node_state |=
							NODE_STATE_POWER_UP;
					} else if (hs)
						hostset_insert(hs, node_name);
					else
						hs = hostset_create(node_name);
				}
			}
			if (node_ptr->reason == NULL) {
				node_ptr->reason = reason;
				reason = NULL;	/* Nothing to free */
				node_ptr->reason_time = reason_time;
				node_ptr->reason_uid = reason_uid;
			}
			node_ptr->gres_list	= gres_list;
			gres_list		= NULL;	/* Nothing to free */
		} else {
			if ((!power_save_mode) &&
			    ((node_state & NODE_STATE_POWER_SAVE) ||
 			     (node_state & NODE_STATE_POWER_UP))) {
				node_state &= (~NODE_STATE_POWER_SAVE);
				node_state &= (~NODE_STATE_POWER_UP);
				if (hs)
					hostset_insert(hs, node_name);
				else
					hs = hostset_create(node_name);
			}
			if (IS_NODE_CLOUD(node_ptr) &&
			    comm_name && node_hostname) {
				/* Recover NodeAddr and NodeHostName */
				xfree(node_ptr->comm_name);
				node_ptr->comm_name = comm_name;
				comm_name = NULL;	/* Nothing to free */
				xfree(node_ptr->node_hostname);
				node_ptr->node_hostname = node_hostname;
				node_hostname = NULL;	/* Nothing to free */
				slurm_reset_alias(node_ptr->name,
						  node_ptr->comm_name,
						  node_ptr->node_hostname);
			}
			node_ptr->node_state    = node_state;
			xfree(node_ptr->reason);
			node_ptr->reason	= reason;
			reason			= NULL;	/* Nothing to free */
			node_ptr->reason_time	= reason_time;
			node_ptr->reason_uid	= reason_uid;
			xfree(node_ptr->features);
			node_ptr->features	= features;
			features		= NULL;	/* Nothing to free */
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
			node_ptr->cpus          = cpus;
			node_ptr->sockets       = sockets;
			node_ptr->cores         = cores;
			node_ptr->core_spec_cnt = core_spec_cnt;
			node_ptr->threads       = threads;
			node_ptr->real_memory   = real_memory;
			node_ptr->mem_spec_limit = mem_spec_limit;
			node_ptr->tmp_disk      = tmp_disk;
			node_ptr->last_response = (time_t) 0;
		}

		if (node_ptr) {
			node_cnt++;
			if (obj_protocol_version != (uint16_t)NO_VAL)
				node_ptr->protocol_version =
					obj_protocol_version;
			else
				node_ptr->protocol_version = protocol_version;
			if (!IS_NODE_POWER_SAVE(node_ptr))
				node_ptr->last_idle = now;
			select_g_update_node_state(node_ptr);
		}

		xfree(features);
		xfree(gres);
		if (gres_list) {
			list_destroy(gres_list);
			gres_list = NULL;
		}
		xfree (comm_name);
		xfree (node_hostname);
		xfree (node_name);
		xfree(reason);
	}

fini:	info("Recovered state of %d nodes", node_cnt);
	if (hs) {
		char node_names[128];
		hostset_ranged_string(hs, sizeof(node_names), node_names);
		info("Cleared POWER_SAVE flag from nodes %s", node_names);
		hostset_destroy(hs);
	}
	free_buf (buffer);
	return error_code;

unpack_error:
	error("Incomplete node data checkpoint file");
	error_code = EFAULT;
	xfree(features);
	xfree(gres);
	if (gres_list) {
		list_destroy(gres_list);
		gres_list = NULL;
	}
	xfree(comm_name);
	xfree(node_hostname);
	xfree(node_name);
	xfree(reason);
	goto fini;
}


/* list_compare_config - compare two entry from the config list based upon
 *	weight, see common/list.h for documentation */
int list_compare_config (void *config_entry1, void *config_entry2)
{
	int weight1, weight2;
	struct config_record *c1;
	struct config_record *c2;

	c1 = *(struct config_record **)config_entry1;
	c2 = *(struct config_record **)config_entry2;

	weight1 = c1->weight;
	weight2 = c2->weight;

	return (weight1 - weight2);
}

/* Return TRUE if the node should be hidden by virtue of being powered down
 * and in the cloud. */
static bool _is_cloud_hidden(struct node_record *node_ptr)
{
	if (((slurmctld_conf.private_data & PRIVATE_CLOUD_NODES) == 0) &&
	    IS_NODE_CLOUD(node_ptr) && IS_NODE_POWER_SAVE(node_ptr))
		return true;
	return false;
}

static bool _node_is_hidden(struct node_record *node_ptr)
{
	int i;
	bool shown = false;

	for (i=0; i<node_ptr->part_cnt; i++) {
		if (!(node_ptr->part_pptr[i]->flags & PART_FLAG_HIDDEN)) {
			shown = true;
			break;
		}
	}

	if (shown || (node_ptr->part_cnt == 0))
		return false;
	return true;
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
 * NOTE: READ lock_slurmctld config before entry
 */
extern void pack_all_node (char **buffer_ptr, int *buffer_size,
			   uint16_t show_flags, uid_t uid,
			   uint16_t protocol_version)
{
	int inx;
	uint32_t nodes_packed, tmp_offset, node_scaling;
	Buf buffer;
	time_t now = time(NULL);
	struct node_record *node_ptr = node_record_table_ptr;
	bool hidden;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	buffer = init_buf (BUF_SIZE*16);
	nodes_packed = 0;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		/* write header: count and time */
		pack32(nodes_packed, buffer);
		select_g_alter_node_cnt(SELECT_GET_NODE_SCALING,
					&node_scaling);
		pack32(node_scaling, buffer);

		pack_time(now, buffer);

		/* write node records */
		part_filter_set(uid);
		for (inx = 0; inx < node_record_count; inx++, node_ptr++) {
			xassert (node_ptr->magic == NODE_MAGIC);
			xassert (node_ptr->config_ptr->magic ==
				 CONFIG_MAGIC);

			/* We can't avoid packing node records without breaking
			 * the node index pointers. So pack a node
			 * with a name of NULL and let the caller deal
			 * with it. */
			hidden = false;
			if (((show_flags & SHOW_ALL) == 0) && (uid != 0) &&
			    (_node_is_hidden(node_ptr)))
				hidden = true;
			else if (IS_NODE_FUTURE(node_ptr))
				hidden = true;
			else if (_is_cloud_hidden(node_ptr))
				hidden = true;
			else if ((node_ptr->name == NULL) ||
				 (node_ptr->name[0] == '\0'))
				hidden = true;

			if (hidden) {
				char *orig_name = node_ptr->name;
				node_ptr->name = NULL;
				_pack_node(node_ptr, buffer, protocol_version,
				           show_flags);
				node_ptr->name = orig_name;
			} else {
				_pack_node(node_ptr, buffer, protocol_version,
					   show_flags);
			}
			nodes_packed++;
		}
		part_filter_clear();
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
 * NOTE: READ lock_slurmctld config before entry
 */
extern void pack_one_node (char **buffer_ptr, int *buffer_size,
			   uint16_t show_flags, uid_t uid, char *node_name,
			   uint16_t protocol_version)
{
	uint32_t nodes_packed, tmp_offset, node_scaling;
	Buf buffer;
	time_t now = time(NULL);
	struct node_record *node_ptr;
	bool hidden;

	buffer_ptr[0] = NULL;
	*buffer_size = 0;

	buffer = init_buf (BUF_SIZE);
	nodes_packed = 0;

	if (protocol_version >= SLURM_MIN_PROTOCOL_VERSION) {
		/* write header: count and time */
		pack32(nodes_packed, buffer);
		select_g_alter_node_cnt(SELECT_GET_NODE_SCALING,
					&node_scaling);
		pack32(node_scaling, buffer);

		pack_time(now, buffer);

		/* write node records */
		part_filter_set(uid);
		if (node_name)
			node_ptr = find_node_record(node_name);
		else
			node_ptr = node_record_table_ptr;
		if (node_ptr) {
			hidden = false;
			if (((show_flags & SHOW_ALL) == 0) && (uid != 0) &&
			    (_node_is_hidden(node_ptr)))
				hidden = true;
			else if (IS_NODE_FUTURE(node_ptr))
				hidden = true;
			else if (_is_cloud_hidden(node_ptr))
				hidden = false;
			else if ((node_ptr->name == NULL) ||
				 (node_ptr->name[0] == '\0'))
				hidden = true;

			if (!hidden) {
				_pack_node(node_ptr, buffer, protocol_version,
					   show_flags);
				nodes_packed++;
			}
		}
		part_filter_clear();
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
}

/*
 * _pack_node - dump all configuration information about a specific node in
 *	machine independent form (for network transmission)
 * IN dump_node_ptr - pointer to node for which information is requested
 * IN/OUT buffer - buffer where data is placed, pointers automatically updated
 * IN protocol_version - slurm protocol version of client
 * IN show_flags -
 * NOTE: if you make any changes here be sure to make the corresponding
 *	changes to load_node_config in api/node_info.c
 * NOTE: READ lock_slurmctld config before entry
 */
static void _pack_node (struct node_record *dump_node_ptr, Buf buffer,
			uint16_t protocol_version, uint16_t show_flags)
{
	char *gres_drain = NULL, *gres_used = NULL;

	if (protocol_version >= SLURM_14_11_PROTOCOL_VERSION) {
		packstr (dump_node_ptr->name, buffer);
		packstr (dump_node_ptr->node_hostname, buffer);
		packstr (dump_node_ptr->comm_name, buffer);
		pack32(dump_node_ptr->node_state, buffer);
		packstr (dump_node_ptr->version, buffer);
		/* On a bluegene system always use the regular node
		* infomation not what is in the config_ptr. */
#ifndef HAVE_BG
		if (slurmctld_conf.fast_schedule) {
			/* Only data from config_record used for scheduling */
			pack16(dump_node_ptr->config_ptr->cpus, buffer);
			pack16(dump_node_ptr->config_ptr->boards, buffer);
			pack16(dump_node_ptr->config_ptr->sockets, buffer);
			pack16(dump_node_ptr->config_ptr->cores, buffer);
			pack16(dump_node_ptr->config_ptr->threads, buffer);
			pack32(dump_node_ptr->config_ptr->real_memory, buffer);
			pack32(dump_node_ptr->config_ptr->tmp_disk, buffer);
		} else {
#endif
			/* Individual node data used for scheduling */
			pack16(dump_node_ptr->cpus, buffer);
			pack16(dump_node_ptr->boards, buffer);
			pack16(dump_node_ptr->sockets, buffer);
			pack16(dump_node_ptr->cores, buffer);
			pack16(dump_node_ptr->threads, buffer);
			pack32(dump_node_ptr->real_memory, buffer);
			pack32(dump_node_ptr->tmp_disk, buffer);
#ifndef HAVE_BG
		}
#endif
		pack16(dump_node_ptr->core_spec_cnt, buffer);
		pack32(dump_node_ptr->mem_spec_limit, buffer);
		packstr(dump_node_ptr->cpu_spec_list, buffer);

		pack32(dump_node_ptr->cpu_load, buffer);
		pack32(dump_node_ptr->config_ptr->weight, buffer);
		pack32(dump_node_ptr->reason_uid, buffer);

		pack_time(dump_node_ptr->boot_time, buffer);
		pack_time(dump_node_ptr->reason_time, buffer);
		pack_time(dump_node_ptr->slurmd_start_time, buffer);

		select_g_select_nodeinfo_pack(dump_node_ptr->select_nodeinfo,
					      buffer, protocol_version);

		packstr(dump_node_ptr->arch, buffer);
		packstr(dump_node_ptr->features, buffer);
		if (dump_node_ptr->gres)
			packstr(dump_node_ptr->gres, buffer);
		else
			packstr(dump_node_ptr->config_ptr->gres, buffer);

		/* Gathering GRES deails is slow, so don't by default */
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
		packstr(dump_node_ptr->reason, buffer);
		acct_gather_energy_pack(dump_node_ptr->energy, buffer,
					protocol_version);
		ext_sensors_data_pack(dump_node_ptr->ext_sensors, buffer,
					protocol_version);
	} else if (protocol_version >= SLURM_14_03_PROTOCOL_VERSION) {
		packstr (dump_node_ptr->name, buffer);
		packstr (dump_node_ptr->node_hostname, buffer);
		packstr (dump_node_ptr->comm_name, buffer);
		pack16  (dump_node_ptr->node_state, buffer);
		packstr (dump_node_ptr->version, buffer);
		/* On a bluegene system always use the regular node
		* infomation not what is in the config_ptr.
		*/
#ifndef HAVE_BG
		if (slurmctld_conf.fast_schedule) {
			/* Only data from config_record used for scheduling */
			pack16(dump_node_ptr->config_ptr->cpus, buffer);
			pack16(dump_node_ptr->config_ptr->boards, buffer);
			pack16(dump_node_ptr->config_ptr->sockets, buffer);
			pack16(dump_node_ptr->config_ptr->cores, buffer);
			pack16(dump_node_ptr->config_ptr->threads, buffer);
			pack32(dump_node_ptr->config_ptr->real_memory, buffer);
			pack32(dump_node_ptr->config_ptr->tmp_disk, buffer);
		} else {
#endif
			/* Individual node data used for scheduling */
			pack16(dump_node_ptr->cpus, buffer);
			pack16(dump_node_ptr->boards, buffer);
			pack16(dump_node_ptr->sockets, buffer);
			pack16(dump_node_ptr->cores, buffer);
			pack16(dump_node_ptr->threads, buffer);
			pack32(dump_node_ptr->real_memory, buffer);
			pack32(dump_node_ptr->tmp_disk, buffer);
#ifndef HAVE_BG
		}
#endif
		pack32(dump_node_ptr->cpu_load, buffer);
		pack32(dump_node_ptr->config_ptr->weight, buffer);
		pack32(dump_node_ptr->reason_uid, buffer);

		pack_time(dump_node_ptr->boot_time, buffer);
		pack_time(dump_node_ptr->reason_time, buffer);
		pack_time(dump_node_ptr->slurmd_start_time, buffer);

		select_g_select_nodeinfo_pack(dump_node_ptr->select_nodeinfo,
					      buffer, protocol_version);

		packstr(dump_node_ptr->arch, buffer);
		packstr(dump_node_ptr->features, buffer);
		if (dump_node_ptr->gres)
			packstr(dump_node_ptr->gres, buffer);
		else
			packstr(dump_node_ptr->config_ptr->gres, buffer);
		packstr(dump_node_ptr->os, buffer);
		packstr(dump_node_ptr->reason, buffer);
		acct_gather_energy_pack(dump_node_ptr->energy, buffer,
					protocol_version);
		ext_sensors_data_pack(dump_node_ptr->ext_sensors, buffer,
					protocol_version);
	} else if (protocol_version >= SLURM_2_6_PROTOCOL_VERSION) {
		packstr (dump_node_ptr->name, buffer);
		packstr (dump_node_ptr->node_hostname, buffer);
		packstr (dump_node_ptr->comm_name, buffer);
		pack16  (dump_node_ptr->node_state, buffer);
		/* On a bluegene system always use the regular node
		* infomation not what is in the config_ptr.
		*/
#ifndef HAVE_BG
		if (slurmctld_conf.fast_schedule) {
			/* Only data from config_record used for scheduling */
			pack16(dump_node_ptr->config_ptr->cpus, buffer);
			pack16(dump_node_ptr->config_ptr->boards, buffer);
			pack16(dump_node_ptr->config_ptr->sockets, buffer);
			pack16(dump_node_ptr->config_ptr->cores, buffer);
			pack16(dump_node_ptr->config_ptr->threads, buffer);
			pack32(dump_node_ptr->config_ptr->real_memory, buffer);
			pack32(dump_node_ptr->config_ptr->tmp_disk, buffer);
		} else {
#endif
			/* Individual node data used for scheduling */
			pack16(dump_node_ptr->cpus, buffer);
			pack16(dump_node_ptr->boards, buffer);
			pack16(dump_node_ptr->sockets, buffer);
			pack16(dump_node_ptr->cores, buffer);
			pack16(dump_node_ptr->threads, buffer);
			pack32(dump_node_ptr->real_memory, buffer);
			pack32(dump_node_ptr->tmp_disk, buffer);
#ifndef HAVE_BG
		}
#endif
		pack32(dump_node_ptr->cpu_load, buffer);
		pack32(dump_node_ptr->config_ptr->weight, buffer);
		pack32(dump_node_ptr->reason_uid, buffer);

		pack_time(dump_node_ptr->boot_time, buffer);
		pack_time(dump_node_ptr->reason_time, buffer);
		pack_time(dump_node_ptr->slurmd_start_time, buffer);

		select_g_select_nodeinfo_pack(dump_node_ptr->select_nodeinfo,
					      buffer, protocol_version);

		packstr(dump_node_ptr->arch, buffer);
		packstr(dump_node_ptr->features, buffer);
		if (dump_node_ptr->gres)
			packstr(dump_node_ptr->gres, buffer);
		else
			packstr(dump_node_ptr->config_ptr->gres, buffer);
		packstr(dump_node_ptr->os, buffer);
		packstr(dump_node_ptr->reason, buffer);
		acct_gather_energy_pack(dump_node_ptr->energy, buffer,
					protocol_version);
		ext_sensors_data_pack(dump_node_ptr->ext_sensors, buffer,
					protocol_version);
	} else {
		error("_pack_node: protocol_version "
		      "%hu not supported", protocol_version);
	}
}


/*
 * set_slurmd_addr - establish the slurm_addr_t for the slurmd on each node
 *	Uses common data structures.
 * NOTE: READ lock_slurmctld config before entry
 */
void set_slurmd_addr (void)
{
#ifndef HAVE_FRONT_END
	int i;
	struct node_record *node_ptr = node_record_table_ptr;
	DEF_TIMERS;

	START_TIMER;
	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if ((node_ptr->name == NULL) ||
		    (node_ptr->name[0] == '\0'))
			continue;
		if (IS_NODE_FUTURE(node_ptr))
			continue;
		if (IS_NODE_CLOUD(node_ptr) && IS_NODE_POWER_SAVE(node_ptr))
			continue;
		if (node_ptr->port == 0)
			node_ptr->port = slurmctld_conf.slurmd_port;
		slurm_set_addr(&node_ptr->slurm_addr, node_ptr->port,
			       node_ptr->comm_name);
		if (node_ptr->slurm_addr.sin_port)
			continue;
		error("slurm_set_addr failure on %s", node_ptr->comm_name);
		node_ptr->node_state = NODE_STATE_FUTURE;
		node_ptr->port = 0;
		xfree(node_ptr->reason);
		node_ptr->reason = xstrdup("NO NETWORK ADDRESS FOUND");
		node_ptr->reason_time = time(NULL);
		node_ptr->reason_uid = getuid();
	}

	END_TIMER2("set_slurmd_addr");
#endif
}

/* Return "true" if a node's state is already "new_state". This is more
 * complex than simply comparing the state values due to flags (e.g.
 * A node might be DOWN + NO_RESPOND or IDLE + DRAIN) */
static bool
_equivalent_node_state(struct node_record *node_ptr, uint32_t new_state)
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

/*
 * update_node - update the configuration data for one or more nodes
 * IN update_node_msg - update node request
 * RET SLURM_SUCCESS or error code
 * global: node_record_table_ptr - pointer to global node table
 */
int update_node ( update_node_msg_t * update_node_msg )
{
	int error_code = 0, node_cnt, node_inx;
	struct node_record *node_ptr = NULL;
	char  *this_node_name = NULL;
	hostlist_t host_list, hostaddr_list = NULL, hostname_list = NULL;
	uint32_t base_state = 0, node_flags, state_val;
	time_t now = time(NULL);

	if (update_node_msg->node_names == NULL ) {
		info("update_node: invalid node name  %s",
		       update_node_msg -> node_names );
		return ESLURM_INVALID_NODE_NAME;
	}

	host_list = hostlist_create(update_node_msg->node_names);
	if (host_list == NULL) {
		info("update_node: hostlist_create error on %s: %m",
		      update_node_msg->node_names);
		return ESLURM_INVALID_NODE_NAME;
	}
	node_cnt = hostlist_count(host_list);

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

		node_ptr = find_node_record (this_node_name);
		node_inx = node_ptr - node_record_table_ptr;
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

		if (update_node_msg->features) {
			xfree(node_ptr->features);
			if (update_node_msg->features[0])
				node_ptr->features = xstrdup(update_node_msg->
							     features);
			/* _update_node_features() logs and updates config */
		}

		if (update_node_msg->gres) {
			xfree(node_ptr->gres);
			if (update_node_msg->gres[0])
				node_ptr->gres = xstrdup(update_node_msg->gres);
			/* _update_node_gres() logs and updates config */
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
			node_ptr->reason_uid = update_node_msg->reason_uid;
			info ("update_node: node %s reason set to: %s",
				this_node_name, node_ptr->reason);
		}

		if (state_val != (uint32_t) NO_VAL) {
			base_state = node_ptr->node_state;
			if (!_valid_node_state_change(base_state, state_val)) {
				info("Invalid node state transition requested "
				     "for node %s from=%s to=%s",
				     this_node_name,
				     node_state_string(base_state),
				     node_state_string(state_val));
				state_val = (uint32_t) NO_VAL;
				error_code = ESLURM_INVALID_NODE_STATE;
			}
			base_state &= NODE_STATE_BASE;
		}

		if (state_val != (uint32_t) NO_VAL) {
			node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
			if (state_val == NODE_RESUME) {
				if (IS_NODE_IDLE(node_ptr) &&
				    (IS_NODE_DRAIN(node_ptr) ||
				     IS_NODE_FAIL(node_ptr))) {
					clusteracct_storage_g_node_up(
						acct_db_conn,
						node_ptr,
						now);
				}
				node_ptr->node_state &= (~NODE_STATE_DRAIN);
				node_ptr->node_state &= (~NODE_STATE_FAIL);
				node_ptr->node_state &= (~NODE_STATE_MAINT);
				if (IS_NODE_DOWN(node_ptr)) {
					state_val = NODE_STATE_IDLE;
#ifndef HAVE_FRONT_END
					node_ptr->node_state |=
							NODE_STATE_NO_RESPOND;
#endif
					node_ptr->last_response = now;
					ping_nodes_now = true;
				} else if (IS_NODE_FUTURE(node_ptr)) {
					if (node_ptr->port == 0) {
						node_ptr->port =slurmctld_conf.
								slurmd_port;
					}
					slurm_set_addr(	&node_ptr->slurm_addr,
							node_ptr->port,
							node_ptr->comm_name);
					if (node_ptr->slurm_addr.sin_port) {
						state_val = NODE_STATE_IDLE;
#ifndef HAVE_FRONT_END
						node_ptr->node_state |=
							NODE_STATE_NO_RESPOND;
#endif
						node_ptr->last_response = now;
						ping_nodes_now = true;
					} else {
						error("slurm_set_addr failure "
						      "on %s",
		       				      node_ptr->comm_name);
						state_val = base_state;
					}
				} else
					state_val = base_state;
			} else if (state_val == NODE_STATE_UNDRAIN) {
				if (IS_NODE_IDLE(node_ptr) &&
				    IS_NODE_DRAIN(node_ptr)) {
					clusteracct_storage_g_node_up(
						acct_db_conn,
						node_ptr,
						now);
				}
				node_ptr->node_state &= (~NODE_STATE_DRAIN);
				state_val = base_state;
			}

			if ((state_val == NODE_STATE_DOWN) ||
			    (state_val == NODE_STATE_FUTURE)) {
				/* We must set node DOWN before killing
				 * its jobs */
				_make_node_down(node_ptr, now);
				kill_running_job_by_node_name (this_node_name);
				if (state_val == NODE_STATE_FUTURE) {
					node_ptr->node_state = NODE_STATE_FUTURE
							       | node_flags;
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
				} else if (IS_NODE_IDLE(node_ptr)   &&
					   (IS_NODE_DRAIN(node_ptr) ||
					    IS_NODE_FAIL(node_ptr))) {
					clusteracct_storage_g_node_up(
						acct_db_conn,
						node_ptr,
						now);
				}	/* else already fully available */
				node_ptr->node_state &= (~NODE_STATE_DRAIN);
				node_ptr->node_state &= (~NODE_STATE_FAIL);
				if (!IS_NODE_NO_RESPOND(node_ptr))
					bit_set (avail_node_bitmap, node_inx);
				bit_set (idle_node_bitmap, node_inx);
				bit_set (up_node_bitmap, node_inx);
				if (IS_NODE_POWER_SAVE(node_ptr))
					node_ptr->last_idle = 0;
				else
					node_ptr->last_idle = now;
			} else if (state_val == NODE_STATE_ALLOCATED) {
				if (!IS_NODE_DRAIN(node_ptr) &&
				    !IS_NODE_FAIL(node_ptr)  &&
				    !IS_NODE_NO_RESPOND(node_ptr))
					bit_set(avail_node_bitmap, node_inx);
				bit_set (up_node_bitmap, node_inx);
				bit_clear (idle_node_bitmap, node_inx);
			} else if ((state_val == NODE_STATE_DRAIN) ||
				   (state_val == NODE_STATE_FAIL)) {
				uint32_t new_state = state_val;
				bit_clear (avail_node_bitmap, node_inx);
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
			} else if (state_val == NODE_STATE_POWER_SAVE) {
				if (IS_NODE_POWER_SAVE(node_ptr)) {
					node_ptr->last_idle = 0;
					node_ptr->node_state &=
						(~NODE_STATE_POWER_SAVE);
					info("power down request repeating "
					     "for node %s", this_node_name);
				} else {
					if (IS_NODE_DOWN(node_ptr) &&
					    IS_NODE_POWER_UP(node_ptr)) {
						/* Abort power up request */
						node_ptr->node_state &=
							(~NODE_STATE_POWER_UP);
#ifndef HAVE_FRONT_END
						node_ptr->node_state |=
							NODE_STATE_NO_RESPOND;
#endif
						node_ptr->node_state =
							NODE_STATE_IDLE |
							(node_ptr->node_state &
							 NODE_STATE_FLAGS);
					}
					node_ptr->last_idle = 0;
					info("powering down node %s",
					     this_node_name);
				}
				free(this_node_name);
				continue;
			} else if (state_val == NODE_STATE_POWER_UP) {
				if (!IS_NODE_POWER_SAVE(node_ptr)) {
					if (IS_NODE_POWER_UP(node_ptr)) {
						node_ptr->last_idle = now;
						node_ptr->node_state |=
							NODE_STATE_POWER_SAVE;
						info("power up request "
						     "repeating for node %s",
						     this_node_name);
					} else {
						verbose("node %s is already "
							"powered up",
							this_node_name);
					}
				} else {
					node_ptr->last_idle = now;
					info("powering up node %s",
					     this_node_name);
				}
				free(this_node_name);
				continue;
			} else if (state_val == NODE_STATE_NO_RESPOND) {
				node_ptr->node_state |= NODE_STATE_NO_RESPOND;
				state_val = base_state;
				bit_clear(avail_node_bitmap, node_inx);
			} else {
				info ("Invalid node state specified %u",
					state_val);
				err_code = 1;
				error_code = ESLURM_INVALID_NODE_STATE;
			}

			if (err_code == 0) {
				node_ptr->node_state = state_val |
						(node_ptr->node_state &
						 NODE_STATE_FLAGS);
				select_g_update_node_state(node_ptr);

				info ("update_node: node %s state set to %s",
					this_node_name,
					node_state_string(state_val));
			}
		}

		if (!IS_NODE_DOWN(node_ptr) &&
		    !IS_NODE_DRAIN(node_ptr) && !IS_NODE_FAIL(node_ptr)) {
			/* reason information is handled in
			   clusteracct_storage_g_node_up()
			*/
			clusteracct_storage_g_node_up(
				acct_db_conn, node_ptr, now);
		}

		free (this_node_name);
	}
	FREE_NULL_HOSTLIST(host_list);
	FREE_NULL_HOSTLIST(hostaddr_list);
	FREE_NULL_HOSTLIST(hostname_list);
	last_node_update = now;

	if ((error_code == 0) && (update_node_msg->features)) {
		error_code = _update_node_features(update_node_msg->node_names,
						   update_node_msg->features);
	}
	if ((error_code == 0) && (update_node_msg->gres)) {
		error_code = _update_node_gres(update_node_msg->node_names,
					       update_node_msg->gres);
	}

	/* Update weight. Weight is part of config_ptr,
	 * hence split config records if required */
	if ((error_code == 0) && (update_node_msg->weight != NO_VAL))	{
		error_code = _update_node_weight(update_node_msg->node_names,
						 update_node_msg->weight);
		if (!error_code)
			/* sort config_list by weight for scheduling */
			list_sort(config_list, &list_compare_config);

	}

	return error_code;
}

/* variation of strcmp that accepts NULL pointers */
static int _strcmp(char *str1, char *str2)
{
	if (!str1 && !str2)
		return 0;
	if (str1 && !str2)
		return 1;
	if (!str1 && str2)
		return -1;
	return strcmp(str1, str2);
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
	int i;
	struct node_record *node_ptr;

	for (i=0, node_ptr=node_record_table_ptr; i<node_record_count;
	     i++, node_ptr++) {

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

		if (_strcmp(node_ptr->config_ptr->feature, node_ptr->features)){
			error("Node %s Features(%s) differ from slurm.conf",
			      node_ptr->name, node_ptr->features);
			if (recover == 2) {
				_update_node_features(node_ptr->name,
						      node_ptr->features);
			} else {
				xfree(node_ptr->features);
				node_ptr->features = xstrdup(node_ptr->
							     config_ptr->
							     feature);
			}
		}

		/* We lose the gres information updated manually and always
		 * use the information from slurm.conf */
		(void) gres_plugin_node_reconfig(node_ptr->name,
						 node_ptr->config_ptr->gres,
						 &node_ptr->gres,
						 &node_ptr->gres_list,
						 slurmctld_conf.fast_schedule);
		gres_plugin_node_state_log(node_ptr->gres_list, node_ptr->name);
	}
}

/* Duplicate a configuration record except for the node names & bitmap */
struct config_record * _dup_config(struct config_record *config_ptr)
{
	struct config_record *new_config_ptr;

	new_config_ptr = create_config_record();
	new_config_ptr->magic       = config_ptr->magic;
	new_config_ptr->cpus        = config_ptr->cpus;
	new_config_ptr->cpu_spec_list = xstrdup(config_ptr->cpu_spec_list);
	new_config_ptr->boards      = config_ptr->boards;
	new_config_ptr->sockets     = config_ptr->sockets;
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
	struct config_record *config_ptr, *new_config_ptr;
	struct config_record *first_new = NULL;
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
	while ((config_ptr = (struct config_record *)
			list_next(config_iterator))) {
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
			if (first_new == NULL);
				first_new = new_config_ptr;
			/* Change weight for the given node */
			new_config_ptr->weight      = weight;
			new_config_ptr->node_bitmap = bit_copy(tmp_bitmap);
			new_config_ptr->nodes = bitmap2node_name(tmp_bitmap);

			build_config_feature_list(new_config_ptr);
			_update_config_ptr(tmp_bitmap, new_config_ptr);

			/* Update remaining records */
			bit_not(tmp_bitmap);
			bit_and(config_ptr->node_bitmap, tmp_bitmap);
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

/*
 * _update_node_features - Update features associated with nodes
 *	build new config list records as needed
 * IN node_names - List of nodes to update
 * IN features - New features value
 * RET: SLURM_SUCCESS or error code
 */
static int _update_node_features(char *node_names, char *features)
{
	bitstr_t *node_bitmap = NULL, *tmp_bitmap;
	ListIterator config_iterator;
	struct config_record *config_ptr, *new_config_ptr;
	struct config_record *first_new = NULL;
	int rc, config_cnt, tmp_cnt;

	rc = node_name2bitmap(node_names, false, &node_bitmap);
	if (rc) {
		info("_update_node_features: invalid node_name");
		return rc;
	}

	/* For each config_record with one of these nodes,
	 * update it (if all nodes updated) or split it into
	 * a new entry */
	config_iterator = list_iterator_create(config_list);
	while ((config_ptr = (struct config_record *)
			list_next(config_iterator))) {
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
			if (features && features[0])
				config_ptr->feature = xstrdup(features);
			build_config_feature_list(config_ptr);
		} else {
			/* partial update, split config_record */
			new_config_ptr = _dup_config(config_ptr);
			if (first_new == NULL);
				first_new = new_config_ptr;
			xfree(new_config_ptr->feature);
			if (features && features[0])
				new_config_ptr->feature = xstrdup(features);
			new_config_ptr->node_bitmap = bit_copy(tmp_bitmap);
			new_config_ptr->nodes = bitmap2node_name(tmp_bitmap);

			build_config_feature_list(new_config_ptr);
			_update_config_ptr(tmp_bitmap, new_config_ptr);

			/* Update remaining records */
			bit_not(tmp_bitmap);
			bit_and(config_ptr->node_bitmap, tmp_bitmap);
			xfree(config_ptr->nodes);
			config_ptr->nodes = bitmap2node_name(config_ptr->
							     node_bitmap);
		}
		FREE_NULL_BITMAP(tmp_bitmap);
	}
	list_iterator_destroy(config_iterator);
	FREE_NULL_BITMAP(node_bitmap);

	info("_update_node_features: nodes %s features set to: %s",
		node_names, features);
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
	bitstr_t *node_bitmap = NULL, *tmp_bitmap;
	ListIterator config_iterator;
	struct config_record *config_ptr, *new_config_ptr;
	struct config_record *first_new = NULL;
	struct node_record *node_ptr;
	int rc, config_cnt, tmp_cnt;
	int i, i_first, i_last;

	rc = node_name2bitmap(node_names, false, &node_bitmap);
	if (rc) {
		info("_update_node_gres: invalid node_name");
		return rc;
	}

	/* For each config_record with one of these nodes,
	 * update it (if all nodes updated) or split it into
	 * a new entry */
	config_iterator = list_iterator_create(config_list);
	while ((config_ptr = (struct config_record *)
			list_next(config_iterator))) {
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
			xfree(config_ptr->gres);
			if (gres && gres[0])
				config_ptr->gres = xstrdup(gres);
		} else {
			/* partial update, split config_record */
			new_config_ptr = _dup_config(config_ptr);
			if (first_new == NULL);
				first_new = new_config_ptr;
			xfree(new_config_ptr->gres);
			if (gres && gres[0])
				new_config_ptr->gres = xstrdup(gres);
			new_config_ptr->node_bitmap = bit_copy(tmp_bitmap);
			new_config_ptr->nodes = bitmap2node_name(tmp_bitmap);

			_update_config_ptr(tmp_bitmap, new_config_ptr);

			/* Update remaining records */
			bit_not(tmp_bitmap);
			bit_and(config_ptr->node_bitmap, tmp_bitmap);
			xfree(config_ptr->nodes);
			config_ptr->nodes = bitmap2node_name(config_ptr->
							     node_bitmap);
		}
		FREE_NULL_BITMAP(tmp_bitmap);
	}
	list_iterator_destroy(config_iterator);

	i_first = bit_ffs(node_bitmap);
	i_last  = bit_fls(node_bitmap);
	for (i=i_first; i<=i_last; i++) {
		node_ptr = node_record_table_ptr + i;
		(void) gres_plugin_node_reconfig(node_ptr->name,
						 node_ptr->config_ptr->gres,
						 &node_ptr->gres,
						 &node_ptr->gres_list,
						 slurmctld_conf.fast_schedule);
		gres_plugin_node_state_log(node_ptr->gres_list, node_ptr->name);
	}
	FREE_NULL_BITMAP(node_bitmap);

	info("_update_node_gres: nodes %s gres set to: %s", node_names, gres);
	return SLURM_SUCCESS;
}

/* Reset the config pointer for updated jobs */
static void _update_config_ptr(bitstr_t *bitmap,
		struct config_record *config_ptr)
{
	int i;

	for (i=0; i<node_record_count; i++) {
		if (bit_test(bitmap, i) == 0)
			continue;
		node_record_table_ptr[i].config_ptr = config_ptr;
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
extern int drain_nodes ( char *nodes, char *reason, uint32_t reason_uid )
{
	int error_code = 0, node_inx;
	struct node_record *node_ptr;
	char  *this_node_name ;
	hostlist_t host_list;
	time_t now = time(NULL);

	if ((nodes == NULL) || (nodes[0] == '\0')) {
		error ("drain_nodes: invalid node name  %s", nodes);
		return ESLURM_INVALID_NODE_NAME;
	}

#ifdef HAVE_ALPS_CRAY
	error("We cannot drain nodes on a Cray/ALPS system, "
	      "use native Cray tools such as xtprocadmin(8).");
	return SLURM_SUCCESS;
#endif

	if ( (host_list = hostlist_create (nodes)) == NULL) {
		error ("hostlist_create error on %s: %m", nodes);
		return ESLURM_INVALID_NODE_NAME;
	}

	while ( (this_node_name = hostlist_shift (host_list)) ) {
		node_ptr = find_node_record (this_node_name);
		node_inx = node_ptr - node_record_table_ptr;
		if (node_ptr == NULL) {
			error ("drain_nodes: node %s does not exist",
				this_node_name);
			error_code = ESLURM_INVALID_NODE_NAME;
			free (this_node_name);
			break;
		}

		if (IS_NODE_DRAIN(node_ptr)) {
			/* state already changed, nothing to do */
			free (this_node_name);
			continue;
		}

		node_ptr->node_state |= NODE_STATE_DRAIN;
		bit_clear (avail_node_bitmap, node_inx);
		info ("drain_nodes: node %s state set to DRAIN",
			this_node_name);
		if ((node_ptr->reason == NULL) ||
		    (strncmp(node_ptr->reason, "Not responding", 14) == 0)) {
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

		select_g_update_node_state(node_ptr);

		free (this_node_name);
	}
	last_node_update = time (NULL);

	hostlist_destroy (host_list);
	return error_code;
}
/* Return true if admin request to change node state from old to new is valid */
static bool _valid_node_state_change(uint32_t old, uint32_t new)
{
	uint32_t base_state, node_flags;

	if (old == new)
		return true;

	base_state = old & NODE_STATE_BASE;
	node_flags = old & NODE_STATE_FLAGS;
	switch (new) {
		case NODE_STATE_DOWN:
		case NODE_STATE_DRAIN:
		case NODE_STATE_FAIL:
		case NODE_STATE_NO_RESPOND:
		case NODE_STATE_POWER_SAVE:
		case NODE_STATE_POWER_UP:
		case NODE_STATE_UNDRAIN:
			return true;

		case NODE_RESUME:
			if ((base_state == NODE_STATE_DOWN)   ||
			    (base_state == NODE_STATE_FUTURE) ||
			    (node_flags & NODE_STATE_DRAIN)   ||
			    (node_flags & NODE_STATE_FAIL)    ||
			    (node_flags & NODE_STATE_MAINT))
				return true;
			break;

		case NODE_STATE_FUTURE:
			if ((base_state == NODE_STATE_DOWN) ||
			    (base_state == NODE_STATE_IDLE))
				return true;
			break;

		case NODE_STATE_IDLE:
			if ((base_state == NODE_STATE_DOWN) ||
			    (base_state == NODE_STATE_IDLE))
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

static int _build_node_spec_bitmap(struct node_record *node_ptr)
{
	uint32_t c, coff, size;
	int *cpu_spec_array;
	uint i, node_inx;

	if (node_ptr->threads == 0) {
		error("Node %s has invalid thread per core count (%u)",
		      node_ptr->name, node_ptr->threads);
		return SLURM_ERROR;
	}

	if (!node_ptr->cpu_spec_list)
		return SLURM_SUCCESS;
	node_inx = node_ptr - node_record_table_ptr;
	c = cr_get_coremap_offset(node_inx);
	coff = cr_get_coremap_offset(node_inx+1);
	size = coff - c;
	FREE_NULL_BITMAP(node_ptr->node_spec_bitmap);
	node_ptr->node_spec_bitmap = bit_alloc(size);
	bit_nset(node_ptr->node_spec_bitmap, 0, size-1);

	/* remove node's specialized cpus now */
	cpu_spec_array = bitfmt2int(node_ptr->cpu_spec_list);
	i = 0;
	while (cpu_spec_array[i] != -1) {
		bit_nclear(node_ptr->node_spec_bitmap,
			   (cpu_spec_array[i] / node_ptr->threads),
			   (cpu_spec_array[i + 1] / node_ptr->threads));
		i += 2;
	}
	xfree(cpu_spec_array);
	return SLURM_SUCCESS;
}

extern int update_node_record_acct_gather_data(
	acct_gather_node_resp_msg_t *msg)
{
	struct node_record *node_ptr;

	node_ptr = find_node_record(msg->node_name);
	if (node_ptr == NULL)
		return ENOENT;

	memcpy(node_ptr->energy, msg->energy, sizeof(acct_gather_energy_t));

	return SLURM_SUCCESS;
}

/*
 * validate_node_specs - validate the node's specifications as valid,
 *	if not set state to down, in any case update last_response
 * IN reg_msg - node registration message
 * IN protocol_version - Version of Slurm on this node
 * OUT newly_up - set if node newly brought into service
 * RET 0 if no error, ENOENT if no such node, EINVAL if values too low
 * NOTE: READ lock_slurmctld config before entry
 */
extern int validate_node_specs(slurm_node_registration_status_msg_t *reg_msg,
			       uint16_t protocol_version, bool *newly_up)
{
	int error_code, i, node_inx;
	struct config_record *config_ptr;
	struct node_record *node_ptr;
	char *reason_down = NULL;
	uint32_t node_flags;
	time_t now = time(NULL);
	bool gang_flag = false;
	bool orig_node_avail;
	static uint32_t cr_flag = NO_VAL;
	int *cpu_spec_array;

	node_ptr = find_node_record (reg_msg->node_name);
	if (node_ptr == NULL)
		return ENOENT;
	node_inx = node_ptr - node_record_table_ptr;
	orig_node_avail = bit_test(avail_node_bitmap, node_inx);

	config_ptr = node_ptr->config_ptr;
	error_code = SLURM_SUCCESS;

	node_ptr->protocol_version = protocol_version;
	xfree(node_ptr->version);
	node_ptr->version = reg_msg->version;
	reg_msg->version = NULL;

	if (cr_flag == NO_VAL) {
		cr_flag = 0;  /* call is no-op for select/linear and bluegene */
		if (select_g_get_info_from_plugin(SELECT_CR_PLUGIN,
						  NULL, &cr_flag)) {
			cr_flag = NO_VAL;	/* error */
		}
	}
	if (slurm_get_preempt_mode() != PREEMPT_MODE_OFF)
		gang_flag = true;

	if (gres_plugin_node_config_unpack(reg_msg->gres_info,
					   node_ptr->name) != SLURM_SUCCESS) {
		error_code = SLURM_ERROR;
		xstrcat(reason_down, "Could not unpack gres data");
	} else if (gres_plugin_node_config_validate(
			   node_ptr->name, config_ptr->gres,
			   &node_ptr->gres, &node_ptr->gres_list,
			   slurmctld_conf.fast_schedule, &reason_down)
		   != SLURM_SUCCESS) {
		error_code = EINVAL;
		/* reason_down set in function above */
	}
	gres_plugin_node_state_log(node_ptr->gres_list, node_ptr->name);

	if (slurmctld_conf.fast_schedule != 2) {
		int sockets1, sockets2;	/* total sockets on node */
		int cores1, cores2;	/* total cores on node */
		int threads1, threads2;	/* total threads on node */

		sockets1 = reg_msg->sockets;
		cores1   = sockets1 * reg_msg->cores;
		threads1 = cores1   * reg_msg->threads;
		sockets2 = config_ptr->sockets;
		cores2   = sockets2 * config_ptr->cores;
		threads2 = cores2   * config_ptr->threads;

		if (threads1 < threads2) {
			error("Node %s has low socket*core*thread count "
			      "(%d < %d)",
			      reg_msg->node_name, threads1, threads2);
			error_code = EINVAL;
			if (reason_down)
				xstrcat(reason_down, ", ");
			xstrcat(reason_down, "Low socket*core*thread count");
		} else if ((slurmctld_conf.fast_schedule == 0) &&
			   ((cr_flag == 1) || gang_flag) && (cores1 < cores2)) {
			error("Node %s has low socket*core count (%d < %d)",
			      reg_msg->node_name, cores1, cores2);
			error_code = EINVAL;
			if (reason_down)
				xstrcat(reason_down, ", ");
			xstrcat(reason_down, "Low socket*core count");
		} else if ((slurmctld_conf.fast_schedule == 0) &&
			   ((cr_flag == 1) || gang_flag) &&
			   ((sockets1 > sockets2) || (cores1 > cores2) ||
			    (threads1 > threads2))) {
			error("Node %s has high socket,core,thread count "
			      "(%d,%d,%d > %d,%d,%d), extra resources ignored",
			      reg_msg->node_name, sockets1, cores1, threads1,
			      sockets2, cores2, threads2);
			/* Preserve configured values */
			reg_msg->sockets = config_ptr->sockets;
			reg_msg->cores   = config_ptr->cores;
			reg_msg->threads = config_ptr->threads;
		}

		if (reg_msg->cpus < config_ptr->cpus) {
			error("Node %s has low cpu count (%u < %u)",
			      reg_msg->node_name, reg_msg->cpus,
			      config_ptr->cpus);
			error_code  = EINVAL;
			if (reason_down)
				xstrcat(reason_down, ", ");
			xstrcat(reason_down, "Low CPUs");
		} else if ((slurmctld_conf.fast_schedule == 0) &&
			   ((cr_flag == 1) || gang_flag) &&
			   (reg_msg->cpus > config_ptr->cpus)) {
			error("Node %s has high CPU count (%u > %u), "
			      "extra resources ignored",
			      reg_msg->node_name, reg_msg->cpus,
			      config_ptr->cpus);
			reg_msg->cpus    = config_ptr->cpus;
		}
	}

	/* reset partition and node config (in that order) */
	if ((node_ptr->cpus != reg_msg->cpus) &&
	    (slurmctld_conf.fast_schedule == 0)) {
		for (i=0; i<node_ptr->part_cnt; i++) {
			node_ptr->part_pptr[i]->total_cpus +=
				(reg_msg->cpus - node_ptr->cpus);
		}
	}
	if (error_code == SLURM_SUCCESS) {
		node_ptr->boards  = reg_msg->boards;
		node_ptr->sockets = reg_msg->sockets;
		node_ptr->cores   = reg_msg->cores;
		node_ptr->threads = reg_msg->threads;
		node_ptr->cpus    = reg_msg->cpus;
	}

	if (reg_msg->real_memory < config_ptr->real_memory) {
		if (slurmctld_conf.fast_schedule == 0) {
			debug("Node %s has low real_memory size (%u < %u)",
			      reg_msg->node_name, reg_msg->real_memory,
			      config_ptr->real_memory);
		} else if (slurmctld_conf.fast_schedule == 1) {
			error("Node %s has low real_memory size (%u < %u)",
			      reg_msg->node_name, reg_msg->real_memory,
			      config_ptr->real_memory);
			error_code  = EINVAL;
			if (reason_down)
				xstrcat(reason_down, ", ");
			xstrcat(reason_down, "Low RealMemory");
		}
	}
	node_ptr->real_memory = reg_msg->real_memory;

	if (reg_msg->tmp_disk < config_ptr->tmp_disk) {
		if (slurmctld_conf.fast_schedule == 0) {
			debug("Node %s has low tmp_disk size (%u < %u)",
			      reg_msg->node_name, reg_msg->tmp_disk,
			      config_ptr->tmp_disk);
		} else if (slurmctld_conf.fast_schedule == 1) {
			error("Node %s has low tmp_disk size (%u < %u)",
			      reg_msg->node_name, reg_msg->tmp_disk,
			      config_ptr->tmp_disk);
			error_code = EINVAL;
			if (reason_down)
				xstrcat(reason_down, ", ");
			xstrcat(reason_down, "Low TmpDisk");
		}
	}
	node_ptr->tmp_disk = reg_msg->tmp_disk;

	if (reg_msg->cpu_spec_list != NULL) {
		xfree(node_ptr->cpu_spec_list);
		node_ptr->cpu_spec_list = reg_msg->cpu_spec_list;
		reg_msg->cpu_spec_list = NULL;	/* Nothing left to free */

		cpu_spec_array = bitfmt2int(node_ptr->cpu_spec_list);
		i = 0;
		node_ptr->core_spec_cnt = 0;
		while (cpu_spec_array[i] != -1) {
			node_ptr->core_spec_cnt += (cpu_spec_array[i + 1] -
				cpu_spec_array[i]) + 1;
			i += 2;
		}
		if (node_ptr->threads)
			node_ptr->core_spec_cnt /= node_ptr->threads;
		xfree(cpu_spec_array);
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

	if (IS_NODE_NO_RESPOND(node_ptr)) {
		node_ptr->node_state &= (~NODE_STATE_NO_RESPOND);
		node_ptr->node_state &= (~NODE_STATE_POWER_UP);
		last_node_update = time (NULL);
	}

	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;

	if (error_code) {
		if (!IS_NODE_DOWN(node_ptr)
			&& !IS_NODE_DRAIN(node_ptr)
			&& ! IS_NODE_FAIL(node_ptr)) {
			error ("Setting node %s state to DRAIN",
				   reg_msg->node_name);
			drain_nodes(reg_msg->node_name,
						reason_down,
						slurmctld_conf.slurm_user_id);
		}
		last_node_update = time (NULL);
	} else if (reg_msg->status == ESLURMD_PROLOG_FAILED) {
		if (!IS_NODE_DRAIN(node_ptr) && !IS_NODE_FAIL(node_ptr)) {
			error("Prolog failure on node %s, draining the node",
			      reg_msg->node_name);
			drain_nodes(reg_msg->node_name, "Prolog error",
				    slurm_get_slurm_user_id());
			last_node_update = time (NULL);
		}
	} else {
		if (IS_NODE_UNKNOWN(node_ptr) || IS_NODE_FUTURE(node_ptr)) {
			bool unknown = 0;

			if (IS_NODE_UNKNOWN(node_ptr))
				unknown = 1;

			debug("validate_node_specs: node %s registered with "
			      "%u jobs",
			      reg_msg->node_name,reg_msg->job_count);
			if (IS_NODE_FUTURE(node_ptr) &&
			    IS_NODE_MAINT(node_ptr) &&
			    !is_node_in_maint_reservation(node_inx))
				node_flags &= (~NODE_STATE_MAINT);
			if (reg_msg->job_count) {
				node_ptr->node_state = NODE_STATE_ALLOCATED |
					node_flags;
			} else {
				node_ptr->node_state = NODE_STATE_IDLE |
					node_flags;
				node_ptr->last_idle = now;
			}
			last_node_update = now;

			/* don't send this on a slurmctld unless needed */
			if (unknown && slurmctld_init_db
			    && !IS_NODE_DRAIN(node_ptr)
			    && !IS_NODE_FAIL(node_ptr)) {
				/* reason information is handled in
				   clusteracct_storage_g_node_up()
				*/
				clusteracct_storage_g_node_up(
					acct_db_conn, node_ptr, now);
			}
		} else if (IS_NODE_DOWN(node_ptr) &&
			   ((slurmctld_conf.ret2service == 2) ||
			    !xstrcmp(node_ptr->reason, "Scheduled reboot") ||
			    ((slurmctld_conf.ret2service == 1) &&
			     !xstrcmp(node_ptr->reason, "Not responding") &&
			     (node_ptr->boot_time <
			      node_ptr->last_response)))) {
			if (reg_msg->job_count) {
				node_ptr->node_state = NODE_STATE_ALLOCATED |
					node_flags;
			} else {
				node_ptr->node_state = NODE_STATE_IDLE |
					node_flags;
				node_ptr->last_idle = now;
			}
			info("node %s returned to service",
			     reg_msg->node_name);
			trigger_node_up(node_ptr);
			last_node_update = now;
			if (!IS_NODE_DRAIN(node_ptr)
			    && !IS_NODE_FAIL(node_ptr)) {
				/* reason information is handled in
				   clusteracct_storage_g_node_up()
				*/
				clusteracct_storage_g_node_up(
					acct_db_conn, node_ptr, now);
			}
		} else if (node_ptr->last_response
			   && (node_ptr->boot_time > node_ptr->last_response)
			   && (slurmctld_conf.ret2service != 2)) {
			if (!node_ptr->reason ||
			    (node_ptr->reason &&
			     !xstrcmp(node_ptr->reason, "Not responding"))) {
				if (node_ptr->reason)
					xfree(node_ptr->reason);
				node_ptr->reason_time = now;
				node_ptr->reason_uid =
					slurm_get_slurm_user_id();
				node_ptr->reason = xstrdup(
					"Node unexpectedly rebooted");
			}
			info("%s: Node %s unexpectedly rebooted boot_time %d"
			     "last response %d",
			     __func__, reg_msg->node_name,
			     (int)node_ptr->boot_time,
			     (int)node_ptr->last_response);
			_make_node_down(node_ptr, now);
			kill_running_job_by_node_name(reg_msg->node_name);
			last_node_update = now;
			reg_msg->job_count = 0;
		} else if (IS_NODE_ALLOCATED(node_ptr) &&
			   (reg_msg->job_count == 0)) {	/* job vanished */
			node_ptr->node_state = NODE_STATE_IDLE | node_flags;
			node_ptr->last_idle = now;
			last_node_update = now;
		} else if (IS_NODE_COMPLETING(node_ptr) &&
			   (reg_msg->job_count == 0)) {	/* job already done */
			node_ptr->node_state &= (~NODE_STATE_COMPLETING);
			last_node_update = now;
			bit_clear(cg_node_bitmap, node_inx);
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
				bit_set(cg_node_bitmap, node_inx);
			}
			last_node_update = now;
		}

		select_g_update_node_config(node_inx);
		select_g_update_node_state(node_ptr);
		_sync_bitmaps(node_ptr, reg_msg->job_count);
	}

	xfree(reason_down);
	if (reg_msg->energy)
		memcpy(node_ptr->energy, reg_msg->energy,
		       sizeof(acct_gather_energy_t));

	node_ptr->last_response = now;

	*newly_up = (!orig_node_avail && bit_test(avail_node_bitmap, node_inx));

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
	    (!strncmp(front_end_ptr->reason, "Not responding", 14))) {
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
 * RET 0 if no error, SLURM error code otherwise
 * NOTE: READ lock_slurmctld config before entry
 */
extern int validate_nodes_via_front_end(
		slurm_node_registration_status_msg_t *reg_msg,
		uint16_t protocol_version, bool *newly_up)
{
	int error_code = 0, i, j, rc;
	bool update_node_state = false;
	struct job_record *job_ptr;
	struct config_record *config_ptr;
	struct node_record *node_ptr;
	time_t now = time(NULL);
	ListIterator job_iterator;
	hostlist_t reg_hostlist = NULL;
	char *host_str = NULL, *reason_down = NULL;
	uint32_t node_flags;
	front_end_record_t *front_end_ptr;

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
		if ( (reg_msg->job_id[i] >= MIN_NOALLOC_JOBID) &&
		     (reg_msg->job_id[i] <= MAX_NOALLOC_JOBID) ) {
			info("NoAllocate job %u.%u reported",
				reg_msg->job_id[i], reg_msg->step_id[i]);
			continue;
		}

		job_ptr = find_job_record(reg_msg->job_id[i]);
		node_ptr = node_record_table_ptr;
		if (job_ptr && job_ptr->node_bitmap &&
		    ((j = bit_ffs(job_ptr->node_bitmap)) >= 0))
			node_ptr += j;

		if (job_ptr == NULL) {
			error("Orphan job %u.%u reported on %s",
			      reg_msg->job_id[i], reg_msg->step_id[i],
			      front_end_ptr->name);
			abort_job_on_node(reg_msg->job_id[i],
					  job_ptr, front_end_ptr->name);
			continue;
		} else if (job_ptr->batch_host == NULL) {
			error("Resetting NULL batch_host of job %u to %s",
			      reg_msg->job_id[i], front_end_ptr->name);
			job_ptr->batch_host = xstrdup(front_end_ptr->name);
		}


		if (IS_JOB_RUNNING(job_ptr) || IS_JOB_SUSPENDED(job_ptr)) {
			debug3("Registered job %u.%u on %s",
			       reg_msg->job_id[i], reg_msg->step_id[i],
			       front_end_ptr->name);
			if (job_ptr->batch_flag) {
				/* NOTE: Used for purging defunct batch jobs */
				job_ptr->time_last_active = now;
			}
		}

		else if (IS_JOB_COMPLETING(job_ptr)) {
			/* Re-send kill request as needed,
			 * not necessarily an error */
			kill_job_on_node(reg_msg->job_id[i], job_ptr,
					 node_ptr);
		}

		else if (IS_JOB_PENDING(job_ptr)) {
			/* Typically indicates a job requeue and the hung
			 * slurmd that went DOWN is now responding */
			error("Registered PENDING job %u.%u on %s",
			      reg_msg->job_id[i], reg_msg->step_id[i],
			      front_end_ptr->name);
			abort_job_on_node(reg_msg->job_id[i], job_ptr,
					  front_end_ptr->name);
		}

		else if (difftime(now, job_ptr->end_time) <
			 slurm_get_msg_timeout()) {	/* Race condition */
			debug("Registered newly completed job %u.%u on %s",
				reg_msg->job_id[i], reg_msg->step_id[i],
				front_end_ptr->name);
		}

		else {		/* else job is supposed to be done */
			error("Registered job %u.%u in state %s on %s",
				reg_msg->job_id[i], reg_msg->step_id[i],
				job_state_string(job_ptr->job_state),
				front_end_ptr->name);
			kill_job_on_node(reg_msg->job_id[i], job_ptr,
					 node_ptr);
		}
	}


	/* purge orphan batch jobs */
	job_iterator = list_iterator_create(job_list);
	while ((job_ptr = (struct job_record *) list_next(job_iterator))) {
		if (!IS_JOB_RUNNING(job_ptr) ||
		    IS_JOB_CONFIGURING(job_ptr) ||
		    (job_ptr->batch_flag == 0))
			continue;
		if (job_ptr->front_end_ptr != front_end_ptr)
			continue;
#ifdef HAVE_BG
		/* slurmd does not report job presence until after prolog
		 * completes which waits for bgblock boot to complete.
		 * This can take several minutes on BlueGene. */
		if (difftime(now, job_ptr->time_last_active) <=
		    (BG_FREE_PREVIOUS_BLOCK + BG_MIN_BLOCK_BOOT +
		     BG_INCR_BLOCK_BOOT * job_ptr->node_cnt))
			continue;
#else
		if (difftime(now, job_ptr->time_last_active) <= 5)
			continue;
#endif
		info("Killing orphan batch job %u", job_ptr->job_id);
		job_complete(job_ptr->job_id, 0, false, false, 0);
	}
	list_iterator_destroy(job_iterator);

	(void) gres_plugin_node_config_unpack(reg_msg->gres_info,
					      node_record_table_ptr->name);
	for (i = 0, node_ptr = node_record_table_ptr; i < node_record_count;
	     i++, node_ptr++) {
		config_ptr = node_ptr->config_ptr;
		node_ptr->last_response = now;

		rc = gres_plugin_node_config_validate(node_ptr->name,
						      config_ptr->gres,
						      &node_ptr->gres,
						      &node_ptr->gres_list,
						      slurmctld_conf.
						      fast_schedule,
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
		gres_plugin_node_state_log(node_ptr->gres_list, node_ptr->name);

		if (reg_msg->up_time) {
			node_ptr->up_time = reg_msg->up_time;
			node_ptr->boot_time = now - reg_msg->up_time;
		}
		node_ptr->slurmd_start_time = reg_msg->slurmd_start_time;

		if (IS_NODE_NO_RESPOND(node_ptr)) {
			update_node_state = true;
#ifndef HAVE_ALPS_CRAY
			/* This is handled by the select/cray plugin */
			node_ptr->node_state &= (~NODE_STATE_NO_RESPOND);
#endif
			node_ptr->node_state &= (~NODE_STATE_POWER_UP);
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
					node_ptr->last_idle = now;
				}
				if (!IS_NODE_DRAIN(node_ptr) &&
				    !IS_NODE_FAIL(node_ptr)) {
					/* reason information is handled in
					   clusteracct_storage_g_node_up()
					*/
					clusteracct_storage_g_node_up(
						acct_db_conn,
						node_ptr, now);
				}
			} else if (IS_NODE_DOWN(node_ptr) &&
				   ((slurmctld_conf.ret2service == 2) ||
				    !xstrcmp(node_ptr->reason,
					     "Scheduled reboot") ||
				    ((slurmctld_conf.ret2service == 1) &&
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
					node_ptr->last_idle = now;
				}
				trigger_node_up(node_ptr);
				if (!IS_NODE_DRAIN(node_ptr) &&
				    !IS_NODE_FAIL(node_ptr)) {
					/* reason information is handled in
					   clusteracct_storage_g_node_up()
					*/
					clusteracct_storage_g_node_up(
						acct_db_conn,
						node_ptr, now);
				}
			} else if (IS_NODE_ALLOCATED(node_ptr) &&
				   (node_ptr->run_job_cnt == 0)) {
				/* job vanished */
				update_node_state = true;
				node_ptr->node_state = NODE_STATE_IDLE |
					node_flags;
				node_ptr->last_idle = now;
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

			select_g_update_node_config(i);
			select_g_update_node_state(node_ptr);
			_sync_bitmaps(node_ptr,
				      (node_ptr->run_job_cnt +
				       node_ptr->comp_job_cnt));
		}
		if (reg_msg->energy)
			memcpy(node_ptr->energy, reg_msg->energy,
			       sizeof(acct_gather_energy_t));

		if (slurmctld_init_db &&
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
static void _sync_bitmaps(struct node_record *node_ptr, int job_count)
{
	int node_inx = node_ptr - node_record_table_ptr;

	if (job_count == 0) {
		bit_set (idle_node_bitmap, node_inx);
		bit_set (share_node_bitmap, node_inx);
	}
	if (IS_NODE_DOWN(node_ptr) || IS_NODE_DRAIN(node_ptr) ||
	    IS_NODE_FAIL(node_ptr) || IS_NODE_NO_RESPOND(node_ptr))
		bit_clear (avail_node_bitmap, node_inx);
	else
		bit_set   (avail_node_bitmap, node_inx);
	if (IS_NODE_DOWN(node_ptr))
		bit_clear (up_node_bitmap, node_inx);
	else
		bit_set   (up_node_bitmap, node_inx);
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
	    ((slurmctld_conf.ret2service == 2) ||
	     !xstrcmp(fe_ptr->reason, "Scheduled reboot") ||
	     ((slurmctld_conf.ret2service == 1) &&
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
static void _node_did_resp(struct node_record *node_ptr)
{
	int node_inx;
	uint32_t node_flags;
	time_t now = time(NULL);

	node_inx = node_ptr - node_record_table_ptr;
	/* Do not change last_response value (in the future) for nodes being
	 *  booted so unexpected reboots are recognized */
	if (node_ptr->last_response < now)
		node_ptr->last_response = now;
	if (IS_NODE_NO_RESPOND(node_ptr) || IS_NODE_POWER_UP(node_ptr)) {
		info("Node %s now responding", node_ptr->name);
		node_ptr->node_state &= (~NODE_STATE_NO_RESPOND);
		node_ptr->node_state &= (~NODE_STATE_POWER_UP);
		if (!is_node_in_maint_reservation(node_inx))
			node_ptr->node_state &= (~NODE_STATE_MAINT);
		last_node_update = now;
	}
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	if (IS_NODE_UNKNOWN(node_ptr)) {
		node_ptr->last_idle = now;
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
	    ((slurmctld_conf.ret2service == 2) ||
	     !xstrcmp(node_ptr->reason, "Scheduled reboot") ||
	     ((slurmctld_conf.ret2service == 1) &&
	      !xstrcmp(node_ptr->reason, "Not responding")))) {
		node_ptr->last_idle = now;
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
		bit_set (idle_node_bitmap, node_inx);
		bit_set (share_node_bitmap, node_inx);
	}
	if (IS_NODE_DOWN(node_ptr) || IS_NODE_DRAIN(node_ptr) ||
	    IS_NODE_FAIL(node_ptr)) {
		bit_clear (avail_node_bitmap, node_inx);
	} else
		bit_set   (avail_node_bitmap, node_inx);
	if (IS_NODE_DOWN(node_ptr))
		bit_clear (up_node_bitmap, node_inx);
	else
		bit_set   (up_node_bitmap, node_inx);
	return;
}
#endif

/*
 * node_did_resp - record that the specified node is responding
 * IN name - name of the node
 * NOTE: READ lock_slurmctld config before entry
 */
void node_did_resp (char *name)
{
#ifdef HAVE_FRONT_END
	front_end_record_t *node_ptr;
	node_ptr = find_front_end_record (name);
#else
	struct node_record *node_ptr;
	node_ptr = find_node_record (name);
#endif
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
	struct node_record *node_ptr;

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
	 */
	if (resp_type != RESPONSE_FORWARD_FAILED)
		node_ptr->last_response = msg_time - 1;

	if (!IS_NODE_DOWN(node_ptr)) {
		/* Logged by node_no_resp_msg() on periodic basis */
		node_ptr->not_responding = true;
	}

	if (IS_NODE_NO_RESPOND(node_ptr))
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
	bit_clear (avail_node_bitmap, (node_ptr - node_record_table_ptr));
#endif
	return;
}

/* For every node with the "not_responding" flag set, clear the flag
 * and log that the node is not responding using a hostlist expression */
extern void node_no_resp_msg(void)
{
	int i;
	struct node_record *node_ptr;
	char *host_str = NULL;
	hostlist_t no_resp_hostlist = NULL;

	for (i=0; i<node_record_count; i++) {
		node_ptr = &node_record_table_ptr[i];
		if (!node_ptr->not_responding)
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
	struct node_record *node_ptr;

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
void set_node_down_ptr (struct node_record *node_ptr, char *reason)
{
	time_t now = time(NULL);

	if ((node_ptr->reason == NULL) ||
	    (strncmp(node_ptr->reason, "Not responding", 14) == 0)) {
		xfree(node_ptr->reason);
		node_ptr->reason = xstrdup(reason);
		node_ptr->reason_time = now;
		node_ptr->reason_uid = slurm_get_slurm_user_id();
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
	struct node_record *node_ptr;

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
	struct node_record *node_ptr;

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
struct node_record *
find_first_node_record (bitstr_t *node_bitmap)
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
		return &node_record_table_ptr[inx];
}

/* msg_to_slurmd - send given msg_type (REQUEST_RECONFIGURE or
 * REQUEST_SHUTDOWN) to every slurmd, no args */
void msg_to_slurmd (slurm_msg_type_t msg_type)
{
	int i;
	shutdown_msg_t *shutdown_req;
	agent_arg_t *kill_agent_args;
#ifdef HAVE_FRONT_END
	front_end_record_t *front_end_ptr;
#else
	struct node_record *node_ptr;
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
	node_ptr = node_record_table_ptr;
	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if (IS_NODE_FUTURE(node_ptr))
			continue;
		if (IS_NODE_CLOUD(node_ptr) && IS_NODE_POWER_SAVE(node_ptr))
			continue;
		if (kill_agent_args->protocol_version >
		    node_record_table_ptr[i].protocol_version)
			kill_agent_args->protocol_version =
				node_record_table_ptr[i].protocol_version;
		hostlist_push_host(kill_agent_args->hostlist, node_ptr->name);
		kill_agent_args->node_count++;
	}
#endif

	if (kill_agent_args->node_count == 0) {
		hostlist_destroy(kill_agent_args->hostlist);
		xfree (kill_agent_args);
	} else {
		debug ("Spawning agent msg_type=%d", msg_type);
		agent_queue_request(kill_agent_args);
	}
}


/* make_node_alloc - flag specified node as allocated to a job
 * IN node_ptr - pointer to node being allocated
 * IN job_ptr  - pointer to job that is starting
 */
extern void make_node_alloc(struct node_record *node_ptr,
			    struct job_record *job_ptr)
{
	int inx = node_ptr - node_record_table_ptr;
	uint32_t node_flags;

	(node_ptr->run_job_cnt)++;
	bit_clear(idle_node_bitmap, inx);
	if (job_ptr->details && (job_ptr->details->share_res == 0)) {
		bit_clear(share_node_bitmap, inx);
		(node_ptr->no_share_job_cnt)++;
	}

	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	node_ptr->node_state = NODE_STATE_ALLOCATED | node_flags;
	xfree(node_ptr->reason);
	node_ptr->reason_time = 0;
	node_ptr->reason_uid = NO_VAL;

	last_node_update = time (NULL);
}

/* make_node_comp - flag specified node as completing a job
 * IN node_ptr - pointer to node marked for completion of job
 * IN job_ptr - pointer to job that is completing
 * IN suspended - true if job was previously suspended
 */
extern void make_node_comp(struct node_record *node_ptr,
			   struct job_record *job_ptr, bool suspended)
{
	int inx = node_ptr - node_record_table_ptr;
	uint32_t node_flags;
	time_t now = time(NULL);

	xassert(node_ptr);
	if (suspended) {
		if (node_ptr->sus_job_cnt)
			(node_ptr->sus_job_cnt)--;
		else
			error("Node %s sus_job_cnt underflow in "
				"make_node_comp", node_ptr->name);
	} else {
		if (node_ptr->run_job_cnt)
			(node_ptr->run_job_cnt)--;
		else
			error("Node %s run_job_cnt underflow in "
				"make_node_comp", node_ptr->name);

		if (job_ptr->details && (job_ptr->details->share_res == 0)) {
			if (node_ptr->no_share_job_cnt)
				(node_ptr->no_share_job_cnt)--;
			else
				error("Node %s no_share_job_cnt underflow in "
					"make_node_comp", node_ptr->name);
			if (node_ptr->no_share_job_cnt == 0)
				bit_set(share_node_bitmap, inx);
		}
	}

	if (!IS_NODE_DOWN(node_ptr))  {
		/* Don't verify  RPC if DOWN */
		(node_ptr->comp_job_cnt)++;
		node_ptr->node_state |= NODE_STATE_COMPLETING;
		bit_set(cg_node_bitmap, inx);
	}
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;

	if ((node_ptr->run_job_cnt  == 0) &&
	    (node_ptr->comp_job_cnt == 0)) {
		bit_set(idle_node_bitmap, inx);
		if (IS_NODE_DRAIN(node_ptr) || IS_NODE_FAIL(node_ptr)) {
			trigger_node_drained(node_ptr);
			clusteracct_storage_g_node_down(
				acct_db_conn,
				node_ptr, now, NULL,
				slurm_get_slurm_user_id());
		}
	}

	if (IS_NODE_DOWN(node_ptr)) {
		debug3("make_node_comp: Node %s being left DOWN",
		       node_ptr->name);
	} else if (node_ptr->run_job_cnt)
		node_ptr->node_state = NODE_STATE_ALLOCATED | node_flags;
	else {
		node_ptr->node_state = NODE_STATE_IDLE | node_flags;
		node_ptr->last_idle = now;
	}
	last_node_update = now;
}

/* _make_node_down - flag specified node as down */
static void _make_node_down(struct node_record *node_ptr, time_t event_time)
{
	int inx = node_ptr - node_record_table_ptr;
	uint32_t node_flags;

	xassert(node_ptr);
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	node_flags &= (~NODE_STATE_COMPLETING);
	node_ptr->node_state = NODE_STATE_DOWN | node_flags;
	bit_clear (avail_node_bitmap, inx);
	bit_clear (cg_node_bitmap,    inx);
	bit_set   (idle_node_bitmap,  inx);
	bit_set   (share_node_bitmap, inx);
	bit_clear (up_node_bitmap,    inx);
	select_g_update_node_state(node_ptr);
	trigger_node_down(node_ptr);
	last_node_update = time (NULL);
	clusteracct_storage_g_node_down(acct_db_conn,
					node_ptr, event_time, NULL,
					node_ptr->reason_uid);
}

/*
 * make_node_idle - flag specified node as having finished with a job
 * IN node_ptr - pointer to node reporting job completion
 * IN job_ptr - pointer to job that just completed
 */
void make_node_idle(struct node_record *node_ptr,
		    struct job_record *job_ptr)
{
	int inx = node_ptr - node_record_table_ptr;
	uint32_t node_flags;
	time_t now = time(NULL);
	bitstr_t *node_bitmap = NULL;
	char jbuf[JBUFSIZ];

	if (job_ptr) { /* Specific job completed */
		if (job_ptr->node_bitmap_cg)
			node_bitmap = job_ptr->node_bitmap_cg;
		else
			node_bitmap = job_ptr->node_bitmap;
	}

	trace_job(job_ptr, __func__, "enter");

	xassert(node_ptr);
	if (node_bitmap && (bit_test(node_bitmap, inx))) {
		/* Not a replay */
		last_job_update = now;
		bit_clear(node_bitmap, inx);

		job_update_cpu_cnt(job_ptr, inx);

		if (job_ptr->node_cnt) {
			/* Clean up the JOB_COMPLETING flag
			 * only if there is not the slurmctld
			 * epilog running, otherwise wait
			 * when it terminates then this
			 * function will be invoked.
			 */
			job_ptr->node_cnt--;
			if (job_ptr->node_cnt == 0
				&& job_ptr->epilog_running == false)
				cleanup_completing(job_ptr);
		} else {
			error("%s: %s node_cnt underflow",
			      __func__, jobid2str(job_ptr, jbuf));
		}

		if (IS_JOB_SUSPENDED(job_ptr)) {
			/* Remove node from suspended job */
			if (node_ptr->sus_job_cnt)
				(node_ptr->sus_job_cnt)--;
			else
				error("%s: %s node %s sus_job_cnt underflow",
				      __func__, jobid2str(job_ptr, jbuf),
				      node_ptr->name);
		} else if (IS_JOB_RUNNING(job_ptr)) {
			/* Remove node from running job */
			if (node_ptr->run_job_cnt)
				(node_ptr->run_job_cnt)--;
			else
				error("%s: %s node %s run_job_cnt underflow",
				      __func__, jobid2str(job_ptr, jbuf),
				      node_ptr->name);
		} else {
			if (node_ptr->comp_job_cnt)
				(node_ptr->comp_job_cnt)--;
			else
				error("%s: %s node %s run_job_cnt underflow",
				      __func__, jobid2str(job_ptr, jbuf),
				      node_ptr->name);
			if (node_ptr->comp_job_cnt > 0)
				return;		/* More jobs completing */
		}
	}

	if (node_ptr->comp_job_cnt == 0) {
		node_ptr->node_state &= (~NODE_STATE_COMPLETING);
		bit_clear(cg_node_bitmap, inx);
	}
	node_flags = node_ptr->node_state & NODE_STATE_FLAGS;
	if (IS_NODE_DOWN(node_ptr)) {
		debug3("%s: %s node %s being left DOWN",
		       __func__, jobid2str(job_ptr, jbuf), node_ptr->name);
		return;
	}
	bit_set(up_node_bitmap, inx);

	if (IS_NODE_DRAIN(node_ptr) || IS_NODE_FAIL(node_ptr) ||
	    IS_NODE_NO_RESPOND(node_ptr))
		bit_clear(avail_node_bitmap, inx);
	else
		bit_set(avail_node_bitmap, inx);

	if ((IS_NODE_DRAIN(node_ptr) || IS_NODE_FAIL(node_ptr)) &&
	    (node_ptr->run_job_cnt == 0) && (node_ptr->comp_job_cnt == 0)) {
		node_ptr->node_state = NODE_STATE_IDLE | node_flags;
		bit_set(idle_node_bitmap, inx);
		debug3("%s: %s node %s is DRAINED",
		       __func__, jobid2str(job_ptr, jbuf), node_ptr->name);
		node_ptr->last_idle = now;
		trigger_node_drained(node_ptr);
		clusteracct_storage_g_node_down(acct_db_conn,
						node_ptr, now, NULL,
						slurm_get_slurm_user_id());
	} else if (node_ptr->run_job_cnt) {
		node_ptr->node_state = NODE_STATE_ALLOCATED | node_flags;
		if (!IS_NODE_NO_RESPOND(node_ptr) &&
		     !IS_NODE_FAIL(node_ptr) && !IS_NODE_DRAIN(node_ptr))
			bit_set(avail_node_bitmap, inx);
	} else {
		node_ptr->node_state = NODE_STATE_IDLE | node_flags;
		if (!IS_NODE_NO_RESPOND(node_ptr) &&
		     !IS_NODE_FAIL(node_ptr) && !IS_NODE_DRAIN(node_ptr))
			bit_set(avail_node_bitmap, inx);
		if (!IS_NODE_NO_RESPOND(node_ptr) &&
		    !IS_NODE_COMPLETING(node_ptr))
			bit_set(idle_node_bitmap, inx);
		node_ptr->last_idle = now;
	}
	last_node_update = now;
}

extern int send_nodes_to_accounting(time_t event_time)
{
	int rc = SLURM_SUCCESS, i = 0;
	struct node_record *node_ptr = NULL;
	uint32_t node_scaling = 0;
	char *reason = NULL;
	slurmctld_lock_t node_read_lock = {
		READ_LOCK, NO_LOCK, READ_LOCK, WRITE_LOCK };

	select_g_alter_node_cnt(SELECT_GET_NODE_SCALING, &node_scaling);

 	lock_slurmctld(node_read_lock);
	/* send nodes not in not 'up' state */
	node_ptr = node_record_table_ptr;
	for (i = 0; i < node_record_count; i++, node_ptr++) {
		if (node_ptr->reason)
			reason = node_ptr->reason;
		else
			reason = "First Registration";
		if (node_ptr->name == '\0' ||
		   (!IS_NODE_DRAIN(node_ptr) && !IS_NODE_FAIL(node_ptr) &&
		    !IS_NODE_DOWN(node_ptr))) {
			/* At this point, the node appears to be up,
			   but on some systems we need to make sure there
			   aren't some part of a node in an error state. */
			if (node_ptr->select_nodeinfo) {
				uint16_t err_cpus = 0;
				select_g_select_nodeinfo_get(
					node_ptr->select_nodeinfo,
					SELECT_NODEDATA_SUBCNT,
					NODE_STATE_ERROR,
					&err_cpus);
				if (err_cpus) {
					struct node_record send_node;
					struct config_record config_rec;
					int cpus_per_node = 1;

					memset(&send_node, 0,
					       sizeof(struct node_record));
					memset(&config_rec, 0,
					       sizeof(struct config_record));
					send_node.name = node_ptr->name;
					send_node.config_ptr = &config_rec;
					select_g_alter_node_cnt(
						SELECT_GET_NODE_SCALING,
						&node_scaling);

					if (node_scaling)
						cpus_per_node = node_ptr->cpus
							/ node_scaling;
					err_cpus *= cpus_per_node;

					send_node.cpus = err_cpus;
					send_node.node_state = NODE_STATE_ERROR;
					config_rec.cpus = err_cpus;

					rc = clusteracct_storage_g_node_down(
						acct_db_conn,
						&send_node, event_time,
						reason,
						slurm_get_slurm_user_id());
				}
			}
		} else
			rc = clusteracct_storage_g_node_down(
				acct_db_conn,
				node_ptr, event_time,
				reason,
				slurm_get_slurm_user_id());
		if (rc == SLURM_ERROR)
			break;
	}
	unlock_slurmctld(node_read_lock);
	return rc;
}

/* node_fini - free all memory associated with node records */
extern void node_fini (void)
{
	FREE_NULL_BITMAP(avail_node_bitmap);
	FREE_NULL_BITMAP(cg_node_bitmap);
	FREE_NULL_BITMAP(idle_node_bitmap);
	FREE_NULL_BITMAP(power_node_bitmap);
	FREE_NULL_BITMAP(share_node_bitmap);
	FREE_NULL_BITMAP(up_node_bitmap);
	node_fini2();
}

/* Reset a node's CPU load value */
extern void reset_node_load(char *node_name, uint32_t cpu_load)
{
#ifdef HAVE_FRONT_END
	return;
#else
	struct node_record *node_ptr;

	node_ptr = find_node_record(node_name);
	if (node_ptr) {
		time_t now = time(NULL);
		node_ptr->cpu_load = cpu_load;
		node_ptr->cpu_load_time = now;
		last_node_update = now;
	} else
		error("is_node_resp unable to find node %s", node_name);
#endif
}
