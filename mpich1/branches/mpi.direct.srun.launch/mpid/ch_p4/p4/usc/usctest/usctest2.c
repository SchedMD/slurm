#include <stdio.h>
#include "../usc.h"

#if defined(CAP2_HOST)
#include <chost.c7.h>
host_main()
#else
#if defined(CAP2_CELL)
cell_main()
#else
main()
#endif
#endif
{
	cconfxy(64,1);
	ccreat("capcell",30,NULL);
}

