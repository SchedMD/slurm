#include "p4.h"
#include "sr_user.h"
    
slave()	
{
    int nslaves;
    int done;
    int type, from, size;
    int next;
    int my_id;
    char *incoming;
    
    my_id = p4_get_my_id();
    nslaves = p4_num_total_ids() - 1;
    
    if (my_id == nslaves)
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
	p4_sendx(type, next, incoming, size, P4INT);
	if (type == END)
	    done = TRUE;
	p4_msg_free(incoming);
    }
}


