/*****************************************************************************\
 *  slurm_acct_gather_energy.c - implementation-independent job energy
 *  accounting plugin definitions
 *****************************************************************************
 *  Copyright (C) 2012 Bull-HN-PHX.
 *  Written by Bull-HN-PHX/d.rusak,
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
#include "src/common/slurm_acct_gather_energy.h"
#include "src/slurmd/slurmstepd/slurmstepd_job.h"

typedef struct slurm_acct_gather_energy_ops {
	int (*update_node_energy) (void);
	int (*get_data)           (enum acct_energy_type data_type,
				   acct_gather_energy_t *energy);
	int (*set_data)           (enum acct_energy_type data_type,
				   acct_gather_energy_t *energy);
} slurm_acct_gather_energy_ops_t;


typedef struct slurm_acct_gather_energy_context {
	char 			*acct_gather_energy_type;
	plugrack_t		plugin_list;
	plugin_handle_t		cur_plugin;
	int			acct_gather_energy_errno;
	slurm_acct_gather_energy_ops_t	ops;
} slurm_acct_gather_energy_context_t;

static slurm_acct_gather_energy_context_t *g_acct_gather_energy_context = NULL;
static pthread_mutex_t g_acct_gather_energy_context_lock =
		PTHREAD_MUTEX_INITIALIZER;

static int _slurm_acct_gather_energy_init(void);

static slurm_acct_gather_energy_context_t *
_slurm_acct_gather_energy_context_create( const char *acct_gather_energy_type)
{
	slurm_acct_gather_energy_context_t *c;

	if ( acct_gather_energy_type == NULL ) {
		error("_slurm_acct_gather_energy_context_create: "
				"no acct_gather_energy type");
		return NULL;
	}

	c = xmalloc( sizeof( struct slurm_acct_gather_energy_context ) );

	c->acct_gather_energy_errno = SLURM_SUCCESS;

	c->acct_gather_energy_type = xstrdup( acct_gather_energy_type );
	if ( c->acct_gather_energy_type == NULL ) {
		error( "can't make local copy of acct_gather_energy type" );
		xfree( c );
		return NULL;
	}

	c->plugin_list = NULL;
	c->cur_plugin = PLUGIN_INVALID_HANDLE;
	c->acct_gather_energy_errno	= SLURM_SUCCESS;

	return c;
}

static int
_slurm_acct_gather_energy_context_destroy( slurm_acct_gather_energy_context_t *c )
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

	xfree( c->acct_gather_energy_type );
	xfree( c );

	return rc;
}

/*
 * Resolve the operations from the plugin.
 */
static slurm_acct_gather_energy_ops_t *
_slurm_acct_gather_energy_get_ops( slurm_acct_gather_energy_context_t *c )
{
	/*
	 * These strings must be in the same order as the fields declared
	 * for slurm_acct_gather_energy_ops_t.
	 */
	static const char *syms[] = {
		"acct_gather_energy_p_update_node_energy",
		"acct_gather_energy_p_get_data",
		"acct_gather_energy_p_set_data",
	};

	int n_syms = sizeof( syms ) / sizeof( char * );
	int rc = 0;

	debug2("slurm_acct_gather_energy_get_ops: "
	       "load acct_gather_energy_type %s",
	       c->acct_gather_energy_type);
	/* Find the correct plugin. */
	c->cur_plugin = plugin_load_and_link(c->acct_gather_energy_type,
					     n_syms, syms,
					     (void **) &c->ops);
	if ( c->cur_plugin != PLUGIN_INVALID_HANDLE )
		return &c->ops;

	if(errno != EPLUGIN_NOTFOUND) {
		error("Couldn't load specified plugin name for %s: %s",
		      c->acct_gather_energy_type, plugin_strerror(errno));
		return NULL;
	}

	error("Couldn't find the specified plugin name for %s "
	      "looking at all files",
	      c->acct_gather_energy_type);

	/* Get the plugin list, if needed. */
	if ( c->plugin_list == NULL ) {
		char *plugin_dir;
		c->plugin_list = plugrack_create();
		if ( c->plugin_list == NULL ) {
			error( "Unable to create a plugin manager" );
			return NULL;
		}

		plugrack_set_major_type( c->plugin_list, "acct_gather_energy" );
		plugrack_set_paranoia( c->plugin_list,
				       PLUGRACK_PARANOIA_NONE,
				       0 );
		plugin_dir = slurm_get_plugin_dir();
		plugrack_read_dir( c->plugin_list, plugin_dir );
		xfree(plugin_dir);
	}

	/* Find the correct plugin. */
	c->cur_plugin =
		plugrack_use_by_type(c->plugin_list,c->acct_gather_energy_type);
	if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
		error( "can't find a plugin for type %s",
		       c->acct_gather_energy_type );
		return NULL;
	}

	/* Dereference the API. */
	if ( (rc = plugin_get_syms( c->cur_plugin,
				    n_syms,
				    syms,
				    (void **) &c->ops )) < n_syms ) {
		error( "incomplete acct_gather_energy plugin detected only "
		       "got %d out of %d",
		       rc, n_syms);
		return NULL;
	}

	return &c->ops;

}

