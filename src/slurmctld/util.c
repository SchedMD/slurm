/* $Id$ */

/* generic utility functions used by slurmctld:
 *
 */

#include <string.h>

#include <src/slurmctld/util.h>

/*
 * block_or_cycle - map string into integer
 * input: in_string: pointer to string containing "BLOCK" or "CYCLE"
 * output: returns DIST_BLOCK for "BLOCK", DIST_CYCLE for "CYCLE", -1 otherwise
 */
enum task_dist
block_or_cycle (char *in_string)
{
	if (strcmp (in_string, "BLOCK") == 0) return DIST_BLOCK;
	if (strcmp (in_string, "CYCLE")  == 0) return DIST_CYCLE;
	return -1;
}

/*
 * yes_or_no - map string into integer
 * input: in_string: pointer to string containing "YES" or "NO"
 * output: returns 1 for "YES", 0 for "NO", -1 otherwise
 */
int
yes_or_no (char *in_string)
{
	if (strcmp (in_string, "YES") == 0) return 1;
	if (strcmp (in_string, "NO")  == 0) return 0;
	return -1;
}



