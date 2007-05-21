/*	$CHeader: mem.c 1.3 1995/11/08 12:48:59 $
 *	Copyright 1992 Convex Computer Corp.
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include "mem.h"
#include "global.h"
#include "ddpro.h"
#include "tdpro.h"
#include <assert.h>
 */
#define SEM 0
#define POLLCOUNT 1000000

#ifdef MPI_cspp
extern int cnx_yield;
#endif
static int debugmask;

int
MPID_SHMEM__wait_lock(ip, val)
#ifdef USE_VOL
	int * volatile ip;
#else
	int *ip;
#endif
	int val;
{
	register int count = 0;

#ifdef MPI_cspp
	if (cnx_yield) {
#ifdef USE_VOL
		while ((*ip) != val)
#else
		while (MPID_SHMEM__read32(ip) != val)
#endif
		{
			if (++count == POLLCOUNT) {
				count = 0;
				p2p_yield();
			}
		}
	} else 
#endif
	{
#ifdef USE_VOL
		while ((*ip) != val);
#else
		while (MPID_SHMEM__read32(ip) != val);
#endif
	}

	return(0);
}

int
MPID_SHMEM__acquire_lock(ip)
#ifdef USE_VOL
	int * volatile ip;
#else
	int *ip;
#endif
{
/* printf("%d: acquire_lock, ip %x *ip %x\n", getpid(), ip, *ip); */
/*
	if ((int)ip & 0x000000f) {
		printf("acquire_lock, bad ip %x\n", ip);
		return -1;
	}
*/
	if (debugmask & SEM) {
		printf("trying to acquire lock %x\n", ip);
	}

	while (1) {
		MPID_SHMEM__wait_lock(ip, 1);

		if (MPID_SHMEM__ldcws32(ip)) break;
	}

	if (debugmask & SEM) {
		printf("Lock %x acquired\n", ip);
	}
/* printf("%d: acquired_lock, ip %x *ip %x\n", getpid(), ip, *ip); */
	return 0;
}
