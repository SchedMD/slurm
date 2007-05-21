#include "p4.h"

#define BARRIER_TYPE 100
#define MSG_OTHER    200

main( argc, argv)
int  argc;
char **argv;
{
    int    w = 0;
    int    left, len = 10, i;
    int    reps = 10;
    char   *rbuffer, *sbuffer;
    int id;
    int type, from, size;

    p4_initenv(&argc, argv);
    p4_create_procgroup();

    id = p4_get_my_id();
    left = !id;

    /*****  RMB -> these values must be passed to p1 when read by p0
    if (id == 0)
    {
	printf("reps? ");   scanf("%d",&reps);
	printf("len? ");    scanf("%d",&len);
	printf("sleep? ");  scanf("%d",&w);
	printf("reps = %d, len = %d, sleep = %d\n", reps, len, w);
    }
    *****/

    sbuffer = (char *) p4_msg_alloc(len);
    rbuffer = (char *) p4_msg_alloc(len);

    p4_dprintf("synchronizing....\n");
    p4_global_barrier(BARRIER_TYPE);
    p4_dprintf("Starting sends %d %d\n",len,reps); 

    for ( i=0; i<reps; i++ )
    {
	usleep(w);
	p4_send(MSG_OTHER,left,sbuffer,len);
    }
    p4_dprintf("Starting receives\n");
    for ( i=0; i<reps; i++ )
    {
	type = MSG_OTHER;
	from = left;
	p4_recv(&type,&from,&rbuffer,&size);
    }
    p4_dprintf("Past receives\n");
    p4_wait_for_end();
    fprintf(stdout,"All done\n"); fflush(stdout);
}
