#ifndef LAUNCH_NODE_H
#define LAUNCH_NODE_H

#include <winsock2.h>
#include <windows.h>

class LaunchNode
{
public:
	LaunchNode();
	~LaunchNode();

	void Set(DWORD dwData);
	void SetExit(int nGroup, int nRank, DWORD dwExitCode);
	void InitData(HANDLE hEndOutputPipe);
	int GetID();
	DWORD GetData(int nTimeout = INFINITE);

	static LaunchNode *AllocLaunchNode();
	static DWORD GetLaunchNodeData(int nID, int nTimeout = INFINITE);
	static void FreeLaunchNode(LaunchNode *pNode);

private:
	static LaunchNode *g_pList;
	static HANDLE g_hMutex;
	static int g_nCurID;

	HANDLE m_hEvent, m_hEndEvent;
	int m_nID;
	DWORD m_dwData;
	HANDLE m_hEndOutputPipe;
	DWORD m_dwExitCode;
	LaunchNode *m_pNext;

	static void RemoveNode(LaunchNode *pNode);
};

#endif
