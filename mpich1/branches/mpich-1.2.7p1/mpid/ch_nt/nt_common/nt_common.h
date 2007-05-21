#ifndef NT_COMMON_H
#define NT_COMMON_H

#include <winsock2.h>
#include <windows.h>

#ifdef __cplusplus

#define DEBUG_OUTPUT

#ifdef DEBUG_OUTPUT
#include <stdio.h>
extern bool g_bVerbose;
#define DPRINTF(a) if (g_bVerbose) { printf("[%d]", g_nIproc); printf a ; fflush(stdout); }
#define WDPRINTF(a) if (g_bVerbose) { printf("[%d]", g_nIproc); wprintf a ; fflush(stdout); }
#else
#define DPRINTF(a) 
#define WDPRINTF(a) 
#endif

#endif

#ifdef __cplusplus
extern "C" {
#endif

extern int g_nIproc;
extern int g_nNproc;

void nt_error(char *string, int value);
void nt_error_socket(char *string, int value);

#ifdef __cplusplus
};
#endif

extern char g_ErrMsg[1024];
void MakeErrMsg(int error, char *pFormat, ...);

#endif
