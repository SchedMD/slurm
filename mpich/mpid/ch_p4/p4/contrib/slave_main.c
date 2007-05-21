#include "p4.h"

main(argc, argv)
int argc;
char **argv;
{
    p4_initenv(&argc, argv);
    /*****
    if (p4_am_i_cluster_master())
        p4_dprintf("I am the cluster master\n");
    *****/
    slave();
    p4_wait_for_end();
}

