/*****************************************************************************\
 *  slurm_auth.h - implementation-independent authentication API definitions
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com>
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

#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include "src/common/macros.h"
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/arg_desc.h"

/*
 * WARNING:  Do not change the order of these fields or add additional
 * fields at the beginning of the structure.  If you do, authentication
 * plugins will stop working.  If you need to add fields, add them at the
 * end of the structure.
 */
typedef struct slurm_auth_ops {
        void *       (*create)    ( void *argv[] );
        int          (*destroy)   ( void *cred );
        int          (*verify)    ( void *cred, void *argv[] );
        uid_t        (*get_uid)   ( void *cred );
        gid_t        (*get_gid)   ( void *cred );
        int          (*pack)      ( void *cred, Buf buf );
        void *       (*unpack)    ( Buf buf );
        int          (*print)     ( void *cred, FILE *fp );
        int          (*sa_errno)  ( void *cred );
        const char * (*sa_errstr) ( int slurm_errno );
} slurm_auth_ops_t;

/*
 * Implementation of the authentication context.  Hopefully everything
 * having to do with plugins will be abstracted under here so that the
 * callers can just deal with creating a context and asking for the
 * operations implemented pertinent to that context.
 *
 * auth_type - the string (presumably from configuration files)
 * describing the desired form of authentication, such as "auth/munge"
 * or "auth/kerberos" or "auth/none".
 *
 * plugin_list - the plugin rack managing the loading and unloading of
 * plugins for authencation.
 *
 * cur_plugin - the plugin currently supplying operations to the caller.
 *
 * ops - a table of pointers to functions in the plugin which correspond
 * to the standardized plugin API.  We create this table by text references
 * into the plugin's symbol table.
 */
struct slurm_auth_context {
        char *           auth_type;
        plugrack_t       plugin_list;
        plugin_handle_t  cur_plugin;
        int              auth_errno;
        slurm_auth_ops_t ops;
};

/*
 * A global authentication context.  "Global" in the sense that there's
 * only one, with static bindings.  We don't export it.
 */
static slurm_auth_context_t g_context    = NULL;
static pthread_mutex_t      context_lock = PTHREAD_MUTEX_INITIALIZER;

static slurm_ctl_conf_t     conf;
static pthread_mutex_t      config_lock  = PTHREAD_MUTEX_INITIALIZER;

/*
 * Order of advisory arguments passed to some of the plugins.
 */
static arg_desc_t auth_args[] = {
        { ARG_HOST_LIST },
        { ARG_TIMEOUT },
        { NULL }
};


static char *
get_plugin_dir( void )
{
        slurm_mutex_lock( &config_lock );
        if ( conf.slurmd_port == 0 ) {
                read_slurm_conf_ctl( &conf );
        }
        if ( conf.plugindir == NULL ) {
                conf.plugindir = xstrdup( SLURM_PLUGIN_PATH );
        }
        slurm_mutex_unlock( &config_lock );
        
        return conf.plugindir;
}

static char *
get_auth_type( void )
{
        slurm_mutex_lock( &config_lock );
        if ( conf.slurmd_port == 0 ) {
                read_slurm_conf_ctl( &conf );
        }
        if ( conf.authtype == NULL ) {
                conf.authtype = xstrdup( "auth/none" );
        }
        slurm_mutex_unlock( &config_lock );

        return conf.authtype;
}


/*
 * Resolve the operations from the plugin.
 */
