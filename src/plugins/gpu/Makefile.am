# Makefile for gpu plugins

SUBDIRS = common generic nrt nvidia

if BUILD_RSMI
SUBDIRS += rsmi
endif

if BUILD_AMDSMI
SUBDIRS += asmi
endif

if BUILD_NVML
SUBDIRS += nvml
endif

if BUILD_ONEAPI
SUBDIRS += oneapi
endif
