/*****************************************************************************\
 * slurm_authentication.h - implementation-independent authentication API.
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by AUTHOR <AUTHOR@llnl.gov>.
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

#if HAVE_CONFIG_H
#  include <config.h>
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

/*
 * Here are included the implementation details of the
 * authentication scheme.  Among other things, that file must define
 * slurm_auth_credentials_t.  That file is identified at compile
 * time.
 */
#include <src/common/slurm_auth_impl.h>


/*
 * Allocation a new set of credentials from the free store.  Returns
 * NULL if no allocation is possible.
 */
slurm_auth_credentials_t *slurm_auth_alloc_credentials( void );

/*
 * Deallocate credentials allocated with slurm_auth_alloc_credentials().
 */
void slurm_auth_free_credentials( slurm_auth_credentials_t *cred );

/*
 * Populate the credentials and validate them for use.
 * cred - The credential to activate.
 * seconds_to_live - the number of seconds after validation that
 *	the credential expires
 * Returns SLURM_ERROR if the credentials could not be populated,
 * validated, or if the authentication daemon is not running.
 */
int slurm_auth_activate_credentials( slurm_auth_credentials_t *cred,
				     time_t seconds_to_live );

/*
 * Verify the credentials.  Returns SLURM_ERROR if the credentials are
 * invalid or has expired.
 */
int slurm_auth_verify_credentials( slurm_auth_credentials_t *cred );

/*
 * Extract user and group identification from the credentials.
 * They are trustworthy only if the credentials have first been
 * verified.
 */
int slurm_auth_uid( slurm_auth_credentials_t *cred );
int slurm_auth_gid( slurm_auth_credentials_t *cred );

/*
 * Methods for packing and unpacking the credentials for transport.
 */
void slurm_auth_pack_credentials( slurm_auth_credentials_t *cred,
				  void **buffer,
				  uint32_t *length );
void slurm_auth_unpack_credentials( slurm_auth_credentials_t **cred,
				    void **buffer,
				    uint32_t *length );

#if DEBUG
void slurm_auth_print_credentials( slurm_auth_credentials_t *cred );
#endif

#endif /*__SLURM_AUTHENTICATION_H__*/
