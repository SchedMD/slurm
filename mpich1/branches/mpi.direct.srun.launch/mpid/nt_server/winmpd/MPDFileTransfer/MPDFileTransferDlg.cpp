// MPDFileTransferDlg.cpp : implementation file
//

#include "stdafx.h"
#include "MPDFileTransfer.h"
#include "MPDFileTransferDlg.h"
#include "PasswordDialog.h"
#include "TransferDialog.h"
#include "AdvancedConnectDialog.h"
#include "AccountPasswordDlg.h"
#include "mpd.h"
#include "crypt.h"
#include "mpdutil.h"
#include "Translate_Error.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CAboutDlg dialog used for App About

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// Dialog Data
	//{{AFX_DATA(CAboutDlg)
	enum { IDD = IDD_ABOUTBOX };
	//}}AFX_DATA

	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CAboutDlg)
	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support
	//}}AFX_VIRTUAL

// Implementation
protected:
	//{{AFX_MSG(CAboutDlg)
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
	//{{AFX_DATA_INIT(CAboutDlg)
	//}}AFX_DATA_INIT
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CAboutDlg)
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
	//{{AFX_MSG_MAP(CAboutDlg)
		// No message handlers
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMPDFileTransferDlg dialog

CMPDFileTransferDlg::CMPDFileTransferDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CMPDFileTransferDlg::IDD, pParent)
{
    //{{AFX_DATA_INIT(CMPDFileTransferDlg)
    m_host1 = _T("");
    m_host2 = _T("");
    m_hostb = _T("Host B:");
	//}}AFX_DATA_INIT
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
    m_bfd1 = BFD_INVALID_SOCKET;
    m_bfd2 = BFD_INVALID_SOCKET;
    strcpy(m_pszRoot1, "C:");
    strcpy(m_pszRoot2, "C:");
    m_pszAccount1[0] = '\0';
    m_pszAccount2[0] = '\0';
    m_pszPassword1[0] = '\0';
    m_pszPassword2[0] = '\0';
    
    m_dragState = DRAGGING_NOT;
    m_DropTarget1.m_pDlg = NULL;
    m_DropTarget2.m_pDlg = NULL;
    
    m_nTimerId = 0;
    m_pTimerTree = NULL;
    m_hTimerItem = NULL;
}

void CMPDFileTransferDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CMPDFileTransferDlg)
	DDX_Control(pDX, IDC_FOLDER_PROGRESS2, m_folder_progress2);
	DDX_Control(pDX, IDC_FOLDER_PROGRESS1, m_folder_progress1);
	DDX_Control(pDX, IDC_FILE_PROGRESS2, m_file_progress2);
	DDX_Control(pDX, IDC_FILE_PROGRESS1, m_file_progress1);
	DDX_Control(pDX, IDC_HOSTB_EDIT, m_hostb_edit);
	DDX_Control(pDX, IDC_TREE2, m_tree2);
	DDX_Control(pDX, IDC_TREE1, m_tree1);
	DDX_Control(pDX, IDC_HOST2, m_host2_edit);
	DDX_Control(pDX, IDC_HOST1, m_host1_edit);
	DDX_Control(pDX, IDC_CONNECT2_BTN, m_connect2_btn);
	DDX_Control(pDX, IDC_CONNECT1_BTN, m_connect1_btn);
	DDX_Text(pDX, IDC_HOST1, m_host1);
	DDX_Text(pDX, IDC_HOST2, m_host2);
	DDX_Text(pDX, IDC_HOSTB_EDIT, m_hostb);
	//}}AFX_DATA_MAP
}

BEGIN_MESSAGE_MAP(CMPDFileTransferDlg, CDialog)
	//{{AFX_MSG_MAP(CMPDFileTransferDlg)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_CLOSE()
	ON_WM_SIZE()
	ON_BN_CLICKED(IDC_CONNECT1_BTN, OnConnect1Btn)
	ON_BN_CLICKED(IDC_CONNECT2_BTN, OnConnect2Btn)
	ON_COMMAND(ID_FILE_EXIT, OnFileExit)
	ON_NOTIFY(TVN_ITEMEXPANDING, IDC_TREE1, OnItemexpandingTree1)
	ON_NOTIFY(TVN_ITEMEXPANDING, IDC_TREE2, OnItemexpandingTree2)
	ON_COMMAND(ID_FILE_CONNECT, OnFileConnect)
	ON_NOTIFY(TVN_BEGINDRAG, IDC_TREE2, OnBegindragTree2)
	ON_NOTIFY(TVN_BEGINDRAG, IDC_TREE1, OnBegindragTree1)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMPDFileTransferDlg message handlers

