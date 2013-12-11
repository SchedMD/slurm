/*****************************************************************************\
 *  slurm_auth.c - implementation-independent authentication API definitions
 *****************************************************************************
 *  Copyright (C) 2002-2007 The Regents of the University of California.
 *  Copyright (C) 2008-2009 Lawrence Livermore National Security.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jay Windley <jwindley@lnxi.com>
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

#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include "src/common/macros.h"
#include "src/common/xmalloc.h"
#include "src/common/xassert.h"
#include "src/common/xstring.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_protocol_api.h"
#include "src/common/plugin.h"
#include "src/common/plugrack.h"
#include "src/common/arg_desc.h"

static bool auth_dummy = false;	/* for security testing */
static bool init_run = false;

/*
 * WARNING:  Do not change the order of these fields or add additional
 * fields at the beginning of the structure.  If you do, authentication
 * plugins will stop working.  If you need to add fields, add them at the
 * end of the structure.
 */
typedef struct slurm_auth_ops {
        void *       (*create)    ( void *argv[], char *auth_info );
        int          (*destroy)   ( void *cred );
        int          (*verify)    ( void *cred, char *auth_info );
        uid_t        (*get_uid)   ( void *cred, char *auth_info );
        gid_t        (*get_gid)   ( void *cred, char *auth_info );
        int          (*pack)      ( void *cred, Buf buf );
        void *       (*unpack)    ( Buf buf );
        int          (*print)     ( void *cred, FILE *fp );
        int          (*sa_errno)  ( void *cred );
        const char * (*sa_errstr) ( int slurm_errno );
} slurm_auth_ops_t;
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

/*
 * A global authentication context.  "Global" in the sense that there's
 * only one, with static bindings.  We don't export it.
 */
static slurm_auth_ops_t ops;
static plugin_context_t *g_context = NULL;
static pthread_mutex_t      context_lock = PTHREAD_MUTEX_INITIALIZER;

static void **
_slurm_auth_marshal_args(void *hosts, int timeout)
{
        void **argv;

        argv = xmalloc(ARG_COUNT * sizeof(void *));
        argv[ARG_HOST_LIST] = hosts;
        /* This strange looking code avoids warnings on IA64 */
        argv[ARG_TIMEOUT] = ((char *) NULL) + timeout;

        return argv;
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
		{ SLURM_AUTH_VERSION, "authentication version too old" },
		{ 0, NULL }
        };

        int i;

        for ( i = 0; ; ++i ) {
                if ( generic_table[ i ].msg == NULL )
			return NULL;
                if ( generic_table[ i ].err == slurm_errno )
                        return generic_table[ i ].msg;
        }
}

extern int slurm_auth_init( char *auth_type )
{
        int retval = SLURM_SUCCESS;
	char *type = NULL;
	char *plugin_type = "auth";

	if (init_run && g_context)
                return retval;

	slurm_mutex_lock(&context_lock);

        if (g_context)
                goto done;

	if (auth_type)
		slurm_set_auth_type(auth_type);

	type = slurm_get_auth_type();

	if (strcmp(type, "auth/dummy") == 0) {
		info( "warning: %s plugin selected", type);
		auth_dummy = true;
		goto done;
	}

	g_context = plugin_context_create(
		plugin_type, type, (void **)&ops, syms, sizeof(syms));

	if (!g_context) {
		error("cannot create %s context for %s", plugin_type, type);
		retval = SLURM_ERROR;
		goto done;
	}
	init_run = true;

done:
	xfree(type);
        slurm_mutex_unlock(&context_lock);
        return retval;
}

/* Release all global memory associated with the plugin */
extern int
slurm_auth_fini( void )
{
	int rc;

	if (!g_context)
		return SLURM_SUCCESS;

	init_run = false;
	rc = plugin_context_destroy(g_context);
	g_context = NULL;
	return rc;
}

/*
 * Static bindings for the global authentication context.  The test
 * of the function pointers is omitted here because the global
 * context initialization includes a test for the completeness of
 * the API function dispatcher.
 */

void *
g_slurm_auth_create( void *hosts, int timeout, char *auth_info )
{
        void **argv;
        void *ret;

	if ( slurm_auth_init(NULL) < 0 )
		return NULL;

	if ( auth_dummy )
		return xmalloc(0);

        if ( ( argv = _slurm_auth_marshal_args(hosts, timeout) ) == NULL ) {
                return NULL;
        }

        ret = (*(ops.create))( argv, auth_info );
        xfree( argv );
        return ret;
}

int
g_slurm_auth_destroy( void *cred )
{
        if ( slurm_auth_init(NULL) < 0 )
                return SLURM_ERROR;

	if ( auth_dummy )	/* don't worry about leak in testing */
		return SLURM_SUCCESS;

        return (*(ops.destroy))( cred );
}

int
g_slurm_auth_verify( void *cred, void *hosts, int timeout, char *auth_info )
{
        int ret;

        if ( slurm_auth_init(NULL) < 0 )
                return SLURM_ERROR;

	if ( auth_dummy )
		return SLURM_SUCCESS;

        ret = (*(ops.verify))( cred, auth_info );
        return ret;
}

uid_t
g_slurm_auth_get_uid( void *cred, char *auth_info )
{
	if (( slurm_auth_init(NULL) < 0 ) || auth_dummy )
                return SLURM_AUTH_NOBODY;

        return (*(ops.get_uid))( cred, auth_info );
}

gid_t
g_slurm_auth_get_gid( void *cred, char *auth_info )
{
	if (( slurm_auth_init(NULL) < 0 ) || auth_dummy )
                return SLURM_AUTH_NOBODY;

        return (*(ops.get_gid))( cred, auth_info );
}

int
g_slurm_auth_pack( void *cred, Buf buf )
{
        if ( slurm_auth_init(NULL) < 0 )
                return SLURM_ERROR;

	if ( auth_dummy )
		return SLURM_SUCCESS;

        return (*(ops.pack))( cred, buf );
}

void *
g_slurm_auth_unpack( Buf buf )
{
	if (( slurm_auth_init(NULL) < 0 ) || auth_dummy )
                return NULL;

        return (*(ops.unpack))( buf );
}

int
g_slurm_auth_print( void *cred, FILE *fp )
{
        if ( slurm_auth_init(NULL) < 0 )
                return SLURM_ERROR;

	if ( auth_dummy )
		return SLURM_SUCCESS;

        return (*(ops.print))( cred, fp );
}

int
g_slurm_auth_errno( void *cred )
{
        if (( slurm_auth_init(NULL) < 0 ) || auth_dummy )
                return SLURM_ERROR;

        return (*(ops.sa_errno))( cred );
}

const char *
g_slurm_auth_errstr( int slurm_errno )
{
        static char auth_init_msg[] = "authentication initialization failure";
        char *generic;

	if (( slurm_auth_init(NULL) < 0 ) || auth_dummy )
		return auth_init_msg;

        if (( generic = (char *) slurm_auth_generic_errstr( slurm_errno ) ))
                return generic;

        return (*(ops.sa_errstr))( slurm_errno );
}
