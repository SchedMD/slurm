/*****************************************************************************\
 *  src/slurmd/slurmstepd/pam_ses.c - functions to manage pam session
 *  $Id: pam_ses.c $
 *****************************************************************************
 *  Copyright (C) 2006 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Donna Mecozzi <dmecozzi@llnl.gov>.
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
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

#if HAVE_CONFIG_H
#  include "config.h"
#endif

#include "slurm/slurm_errno.h"
#include "src/slurmd/slurmstepd/pam_ses.h"
#include "src/common/log.h"
#include "src/slurmd/slurmd/slurmd.h"

#ifdef HAVE_PAM

#ifdef HAVE_PAM_PAM_APPL_H
#  include <pam/pam_appl.h>
#  include <pam/pam_misc.h>
#else
#  include <security/pam_appl.h>
#  include <security/pam_misc.h>
#endif

static pam_handle_t *pam_h = NULL;

/*
 * A stack for slurmstepd must be set up in /etc/pam.d
 */
#define SLURM_SERVICE_PAM "slurm"

/*
 * As these functions are currently written, PAM initialization (pam_start)
 * and cleanup (pam_end) are included. If other aspects of PAM are to be used
 * sometime in the future, these calls should be moved because they should only
 * be called once.
 */ 

int
pam_setup (char *user, char *host)
{
	/*
	 * Any application using PAM must provide a conversion function, which
	 * is used for direct communication between a loaded module and the
	 * application. In this case, SLURM does need a communication mechanism,
	 * so the default (or null) conversation function may be used.
	 */
	struct pam_conv conv = {misc_conv, NULL};
        int             rc = 0;

	if (!conf->use_pam)
		return SLURM_SUCCESS;
	/*
	 * SLURM uses PAM to obtain resource limits established by the system
	 * administrator. PAM's session management library is responsible for
	 * handling resource limits. When a PAM session is opened on behalf of
	 * a user, the limits imposed by the sys admin are picked up. Opening
	 * a PAM session requires a PAM handle, which is obatined when the PAM
	 * interface is intialized. (PAM handles are required with essentially
	 * all PAM calls.) It's also necessary to have the users PAM credentials
	 * to open a user session.
 	 */
        if ((rc = pam_start (SLURM_SERVICE_PAM, user, &conv, &pam_h))
			!= PAM_SUCCESS) {
                error ("pam_start: %s", pam_strerror(pam_h, rc));
                return SLURM_ERROR;
        } else if ((rc = pam_set_item (pam_h, PAM_USER, user))
			!= PAM_SUCCESS) {
                error ("pam_set_item USER: %s", pam_strerror(pam_h, rc));
                return SLURM_ERROR;
        } else if ((rc = pam_set_item (pam_h, PAM_RUSER, user))
			!= PAM_SUCCESS) {
                error ("pam_set_item RUSER: %s", pam_strerror(pam_h, rc));
                return SLURM_ERROR;
        } else if ((rc = pam_set_item (pam_h, PAM_RHOST, host))
			!= PAM_SUCCESS) {
                error ("pam_set_item HOST: %s", pam_strerror(pam_h, rc));
              return SLURM_ERROR;
        } else if ((rc = pam_setcred (pam_h, PAM_ESTABLISH_CRED))
			!= PAM_SUCCESS) {
                error ("pam_setcred: %s", pam_strerror(pam_h, rc));
                return SLURM_ERROR;
        } else if ((rc = pam_open_session (pam_h, 0)) != PAM_SUCCESS) {
                error("pam_open_session: %s", pam_strerror(pam_h, rc));
                return SLURM_ERROR;
        }

	return SLURM_SUCCESS;

}


void
pam_finish ()
{
        int             rc = 0;

	/* 
	 * Allow PAM to clean up its state by closing the user session and
	 * ending the association with PAM.
	 */

	if (!conf->use_pam)
		return;

        if (pam_h != NULL) {
		/*
		 * Log any errors, but there's no need to return a SLURM error.
		 */
                if ((rc = pam_close_session (pam_h, 0)) != PAM_SUCCESS) {
                        error("pam_close_session: %s", pam_strerror(pam_h, rc));
                } else if (pam_end (pam_h, rc) != PAM_SUCCESS) {
                        error("pam_end: %s", pam_strerror(pam_h, rc));
                }
        }
}

#else  /* HAVE_PAM */

int pam_setup (char *user, char *host)
{
	/* Don't have PAM support, do nothing. */
	return SLURM_SUCCESS;
}

void pam_finish ()
{
	/* Don't have PAM support, do nothing. */
}

#endif /* HAVE_PAM */
