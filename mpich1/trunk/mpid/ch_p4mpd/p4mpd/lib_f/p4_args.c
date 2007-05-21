#include "p4.h"
#include "p4_sys.h"

#if defined(NEXT)  
#define ARGC  _NXArgc
#define ARGV  _NXArgv
#endif

#if defined(IBM3090)
#endif

#if defined(NEXT)  ||  defined(IBM3090)

extern int ARGC;
extern char **ARGV;

int numargc_()
{
    return(ARGC);
}

int args_(i,arg)
int i;
char *arg;
{
    strcpy(arg,ARGV[i]);
}

#endif
