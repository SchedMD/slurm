# Microsoft Developer Studio Project File - Name="mpichbnr" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=mpichbnr - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "mpichbnr.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "mpichbnr.mak" CFG="mpichbnr - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "mpichbnr - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "mpichbnr - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
F90=df.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "mpichbnr - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "..\..\..\..\lib"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE F90 /compile_only /include:"Release/" /dll /nologo /warn:nofileopt
# ADD F90 /compile_only /include:"Release/" /dll /nologo /warn:nofileopt
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "MPICHBNR_EXPORTS" /Yu"stdafx.h" /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /I "..\..\..\..\include" /D "NDEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "MPICHBNR_EXPORTS" /D "HAVE_MPICHBNR_API" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# Begin Special Build Tool
SOURCE="$(InputPath)"
PostBuild_Desc=Saving copy to system32 directory
PostBuild_Cmds=copy ..\..\..\..\lib\mpichbnr.dll %SystemRoot%\system32\mpichbnr.dll
# End Special Build Tool

!ELSEIF  "$(CFG)" == "mpichbnr - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "..\..\..\..\lib"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE F90 /check:bounds /compile_only /debug:full /include:"Debug/" /dll /nologo /traceback /warn:argument_checking /warn:nofileopt
# ADD F90 /browser /check:bounds /compile_only /debug:full /include:"Debug/" /dll /nologo /traceback /warn:argument_checking /warn:nofileopt
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "MPICHBNR_EXPORTS" /Yu"stdafx.h" /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "..\..\..\..\include" /D "_DEBUG" /D "WIN32" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "MPICHBNR_EXPORTS" /D "HAVE_MPICHBNR_API" /FR /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /out:"..\..\..\..\lib/mpichbnrd.dll" /pdbtype:sept
# Begin Special Build Tool
SOURCE="$(InputPath)"
PostBuild_Desc=Saving copy to system32 directory
PostBuild_Cmds=copy ..\..\..\..\lib\mpichbnrd.dll %SystemRoot%\system32\mpichbnrd.dll
# End Special Build Tool

!ENDIF 

# Begin Target

# Name "mpichbnr - Win32 Release"
# Name "mpichbnr - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat;f90;for;f;fpp"
# Begin Group "info"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\bnr_info_create.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_info_delete.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_info_dup.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_info_free.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_info_get.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_info_getnks.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_info_getnth.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_info_getvln.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_info_set.cpp
# End Source File
# End Group
# Begin Source File

SOURCE=.\bnr_close_group.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_deposit.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_fence.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_finalize.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_free_group.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_get.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_get_click.cpp
# PROP Exclude_From_Build 1
# End Source File
# Begin Source File

SOURCE=.\bnr_get_group.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_get_parent.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_get_rank.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_get_size.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_global.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_group_node.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_init.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_kill.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_lookup.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_merge.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_open_group.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_put.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_spawn.cpp
# End Source File
# Begin Source File

SOURCE=.\bnr_withdraw.cpp
# End Source File
# Begin Source File

SOURCE=.\mpichbnr.cpp
# End Source File
# Begin Source File

SOURCE=.\parsecliques.cpp
# End Source File
# Begin Source File

SOURCE=.\StdAfx.cpp
# ADD CPP /Yc"stdafx.h"
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl;fi;fd"
# Begin Source File

SOURCE=..\..\..\..\INCLUDE\bnr.h
# End Source File
# Begin Source File

SOURCE=.\bnr_internal.h
# End Source File
# Begin Source File

SOURCE=.\bnrinternal.h
# End Source File
# Begin Source File

SOURCE=.\mpichbnr.h
# End Source File
# Begin Source File

SOURCE=.\parsecliques.h
# End Source File
# Begin Source File

SOURCE=.\StdAfx.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
