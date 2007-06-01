# Microsoft Developer Studio Project File - Name="MPDUpdate" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Application" 0x0101

CFG=MPDUpdate - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "MPDUpdate.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "MPDUpdate.mak" CFG="MPDUpdate - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "MPDUpdate - Win32 Release" (based on "Win32 (x86) Application")
!MESSAGE "MPDUpdate - Win32 Debug" (based on "Win32 (x86) Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
F90=df.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "MPDUpdate - Win32 Release"

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
# ADD CPP /nologo /MT /W3 /GX /O2 /I "..\bsocket" /I "..\mpd" /I "..\crypt" /I "..\resizer" /I "..\mpdutil" /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "HAVE_WINBCONF_H" /D "NO_BSOCKETS" /FR /Yu"stdafx.h" /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 /nologo /subsystem:windows /machine:I386
# ADD LINK32 mpdutil.lib crypt.lib ws2_32.lib netapi32.lib resizer.lib /nologo /subsystem:windows /machine:I386 /libpath:"..\bsocket\release" /libpath:"..\crypt\release" /libpath:"..\resizer\release" /libpath:"..\mpdutil\release"

!ELSEIF  "$(CFG)" == "MPDUpdate - Win32 Debug"

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
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "..\bsocket" /I "..\mpd" /I "..\crypt" /I "..\resizer" /I "..\mpdutil" /D "_DEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "HAVE_WINBCONF_H" /D "NO_BSOCKETS" /FR /Yu"stdafx.h" /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept
# ADD LINK32 mpdutil.lib crypt.lib ws2_32.lib netapi32.lib resizer.lib /nologo /subsystem:windows /debug /machine:I386 /pdbtype:sept /libpath:"..\bsocket\debug" /libpath:"..\crypt\debug" /libpath:"..\resizer\debug" /libpath:"..\mpdutil\debug"

!ENDIF 

# Begin Target

# Name "MPDUpdate - Win32 Release"
# Name "MPDUpdate - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat;f90;for;f;fpp"
# Begin Source File

SOURCE=.\ConnectToHost.cpp
# End Source File
# Begin Source File

SOURCE=.\DomainDlg.cpp
# End Source File
# Begin Source File

SOURCE=.\FindHostsDlg.cpp
# End Source File
# Begin Source File

SOURCE=.\launchprocess.cpp
# End Source File
# Begin Source File

SOURCE=.\MPDConnectionOptionsDlg.cpp
# End Source File
# Begin Source File

SOURCE=.\MPDUpdate.cpp
# End Source File
# Begin Source File

SOURCE=.\MPDUpdate.rc
# End Source File
# Begin Source File

SOURCE=.\MPDUpdateDlg.cpp
# End Source File
# Begin Source File

SOURCE=.\PwdDialog.cpp
# End Source File
# Begin Source File

SOURCE=.\qvs.cpp
# End Source File
# Begin Source File

SOURCE=.\StdAfx.cpp
# ADD CPP /Yc"stdafx.h"
# End Source File
# Begin Source File

SOURCE=.\WildStrDlg.cpp
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl;fi;fd"
# Begin Source File

SOURCE=.\ConnectToHost.h
# End Source File
# Begin Source File

SOURCE=.\DomainDlg.h
# End Source File
# Begin Source File

SOURCE=.\FindHostsDlg.h
# End Source File
# Begin Source File

SOURCE=.\launchprocess.h
# End Source File
# Begin Source File

SOURCE=.\MPDConnectionOptionsDlg.h
# End Source File
# Begin Source File

SOURCE=.\MPDUpdate.h
# End Source File
# Begin Source File

SOURCE=.\MPDUpdateDlg.h
# End Source File
# Begin Source File

SOURCE=.\PwdDialog.h
# End Source File
# Begin Source File

SOURCE=.\qvs.h
# End Source File
# Begin Source File

SOURCE=.\Resource.h
# End Source File
# Begin Source File

SOURCE=.\StdAfx.h
# End Source File
# Begin Source File

SOURCE=.\WildStrDlg.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# Begin Source File

SOURCE=.\res\green_bi.bmp
# End Source File
# Begin Source File

SOURCE=.\res\ico00001.ico
# End Source File
# Begin Source File

SOURCE=.\res\icon1.ico
# End Source File
# Begin Source File

SOURCE=.\res\MPDUpdate.ico
# End Source File
# Begin Source File

SOURCE=.\res\MPDUpdate.rc2
# End Source File
# Begin Source File

SOURCE=.\res\red_bitm.bmp
# End Source File
# Begin Source File

SOURCE=.\res\yellow_b.bmp
# End Source File
# End Group
# End Target
# End Project
