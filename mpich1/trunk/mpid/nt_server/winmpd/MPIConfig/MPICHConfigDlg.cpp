// MPICHConfigDlg.cpp : implementation file
//

#include "stdafx.h"
#include "MPIConfig.h"
#include "MPICHConfigDlg.h"
#include "FindHostsDlg.h"
#include "qvs.h"
#include "mpd.h"
#include "mpdutil.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

#define USER_MSG_DISABLE   WM_USER + 1
#define USER_MSG_ENABLE    WM_USER + 2
#define USER_MSG_NUM_STEPS WM_USER + 3
#define USER_MSG_STEPIT    WM_USER + 4
#define USER_MSG_GETHOST   WM_USER + 5

/////////////////////////////////////////////////////////////////////////////
// CMPICHConfigDlg dialog


CMPICHConfigDlg::CMPICHConfigDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CMPICHConfigDlg::IDD, pParent)
{
	//{{AFX_DATA_INIT(CMPICHConfigDlg)
	m_add_hostname = _T("");
	m_color_no = FALSE;
	m_color_yes = TRUE;
	m_bdots = FALSE;
	m_bcolor = FALSE;
	m_dots_no = FALSE;
	m_dots_yes = TRUE;
	m_host_color_no = FALSE;
	m_bhost_color = FALSE;
	m_host_color_yes = TRUE;
	m_bhost_dots = FALSE;
	m_host_dots_no = FALSE;
	m_host_dots_yes = TRUE;
	m_bhost_hosts = FALSE;
	m_host_hosts = _T("");
	m_host_jobhost = _T("");
	m_host_jobhost_no = TRUE;
	m_host_jobhost_pwd = _T(MPD_DEFAULT_PASSPHRASE);
	m_host_jobhost_yes = FALSE;
	m_bhost_launch = FALSE;
	m_host_launch = 10;
	m_bhost_mapping = FALSE;
	m_host_mapping_no = FALSE;
	m_host_mapping_yes = TRUE;
	m_bhost_popup_debug = FALSE;
	m_host_popup_debug_no = FALSE;
	m_host_popup_debug_yes = TRUE;
	m_config_host = _T("");
	m_bhost_use_jobhost = FALSE;
	m_bhost_use_jobhost_pwd = FALSE;
	m_bhosts = TRUE;
	m_hosts = _T("");
	m_jobhost = _T("");
	m_jobhost_no = TRUE;
	m_jobhost_pwd = _T(MPD_DEFAULT_PASSPHRASE);
	m_jobhost_yes = FALSE;
	m_blaunch = FALSE;
	m_launch = 10;
	m_bmapping = FALSE;
	m_mapping_no = FALSE;
	m_mapping_yes = TRUE;
	m_mpd_phrase = _T("");
	m_nofm = _T("");
	m_bpopup_debug = FALSE;
	m_popup_debug_no = FALSE;
	m_popup_debug_yes = TRUE;
	m_buse_jobhost = FALSE;
	m_buse_jobhost_pwd = FALSE;
	m_bshow_config = FALSE;
	m_config_host_msg = _T("");
	m_bcatch = FALSE;
	m_catch_no = TRUE;
	m_catch_yes = FALSE;
	m_bhost_catch = FALSE;
	m_host_catch_no = TRUE;
	m_host_catch_yes = FALSE;
	m_bcodes = FALSE;
	m_codes_no = TRUE;
	m_codes_yes = FALSE;
	m_bhost_codes = FALSE;
	m_host_codes_no = TRUE;
	m_host_codes_yes = FALSE;
	m_blocalroot = FALSE;
	m_localroot_no = TRUE;
	m_localroot_yes = FALSE;
	m_bhost_localroot = FALSE;
	m_host_localroot_no = TRUE;
	m_host_localroot_yes = FALSE;
	m_blogfile = FALSE;
	m_logfile = _T("");
	m_logfile_no = TRUE;
	m_logfile_yes = FALSE;
	m_bhost_logfile = FALSE;
	m_host_logfile = _T("");
	m_host_logfile_no = TRUE;
	m_host_logfile_yes = FALSE;
	//}}AFX_DATA_INIT
	m_bToggle = false;
	m_bHostToggle = false;
	m_buse_default_passphrase = true;
	strcpy(m_pszPhrase, MPD_DEFAULT_PASSPHRASE);
	m_nPort = MPD_DEFAULT_PORT;
	m_hApplyBtnThread = NULL;
}


void CMPICHConfigDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
	//{{AFX_DATA_MAP(CMPICHConfigDlg)
	DDX_Control(pDX, IDC_REDIRECT_MPD_STATIC, m_logfile_static);
	DDX_Control(pDX, IDC_HOST_REDIRECT_MPD_YES, m_host_logfile_yes_btn);
	DDX_Control(pDX, IDC_HOST_REDIRECT_MPD_NO, m_host_logfile_no_btn);
	DDX_Control(pDX, IDC_HOST_REDIRECT_MPD_EDIT, m_host_logfile_edit);
	DDX_Control(pDX, IDC_HOST_REDIRECT_MPD_CHK, m_host_logfile_chk);
	DDX_Control(pDX, IDC_REDIRECT_MPD_YES, m_logfile_yes_btn);
	DDX_Control(pDX, IDC_REDIRECT_MPD_NO, m_logfile_no_btn);
	DDX_Control(pDX, IDC_REDIRECT_MPD_EDIT, m_logfile_edit);
	DDX_Control(pDX, IDC_HOST_CODES_YES, m_host_codes_yes_btn);
	DDX_Control(pDX, IDC_HOST_CODES_NO, m_host_codes_no_btn);
	DDX_Control(pDX, IDC_HOST_CODES_CHK, m_host_codes_chk);
	DDX_Control(pDX, IDC_CODES_YES, m_codes_yes_btn);
	DDX_Control(pDX, IDC_CODES_NO, m_codes_no_btn);
	DDX_Control(pDX, IDC_HOST_LOCALROOT_YES, m_host_localroot_yes_btn);
	DDX_Control(pDX, IDC_HOST_LOCALROOT_NO, m_host_localroot_no_btn);
	DDX_Control(pDX, IDC_HOST_LOCALROOT_CHK, m_host_localroot_chk);
	DDX_Control(pDX, IDC_LOCALROOT_YES, m_localroot_yes_btn);
	DDX_Control(pDX, IDC_LOCALROOT_NO, m_localroot_no_btn);
	DDX_Control(pDX, IDC_HOST_MSG_STATIC, m_config_host_msg_static);
	DDX_Control(pDX, IDC_HOST_CATCH_YES, m_host_catch_yes_btn);
	DDX_Control(pDX, IDC_HOST_CATCH_NO, m_host_catch_no_btn);
	DDX_Control(pDX, IDC_HOST_CATCH_CHK, m_host_catch_chk);
	DDX_Control(pDX, IDC_CATCH_YES, m_catch_yes_btn);
	DDX_Control(pDX, IDC_CATCH_NO, m_catch_no_btn);
	DDX_Control(pDX, IDC_HOST_STATIC, m_config_host_static);
	DDX_Control(pDX, IDC_MODIFY_STATIC, m_modify_static);
	DDX_Control(pDX, IDC_USE_JOBHOST_PWD_CHK, m_use_jobhost_pwd_chk);
	DDX_Control(pDX, IDC_TOGGLE_BTN, m_toggle_btn);
	DDX_Control(pDX, IDC_PROGRESS, m_progress);
	DDX_Control(pDX, IDC_POPUP_DEBUG_YES, m_popup_debug_yes_btn);
	DDX_Control(pDX, IDC_POPUP_DEBUG_NO, m_popup_debug_no_btn);
	DDX_Control(pDX, IDC_MPD_PHRASE, m_mpd_phrase_edit);
	DDX_Control(pDX, IDC_MODIFY_BTN, m_modify_btn);
	DDX_Control(pDX, IDC_MAPPING_YES, m_mapping_yes_btn);
	DDX_Control(pDX, IDC_MAPPING_NO, m_mapping_no_btn);
	DDX_Control(pDX, IDC_LAUNCH_EDIT, m_launch_edit);
	DDX_Control(pDX, IDC_JOBHOST_YES, m_jobhost_yes_btn);
	DDX_Control(pDX, IDC_JOBHOST_STATIC, m_jobhost_static);
	DDX_Control(pDX, IDC_JOBHOST_PWD_EDIT, m_jobhost_pwd_edit);
	DDX_Control(pDX, IDC_JOBHOST_NO, m_jobhost_no_btn);
	DDX_Control(pDX, IDC_JOBHOST_EDIT, m_jobhost_edit);
	DDX_Control(pDX, IDC_HOSTS_EDIT, m_hosts_edit);
	DDX_Control(pDX, IDC_HOST_USE_JOBHOST_PWD_CHK, m_host_use_jobhost_pwd_chk);
	DDX_Control(pDX, IDC_HOST_USE_JOBHOST_CHK, m_host_use_jobhost_chk);
	DDX_Control(pDX, IDC_HOST_TOGGLE_BTN, m_host_toggle_btn);
	DDX_Control(pDX, IDC_HOST_POPUP_DEBUG_YES, m_host_popup_debug_yes_btn);
	DDX_Control(pDX, IDC_HOST_POPUP_DEBUG_NO, m_host_popup_debug_no_btn);
	DDX_Control(pDX, IDC_HOST_POPUP_DEBUG_CHK, m_host_popup_debug_chk);
	DDX_Control(pDX, IDC_HOST_MAPPING_YES, m_host_mapping_yes_btn);
	DDX_Control(pDX, IDC_HOST_MAPPING_NO, m_host_mapping_no_btn);
	DDX_Control(pDX, IDC_HOST_MAPPING_CHK, m_host_mapping_chk);
	DDX_Control(pDX, IDC_HOST_LIST, m_host_list);
	DDX_Control(pDX, IDC_HOST_LAUNCH_EDIT, m_host_launch_edit);
	DDX_Control(pDX, IDC_HOST_LAUNCH_CHK, m_host_launch_chk);
	DDX_Control(pDX, IDC_HOST_HOSTS_CHK, m_host_hosts_chk);
	DDX_Control(pDX, IDC_HOST_DOTS_CHK, m_host_dots_chk);
	DDX_Control(pDX, IDC_HOST_COLOR_CHK, m_host_color_chk);
	DDX_Control(pDX, IDC_HOST_JOBHOST_YES, m_host_jobhost_yes_btn);
	DDX_Control(pDX, IDC_HOST_JOBHOST_PWD_EDIT, m_host_jobhost_pwd_edit);
	DDX_Control(pDX, IDC_HOST_JOBHOST_NO, m_host_jobhost_no_btn);
	DDX_Control(pDX, IDC_HOST_JOBHOST_EDIT, m_host_jobhost_edit);
	DDX_Control(pDX, IDC_HOST_HOSTS_EDIT, m_host_hosts_edit);
	DDX_Control(pDX, IDC_HOST_DOTS_YES, m_host_dots_yes_btn);
	DDX_Control(pDX, IDC_HOST_DOTS_NO, m_host_dots_no_btn);
	DDX_Control(pDX, IDC_HOST_COLOR_YES, m_host_color_yes_btn);
	DDX_Control(pDX, IDC_HOST_COLOR_NO, m_host_color_no_btn);
	DDX_Control(pDX, IDC_DOTS_YES, m_dots_yes_btn);
	DDX_Control(pDX, IDC_DOTS_NO, m_dots_no_btn);
	DDX_Control(pDX, IDC_COLOR_YES, m_color_yes_btn);
	DDX_Control(pDX, IDC_COLOR_NO, m_color_no_btn);
	DDX_Control(pDX, IDC_APPLY_SINGLE_BTN, m_apply_single_btn);
	DDX_Control(pDX, IDC_APPLY_BTN, m_apply_btn);
	DDX_Control(pDX, IDC_ADD_BTN, m_add_btn);
	DDX_Text(pDX, IDC_ADD_HOSTNAME, m_add_hostname);
	DDX_Check(pDX, IDC_COLOR_NO, m_color_no);
	DDX_Check(pDX, IDC_COLOR_YES, m_color_yes);
	DDX_Check(pDX, IDC_DOTS_CHK, m_bdots);
	DDX_Check(pDX, IDC_COLOR_CHK, m_bcolor);
	DDX_Check(pDX, IDC_DOTS_NO, m_dots_no);
	DDX_Check(pDX, IDC_DOTS_YES, m_dots_yes);
	DDX_Check(pDX, IDC_HOST_COLOR_NO, m_host_color_no);
	DDX_Check(pDX, IDC_HOST_COLOR_CHK, m_bhost_color);
	DDX_Check(pDX, IDC_HOST_COLOR_YES, m_host_color_yes);
	DDX_Check(pDX, IDC_HOST_DOTS_CHK, m_bhost_dots);
	DDX_Check(pDX, IDC_HOST_DOTS_NO, m_host_dots_no);
	DDX_Check(pDX, IDC_HOST_DOTS_YES, m_host_dots_yes);
	DDX_Check(pDX, IDC_HOST_HOSTS_CHK, m_bhost_hosts);
	DDX_Text(pDX, IDC_HOST_HOSTS_EDIT, m_host_hosts);
	DDX_Text(pDX, IDC_HOST_JOBHOST_EDIT, m_host_jobhost);
	DDX_Check(pDX, IDC_HOST_JOBHOST_NO, m_host_jobhost_no);
	DDX_Text(pDX, IDC_HOST_JOBHOST_PWD_EDIT, m_host_jobhost_pwd);
	DDX_Check(pDX, IDC_HOST_JOBHOST_YES, m_host_jobhost_yes);
	DDX_Check(pDX, IDC_HOST_LAUNCH_CHK, m_bhost_launch);
	DDX_Text(pDX, IDC_HOST_LAUNCH_EDIT, m_host_launch);
	DDV_MinMaxInt(pDX, m_host_launch, 1, 1000);
	DDX_Check(pDX, IDC_HOST_MAPPING_CHK, m_bhost_mapping);
	DDX_Check(pDX, IDC_HOST_MAPPING_NO, m_host_mapping_no);
	DDX_Check(pDX, IDC_HOST_MAPPING_YES, m_host_mapping_yes);
	DDX_Check(pDX, IDC_HOST_POPUP_DEBUG_CHK, m_bhost_popup_debug);
	DDX_Check(pDX, IDC_HOST_POPUP_DEBUG_NO, m_host_popup_debug_no);
	DDX_Check(pDX, IDC_HOST_POPUP_DEBUG_YES, m_host_popup_debug_yes);
	DDX_Text(pDX, IDC_HOST_STATIC, m_config_host);
	DDX_Check(pDX, IDC_HOST_USE_JOBHOST_CHK, m_bhost_use_jobhost);
	DDX_Check(pDX, IDC_HOST_USE_JOBHOST_PWD_CHK, m_bhost_use_jobhost_pwd);
	DDX_Check(pDX, IDC_HOSTS_CHK, m_bhosts);
	DDX_Text(pDX, IDC_HOSTS_EDIT, m_hosts);
	DDX_Text(pDX, IDC_JOBHOST_EDIT, m_jobhost);
	DDX_Check(pDX, IDC_JOBHOST_NO, m_jobhost_no);
	DDX_Text(pDX, IDC_JOBHOST_PWD_EDIT, m_jobhost_pwd);
	DDX_Check(pDX, IDC_JOBHOST_YES, m_jobhost_yes);
	DDX_Check(pDX, IDC_LAUNCH_CHK, m_blaunch);
	DDX_Text(pDX, IDC_LAUNCH_EDIT, m_launch);
	DDV_MinMaxInt(pDX, m_launch, 1, 1000);
	DDX_Check(pDX, IDC_MAPPING_CHK, m_bmapping);
	DDX_Check(pDX, IDC_MAPPING_NO, m_mapping_no);
	DDX_Check(pDX, IDC_MAPPING_YES, m_mapping_yes);
	DDX_Text(pDX, IDC_MPD_PHRASE, m_mpd_phrase);
	DDX_Text(pDX, IDC_N_OF_M_STATIC, m_nofm);
	DDX_Check(pDX, IDC_POPUP_DEBUG_CHK, m_bpopup_debug);
	DDX_Check(pDX, IDC_POPUP_DEBUG_NO, m_popup_debug_no);
	DDX_Check(pDX, IDC_POPUP_DEBUG_YES, m_popup_debug_yes);
	DDX_Check(pDX, IDC_USE_JOBHOST_CHK, m_buse_jobhost);
	DDX_Check(pDX, IDC_USE_JOBHOST_PWD_CHK, m_buse_jobhost_pwd);
	DDX_Check(pDX, IDC_SHOW_CONFIG_CHK, m_bshow_config);
	DDX_Control(pDX, IDC_MPD_PHRASE_DEFAULT_RADIO, m_mpd_default_radio);
	DDX_Text(pDX, IDC_HOST_MSG_STATIC, m_config_host_msg);
	DDX_Check(pDX, IDC_CATCH_CHK, m_bcatch);
	DDX_Check(pDX, IDC_CATCH_NO, m_catch_no);
	DDX_Check(pDX, IDC_CATCH_YES, m_catch_yes);
	DDX_Check(pDX, IDC_HOST_CATCH_CHK, m_bhost_catch);
	DDX_Check(pDX, IDC_HOST_CATCH_NO, m_host_catch_no);
	DDX_Check(pDX, IDC_HOST_CATCH_YES, m_host_catch_yes);
	DDX_Check(pDX, IDC_CODES_CHK, m_bcodes);
	DDX_Check(pDX, IDC_CODES_NO, m_codes_no);
	DDX_Check(pDX, IDC_CODES_YES, m_codes_yes);
	DDX_Check(pDX, IDC_HOST_CODES_CHK, m_bhost_codes);
	DDX_Check(pDX, IDC_HOST_CODES_NO, m_host_codes_no);
	DDX_Check(pDX, IDC_HOST_CODES_YES, m_host_codes_yes);
	DDX_Check(pDX, IDC_LOCALROOT_CHK, m_blocalroot);
	DDX_Check(pDX, IDC_LOCALROOT_NO, m_localroot_no);
	DDX_Check(pDX, IDC_LOCALROOT_YES, m_localroot_yes);
	DDX_Check(pDX, IDC_HOST_LOCALROOT_CHK, m_bhost_localroot);
	DDX_Check(pDX, IDC_HOST_LOCALROOT_NO, m_host_localroot_no);
	DDX_Check(pDX, IDC_HOST_LOCALROOT_YES, m_host_localroot_yes);
	DDX_Check(pDX, IDC_REDIRECT_MPD_CHK, m_blogfile);
	DDX_Text(pDX, IDC_REDIRECT_MPD_EDIT, m_logfile);
	DDX_Check(pDX, IDC_REDIRECT_MPD_NO, m_logfile_no);
	DDX_Check(pDX, IDC_REDIRECT_MPD_YES, m_logfile_yes);
	DDX_Check(pDX, IDC_HOST_REDIRECT_MPD_CHK, m_bhost_logfile);
	DDX_Text(pDX, IDC_HOST_REDIRECT_MPD_EDIT, m_host_logfile);
	DDX_Check(pDX, IDC_HOST_REDIRECT_MPD_NO, m_host_logfile_no);
	DDX_Check(pDX, IDC_HOST_REDIRECT_MPD_YES, m_host_logfile_yes);
	//}}AFX_DATA_MAP
}


