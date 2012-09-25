/*****************************************************************************\
 *  slurm_energy_accounting.c - implementation-independent job accounting
 *  logging functions
 *****************************************************************************
 *  Copyright (C) 2003-2007/ The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Bull-HN-PHX/d.rusak,
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  Copyright (C) 2005 Hewlett-Packard Development Company, L.P.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.schedmd.com/slurmdocs/>.
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


#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "src/common/macros.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/xmalloc.h"
#include "src/common/xstring.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

static char	*energy_accounting_type = NULL;
static uint16_t  energy_accounting_freq = 0;
static uint16_t  _shutdown = 0;


typedef struct slurm_energy_accounting_ops {
	int (*energy_accounting_updatenodeenergy) (void);
	uint32_t (*energy_accounting_p_getjoules_task)
		(struct jobacctinfo *jobacct);
	int (*energy_accounting_p_getjoules_scaled)
		(uint32_t step_sampled_cputime, ListIterator itr);
	int (*energy_accounting_p_setbasewatts) ();
	uint32_t (*energy_accounting_p_getcurrentwatts) ();
	uint32_t (*energy_accounting_p_getbasewatts) ();
	uint32_t (*energy_accounting_p_getnodeenergy) (uint32_t up_time);
	int (*init) ();
} slurm_energy_accounting_ops_t;


typedef struct slurm_energy_accounting_context {
	char 			*energy_accounting_type;
	plugrack_t		plugin_list;
	plugin_handle_t		cur_plugin;
	int			energy_accounting_errno;
	slurm_energy_accounting_ops_t	ops;
} slurm_energy_accounting_context_t;

static slurm_energy_accounting_context_t *g_energy_accounting_context = NULL;
static pthread_mutex_t g_energy_accounting_context_lock =
		PTHREAD_MUTEX_INITIALIZER;

static int _slurm_energy_accounting_init(void);
static void *_getjoules_rand(void *arg);
static void *_issue_ipmi(void *arg);
static void *_aggr_steps(void *arg);


static slurm_energy_accounting_context_t *
_slurm_energy_accounting_context_create( const char *energy_accounting_type)
{
	slurm_energy_accounting_context_t *c;

	if ( energy_accounting_type == NULL ) {
		error("_slurm_energy_accounting_context_create: "
				"no energy_accounting type");
		return NULL;
	}

	c = xmalloc( sizeof( struct slurm_energy_accounting_context ) );

	c->energy_accounting_errno = SLURM_SUCCESS;

	c->energy_accounting_type = xstrdup( energy_accounting_type );
	if ( c->energy_accounting_type == NULL ) {
		error( "can't make local copy of energy_accounting type" );
		xfree( c );
		return NULL;
	}

	c->plugin_list = NULL;
	c->cur_plugin = PLUGIN_INVALID_HANDLE;
	c->energy_accounting_errno	= SLURM_SUCCESS;

	return c;
}

static int
_slurm_energy_accounting_context_destroy( slurm_energy_accounting_context_t *c )
{
	int rc = SLURM_SUCCESS;
	/*
	 * Must check return code here because plugins might still
	 * be loaded and active.
	 */
	if ( c->plugin_list ) {
		if ( plugrack_destroy( c->plugin_list ) != SLURM_SUCCESS ) {
			rc = SLURM_ERROR;
		}
	} else {
		plugin_unload(c->cur_plugin);
	}

	xfree( c->energy_accounting_type );
	xfree( c );

	return rc;
}

/*
 * Resolve the operations from the plugin.
 */
static slurm_energy_accounting_ops_t *
_slurm_energy_accounting_get_ops( slurm_energy_accounting_context_t *c )
{
	/*
	 * These strings must be in the same order as the fields declared
	 * for slurm_energy_accounting_ops_t.
	 */
	static const char *syms[] = {
		"energy_accounting_p_updatenodeenergy",
		"energy_accounting_p_getjoules_task" ,
		"energy_accounting_p_getjoules_scaled" ,
		"energy_accounting_p_setbasewatts" ,
		"energy_accounting_p_getcurrentwatts" ,
		"energy_accounting_p_getbasewatts" ,
		"energy_accounting_p_getnodeenergy" ,
		"init"
	};

	int n_syms = sizeof( syms ) / sizeof( char * );
	int rc = 0;

	debug2("slurm_energy_accounting_get_ops:load energy_accounting_type %s",
			c->energy_accounting_type);
	/* Find the correct plugin. */
	c->cur_plugin = plugin_load_and_link(c->energy_accounting_type,
					     n_syms, syms,
					     (void **) &c->ops);
	if ( c->cur_plugin != PLUGIN_INVALID_HANDLE )
		return &c->ops;

	if(errno != EPLUGIN_NOTFOUND) {
		error("Couldn't load specified plugin name for %s: %s",
		      c->energy_accounting_type, plugin_strerror(errno));
		return NULL;
	}

	error("Couldn't find the specified plugin name for %s "
	      "looking at all files",
	      c->energy_accounting_type);

	/* Get the plugin list, if needed. */
	if ( c->plugin_list == NULL ) {
		char *plugin_dir;
		c->plugin_list = plugrack_create();
		if ( c->plugin_list == NULL ) {
			error( "Unable to create a plugin manager" );
			return NULL;
		}

		plugrack_set_major_type( c->plugin_list, "energy_accounting" );
		plugrack_set_paranoia( c->plugin_list,
				       PLUGRACK_PARANOIA_NONE,
				       0 );
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
	}

	/* Find the correct plugin. */
	c->cur_plugin =
		plugrack_use_by_type(c->plugin_list,c->energy_accounting_type);
	if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
		error( "can't find a plugin for type %s",
		       c->energy_accounting_type );
		return NULL;
	}

	/* Dereference the API. */
	if ( (rc = plugin_get_syms( c->cur_plugin,
				    n_syms,
				    syms,
				    (void **) &c->ops )) < n_syms ) {
		error( "incomplete energy_accounting plugin detected only "
		       "got %d out of %d",
		       rc, n_syms);
		return NULL;
	}

	return &c->ops;

}

