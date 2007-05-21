#ifndef MPDLIST_H
#define MPDLIST_H

#include <stdio.h>

#define MPDLIST_SUCCESS			0
#define MPDLIST_FAIL			1
#define MPDLIST_GET_BEFORE_SET	2

struct MPDAvailableNode
{
	unsigned long nIP;
	int nPort;
	MPDAvailableNode *pNext;
};

class MPDList
{
public:
	MPDList();
	~MPDList();

	int Add(unsigned long nIP, int nPort, int nSpawns = 1);
	int Remove(unsigned long nIP, int nPort);
	int Enable(unsigned long nIP, int nPort);
	int Disable(unsigned long nIP, int nPort);
	int SetNumSpawns(unsigned long nIP, int nPort, int nSpawns);
	int Increment(unsigned long nIP, int nPort);
	int Decrement(unsigned long nIP, int nPort);
	int GetNextAvailable(unsigned long *pnIP, int *pnPort);
	MPDAvailableNode* GetNextAvailable(int n);
	int GetID(char *pszHost, unsigned long *pnIP, int *pnPort, int *pnSpawns = NULL);
	int GetMyID(unsigned long *pnIP, int *pnPort, int *pnSpawns = NULL);
	int SetMyID(unsigned long nIP, int nPort);
	int SetMyID(char *pszHost, int nPort);
	int SetMySpawns(int nSpawns);

	void Print();
	void PrintToString(char *pBuffer);

	bool m_bLookupIP;

	struct Node
	{
		unsigned long nIP;
		int nPort;
		int nSpawns;
		int nSpawned;
		bool bEnabled;
		char pszhost[50];
		Node *pNext;
	};
private:
	unsigned long m_nIP;
	int m_nPort;
	int m_nSpawns;
	char m_pszHost[50];
	
	Node *m_pList;
};

#endif