BEGIN_MESSAGE_MAP(CMPICHConfigDlg, CDialog)
	//{{AFX_MSG_MAP(CMPICHConfigDlg)
	ON_BN_CLICKED(IDC_ADD_BTN, OnAddBtn)
	ON_BN_CLICKED(IDC_SELECT_BTN, OnSelectBtn)
	ON_BN_CLICKED(IDC_MPD_PHRASE_RADIO, OnMpdPhraseRadio)
	ON_BN_CLICKED(IDC_MPD_PHRASE_DEFAULT_RADIO, OnMpdPhraseDefaultRadio)
	ON_BN_CLICKED(IDC_TOGGLE_BTN, OnToggleBtn)
	ON_BN_CLICKED(IDC_HOSTS_CHK, OnHostsChk)
	ON_BN_CLICKED(IDC_LAUNCH_CHK, OnLaunchChk)
	ON_BN_CLICKED(IDC_USE_JOBHOST_CHK, OnUseJobhostChk)
	ON_BN_CLICKED(IDC_JOBHOST_YES, OnJobhostYes)
	ON_BN_CLICKED(IDC_JOBHOST_NO, OnJobhostNo)
	ON_BN_CLICKED(IDC_USE_JOBHOST_PWD_CHK, OnUseJobhostPwdChk)
	ON_BN_CLICKED(IDC_COLOR_CHK, OnColorChk)
	ON_BN_CLICKED(IDC_COLOR_YES, OnColorYes)
	ON_BN_CLICKED(IDC_COLOR_NO, OnColorNo)
	ON_BN_CLICKED(IDC_DOTS_CHK, OnDotsChk)
	ON_BN_CLICKED(IDC_DOTS_YES, OnDotsYes)
	ON_BN_CLICKED(IDC_DOTS_NO, OnDotsNo)
	ON_BN_CLICKED(IDC_MAPPING_CHK, OnMappingChk)
	ON_BN_CLICKED(IDC_MAPPING_YES, OnMappingYes)
	ON_BN_CLICKED(IDC_MAPPING_NO, OnMappingNo)
	ON_BN_CLICKED(IDC_POPUP_DEBUG_CHK, OnPopupDebugChk)
	ON_BN_CLICKED(IDC_POPUP_DEBUG_YES, OnPopupDebugYes)
	ON_BN_CLICKED(IDC_POPUP_DEBUG_NO, OnPopupDebugNo)
	ON_BN_CLICKED(IDC_APPLY_BTN, OnApplyBtn)
	ON_BN_CLICKED(IDC_APPLY_SINGLE_BTN, OnApplySingleBtn)
	ON_BN_CLICKED(IDC_HOST_TOGGLE_BTN, OnHostToggleBtn)
	ON_BN_CLICKED(IDC_HOST_HOSTS_CHK, OnHostHostsChk)
	ON_BN_CLICKED(IDC_HOST_LAUNCH_CHK, OnHostLaunchChk)
	ON_BN_CLICKED(IDC_HOST_USE_JOBHOST_CHK, OnHostUseJobhostChk)
	ON_BN_CLICKED(IDC_HOST_JOBHOST_YES, OnHostJobhostYes)
	ON_BN_CLICKED(IDC_HOST_JOBHOST_NO, OnHostJobhostNo)
	ON_BN_CLICKED(IDC_HOST_USE_JOBHOST_PWD_CHK, OnHostUseJobhostPwdChk)
	ON_BN_CLICKED(IDC_HOST_COLOR_CHK, OnHostColorChk)
	ON_BN_CLICKED(IDC_HOST_COLOR_YES, OnHostColorYes)
	ON_BN_CLICKED(IDC_HOST_COLOR_NO, OnHostColorNo)
	ON_BN_CLICKED(IDC_HOST_DOTS_CHK, OnHostDotsChk)
	ON_BN_CLICKED(IDC_HOST_DOTS_YES, OnHostDotsYes)
	ON_BN_CLICKED(IDC_HOST_DOTS_NO, OnHostDotsNo)
	ON_BN_CLICKED(IDC_HOST_MAPPING_CHK, OnHostMappingChk)
	ON_BN_CLICKED(IDC_HOST_MAPPING_YES, OnHostMappingYes)
	ON_BN_CLICKED(IDC_HOST_MAPPING_NO, OnHostMappingNo)
	ON_BN_CLICKED(IDC_HOST_POPUP_DEBUG_CHK, OnHostPopupDebugChk)
	ON_BN_CLICKED(IDC_HOST_POPUP_DEBUG_YES, OnHostPopupDebugYes)
	ON_BN_CLICKED(IDC_HOST_POPUP_DEBUG_NO, OnHostPopupDebugNo)
	ON_BN_CLICKED(IDC_MODIFY_BTN, OnModifyBtn)
	ON_BN_CLICKED(IDC_SHOW_CONFIG_CHK, OnShowConfigChk)
	ON_NOTIFY(LVN_KEYDOWN, IDC_HOST_LIST, OnKeydownHostList)
	ON_WM_CLOSE()
	ON_NOTIFY(LVN_ITEMCHANGING, IDC_HOST_LIST, OnItemchangingHostList)
	ON_BN_CLICKED(IDC_HOST_CATCH_CHK, OnHostCatchChk)
	ON_BN_CLICKED(IDC_HOST_CATCH_YES, OnHostCatchYes)
	ON_BN_CLICKED(IDC_HOST_CATCH_NO, OnHostCatchNo)
	ON_BN_CLICKED(IDC_CATCH_CHK, OnCatchChk)
	ON_BN_CLICKED(IDC_CATCH_YES, OnCatchYes)
	ON_BN_CLICKED(IDC_CATCH_NO, OnCatchNo)
	ON_BN_CLICKED(IDC_HOST_CODES_YES, OnHostCodesYes)
	ON_BN_CLICKED(IDC_HOST_CODES_NO, OnHostCodesNo)
	ON_BN_CLICKED(IDC_HOST_CODES_CHK, OnHostCodesChk)
	ON_BN_CLICKED(IDC_CODES_CHK, OnCodesChk)
	ON_BN_CLICKED(IDC_CODES_YES, OnCodesYes)
	ON_BN_CLICKED(IDC_CODES_NO, OnCodesNo)
	ON_BN_CLICKED(IDC_HOST_LOCALROOT_YES, OnHostLocalRootYes)
	ON_BN_CLICKED(IDC_HOST_LOCALROOT_NO, OnHostLocalRootNo)
	ON_BN_CLICKED(IDC_HOST_LOCALROOT_CHK, OnHostLocalRootChk)
	ON_BN_CLICKED(IDC_LOCALROOT_CHK, OnLocalRootChk)
	ON_BN_CLICKED(IDC_LOCALROOT_YES, OnLocalRootYes)
	ON_BN_CLICKED(IDC_LOCALROOT_NO, OnLocalRootNo)
	ON_BN_CLICKED(IDC_REDIRECT_MPD_CHK, OnRedirectMpdChk)
	ON_BN_CLICKED(IDC_REDIRECT_MPD_NO, OnRedirectMpdNo)
	ON_BN_CLICKED(IDC_REDIRECT_MPD_YES, OnRedirectMpdYes)
	ON_BN_CLICKED(IDC_HOST_REDIRECT_MPD_CHK, OnHostRedirectMpdChk)
	ON_BN_CLICKED(IDC_HOST_REDIRECT_MPD_NO, OnHostRedirectMpdNo)
	ON_BN_CLICKED(IDC_HOST_REDIRECT_MPD_YES, OnHostRedirectMpdYes)
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CMPICHConfigDlg message handlers

void CMPICHConfigDlg::ParseRegistry()
{
    HKEY tkey;
    DWORD result, len;
    char name[100];
    
    // Set the defaults.
    m_nPort = MPD_DEFAULT_PORT;
    len = 100;
    GetComputerName(name, &len);
    m_add_hostname = name;
    
    m_bNeedPassword = true;

    // Open the root key
    if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, MPD_REGISTRY_KEY,
	0, KEY_ALL_ACCESS, &tkey) != ERROR_SUCCESS)
    {
	printf("Unable to open SOFTWARE\\MPICH\\MPD registry key, error %d\n", GetLastError());
	return;
    }
    
    // Read the port
    len = sizeof(int);
    result = RegQueryValueEx(tkey, "port", 0, NULL, (unsigned char *)&m_nPort, &len);
    
    // Check to see if a passphrase has been set and set it to the default if necessary.
    len = 100;
    result = RegQueryValueEx(tkey, "phrase", 0, NULL, (unsigned char *)m_pszPhrase, &len);
    if (result == ERROR_SUCCESS)
	m_bNeedPassword = false;
    
    RegCloseKey(tkey);
}

void CMPICHConfigDlg::OnAddBtn() 
{
    UpdateData();
    
    if (m_add_hostname.GetLength() != 0)
    {
	CString str;
	int n = m_host_list.GetItemCount();
	if (n != LB_ERR)
	{
	    bool bFound = false;
	    for (int i=0; i<n; i++)
	    {
		str = m_host_list.GetItemText(i, 0);
		if (str.CompareNoCase(m_add_hostname) == 0)
		{
		    bFound = true;
		    //break;
		}
	    }
	    if (!bFound)
	    {
		m_host_list.InsertItem(0, m_add_hostname);
		GetHostsString();
		UpdateData(FALSE);
	    }
	}
    }
}

void CMPICHConfigDlg::OnSelectBtn() 
{
    CFindHostsDlg dlg;

    UpdateData();
    if (dlg.DoModal() == IDOK)
    {
	QVS_Container qvs;
	char str[100];

	m_host_list.DeleteAllItems();

	qvs.decode_string((char*)(LPCTSTR)dlg.m_encoded_hosts);
	if (qvs.first(str, 100))
	{
	    m_host_list.InsertItem(0, str);
	    while (qvs.next(str, 100))
	    {
		m_host_list.InsertItem(0, str);
	    }
	}
	GetHostsString();
    }
}

