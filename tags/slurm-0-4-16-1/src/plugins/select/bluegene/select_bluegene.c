/*****************************************************************************\
 *  select_bluegene.c - node selection plugin for Blue Gene system.
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Dan Phung <phung4@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#include "bluegene.h"

/*
 * These variables are required by the generic plugin interface.  If they
 * are not found in the plugin, the plugin loader will ignore it.
 *
 * plugin_name - a string giving a human-readable description of the
 * plugin.  There is no maximum length, but the symbol must refer to
 * a valid string.
 *
 * plugin_type - a string suggesting the type of the plugin or its
 * applicability to a particular form of data or method of data handling.
 * If the low-level plugin API is used, the contents of this string are
 * unimportant and may be anything.  SLURM uses the higher-level plugin
 * interface which requires this string to be of the form
 *
 *	<application>/<method>
 *
 * where <application> is a description of the intended application of
 * the plugin (e.g., "select" for SLURM node selection) and <method>
 * is a description of how this plugin satisfies that application.  SLURM will
 * only load select plugins if the plugin_type string has a 
 * prefix of "select/".
 *
 * plugin_version - an unsigned 32-bit integer giving the version number
 * of the plugin.  If major and minor revisions are desired, the major
 * version number may be multiplied by a suitable magnitude constant such
 * as 100 or 1000.  Various SLURM versions will likely require a certain
 * minimum versions for their plugins as the node selection API matures.
 */
const char plugin_name[]       	= "Blue Gene node selection plugin";
const char plugin_type[]       	= "select/bluegene";
const uint32_t plugin_version	= 90;

/* pthread stuff for updating BGL node status */
static pthread_t bluegene_thread = 0;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;

/** initialize the status pthread */
static int _init_status_pthread(void);

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
extern int init ( void )
{
#ifndef HAVE_BGL
	fatal("Plugin select/bluegene is illegal on non-BlueGene computers");
#endif
#if (SYSTEM_DIMENSIONS != 3)
	fatal("SYSTEM_DIMENSIONS value (%d) invalid for Blue Gene",
		SYSTEM_DIMENSIONS);
#endif
#ifdef HAVE_BGL_FILES
	if (!getenv("CLASSPATH") || !getenv("DB2INSTANCE") 
	||  !getenv("VWSPATH"))
		fatal("db2profile has not been run to setup DB2 environment");

	if ((SELECT_MESH  != RM_MESH)
	||  (SELECT_TORUS != RM_TORUS)
	||  (SELECT_NAV   != RM_NAV))
		fatal("enum conn_type out of sync with rm_api.h");

	if ((SELECT_COPROCESSOR_MODE  != RM_PARTITION_COPROCESSOR_MODE)
	||  (SELECT_VIRTUAL_NODE_MODE != RM_PARTITION_VIRTUAL_NODE_MODE))
		fatal("enum node_use_type out of sync with rm_api.h");
#endif

	verbose("%s loading...", plugin_name);
	if (init_bgl() || _init_status_pthread())
		return SLURM_ERROR;

	return SLURM_SUCCESS;
}

static int _init_status_pthread(void)
{
	pthread_attr_t attr;

	pthread_mutex_lock( &thread_flag_mutex );
	if ( bluegene_thread ) {
		debug2("Bluegene thread already running, not starting "
			"another");
		pthread_mutex_unlock( &thread_flag_mutex );
		return SLURM_ERROR;
	}

	slurm_attr_init( &attr );
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
	pthread_create( &bluegene_thread, &attr, bluegene_agent, NULL);
	pthread_mutex_unlock( &thread_flag_mutex );
	pthread_attr_destroy( &attr );

	return SLURM_SUCCESS;
}

static int _wait_for_thread (pthread_t thread_id)
{
	int i;

	for (i=0; i<4; i++) {
		if (pthread_kill(thread_id, 0))
			return SLURM_SUCCESS;
		sleep(1);
	}
	error("Could not kill select script pthread");
	return SLURM_ERROR;
}

extern int fini ( void )
{
	int rc = SLURM_SUCCESS;

	pthread_mutex_lock( &thread_flag_mutex );
	if ( bluegene_thread ) {
		agent_fini = true;
		verbose("Bluegene select plugin shutting down");
		rc = _wait_for_thread(bluegene_thread);
		bluegene_thread = 0;
	}
	pthread_mutex_unlock( &thread_flag_mutex );

	fini_bgl();

	return rc;
}

/*
 * The remainder of this file implements the standard SLURM 
 * node selection API.
 */

/*
 * Called by slurmctld when a new configuration file is loaded
 * or scontrol is used to change partition configuration
 */
 extern int select_p_part_init(List part_list)
{
	xassert(part_list);
#ifdef HAVE_BGL
	if(read_bgl_conf() == SLURM_ERROR) {
		fatal("Error, could not read the file");
		return SLURM_ERROR;
	}
#else
	/*looking for partitions only I created */
	if (create_static_partitions(part_list) == SLURM_ERROR) {
		/* error in creating the static partitions, so
		 * partitions referenced by submitted jobs won't
		 * correspond to actual slurm partitions/bgl
		 * partitions.
		 */
		fatal("Error, could not create the static partitions");
		return SLURM_ERROR;
	}
#endif
	sort_bgl_record_inc_size(bgl_list);

	return SLURM_SUCCESS; 
}

/* We rely upon DB2 to save and restore BlueGene state */
extern int select_p_state_save(char *dir_name)
{
	return SLURM_SUCCESS;
}

extern int select_p_state_restore(char *dir_name)
{
	return SLURM_SUCCESS;
}

/* Sync BGL blocks to currently active jobs */
extern int select_p_job_init(List job_list)
{
	return sync_jobs(job_list);
}

/* All initialization is performed by select_p_part_init() */
extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
{
	return SLURM_SUCCESS;
}

/*
 * select_p_job_test - Given a specification of scheduling requirements, 
 *	identify the nodes which "best" satify the request. The specified 
 *	nodes may be DOWN or BUSY at the time of this test as may be used 
 *	to deterime if a job could ever run.
 * IN job_ptr - pointer to job being scheduled
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to 
 *	satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN max_nodes - maximum count of nodes (0==don't care)
 * RET zero on success, EINVAL otherwise
 * NOTE: bitmap must be a superset of req_nodes at the time that 
 *	select_p_job_test is called
 */
extern int select_p_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			     int min_nodes, int max_nodes)
{
	/* bgl partition test - is there a partition where we have:
	 * 1) geometry requested
	 * 2) min/max nodes (BPs) requested
	 * 3) type: TORUS or MESH or NAV (torus else mesh)
	 * 4) use: VIRTUAL or COPROCESSOR
	 * 
	 * note: we don't have to worry about security at this level
	 * as the SLURM partition logic will handle access rights.
	 */

	return submit_job(job_ptr, bitmap, min_nodes, max_nodes);
}

extern int select_p_job_begin(struct job_record *job_ptr)
{
	return start_job(job_ptr);
}

extern int select_p_job_fini(struct job_record *job_ptr)
{
	return term_job(job_ptr);
}