void CMPDFileTransferDlg::ParseRegistry()
{
    HKEY tkey;
    DWORD result, len;
    char str[100];
    
    // Set the defaults.
    m_nPort1 = m_nPort2 = MPD_DEFAULT_PORT;
    gethostname(m_pszHost, 100);
    
    m_bNeedPassword1 = m_bNeedPassword2 = true;
    m_bNeedAccount1 = m_bNeedAccount2 = true;

    // Open the root key
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, MPD_REGISTRY_KEY,
	0, KEY_ALL_ACCESS, &tkey) != ERROR_SUCCESS)
    {
	printf("Unable to open SOFTWARE\\MPICH\\MPD registry key, error %d\n", GetLastError());
	return;
    }
    
    // Read the port
    len = sizeof(int);
    result = RegQueryValueEx(tkey, "port", 0, NULL, (unsigned char *)&m_nPort1, &len);
    m_nPort2 = m_nPort1;
    
    // Read the passphrase
    len = 100;
    result = RegQueryValueEx(tkey, "phrase", 0, NULL, (unsigned char *)m_pszPhrase1, &len);
    if (result == ERROR_SUCCESS)
    {
	m_bNeedPassword1 = false;
	m_bNeedPassword2 = false;
	strcpy(m_pszPhrase2, m_pszPhrase1);
    }
    
    len = 100;
    result = RegQueryValueEx(tkey, "SingleUser", 0, NULL, (unsigned char *)str, &len);
    if (result == ERROR_SUCCESS)
    {
	if (stricmp(str, "yes") == 0)
	{
	    m_bNeedAccount1 = false;
	    m_bNeedAccount2 = false;
	}
    }

    // Read the 
    RegCloseKey(tkey);
}

