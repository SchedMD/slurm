#ifndef NT_LOG_H
#define NT_LOG_H

#include <windows.h>

extern HANDLE g_hLogMutex;

void ClearLog();
void LogMsg(LPCSTR pFormat, ...);
void LogWMsg(WCHAR* pFormat, ...);
void DLogMsg(LPCTSTR pFormat, ...);
void DLogWMsg(WCHAR* pFormat, ...);

#endif
