#include "p4.h"
#include "sr_user.h"
    
main(argc,argv)
int argc;
char **argv;
{
    p4_initenv(&argc,argv);
    p4_create_procgroup();
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
	p4_send(100, 1, msg, i);
	type = -1;
	from = -1;
	incoming = NULL;
	p4_recv(&type, &from, &incoming, &size);
	end_time = p4_clock();
	printf("total time=%d \n",end_time-start_time);
	printf("master received :%s: from %d\n", incoming, from);
	p4_msg_free(incoming);
	printf("enter a string:\n");
    }
    
    p4_send(END, 1, msg, 0);
    type = -1;
    from = -1;
    incoming = NULL;
    p4_recv(&type, &from, &incoming, &size);
    p4_msg_free(incoming);
    
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
    while (!done)
    {
	type = -1;
	from = -1;
	incoming = NULL;
	p4_recv(&type,&from, &incoming, &size);
	if (type == END)
	    done = TRUE;
	p4_send(type, next, incoming, size);
	p4_msg_free(incoming);
    }
}

