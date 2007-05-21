#include "p4.h"

extern void slave();

int main(argc,argv)
     int argc;
     char **argv;
{
  p4_initenv(&argc,argv);
  p4_create_procgroup();

  slave();

  p4_wait_for_end();
}

void slave()
{
    int i,j;
    char *buffer;
    int msg_len;
    int len;
    int type;
    int me = p4_get_my_id();
    int nproc = p4_num_total_ids();
    
    len = 32800;  /* fails with 32800 */
    
    if ( (buffer = p4_shmalloc((unsigned) len)) == (char *) NULL)
	p4_error("Ring: failed to allocate buffer",len);
  
    type = 5;

    for (j = 0; j < 2; j++)
    {
	for(i=0; i<nproc; i++)
	    if (i!=me)
	    {
		(void) p4_send(type,i, buffer, len);
		(void) p4_send(type,i, buffer, len);
	
	    }
	for(i=0; i<nproc; i++)
	    if (i!=me)
	    {
		msg_len = len;
		(void) p4_recv(&type, &i, &buffer, &msg_len);
		(void) p4_recv(&type, &i, &buffer, &msg_len);
	    }

	if (me == 0)
	    (void) printf("Messages whith %d bytes lenght sent and received\n",
			  len);
    }
    

    if (me == 0)
	(void) p4_shfree(buffer);

}