void CMPICHConfigDlg::OnMpdPhraseRadio() 
{
    m_mpd_phrase_edit.EnableWindow();
    m_buse_default_passphrase = false;
    m_bNeedPassword = true;
}

void CMPICHConfigDlg::OnMpdPhraseDefaultRadio() 
{
    m_mpd_phrase_edit.EnableWindow(FALSE);
    m_buse_default_passphrase = true;
}

void CMPICHConfigDlg::OnToggleBtn() 
{
    UpdateData();

    m_bToggle = !m_bToggle;
    
    m_bcolor = m_bToggle;
    m_bdots = m_bToggle;
    m_bhosts = m_bToggle;
    m_blaunch = m_bToggle;
    m_bmapping = m_bToggle;
    m_bpopup_debug = m_bToggle;
    m_buse_jobhost = m_bToggle;
    m_bcatch = m_bToggle;
    m_bcodes = m_bToggle;
    m_blocalroot = m_bToggle;
    m_blogfile = m_bToggle;

    UpdateData(FALSE);

    OnColorChk();
    OnDotsChk();
    OnHostsChk();
    OnLaunchChk();
    OnMappingChk();
    OnPopupDebugChk();
    OnUseJobhostChk();
    OnCatchChk();
    OnCodesChk();
    OnLocalRootChk();
    OnRedirectMpdChk();
}

void CMPICHConfigDlg::OnHostsChk() 
{
    UpdateData();
    m_hosts_edit.EnableWindow(m_bhosts);
    UpdateApplyButtonStates();
}

void CMPICHConfigDlg::OnLaunchChk() 
{
    UpdateData();
    m_launch_edit.EnableWindow(m_blaunch);
    UpdateApplyButtonStates();
}

void CMPICHConfigDlg::OnUseJobhostChk() 
{
    UpdateData();
    m_jobhost_yes_btn.EnableWindow(m_buse_jobhost);
    m_jobhost_no_btn.EnableWindow(m_buse_jobhost);

    m_jobhost_static.EnableWindow(m_buse_jobhost && m_jobhost_yes);
    m_jobhost_edit.EnableWindow(m_buse_jobhost && m_jobhost_yes);
    m_use_jobhost_pwd_chk.EnableWindow(m_buse_jobhost && m_jobhost_yes);
    m_jobhost_pwd_edit.EnableWindow(m_buse_jobhost && m_jobhost_yes && m_buse_jobhost_pwd);
    UpdateApplyButtonStates();
}

