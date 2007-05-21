#include "stdafx.h"
#include "guiMPIJob.h"
#include "guiMPIJobDlg.h"
#include "mpd.h"
#include "mpdutil.h"
#include "MPDConnectDlg.h"
#include "Translate_Error.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

void GetKeyAndValue(char *str, char *key, char *value);

void DeleteJob(SOCKET sock, char *pszJob)
{
    char str[256];
    int error;

    sprintf(str, "dbdestroy %s", pszJob);
    if (WriteString(sock, str) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	//printf("Error: DeleteJob, writing '%s' failed, %d\n", str, error);
	Translate_Error(error, str);
	//printf("%s\n", str);
	//easy_closesocket(sock);
	return;
    }
    if (!ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
    {
	//printf("Error, DeleteJob, unable to delete the job '%s'.\n", pszJob);
	//WriteString(sock, "done");
	//easy_closesocket(sock);
	return;
    }
}

void DeleteKey(SOCKET sock, char *key)
{
    char str[256];
    int error;

    sprintf(str, "dbdelete jobs:%s", key);
    if (WriteString(sock, str) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	//printf("Error: DeleteKey, writing '%s' failed, %d\n", str, error);
	Translate_Error(error, str);
	//printf("%s\n", str);
	//easy_closesocket(sock);
	return;
    }
    if (!ReadStringTimeout(sock, str, MPD_DEFAULT_TIMEOUT))
    {
	//printf("Error, DeleteKey, unable to delete the job entry '%s'.\n", key);
	//WriteString(sock, "done");
	//easy_closesocket(sock);
	return;
    }
}

struct KeyNode
{
    char key[256];
    KeyNode *next;
};

static KeyNode *s_pKeyList = NULL;
void SaveKeyToDelete(char *key)
{
    KeyNode *n = new KeyNode;

    strcpy(n->key, key);
    n->next = s_pKeyList;
    s_pKeyList = n;
}

void CGuiMPIJobDlg::OnRemoveBtn() 
{
    char str[CONSOLE_STR_LENGTH+1];
    int error;
    char key[100];
    char value[CONSOLE_STR_LENGTH];
    char *pszJob;
    int index;

    CString jobstr;

    UpdateData();

    if ((m_job.GetLength() < 1) || (m_sock == INVALID_SOCKET))
	return;

    index = m_job_list.GetCurSel();

    jobstr = m_job;
    jobstr.Delete(0, jobstr.Find('@')+1);
    jobstr = jobstr.Left(jobstr.Find(' '));

    strcpy(str, "dbfirst jobs");
    if (WriteString(m_sock, str) == SOCKET_ERROR)
    {
	error = WSAGetLastError();
	sprintf(value, "Error: JobsToFile, writing '%s' failed, %d\n", str, error);
	Translate_Error(error, str);
	strcat(value, str);
	MessageBox(value, "Connection Error");
	Disconnect();
	return;
    }
    if (ReadStringTimeout(m_sock, str, MPD_DEFAULT_TIMEOUT))
    {
	if (strcmp(str, "DBS_FAIL") == 0)
	{
	    //printf("no jobs on %s\n", host);
	    //WriteString(m_sock, "done");
	    //easy_closesocket(m_sock);
	    return;
	}
	if (strcmp(str, "DBS_END") == 0)
	{
	    //printf("no jobs on %s\n", host);
	    //WriteString(m_sock, "done");
	    //easy_closesocket(m_sock);
	    return;
	}
	GetKeyAndValue(str, key, value);
	pszJob = strstr(value, "@")+1;

	if (strcmp(pszJob, jobstr) == 0)
	{
	    //printf("%s : %s\n", key, value);
	    DeleteJob(m_sock, pszJob);
	    SaveKeyToDelete(key);
	}
    }
    else
    {
	//printf("Error, JobsToFile, unable to read the jobs on %s.\n", host);
	//WriteString(m_sock, "done");
	//easy_closesocket(m_sock);
	return;
    }

    while (true)
    {
	strcpy(str, "dbnext jobs");
	if (WriteString(m_sock, str) == SOCKET_ERROR)
	{
	    error = WSAGetLastError();
	    sprintf(value, "writing '%s' failed, %d\n", str, error);
	    Translate_Error(error, str);
	    strcat(value, str);
	    MessageBox(value, "Connection Error");
	    Disconnect();
	    return;
	}
	if (ReadStringTimeout(m_sock, str, MPD_DEFAULT_TIMEOUT))
	{
	    if (strcmp(str, "DBS_FAIL") == 0)
	    {
		//printf("unexpected error reading the next job\n");
		//WriteString(m_sock, "done");
		//easy_closesocket(m_sock);
		return;
	    }
	    if (strcmp(str, "DBS_END") == 0)
	    {
		break;
	    }
	    GetKeyAndValue(str, key, value);
	    pszJob = strstr(value, "@")+1;
	    if (strcmp(pszJob, jobstr) == 0)
	    {
		//printf("%s : %s\n", key, value);
		DeleteJob(m_sock, pszJob);
		SaveKeyToDelete(key);
	    }
	}
	else
	{
	    //printf("Unable to read the jobs on %s.\n", host);
	    //WriteString(m_sock, "done");
	    //easy_closesocket(m_sock);
	    return;
	}
    }

    if (s_pKeyList == NULL)
    {
	sprintf(value, "The specified job, %s, does not exist on %s\n", pszJob, m_host);
	MessageBox(value, "Note");
    }
    KeyNode *n;
    while (s_pKeyList)
    {
	n = s_pKeyList;
	s_pKeyList = s_pKeyList->next;
	DeleteKey(m_sock, n->key);
	delete n;
    }

    if (m_job_list.GetCount() > 1)
    {
	OnRefreshBtn();
	if (index == m_job_list.GetCount())
	    index--;
	m_job_list.SetCurSel(index);
	GetJobDetails();
    }
    else
    {
	OnRefreshBtn();
    }
}
