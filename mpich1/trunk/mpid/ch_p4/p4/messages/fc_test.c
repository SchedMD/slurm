#include "p4.h"
#include "sr_user.h"
    
main(argc,argv)
int argc;
char **argv;
{
    p4_initenv(&argc,argv);
    p4_create_procgroup();   
    p4_dprintf("calling p4_initfc\n");
    p4_initfc();
    p4_dprintf("got past p4_initfc\n");
    if (p4_get_my_id() == 0)
    {
	master();
    }
    else
    {
	worker();
    }
    p4_wait_for_end();
}

master()
{
    char msg[200];
    int i, my_id, size, type, from;
    char *incoming, *cp;
    int start_time,end_time;
    
    my_id = p4_get_my_id();
    
    incoming = p4_msg_alloc(1000);
    printf("enter a string:\n");
    while (gets(msg) != NULL)
    {
        for(cp=msg, i=1; *cp; i++, cp++)
	    if(*cp == '\n')
	    {
		*cp = 0;
		break;
	    }
	start_time = p4_clock();
	p4_sendfc(100, 1, msg, i);
	type = -1;
	from = 1;
	p4_recvfc(&type, &from, &incoming, &size);
	end_time = p4_clock();
	printf("total time=%d \n",end_time-start_time);
	printf("master received :%s: from %d\n", incoming, from);
	printf("enter a string:\n");
    }
    
    p4_sendfc(END, 1, msg, 0);
    type = -1;
    from = 1;
    p4_recvfc(&type, &from, &incoming, &size);
    
    printf("master exiting normally\n");
}

worker()	
{
    int done;
    int my_id, next, size;
    int num_total_slaves, type, from;
    char *incoming;
    
    my_id = p4_get_my_id();
    num_total_slaves = p4_num_total_slaves();
    if (my_id == num_total_slaves)
        next = 0;
    else
	next = my_id + 1;
    done = FALSE;
    incoming = p4_msg_alloc(1000);
    while (!done)
    {
	type = -1;
	from = 0;
	p4_dprintf("about to receive, incoming = 0x%x\n",incoming);
	p4_recvfc(&type,&from, &incoming, &size);
	if (type == END)
	    done = TRUE;
	p4_sendfc(type, next, incoming, size);
    }
}

