AUTOMAKE_OPTIONS = foreign
php_dir=slurm_php
phpize=/usr/bin/phpize

if HAVE_AIX
config_line=CC="$(CC)" CCFLAGS="-g -static $(CFLAGS) $(CPPFLAGS)" ./configure
else
config_line=CC="$(CC)" LD="$(CC) $(CFLAGS) $(LDFLAGS)" CCFLAGS="-g -static $(CFLAGS)" CFLAGS="$(CFLAGS)" CPPFLAGS="$(CPPFLAGS)" ./configure
endif

all-local:
	@cd $(php_dir) && \
	if [ ! -f Makefile ]; then \
		if [ ! -f configure ]; then \
			$(phpize); \
		fi && \
		$(config_line); \
		if [ ! -f Makefile ]; then \
			exit 0;\
		fi \
	fi && \
	$(MAKE); \
	cd ..;

install-exec-local:
	@cd $(php_dir) && \
	if [ ! -f Makefile ]; then \
		exit 0;\
	fi && \
	$(MAKE) INSTALL_ROOT=$(DESTDIR) install && \
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
