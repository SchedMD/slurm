cd ..\..\src\fortran\src
mkdir g77
cd g77
del *.c
cd ..
copy *.c g77
cd g77
rename *.c *.g77.c
cd ..\..\..\..
cd romio\mpi-io\fortran
mkdir g77
cd g77
del *.c
cd ..
copy *.c g77
cd g77
rename *.c *.g77.c
cd ..\..\..\..

cd src\fortran\src
mkdir intel
cd intel
del *.c
cd ..
copy *.c intel
cd intel
rename *.c *.intel.c
cd ..\..\..\..
cd romio\mpi-io\fortran
mkdir intel
cd intel
del *.c
cd ..
copy *.c intel
cd intel
rename *.c *.intel.c
cd ..\..\..\..
cd mpid\ch_nt
copy nt_ipvishm\include\*.h ..\..\include
