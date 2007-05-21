///////////////////////////////////////////////////////////////////////////
// DirDialog.cpp: implementation of the CDirDialog class.
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "DirDialog.h"
#include "shlobj.h"

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

// Callback function called by SHBrowseForFolder's browse control
// after initialization and when selection changes
static int __stdcall BrowseCtrlCallback(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
  CDirDialog* pDirDialogObj = (CDirDialog*)lpData;

  if (uMsg == BFFM_INITIALIZED && !pDirDialogObj->m_strSelDir.IsEmpty())
  {
    ::SendMessage(hwnd, BFFM_SETSELECTION, TRUE, (LPARAM)(LPCTSTR)(pDirDialogObj->m_strSelDir));
  }
  else // uMsg == BFFM_SELCHANGED
  {
  }

  return 0;
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

CDirDialog::CDirDialog()
{
}

CDirDialog::~CDirDialog()
{
}

int CDirDialog::DoBrowse()
{
  LPMALLOC pMalloc;
  if (SHGetMalloc (&pMalloc)!= NOERROR)
  {
      return 0;
  }

  BROWSEINFO bInfo;
  LPITEMIDLIST pidl;
  ZeroMemory ( (PVOID) &bInfo,sizeof (BROWSEINFO));

  if (!m_strInitDir.IsEmpty ())
  {
    OLECHAR       olePath[MAX_PATH];
    ULONG         chEaten;
    ULONG         dwAttributes;
    HRESULT       hr;
    LPSHELLFOLDER pDesktopFolder;
    // 
    // Get a pointer to the Desktop's IShellFolder interface. 
    //
    if (SUCCEEDED(SHGetDesktopFolder(&pDesktopFolder)))
    {
      //
      // IShellFolder::ParseDisplayName requires the file name be in Unicode.
      //
      MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, m_strInitDir.GetBuffer(MAX_PATH), -1,
                          olePath, MAX_PATH);

      m_strInitDir.ReleaseBuffer (-1);
      //
      // Convert the path to an ITEMIDLIST.
      //
      hr = pDesktopFolder->ParseDisplayName(NULL,
                                            NULL,
                                            olePath,
                                            &chEaten,
                                            &pidl,
                                            &dwAttributes);
      if (FAILED(hr))
      {
        pMalloc ->Free (pidl);
        pMalloc ->Release ();
        return 0;
      }
      bInfo.pidlRoot = pidl;

    }
  }
  bInfo.hwndOwner = NULL;
  bInfo.pszDisplayName = m_strPath.GetBuffer (MAX_PATH);
  bInfo.lpszTitle = (m_strTitle.IsEmpty()) ? "Open":m_strTitle;
  bInfo.ulFlags = BIF_RETURNFSANCESTORS|BIF_RETURNONLYFSDIRS;
  bInfo.lpfn = BrowseCtrlCallback;  // address of callback function
  bInfo.lParam = (LPARAM)this;      // pass address of object to callback function

  if ((pidl = ::SHBrowseForFolder(&bInfo)) == NULL)
  {
    return 0;
  }
  m_strPath.ReleaseBuffer();
  m_iImageIndex = bInfo.iImage;

  if (::SHGetPathFromIDList(pidl,m_strPath.GetBuffer(MAX_PATH)) == FALSE)
  {
    pMalloc ->Free(pidl);
    pMalloc ->Release();
    return 0;
  }

  m_strPath.ReleaseBuffer();

  pMalloc ->Free(pidl);
  pMalloc ->Release();

  return 1;
}