#ifndef NT_GLOBAL_H
#define NT_GLOBAL_H

#include "mpidefs.h"
#include "nt_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// nt_ipvishm_priv.cpp
extern int g_nLastRecvFrom;
void MPID_NT_ipvishm_Init( int *argc, char ***argv );
void MPID_NT_ipvishm_End();
int MPID_NT_ipvishm_exitall(char *msg, int code);
int nt_ipvishm_proc_info(int i, char **hostname, char **exename);
int NT_PIbsend(int type, void *buffer, int length, int to, int datatype);
int NT_PIbrecv(int type, void *buffer, int length, int datatype);
int NT_PInprobe(int type);
int NT_PInsend(int type, void *buffer, int length, int to, int datatype, int *pId);
int NT_PInrecv(int type, void *buffer, int length, int datatype, int *pId);
int NT_PIwait(int *pId);
int NT_PInstatus(int *pId);
void SetupMinimal();
#ifdef MPID_HAS_HETERO
int NT_PIgimax(void *val, int n, int work, int procset);
#endif

#ifdef __cplusplus
};
#endif

#endif
