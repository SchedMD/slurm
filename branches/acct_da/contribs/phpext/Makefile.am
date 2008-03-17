AUTOMAKE_OPTIONS = foreign
php_dir = slurm_php
phpize = /usr/bin/phpize

if HAVE_AIX
	add_flags = "CC=\"$(CC)\" CCFLAGS=\"-g -static $(CFLAGS)\""
else
	add_flags = "CC=\"$(CC)\" LD=\"$(CC) $(CFLAGS)\" CCFLAGS=\"-g -static $(CFLAGS)\""
endif

all-local:
	@cd $(php_dir) && \
	if [ ! -f Makefile ]; then \
		if [ ! -f configure ]; then \
			$(phpize); \
		fi && \
		./configure ; \
		if [ ! -f Makefile ]; then \
			exit 0;\
		fi \
	fi && \
	$(MAKE) $(add_flags); \
	cd ..;	

install-exec-local:
	@cd $(php_dir) && \
	if [ ! -f Makefile ]; then \
		exit 0;\
	fi && \
	$(MAKE) INSTALL_ROOT=$(DESTDIR) $(add_flags) install && \
	cd ..;

clean-generic:
	@cd $(php_dir); \
	if [ ! -f Makefile ]; then \
		exit 0;\
	fi && \
	$(MAKE) clean; \
	cd ..;

distclean-generic:
	@cd $(php_dir); \
	if [ ! -f Makefile ]; then \
		exit 0;\
	fi && \
	$(MAKE) clean; \
	$(phpize) --clean; \
	cd ..;
