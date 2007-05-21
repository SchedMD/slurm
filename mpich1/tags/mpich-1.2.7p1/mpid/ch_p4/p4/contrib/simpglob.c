#include "p4.h"

#define ASIZE 10

main(argc,argv)
int argc;
char **argv;
{
    int i;

    p4_initenv(&argc,argv);
    if (p4_get_my_id() == 0)
        p4_create_procgroup();

    slave();

    p4_wait_for_end();
    p4_dprintf("exiting pgm\n");
}

slave()
{
int i, n;
double a[ASIZE];

    for (i=0; i < ASIZE; i++)
	a[i] = (double) i;

    p4_global_op(44,(char *) a,ASIZE,sizeof(double),p4_dbl_sum_op,P4DBL);

    for (i=0; i < ASIZE; i++)
	p4_dprintf("%4.1f\n",a[i]);
}
