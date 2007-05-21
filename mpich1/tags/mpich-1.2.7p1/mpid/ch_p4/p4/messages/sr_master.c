#include "p4.h"
#include "sr_user.h"
    
main(argc,argv)
int argc;
char **argv;
{
    char buf[100];
    int i, n;
    int size;
    int slv = 3;
    int next;
    int nslaves_t;
    int nslaves_l;
    int start, end;
    char msg[200];
    int type;
    int id;
    int from;
    int my_id;
    int my_cl_id;
    char *incoming, *cp;
    int start_time,end_time;
    
    p4_initenv(&argc,argv);
    p4_create_procgroup();
    if (p4_get_my_id() != 0)
    {
	slave();
	exit(0);
    }
    
    p4_dprintfl(9,"Starting master code.\n");

    nslaves_t = p4_num_total_ids() - 1;
    nslaves_l = p4_num_cluster_ids() - 1;
    p4_get_cluster_ids(&start,&end);

    my_id = p4_get_my_id();
    my_cl_id = p4_get_my_cluster_id();
    
    p4_dprintfl(5,"p4_num_total_slaves=%d num_cluster_slaves=%d\n", 
	    nslaves_t,nslaves_l);
    p4_dprintfl(5,"first_local_id=%d last_local_id=%d\n",start,end);
    p4_dprintfl(5,"my_id=%d my_cluster_id=%d\n\n",my_id,my_cl_id);
    
    printf("enter a string:\n");
    while (fgets(msg, sizeof(msg), stdin) != NULL)
    {
        for(cp=msg, i=1; *cp; i++, cp++)
	    if(*cp == '\n')
	    {
		*cp = 0;
		break;
	    }
	p4_dprintfl(99,"sr_master sending %s size=%d\n", msg,i);
	start_time = p4_clock();
	p4_sendr(100, 1, msg, i);
	p4_dprintfl(99,"sr_master receiving...\n");
	type = -1;
	from = -1;
	/**********
	while (!p4_messages_available(&type,&from))
	    ;
	printf("master has msg available: type=%d from=%d\n",type,from);
	**********/
	incoming = NULL;
	p4_recv(&type,&from, &incoming, &size);
	end_time = p4_clock();
	printf("total time=%d \n",end_time-start_time);
	printf("master received :%s: from %d\n", incoming, from);
	p4_dprintfl(10,"master: received from=%d type=%d size=%d buf=%s\n", 
	       from, type, size, incoming);
	p4_msg_free(incoming);
	printf("enter a string:\n");
    }
    

    p4_dprintfl(8,"waiting for end msg\n");

    p4_sendr(END, 1, msg, 0);
    type = -1;
    from = -1;
    incoming = NULL;
    p4_recv(&type, &from, &incoming, &size);
    p4_dprintfl(8,"done  from=%d type=%d size=%d \n", 
	     from, type, size);
    p4_msg_free(incoming);
    
    p4_dprintfl(8,"master entering waitforend\n");
    p4_wait_for_end();
    p4_dprintfl(8,"master past waitforend\n");
    printf("master exiting normally\n");
}