static slurm_auth_ops_t *
slurm_auth_get_ops( slurm_auth_context_t c )
{
        /*
         * These strings must be kept in the same order as the fields
         * declared for slurm_auth_ops_t.
         */
        static const char *syms[] = {
                "slurm_auth_create",
                "slurm_auth_destroy",
                "slurm_auth_verify",
                "slurm_auth_get_uid",
                "slurm_auth_get_gid",
                "slurm_auth_pack",
                "slurm_auth_unpack",
                "slurm_auth_print",
                "slurm_auth_errno",
                "slurm_auth_errstr"
        };
        int n_syms = sizeof( syms ) / sizeof( char * );

        /* Get the plugin list, if needed. */
        if ( c->plugin_list == NULL ) {
                c->plugin_list = plugrack_create();
                if ( c->plugin_list == NULL ) {
                        verbose( "Unable to create a plugin manager" );
                        return NULL;
                }

                plugrack_set_major_type( c->plugin_list, "auth" );
                plugrack_set_paranoia( c->plugin_list, 
				       PLUGRACK_PARANOIA_NONE, 
				       0 );
                plugrack_read_dir( c->plugin_list, get_plugin_dir() );
        }
  
        /* Find the correct plugin. */
        c->cur_plugin = plugrack_use_by_type( c->plugin_list, c->auth_type );
        if ( c->cur_plugin == PLUGIN_INVALID_HANDLE ) {
                verbose( "can't find a plugin for type %s", c->auth_type );
                return NULL;
        }  

        /* Dereference the API. */
        if ( plugin_get_syms( c->cur_plugin,
                              n_syms,
                              syms,
                              (void **) &c->ops ) < n_syms ) {
                verbose( "incomplete plugin detected" );
                return NULL;
        }

        return &c->ops;
}

const arg_desc_t *
slurm_auth_get_arg_desc( void )
{
        return auth_args;
}

static void **
slurm_auth_marshal_args( void *hosts, int timeout )
{
        static int hostlist_idx = -1;
        static int timeout_idx = -1;
        static int count = sizeof( auth_args ) / sizeof( struct _arg_desc ) - 1;
        void **argv;
        
        /* Get indices from descriptor, if we haven't already. */
        if ( ( hostlist_idx == -1 ) &&
             ( timeout_idx == -1 ) ) {
                hostlist_idx = arg_idx_by_name( auth_args, ARG_HOST_LIST );
                timeout_idx = arg_idx_by_name( auth_args, ARG_TIMEOUT );                
        }

        argv = xmalloc( count * sizeof( void * ) );
        
        /* Marshal host list.  Don't quite know how to do this yet. */
        argv[ hostlist_idx ] = hosts;

        /* Marshal timeout.
         * This strange looking code avoids warnings on IA64 */
        argv[ timeout_idx ] = ((char *) NULL) + timeout;

        return argv;
}


slurm_auth_context_t
slurm_auth_context_create( const char *auth_type )
{
        slurm_auth_context_t c;

        if ( auth_type == NULL ) {
                debug3( "slurm_auth_context_create: no authentication type" );
                return NULL;
        }

        c = xmalloc( sizeof( struct slurm_auth_context ) );

        c->auth_errno = SLURM_SUCCESS;

        /* Copy the authentication type. */
        c->auth_type = xstrdup( auth_type );
        if ( c->auth_type == NULL ) {
                debug3( "can't make local copy of authentication type" );
                xfree( c );
                return NULL;
        }

        /* Plugin rack is demand-loaded on first reference. */
        c->plugin_list = NULL;
        c->cur_plugin = PLUGIN_INVALID_HANDLE;  

        return c;
}


static const char *
slurm_auth_generic_errstr( int slurm_errno )         
{
        static struct {
                int err;
                const char *msg;
        } generic_table[] = {
                { SLURM_SUCCESS, "no error" },
                { SLURM_ERROR, "unknown error" },
                { SLURM_AUTH_NOPLUGIN, "no authentication plugin installed" },
                { SLURM_AUTH_BADARG, "bad argument to plugin function" },
                { SLURM_AUTH_MEMORY, "memory management error" },
                { SLURM_AUTH_NOUSER, "no such user" },
                { SLURM_AUTH_INVALID, "authentication credential invalid" },
                { SLURM_AUTH_MISMATCH, "authentication type mismatch" },
                { 0, NULL }
        };

        int i;

        for ( i = 0; ; ++i ) {
                if ( generic_table[ i ].msg == NULL ) return NULL;
                if ( generic_table[ i ].err == slurm_errno )
                        return generic_table[ i ].msg;
        }
}


int
slurm_auth_context_destroy( slurm_auth_context_t c )
{    
        /*
         * Must check return code here because plugins might still
         * be loaded and active.
         */
        if ( c->plugin_list ) {
                if ( plugrack_destroy( c->plugin_list ) != SLURM_SUCCESS ) {
                        return SLURM_ERROR;
                }
        }  

        xfree( c->auth_type );
        xfree( c );
        
        return SLURM_SUCCESS;
}

