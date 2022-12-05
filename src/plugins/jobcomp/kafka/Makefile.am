# Makefile for jobcomp/kafka plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) \
	-I$(top_srcdir)/src/common $(RDKAFKA_CPPFLAGS)

if WITH_RDKAFKA

pkglib_LTLIBRARIES = jobcomp_kafka.la

jobcomp_kafka_la_SOURCES = jobcomp_kafka.c \
			jobcomp_kafka_conf.c jobcomp_kafka_conf.h \
			jobcomp_kafka_message.c jobcomp_kafka_message.h
jobcomp_kafka_la_LDFLAGS = $(PLUGIN_FLAGS) $(RDKAFKA_LDFLAGS)
jobcomp_kafka_la_LIBADD = $(RDKAFKA_LIBS) ../common/libjobcomp_common.la

force:
$(jobcomp_kafka_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

endif