static int _slurm_energy_accounting_init(void)
{

	int	retval=SLURM_SUCCESS;


	slurm_mutex_lock( &g_energy_accounting_context_lock );
	if ( g_energy_accounting_context )
		goto done;
	energy_accounting_type = slurm_get_energy_accounting_type();
	energy_accounting_freq = slurm_get_energy_accounting_freq();

	debug2("_slurm_energy_accounting_init: energy_accounting_type %s ",
			energy_accounting_type);
	debug2("_slurm_energy_accounting_init: freq %d ",
			energy_accounting_freq);

	g_energy_accounting_context = _slurm_energy_accounting_context_create(
		energy_accounting_type);
	if ( g_energy_accounting_context == NULL ) {
		error( "cannot create a context for %s",energy_accounting_type);
		retval = SLURM_ERROR;
		goto done;
	}

	if ( _slurm_energy_accounting_get_ops( g_energy_accounting_context )
	     == NULL ) {
		error( "cannot resolve job accounting plugin operations" );
		_slurm_energy_accounting_context_destroy(
			g_energy_accounting_context);
		g_energy_accounting_context = NULL;
		retval = SLURM_ERROR;
	}

	retval = (*(g_energy_accounting_context->ops.init))();

done:
	slurm_mutex_unlock( &g_energy_accounting_context_lock );
	xfree(energy_accounting_type);

	return(retval);
}

extern int slurm_energy_accounting_init(void)
{
	if (_slurm_energy_accounting_init() < 0)
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

extern int slurm_energy_accounting_fini(void)
{
	int rc;

	if (!g_energy_accounting_context)
		return SLURM_SUCCESS;

	rc =
	  _slurm_energy_accounting_context_destroy(g_energy_accounting_context);
	g_energy_accounting_context = NULL;
	return rc;
}

extern uint32_t energy_accounting_g_getcurrentwatts(void)
{
	uint32_t retval = 0;
	if (_slurm_energy_accounting_init() < 0)
		return SLURM_ERROR;

	slurm_mutex_lock( &g_energy_accounting_context_lock );
	if ( g_energy_accounting_context ) {
		retval = (*(g_energy_accounting_context->
			    ops.energy_accounting_p_getcurrentwatts))();
	}
	slurm_mutex_unlock( &g_energy_accounting_context_lock );

	return retval;
}

extern uint32_t energy_accounting_g_getbasewatts(void)
{
	uint32_t retval = 0;
	if (_slurm_energy_accounting_init() < 0)
		return SLURM_ERROR;

	slurm_mutex_lock( &g_energy_accounting_context_lock );
	if ( g_energy_accounting_context ) {
		retval = (*(g_energy_accounting_context->
			    ops.energy_accounting_p_getbasewatts))();
	}
	slurm_mutex_unlock( &g_energy_accounting_context_lock );

	return retval;
}

extern uint32_t energy_accounting_g_getnodeenergy(uint32_t up_time)
{
	uint32_t retval = 0;
	if (_slurm_energy_accounting_init() < 0)
		return SLURM_ERROR;

	slurm_mutex_lock( &g_energy_accounting_context_lock );
	if ( g_energy_accounting_context ) {
		retval = (*(g_energy_accounting_context->
			    ops.energy_accounting_p_getnodeenergy)) (up_time);
	}
	slurm_mutex_unlock( &g_energy_accounting_context_lock );

	return retval;
}


extern uint32_t energy_accounting_g_getjoules_task(struct jobacctinfo *jobacct)
{
	int retval = SLURM_SUCCESS;

	if (_slurm_energy_accounting_init() < 0)
		return SLURM_ERROR;

	slurm_mutex_lock( &g_energy_accounting_context_lock );
	if ( g_energy_accounting_context ) {
			retval = (*(g_energy_accounting_context->
			    ops.energy_accounting_p_getjoules_task))(jobacct);
	}
	slurm_mutex_unlock( &g_energy_accounting_context_lock );

	return retval;
}

extern int energy_accounting_g_getjoules_scaled(uint32_t step_sampled_cputime,
		ListIterator itr)
{
	int retval = SLURM_SUCCESS;

	if (_slurm_energy_accounting_init() < 0)
		return SLURM_ERROR;

	slurm_mutex_lock( &g_energy_accounting_context_lock );
	if ( g_energy_accounting_context ) {
		retval = (*(g_energy_accounting_context->
			    ops.energy_accounting_p_getjoules_scaled))
			    (step_sampled_cputime, itr);
	}
	slurm_mutex_unlock( &g_energy_accounting_context_lock );
	return retval;
}

extern int energy_accounting_g_updatenodeenergy(void)
{
	int retval = SLURM_SUCCESS;

	if (_slurm_energy_accounting_init() < 0)
		return retval;

	slurm_mutex_lock( &g_energy_accounting_context_lock );
	if ( g_energy_accounting_context )
		retval = (*(g_energy_accounting_context->ops.
				energy_accounting_updatenodeenergy))();

	slurm_mutex_unlock( &g_energy_accounting_context_lock );
	return retval;
}


extern int energy_accounting_g_setbasewatts(void)
{
	int retval = SLURM_SUCCESS;
	slurm_mutex_lock( &g_energy_accounting_context_lock );
	if ( g_energy_accounting_context )
		retval = (*(g_energy_accounting_context->ops.
				energy_accounting_p_setbasewatts))();
	slurm_mutex_unlock( &g_energy_accounting_context_lock );
	return retval;
}


