/**********************************************************************\
 * Simple I/O test program for SLURM regression test1.18
 * Print "waiting\n" to stdout and wait for "exit" as stdin
\**********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

main (int argc, char **argv) 
{
	char in_line[10];
	int i;

	fprintf(stdout, "WAITING\n");
	fflush(stdout);

	for (i=0; i<sizeof(in_line); ) {
		in_line[i] = getc(stdin);
		if ((in_line[i] < 'a') ||
		    (in_line[i] > 'z'))
			i = 0;
		else if (strncmp(in_line, "exit", 4) == 0)
			exit(0);
		else
			i++;
	}

	fprintf(stderr, "Invalid input\n");
	exit(1);
}
