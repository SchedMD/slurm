	
// RemoteShell.h : Declaration of the CRemoteShell

#ifndef __REMOTESHELL_H_
#define __REMOTESHELL_H_

#include "resource.h"       // main symbols
#include "..\Common\RemoteShellLog.h"
#include "..\Common\MPIJobDefs.h"
#include "ChunkNode.h"

extern HANDLE g_hLaunchSyncMutex;
#define DEFAULT_LAUNCH_TIMEOUT 15000
extern DWORD g_nLaunchTimeout;

/////////////////////////////////////////////////////////////////////////////
// CRemoteShell
class ATL_NO_VTABLE CRemoteShell : 
	public CComObjectRootEx<CComMultiThreadModel>,
	public CComCoClass<CRemoteShell, &CLSID_RemoteShell>,
	public IDispatchImpl<IRemoteShell, &IID_IRemoteShell, &LIBID_REMOTESHELLSERVERLib>
{
public:
	CRemoteShell()
	{
		m_pUnkMarshaler = NULL;
		m_hProcess = NULL;
		m_hOutputMutex = CreateMutex(NULL, FALSE, NULL);
		m_hOutputEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
		m_hStdoutPipeR = NULL;
		m_hStderrPipeR = NULL;
		m_hStdinPipeW = NULL;
		m_pOutList = NULL;
		m_pOutListTail = NULL;
		m_hStdoutThread = NULL;
		m_hStderrThread = NULL;
		m_dwProcessId = 0;
		m_dwExitCode = 0;
		m_bLaunchOnDesktop = false;
		m_pMapping = NULL;
		m_hMapping = NULL;
	}
	~CRemoteShell();

DECLARE_REGISTRY_RESOURCEID(IDR_REMOTESHELL)
DECLARE_GET_CONTROLLING_UNKNOWN()

DECLARE_PROTECT_FINAL_CONSTRUCT()

BEGIN_COM_MAP(CRemoteShell)
	COM_INTERFACE_ENTRY(IRemoteShell)
	COM_INTERFACE_ENTRY(IDispatch)
	COM_INTERFACE_ENTRY_AGGREGATE(IID_IMarshal, m_pUnkMarshaler.p)
END_COM_MAP()

	HRESULT FinalConstruct()
	{
		return CoCreateFreeThreadedMarshaler(
			GetControllingUnknown(), &m_pUnkMarshaler.p);
	}

	void FinalRelease()
	{
		m_pUnkMarshaler.Release();
	}

	CComPtr<IUnknown> m_pUnkMarshaler;

// IRemoteShell
public:
	STDMETHOD(GetPortFromMapping)(long *nPort, long *nError, BSTR *bErrorMsg);
	STDMETHOD(CreateFileMapping)(BSTR bName, long *nError, BSTR *bErrorMsg);
	STDMETHOD(GrantAccessToDesktop)(BSTR bAccount, BSTR bPassword, long *nError, BSTR *bErrorMsg);
	STDMETHOD(GetPortFromFile)(BSTR bFileName, long *nPort, long *nError, BSTR *bErrorMsg);
	STDMETHOD(CreateTempFile)(BSTR *bFileName, long *nError, BSTR *bErrorMsg);
	STDMETHOD(SendBreak)(long *nError, BSTR *bErrorMsg);
	STDMETHOD(Abort)(long *nError, BSTR *bErrorMsg);
	STDMETHOD(PutProcessInput)(VARIANT vInput, long *nError, BSTR *bErrorMsg);
	STDMETHOD(GetProcessOutput)(VARIANT *vOutput, long *nState, long *nError, BSTR *bErrorMsg);
	STDMETHOD(LaunchProcess)(BSTR bCmdLine, BSTR bEnv, BSTR bDir, BSTR bAccount, BSTR bPassword, long *nPid, long *nError, BSTR *bErrorMsg);

	HANDLE m_hProcess, m_hOutputMutex, m_hStdoutPipeR, m_hStderrPipeR, m_hStdinPipeW;
	DWORD m_dwProcessId, m_dwExitCode;
	HANDLE m_hOutputEvent;
	HANDLE m_hStdoutThread;
	HANDLE m_hStderrThread;
	ChunkNode *m_pOutList, *m_pOutListTail;
	bool m_bLaunchOnDesktop;

	HANDLE m_hMapping;
	LONG *m_pMapping;
};

#endif //__REMOTESHELL_H_