BOOL CMPDFileTransferDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// Add "About..." menu item to system menu.

	// IDM_ABOUTBOX must be in the system command range.
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	SetIcon(m_hIcon, TRUE);			// Set big icon
	SetIcon(m_hIcon, FALSE);		// Set small icon
	
	// TODO: Add extra initialization here
	bsocket_init();
	ParseRegistry();

	m_rsrHost1.SetInitialPosition(m_host1_edit.m_hWnd,      RSR_LEFT_ANCHOR       | RSR_RIGHT_PROPORTIONAL | RSR_TOP_ANCHOR | RSR_BOTTOM_ANCHOR );
	m_rsrConnect1.SetInitialPosition(m_connect1_btn.m_hWnd, RSR_LEFT_PROPORTIONAL | RSR_RIGHT_PROPORTIONAL | RSR_TOP_ANCHOR | RSR_BOTTOM_ANCHOR );
	m_rsrTree1.SetInitialPosition(m_tree1.m_hWnd,           RSR_LEFT_ANCHOR       | RSR_RIGHT_PROPORTIONAL | RSR_TOP_ANCHOR | RSR_BOTTOM_MOVE );
	m_rsrHostB.SetInitialPosition(m_hostb_edit.m_hWnd,      RSR_LEFT_PROPORTIONAL | RSR_RIGHT_PROPORTIONAL | RSR_TOP_ANCHOR | RSR_BOTTOM_ANCHOR );
	m_rsrHost2.SetInitialPosition(m_host2_edit.m_hWnd,      RSR_LEFT_PROPORTIONAL | RSR_RIGHT_PROPORTIONAL | RSR_TOP_ANCHOR | RSR_BOTTOM_ANCHOR );
	m_rsrConnect2.SetInitialPosition(m_connect2_btn.m_hWnd, RSR_LEFT_PROPORTIONAL | RSR_RIGHT_PROPORTIONAL | RSR_TOP_ANCHOR | RSR_BOTTOM_ANCHOR );
	m_rsrTree2.SetInitialPosition(m_tree2.m_hWnd,           RSR_LEFT_PROPORTIONAL | RSR_RIGHT_MOVE         | RSR_TOP_ANCHOR | RSR_BOTTOM_MOVE );
	m_rsrFileProgress1.SetInitialPosition(m_file_progress1.m_hWnd,     RSR_LEFT_ANCHOR       | RSR_RIGHT_PROPORTIONAL | RSR_TOP_ANCHOR | RSR_BOTTOM_ANCHOR );
	m_rsrFolderProgress1.SetInitialPosition(m_folder_progress1.m_hWnd, RSR_LEFT_ANCHOR       | RSR_RIGHT_PROPORTIONAL | RSR_TOP_ANCHOR | RSR_BOTTOM_ANCHOR );
	m_rsrFileProgress2.SetInitialPosition(m_file_progress2.m_hWnd,     RSR_LEFT_PROPORTIONAL | RSR_RIGHT_MOVE         | RSR_TOP_ANCHOR | RSR_BOTTOM_ANCHOR );
	m_rsrFolderProgress2.SetInitialPosition(m_folder_progress2.m_hWnd, RSR_LEFT_PROPORTIONAL | RSR_RIGHT_MOVE         | RSR_TOP_ANCHOR | RSR_BOTTOM_ANCHOR );

	m_host1 = m_pszHost;

	CImageList* pImageList = new CImageList();
	pImageList->Create(16, 16, ILC_COLOR8, 3, 2);
	//pImageList->Create(16, 16, TRUE, 3, 2);
	//pImageList->Create(16, 16, ILC_COLOR, 3, 2);

	CBitmap bitmap;
	bitmap.LoadBitmap(IDB_FOLDER);
	//pImageList->Add(&bitmap, (COLORREF)0xFFFFFF);
	//pImageList->Add(&bitmap, RGB(255,255,255));
	pImageList->Add(&bitmap, (COLORREF)0);
	bitmap.DeleteObject();
	bitmap.LoadBitmap(IDB_FILE);
	//pImageList->Add(&bitmap, (COLORREF)0xFFFFFF);
	//pImageList->Add(&bitmap, RGB(255,255,255));
	pImageList->Add(&bitmap, (COLORREF)0);
	bitmap.DeleteObject();
	bitmap.LoadBitmap(IDB_FOLDER_OPEN);
	//pImageList->Add(&bitmap, (COLORREF)0xFFFFFF);
	//pImageList->Add(&bitmap, RGB(255,255,255));
	pImageList->Add(&bitmap, (COLORREF)0);
	bitmap.DeleteObject();

	m_tree1.SetImageList(pImageList, TVSIL_NORMAL);
	m_tree2.SetImageList(pImageList, TVSIL_NORMAL);

	m_DropTarget1.Register(&m_tree1);
	m_DropTarget2.Register(&m_tree2);

	UpdateData(FALSE);

	return TRUE;  // return TRUE  unless you set the focus to a control
}

void CMPDFileTransferDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}

// If you add a minimize button to your dialog, you will need the code below
//  to draw the icon.  For MFC applications using the document/view model,
//  this is automatically done for you by the framework.

