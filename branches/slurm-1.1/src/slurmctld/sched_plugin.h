/*****************************************************************************\
 *  sched_plugin.h - Define scheduler plugin functions.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com>
 *  UCRL-CODE-217948.
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

#ifndef __SLURM_CONTROLLER_SCHED_PLUGIN_API_H__
#define __SLURM_CONTROLLER_SCHED_PLUGIN_API_H__

#include <slurm/slurm.h>

/*
 * Initialize the external scheduler adapter.
 *
 * Returns a SLURM errno.
 */
int slurm_sched_init( void );

/*
 * Terminate external scheduler, free memory.
 * 
 * Returns a SLURM errno.
 */
extern int slurm_sched_fini(void);

/*
 **************************************************************************
 *                          P L U G I N   C A L L S                       *
 **************************************************************************
 */

/*
 * For passive schedulers, invoke a scheduling pass.
 */
int slurm_sched_schedule( void );

/*
 * Supply the initial SLURM priority for a newly-submitted job.
 */
u_int32_t slurm_sched_initial_priority( u_int32_t max_prio );

/*
 * Note that some job is pending.
 */
void slurm_sched_job_is_pending( void );

/* 
 * Return any plugin-specific error number
 */
int slurm_sched_p_get_errno( void );

/*
 * Return any plugin-specific error description
 */
char *slurm_sched_p_strerror( int errnum );

/*
 **************************************************************************
 *                              U P C A L L S                             *
 **************************************************************************
 */

/*
 * Returns the port number associated with the remote scheduler.  The
 * port may either be the remote port on which the scheduler listens,
 * or the local port upon which the controller should listen for
 * scheduler requests.  The interpretation of this value depends on
 * the scheduler type.  The value is returned in host byte order.
 */
u_int16_t sched_get_port( void );

/*
 * Returns the authentication credentials.
 */
const char * sched_get_auth( void );

/*
 * RootOnly partitions are typically exempted from external scheduling
 * because these partitions are expected to be directly maintained by
 * the root user (or some external meta-scheduler) that may have it's
 * own mechanisms for scheduling. However some cluster configurations
 * may want to use RootOnly partitions simply to prevent non-root
 * access, and would still like normal external scheduler operation to
 * occur.
 *
 * This procedure reflects the "SchedulerRootFilter" setting in
 * slurm.conf which allows the SLURM configuration to request how
 * external schedulers handle RootOnly partition, if supported by
 * the external scheduler. Currently only the SLURM backfill
 * scheduler makes use of this.
 *
 * Returns non-zero if RootOnly partitions are to be filtered from
 * any external scheduling efforts.
 */
u_int16_t sched_get_root_filter( void );

/*
 * Opaque type for a list of objects supplied by the controller.
 * These objects are either jobs in the job queue, or nodes in the
 * cluster.
 */
typedef struct sched_obj_list *sched_obj_list_t;

/* Functional type for a field accessor. */
typedef void * (*sched_accessor_fn_t)( sched_obj_list_t,
				       int32_t,
				       char * );

/*
 * Functional type for an object list (i.e., job queue or node list)
 * retriever.  This is for abstracting sched_get_node_list() and
 * sched_get_job_list() to facilitate any plugin that may wish to
 * consolidate code.
 */
typedef sched_obj_list_t (*sched_objlist_fn_t)( void );

/*
 * Retrieve a pointer to a function that will, when called with an
 * object index, return a poiner to the value of the named field in
 * the opaque object structure.  This accessor is guaranteed to be
 * valid for the time in which the plugin is loaded and so can be
 * dereferenced once at plugin load time.  The return value is always
 * to be interpreted as a pointer, regardless of the size of the
 * pointed-to type.
 *
 * field (in) - the name of the field whose accessor is to be returned.
 *
 * Returns a pointer to a function of type
 *
 *	void *func( void *data, uint32_t idx, char *type );
 *
 * where "data" is the opaque data provided by sched_get_<some>_list(),
 * "idx" is the index of the node in "data" whose attribute value is
 * desired, and "type" is an optional pointer to a byte in which is
 * placed a value identifying the data type of the returned value.
 * "type" may be NULL.  Returns NULL if no accessor can be provided
 * for the named field.
 *
 * The values placed into "type", if addressable, are ASCII-encoded
 * characters with the following meanings:
 *
 * 'e' - An enumeration encoded as a string.  This is for data that is
 * stored internally in SLURM as a C enum, but which for version skew
 * reasons we do not want to transmit in its underyling numerical
 * representation.  The strings used are defined below in conjunction
 * with the objects and fields to which they apply.
 *
 * 's' - A string value that should probably be passed to the
 * scheduler without further interpretation.
 *
 * 'S' - A string value that may require interpretation by the plugin
 * prior to passing it to the scheduler.  This includes strings that
 * have embedded delimetersor other structure.  The difference between
 * 's' and 'S' is fairly arbitrary and merely serves as a hint to the
 * plugin about the format of the returned string.
 *
 * 't' - A value of type "time_t" as defined on the SLURM controller.
 *
 * 'i' - A 16-bit signed integer.
 *
 * 'I' - A 32-bit signed integer.
 *
 * 'u' - A 16-bit unsigned integer.
 *
 * 'U' - a 32-bit unsigned integer.
 *
 * TESTED: 16 May 2003
 */