int
slurm_auth_init( void )
{
        int retval = SLURM_SUCCESS;

        slurm_mutex_lock( &context_lock );

        if ( g_context ) 
                goto done;
        
        g_context = slurm_auth_context_create( get_auth_type() );
        if ( g_context == NULL ) {
                verbose( "cannot create a context for %s", get_auth_type() );
                retval = SLURM_ERROR;
                goto done;
        }
        
        if ( slurm_auth_get_ops( g_context ) == NULL ) {
                verbose( "cannot resolve plugin operations" );
                slurm_auth_context_destroy( g_context );
                g_context = NULL;
                retval = SLURM_ERROR;
        }

 done:
        slurm_mutex_unlock( &context_lock );
        return retval;
}

void
slurm_auth_fini( void )
{
	if ( g_context )
		slurm_auth_context_destroy( g_context );

	slurm_mutex_lock( &config_lock );
	if ( conf.slurmd_port ) {
		free_slurm_conf( &conf );
		conf.slurmd_port = 0;
	}
	slurm_mutex_unlock( &config_lock );
}

/*
 * Static bindings for the global authentication context.  The test
 * of the function pointers is omitted here because the global
 * context initialization includes a test for the completeness of
 * the API function dispatcher.
 */
          
void *
g_slurm_auth_create( void *hosts, int timeout )
{
        void **argv;
        void *ret;
        
        if ( slurm_auth_init() < 0 )
                return NULL;

        if ( ( argv = slurm_auth_marshal_args( hosts, timeout ) ) == NULL ) {
                return NULL;
        }

        ret = (*(g_context->ops.create))( argv );
        xfree( argv );
        return ret;
}

int
g_slurm_auth_destroy( void *cred )
{
        if ( slurm_auth_init() < 0 )
                return SLURM_ERROR;

        return (*(g_context->ops.destroy))( cred );
}

int
g_slurm_auth_verify( void *cred, void *hosts, int timeout )
{
        int ret;
        void **argv;
        
        if ( slurm_auth_init() < 0 )
                return SLURM_ERROR;

        if ( ( argv = slurm_auth_marshal_args( hosts, timeout ) ) == NULL ) {
                return SLURM_ERROR;
        }
        
        ret = (*(g_context->ops.verify))( cred, argv );
        xfree( argv );
        return ret;
}

uid_t
g_slurm_auth_get_uid( void *cred )
{
        if ( slurm_auth_init() < 0 )
                return SLURM_AUTH_NOBODY;
        
        return (*(g_context->ops.get_uid))( cred );
}

gid_t
g_slurm_auth_get_gid( void *cred )
{
        if ( slurm_auth_init() < 0 )
                return SLURM_AUTH_NOBODY;
        
        return (*(g_context->ops.get_gid))( cred );
}

int
g_slurm_auth_pack( void *cred, Buf buf )
{
        if ( slurm_auth_init() < 0 )
                return SLURM_ERROR;
        
        return (*(g_context->ops.pack))( cred, buf );
}

void *
g_slurm_auth_unpack( Buf buf )
{
        if ( slurm_auth_init() < 0 )
                return NULL;
        
        return (*(g_context->ops.unpack))( buf );
}

int
g_slurm_auth_print( void *cred, FILE *fp )
{
        if ( slurm_auth_init() < 0 )
                return SLURM_ERROR;
        
        return (*(g_context->ops.print))( cred, fp );
}

int
g_slurm_auth_errno( void *cred )
{
        if ( slurm_auth_init() < 0 )
                return SLURM_ERROR;

        return (*(g_context->ops.sa_errno))( cred );
}

const char *
g_slurm_auth_errstr( int slurm_errno )
{
        static char auth_init_msg[] = "authentication initialization failure";
        char *generic;
        
        if ( slurm_auth_init() < 0 )
                return auth_init_msg;

        if (( generic = (char *) slurm_auth_generic_errstr( slurm_errno ) ))
                return generic;

        return (*(g_context->ops.sa_errstr))( slurm_errno );
}
