#include "p4.h"
#include "p4_sys.h"
#include "p4_get_info.h"

int p4_get_conntype(int dest)
{  /* begin p4_get_conntype */
    
    return p4_local->conntab[dest].type;

}  /* end p4_get_conntype */


int p4_get_fd(int dest)
{  /* begin p4_get_fd */

    return p4_local->conntab[dest].port;

}  /* end p4_get_fd */


int p4_get_num_in_proctable( void )
{  /* begin p4_get_num_in_proctable */

    return p4_global->num_in_proctable;

} /* end p4_get_num_in_proctable */

