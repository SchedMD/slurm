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
 * This is really two APIs.  We provide an authentication context object
 * which can be bound to any authentication type available in the system
 * and several of which may happily exist together.  The API thunks with
 * a "c_" prefix operate on these.  The typical order of calls is:
 *
 * 	slurm_auth_context_t foo = slurm_auth_context_create( my type );
 *     	void *bar = c_slurm_auth_alloc( foo );
 *	c_slurm_auth_activate( foo, bar, 1 );
 *	c_slurm_auth_verify( foo, bar );
 *	c_slurm_auth_free( foo, bar );
 *	slurm_auth_context_destroy( foo );
 *
 * There is also a parallel API operating on a global authentication
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
 *	g_slurm_auth_activate( bar, 1 );
 *	g_slurm_auth_verify( bar );
 *	g_slurm_auth_free( bar );
 *
 */

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
 * Static bindings for an arbitrary authentication context.  We avoid
 * exposing the API directly to avoid object lifetime issues.
 */
void	*c_slurm_auth_alloc( slurm_auth_context_t c );
void	c_slurm_auth_free( slurm_auth_context_t c, void *cred );
int	c_slurm_auth_activate( slurm_auth_context_t c, void *cred, int secs );
int	c_slurm_auth_verify( slurm_auth_context_t c, void *cred );
uid_t	c_slurm_auth_get_uid( slurm_auth_context_t c, void *cred );
gid_t	c_slurm_auth_get_gid( slurm_auth_context_t c, void *cred );
void	c_slurm_auth_pack( slurm_auth_context_t c, void *cred, Buf buf );
int	c_slurm_auth_unpack( slurm_auth_context_t c, void *cred, Buf buf );
void	c_slurm_auth_print( slurm_auth_context_t c, void *cred, FILE *fp );

/*
 * Prepare the global context.
 */
int slurm_auth_init( void );

/*
 * Static bindings for the global authentication context.
 */
void	*g_slurm_auth_alloc( void );
void	g_slurm_auth_free( void *cred );
int	g_slurm_auth_activate( void *cred, int secs );
int	g_slurm_auth_verify( void *cred );
uid_t	g_slurm_auth_get_uid( void *cred );
gid_t	g_slurm_auth_get_gid( void *cred );
void	g_slurm_auth_pack( void *cred, Buf buf );
int	g_slurm_auth_unpack( void *cred, Buf buf );
void	g_slurm_auth_print( void *cred, FILE *fp );

#endif /*__SLURM_AUTHENTICATION_H__*/
