/*****************************************************************************\
 *  wiki_wrapper.c - provides the scheduler plugin API.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov> et. al.
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

#include <stdio.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include <slurm/slurm_errno.h>

extern "C" {
#       include "src/common/log.h"
#	include "src/common/plugin.h"
#	include "src/common/read_config.h"
#	include "src/common/xmalloc.h"
}

#include "../receptionist.h"
#include "../prefix_courier.h"
#include "wiki_mailbag.h"

/*
 * Don't make these "const".  G++ won't generate external symbols
 * if you do.  Grr.
 */
extern "C" {
	char		plugin_name[]	= "SLURM Maui Scheduler plugin";
	char		plugin_type[]	= "sched/wiki";
	uint32_t	plugin_version	= 90;
}

/* A plugin-global errno. */
static int plugin_errno = SLURM_SUCCESS;

static pthread_t receptionist_thread;
static bool thread_running = false;
static pthread_mutex_t thread_flag_mutex = PTHREAD_MUTEX_INITIALIZER;

extern "C" void  _get_wiki_conf_path(char *buf, int size);
extern "C" void  _parse_wiki_config(void);
#define PRIO_HOLD      0
#define PRIO_DECREMENT 1
static int init_prio_mode = PRIO_HOLD;

// **************************************************************************
//  TAG(                    receptionist_thread_entry                    ) 
// **************************************************************************
static void *
receptionist_thread_entry( void *dummy )
{
	prefix_courier_factory_t courier_factory;
	wiki_mailbag_factory_t mailbag_factory;
	receptionist_t *receptionist = NULL;
	struct sockaddr_in sockaddr;

	// * Set up Wiki scheduler address.
	sockaddr.sin_family = AF_INET;

	sockaddr.sin_addr.s_addr = INADDR_ANY;
	sockaddr.sin_port = htons( sched_get_port() );

	try {
		receptionist = new receptionist_t( &courier_factory,
						   &mailbag_factory,
						   &sockaddr );
	} catch ( const char *msg ) {
		error( "Wiki scheduler plugin: %s", msg );
		pthread_exit( 0 );
	}
		
	// *
	// * The receptionist listen() method does not return if it
	// * obtains a connection.
	// *
	verbose( "Wiki scheduler interface starting ..." );
	try {
		if ( receptionist->listen() < 0 ) {
			error( "Wiki: unable to listen on connection\n" );
		}
	} catch ( const char *msg ) {
		error( "Wiki scheduler plugin: %s", msg );
	}

	delete receptionist;
	
	pthread_mutex_lock( &thread_flag_mutex );
	thread_running = false;
	pthread_mutex_unlock( &thread_flag_mutex );
	return NULL;
}

	
// **************************************************************************
//  TAG(                              init                              ) 
// **************************************************************************
extern "C"
int init( void )
{
	pthread_attr_t attr;
	
	verbose( "Wiki scheduler plugin loaded" );

	pthread_mutex_lock( &thread_flag_mutex );
	if ( thread_running ) {
		debug2( "Wiki thread already running, not starting another" );
		pthread_mutex_unlock( &thread_flag_mutex );
		return SLURM_ERROR;
	}

	_parse_wiki_config( );
	pthread_attr_init( &attr );
	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
	pthread_create( &receptionist_thread,
			NULL,
			receptionist_thread_entry,
			NULL );
	thread_running = true;
	pthread_mutex_unlock( &thread_flag_mutex );

	return SLURM_SUCCESS;
}

// **************************************************************************
//  TAG(                      _get_wiki_conf_path                          )
// **************************************************************************
extern "C" void _get_wiki_conf_path(char *buf, int size)
{
	char *val = getenv("SLURM_CONF");
	int i;

	if (!val)
		val = default_slurm_config_file;

	/* Replace file name on end of path */
	i = strlen(val) - strlen("slurm.conf") + strlen("wiki.conf") + 1;
	if (i > size)
		fatal("_get_wiki_conf_path: buffer too small");
	strcpy(buf, val);
	val = strrchr(buf, (int)'/');
	if (val)	/* absolute path */
		val++;
	else		/* not absolute path */
		val = buf;
	strcpy(val, "wiki.conf");

	return;
}

