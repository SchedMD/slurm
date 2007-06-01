/* Procedures for recording and printing test results */

#include <stdio.h>
#include <string.h>
#include "test.h"
#include "mpi.h"

#if defined(USE_STDARG)
#include <stdarg.h>
#endif

static int tests_passed = 0;
static int tests_failed = 0;
static char failed_tests[255][81];
static char suite_name[255];
FILE *fileout = NULL;

void Test_Init(suite, rank)
char *suite;
int rank;
{
    char filename[512];

    sprintf(filename, "%s-%d.out", suite, rank);
    strncpy(suite_name, suite, 255);
    fileout = fopen(filename, "w");
    if (!fileout) {
	fprintf( stderr, "Could not open %s on node %d\n", filename, rank );
	MPI_Abort( MPI_COMM_WORLD, 1 );
    }
}

#ifdef USE_STDARG
void Test_Printf(char *format, ...)
{
    va_list arglist;

    va_start(arglist, format);
    (void)vfprintf(fileout, format, arglist);
    va_end(arglist);
}
#else
void Test_Printf(va_alist)
va_dcl
{
    char *format;
    va_list arglist;

    va_start(arglist);
    format = va_arg(arglist, char *);
    (void)vfprintf(fileout, format, arglist);
    fflush(fileout);
    va_end(arglist);
}
#endif

void Test_Message(mess)
char *mess;
{
    fprintf(fileout, "[%s]: %s\n", suite_name, mess);
    fflush(fileout);
}

void Test_Failed(test)
char *test;
{
    fprintf(fileout, "[%s]: *** Test '%s' Failed! ***\n", suite_name, test);
    strncpy(failed_tests[tests_failed], test, 81);
    fflush(fileout);
    tests_failed++;
}

void Test_Passed(test)
char *test;
{
#ifdef VERBOSE
    fprintf(fileout, "[%s]: Test '%s' Passed.\n", suite_name, test);
    fflush(fileout);
#endif
    tests_passed++;
}

int Summarize_Test_Results()
{
#ifdef VERBOSE
    fprintf(fileout, "For test suite '%s':\n", suite_name);
#else
    if (tests_failed > 0)
#endif
    {
	fprintf(fileout, "Of %d attempted tests, %d passed, %d failed.\n", 
		tests_passed + tests_failed, tests_passed, tests_failed);
    }
    if (tests_failed > 0) {
	int i;

	fprintf(fileout, "*** Tests Failed:\n");
	for (i = 0; i < tests_failed; i++)
	    fprintf(fileout, "*** %s\n", failed_tests[i]);
    }
    return tests_failed;
}

void Test_Finalize()
{
    fflush(fileout);
    fclose(fileout);
}

#include "mpi.h"
/* Wait for every process to pass through this point.  This test is used
   to make sure that all processes complete, and that a test "passes" because
   it executed, not because it some process failed.  
 */
void Test_Waitforall( )
{
int m, one, myrank, n;

MPI_Comm_rank( MPI_COMM_WORLD, &myrank );
MPI_Comm_size( MPI_COMM_WORLD, &n );
one = 1;
MPI_Allreduce( &one, &m, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD );

if (m != n) {
    printf( "[%d] Expected %d processes to wait at end, got %d\n", myrank, 
	    n, m );
    }
if (myrank == 0) 
    printf( " No Errors\n" );
}
