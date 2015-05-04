/* helper.h
 *
 * Some helper functions needed for pam_slurm_adopt.c */

#define PAM_SM_ACCOUNT
#include <security/pam_modules.h>
#include <security/_pam_macros.h>

extern void send_user_msg(pam_handle_t *pamh, const char *msg);
extern void libpam_slurm_init (void);
extern void libpam_slurm_fini (void);
