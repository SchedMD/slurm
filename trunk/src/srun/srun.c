/* $Id$ */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#ifdef HAVE_PTHREAD_H
#  include <pthread.h>
#endif

#include <stdio.h>
#include <stdlib.h>

#include <src/common/log.c>

#include <opt.h>
#include <env.h>


/*
 * forward declaration of static funcs
 */
static void create_job_spec(void);

int
main(int ac, char **av)
{
	log_options_t logopt = LOG_OPTS_STDERR_ONLY;

	log_init(xbasename(av[0]), logopt, 0, NULL);

	/* set default options, process commandline arguments, and
	 * verify some basic values
	 */
	initialize_and_process_args(ac, av);

	/* now global "opt" should be filled in and available,
	 * create a job_spec from opt
	 */

	exit(0);
}

