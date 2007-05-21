/* 
   This is a modification of an example provided by Ralph Butler.  
   When using gcc (on some platforms?), this allows the MPI error handler 
   to provide a traceback.

   Modified by William Gropp, December, 2000.

   This example is from Ralph Butler.  It uses a feature of gcc to get
   information on the call stack

   build as
   gcc -o bktrace backtrace.c

 */

#include <stdio.h>
#include <stdarg.h>
void MPIR_Print_backtrace( char *, int, char *, ... );

#ifdef TEST_BACKTRACE
int f1(), f2();

main()
{
    f1();
    return 0;
}

f1()
{
    f2();
}

f2()
{
    MPIR_Print_backtrace("conftest",1,"this is some user message\n");
}
#endif

/* Local variables used for the traceback.  Should we attempt to allocate
 these? */
#define MAX_SYMBOLS 512
static char savedExecutable[1025] = "\0";
static unsigned int  idx, address[1024];
static char loc[1024][MAX_SYMBOLS];
static char procname[1024][MAX_SYMBOLS];

void MPIR_Print_backtrace( char *executable, int print_flag, char *fmt, ... )
{
    va_list ap;
    FILE *fp;
    int i, j, rc;
    void *ra;
    char type[8], cmd[101], input_line[101];

    if (!print_flag) 
        return;
    va_start( ap, fmt );
    vfprintf( stderr, fmt, ap );
    va_end( ap );
    fflush( stderr );
    if (!executable) {
	if (!savedExecutable[0]) return;
	executable = savedExecutable;
    }
    sprintf(cmd,"nm -l -g -n %s\n",executable);
    if ((fp = popen(cmd,"r")) == NULL)
    {
        fprintf(stderr,"Unable to find symbols for %s\n",executable);
	fflush( stderr );
	return;
    }
    idx = 0;
    while (fgets(input_line,100,fp) != NULL && idx < MAX_SYMBOLS)
    {
	if (!isdigit(input_line[0]))
	    continue;
	rc = sscanf(input_line,"%x %s %s %s\n",
		    &address[idx],&type,&procname[idx],&loc[idx]);
	if (strcmp(type,"T") != 0)
	    continue;
	idx++;
    }
    fclose(fp);

    fprintf( stderr, "backtrace:\n");
    /* fprintf( stderr, "    %s %d\n",__FILE__,__LINE__); */
    fflush( stderr );

    ra = (void *) 1;
    for (i=0; ra; i++)
    {
	switch (i)
	{
	    case 0:
		ra = __builtin_return_address(0);
		break;
	    case 1:
		ra = __builtin_return_address(1);
		break;
	    case 2:
		ra = __builtin_return_address(2);
		break;
	    case 3:
		ra = __builtin_return_address(3);
		break;
	    default:
		ra = 0;
	}
	/* printf("GOT RA=%x\n",ra); */
	for (j=0; ra && ra > (void *)address[j] && j < idx;  j++)
	{
	    /* printf("comparing %x %x \n",ra,address[j]); */
	}
	if (j > 0 && j < idx)
	{
	    fprintf(stderr,"    %s %s %x\n",procname[j-1],loc[j-1],address[j-1]);
	    fflush( stderr );
	}
    }
}

void MPIR_Save_executable_name( const char *name )
{
    strncpy( savedExecutable, name, 1024 );
    savedExecutable[1024] = 0;
}
