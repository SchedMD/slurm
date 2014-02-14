/*****************************************************************************\
 *  smd.c - Command interface for fault tolerant application support
 *****************************************************************************
 *  Copyright (C) 2013 SchedMD LLC
 *  Written by Morris Jette <jette@schedmd.com>
 *  Written by David Bigagli <david@schedmd.com>
 *  All rights reserved
\*****************************************************************************/

#include "src/smd/smd.h"

struct nonstop_params *params;

/* main()
 */
int
main(int argc, char **argv)
{
	int cc;

	params = calloc(1, sizeof(struct nonstop_params));
	if (params == NULL) { /* ouch bad start.. */
		perror("calloc()");
		return -1;
	}

	/* Set program controlling parameters.
	 */
	cc = set_params(argc, argv, params);
	if (cc < 0)
		goto bye;

	/* Check that all parameters are all right.
	 */
	cc = check_params(params);
	if (cc < 0)
		goto bye;

	/* See if we are requested to do automatic failure
	 * handling for the job or manually execute what
	 * the user is asking.
	 */
	if (params->handle_failed
	    || params->handle_failing)
		cc = automatic();
	else
		cc = manual();

bye:

	free_params(params);
	freeit(params);

	return cc;
}
