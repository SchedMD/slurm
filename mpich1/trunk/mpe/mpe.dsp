# Microsoft Developer Studio Project File - Name="mpe" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Static Library" 0x0104

CFG=mpe - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "mpe.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "mpe.mak" CFG="mpe - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "mpe - Win32 Release" (based on "Win32 (x86) Static Library")
!MESSAGE "mpe - Win32 Debug" (based on "Win32 (x86) Static Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
F90=df.exe
RSC=rc.exe

!IF  "$(CFG)" == "mpe - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Target_Dir ""
# ADD BASE F90 /compile_only /include:"Release/" /nologo /warn:nofileopt
# ADD F90 /browser /compile_only /include:"Release/" /nologo /warn:nofileopt
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_MBCS" /D "_LIB" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /I ".\\" /I "..\include" /I "include" /I "include\windows" /I "slog_api\include" /I "slog_api" /I "..\romio\include" /D "NDEBUG" /D "WIN32" /D "_MBCS" /D "_LIB" /D "HAVE_SLOG_WINCONFIG_H" /FR /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo /out:"..\lib\mpe.lib"

!ELSEIF  "$(CFG)" == "mpe - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Target_Dir ""
# ADD BASE F90 /check:bounds /compile_only /debug:full /include:"Debug/" /nologo /traceback /warn:argument_checking /warn:nofileopt
# ADD F90 /browser /check:bounds /compile_only /debug:full /include:"Debug/" /nologo /traceback /warn:argument_checking /warn:nofileopt
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_MBCS" /D "_LIB" /YX /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I ".\\" /I "..\include" /I "include" /I "include\windows" /I "slog_api\include" /I "slog_api" /I "..\romio\include" /D "_DEBUG" /D "WIN32" /D "_MBCS" /D "_LIB" /D "HAVE_SLOG_WINCONFIG_H" /FR /YX /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LIB32=link.exe -lib
# ADD BASE LIB32 /nologo
# ADD LIB32 /nologo /out:"..\lib\mped.lib"

!ENDIF 

# Begin Target

# Name "mpe - Win32 Release"
# Name "mpe - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat;f90;for;f;fpp"
# Begin Group "slog"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\slog_api\src\bswp_fileio.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\fbuf.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\slog_assoc.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\slog_bbuf.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\slog_bebits.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\slog_fileio.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\slog_header.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\slog_impl.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\slog_irec_common.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\slog_irec_read.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\slog_irec_write.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\slog_preview.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\slog_profile.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\slog_pstat.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\slog_recdefs.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\slog_strlist.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\slog_tasklabel.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\slog_ttable.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\slog_vtrarg.c
# End Source File
# Begin Source File

SOURCE=.\slog_api\src\str_util.c
# End Source File
# End Group
# Begin Source File

SOURCE=.\src\c2s_util.c
# End Source File
# Begin Source File

SOURCE=.\src\clog.c
# End Source File
# Begin Source File

SOURCE=.\src\clog2alog.c
# End Source File
# Begin Source File

SOURCE=.\src\clog_merge.c
# End Source File
# Begin Source File

SOURCE=.\src\clog_sysio.c
# End Source File
# Begin Source File

SOURCE=.\src\clog_time.c
# End Source File
# Begin Source File

SOURCE=.\src\clog_util.c
# End Source File
# Begin Source File

SOURCE=.\src\decomp.c
# End Source File
# Begin Source File

SOURCE=.\src\examine.c
# End Source File
# Begin Source File

SOURCE=.\src\getgrank.c
# End Source File
# Begin Source File

SOURCE=.\src\log_wrap.c
# End Source File
# Begin Source File

SOURCE=.\src\mpe_io.c
# End Source File
# Begin Source File

SOURCE=.\src\mpe_log.c
# End Source File
# Begin Source File

SOURCE=.\src\mpe_seq.c
# End Source File
# Begin Source File

SOURCE=.\src\privtags.c
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl;fi;fd"
# Begin Source File

SOURCE=.\slog_api\include\bswp_fileio.h
# End Source File
# Begin Source File

SOURCE=.\include\clog.h
# End Source File
# Begin Source File

SOURCE=.\include\clog2slog.h
# End Source File
# Begin Source File

SOURCE=.\include\clog_merge.h
# End Source File
# Begin Source File

SOURCE=.\include\clog_time.h
# End Source File
# Begin Source File

SOURCE=.\include\clogimpl.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\include\fbuf.h
# End Source File
# Begin Source File

SOURCE=.\include\mpe.h
# End Source File
# Begin Source File

SOURCE=.\include\mpe_log.h
# End Source File
# Begin Source File

SOURCE=.\include\windows\mpeconf.h
# End Source File
# Begin Source File

SOURCE=.\include\mpeexten.h
# End Source File
# Begin Source File

SOURCE=.\include\requests.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\include\slog.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\include\slog_assoc.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\include\slog_bbuf.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\include\slog_bebits.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\include\slog_fileio.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\include\slog_header.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\include\slog_impl.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\include\slog_preview.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\include\slog_profile.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\include\slog_pstat.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\include\slog_recdefs.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\include\slog_strlist.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\include\slog_tasklabel.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\include\slog_ttable.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\include\slog_vtrarg.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\slog_winconfig.h
# End Source File
# Begin Source File

SOURCE=.\slog_api\include\str_util.h
# End Source File
# End Group
# End Target
# End Project
