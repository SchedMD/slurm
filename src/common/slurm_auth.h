/*****************************************************************************\
 *  slurm_auth.h - implementation-independent authentication API definitions
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/

#ifndef __SLURM_AUTHENTICATION_H__
#define __SLURM_AUTHENTICATION_H__

#include <stdio.h>

#if HAVE_CONFIG_H
#  include "config.h"
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif  /* HAVE_INTTYPES_H */
#else   /* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif  /*  HAVE_CONFIG_H */

#include "src/common/plugrack.h"
#include "src/common/pack.h"

/*
 * This API operates on a global authentication
 * context, one per application.  The API thunks with the "g_" prefix
 * operate on that global instance.  It is initialized implicitly if
 * necessary when any API thunk is called, or explicitly with
 *
 *	slurm_auth_init();
 *
 * The authentication type and other parameters are taken from the
 * system's global configuration.  A typical order of calls is:
 *
 *	void *bar = g_slurm_auth_alloc();
 *	g_slurm_auth_verify( bar );
 *	g_slurm_auth_free( bar );
 *
 */

enum {
    SLURM_AUTH_NOPLUGIN,
    SLURM_AUTH_BADARG,
    SLURM_AUTH_MEMORY,
    SLURM_AUTH_NOUSER,
    SLURM_AUTH_INVALID,
    SLURM_AUTH_MISMATCH,

    SLURM_AUTH_FIRST_LOCAL_ERROR	/* Always keep me last. */
};

/*
 * SLURM authentication context opaque type.
 */
typedef struct slurm_auth_context * slurm_auth_context_t;

/*
 * Create an authentication context.
 *
 * Returns NULL on failure.
 */
slurm_auth_context_t slurm_auth_context_create( const char *auth_type );

/*
 * Destroy an authentication context.  Any jumptables returned by
 * calls to slurm_auth_get_ops() for this context will silently become
 * invalid, and calls to their functions may result in core dumps and
 * other nasty behavior.
 *
 * Returns a SLURM errno.
 */
int slurm_auth_context_destroy( slurm_auth_context_t ctxt );

/*
 * This is what the UID and GID accessors return on error.  The value
 * is currently RedHat Linux's ID for the user "nobody".
 */
#define SLURM_AUTH_NOBODY		99

/*
 * Prepare the global context.
 */
int slurm_auth_init( void );

/*
 * Static bindings for the global authentication context.
 */
void	*g_slurm_auth_alloc( void );
int	g_slurm_auth_free( void *cred );
int	g_slurm_auth_activate( void *cred );
int	g_slurm_auth_verify( void *cred );
uid_t	g_slurm_auth_get_uid( void *cred );
gid_t	g_slurm_auth_get_gid( void *cred );
int	g_slurm_auth_pack( void *cred, Buf buf );
int	g_slurm_auth_unpack( void *cred, Buf buf );
int	g_slurm_auth_print( void *cred, FILE *fp );
int	g_slurm_auth_errno( void *cred );
const char *g_slurm_auth_errstr( int slurm_errno );

#endif /*__SLURM_AUTHENTICATION_H__*/
