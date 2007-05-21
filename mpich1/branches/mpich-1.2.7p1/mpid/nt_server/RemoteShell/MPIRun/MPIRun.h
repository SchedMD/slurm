#ifndef MPIRUN_H
#define MPIRUN_H

#include <windows.h>
#include <tchar.h>

extern long g_nHosts;
extern TCHAR g_pszExe[MAX_PATH];
extern TCHAR g_pszArgs[MAX_PATH];
extern TCHAR g_pszEnv[1024];
extern bool g_bNoMPI;

#endif