void CMPDFileTransferDlg::OnPaint() 
{
	if (IsIconic())
	{
		CPaintDC dc(this); // device context for painting

		SendMessage(WM_ICONERASEBKGND, (WPARAM) dc.GetSafeHdc(), 0);

		// Center icon in client rectangle
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// Draw the icon
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

HCURSOR CMPDFileTransferDlg::OnQueryDragIcon()
{
	return (HCURSOR) m_hIcon;
}

void CMPDFileTransferDlg::OnClose() 
{
    if (m_bfd1 != BFD_INVALID_SOCKET)
    {
	WriteString(m_bfd1, "done");
	easy_closesocket(m_bfd1);
    }
    if (m_bfd2 != BFD_INVALID_SOCKET)
    {
	WriteString(m_bfd2, "done");
	easy_closesocket(m_bfd2);
    }
    bsocket_finalize();
	CDialog::OnClose();
}

void CMPDFileTransferDlg::OnSize(UINT nType, int cx, int cy) 
{
	CDialog::OnSize(nType, cx, cy);
	
	m_rsrHost1.Resize(cx, cy);
	m_rsrConnect1.Resize(cx, cy);
	m_rsrTree1.Resize(cx, cy);
	m_rsrFileProgress1.Resize(cx, cy);
	m_rsrFolderProgress1.Resize(cx, cy);

	m_rsrHostB.Resize(cx, cy);
	m_rsrHost2.Resize(cx, cy);
	m_rsrConnect2.Resize(cx, cy);
	m_rsrTree2.Resize(cx, cy);
	m_rsrFileProgress2.Resize(cx, cy);
	m_rsrFolderProgress2.Resize(cx, cy);
}

void CMPDFileTransferDlg::OnConnect1Btn() 
{
    int ret_val;

    UpdateData();

    if (m_bfd1 != BFD_INVALID_SOCKET)
    {
	WriteString(m_bfd1, "done");
	easy_closesocket(m_bfd1);
    }

    if (m_host1.GetLength() == 0)
    {
	m_tree1.DeleteAllItems();
	return;
    }

    if (m_bNeedPassword1)
    {
	CPasswordDialog dlg;
	dlg.DoModal();
	if (dlg.m_bUseDefault)
	    strcpy(m_pszPhrase1, MPD_DEFAULT_PASSPHRASE);
	else
	    strcpy(m_pszPhrase1, dlg.m_phrase);
    }

    if (m_bNeedAccount1)
    {
	CAccountPasswordDlg dlg;
	dlg.m_host = m_host1;
	if (dlg.DoModal() == IDCANCEL)
	{
	    return;
	}
	strcpy(m_pszAccount1, dlg.m_account.GetBuffer(0));
	strcpy(m_pszPassword1, dlg.m_password.GetBuffer(0));
	m_bNeedAccount1 = false;
    }

    HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );

    ret_val = ConnectToMPD(m_host1.GetBuffer(0), m_nPort1, m_pszPhrase1, &m_bfd1);
    if (ret_val == 0)
    {
	int i;
	int nFolders, nFiles;
	char pszStr[256], pszFile[256], pszLength[50];
	char *pszEncoded;

	m_tree1.DeleteAllItems();

	pszEncoded = EncodePassword(m_pszPassword1);
	sprintf(pszStr, "fileinit account=%s password=%s", m_pszAccount1, pszEncoded);
	if (pszEncoded != NULL) free(pszEncoded);
	WriteString(m_bfd1, pszStr);

	sprintf(pszStr, "getdir path=%s\\", m_pszRoot1);
	WriteString(m_bfd1, pszStr);
	ReadString(m_bfd1, pszStr);
	if (strnicmp(pszStr, "ERROR", 5) == 0)
	{
	    strcat(pszStr, "\r\n");
	    strcat(pszStr, m_pszRoot1);
	    MessageBox(pszStr);
	}
	else
	{
	    HTREEITEM pItem, hItem;
	    pItem = m_tree1.InsertItem(m_pszRoot1, 0, 2, TVI_ROOT, TVI_LAST);
	    m_tree1.SetItemData(pItem, TREE_FOLDER_OPENED);

	    nFolders = atoi(pszStr);
	    for (i=0; i<nFolders; i++)
	    {
		ReadString(m_bfd1, pszStr);
		hItem = m_tree1.InsertItem(pszStr, 0, 2, pItem, TVI_LAST);
		m_tree1.SetItemData(hItem, TREE_FOLDER_UNOPENED);
		m_tree1.InsertItem(".", 1, 1, hItem, TVI_LAST);
	    }

	    ReadString(m_bfd1, pszStr);
	    nFiles = atoi(pszStr);
	    for (i=0; i<nFiles; i++)
	    {
		ReadString(m_bfd1, pszFile);
		ReadString(m_bfd1, pszLength);
		sprintf(pszStr, "%s %s", pszFile, pszLength);
		hItem = m_tree1.InsertItem(pszStr, 1, 1, pItem, TVI_LAST);
		m_tree1.SetItemData(hItem, TREE_FILE);
	    }
	}
    }
    else
    {
	CString str;
	if (ret_val == -1)
	    str.Format("Connect to %s failed", m_host1, ret_val);
	else
	{
	    char pszStr1[50], pszStr[256];
	    sprintf(pszStr1, "Connect to %s failed:\r\n", m_host1);
	    Translate_Error(ret_val, pszStr, pszStr1);
	    str = pszStr;
	}
	MessageBox(str);
    }

    SetCursor(hOldCursor);
}

void CMPDFileTransferDlg::OnConnect2Btn() 
{
    int ret_val;

    UpdateData();

    if (m_bfd2 != BFD_INVALID_SOCKET)
    {
	WriteString(m_bfd2, "done");
	easy_closesocket(m_bfd2);
    }

    if (m_host2.GetLength() == 0)
    {
	m_tree2.DeleteAllItems();
	return;
    }

    if (m_bNeedPassword2)
    {
	CPasswordDialog dlg;
	dlg.DoModal();
	if (dlg.m_bUseDefault)
	    strcpy(m_pszPhrase2, MPD_DEFAULT_PASSPHRASE);
	else
	    strcpy(m_pszPhrase2, dlg.m_phrase);
    }

    if (m_bNeedAccount2)
    {
	CAccountPasswordDlg dlg;
	dlg.m_host = m_host2;
	if (dlg.DoModal() == IDCANCEL)
	{
	    return;
	}
	strcpy(m_pszAccount2, dlg.m_account.GetBuffer(0));
	strcpy(m_pszPassword2, dlg.m_password.GetBuffer(0));
	m_bNeedAccount2 = false;
    }

    HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );

    ret_val = ConnectToMPD(m_host2.GetBuffer(0), m_nPort2, m_pszPhrase2, &m_bfd2);
    if (ret_val == 0)
    {
	int i;
	int nFolders, nFiles;
	char pszStr[256], pszFile[256], pszLength[50];
	char *pszEncoded;

	m_tree2.DeleteAllItems();

	pszEncoded = EncodePassword(m_pszPassword2);
	sprintf(pszStr, "fileinit account=%s password=%s", m_pszAccount2, pszEncoded);
	if (pszEncoded != NULL) free(pszEncoded);
	WriteString(m_bfd2, pszStr);

	sprintf(pszStr, "getdir path=%s\\", m_pszRoot2);
	WriteString(m_bfd2, pszStr);
	ReadString(m_bfd2, pszStr);
	if (strnicmp(pszStr, "ERROR", 5) == 0)
	{
	    strcat(pszStr, "\r\n");
	    strcat(pszStr, m_pszRoot2);
	    MessageBox(pszStr);
	}
	else
	{
	    HTREEITEM pItem, hItem;
	    pItem = m_tree2.InsertItem(m_pszRoot2, 0, 2, TVI_ROOT, TVI_LAST);
	    m_tree2.SetItemData(pItem, TREE_FOLDER_OPENED);

	    nFolders = atoi(pszStr);
	    for (i=0; i<nFolders; i++)
	    {
		ReadString(m_bfd2, pszStr);
		hItem = m_tree2.InsertItem(pszStr, 0, 2, pItem, TVI_LAST);
		m_tree2.SetItemData(hItem, TREE_FOLDER_UNOPENED);
		m_tree2.InsertItem(".", 1, 1, hItem, TVI_LAST);
	    }

	    ReadString(m_bfd2, pszStr);
	    nFiles = atoi(pszStr);
	    for (i=0; i<nFiles; i++)
	    {
		ReadString(m_bfd2, pszFile);
		ReadString(m_bfd2, pszLength);
		sprintf(pszStr, "%s %s", pszFile, pszLength);
		hItem = m_tree2.InsertItem(pszStr, 1, 1, pItem, TVI_LAST);
		m_tree2.SetItemData(hItem, TREE_FILE);
	    }
	}
    }
    else
    {
	CString str;
	if (ret_val == -1)
	    str.Format("Connect to %s failed", m_host2, ret_val);
	else
	{
	    char pszStr1[50], pszStr[256];
	    sprintf(pszStr1, "Connect to %s failed:\r\n", m_host2);
	    Translate_Error(ret_val, pszStr, pszStr1);
	    str = pszStr;
	}
	MessageBox(str);
    }

    SetCursor(hOldCursor);
}

