
/* Author: Florian Sukup
   Technical University of Vienna */

#include "p4.h"

#define FINAL_BARRIER 4
#define MESSAGE_SIZE 16000
    
void slave();

void main(argc,argv)
int argc;
char **argv;
{
    
    p4_initenv(&argc,argv);
    if (p4_get_my_id() == 0)
        p4_create_procgroup();
    slave();
    p4_wait_for_end();
}

void slave()	
{
    int my_num, work_num;
    int mtyp = 6, len, i;
    char *msg0, *msg1;
    
    my_num = p4_get_my_id();
    work_num = p4_num_total_ids();

    if ((msg0 = p4_msg_alloc(MESSAGE_SIZE * work_num)) == NULL)
	printf("problems with allocating msg0\n");

    p4_broadcast(mtyp, msg0, MESSAGE_SIZE);
	
    p4_dprintf("broadcasted %d bytes\n",MESSAGE_SIZE);
	
    for (i=0; i<work_num; i++)
	if (i != my_num)
	{
	    len = MESSAGE_SIZE;
	    msg1 = NULL;
	    p4_recv(&mtyp, &i, &msg1, &len);
	    p4_dprintf("received %d bytes from worker %d\n",len, i);
	}
	
    p4_msg_free(msg0);

    p4_global_barrier(FINAL_BARRIER);
}
