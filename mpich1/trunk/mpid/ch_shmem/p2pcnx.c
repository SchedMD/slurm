/*
  This file provides the p2p functions for the Convex SPP.  It is included
  by p2p.c
 */
extern char		*cnx_exec;
extern int		cnx_debug;
extern int		cnx_touch;
extern int		masterid;
extern unsigned int	procNode[];
extern unsigned int	numCPUs[];
extern unsigned int	numNodes;
static char		*myshmem;
static int		myshmemsize;

int
p2p_shnode(ptr)
void			*ptr;
{
    char		*p;
    
    p = ptr;
    return(((p >= myshmem) && (p < (myshmem + myshmemsize))) ? 0 : -1);
}