void CMPDFileTransferDlg::OnFileConnect() 
{
    CAdvancedConnectDialog dlg;

    UpdateData();

    dlg.m_account1 = m_pszAccount1;
    dlg.m_host1 = m_host1;
    dlg.m_password1 = m_pszPassword1;
    dlg.m_phrase1 = m_pszPhrase1;
    dlg.m_port1 = m_nPort1;
    dlg.m_root1 = m_pszRoot1;

    dlg.m_account2 = m_pszAccount2;
    dlg.m_host2 = m_host2;
    dlg.m_password2 = m_pszPassword2;
    dlg.m_phrase2 = m_pszPhrase2;
    dlg.m_port2 = m_nPort2;
    dlg.m_root2 = m_pszRoot2;

    if (dlg.DoModal() == IDOK)
    {
	strcpy(m_pszAccount1, dlg.m_account1.GetBuffer(0));
	m_host1 = dlg.m_host1;
	strcpy(m_pszPassword1, dlg.m_password1.GetBuffer(0));
	strcpy(m_pszPhrase1, dlg.m_phrase1.GetBuffer(0));
	m_nPort1 = dlg.m_port1;
	strcpy(m_pszRoot1, dlg.m_root1.GetBuffer(0));
	if (m_pszRoot1[strlen(m_pszRoot1)-1] == '\\')
	    m_pszRoot1[strlen(m_pszRoot1)-1] = '\0';

	strcpy(m_pszAccount2, dlg.m_account2.GetBuffer(0));
	m_host2 = dlg.m_host2;
	strcpy(m_pszPassword2, dlg.m_password2.GetBuffer(0));
	strcpy(m_pszPhrase2, dlg.m_phrase2.GetBuffer(0));
	m_nPort2 = dlg.m_port2;
	strcpy(m_pszRoot2, dlg.m_root2.GetBuffer(0));
	if (m_pszRoot2[strlen(m_pszRoot2)-1] == '\\')
	    m_pszRoot2[strlen(m_pszRoot2)-1] = '\0';

	m_bNeedAccount1 = false;
	m_bNeedAccount2 = false;

	UpdateData(FALSE);

	OnConnect1Btn();
	OnConnect2Btn();
    }
}

