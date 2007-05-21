#include "mpid.h"
#include "mpiddev.h"
#include "flow.h"
#include "../util/queue.h"

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include "p4mpd/lib/p4_get_info.h"

void MPID_Close_sockets(void);

void MPID_Close_sockets(void)

{  /* begin MPID_Close_sockets */

    int i;
    int conntype;
    int fd;
    int num_procs;
    int *closed_array;

    num_procs = p4_get_num_in_proctable();
    closed_array = (int *) malloc(num_procs * sizeof(int));

    /* Wait until all processes have set their connection type to closed */
    if (num_procs > 1) {  /* begin if num_procs > 1 */
	for (i=0; i<num_procs; i++)
	    closed_array[i] = 0;	

	for (i = 0; i < num_procs; i++) {  /* begin for i loop */
	    conntype = p4_get_conntype(i);
      
	    if (conntype == CONN_REMOTE_CLOSED)
		closed_array[i] = 1;

	}  /* end for i loop */

	/* Now close the sockets */
	for (i=0; i<num_procs; i++) {  /* begin for i loop */
	    if (closed_array[i] == 1) {  /* begin if loop */
		fd = p4_get_fd(i);
		close(fd);
	    }  /* end if loop */
	}  /* end for i loop */
	free( closed_array );

    }  /* end if num_procs > 1 */
    
    else {  /* begin else */
	fd = p4_get_fd(0);
	close(fd);
    }  /* end else */

}  /* end MPID_Close_sockets */


