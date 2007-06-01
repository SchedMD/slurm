#include "p4.h"
#include "sr_user.h"

slave()	
{
    char buf[100];
    int size;
    int n;
    int nslaves_t;
    int nslaves_l;
    int start, end;
    int type;
    int done;
    int from;
    int next;
    int my_id;
    int my_cl_id;
    int rm_ind;
    char *incoming;
    int start_time,end_time;
    FILE *tempf;
    
    my_id = p4_get_my_id();
    p4_dprintfl(10,"sr_slave:in slave %d\n",my_id);
    /****
    dump_global(5);
    dump_local(5);
    ****/
    nslaves_t = p4_num_total_ids() - 1;

    /***/
    rm_ind = p4_am_i_cluster_master();
    nslaves_l = p4_num_cluster_ids() - 1;
    p4_get_cluster_ids(&start, &end); 
    my_cl_id = p4_get_my_cluster_id();

    p4_dprintfl(5,"p4_num_total_slaves=%d num_cluster_slaves=%d\n",
	    nslaves_t,nslaves_l);
    p4_dprintfl(5,"first_local_id=%d last_local_id=%d\n",start,end);
    p4_dprintfl(5,"my_cluster_id=%d my_id=%d rm=%d\n",
	     my_cl_id,my_id,rm_ind);
    /***/

    if (my_id == nslaves_t)
        next = 0;
    else
	next = my_id + 1;
    done = FALSE;
    while (!done)
    {
        p4_dprintfl(99,"sr_slave recving \n");
	type = -1;
	from = -1;
        start_time = p4_clock();
	incoming = NULL;
	p4_recv(&type,&from, &incoming, &size);
	p4_dprintfl(99,"sr_slave received, from=%d, type = %d\n",from,type);
	if (type == END)
	    done = TRUE;
	else 
	    p4_dprintfl(99,"sr_slave: got buf=\"%s\"\n",incoming);
	p4_dprintfl(99,"sr_slave: slave %d sending to %d\n", my_id,next);
	p4_send(type, next, incoming, size);
        end_time = p4_clock();
	p4_dprintfl(99,"sr_slave: slave %d sent to %d\n",my_id,next);
        p4_dprintfl(5,"total time=%d \n",end_time-start_time);
	p4_msg_free(incoming);
    }
    p4_dprintfl(10,"sr_slave %d exiting\n", p4_get_my_id());
}