void CMPDFileTransferDlg::OnFileExit() 
{
    PostMessage(WM_CLOSE);
}

CString GetPathFromItem(CTreeCtrl &tree, HTREEITEM hItem);
/*
{
    CString str;
    HTREEITEM hParent;

    if (hItem == NULL)
	return CString("");

    str = tree.GetItemText(hItem);
    hParent = tree.GetNextItem(hItem, TVGN_PARENT);

    return (GetPathFromItem(tree, hParent) + str + "\\");
}
*/

void CMPDFileTransferDlg::OnItemexpandingTree1(NMHDR* pNMHDR, LRESULT* pResult) 
{
    NM_TREEVIEW* pNMTreeView = (NM_TREEVIEW*)pNMHDR;
    
    int i, nFolders, nFiles;
    char pszStr[MAX_PATH], pszFile[100], pszLength[50];
    DWORD dwData;
    HTREEITEM hChildItem;
    HTREEITEM hItem = pNMTreeView->itemNew.hItem;
    HTREEITEM hItemOld = pNMTreeView->itemOld.hItem;
    CString str;
    
    if (hItem != NULL)
    {
	dwData = m_tree1.GetItemData(hItem);
	if (dwData == TREE_FOLDER_UNOPENED)
	{
	    HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );

	    hChildItem = m_tree1.GetChildItem(hItem);
	    m_tree1.DeleteItem(hChildItem);
	    m_tree1.SetItemData(hItem, TREE_FOLDER_OPENED);
	    
	    str = GetPathFromItem(m_tree1, hItem);
	    
	    sprintf(pszStr, "getdir path=%s", str.GetBuffer(0));
	    WriteString(m_bfd1, pszStr);
	    ReadString(m_bfd1, pszStr);
	    if (strnicmp(pszStr, "ERROR", 5) == 0)
	    {
		MessageBox(pszStr);
	    }
	    else
	    {
		nFolders = atoi(pszStr);
		m_folder_progress1.SetRange(0, nFolders);
		m_folder_progress1.SetStep(1);
		m_folder_progress1.SetPos(0);
		if (nFolders == 0)
		    m_folder_progress1.StepIt();
		for (i=0; i<nFolders; i++)
		{
		    ReadString(m_bfd1, pszStr);
		    hChildItem = m_tree1.InsertItem(pszStr, 0, 2, hItem, TVI_LAST);
		    m_tree1.SetItemData(hChildItem, TREE_FOLDER_UNOPENED);
		    m_tree1.InsertItem(".", 1, 1, hChildItem, TVI_LAST);
		    m_folder_progress1.StepIt();
		}
		
		ReadString(m_bfd1, pszStr);
		nFiles = atoi(pszStr);
		m_file_progress1.SetRange(0, nFiles);
		m_file_progress1.SetStep(1);
		m_file_progress1.SetPos(0);
		for (i=0; i<nFiles; i++)
		{
		    ReadString(m_bfd1, pszFile);
		    ReadString(m_bfd1, pszLength);
		    sprintf(pszStr, "%s %s", pszFile, pszLength);
		    hChildItem = m_tree1.InsertItem(pszStr, 1, 1, hItem, TVI_LAST);
		    m_tree1.SetItemData(hChildItem, TREE_FILE);
		    m_file_progress1.StepIt();
		}
		m_folder_progress1.SetPos(0);
		m_file_progress1.SetPos(0);
	    }
	    SetCursor(hOldCursor);
	}
    }
    
    *pResult = 0;
}