// **************************************************************************
//  TAG(                      _parse_wiki_config                        )
// **************************************************************************
extern "C" void _parse_wiki_config(void)
{
	s_p_options_t options[] = { {"JobPriority", S_P_STRING}, {NULL} };
	s_p_hashtbl_t *tbl;
	char *priority_mode;
	struct stat buf;

	/* need to define wiki_conf as static array rather than perform
	 * malloc due to memory corruption problem in gcc v3.2 */
	static char wiki_conf[256];

	_get_wiki_conf_path(wiki_conf, sizeof(wiki_conf));
	if (stat(wiki_conf, &buf) == -1) {
		debug("No wiki.conf file (%s)", wiki_conf);
		return;
	}

	debug("Reading wiki.conf file (%s)",wiki_conf);
	tbl = s_p_hashtbl_create(options);
	if (s_p_parse_file(tbl, wiki_conf) == SLURM_ERROR)
		fatal("something wrong with opening/reading wiki.conf file");

	if (s_p_get_string(&priority_mode, "JobPriority", tbl)) {
		if (strcasecmp(priority_mode, "hold") == 0)
			init_prio_mode = PRIO_HOLD;
		else if (strcasecmp(priority_mode, "run") == 0)
			init_prio_mode = PRIO_DECREMENT;
		else
			error("Invalid value for JobPriority in wiki.conf");	
		xfree(priority_mode);
	}
	s_p_hashtbl_destroy(tbl);

	return;
}

// **************************************************************************
//  TAG(                              fini                              ) 
// **************************************************************************
extern "C" void fini( void )
{
	pthread_mutex_lock( &thread_flag_mutex );
	if ( thread_running ) {
		verbose( "Wiki scheduler plugin shutting down" );
		// pthread_mutex_unlock( &thread_flag_mutex ); -- ???
		pthread_cancel( receptionist_thread );
		thread_running = false;
	}
	pthread_mutex_unlock( &thread_flag_mutex );
}


// **************************************************************************
//  TAG(                   slurm_sched_plugin_schedule                   ) 
// **************************************************************************
extern "C" int
slurm_sched_plugin_schedule( void )
{
	verbose( "Wiki plugin: schedule() is a NO-OP" );
	return SLURM_SUCCESS;
}


// **************************************************************************
//  TAG(                   slurm_sched_plugin_initial_priority              ) 
// **************************************************************************
extern "C" u_int32_t
slurm_sched_plugin_initial_priority( u_int32_t last_prio )
{
	// Two modes of operation are currently supported:
	//
	// PRIO_HOLD: Wiki is a polling scheduler, so the initial priority
	// is always zero to keep SLURM from spontaneously starting the
	// job.  The scheduler will suggest which job's priority should
	// be made non-zero and thus allowed to proceed.
	//
	// PRIO_DECREMENT: Set the job priority to one less than the last
	// job and let Wiki change priorities of jobs as desired to re-order 
	// the queue)
	//
	if (init_prio_mode == PRIO_DECREMENT) {
		if (last_prio >= 2)
			return (last_prio - 1);
		else
			return 1;
	} else 
		return 0;
}

// **************************************************************************
//  TAG(                slurm_sched_plugin_job_is_pending                   ) 
// **************************************************************************
extern "C" void slurm_sched_plugin_job_is_pending( void )
{
	// Wiki does not respond to pending job
}

// **************************************************************************
//  TAG(                 slurm_sched_plugin_partition_change                )
// **************************************************************************
extern "C" void slurm_sched_plugin_partition_change( void )
{
	// Wiki does not respond to partition changes
}

// *************************************************************************
//  TAG(              slurm_sched_get_errno                                )
// *************************************************************************
extern "C" int 
slurm_sched_get_errno( void )
{
	return plugin_errno;
}

// ************************************************************************
// TAG(              slurm_sched_strerror                                 ) 
// ************************************************************************
extern "C" char *
slurm_sched_strerror( int errnum )
{
	return NULL;
}