static int _slurm_acct_gather_energy_init(void)
{

	int	retval = SLURM_SUCCESS;
	char *acct_gather_energy_type = NULL;

	slurm_mutex_lock( &g_acct_gather_energy_context_lock );
	if ( g_acct_gather_energy_context )
		goto done;
	acct_gather_energy_type = slurm_get_acct_gather_energy_type();

	debug2("_slurm_acct_gather_energy_init: acct_gather_energy_type %s ",
	       acct_gather_energy_type);

	g_acct_gather_energy_context = _slurm_acct_gather_energy_context_create(
		acct_gather_energy_type);
	if ( g_acct_gather_energy_context == NULL ) {
		error( "cannot create a context for %s",acct_gather_energy_type);
		retval = SLURM_ERROR;
		goto done;
	}

	if ( _slurm_acct_gather_energy_get_ops( g_acct_gather_energy_context )
	     == NULL ) {
		error( "cannot resolve job accounting plugin operations" );
		_slurm_acct_gather_energy_context_destroy(
			g_acct_gather_energy_context);
		g_acct_gather_energy_context = NULL;
		retval = SLURM_ERROR;
	}

done:
	slurm_mutex_unlock( &g_acct_gather_energy_context_lock );
	xfree(acct_gather_energy_type);

	return(retval);
}

extern int slurm_acct_gather_energy_init(void)
{
	if (_slurm_acct_gather_energy_init() < 0)
		return SLURM_ERROR;
	return SLURM_SUCCESS;
}

extern int slurm_acct_gather_energy_fini(void)
{
	int rc;

	if (!g_acct_gather_energy_context)
		return SLURM_SUCCESS;

	rc =
	  _slurm_acct_gather_energy_context_destroy(g_acct_gather_energy_context);
	g_acct_gather_energy_context = NULL;
	return rc;
}

extern acct_gather_energy_t *acct_gather_energy_alloc(void)
{
	acct_gather_energy_t *energy =
		xmalloc(sizeof(struct acct_gather_energy));

	return energy;
}

extern void acct_gather_energy_destroy(acct_gather_energy_t *energy)
{
	xfree(energy);
}

extern void acct_gather_energy_pack(acct_gather_energy_t *energy, Buf buffer,
				    uint16_t protocol_version)
{
	if (!energy) {
		int i;
		for (i=0; i<4; i++)
			pack32(0, buffer);
		return;
	}

	pack32(energy->base_consumed_energy, buffer);
	pack32(energy->base_watts, buffer);
	pack32(energy->consumed_energy, buffer);
	pack32(energy->current_watts, buffer);
}

extern int acct_gather_energy_unpack(acct_gather_energy_t **energy, Buf buffer,
				     uint16_t protocol_version)
{
	acct_gather_energy_t *energy_ptr = acct_gather_energy_alloc();
	*energy = energy_ptr;

	safe_unpack32(&energy_ptr->base_consumed_energy, buffer);
	safe_unpack32(&energy_ptr->base_watts, buffer);
	safe_unpack32(&energy_ptr->consumed_energy, buffer);
	safe_unpack32(&energy_ptr->current_watts, buffer);

	return SLURM_SUCCESS;

unpack_error:
	acct_gather_energy_destroy(energy_ptr);
	*energy = NULL;
	return SLURM_ERROR;
}

/* extern uint32_t acct_gather_energy_g_getcurrentwatts(void) */
/* { */
/* 	uint32_t retval = 0; */
/* 	if (_slurm_acct_gather_energy_init() < 0) */
/* 		return SLURM_ERROR; */

/* 	slurm_mutex_lock( &g_acct_gather_energy_context_lock ); */
/* 	if ( g_acct_gather_energy_context ) { */
/* 		retval = (*(g_acct_gather_energy_context-> */
/* 			    ops.acct_gather_energy_p_getcurrentwatts))(); */
/* 	} */
/* 	slurm_mutex_unlock( &g_acct_gather_energy_context_lock ); */

/* 	return retval; */
/* } */

/* extern uint32_t acct_gather_energy_g_getbasewatts(void) */
/* { */
/* 	uint32_t retval = 0; */
/* 	if (_slurm_acct_gather_energy_init() < 0) */
/* 		return SLURM_ERROR; */

