
#include <src/common/log.h>

int main(int ac, char **av)
{
	/* test elements */
	char string[]  = "test string";
	double f       = 9876543210.0123456;
	int   i        = 67890;
	int   negi     = -i;
	unsigned int u = 1234;
	char *p        = NULL;

	log_options_t log_opts = LOG_OPTS_INITIALIZER;

	log_opts.stderr_level = LOG_LEVEL_DEBUG2;

	/* test log to stderr when log not initialized */
	error("test: log not initialized.");

	/* now initialize log: */
	log_init("log-test", log_opts, 0, NULL);

	error  ("error message");
	info   ("informational");
	verbose("verbose");
	debug  ("debug level 1");
	debug2 ("debug level 2");
	debug3 ("ERROR: Should not see this.");

	info   ("testing print of null pointer: %p = %s",  p, p);

	info   ("double: %18.7f int: %05i string `%s'", f, i, string);

	info   ("unsigned: %u   int: % 08d", u, negi);

	/* for now, this test passes if we make it through without 
	 * dumping core
	 */
	return 0;
}
	