extern sched_accessor_fn_t sched_get_accessor( char *field );

/*
 * Return the number of items in the object list.
 */
extern int32_t sched_get_obj_count( sched_obj_list_t data );

/*
 * Free an object list produced by any function of type
 * sched_objlist_fn_t. 
 *
 * data (in) - A block of data supplied by sched_get_<whatever>_list().
 *
 * Returns SLURM_SUCCESS if successful and SLURM_ERROR otherwise.
 *
 * TESTED: 16 May 2003
 */
extern int sched_free_obj_list( sched_obj_list_t data );


/*
 * Retrieve a snapshot of node data from the controller.  The data returned
 * is guaranteed to be self-consistent.  That is, it is guaranteed that the
 * data will not have been modified during the acquisition of the snapshot.
 * However it is not guaranteed to be persistently accurate.  It is accurate
 * at the time at which it is delivered to the plugin, but after delivery
 * the controller's node list is made available for subsequent operations.
 *
 * TESTED: 16 May 2003
 */
extern sched_obj_list_t sched_get_node_list( void );

#define NODE_FIELD_NAME			"node.name"
#define NODE_FIELD_STATE		"node.state"
#define NODE_FIELD_REAL_MEM		"node.real_mem"
#define NODE_FIELD_TMP_DISK		"node.tmp_disk"
#define NODE_FIELD_NUM_CPUS		"node.num_cpus"
#define NODE_FIELD_MOD_TIME		"node.mod_time"
#define NODE_FIELD_PARTITION		"node.partition"

#define NODE_STATE_LABEL_DOWN		"DOWN"
#define NODE_STATE_LABEL_UNKNOWN       	"UNKNOWN"
#define NODE_STATE_LABEL_IDLE		"IDLE"
#define NODE_STATE_LABEL_ALLOCATED     	"ALLOCATED"
#define NODE_STATE_LABEL_DRAINED       	"DRAINED"
#define NODE_STATE_LABEL_DRAINING      	"DRAINING"
#define NODE_STATE_LABEL_COMPLETING    	"COMPLETING"


/*
 * Retrieve a snapshot of the job queue from the controller.  The data
 * returned is guarantted to be self-consistent.  (See
 * sched_get_node_list() above.)
 *
 * data (in/out) - place to store an opaque chunk of job data.
 *
 * count (in/out ) - place to store the number of jobs that the opaque
 *	data represents. 
 *
 * Returns SLURM_SUCCESS if successful and SLURM_ERROR otherwise.
 *
 */
extern sched_obj_list_t sched_get_job_list( void );

#define JOB_FIELD_ID			"job.id"
#define JOB_FIELD_NAME			"job.name"
#define JOB_FIELD_LAST_ACTIVE  		"job.last_active"
#define JOB_FIELD_STATE			"job.state"
#define JOB_FIELD_TIME_LIMIT   		"job.time_limit"
#define JOB_FIELD_NUM_TASKS		"job.num_tasks"
#define JOB_FIELD_SUBMIT_TIME  		"job.submit_time"
#define JOB_FIELD_START_TIME   		"job.start_time"
#define JOB_FIELD_END_TIME     		"job.end_time"
#define JOB_FIELD_USER_ID      		"job.user_id"
#define JOB_FIELD_GROUP_ID		"job.group_id"
#define JOB_FIELD_MIN_NODES    		"job.min_nodes"
#define JOB_FIELD_FEATURES     		"job.features"
#define JOB_FIELD_PRIORITY     		"job.priority"
#define JOB_FIELD_WORK_DIR     		"job.work_dir"
#define JOB_FIELD_PARTITION	       	"job.partition"
#define JOB_FIELD_MIN_DISK 		"job.min_disk"
#define JOB_FIELD_MIN_MEMORY	       	"job.min_mem"
#define JOB_FIELD_REQ_NODES		"job.req_nodes"
#define JOB_FIELD_ALLOC_NODES		"job.alloc_nodes"
#define JOB_FIELD_MIN_NODES		"job.min_nodes"

#define JOB_STATE_LABEL_PENDING		"PENDING"
#define JOB_STATE_LABEL_RUNNING		"RUNNING"
#define JOB_STATE_LABEL_SUSPENDED	"SUSPENDED"
#define JOB_STATE_LABEL_COMPLETE	"COMPLETE"
#define JOB_STATE_LABEL_FAILED		"FAILED"
#define JOB_STATE_LABEL_TIMEOUT		"TIMEOUT"
#define JOB_STATE_LABEL_NODE_FAIL	"NODE_FAIL"


/*
 * Set the list of nodes on which the job will run.
 *
 * nodes is a comma-separated string of node names.  It is
 * copied by sched_set_nodelist().
 */
extern int sched_set_nodelist( const uint32_t job_id, char *nodes );

/*
 * Start the job identified by the job ID.
 *
 * Returns a SLURM errno.
 */
extern int sched_start_job( const uint32_t job_id, const uint32_t new_prio );

/*
 * Stop the job identified by the job ID.
 *
 * Returns a SLURM errno.
 */
extern int sched_cancel_job( const uint32_t job_id );

#endif /*__SLURM_CONTROLLER_SCHED_PLUGIN_API_H__*/
