SLURM_INSTALL=/usr/local
SLURM_MODULES=${SLURM_INSTALL}/lib/slurm/auth_*.so ${SLURM_INSTALL}/lib/slurm/switch_*.so
PY_INCLUDE_PATH=/usr/include/python2.4

_slurm.so: slurm_wrap.o
	${LD} -shared -o _slurm.so slurm_wrap.o ${SLURM_MODULES} -lslurm -ldl \
	-rpath ${SLURM_INSTALL}/lib -rpath ${SLURM_INSTALL}/lib/slurm

slurm_wrap.o: slurm_wrap.c
	$(CC) -fpic -c slurm_wrap.c -I${PY_INCLUDE_PATH}

slurm_wrap.c: slurm.swg
	swig -I${SLURM_INSTALL}/include -python slurm.swg

clean:
	-rm -f _slurm.so slurm_wrap.[co] slurm.py slurm.pyc
