// Database.h: interface for the Database class.
//
//////////////////////////////////////////////////////////////////////

#if !defined(AFX_Database_H__39ADCE33_DB1E_11D3_9604_009027106653__INCLUDED_)
#define AFX_Database_H__39ADCE33_DB1E_11D3_9604_009027106653__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "nt_common.h"
#include "nt_tcp_sockets.h"

extern HANDLE g_hStopDBSLoopEvent;

#define MPI_DBS_SUCCESS		 		0
#define MPI_DBS_FAIL				1

#define MPI_DBS_CMD_EXISTS			0
#define MPI_DBS_CMD_PUT_PERSISTENT	1
#define MPI_DBS_CMD_PUT_CONSUMABLE	2
#define MPI_DBS_CMD_GET				3
#define MPI_DBS_CMD_DELETE			4
#define MPI_DBS_CMD_GETSTATE		5

#define DBSIDLEN					100
#define DATABASE_TIMEOUT			10000
#define DBS_CREATE_THREAD_RETRIES     5
#define DBS_CREATE_THREAD_SLEEP_TIME  250

class Database  
{
public:
	Database();
	virtual ~Database();

	bool Init();
	void SetID(char *pszID);
	bool GetID(char *pszID, int *length);
	int Get(char *pszKey, void *pValue, int *length);
	int Put(char *pszKey, void *pValue, int length, bool bPersistent = true);
	int Delete();
	int Print(char *pBuffer, int *length);
	Database& operator=(Database &db);

private:
	char m_pszServerHost[100];
	long m_nServerPort;
	char m_pszID[DBSIDLEN];
};

class DatabaseServer
{
public:
	struct ValueNode
	{
		void *pData;
		int length;
		ValueNode *pNext;
	};
	struct KeyNode
	{
		char *pszKey;
		bool bPersistent;
		ValueNode *pValueList;
		KeyNode *pNext;
	};
	struct IDNode
	{
		char pszID[DBSIDLEN];
		KeyNode *pKeyList;
		IDNode *pNext;
	};
	DatabaseServer();
	virtual ~DatabaseServer();

	bool SetPort(int nPort);
	bool Start();
	bool GetHost(char *pszHost, int length);
	int GetPort();
	bool Stop();
	int Get(char *pszID, char *pszKey, void *&pValueData, int *length);
	int Put(char *pszID, char *pszKey, void *pValueData, int length, bool bPersistent = true);
	int Delete(char *pszID);
	void PrintState();
	void PrintStateToBuffer(char *pszBuffer, int *pnLength);
	int GetState(char *pszOutput, int *length);

	friend void DatabaseServerThread(DatabaseServer *pServer);

private:
	HANDLE m_hMutex;
	HANDLE m_hServerThread;
	int m_nPort;
	char m_pszHost[100];
	IDNode *m_pList;
};

struct DBSClientArg
{
	SOCKET sock;
	WSAEVENT sock_event;
	DatabaseServer *pServer;
};

int DatabaseClientThread(DBSClientArg *arg);
int dbs_error(char *string, int value, bool bExit=false);
int dbs_error(char *string, int value, SOCKET sock, WSAEVENT sock_event, bool bExit=false);

#endif // !defined(AFX_Database_H__39ADCE33_DB1E_11D3_9604_009027106653__INCLUDED_)
