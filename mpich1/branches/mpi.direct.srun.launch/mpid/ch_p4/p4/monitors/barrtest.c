#include "p4.h"

#define MAXLEN 500
#define MAXPROCS 256

struct globmem {
    int length;
    int a[MAXLEN], b[MAXLEN], c[MAXLEN];
    int num_added[MAXPROCS];
    int sub;
    int slave_id;
    p4_lock_t go_lock;
    p4_askfor_monitor_t askfor;
} *glob;


main(argc,argv)
int argc;
char **argv;
{
int i, j, n, start, end;

    p4_initenv(&argc,argv);   /* args not used but passed for compat */

    glob = (struct globmem *) p4_shmalloc(sizeof(struct globmem));

    p4_create_procgroup();

    n = p4_num_cluster_ids();
    initbar(&n);
    n = p4_num_cluster_ids();
    pidbar(&n);
    if (p4_get_my_id() == 0)
    {
    }

    p4_dprintfl(00,"before the waitbarr\n");
    waitbar();
    p4_dprintfl(00,"past the waitbarr\n");

    p4_wait_for_end();
}

