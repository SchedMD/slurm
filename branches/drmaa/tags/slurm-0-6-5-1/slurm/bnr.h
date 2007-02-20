/*****************************************************************************\
 *  BNR interface definitions, based upon
 *  Interfacing Parallel Jobs to Process Managers
 *  Brian Toonen, et. al.
 *
 *  http://csdl.computer.org/comp/proceedings/hpdc/2001/1296/00/12960431abs.htm
 *  http://www-unix.globus.org/mail_archive/mpich-g/2001/Archive/ps00000.ps
 *****************************************************************************
 *                                  COPYRIGHT
 *
 *  The following is a notice of limited availability of the code, and disclaimer
 *  which must be included in the prologue of the code and in all source listings
 *  of the code.
 *
 *  Copyright Notice
 *  + 1993 University of Chicago
 *  + 1993 Mississippi State University
 *
 *  Permission is hereby granted to use, reproduce, prepare derivative works, and
 *  to redistribute to others.  This software was authored by:
 *
 *  Argonne National Laboratory Group
 *  W. Gropp: (630) 252-4318; FAX: (630) 252-5986; e-mail: gropp@mcs.anl.gov
 *  E. Lusk:  (630) 252-7852; FAX: (630) 252-5986; e-mail: lusk@mcs.anl.gov
 *  Mathematics and Computer Science Division
 *  Argonne National Laboratory, Argonne IL 60439
 *
 *  Mississippi State Group
 *  N. Doss:  (601) 325-2565; FAX: (601) 325-7692; e-mail: doss@erc.msstate.edu
 *  A. Skjellum:(601) 325-8435; FAX: (601) 325-8997; e-mail: tony@erc.msstate.edu
 *  Mississippi State University, Computer Science Department &
 *   NSF Engineering Research Center for Computational Field Simulation
 *  P.O. Box 6176, Mississippi State MS 39762
 *
 *                              GOVERNMENT LICENSE
 *
 *  Portions of this material resulted from work developed under a U.S.
 *  Government Contract and are subject to the following license: the Government
 *  is granted for itself and others acting on its behalf a paid-up, nonexclusive,
 *  irrevocable worldwide license in this computer software to reproduce, prepare
 *  derivative works, and perform publicly and display publicly.
 * 
 *                                   DISCLAIMER
 *
 *  This computer code material was prepared, in part, as an account of work
 *  sponsored by an agency of the United States Government.  Neither the United
 *  States, nor the University of Chicago, nor Mississippi State University, nor
 *  any of their employees, makes any warranty express or implied, or assumes any
 *  legal liability or responsibility for the accuracy, completeness, or
 *  usefulness of any information, apparatus, product, or process disclosed, or
 *  represents that its use would not infringe privately owned rights.
\*****************************************************************************/


/* BNR group ID
 * A single job step may initialize multiple BNR groups
 * BNR can be used to establish key=value pairs and communicate
 * that information between the tasks of a single SLURM job step */
typedef int BNR_gid;

/* Maximum size of a BNR key, in bytes */
#define BNR_MAXATTRLEN 64

/* Maximum size of a BNR value, in bytes */
#define BNR_MAXVALLEN  3*1024

/* Return codes associated with all BNR functions */
#define BNR_SUCCESS 0
#define BNR_ERROR   1


/* Initialize a BNR group and return a BNR group ID in mygid */
extern int BNR_Init(BNR_gid *mygid);

/* For a given BNR group ID, store an key (attr) and associated value (val) */
extern int BNR_Put(BNR_gid gid, char *attr, char *val);

/* For a given BNR group ID, wait until all tasks have executed BNR_Fence
 * before proceeding */
extern int BNR_Fence(BNR_gid gid);

/* For a given BNR group ID and key (attr) return its associated value (val) */
extern int BNR_Get(BNR_gid  gid, char *attr, char *val);

/* Terminate a BNR session and release all associated storage */
extern int BNR_Finalize();

/* Return the zero-origin task ID of this job step
 * Equivalent to SLURM_PROCID environment variable */
extern int BNR_Rank(BNR_gid group, int *myrank);

/* Return the number of tasks associated with this job step
 * Equivalent to SLURM_NPROCS environment variable */
extern int BNR_Nprocs(BNR_gid group, int *nprocs);