void CMPICHConfigDlg::OnJobhostYes() 
{
    UpdateData();
    m_jobhost_yes = TRUE;
    m_jobhost_no = FALSE;
    m_jobhost_static.EnableWindow();
    m_jobhost_edit.EnableWindow();
    m_use_jobhost_pwd_chk.EnableWindow();
    m_jobhost_pwd_edit.EnableWindow(m_buse_jobhost_pwd);
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnJobhostNo() 
{
    UpdateData();
    m_jobhost_yes = FALSE;
    m_jobhost_no = TRUE;
    m_jobhost_static.EnableWindow(FALSE);
    m_jobhost_edit.EnableWindow(FALSE);
    m_use_jobhost_pwd_chk.EnableWindow(FALSE);
    m_jobhost_pwd_edit.EnableWindow(FALSE);
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnUseJobhostPwdChk() 
{
    UpdateData();
    m_jobhost_pwd_edit.EnableWindow(m_buse_jobhost_pwd);
}

void CMPICHConfigDlg::OnColorChk() 
{
    UpdateData();
    m_color_yes_btn.EnableWindow(m_bcolor);
    m_color_no_btn.EnableWindow(m_bcolor);
    UpdateApplyButtonStates();
}

void CMPICHConfigDlg::OnColorYes() 
{
    UpdateData();
    m_color_yes = TRUE;
    m_color_no = FALSE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnColorNo() 
{
    UpdateData();
    m_color_yes = FALSE;
    m_color_no = TRUE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnDotsChk() 
{
    UpdateData();
    m_dots_yes_btn.EnableWindow(m_bdots);
    m_dots_no_btn.EnableWindow(m_bdots);
    UpdateApplyButtonStates();
}

void CMPICHConfigDlg::OnDotsYes() 
{
    UpdateData();
    m_dots_yes = TRUE;
    m_dots_no = FALSE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnDotsNo() 
{
    UpdateData();
    m_dots_yes = FALSE;
    m_dots_no = TRUE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnMappingChk() 
{
    UpdateData();
    m_mapping_yes_btn.EnableWindow(m_bmapping);
    m_mapping_no_btn.EnableWindow(m_bmapping);
    UpdateApplyButtonStates();
}

void CMPICHConfigDlg::OnMappingYes() 
{
    UpdateData();
    m_mapping_yes = TRUE;
    m_mapping_no = FALSE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnMappingNo() 
{
    UpdateData();
    m_mapping_yes = FALSE;
    m_mapping_no = TRUE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnPopupDebugChk() 
{
    UpdateData();
    m_popup_debug_no_btn.EnableWindow(m_bpopup_debug);
    m_popup_debug_yes_btn.EnableWindow(m_bpopup_debug);
    UpdateApplyButtonStates();
}

void CMPICHConfigDlg::OnPopupDebugYes() 
{
    UpdateData();
    m_popup_debug_yes = TRUE;
    m_popup_debug_no = FALSE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnPopupDebugNo() 
{
    UpdateData();
    m_popup_debug_yes = FALSE;
    m_popup_debug_no = TRUE;
    UpdateData(FALSE);
}

void ApplyBtnThread(CMPICHConfigDlg *pDlg)
{
    int i;
    int num_hosts;
    char pszStr[8192];
    char host[100];
    SOCKET sock;

    num_hosts = pDlg->m_host_list.GetItemCount();
    if (num_hosts == 0)
    {
	CloseHandle(pDlg->m_hApplyBtnThread);
	pDlg->m_hApplyBtnThread = NULL;
	return;
    }
    
    if (pDlg->m_bNeedPassword)
    {
	if (pDlg->m_buse_default_passphrase)
	    strcpy(pDlg->m_pszPhrase, MPD_DEFAULT_PASSPHRASE);
	else
	    strcpy(pDlg->m_pszPhrase, pDlg->m_mpd_phrase);
    }

    // disable the dialog buttons
    PostMessage(pDlg->m_hWnd, USER_MSG_DISABLE, 0, 0);

    PostMessage(pDlg->m_hWnd, USER_MSG_NUM_STEPS, num_hosts, 0);

    for (i=0; i<num_hosts; i++)
    {
	if (pDlg->m_host_list.GetItemText(i, 0, host, 100) == 0)
	{
	    PostMessage(pDlg->m_hWnd, USER_MSG_STEPIT, 0, 0);
	    continue;
	}
	
	if (ConnectToMPD(host, pDlg->m_nPort, pDlg->m_pszPhrase, &sock) != 0)
	{
	    PostMessage(pDlg->m_hWnd, USER_MSG_STEPIT, 0, 0);
	    continue;
	}

	// set hosts
	if (pDlg->m_bhosts)
	{
	    sprintf(pszStr, "lset hosts=%s", pDlg->m_hosts);
	    WriteString(sock, pszStr);
	}
	// set launch timeout
	if (pDlg->m_blaunch)
	{
	    sprintf(pszStr, "lset timeout=%d", pDlg->m_launch);
	    WriteString(sock, pszStr);
	}
	// set jobhost
	if (pDlg->m_buse_jobhost)
	{
	    sprintf(pszStr, "lset usejobhost=%s", (pDlg->m_jobhost_yes) ? "yes" : "no");
	    WriteString(sock, pszStr);
	    if (pDlg->m_jobhost_yes)
	    {
		sprintf(pszStr, "lset jobhost=%s", pDlg->m_jobhost);
		WriteString(sock, pszStr);
		if (pDlg->m_buse_jobhost_pwd)
		{
		    sprintf(pszStr, "lset jobhostpwd=%s", pDlg->m_jobhost_pwd);
		    WriteString(sock, pszStr);
		}
		else
		{
		    WriteString(sock, "ldelete jobhostpwd");
		}
	    }
	}
	// set logfile
	if (pDlg->m_blogfile)
	{
	    if (pDlg->m_logfile_yes)
		sprintf(pszStr, "setdbgoutput %s", pDlg->m_logfile);
	    else
		sprintf(pszStr, "canceldbgoutput");
	    WriteString(sock, pszStr);
	    ReadString(sock, pszStr);
	}
	// set color
	if (pDlg->m_bcolor)
	{
	    sprintf(pszStr, "lset nocolor=%s", (pDlg->m_color_yes) ? "no" : "yes");
	    WriteString(sock, pszStr);
	}
	// set dots
	if (pDlg->m_bdots)
	{
	    sprintf(pszStr, "lset nodots=%s", (pDlg->m_dots_yes) ? "no" : "yes");
	    WriteString(sock, pszStr);
	}
	// set mapping
	if (pDlg->m_bmapping)
	{
	    sprintf(pszStr, "lset nomapping=%s", (pDlg->m_mapping_yes) ? "no" : "yes");
	    WriteString(sock, pszStr);
	}
	// set popup_debug
	if (pDlg->m_bpopup_debug)
	{
	    sprintf(pszStr, "lset nopopup_debug=%s", (pDlg->m_popup_debug_yes) ? "no" : "yes");
	    WriteString(sock, pszStr);
	}
	// set dbg
	if (pDlg->m_bcatch)
	{
	    sprintf(pszStr, "lset dbg=%s", (pDlg->m_catch_yes) ? "yes" : "no");
	    WriteString(sock, pszStr);
	}
	// set exitcodes
	if (pDlg->m_bcodes)
	{
	    sprintf(pszStr, "lset exitcodes=%s", (pDlg->m_codes_yes) ? "yes" : "no");
	    WriteString(sock, pszStr);
	}
	// set localroot
	if (pDlg->m_blocalroot)
	{
	    sprintf(pszStr, "lset localroot=%s", (pDlg->m_localroot_yes) ? "yes" : "no");
	    WriteString(sock, pszStr);
	}
	// close the session
	WriteString(sock, "done");
	easy_closesocket(sock);

	PostMessage(pDlg->m_hWnd, USER_MSG_STEPIT, 0, 0);
    }

    // enable the dialog buttons
    PostMessage(pDlg->m_hWnd, USER_MSG_ENABLE, 0, 0);

    if (pDlg->m_bshow_config)
    {
	PostMessage(pDlg->m_hWnd, USER_MSG_GETHOST, 0, 0);
    }

    CloseHandle(pDlg->m_hApplyBtnThread);
    pDlg->m_hApplyBtnThread = NULL;
}

void CMPICHConfigDlg::OnApplyBtn() 
{
    UpdateData();
    if (m_blogfile && m_logfile_yes && m_logfile.GetLength() < 1)
    {
	MessageBox("You must specify a log file if you are setting the logfile redirection option", "Error");
	return;
    }
    m_hApplyBtnThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)ApplyBtnThread, this, 0, NULL);
}

void CMPICHConfigDlg::OnApplySingleBtn() 
{
    int index;
    char pszStr[8192];
    char host[100];
    SOCKET sock;
    POSITION pos;

    UpdateData();

    if (m_blogfile && m_logfile_yes && m_logfile.GetLength() < 1)
    {
	MessageBox("You must specify a log file if you are setting the logfile redirection option", "Error");
	return;
    }

    pos = m_host_list.GetFirstSelectedItemPosition();
    if (pos == NULL)
	return;

    index = m_host_list.GetNextSelectedItem(pos);

    if (m_bNeedPassword)
    {
	if (m_buse_default_passphrase)
	    strcpy(m_pszPhrase, MPD_DEFAULT_PASSPHRASE);
	else
	    strcpy(m_pszPhrase, m_mpd_phrase);
    }

    HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );

    if (m_host_list.GetItemText(index, 0, host, 100) == 0)
    {
	SetCursor(hOldCursor);
	return;
    }

    if (ConnectToMPD(host, m_nPort, m_pszPhrase, &sock) != 0)
    {
	SetCursor(hOldCursor);
	sprintf(pszStr, "Failed to connect to the mpd on host %s", host);
	MessageBox(pszStr, "Error");
	return;
    }

    // set hosts
    if (m_bhosts)
    {
	sprintf(pszStr, "lset hosts=%s", m_hosts);
	WriteString(sock, pszStr);
    }
    // set launch timeout
    if (m_blaunch)
    {
	sprintf(pszStr, "lset timeout=%d", m_launch);
	WriteString(sock, pszStr);
    }
    // set jobhost
    if (m_buse_jobhost)
    {
	sprintf(pszStr, "lset usejobhost=%s", (m_jobhost_yes) ? "yes" : "no");
	WriteString(sock, pszStr);
	if (m_jobhost_yes)
	{
	    sprintf(pszStr, "lset jobhost=%s", m_jobhost);
	    WriteString(sock, pszStr);
	    if (m_buse_jobhost_pwd)
	    {
		sprintf(pszStr, "lset jobhostpwd=%s", m_jobhost_pwd);
		WriteString(sock, pszStr);
	    }
	    else
	    {
		WriteString(sock, "ldelete jobhostpwd");
	    }
	}
    }
    // set logfile
    if (m_blogfile)
    {
	if (m_logfile_yes)
	    sprintf(pszStr, "setdbgoutput %s", m_logfile);
	else
	    sprintf(pszStr, "canceldbgoutput");
	WriteString(sock, pszStr);
	ReadString(sock, pszStr);
    }
    // set color
    if (m_bcolor)
    {
	sprintf(pszStr, "lset nocolor=%s", (m_color_yes) ? "no" : "yes");
	WriteString(sock, pszStr);
    }
    // set dots
    if (m_bdots)
    {
	sprintf(pszStr, "lset nodots=%s", (m_dots_yes) ? "no" : "yes");
	WriteString(sock, pszStr);
    }
    // set mapping
    if (m_bmapping)
    {
	sprintf(pszStr, "lset nomapping=%s", (m_mapping_yes) ? "no" : "yes");
	WriteString(sock, pszStr);
    }
    // set popup_debug
    if (m_bpopup_debug)
    {
	sprintf(pszStr, "lset nopopup_debug=%s", (m_popup_debug_yes) ? "no" : "yes");
	WriteString(sock, pszStr);
    }
    // set dbg
    if (m_bcatch)
    {
	sprintf(pszStr, "lset dbg=%s", (m_catch_yes) ? "yes" : "no");
	WriteString(sock, pszStr);
    }
    // set exitcodes
    if (m_bcodes)
    {
	sprintf(pszStr, "lset exitcodes=%s", (m_codes_yes) ? "yes" : "no");
	WriteString(sock, pszStr);
    }
    // set localroot
    if (m_blocalroot)
    {
	sprintf(pszStr, "lset localroot=%s", (m_localroot_yes) ? "yes" : "no");
	WriteString(sock, pszStr);
    }
    // close the session
    WriteString(sock, "done");
    easy_closesocket(sock);

    SetCursor(hOldCursor);

    if (m_bshow_config)
	GetHostConfig();
}

void CMPICHConfigDlg::OnShowConfigChk() 
{
    UpdateData();
    
    m_host_toggle_btn.EnableWindow(m_bshow_config);
    m_host_hosts_chk.EnableWindow(m_bshow_config);
    m_host_launch_chk.EnableWindow(m_bshow_config);
    m_host_use_jobhost_chk.EnableWindow(m_bshow_config);
    m_host_color_chk.EnableWindow(m_bshow_config);
    m_host_dots_chk.EnableWindow(m_bshow_config);
    m_host_mapping_chk.EnableWindow(m_bshow_config);
    m_host_popup_debug_chk.EnableWindow(m_bshow_config);
    m_host_catch_chk.EnableWindow(m_bshow_config);
    m_host_codes_chk.EnableWindow(m_bshow_config);
    m_host_localroot_chk.EnableWindow(m_bshow_config);
    m_host_logfile_chk.EnableWindow(m_bshow_config);

    if (m_bshow_config)
    {
	GetHostConfig();

	OnHostHostsChk();
	OnHostLaunchChk();
	OnHostUseJobhostChk();
	OnHostColorChk();
	OnHostDotsChk();
	OnHostMappingChk();
	OnHostPopupDebugChk();
	OnHostCatchChk();
	OnHostCodesChk();
	OnHostLocalRootChk();
	OnHostRedirectMpdChk();

	//GetHostConfig();

	//m_config_host_static.EnableWindow();
	//m_config_host_msg_static.EnableWindow();
	m_config_host_static.ShowWindow(SW_SHOW);
	m_config_host_msg_static.ShowWindow(SW_SHOW);
    }
    else
    {
	m_host_hosts_edit.EnableWindow(FALSE);
	m_host_launch_edit.EnableWindow(FALSE);
	m_host_jobhost_yes_btn.EnableWindow(FALSE);
	m_host_jobhost_no_btn.EnableWindow(FALSE);
	m_host_use_jobhost_pwd_chk.EnableWindow(FALSE);
	m_host_jobhost_edit.EnableWindow(FALSE);
	m_host_jobhost_pwd_edit.EnableWindow(FALSE);
	m_host_color_yes_btn.EnableWindow(FALSE);
	m_host_color_no_btn.EnableWindow(FALSE);
	m_host_dots_yes_btn.EnableWindow(FALSE);
	m_host_dots_no_btn.EnableWindow(FALSE);
	m_host_mapping_yes_btn.EnableWindow(FALSE);
	m_host_mapping_no_btn.EnableWindow(FALSE);
	m_host_popup_debug_yes_btn.EnableWindow(FALSE);
	m_host_popup_debug_no_btn.EnableWindow(FALSE);
	m_host_catch_yes_btn.EnableWindow(FALSE);
	m_host_catch_no_btn.EnableWindow(FALSE);
	m_host_codes_yes_btn.EnableWindow(FALSE);
	m_host_localroot_yes_btn.EnableWindow(FALSE);
	m_host_codes_no_btn.EnableWindow(FALSE);
	m_host_localroot_no_btn.EnableWindow(FALSE);
	m_host_logfile_yes_btn.EnableWindow(FALSE);
	m_host_logfile_no_btn.EnableWindow(FALSE);
	m_host_logfile_edit.EnableWindow(FALSE);

	//m_config_host_static.EnableWindow(FALSE);
	//m_config_host_msg_static.EnableWindow(FALSE);
	m_config_host_static.ShowWindow(SW_HIDE);
	m_config_host_msg_static.ShowWindow(SW_HIDE);
    }

    UpdateModifyButtonState();
}

void CMPICHConfigDlg::OnHostToggleBtn() 
{
    UpdateData();

    m_bHostToggle = !m_bHostToggle;
    
    m_bhost_color = m_bHostToggle;
    m_bhost_dots = m_bHostToggle;
    m_bhost_hosts = m_bHostToggle;
    m_bhost_launch = m_bHostToggle;
    m_bhost_mapping = m_bHostToggle;
    m_bhost_popup_debug = m_bHostToggle;
    m_bhost_use_jobhost = m_bHostToggle;
    m_bhost_catch = m_bHostToggle;
    m_bhost_codes = m_bHostToggle;
    m_bhost_localroot = m_bHostToggle;
    m_bhost_logfile = m_bHostToggle;

    UpdateData(FALSE);

    OnHostColorChk();
    OnHostDotsChk();
    OnHostHostsChk();
    OnHostLaunchChk();
    OnHostMappingChk();
    OnHostPopupDebugChk();
    OnHostUseJobhostChk();
    OnHostCatchChk();
    OnHostCodesChk();
    OnHostLocalRootChk();
    OnHostRedirectMpdChk();
}

void CMPICHConfigDlg::OnHostHostsChk() 
{
    UpdateData();
    //m_host_hosts_edit.EnableWindow(m_bhost_hosts);
    m_host_hosts_edit.EnableWindow();
    UpdateModifyButtonState();
}

void CMPICHConfigDlg::OnHostLaunchChk() 
{
    UpdateData();
    //m_host_launch_edit.EnableWindow(m_bhost_launch);
    m_host_launch_edit.EnableWindow();
    UpdateModifyButtonState();
}

void CMPICHConfigDlg::OnHostUseJobhostChk() 
{
    UpdateData();
    /*
    m_host_jobhost_yes_btn.EnableWindow(m_bhost_use_jobhost);
    m_host_jobhost_no_btn.EnableWindow(m_bhost_use_jobhost);

    m_host_jobhost_edit.EnableWindow(m_bhost_use_jobhost && m_host_jobhost_yes);
    m_host_use_jobhost_pwd_chk.EnableWindow(m_bhost_use_jobhost && m_host_jobhost_yes);
    m_host_jobhost_pwd_edit.EnableWindow(m_bhost_use_jobhost && m_host_jobhost_yes && m_bhost_use_jobhost_pwd);
    */
    m_host_jobhost_yes_btn.EnableWindow();
    m_host_jobhost_no_btn.EnableWindow();

    m_host_jobhost_edit.EnableWindow(m_host_jobhost_yes);
    m_host_use_jobhost_pwd_chk.EnableWindow(m_host_jobhost_yes);
    m_host_jobhost_pwd_edit.EnableWindow(m_host_jobhost_yes && m_bhost_use_jobhost_pwd);
    UpdateModifyButtonState();
}

void CMPICHConfigDlg::OnHostJobhostYes() 
{
    UpdateData();
    m_host_jobhost_yes = TRUE;
    m_host_jobhost_no = FALSE;
    m_host_jobhost_edit.EnableWindow();
    m_host_use_jobhost_pwd_chk.EnableWindow();
    m_host_jobhost_pwd_edit.EnableWindow(m_bhost_use_jobhost_pwd);
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostJobhostNo() 
{
    UpdateData();
    m_host_jobhost_yes = FALSE;
    m_host_jobhost_no = TRUE;
    m_host_jobhost_edit.EnableWindow(FALSE);
    m_host_use_jobhost_pwd_chk.EnableWindow(FALSE);
    m_host_jobhost_pwd_edit.EnableWindow(FALSE);
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostUseJobhostPwdChk() 
{
    UpdateData();
    //m_host_jobhost_pwd_edit.EnableWindow(m_bhost_use_jobhost_pwd);
    m_host_jobhost_pwd_edit.EnableWindow();
}

void CMPICHConfigDlg::OnHostColorChk() 
{
    UpdateData();
    //m_host_color_yes_btn.EnableWindow(m_bhost_color);
    //m_host_color_no_btn.EnableWindow(m_bhost_color);
    m_host_color_yes_btn.EnableWindow();
    m_host_color_no_btn.EnableWindow();
    UpdateModifyButtonState();
}

void CMPICHConfigDlg::OnHostColorYes() 
{
    UpdateData();
    m_host_color_yes = TRUE;
    m_host_color_no = FALSE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostColorNo() 
{
    UpdateData();
    m_host_color_yes = FALSE;
    m_host_color_no = TRUE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostDotsChk() 
{
    UpdateData();
    //m_host_dots_yes_btn.EnableWindow(m_bhost_dots);
    //m_host_dots_no_btn.EnableWindow(m_bhost_dots);
    m_host_dots_yes_btn.EnableWindow();
    m_host_dots_no_btn.EnableWindow();
    UpdateModifyButtonState();
}

void CMPICHConfigDlg::OnHostDotsYes() 
{
    UpdateData();
    m_host_dots_yes = TRUE;
    m_host_dots_no = FALSE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostDotsNo() 
{
    UpdateData();
    m_host_dots_yes = FALSE;
    m_host_dots_no = TRUE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostMappingChk() 
{
    UpdateData();
    //m_host_mapping_yes_btn.EnableWindow(m_bhost_mapping);
    //m_host_mapping_no_btn.EnableWindow(m_bhost_mapping);
    m_host_mapping_yes_btn.EnableWindow();
    m_host_mapping_no_btn.EnableWindow();
    UpdateModifyButtonState();
}

void CMPICHConfigDlg::OnHostMappingYes() 
{
    UpdateData();
    m_host_mapping_yes = TRUE;
    m_host_mapping_no = FALSE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostMappingNo() 
{
    UpdateData();
    m_host_mapping_yes = FALSE;
    m_host_mapping_no = TRUE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostPopupDebugChk() 
{
    UpdateData();
    //m_host_popup_debug_yes_btn.EnableWindow(m_bhost_popup_debug);
    //m_host_popup_debug_no_btn.EnableWindow(m_bhost_popup_debug);
    m_host_popup_debug_yes_btn.EnableWindow();
    m_host_popup_debug_no_btn.EnableWindow();
    UpdateModifyButtonState();
}

void CMPICHConfigDlg::OnHostPopupDebugYes() 
{
    UpdateData();
    m_host_popup_debug_yes = TRUE;
    m_host_popup_debug_no = FALSE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostPopupDebugNo() 
{
    UpdateData();
    m_host_popup_debug_yes = FALSE;
    m_host_popup_debug_no = TRUE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnModifyBtn() 
{
    char pszStr[8192];
    SOCKET sock;

    UpdateData();

    if (m_config_host == "")
	return;

    if (m_bNeedPassword)
    {
	if (m_buse_default_passphrase)
	    strcpy(m_pszPhrase, MPD_DEFAULT_PASSPHRASE);
	else
	    strcpy(m_pszPhrase, m_mpd_phrase);
    }

    HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );

    if (ConnectToMPD(m_config_host, m_nPort, m_pszPhrase, &sock) != 0)
    {
	SetCursor(hOldCursor);
	sprintf(pszStr, "Failed to connect to the mpd on host %s", m_config_host);
	MessageBox(pszStr, "Error");
	return;
    }

    // set hosts
    if (m_bhost_hosts)
    {
	sprintf(pszStr, "lset hosts=%s", m_host_hosts);
	WriteString(sock, pszStr);
    }
    // set launch timeout
    if (m_bhost_launch)
    {
	sprintf(pszStr, "lset timeout=%d", m_host_launch);
	WriteString(sock, pszStr);
    }
    // set jobhost
    if (m_bhost_use_jobhost)
    {
	sprintf(pszStr, "lset usejobhost=%s", (m_host_jobhost_yes) ? "yes" : "no");
	WriteString(sock, pszStr);
	if (m_host_jobhost_yes)
	{
	    sprintf(pszStr, "lset jobhost=%s", m_host_jobhost);
	    WriteString(sock, pszStr);
	    if (m_bhost_use_jobhost_pwd)
	    {
		sprintf(pszStr, "lset jobhostpwd=%s", m_host_jobhost_pwd);
		WriteString(sock, pszStr);
	    }
	    else
	    {
		WriteString(sock, "ldelete jobhostpwd");
	    }
	}
    }
    // set logfile
    if (m_bhost_logfile)
    {
	if (m_host_logfile_yes)
	    sprintf(pszStr, "setdbgoutput %s", m_host_logfile);
	else
	    sprintf(pszStr, "canceldbgoutput");
	WriteString(sock, pszStr);
	ReadString(sock, pszStr);
    }
    // set color
    if (m_bhost_color)
    {
	sprintf(pszStr, "lset nocolor=%s", (m_host_color_yes) ? "no" : "yes");
	WriteString(sock, pszStr);
    }
    // set dots
    if (m_bhost_dots)
    {
	sprintf(pszStr, "lset nodots=%s", (m_host_dots_yes) ? "no" : "yes");
	WriteString(sock, pszStr);
    }
    // set mapping
    if (m_bhost_mapping)
    {
	sprintf(pszStr, "lset nomapping=%s", (m_host_mapping_yes) ? "no" : "yes");
	WriteString(sock, pszStr);
    }
    // set popup_debug
    if (m_bhost_popup_debug)
    {
	sprintf(pszStr, "lset nopopup_debug=%s", (m_host_popup_debug_yes) ? "no" : "yes");
	WriteString(sock, pszStr);
    }
    // set dbg
    if (m_bhost_catch)
    {
	sprintf(pszStr, "lset dbg=%s", (m_host_catch_yes) ? "yes" : "no");
	WriteString(sock, pszStr);
    }
    // set exitcodes
    if (m_bhost_codes)
    {
	sprintf(pszStr, "lset exitcodes=%s", (m_host_codes_yes) ? "yes" : "no");
	WriteString(sock, pszStr);
    }
    // set localroot
    if (m_bhost_localroot)
    {
	sprintf(pszStr, "lset localroot=%s", (m_host_localroot_yes) ? "yes" : "no");
	WriteString(sock, pszStr);
    }
    // close the session
    WriteString(sock, "done");
    easy_closesocket(sock);

    SetCursor(hOldCursor);

    if (m_bshow_config)
	GetHostConfig();
}

BOOL CMPICHConfigDlg::OnInitDialog() 
{
    CDialog::OnInitDialog();
    
    easy_socket_init();
    ParseRegistry();

    // set the host configuration stuff
    OnShowConfigChk();
    
    // set the configuration selection stuff
    OnHostsChk();
    OnLaunchChk();
    OnUseJobhostChk();
    OnColorChk();
    OnDotsChk();
    OnMappingChk();
    OnPopupDebugChk();
    OnCatchChk();
    OnCodesChk();
    OnLocalRootChk();
    OnRedirectMpdChk();
    
    char host[100] = "";
    gethostname(host, 100);
    m_add_hostname = host;
    UpdateData(FALSE);

    m_mpd_default_radio.SetCheck(1);
    m_mpd_phrase_edit.EnableWindow(FALSE);

    return TRUE;  // return TRUE unless you set the focus to a control
		  // EXCEPTION: OCX Property Pages should return FALSE
}

void CMPICHConfigDlg::UpdateModifyButtonState()
{
    UpdateData();

    if (m_bhost_hosts || m_bhost_launch || m_bhost_use_jobhost || m_bhost_color || m_bhost_dots || m_bhost_mapping || m_bhost_popup_debug || m_bhost_catch || m_bhost_codes || m_bhost_logfile || m_bhost_localroot)
    {
	m_modify_btn.EnableWindow(m_bshow_config);
	m_modify_static.EnableWindow(m_bshow_config);
    }
    else
    {
	m_modify_btn.EnableWindow(FALSE);
	m_modify_static.EnableWindow(FALSE);
    }
}

void CMPICHConfigDlg::UpdateApplyButtonStates()
{
    UpdateData();

    if (m_bhosts || m_blaunch || m_buse_jobhost || m_bcolor || m_bdots || m_bmapping || m_bpopup_debug || m_bcatch || m_bcodes || m_blogfile || m_blocalroot)
    {
	m_apply_btn.EnableWindow(TRUE);
	m_apply_single_btn.EnableWindow(TRUE);
    }
    else
    {
	m_apply_btn.EnableWindow(FALSE);
	m_apply_single_btn.EnableWindow(FALSE);
    }
}

void CMPICHConfigDlg::OnKeydownHostList(NMHDR* pNMHDR, LRESULT* pResult) 
{
    LV_KEYDOWN* pLVKeyDow = (LV_KEYDOWN*)pNMHDR;
    
    if (pLVKeyDow->wVKey == VK_DELETE)
    {
	if (pLVKeyDow->hdr.hwndFrom == m_host_list.m_hWnd)
	{
	    int index, n;
	    POSITION pos;
	    pos = m_host_list.GetFirstSelectedItemPosition();
	    index = m_host_list.GetNextSelectedItem(pos);
	    if (index != LB_ERR)
	    {
		m_host_list.DeleteItem(index);
		n = m_host_list.GetItemCount();
		if (n == index)
		    index--;
		if (n > 0)
		    m_host_list.SetItemState(index, LVIS_SELECTED, LVIS_SELECTED);
		GetHostsString();
	    }
	}
    }

    *pResult = 0;
}

void CMPICHConfigDlg::OnClose() 
{
    if (m_hApplyBtnThread != NULL)
    {
	TerminateThread(m_hApplyBtnThread, 0);
	CloseHandle(m_hApplyBtnThread);
	m_hApplyBtnThread = NULL;
    }
    easy_socket_finalize();
    CDialog::OnClose();
}

void CMPICHConfigDlg::OnItemchangingHostList(NMHDR* pNMHDR, LRESULT* pResult) 
{
    NM_LISTVIEW* pNMListView = (NM_LISTVIEW*)pNMHDR;

    if (pNMListView->uNewState & LVIS_SELECTED)
    {
	m_host_list.GetItemText(pNMListView->iItem, pNMListView->iSubItem, m_config_host.GetBuffer(100), 100);
	m_config_host.ReleaseBuffer();
	m_config_host_msg = "";
	UpdateData(FALSE);
	if (m_bshow_config)
	{
	    GetHostConfig();
	}
    }

    *pResult = 0;
}

void CMPICHConfigDlg::GetHostConfig()
{
    SOCKET sock;
    char pszStr[MAX_CMD_LENGTH] = "mpd not installed";

    UpdateData();

    if (m_config_host == "")
    {
	int index;
	POSITION pos;
	pos = m_host_list.GetFirstSelectedItemPosition();
	index = m_host_list.GetNextSelectedItem(pos);
	if (index == LB_ERR)
	    return;

	m_config_host = m_host_list.GetItemText(index, 0);
    }

    if (m_bNeedPassword)
    {
	if (m_buse_default_passphrase)
	    strcpy(m_pszPhrase, MPD_DEFAULT_PASSPHRASE);
	else
	    strcpy(m_pszPhrase, m_mpd_phrase);
    }
    
    HCURSOR hOldCursor = SetCursor( LoadCursor(NULL, IDC_WAIT) );
    
    if (ConnectToMPDquickReport(m_config_host, m_nPort, m_pszPhrase, &sock, pszStr) != 0)
    {
	//m_config_host_msg = "connect to " + m_config_host + " failed";
	if (strstr(pszStr, "10061"))
	    m_config_host_msg = "mpd not installed";
	else if (strstr(pszStr, "11001"))
	    m_config_host_msg = "unknown host";
	else
	    m_config_host_msg = pszStr;
	UpdateData(FALSE);
	SetCursor(hOldCursor);
	return;
    }

    // get mpd version
    WriteString(sock, "version");
    if (!ReadStringTimeout(sock, pszStr, MPD_SHORT_TIMEOUT))
    {
	WriteString(sock, "done");
	m_config_host_msg = "unable to reach mpd";
	UpdateData(FALSE);
	SetCursor(hOldCursor);
	return;
    }
    // where do I put the mpd version?

    // get hosts
    WriteString(sock, "lget hosts");
    if (!ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT))
    {
	WriteString(sock, "done");
	m_config_host_msg = "unable to reach mpd";
	UpdateData(FALSE);
	SetCursor(hOldCursor);
	return;
    }
    m_host_hosts = pszStr;

    // get launch timeout
    WriteString(sock, "lget timeout");
    if (!ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT))
    {
	WriteString(sock, "done");
	m_config_host_msg = "unable to reach mpd";
	UpdateData(FALSE);
	SetCursor(hOldCursor);
	return;
    }
    m_host_launch = atoi(pszStr);
    if (m_host_launch < 1)
	m_host_launch = 10;

    // get nocolor
    WriteString(sock, "lget nocolor");
    if (!ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT))
    {
	WriteString(sock, "done");
	m_config_host_msg = "unable to reach mpd";
	UpdateData(FALSE);
	SetCursor(hOldCursor);
	return;
    }
    if (strlen(pszStr) == 0)
    {
	// if nocolor is not set, get color
	WriteString(sock, "lget color");
	if (!ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT))
	{
	    WriteString(sock, "done");
	    m_config_host_msg = "unable to reach mpd";
	    UpdateData(FALSE);
	    SetCursor(hOldCursor);
	    return;
	}
	if (strlen(pszStr) == 0)
	    m_host_color_no = FALSE;
	else
	    m_host_color_no = (stricmp(pszStr, "yes") != 0);
    }
    else
    {
	m_host_color_no = (stricmp(pszStr, "yes") == 0);
    }
    m_host_color_yes = !m_host_color_no;

    // get nodots
    WriteString(sock, "lget nodots");
    if (!ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT))
    {
	WriteString(sock, "done");
	m_config_host_msg = "unable to reach mpd";
	UpdateData(FALSE);
	SetCursor(hOldCursor);
	return;
    }
    m_host_dots_no = (stricmp(pszStr, "yes") == 0);
    m_host_dots_yes = !m_host_dots_no;

    // get nomapping
    WriteString(sock, "lget nomapping");
    if (!ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT))
    {
	WriteString(sock, "done");
	m_config_host_msg = "unable to reach mpd";
	UpdateData(FALSE);
	SetCursor(hOldCursor);
	return;
    }
    m_host_mapping_no = (stricmp(pszStr, "yes") == 0);
    m_host_mapping_yes = !m_host_mapping_no;

    // get nopopup_debug
    WriteString(sock, "lget nopopup_debug");
    if (!ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT))
    {
	WriteString(sock, "done");
	m_config_host_msg = "unable to reach mpd";
	UpdateData(FALSE);
	SetCursor(hOldCursor);
	return;
    }
    m_host_popup_debug_no = (stricmp(pszStr, "yes") == 0);
    m_host_popup_debug_yes = !m_host_popup_debug_no;

    // get dbg
    WriteString(sock, "lget dbg");
    if (!ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT))
    {
	WriteString(sock, "done");
	m_config_host_msg = "unable to reach mpd";
	UpdateData(FALSE);
	SetCursor(hOldCursor);
	return;
    }
    m_host_catch_yes = (stricmp(pszStr, "yes") == 0);
    m_host_catch_no = !m_host_catch_yes;

    // get exitcodes
    WriteString(sock, "lget exitcodes");
    if (!ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT))
    {
	WriteString(sock, "done");
	m_config_host_msg = "unable to reach mpd";
	UpdateData(FALSE);
	SetCursor(hOldCursor);
	return;
    }
    m_host_codes_yes = (stricmp(pszStr, "yes") == 0);
    m_host_codes_no = !m_host_codes_yes;

    // get localroot
    WriteString(sock, "lget localroot");
    if (!ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT))
    {
	WriteString(sock, "done");
	m_config_host_msg = "unable to reach mpd";
	UpdateData(FALSE);
	SetCursor(hOldCursor);
	return;
    }
    m_host_localroot_yes = (stricmp(pszStr, "yes") == 0);
    m_host_localroot_no = !m_host_localroot_yes;

    // get usejobhost
    WriteString(sock, "lget usejobhost");
    if (!ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT))
    {
	WriteString(sock, "done");
	m_config_host_msg = "unable to reach mpd";
	UpdateData(FALSE);
	SetCursor(hOldCursor);
	return;
    }
    if (strlen(pszStr))
    {
	m_host_jobhost_yes = (stricmp(pszStr, "yes") == 0);
	m_host_jobhost_no = !m_host_jobhost_yes;
	if (m_host_jobhost_yes)
	{
	    // get jobhost
	    WriteString(sock, "lget jobhost");
	    if (!ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT))
	    {
		WriteString(sock, "done");
		m_config_host_msg = "unable to reach mpd";
		UpdateData(FALSE);
		SetCursor(hOldCursor);
		return;
	    }
	    m_host_jobhost = pszStr;
	    m_host_jobhost_edit.EnableWindow();

	    // get jobhostpwd
	    WriteString(sock, "lget jobhostpwd");
	    if (!ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT))
	    {
		WriteString(sock, "done");
		m_config_host_msg = "unable to reach mpd";
		UpdateData(FALSE);
		SetCursor(hOldCursor);
		return;
	    }
	    if (strlen(pszStr))
	    {
		m_bhost_use_jobhost_pwd = TRUE;
		m_host_jobhost_pwd = pszStr;
		if (m_bhost_use_jobhost)
		    m_host_jobhost_pwd_edit.EnableWindow(); // only enable if option checked
	    }
	    else
	    {
		m_bhost_use_jobhost_pwd = FALSE;
		m_host_jobhost_pwd_edit.EnableWindow(FALSE);
	    }
	}
    }
    else
    {
	m_host_jobhost_yes = false;
	m_host_jobhost_no = true;
	m_host_jobhost = "";
	m_host_jobhost_edit.EnableWindow(FALSE);
	m_bhost_use_jobhost_pwd = FALSE;
	m_host_jobhost_pwd_edit.EnableWindow(FALSE);
    }

    // get logfile
    WriteString(sock, "lget RedirectToLogfile");
    if (!ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT))
    {
	WriteString(sock, "done");
	m_config_host_msg = "unable to reach mpd";
	UpdateData(FALSE);
	SetCursor(hOldCursor);
	return;
    }
    if (strlen(pszStr))
    {
	m_host_logfile_yes = (stricmp(pszStr, "yes") == 0);
	m_host_logfile_no = !m_host_logfile_yes;
    }
    else
    {
	m_host_logfile_yes = false;
	m_host_logfile_no = true;
	m_host_logfile = "";
	m_host_logfile_edit.EnableWindow(FALSE);
    }

    // get logfile
    WriteString(sock, "lget LogFile");
    if (!ReadStringTimeout(sock, pszStr, MPD_DEFAULT_TIMEOUT))
    {
	WriteString(sock, "done");
	m_config_host_msg = "unable to reach mpd";
	UpdateData(FALSE);
	SetCursor(hOldCursor);
	return;
    }
    m_host_logfile = pszStr;
    if (m_host_logfile_yes)
	m_host_logfile_edit.EnableWindow();

    // get mpich version
    WriteString(sock, "mpich version");
    if (ReadStringTimeout(sock, pszStr, MPD_SHORT_TIMEOUT))
    {
	m_config_host_msg = "mpich ";
	m_config_host_msg += pszStr;
    }
    else
    {
	m_config_host_msg = "mpich - unknown version";
    }

    // close the session
    WriteString(sock, "done");
    easy_closesocket(sock);

    SetCursor(hOldCursor);
    UpdateData(FALSE);
}

void CMPICHConfigDlg::GetHostsString()
{
    int num_hosts;
    QVS_Container qvs;
    char host[100];
    int i;

    UpdateData();

    num_hosts = m_host_list.GetItemCount();

    if (num_hosts < 1)
    {
	m_hosts = "";
	UpdateData(FALSE);
	return;
    }

    for (i=0; i<num_hosts; i++)
    {
	m_host_list.GetItemText(i, 0, host, 100);
	qvs.encode_string(host);
    }

    qvs.output_encoded_string(m_hosts.GetBuffer(8192), 8192);
    m_hosts.ReleaseBuffer();

    UpdateData(FALSE);
}

LRESULT CMPICHConfigDlg::WindowProc(UINT message, WPARAM wParam, LPARAM lParam) 
{
    static int num_steps=0, cur_step=0;
    switch (message)
    {
    case USER_MSG_DISABLE:
	m_apply_btn.EnableWindow(FALSE);
	m_apply_single_btn.EnableWindow(FALSE);
	//m_modify_btn.EnableWindow(FALSE);
	break;
    case USER_MSG_ENABLE:
	m_apply_btn.EnableWindow();
	m_apply_single_btn.EnableWindow();
	//m_modify_btn.EnableWindow();
	break;
    case USER_MSG_NUM_STEPS:
	num_steps = wParam;
	cur_step = 0;
	m_progress.SetRange(0, num_steps);
	m_progress.SetStep(1);
	m_progress.SetPos(0);
	break;
    case USER_MSG_STEPIT:
	cur_step++;
	m_nofm.Format("%d of %d", cur_step, num_steps);
	UpdateData(FALSE);
	m_progress.StepIt();
	break;
    case USER_MSG_GETHOST:
	GetHostConfig();
	break;
    }
    return CDialog::WindowProc(message, wParam, lParam);
}

void CMPICHConfigDlg::OnHostCatchChk() 
{
    UpdateData();
    //m_host_catch_yes_btn.EnableWindow(m_bhost_catch);
    //m_host_catch_no_btn.EnableWindow(m_bhost_catch);
    m_host_catch_yes_btn.EnableWindow();
    m_host_catch_no_btn.EnableWindow();
    UpdateModifyButtonState();
}

void CMPICHConfigDlg::OnHostCatchYes() 
{
    UpdateData();
    m_host_catch_yes = TRUE;
    m_host_catch_no = FALSE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostCatchNo() 
{
    UpdateData();
    m_host_catch_yes = FALSE;
    m_host_catch_no = TRUE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnCatchChk() 
{
    UpdateData();
    m_catch_yes_btn.EnableWindow(m_bcatch);
    m_catch_no_btn.EnableWindow(m_bcatch);
    UpdateApplyButtonStates();
}

void CMPICHConfigDlg::OnCatchYes() 
{
    UpdateData();
    m_catch_yes = TRUE;
    m_catch_no = FALSE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnCatchNo() 
{
    UpdateData();
    m_catch_yes = FALSE;
    m_catch_no = TRUE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostCodesYes() 
{
    UpdateData();
    m_host_codes_yes = TRUE;
    m_host_codes_no = FALSE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostCodesNo() 
{
    UpdateData();
    m_host_codes_yes = FALSE;
    m_host_codes_no = TRUE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostCodesChk() 
{
    UpdateData();
    //m_host_codes_yes_btn.EnableWindow(m_bhost_codes);
    //m_host_codes_no_btn.EnableWindow(m_bhost_codes);
    m_host_codes_yes_btn.EnableWindow();
    m_host_codes_no_btn.EnableWindow();
    UpdateModifyButtonState();
}

void CMPICHConfigDlg::OnCodesChk() 
{
    UpdateData();
    m_codes_yes_btn.EnableWindow(m_bcodes);
    m_codes_no_btn.EnableWindow(m_bcodes);
    UpdateApplyButtonStates();
}

void CMPICHConfigDlg::OnCodesYes() 
{
    UpdateData();
    m_codes_yes = TRUE;
    m_codes_no = FALSE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnCodesNo() 
{
    UpdateData();
    m_codes_yes = FALSE;
    m_codes_no = TRUE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostLocalRootYes() 
{
    UpdateData();
    m_host_localroot_yes = TRUE;
    m_host_localroot_no = FALSE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostLocalRootNo() 
{
    UpdateData();
    m_host_localroot_yes = FALSE;
    m_host_localroot_no = TRUE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostLocalRootChk() 
{
    UpdateData();
    //m_host_localroot_yes_btn.EnableWindow(m_bhost_localroot);
    //m_host_localroot_no_btn.EnableWindow(m_bhost_localroot);
    m_host_localroot_yes_btn.EnableWindow();
    m_host_localroot_no_btn.EnableWindow();
    UpdateModifyButtonState();
}

void CMPICHConfigDlg::OnLocalRootChk() 
{
    UpdateData();
    m_localroot_yes_btn.EnableWindow(m_blocalroot);
    m_localroot_no_btn.EnableWindow(m_blocalroot);
    UpdateApplyButtonStates();
}

void CMPICHConfigDlg::OnLocalRootYes() 
{
    UpdateData();
    m_localroot_yes = TRUE;
    m_localroot_no = FALSE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnLocalRootNo() 
{
    UpdateData();
    m_localroot_yes = FALSE;
    m_localroot_no = TRUE;
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnRedirectMpdChk() 
{
    UpdateData();
    m_logfile_yes_btn.EnableWindow(m_blogfile);
    m_logfile_no_btn.EnableWindow(m_blogfile);

    m_logfile_static.EnableWindow(m_blogfile && m_logfile_yes);
    m_logfile_edit.EnableWindow(m_blogfile && m_logfile_yes);
    UpdateApplyButtonStates();
}

void CMPICHConfigDlg::OnRedirectMpdNo() 
{
    UpdateData();
    m_logfile_yes = FALSE;
    m_logfile_no = TRUE;
    m_logfile_static.EnableWindow(FALSE);
    m_logfile_edit.EnableWindow(FALSE);
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnRedirectMpdYes() 
{
    UpdateData();
    m_logfile_yes = TRUE;
    m_logfile_no = FALSE;
    m_logfile_static.EnableWindow();
    m_logfile_edit.EnableWindow();
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostRedirectMpdChk() 
{
    UpdateData();
    m_host_logfile_yes_btn.EnableWindow();
    m_host_logfile_no_btn.EnableWindow();
    m_host_logfile_edit.EnableWindow(m_host_logfile_yes);
    UpdateModifyButtonState();
}

void CMPICHConfigDlg::OnHostRedirectMpdNo() 
{
    UpdateData();
    m_host_logfile_yes = FALSE;
    m_host_logfile_no = TRUE;
    m_host_logfile_edit.EnableWindow(FALSE);
    UpdateData(FALSE);
}

void CMPICHConfigDlg::OnHostRedirectMpdYes() 
{
    UpdateData();
    m_host_logfile_yes = TRUE;
    m_host_logfile_no = FALSE;
    m_host_logfile_edit.EnableWindow();
    UpdateData(FALSE);
}
