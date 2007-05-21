#include "p4.h"

#define FINAL_BARRIER 4
#define MESSAGE       6
    
main(argc,argv)
int argc;
char **argv;
{
    
    p4_initenv(&argc,argv);
    if (p4_get_my_id() == 0)
        p4_create_procgroup();
    slave();
    p4_global_barrier(FINAL_BARRIER); /* broadcasts may be in progress */
    p4_wait_for_end();
}

slave()	
{
    int my_num, work_num;
    int mtyp = MESSAGE, dummy, len, i;
    char *msg;
    int tempfrom;
    
    my_num = p4_get_my_id();
    work_num = p4_num_total_ids();
    
    p4_broadcast(mtyp, (char *) &dummy, sizeof(int));
    
    for (i=0; i<work_num; i++)
	if (i != my_num)
	{
	    msg = NULL;
	    p4_dprintf("hallo: %d\n",i);
	    p4_recv(&mtyp, &i, &msg, &len);
	}
    p4_msg_free(msg);
}

