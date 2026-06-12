ETC_FILES = init.d.slurmdbd init.d.slurm \
	    sackd.service slurmctld.service slurmdbd.service slurmd.service \
	    slurmrestd.service slurm.pc

CLEANFILES = $(ETC_FILES)

edit = sed \
        -e 's|@bindir[@]|$(bindir)|g' \
        -e 's|@includedir[@]|$(includedir)|g' \
        -e 's|@libdir[@]|$(libdir)|g' \
        -e 's|@sbindir[@]|$(sbindir)|g' \
        -e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	-e 's|@sharedstatedir[@]|$(sharedstatedir)|g' \
	-e 's|@SLURM_VERSION_STRING[@]|@SLURM_VERSION_STRING@|g' \
	-e 's|@SLURMRESTD_PORT[@]|@SLURMRESTD_PORT@|g' \
	-e 's|@SYSTEMD_TASKSMAX_OPTION[@]|$(SYSTEMD_TASKSMAX_OPTION)|g'

noinst_DATA = $(ETC_FILES)

init.d.slurmdbd: Makefile $(srcdir)/init.d.slurmdbd.in
	$(edit) $(srcdir)/init.d.slurmdbd.in > init.d.slurmdbd

init.d.slurm: Makefile $(srcdir)/init.d.slurm.in
	$(edit) $(srcdir)/init.d.slurm.in > init.d.slurm

sackd.service: Makefile $(srcdir)/sackd.service.in
	$(edit) $(srcdir)/sackd.service.in > sackd.service

slurmctld.service: Makefile $(srcdir)/slurmctld.service.in
	$(edit) $(srcdir)/slurmctld.service.in > slurmctld.service

slurmdbd.service: Makefile $(srcdir)/slurmdbd.service.in
	$(edit) $(srcdir)/slurmdbd.service.in > slurmdbd.service

slurmd.service: Makefile $(srcdir)/slurmd.service.in
	$(edit) $(srcdir)/slurmd.service.in > slurmd.service

slurmrestd.service: Makefile $(srcdir)/slurmrestd.service.in
	$(edit) $(srcdir)/slurmrestd.service.in > slurmrestd.service

slurm.pc: Makefile $(srcdir)/slurm.pc.in
	$(edit) $(srcdir)/slurm.pc.in > slurm.pc

if WITH_PKG_CONFIG
pkgconfig_DATA = slurm.pc
endif

if WITH_SYSTEMD_UNITS
systemdsystemunit_DATA = sackd.service slurmctld.service slurmdbd.service \
	slurmd.service
if WITH_SLURMRESTD
systemdsystemunit_DATA += slurmrestd.service
endif
endif
