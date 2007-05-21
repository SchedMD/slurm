#ifndef REMOTESHELLLOG_H
#define REMOTESHELLLOG_H

extern HANDLE g_hLogMutex;

void LogMsg(LPCTSTR pFormat, ...);
void LogWMsg(WCHAR* pFormat, ...);
void DLogMsg(LPCTSTR pFormat, ...);
void DLogWMsg(WCHAR* pFormat, ...);

#endif
