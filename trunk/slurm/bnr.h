/*
 * BNR interface definitions, based upon
 * Interfacing Prallel Jobs to Process Managers
 * Brian Toonen, et. al.
 *
 * http://www-unix.globus.org/mail_archive/mpich-g/2001/Archive/ps00000.ps
 */

typedef int BNR_gid;
#define BNR_MAXATTRLEN 64
#define BNR_MAXVALLEN  3*1024

#define BNR_SUCCESS 0
#define BNR_ERROR   1

extern int BNR_Init(BNR_gid *mygid);
extern int BNR_Put(BNR_gid gid, char *attr, char *val);
extern int BNR_Fence(BNR_gid gid);
extern int BNR_Get(BNR_gid  gid, char *attr, char *val);
extern int BNR_Finalize();
extern int BNR_Rank(BNR_gid group, int *myrank);
extern int BNR_Nprocs(BNR_gid group, int *nprocs);

