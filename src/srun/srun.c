/* */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifdef HAVE_PTHREAD_H
#  include <pthread.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include <opt.h>
#include <env.h>


int
main(int ac, char **av)
{
	/* set default options, process commandline arguments, and
	 * verify some basic values
	 */
	initialize_and_process_args(ac, av);

	exit(0);
}

static void 
create_job_spec()
{

}



