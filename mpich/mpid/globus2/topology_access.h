
/*
 * CVS Id: $Id: topology_access.h,v 1.2 2002/09/13 23:24:44 lacour Exp $
 */

/* This module allows the user to access the underlying topology of
 * processes */

#ifndef TOPOLOGY_ACCESS_H
#define TOPOLOGY_ACCESS_H


#include "comm.h"   /* for "struct MPIR_COMMUNICATOR" */

/* create the topology keys to access the Depths and Colors of the
 * processes (information cached in the communicators).  Also create a
 * copy to backup these keys, in case the user messes them up.  This
 * function is called at initialization time by MPID_Init(). */
extern void
create_topology_access_keys (void);

/* free the topology keys; this function is called by MPID_End(). */
extern void
destroy_topology_access_keys (void);

/* put the topology information (Depths & Colors) into the
 * communicator; this function is called by topology_initialization()
 * when a communicator is created/initilized in MPID_Comm_init(). */
extern int
cache_topology_information (struct MPIR_COMMUNICATOR * const);


#endif   /* TOPOLOGY_ACCESS_H */

