SLURM_INSTALL=/usr/local
SLURM_MODULES=${SLURM_INSTALL}/lib/slurm/auth_*.so ${SLURM_INSTALL}/lib/slurm/switch_*.so

PYTHON_INCLUDE_PATH=`python -c 'from distutils.sysconfig import get_python_inc; print get_python_inc()'`

PERL_INCLUDE_PATH=`perl -e 'use Config; print $$Config{archlib},"/CORE";'`
PERL_CCFLAGS=`perl -e 'use Config; print $$Config{ccflags}'`

###########################################################################
# Python rules

python: _slurm.so

_slurm.so: slurm_wrap_python.o
	${LD} -shared -o $@ $< ${SLURM_MODULES} -lslurm -ldl \
	-rpath ${SLURM_INSTALL}/lib -rpath ${SLURM_INSTALL}/lib/slurm

slurm_wrap_python.o: slurm_wrap_python.c
	@echo ${PYTHON_INCLUDE_PATH}
	$(CC) -fpic -c $< -I${PYTHON_INCLUDE_PATH}

slurm_wrap_python.c: slurm.swg
	swig -python -o $@ -I${SLURM_INSTALL}/include $<

###########################################################################
# Perl rules
perl: slurm.so

slurm.so: slurm_wrap_perl.o
	${LD} -shared -o $@ $< ${SLURM_MODULES} -lslurm -ldl \
	-rpath ${SLURM_INSTALL}/lib -rpath ${SLURM_INSTALL}/lib/slurm

slurm_wrap_perl.o: slurm_wrap_perl.c
	@echo ${PERL_INCLUDE_PATH}
	gcc -c $< -I${PERL_INCLUDE_PATH} ${PERL_CCFLAGS} -Dbool=char

slurm_wrap_perl.c: slurm.swg
	swig -perl -o $@ -I${SLURM_INSTALL}/include $<

###########################################################################
# Cleanup

clean:
	-rm -f _slurm.so slurm.so slurm_wrap_*.[co] slurm.py slurm.pyc slurm.pm
