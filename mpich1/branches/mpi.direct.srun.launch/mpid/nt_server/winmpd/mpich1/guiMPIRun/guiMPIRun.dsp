# Microsoft Developer Studio Project File - Name="guiMPIRun" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Application" 0x0101

CFG=guiMPIRun - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "guiMPIRun.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "guiMPIRun.mak" CFG="guiMPIRun - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "guiMPIRun - Win32 Release" (based on "Win32 (x86) Application")
!MESSAGE "guiMPIRun - Win32 Debug" (based on "Win32 (x86) Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
F90=df.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "guiMPIRun - Win32 Release"

# PROP BASE Use_MFC 5
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 5
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE F90 /compile_only /include:"Release/" /nologo /warn:nofileopt /winapp
# ADD F90 /browser /compile_only /include:"Release/" /nologo /warn:nofileopt /winapp
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /Yu"stdafx.h" /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /I "..\..\mpd" /I "..\common" /I "..\..\bsocket" /I "..\..\mpdutil" /I "..\mpirun" /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "HAVE_WINBCONF_H" /D _WIN32_WINNT=0x0400 /D FD_SETSIZE=256 /D "WSOCK2_BEFORE_WINDOWS" /D "NO_BSOCKETS" /FR /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 /nologo /subsystem:windows /machine:I386
# ADD LINK32 crypt.lib mpdutil.lib ws2_32.lib mpr.lib /nologo /subsystem:windows /machine:I386 /libpath:"..\..\..\..\lib" /libpath:"..\lib" /libpath:"..\..\mpdutil\release" /libpath:"..\..\crypt\release" /libpath:"..\..\bsocket\release"

!ELSEIF  "$(CFG)" == "guiMPIRun - Win32 Debug"

# PROP BASE Use_MFC 5
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 5
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE F90 /check:bounds /compile_only /debug:full /include:"Debug/" /nologo /traceback /warn:argument_checking /warn:nofileopt /winapp
# ADD F90 /browser /check:bounds /compile_only /debug:full /include:"Debug/" /nologo /traceback /warn:argument_checking /warn:nofileopt /winapp
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /Yu"stdafx.h" /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "..\..\mpd" /I "..\common" /I "..\..\bsocket" /I "..\..\mpdutil" /I "..\mpirun" /D "_DEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "HAVE_WINBCONF_H" /D _WIN32_WINNT=0x0400 /D FD_SETSIZE=256 /D "WSOCK2_BEFORE_WINDOWS" /D "NO_BSOCKETS" /FR /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept
# ADD LINK32 crypt.lib mpdutil.lib ws2_32.lib mpr.lib /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept /libpath:"..\..\..\..\lib" /libpath:"..\lib" /libpath:"..\..\mpdutil\debug" /libpath:"..\..\crypt\debug" /libpath:"..\..\bsocket\debug"

!ENDIF 

# Begin Target

# Name "guiMPIRun - Win32 Release"
# Name "guiMPIRun - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat;f90;for;f;fpp"
# Begin Source File

SOURCE=.\AdvancedOptionsDlg.cpp
# End Source File
# Begin Source File

SOURCE=.\DirDialog.cpp
# End Source File
# Begin Source File

SOURCE=.\global.cpp
# End Source File
# Begin Source File

SOURCE=.\guiMPIRun.cpp
# End Source File
# Begin Source File

SOURCE=.\guiMPIRun.rc
# End Source File
# Begin Source File

SOURCE=.\guiMPIRunDoc.cpp
# End Source File
# Begin Source File

SOURCE=.\guiMPIRunView.cpp
# End Source File
# Begin Source File

SOURCE=.\HelpDlg.cpp
# End Source File
# Begin Source File

SOURCE=.\HtmlCtrl.cpp
# End Source File
# Begin Source File

SOURCE=.\job.cpp
# End Source File
# Begin Source File

SOURCE=.\LaunchProcess.cpp
# End Source File
# Begin Source File

SOURCE=.\MainFrm.cpp
# End Source File
# Begin Source File

SOURCE=.\MPICH_pwd.cpp
# End Source File
# Begin Source File

SOURCE=.\RedirectIO.cpp
# End Source File
# Begin Source File

SOURCE=.\resizer.cpp
# End Source File
# Begin Source File

SOURCE=.\run.cpp
# End Source File
# Begin Source File

SOURCE=.\StdAfx.cpp
# ADD CPP /Yc"stdafx.h"
# End Source File
# Begin Source File

SOURCE=.\UserPwdDialog.cpp
# End Source File
# Begin Source File

SOURCE=.\WaitThread.cpp
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl;fi;fd"
# Begin Source File

SOURCE=.\AdvancedOptionsDlg.h
# End Source File
# Begin Source File

SOURCE=.\DirDialog.h
# End Source File
# Begin Source File

SOURCE=.\global.h
# End Source File
# Begin Source File

SOURCE=.\guiMPIRun.h
# End Source File
# Begin Source File

SOURCE=.\guiMPIRunDoc.h
# End Source File
# Begin Source File

SOURCE=.\guiMPIRunView.h
# End Source File
# Begin Source File

SOURCE=.\HelpDlg.h
# End Source File
# Begin Source File

SOURCE=.\HtmlCtrl.h
# End Source File
# Begin Source File

SOURCE=.\LaunchProcess.h
# End Source File
# Begin Source File

SOURCE=.\MainFrm.h
# End Source File
# Begin Source File

SOURCE=.\MPICH_pwd.h
# End Source File
# Begin Source File

SOURCE=.\RedirectIO.h
# End Source File
# Begin Source File

SOURCE=.\Resizer.h
# End Source File
# Begin Source File

SOURCE=.\Resource.h
# End Source File
# Begin Source File

SOURCE=.\StdAfx.h
# End Source File
# Begin Source File

SOURCE=.\UserPwdDialog.h
# End Source File
# Begin Source File

SOURCE=.\WaitThread.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# Begin Source File

SOURCE=.\res\guiMPIRun.ico
# End Source File
# Begin Source File

SOURCE=.\res\guiMPIRun.rc2
# End Source File
# Begin Source File

SOURCE=.\res\guiMPIRunDoc.ico
# End Source File
# End Group
# Begin Source File

SOURCE=.\res\help_htm.htm
# End Source File
# End Target
# End Project
