#include "mpid.h"
#include <string.h>

void MPID_Node_name( name, nlen )
char *name;
int  nlen;
{
    sprintf( name, "Cenju-3 - W%d", MPID_MyWorldRank );
}
