#ifndef _GET_MACH_STAT_H
#define _GET_MACH_STAT_H
#define _SLURMD_H
#if HAVE_CONFIG_H
#  include <config.h>
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif  /* HAVE_INTTYPES_H */
#else   /* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif  /*  HAVE_CONFIG_H */
int get_procs(uint32_t *procs);
int get_mach_name(char *node_name);
int get_memory(uint32_t *real_memory);
int get_tmp_disk(uint32_t *tmp_disk);
#ifdef USE_OS_NAME
int get_os_name(char *os_name);
#endif
#ifdef USE_CPU_SPEED
int get_speed(float *speed);
#endif

#endif
