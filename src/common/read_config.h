#ifndef _READ_CONFIG_H
#define _READ_CONFIG_H

#include <src/common/slurm_protocol_defs.h>

/* 
 * init_slurm_conf - initialize or re-initialize the slurm configuration values.   
 * ctl_conf_ptr(I) - pointer to data structure to be initialized
 */
extern void init_slurm_conf (slurm_ctl_conf_t *ctl_conf_ptr);

/*
 * parse_config_spec - parse the overall configuration specifications, update values
 * in_line(I/O) - input line, parsed info overwritten with white-space
 * ctl_conf_ptr(I) - pointer to data structure to be updated
 * returns - 0 if no error, otherwise an error code
 */
extern int parse_config_spec (char *in_line, slurm_ctl_conf_t *ctl_conf_ptr);

/*
 * read_slurm_conf_ctl - load the slurm configuration from the configured file. 
 * ctl_conf_ptr(I) - pointer to data structure to be filled
 * returns - 0 if no error, otherwise an error code
 */
extern int read_slurm_conf_ctl (slurm_ctl_conf_t *ctl_conf_ptr);

/* 
 * report_leftover - report any un-parsed (non-whitespace) characters on the
 * configuration input line (we over-write parsed characters with whitespace).
 * input: in_line - what is left of the configuration input line.
 *        line_num - line number of the configuration file.
 */
extern void report_leftover (char *in_line, int line_num);

#endif /* !_READ_CONFIG_H */