void CMPDFileTransferDlg::OnItemexpandingTree2(NMHDR* pNMHDR, LRESULT* pResult) 
{
    NM_TREEVIEW* pNMTreeView = (NM_TREEVIEW*)pNMHDR;
    
    int i, nFolders, nFiles;
    char pszStr[MAX_PATH], pszFile[100], pszLength[50];
    DWORD dwData;
    HTREEITEM hChildItem;
    HTREEITEM hItem = pNMTreeView->itemNew.hItem;
    CString str;
    
    if (hItem != NULL)
    {
	dwData = m_tree2.GetItemData(hItem);
	if (dwData == TREE_FOLDER_UNOPENED)
	{
	    HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );

	    hChildItem = m_tree2.GetChildItem(hItem);
	    m_tree2.DeleteItem(hChildItem);
	    m_tree2.SetItemData(hItem, TREE_FOLDER_OPENED);
	    
	    str = GetPathFromItem(m_tree2, hItem);
	    
	    sprintf(pszStr, "getdir path=%s", str.GetBuffer(0));
	    WriteString(m_bfd2, pszStr);
	    ReadString(m_bfd2, pszStr);
	    if (strnicmp(pszStr, "ERROR", 5) == 0)
	    {
		MessageBox(pszStr);
	    }
	    else
	    {
		nFolders = atoi(pszStr);
		m_folder_progress2.SetRange(0, nFolders);
		m_folder_progress2.SetStep(1);
		m_folder_progress2.SetPos(0);
		if (nFolders == 0)
		    m_folder_progress2.StepIt();
		for (i=0; i<nFolders; i++)
		{
		    ReadString(m_bfd2, pszStr);
		    hChildItem = m_tree2.InsertItem(pszStr, 0, 2, hItem, TVI_LAST);
		    m_tree2.SetItemData(hChildItem, TREE_FOLDER_UNOPENED);
		    m_tree2.InsertItem(".", 1, 1, hChildItem, TVI_LAST);
		    m_folder_progress2.StepIt();
		}
		
		ReadString(m_bfd2, pszStr);
		nFiles = atoi(pszStr);
		m_file_progress2.SetRange(0, nFiles);
		m_file_progress2.SetStep(1);
		m_file_progress2.SetPos(0);
		for (i=0; i<nFiles; i++)
		{
		    ReadString(m_bfd2, pszFile);
		    ReadString(m_bfd2, pszLength);
		    sprintf(pszStr, "%s %s", pszFile, pszLength);
		    hChildItem = m_tree2.InsertItem(pszStr, 1, 1, hItem, TVI_LAST);
		    m_tree2.SetItemData(hChildItem, TREE_FILE);
		    m_file_progress2.StepIt();
		}
		m_folder_progress2.SetPos(0);
		m_file_progress2.SetPos(0);
	    }

	    SetCursor(hOldCursor);
	}
    }
    
    *pResult = 0;
}

