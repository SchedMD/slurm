#include "p4.h"

#define MAXLEN 500
#define MAXPROCS 256


struct globmem {
    int length;
    int a[MAXLEN], b[MAXLEN], c[MAXLEN];
    int num_added[MAXPROCS];
    p4_barrier_monitor_t barrier;
    p4_getsub_monitor_t getsub;
} *glob;


slave()
{
    work();
}

main(argc,argv)
int  argc;
char **argv;
{
int i, j, n;

    p4_initenv(&argc,argv);

    glob = (struct globmem *) p4_shmalloc(sizeof(struct globmem));

    p4_barrier_init(&(glob->barrier));
    p4_getsub_init(&(glob->getsub));

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

    work();
    /* print the answer */
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
int i, myid, nprocs;

    myid = p4_get_my_id();
    glob->num_added[myid] = 0;
    nprocs = p4_num_total_ids();
    p4_barrier(&(glob->barrier),nprocs);
    p4_getsub(&(glob->getsub),&i,glob->length - 1,nprocs);
    while (i >= 0) {
	glob->c[i] = glob->a[i] + glob->b[i];
	glob->num_added[myid]++;
	p4_getsub(&(glob->getsub),&i,glob->length - 1,nprocs);
    }
}
