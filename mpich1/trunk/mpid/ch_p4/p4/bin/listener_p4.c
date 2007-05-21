#include "p4.h"
#include "p4_defs.h"
#include "p4_globals.h"
#include "p4_sys_funcs.h"

/*
 *  listener_p4
 *
 *  args:
 *    debug_level
 *    max_connections
 *    listening_fd
 *    slave_fd
 */

main(argc, argv)
int argc;
char **argv;
{
#if !defined(IPSC860) && !defined(CM5)
    sprintf(whoami_p4, "list_%d", getpid());

    if (argc != 5)
	p4_error("listener_p4: invalid argc", argc);

    debug_level = atoi(argv[1]);
    p4_dprintfl(70, "got: %s %s %s %s\n",
		argv[1], argv[2], argv[3], argv[4]);

    p4_global = (struct p4_global_data *)
		p4_shmalloc(sizeof(struct p4_global_data));
    p4_global->max_connections = atoi(argv[2]);

    p4_local = alloc_local_listener();

    listener_info = alloc_listener_info();
    listener_info->listening_fd = atoi(argv[3]);
    listener_info->slave_fd = atoi(argv[4]);

    listener();
#endif
}