void CMPDFileTransferDlg::OnBegindragTree2(NMHDR* pNMHDR, LRESULT* pResult) 
{
    NM_TREEVIEW* pNMTreeView = (NM_TREEVIEW*)pNMHDR;
    // TODO: Add your control notification handler code here
    
    *pResult = 0;

    //CString str;
    //str = GetPathFromItem(m_tree2, pNMTreeView->itemNew.hItem);
    
    m_hDragItem = pNMTreeView->itemNew.hItem;
    if (m_tree2.GetItemData(pNMTreeView->itemNew.hItem) == TREE_FILE)
	m_dragState = DRAGGING_RIGHT_FILE;
    else
	m_dragState = DRAGGING_RIGHT_FOLDER;
    m_DropTarget2.m_pDlg = this;
    m_DropTarget1.m_pDlg = this;

    //m_tree2.SelectItem(pNMTreeView->itemNew.hItem);

    COleDataSource *pOleSource = new COleDataSource ;
    // Begin Drag operation
    DROPEFFECT dropeffect = pOleSource->DoDragDrop();
    // Remove the highlighting
    //SendMessage(TVM_SELECTITEM, TVGN_DROPHILITE,0);
    // If user is moving item by pressing Shift, delete selected item
    //if ( dropeffect == DROPEFFECT_MOVE) DeleteItem(hTSelItem); 
    delete pOleSource;
}

void CMPDFileTransferDlg::OnBegindragTree1(NMHDR* pNMHDR, LRESULT* pResult) 
{
    NM_TREEVIEW* pNMTreeView = (NM_TREEVIEW*)pNMHDR;
    
    *pResult = 0;
    
    //CString str;
    //str = GetPathFromItem(m_tree1, pNMTreeView->itemNew.hItem);
    
    m_hDragItem = pNMTreeView->itemNew.hItem;
    if (m_tree1.GetItemData(pNMTreeView->itemNew.hItem) == TREE_FILE)
	m_dragState = DRAGGING_LEFT_FILE;
    else
	m_dragState = DRAGGING_LEFT_FOLDER;
    m_DropTarget1.m_pDlg = this;
    m_DropTarget2.m_pDlg = this;
    
    //m_tree1.SelectItem(pNMTreeView->itemNew.hItem);

    COleDataSource *pOleSource = new COleDataSource ;
    // Begin Drag operation
    DROPEFFECT dropeffect = pOleSource->DoDragDrop();
    // Remove the highlighting
    //SendMessage(TVM_SELECTITEM, TVGN_DROPHILITE,0);
    // If user is moving item by pressing Shift, delete selected item
    //if ( dropeffect == DROPEFFECT_MOVE) DeleteItem(hTSelItem); 
    delete pOleSource;
}

/*
void CMPDFileTransferDlg::OnTimer(UINT nIDEvent) 
{
    if (nIDEvent == m_nTimerId)
    //if (nIDEvent == 123)
    {
	KillTimer(m_nTimerId);
	//KillTimer(123);
	m_nTimerId = 0;
	if (m_pTimerTree != NULL)
	{
	    m_pTimerTree->Expand(m_hTimerItem, TVE_EXPAND);
	    m_pTimerTree->SelectDropTarget(m_hTimerItem);
	}
	m_pTimerTree = NULL;
	m_hTimerItem = NULL;
    }
    else
    {
	CString str;
	str.Format("id: %d", nIDEvent);
	MessageBox(str);
    }
    CDialog::OnTimer(nIDEvent);
}
*/
