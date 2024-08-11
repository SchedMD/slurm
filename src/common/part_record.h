/*****************************************************************************\
 *  part_record.h - PARTITION parameters and data structures
 *****************************************************************************
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

#ifndef _SLURM_PART_RECORD_H
#define _SLURM_PART_RECORD_H
#define PART_MAGIC 0xaefe8495

#include "slurm/slurmdb.h"

#include "src/common/pack.h"
#include "src/common/xhash.h"

typedef struct {
	slurmdb_bf_usage_t *job_usage;
	slurmdb_bf_usage_t *resv_usage;
	xhash_t *user_usage;
} bf_part_data_t;

typedef struct part_record {
	uint32_t magic;		/* magic cookie to test data integrity */
				/* DO NOT ALPHABETIZE */
	list_t *allow_accts_list; /* List of ptrs to allow_accounts in
				   * assoc_mgr */
	char *allow_accounts;	/* comma delimited list of accounts,
				 * NULL indicates all */
	char **allow_account_array; /* NULL terminated list of allowed
				 * accounts */
	char *allow_alloc_nodes;/* comma delimited list of allowed
				 * allocating nodes
				 * NULL indicates all */
	char *allow_groups;	/* comma delimited list of groups,
				 * NULL indicates all */
	uid_t *allow_uids;	/* list of allowed user IDs */
	int allow_uids_cnt;	/* count of allowed user IDs */
	char *allow_qos;	/* comma delimited list of qos,
				 * NULL indicates all */
	bitstr_t *allow_qos_bitstr; /* (DON'T PACK) assocaited with
				 * char *allow_qos but used internally */
	char *alternate; 	/* name of alternate partition */
	double *billing_weights;    /* array of TRES billing weights */
	char   *billing_weights_str;/* per TRES billing weight string */
	uint32_t cpu_bind;	/* default CPU binding type */
	uint64_t def_mem_per_cpu; /* default MB memory per allocated CPU */
	uint32_t default_time;	/* minutes, NO_VAL or INFINITE */
	char *deny_accounts;	/* comma delimited list of denied accounts */
	list_t *deny_accts_list; /* List of ptrs to deny_accounts in
				  * assoc_mgr */
	char **deny_account_array; /* NULL terminated list of denied accounts */
	char *deny_qos;		/* comma delimited list of denied qos */
	bitstr_t *deny_qos_bitstr; /* (DON'T PACK) associated with
				 * char *deny_qos but used internallly */
	uint32_t flags;		/* see PART_FLAG_* in slurm.h */
	uint32_t grace_time;	/* default preempt grace time in seconds */
	list_t *job_defaults_list; /* List of job_defaults_t elements */
	uint32_t max_cpus_per_node; /* maximum allocated CPUs per node */
	uint32_t max_cpus_per_socket; /*maximum allocated CPUs per socket */
	uint64_t max_mem_per_cpu; /* maximum MB memory per allocated CPU */
	uint32_t max_nodes;	/* per job or INFINITE */
	uint32_t max_nodes_orig;/* unscaled value (c-nodes on BlueGene) */
	uint16_t max_share;	/* number of jobs to gang schedule */
	uint32_t max_time;	/* minutes or INFINITE */
	uint32_t num_sched_jobs; /* number of jobs scheduled on a scheduling
				  * iteration, internal use only, NO NOT PACK */
	uint32_t min_nodes;	/* per job */
	uint32_t min_nodes_orig;/* unscaled value (c-nodes on BlueGene) */
	char *name;		/* name of the partition */
	bitstr_t *node_bitmap;	/* bitmap of nodes in partition */
	char *nodes;		/* expanded nodelist from orig_nodes */
	char *orig_nodes;	/* comma delimited list names of nodes */
	char *nodesets;		/* store nodesets for display, NO PACK */
	double   norm_priority;	/* normalized scheduling priority for
				 * jobs (DON'T PACK) */
	uint16_t over_time_limit; /* job's time limit can be exceeded by this
				   * number of minutes before cancellation */
	uint16_t preempt_mode;	/* See PREEMPT_MODE_* in slurm/slurm.h */
	uint16_t priority_job_factor;	/* job priority weight factor */
	uint16_t priority_tier;	/* tier for scheduling and preemption */
	char *qos_char;         /* requested QOS from slurm.conf */
	slurmdb_qos_rec_t *qos_ptr; /* pointer to the quality of
				     * service record attached to this
				     * partition confirm the value before use */
	uint16_t resume_timeout; /* time required in order to perform a node
				  * resume operation */
	uint16_t state_up;	/* See PARTITION_* states in slurm.h */
	uint32_t suspend_time;  /* node idle for this long before power save
				 * mode */
	uint16_t suspend_timeout; /* time required in order to perform a node
				   * suspend operation */
	uint32_t total_nodes;	/* total number of nodes in the partition */
	uint32_t total_cpus;	/* total number of cpus in the partition */
	uint32_t max_cpu_cnt;	/* max # of cpus on a node in the partition */
	uint32_t max_core_cnt;	/* max # of cores on a node in the partition */
	uint16_t cr_type;	/* Custom CR values for partition (if supported by select plugin) */
	uint64_t *tres_cnt;	/* array of total TRES in partition. NO_PACK */
	char     *tres_fmt_str;	/* str of configured TRES in partition */
	bf_part_data_t *bf_data;/* backfill data, NO PACK */
} part_record_t;

extern part_record_t *part_record_create(void);
extern void part_record_delete(part_record_t *part_ptr);
extern void part_record_pack(part_record_t *part_ptr,
			     buf_t *buffer,
			     uint16_t protocol_version);
extern int part_record_unpack(part_record_t **part,
			      buf_t *buffer,
			      uint16_t protocol_version);

#endif /* _SLURM_PART_RECORD_H */
