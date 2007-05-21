#include "chconfig.h"
#include "globdev.h"

int mpich_globus2_debug_rank = -1;
int mpich_globus2_debug_level = 0;
int mpich_globus2_debug_modules = 0;
int mpich_globus2_debug_info = 0;

void mpich_globus2_debug_init()
{
    mpich_globus2_debug_rank = MPID_MyWorldRank;
}
