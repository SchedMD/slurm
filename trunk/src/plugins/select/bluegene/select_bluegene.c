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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#if HAVE_STDINT_H
#  include <stdint.h>
#endif
#if HAVE_INTTYPES_H
#  include <inttypes.h>
#endif

#include <stdio.h>
#include <slurm/slurm.h>
#include <slurm/slurm_errno.h>

#include "src/common/slurm_xlator.h"
#include "src/slurmctld/slurmctld.h"

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif /* WITH_PTHREADS */

#include "slurm/slurm_errno.h"
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

/** pthread stuff for updating BGL node status */
static pthread_t bluegene_thread;
static bool thread_running = false;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;

/** initialize the status pthread */
int _init_status_pthread();

/*
 * init() is called when the plugin is loaded, before any other functions
 * are called.  Put global initialization here.
 */
int init ( void )
{
	debug("init");
	verbose("%s loading...", plugin_name);

	if (init_bgl())
		return SLURM_ERROR;
	
	if (_init_status_pthread())
		return SLURM_ERROR;

	verbose("%s done loading, system ready for use.", plugin_name);
	return SLURM_SUCCESS;
}

int _init_status_pthread()
{
	pthread_attr_t attr;

	pthread_mutex_lock( &thread_flag_mutex );
	if ( thread_running ) {
		debug2( "Bluegene thread already running, not starting another" );
		pthread_mutex_unlock( &thread_flag_mutex );
		return SLURM_ERROR;
	}

	slurm_attr_init( &attr );
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
	pthread_create( &bluegene_thread, &attr, bluegene_agent, NULL);
	thread_running = true;
	pthread_mutex_unlock( &thread_flag_mutex );

	return SLURM_SUCCESS;
}

int fini ( void )
{
	debug("fini");

	pthread_mutex_lock( &thread_flag_mutex );
	if ( thread_running ) {
		verbose( "Bluegene select plugin shutting down" );
		pthread_cancel( bluegene_thread );
		thread_running = false;
	}
	pthread_mutex_unlock( &thread_flag_mutex );

	return SLURM_SUCCESS;
}

/*
 * The remainder of this file implements the standard SLURM 
 * node selection API.
 */

/** 
 * this is called periodically by slurmctld when 
 * - new nodes are added 
 * - new configuration file is loaded
 */
extern int select_p_part_init(List part_list)
{ 
	debug("select_p_part_init");
	/** isn't the part_list already accessible to me? */
	slurm_part_list = part_list;

	if (read_bgl_conf())
		return SLURM_ERROR;

	/* create_static_partitions */
	if (create_static_partitions()){
		/* error in creating the static partitions, so
		 * partitions referenced by submitted jobs won't
		 * correspond to actual slurm partitions/bgl
		 * partitions.
		 */
		fatal("Error, could not create the static partitions");
		return 1;
	}

	return SLURM_SUCCESS; 
}

extern int select_p_state_save(char *dir_name)
{
	debug("select_p_state_save");
	return SLURM_SUCCESS;
}

extern int select_p_state_restore(char *dir_name)
{
	debug("select_p_state_restore");
	return SLURM_SUCCESS;
}

extern int select_p_node_init(struct node_record *node_ptr, int node_cnt)
{
	debug("select_p_node_init");
	if (node_ptr == NULL) {
		error("select_p_node_init: node_ptr == NULL");
		return SLURM_ERROR;
	}

	if (node_cnt < 0) {
		error("select_p_node_init: node_cnt < 0");
		return SLURM_ERROR;
	}

	// error("select/bluegene plugin not yet functional");
	debug("select_p_node_init should be doing a system wide status "
	      "check on all the nodes to updated the system bitmap, "
	      "along with killing old jobs, etc");
	// return SLURM_ERROR;
	return SLURM_SUCCESS;
}

/*
 * select_p_job_test - Given a specification of scheduling requirements, 
 *	identify the nodes which "best" satify the request.
 * 	"best" is defined as either single set of consecutive nodes satisfying 
 *	the request and leaving the minimum number of unused nodes OR 
 *	the fewest number of consecutive node sets
 * IN job_ptr - pointer to job being scheduled
 * IN/OUT bitmap - usable nodes are set on input, nodes not required to 
 *	satisfy the request are cleared, other left set
 * IN min_nodes - minimum count of nodes
 * IN max_nodes - maximum count of nodes (0==don't care)
 * RET zero on success, EINVAL otherwise
 * globals (passed via select_p_node_init): 
 *	node_record_count - count of nodes configured
 *	node_record_table_ptr - pointer to global node table
 * NOTE: bitmap must be a superset of req_nodes at the time that 
 *	select_p_job_test is called
 */
extern int select_p_job_test(struct job_record *job_ptr, bitstr_t *bitmap,
			     int min_nodes, int max_nodes)
{
	debug("select_p_job_test");
	debug("select/bluegene plugin in alpha development");


	/* bgl partition test - is there a partition where we have:
	 * 1) geometery requested
	 * 2) min/max nodes (BPs) requested
	 * 3) type? (TORUS is harder than MESH to fulfill)...HOW TO TEST?!?!!
	 * 
	 * note: we don't have to worry about security at this level
	 * b/c the SLURM partition logic will handle access rights.
	 */

	if (submit_job(job_ptr, bitmap, min_nodes, max_nodes)){
		return SLURM_ERROR;
	} else {
		return SLURM_SUCCESS;
	}
}

extern int select_p_job_init(struct job_record *job_ptr)
{
	debug("select_p_job_init");
	return SLURM_SUCCESS;
}

extern int select_p_job_fini(struct job_record *job_ptr)
{
	debug("select_p_job_fini");
	return SLURM_SUCCESS;
}
