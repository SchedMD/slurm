/*
 * set_oomadj.h - prevent slurmd/slurmstepd from being killed by the
 *	kernel OOM killer
 */

#ifndef _SET_OOMADJ_H
#define _SET_OOMADJ_H

/* from linux/mm.h */
#define OOM_DISABLE (-17)

extern int set_oom_adj(int adj);

#endif /* _SET_OOMADJ_H */

