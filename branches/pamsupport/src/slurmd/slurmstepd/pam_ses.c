/*****************************************************************************\
 *  src/slurmd/slurmstepd/pam_ses.c - functions to manage pam session
 *  $Id: pam_ses.c $
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Donna Mecozzi <dmecozzi@llnl.gov>.
 *  UCRL-CODE-217948.
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


/* #define SLURM_SERVICE_PAM "slurmstepd"   */
#define SLURM_SERVICE_PAM "tbsshep"

#if (HAVE_PAM)

#include <security/pam_appl.h>
#include <security/pam_misc.h>

#include "slurm/slurm_errno.h"


/*
 *  Any application using PAM must provide a conversion function, which is used for
 *  direct communication between a loaded module and the application. In this case,
 *  SLURM does need a communication mechanism, so the default (or null) conversation
 *  function may be used.
 */
static struct pam_conv conv = {
        misc_conv,
        NULL
};

/*
 * As these functions are currently written, the PAM initialization (pam_start)
 * and cleanup (pam_end) are included.  If other aspects of PAM are to be used
 * sometime in the future, these calls should be moved out, as they should only
 * be done once.
 */ 

int
pam_setup (pam_handle_t **pam_h, char *user, char *host)
{
        int             rc = 0;

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
        if ((rc = pam_start (SLURM_SERVICE_PAM, user, &conv, pam_h))
			!= PAM_SUCCESS) {
                error ("pam_start: %s", pam_strerror(*pam_h, rc));
                return SLURM_ERROR;
        } else if ((rc = pam_set_item (*pam_h, PAM_USER, user))
			!= PAM_SUCCESS) {
                error ("pam_set_item USER: %s", pam_strerror(*pam_h, rc));
                return SLURM_ERROR;
        } else if ((rc = pam_set_item (*pam_h, PAM_RUSER, user))
			!= PAM_SUCCESS) {
                error ("pam_set_item RUSER: %s", pam_strerror(*pam_h, rc));
                return SLURM_ERROR;
        } else if ((rc = pam_set_item (*pam_h, PAM_RHOST, host))
			!= PAM_SUCCESS) {
                error ("pam_set_item HOST: %s", pam_strerror(*pam_h, rc));
                return SLURM_ERROR;
        } else if ((rc = pam_setcred (*pam_h, PAM_ESTABLISH_CRED))
			!= PAM_SUCCESS) {
                error ("pam_setcred: %s", pam_strerror(*pam_h, rc));
                return SLURM_ERROR;
        } else
                 if ((rc = pam_open_session (*pam_h, 0)) != PAM_SUCCESS) {
                        error("pam_open_session: %s", pam_strerror(*pam_h, rc));
                        return SLURM_ERROR;
        }

	return SLURM_SUCCESS;

}



void
pam_finish (pam_handle_t *pam_h)
{
        int             rc = 0;

/*     FIX FIOR SLURM
 *      Close the pam session and end the connection to pam. Because the pam
 *      handle is opaque data this is the place where this is done, although
 *      it might be preferred (for user tracking reasons) if this were done
 *      when the user's job terminates.
 */

	debug ("in pam_finish pam_h = %d, &pam_h = %d \n",
                pam_h, &pam_h);
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


#endif  /* HAVE_PAM */
