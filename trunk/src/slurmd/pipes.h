#ifndef _SLURMD_PIPES_H_
#define _SLURMD_PIPES_H_

/*pipes.c*/
int init_parent_pipes ( int * pipes ) ;
void setup_parent_pipes ( int * pipes ) ; 
int setup_child_pipes ( int * pipes ) ;

#endif

