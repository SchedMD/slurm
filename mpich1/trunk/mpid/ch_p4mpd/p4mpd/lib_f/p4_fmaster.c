
#include "p4.h"
#include "p4_sys.h"
#include "p4_fort.h"

VOID p4init_()
{
    int argidx = 0;
    char *argv[200];
    char *q;
    int argcnt;
    
    /*  DO NOT do any dprintfl's until after the p4_initenv below */
    numargc_(&argcnt);
    while (argidx < argcnt)
    {
        q = (char *)malloc(200);
        /* args Fortran subroutine */
        fflush(stdout);
        args_(&argidx,q);
        argv[argidx] = q;
        q = (char *)index(argv[argidx],' ');
        *q = '\0';
        ++argidx;
    }
    p4_initenv(&argidx,argv);
    p4_dprintfl(30, "exit fortran p4init\n");
}

VOID p4crpg_()
{
    if (p4_create_procgroup() < 0)
        p4_error("p4crpg_: p4_create_procgroup failed",0);
}

VOID p4cleanup_()
{
    p4_wait_for_end();
}