/* 	slurm_mutex_lock( &g_acct_gather_energy_context_lock ); */
/* 	if ( g_acct_gather_energy_context ) { */
/* 		retval = (*(g_acct_gather_energy_context-> */
/* 			    ops.acct_gather_energy_p_getbasewatts))(); */
/* 	} */
/* 	slurm_mutex_unlock( &g_acct_gather_energy_context_lock ); */

/* 	return retval; */
/* } */

/* extern uint32_t acct_gather_energy_g_getnodeenergy(uint32_t up_time) */
/* { */
/* 	uint32_t retval = 0; */
/* 	if (_slurm_acct_gather_energy_init() < 0) */
/* 		return SLURM_ERROR; */

/* 	slurm_mutex_lock( &g_acct_gather_energy_context_lock ); */
/* 	if ( g_acct_gather_energy_context ) { */
/* 		retval = (*(g_acct_gather_energy_context-> */
/* 			    ops.acct_gather_energy_p_getnodeenergy)) (up_time); */
/* 	} */
/* 	slurm_mutex_unlock( &g_acct_gather_energy_context_lock ); */

/* 	return retval; */
/* } */


/* extern uint32_t acct_gather_energy_g_getjoules_task(struct jobacctinfo *jobacct) */
/* { */
/* 	int retval = SLURM_SUCCESS; */

/* 	if (_slurm_acct_gather_energy_init() < 0) */
/* 		return SLURM_ERROR; */

/* 	slurm_mutex_lock( &g_acct_gather_energy_context_lock ); */
/* 	if ( g_acct_gather_energy_context ) { */
/* 			retval = (*(g_acct_gather_energy_context-> */
/* 			    ops.acct_gather_energy_p_getjoules_task))(jobacct); */
/* 	} */
/* 	slurm_mutex_unlock( &g_acct_gather_energy_context_lock ); */

/* 	return retval; */
/* } */

/* extern int acct_gather_energy_g_getjoules_scaled( */
/* 	uint32_t step_sampled_cputime, ListIterator itr) */
/* { */
/* 	int retval = SLURM_SUCCESS; */

/* 	if (_slurm_acct_gather_energy_init() < 0) */
/* 		return SLURM_ERROR; */

/* 	slurm_mutex_lock( &g_acct_gather_energy_context_lock ); */
/* 	if ( g_acct_gather_energy_context ) { */
/* 		retval = (*(g_acct_gather_energy_context-> */
/* 			    ops.acct_gather_energy_p_getjoules_scaled)) */
/* 			    (step_sampled_cputime, itr); */
/* 	} */
/* 	slurm_mutex_unlock( &g_acct_gather_energy_context_lock ); */
/* 	return retval; */
/* } */

extern int acct_gather_energy_g_update_node_energy(void)
{
	int retval = SLURM_SUCCESS;

	if (_slurm_acct_gather_energy_init() < 0)
		return retval;

	slurm_mutex_lock( &g_acct_gather_energy_context_lock );
	if ( g_acct_gather_energy_context )
		retval = (*(g_acct_gather_energy_context->ops.
			    update_node_energy))();

	slurm_mutex_unlock( &g_acct_gather_energy_context_lock );
	return retval;
}


/* extern int acct_gather_energy_g_setbasewatts(void) */
/* { */
/* 	int retval = SLURM_SUCCESS; */
/* 	slurm_mutex_lock( &g_acct_gather_energy_context_lock ); */
/* 	if ( g_acct_gather_energy_context ) */
/* 		retval = (*(g_acct_gather_energy_context->ops. */
/* 				acct_gather_energy_p_setbasewatts))(); */
/* 	slurm_mutex_unlock( &g_acct_gather_energy_context_lock ); */
/* 	return retval; */
/* } */


extern int acct_gather_energy_g_get_data(enum acct_energy_type data_type,
					 acct_gather_energy_t *energy)
{
	int retval = SLURM_SUCCESS;
	slurm_mutex_lock(&g_acct_gather_energy_context_lock);
	if (g_acct_gather_energy_context)
		retval = (*(g_acct_gather_energy_context->ops.get_data))(
			data_type, energy);
	slurm_mutex_unlock(&g_acct_gather_energy_context_lock);
	return retval;
}

extern int acct_gather_energy_g_set_data(enum acct_energy_type data_type,
					 acct_gather_energy_t *energy)
{
	int retval = SLURM_SUCCESS;
	slurm_mutex_lock(&g_acct_gather_energy_context_lock);
	if (g_acct_gather_energy_context)
		retval = (*(g_acct_gather_energy_context->ops.set_data))(
			data_type, energy);
	slurm_mutex_unlock(&g_acct_gather_energy_context_lock);
	return retval;
}
