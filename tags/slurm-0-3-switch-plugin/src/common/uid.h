#ifndef __SLURM_UID_UTILITY_H__
#define __SLURM_UID_UTILITY_H__

#define NFS_NOBODY 65534

int	is_digit_string( char *str );
uid_t	uid_from_name( char *name );
gid_t	gid_from_name( char *name );

#endif /*__SLURM_UID_UTILITY_H__*/
