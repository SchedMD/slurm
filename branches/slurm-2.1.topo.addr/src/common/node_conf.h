/*****************************************************************************\
 *  node_conf.h - definitions for reading the node part of slurm configuration 
 *  file and work with the corresponding structures
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Morris Jette <jette1@llnl.gov> et. al.
 *  CODE-OCEC-09-009. All rights reserved.
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

#ifndef _HAVE_NODE_CONF_H
#define _HAVE_NODE_CONF_H

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif			/* HAVE_INTTYPES_H */
#endif

#include <time.h>

#include "src/common/bitstring.h"
#include "src/common/list.h"
#include "src/common/slurm_protocol_defs.h"
#include "src/common/slurm_protocol_socket_common.h"

#define CONFIG_MAGIC	0xc065eded
#define FEATURE_MAGIC	0x34dfd8b5
#define NODE_MAGIC	0x0de575ed

struct config_record {
	uint32_t magic;		/* magic cookie to test data integrity */
	uint16_t cpus;		/* count of processors running on the node */
	uint16_t sockets;	/* number of sockets per node */
	uint16_t cores;		/* number of cores per CPU */
	uint16_t threads;	/* number of threads per core */
	uint32_t real_memory;	/* MB real memory on the node */
	uint32_t tmp_disk;	/* MB total storage in TMP_FS file system */
	uint32_t weight;	/* arbitrary priority of node for 
				 * scheduling work on */
	char *feature;		/* arbitrary list of features associated */
	char **feature_array;	/* array of feature names */
	char *nodes;		/* name of nodes with this configuration */
	bitstr_t *node_bitmap;	/* bitmap of nodes with this configuration */
};
extern List config_list;	/* list of config_record entries */

struct features_record {
	uint32_t magic;		/* magic cookie to test data integrity */
	char *name;		/* name of a feature */
	bitstr_t *node_bitmap;	/* bitmap of nodes with this feature */
};
extern List feature_list;	/* list of features_record entries */

struct node_record {
	uint32_t magic;			/* magic cookie for data integrity */
	char *name;			/* name of the node. NULL==defunct */
	uint16_t node_state;		/* enum node_states, ORed with 
					 * NODE_STATE_NO_RESPOND if not 
					 * responding */
	bool not_responding;		/* set if fails to respond, 
					 * clear after logging this */
	time_t last_response;		/* last response from the node */
	time_t last_idle;		/* time node last become idle */
	uint16_t cpus;			/* count of processors on the node */
	uint16_t sockets;		/* number of sockets per node */
	uint16_t cores;			/* number of cores per CPU */
	uint16_t threads;		/* number of threads per core */
	uint32_t real_memory;		/* MB real memory on the node */
	uint32_t tmp_disk;		/* MB total disk in TMP_FS */
	uint32_t up_time;		/* seconds since node boot */
	struct config_record *config_ptr;  /* configuration spec ptr */
	uint16_t part_cnt;		/* number of associated partitions */
	struct part_record **part_pptr;	/* array of pointers to partitions 
					 * associated with this node*/
	char *comm_name;		/* communications path name to node */
	uint16_t port;			/* TCP port number of the slurmd */
	slurm_addr slurm_addr;		/* network address */
	uint16_t comp_job_cnt;		/* count of jobs completing on node */
	uint16_t run_job_cnt;		/* count of jobs running on node */
	uint16_t no_share_job_cnt;	/* count of jobs running that will
					 * not share nodes */
	char *reason; 			/* why a node is DOWN or DRAINING */
	char *features;			/* associated features, used only
					 * for state save/restore, DO NOT
					 * use for scheduling purposes */
	char *arch;			/* computer architecture */
	char *os;			/* operating system now running */
	struct node_record *node_next;	/* next entry with same hash index */
	uint32_t hilbert_integer;	/* Hilbert number based on node name,
					 * no need to save/restore */
#ifdef APBASIL_LOC
	uint32_t basil_node_id;		/* Cray/BASIL node ID,
					 * no need to save/restore */
#endif	/* APBASIL_LOC */
	select_nodeinfo_t *select_nodeinfo; /* opaque data structure,
					     * use select_g_get_nodeinfo()
					     * to access conents */

};
extern struct node_record *node_record_table_ptr;  /* ptr to node records */
extern int node_record_count;		/* count in node_record_table_ptr */
extern time_t last_node_update;		/* time of last node record update */



/* 
 * _build_all_nodeline_info - get a array of slurm_conf_node_t structures
 *	from the slurm.conf reader, build table, and set values
 * RET 0 if no error, error code otherwise
 */
extern int build_all_nodeline_info (void);

/* Given a config_record with it's bitmap already set, update feature_list */
extern void  build_config_feature_list (struct config_record *config_ptr);

/*
 * create_config_record - create a config_record entry and set is values to 
 *	the defaults. each config record corresponds to a line in the  
 *	slurm.conf file and typically describes the configuration of a 
 *	large number of nodes
 * RET pointer to the config_record
 * NOTE: memory allocated will remain in existence until 
 *	_delete_config_record() is called to delete all configuration records
 */
extern struct config_record *create_config_record (void);

/* 
 * create_node_record - create a node record and set its values to defaults
 * IN config_ptr - pointer to node's configuration information
 * IN node_name - name of the node
 * RET pointer to the record or NULL if error
 * NOTE: allocates memory at node_record_table_ptr that must be xfreed when  
 *	the global node table is no longer required
 */
extern struct node_record *create_node_record (
			struct config_record *config_ptr, char *node_name);

/* 
 * find_node_record - find a record for node with specified name
 * input: name - name of the desired node 
 * output: return pointer to node record or NULL if not found
 *         node_hash_table - table of hash indecies
 */
extern struct node_record *find_node_record (char *name);

/* 
 * init_node_conf - initialize the node configuration tables and values. 
 *	this should be called before creating any node or configuration 
 *	entries.
 * RET 0 if no error, otherwise an error code
 */
extern int init_node_conf (void);

/* node_fini2 - free memory associated with node records (except bitmaps) */
extern void node_fini2 (void);

/* Purge the contents of a node record */
extern void purge_node_rec (struct node_record *node_ptr);

/* 
 * rehash_node - build a hash table of the node_record entries. 
 * NOTE: manages memory for node_hash_table
 */
extern void rehash_node (void);

/* Convert a node state string to it's equivalent enum value */
extern int state_str2int(const char *state_str);

#endif /* !_HAVE_NODE_CONF_H */
