#include <stdlib.h>
#include <pwd.h>
#include <grp.h>
#include <ctype.h>

#include "uid.h"

int
is_digit_string( char *str )
{
	char *p;

	for ( p = str; *p; ++p ) {
		if ( ! isdigit( *p ) ) return 0;
	}
	return 1;
}

uid_t
uid_from_name( char *name )
{
	struct passwd *p = getpwnam( name );
	return p ? p->pw_uid : NFS_NOBODY;
}

gid_t
gid_from_name( char *name )
{
	struct group *g = getgrnam( name );
	return g ? g->gr_gid : NFS_NOBODY;
}

