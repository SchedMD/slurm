#include "p4.h"

#define MAXLEN 500
#define MAXPROCS 256

struct globmem {
    int length;
    int a[MAXLEN], b[MAXLEN], c[MAXLEN];
    int num_added[MAXPROCS];
    int sub;
    int slave_id;
    p4_lock_t go_lock;
    p4_askfor_monitor_t askfor;
} *glob;


int getprob(v)			/* return next available subscript */
P4VOID *v;
{
int rc = 1;   /* any non-zero means NO problem found */
int *p;

    p = (int *) v;
    if (glob->sub < glob->length)
    {
	*p = glob->sub++;
	rc = 0;    /* FOUND a problem */
    }
    return(rc);
}

P4VOID reset()
{
}

slave()
{
    work();
}

main(argc,argv)
int argc;
char **argv;
{
int i, j, n, start, end;

    p4_initenv(&argc,argv);   /* args not used but passed for compat */

    glob = (struct globmem *) p4_shmalloc(sizeof(struct globmem));

    glob->sub = 0;
    glob->slave_id = 0;
    p4_lock_init(&(glob->go_lock));
    /* p4_lock(&(glob->go_lock)); */
    p4_askfor_init(&(glob->askfor));

    /* read in the length and the two vectors */
    scanf("%d",&glob->length);
    for (i = 0; i < glob->length; i++)
	scanf("%d",&glob->a[i]);
    for (i = 0; i < glob->length; i++)
	scanf("%d",&glob->b[i]);
    
    p4_create_procgroup();
    if (p4_get_my_id() != 0)
    {
	slave();
	exit(0);
    }

    /* p4_unlock(&(glob->go_lock)); */
    start = p4_clock();
    work();
    end   = p4_clock();

    for (i = 0; i < glob->length;) 
    {
	for (j = 0; (j < 9) && (i < glob->length); j++)
	    printf("%d\t", glob->c[i++]);
	printf("\n");
    }
    for (i = 0, n = p4_num_total_ids(); i < n; i++)
	printf("num by %d = %d \n",i,glob->num_added[i]);

    p4_wait_for_end();
}

work()
{
int i, j, myid, nprocs, rc;

    /* lock/unlock acts like a barrier */
    p4_lock(&(glob->go_lock));
    p4_unlock(&(glob->go_lock));

    myid = p4_get_my_id();
    glob->num_added[myid] = 0;
    nprocs = p4_num_total_ids();
    while (p4_askfor(&(glob->askfor),nprocs,getprob,(P4VOID *)&i,reset) == 0)
    {
	glob->c[i] = glob->a[i] + glob->b[i];
	glob->num_added[myid]++;
    }
}
