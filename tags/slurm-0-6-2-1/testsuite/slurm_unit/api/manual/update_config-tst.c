#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <slurm/slurm.h>
#include "testsuite/dejagnu.h"

#ifndef true
#define false 0
#define true  1
#endif

#define NAME_LEN 128
static int _getnodename (char *name, size_t len);

/* main is used here for module testing purposes only */
/* DO NOT RUN AGAINST PRODUCTION NODES, IT CAN MESS UP STATE */
int
main (int argc, char *argv[]) {
	int error_code;
	update_part_msg_t	part_update1 ;
	update_part_msg_t	part_update2 ;
	update_node_msg_t	node_update1 ;
	update_node_msg_t	node_update2 ;
	char node_name[NAME_LEN];

	slurm_init_part_desc_msg ( &part_update1 );
	slurm_init_part_desc_msg ( &part_update2 );
	part_update1 . name = "batch" ;
	part_update2 . name = "batch" ;
	part_update1 . state_up = false ;
	part_update2 . state_up = true ; 

	_getnodename(node_name, NAME_LEN);
	node_update1 . node_names = node_name ;
	node_update2 . node_names = node_name ;
	node_update1 . node_state = NODE_STATE_DRAINING ;
	node_update2 . node_state = NODE_STATE_IDLE ; 

	error_code = slurm_update_partition ( &part_update1);
	if (error_code)
		slurm_perror ("slurm_update_partition #1");

	error_code = slurm_update_partition ( &part_update2);
	if (error_code)
		slurm_perror ("slurm_update_partition #2");

	error_code = slurm_update_node ( &node_update1);
	if (error_code)
		slurm_perror ("slurm_update_node #1");

	error_code = slurm_update_node ( &node_update2);
	if (error_code)
		slurm_perror ("slurm_update_node #2");

	return (errno);
}


/* getnodename - equivalent to gethostname, but return only the first 
 * component of the fully qualified name 
 * (e.g. "linux123.foo.bar" becomes "linux123") 
 */
static int _getnodename (char *name, size_t len)
{
	int error_code, name_len;
	char *dot_ptr, path_name[1024];

	error_code = gethostname (path_name, sizeof(path_name));
	if (error_code)
		return error_code;

	dot_ptr = strchr (path_name, '.');
	if (dot_ptr == NULL)
		dot_ptr = path_name + strlen(path_name);
	else
		dot_ptr[0] = '\0';

	name_len = (dot_ptr - path_name);
	if (name_len > len)
		return ENAMETOOLONG;

	strcpy (name, path_name);
	return 0;
}
