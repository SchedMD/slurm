#include "lock.h"

//*
void lock(LONG *ptr)
{
	int i;
	for (;;)
	{
		for (i=0; i<100; i++)
			if (InterlockedExchange(ptr, 1) == 0)
				return;
		Sleep(0);
	}
}
/*/
void lock(LONG *ptr)
{
	while ( InterlockedExchange(ptr, 1) != 0)
		Sleep(0);
}
//*/

bool ilock(LONG *ptr)
{
	return (InterlockedExchange(ptr, 1) == 0);
}

/*
void unlock(LONG *ptr)
{
	*ptr = 0;
}

void initlock(LONG *ptr)
{
	*ptr = 0;
}
//*/

