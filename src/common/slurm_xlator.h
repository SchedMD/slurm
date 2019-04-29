/*****************************************************************************\
 *  slurm_xlator.h - Definitions required to translate Slurm function names
 *  to aliases containing  a prefix of "slurm_".
 *
 *  This is required because some Slurm functions have common names
 *  (e.g. "debug" and "info"). If a user application defines these functions
 *  and uses Slurm APIs, they could link to the user function rather than
 *  the Slurm function. By renaming the functions, inappropriate linking
 *  should be avoided.
 *
 *  All Slurm functions referenced from the switch, auth, and mpi plugins should
 *  have aliases established. Functions not referenced from the plugins
 *  need not be aliased.
 *
 *  To use this header file:
 *  1. In the module containing the exported function code, establish an
 *     alias for each of the functions after they are defined.
 *     #include "src/common/macros.h"
 *     strong_alias(<name>, slurm_<name>);
 *  2. For each function, change it's name then include the appropriate
 *     header file with definitions.
 *     #define <name> slurm_<name>
 *  3. In the plugin modules using the functions, include this header file
 *     and remove other slurm header files (they should all be in this header).
 *     This logic will have the plugin link only to the function names with
 *     the "slurm_" prefix.
 *
 *  NOTE: Not all operating systems support this function aliasing (e.g. AIX).
 *****************************************************************************
 *  Copyright (C) 2004 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Mark Grondona <grondona1@llnl.gov>,
 *             Morris Jette <jette1@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of Slurm, a resource management program.
 *  For details, see <https://slurm.schedmd.com/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  Slurm is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  In addition, as a special exception, the copyright holders give permission
 *  to link the code of portions of this program with the OpenSSL library under
 *  certain conditions as described in each individual source file, and
 *  distribute linked combinations including the two. You must obey the GNU
 *  General Public License in all respects for all of the code used other than
 *  OpenSSL. If you modify file(s) with this exception, you may extend this
 *  exception to your version of the file(s), but you are not obligated to do
 *  so. If you do not wish to do so, delete this exception statement from your
 *  version.  If you delete this exception statement from all source files in
 *  the program, then also delete it here.
 *
 *  Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with Slurm; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifndef __SLURM_XLATOR_H__
#define __SLURM_XLATOR_H__

#include "config.h"

#if USE_ALIAS

/* bitstring.[ch] functions*/
#define	bit_alloc		slurm_bit_alloc
#define	bit_test		slurm_bit_test
#define	bit_set			slurm_bit_set
#define	bit_clear		slurm_bit_clear
#define	bit_nclear		slurm_bit_nclear
#define	bit_nset		slurm_bit_nset
#define	bit_set_all		slurm_bit_set_all
#define	bit_clear_all		slurm_bit_clear_all
#define	bit_ffc			slurm_bit_ffc
#define	bit_ffs			slurm_bit_ffs
#define	bit_free		slurm_bit_free
#define	bit_realloc		slurm_bit_realloc
#define	bit_size		slurm_bit_size
#define	bit_and			slurm_bit_and
#define	bit_not			slurm_bit_not
#define	bit_or			slurm_bit_or
#define	bit_set_count		slurm_bit_set_count
#define	bit_set_count_range	slurm_bit_set_count_range
#define	bit_clear_count		slurm_bit_clear_count
#define	bit_nset_max_count	slurm_bit_nset_max_count
#define	bit_rotate_copy		slurm_bit_rotate_copy
#define	bit_rotate		slurm_bit_rotate
#define	bit_fmt			slurm_bit_fmt
#define bit_unfmt		slurm_bit_unfmt
#define	bitfmt2int		slurm_bitfmt2int
#define	bit_fmt_hexmask		slurm_bit_fmt_hexmask
#define bit_unfmt_hexmask	slurm_bit_unfmt_hexmask
#define	bit_fmt_binmask		slurm_bit_fmt_binmask
#define bit_unfmt_binmask	slurm_bit_unfmt_binmask
#define	bit_fls			slurm_bit_fls
#define	bit_fill_gaps		slurm_bit_fill_gaps
#define	bit_super_set		slurm_bit_super_set
#define	bit_copy		slurm_bit_copy
#define	bit_pick_cnt		slurm_bit_pick_cnt
#define bit_nffc		slurm_bit_nffc
#define bit_noc			slurm_bit_noc
#define bit_nffs		slurm_bit_nffs
#define bit_copybits		slurm_bit_copybits

/* fd.[ch] functions */
#define fd_set_blocking		slurm_fd_set_blocking
#define fd_set_nonblocking	slurm_fd_set_nonblocking

/* hostlist.[ch] functions */
#define	hostlist_create		slurm_hostlist_create
#define	hostlist_copy		slurm_hostlist_copy
#define	hostlist_count		slurm_hostlist_count
#define	hostlist_delete		slurm_hostlist_delete
#define	hostlist_delete_host	slurm_hostlist_delete_host
#define	hostlist_delete_nth	slurm_hostlist_delete_nth
#define	hostlist_deranged_string slurm_hostlist_deranged_string
#define	hostlist_deranged_string_malloc \
				slurm_hostlist_deranged_string_malloc
#define	hostlist_deranged_string_xmalloc \
				slurm_hostlist_deranged_string_xmalloc
#define	hostlist_destroy	slurm_hostlist_destroy
#define	hostlist_find		slurm_hostlist_find
#define	hostlist_iterator_create  slurm_hostlist_iterator_create
#define	hostlist_iterator_destroy slurm_hostlist_iterator_destroy
#define	hostlist_iterator_reset	slurm_hostlist_iterator_reset
#define	hostlist_next		slurm_hostlist_next
#define	hostlist_next_range	slurm_hostlist_next_range
#define	hostlist_nth		slurm_hostlist_nth
#define	hostlist_pop            slurm_hostlist_pop
#define	hostlist_pop_range      slurm_hostlist_pop_range
#define	hostlist_push		slurm_hostlist_push
#define	hostlist_push_host	slurm_hostlist_push_host
#define	hostlist_push_list	slurm_hostlist_push_list
#define	hostlist_ranged_string	slurm_hostlist_ranged_string
#define	hostlist_ranged_string_malloc \
				slurm_hostlist_ranged_string_malloc
#define	hostlist_ranged_string_xmalloc \
				slurm_hostlist_ranged_string_xmalloc
#define	hostlist_remove		slurm_hostlist_remove
#define	hostlist_shift		slurm_hostlist_shift
#define	hostlist_shift_range	slurm_hostlist_shift_range
#define	hostlist_sort		slurm_hostlist_sort
#define	hostlist_uniq		slurm_hostlist_uniq
#define	hostset_copy		slurm_hostset_copy
#define	hostset_count		slurm_hostset_count
#define	hostset_create		slurm_hostset_create
#define	hostset_delete		slurm_hostset_delete
#define	hostset_destroy		slurm_hostset_destroy
#define	hostset_insert		slurm_hostset_insert
#define	hostset_shift		slurm_hostset_shift
#define	hostset_shift_range	slurm_hostset_shift_range
#define	hostset_within		slurm_hostset_within

/* gres.[ch] functions */
#define gres_gresid_to_gresname	slurm_gres_gresid_to_gresname
#define gres_get_node_used	slurm_gres_get_node_used
#define gres_get_system_cnt	slurm_gres_get_system_cnt
#define gres_get_value_by_type	slurm_gres_get_value_by_type
#define gres_get_job_info	slurm_gres_get_job_info
#define gres_build_job_details	slurm_gres_build_job_details
#define gres_get_step_info	slurm_gres_get_step_info
#define gres_get_step_state	slurm_gres_get_step_state
#define gres_get_job_state	slurm_gres_get_job_state
#define gres_2_tres_str		slurm_gres_2_tres_str
#define gres_set_job_tres_cnt	slurm_gres_set_job_tres_cnt
#define gres_set_node_tres_cnt	slurm_gres_set_node_tres_cnt
#define gres_device_major	slurm_gres_device_major
#define destroy_gres_device	slurm_destroy_gres_device

/* list.[ch] functions */
#define	list_create		slurm_list_create
#define	list_destroy		slurm_list_destroy
#define	list_is_empty		slurm_list_is_empty
#define	list_count		slurm_list_count
#define	list_append		slurm_list_append
#define	list_prepend		slurm_list_prepend
#define	list_find_first		slurm_list_find_first
#define	list_delete_all		slurm_list_delete_all
#define	list_for_each		slurm_list_for_each
#define	list_sort		slurm_list_sort
#define	list_push		slurm_list_push
#define	list_pop		slurm_list_pop
#define	list_peek		slurm_list_peek
#define	list_enqueue		slurm_list_enqueue
#define	list_dequeue		slurm_list_dequeue
#define	list_iterator_create	slurm_list_iterator_create
#define	list_iterator_reset	slurm_list_iterator_reset
#define	list_iterator_destroy	slurm_list_iterator_destroy
#define	list_next		slurm_list_next
#define	list_insert		slurm_list_insert
#define	list_find		slurm_list_find
#define	list_remove		slurm_list_remove
#define	list_delete_item	slurm_list_delete_item
#define	list_install_fork_handlers slurm_list_install_fork_handlers

/* log.[ch] functions */
#define get_log_level		slurm_get_log_level
#define get_sched_log_level	slurm_get_sched_log_level
#define	log_init		slurm_log_init
#define	log_reinit		slurm_log_reinit
#define	log_fini		slurm_log_fini
#define	log_alter		slurm_log_alter
#define	log_alter_with_fp	slurm_log_alter_with_fp
#define	log_set_fpfx		slurm_log_set_fpfx
#define	log_fp			slurm_log_fp
#define	log_has_data		slurm_log_has_data
#define	log_flush		slurm_log_flush
#define log_var			slurm_log_var
#define	fatal_abort		slurm_fatal_abort
#define	fatal			slurm_fatal
#define	error			slurm_error
#define	info			slurm_info
#define	verbose			slurm_verbose
#define	debug			slurm_debug
#define	debug2			slurm_debug2
#define	debug3			slurm_debug3
#define	debug4			slurm_debug4
#define	debug5			slurm_debug5

/* macros.h functions
 * None exported today.
 * The header file used only for #define values. */

/* net.[ch] functions */
#define net_stream_listen	slurm_net_stream_listen
#define net_set_low_water	slurm_net_set_low_water

/* pack.[ch] functions */
#define	create_buf		slurm_create_buf
#define	free_buf		slurm_free_buf
#define grow_buf		slurm_grow_buf
#define	init_buf		slurm_init_buf
#define	xfer_buf_data		slurm_xfer_buf_data
#define	pack_time		slurm_pack_time
#define	unpack_time		slurm_unpack_time
#define	packdouble		slurm_packdouble
#define	unpackdouble		slurm_unpackdouble
#define	packlongdouble		slurm_packlongdouble
#define	unpacklongdouble	slurm_unpacklongdouble
#define	pack64			slurm_pack64
#define	unpack64		slurm_unpack64
#define	pack32			slurm_pack32
#define	unpack32		slurm_unpack32
#define	pack16			slurm_pack16
#define	unpack16		slurm_unpack16
#define	pack8			slurm_pack8
#define	unpack8			slurm_unpack8
#define	pack32_array		slurm_pack32_array
#define	unpack32_array		slurm_unpack32_array
#define	packmem			slurm_packmem
#define	unpackmem		slurm_unpackmem
#define	unpackmem_ptr		slurm_unpackmem_ptr
#define	unpackmem_xmalloc	slurm_unpackmem_xmalloc
#define	unpackmem_malloc	slurm_unpackmem_malloc
#define	unpackstr_xmalloc_escaped slurm_unpackstr_xmalloc_escaped
#define	unpackstr_xmalloc_chooser slurm_unpackstr_xmalloc_chooser
#define	packstr_array		slurm_packstr_array
#define	unpackstr_array		slurm_unpackstr_array
#define	packmem_array		slurm_packmem_array
#define	unpackmem_array		slurm_unpackmem_array

/* parse_time.[ch] functions */
#define parse_time              slurm_parse_time
#define time_str2mins           slurm_time_str2mins
#define time_str2secs           slurm_time_str2secs
#define secs2time_str           slurm_secs2time_str
#define mins2time_str           slurm_mins2time_str
#define mon_abbr                slurm_mon_abbr

/* env.[ch] functions */
#define	setenvf 		slurm_setenvpf
#define	unsetenvp		slurm_unsetenvp
#define	getenvp			slurm_getenvp
#define env_array_create	slurm_env_array_create
#define env_array_merge		slurm_env_array_merge
#define env_array_copy		slurm_env_array_copy
#define env_array_free		slurm_env_array_free
#define env_array_append	slurm_env_array_append
#define env_array_append_fmt	slurm_env_array_append_fmt
#define env_array_overwrite	slurm_env_array_overwrite
#define env_array_overwrite_fmt slurm_env_array_overwrite_fmt
#define env_array_overwrite_pack_fmt  slurm_env_array_overwrite_pack_fmt

/* read_config.[ch] functions */
#define destroy_config_key_pair	slurm_destroy_config_key_pair
#define get_extra_conf_path	slurm_get_extra_conf_path
#define sort_key_pairs		slurm_sort_key_pairs
#define run_in_daemon           slurm_run_in_daemon

/* slurm_auth.[ch] functions
 * None exported today.
 * The header file used only for #define values. */

/* strlcpy.[ch] functions */
#define	strlcpy			slurm_strlcpy

/* switch.[ch] functions
 * None exported today.
 * The header file used only for #define values. */

/* xassert.[ch] functions */
#define	__xassert_failed	slurm_xassert_failed

/* xsignal.[ch] functions */
#define	xsignal			slurm_xsignal
#define	xsignal_save_mask	slurm_xsignal_save_mask
#define	xsignal_set_mask	slurm_xsignal_set_mask
#define	xsignal_block		slurm_xsignal_block
#define	xsignal_unblock		slurm_xsignal_unblock
#define	xsignal_sigset_create	slurm_xsignal_sigset_create

/* xstring.[ch] functions */
#define	_xstrcat		slurm_xstrcat
#define	_xstrncat		slurm_xstrncat
#define	_xstrcatchar		slurm_xstrcatchar
#define	_xstrftimecat		slurm_xstrftimecat
#define	_xiso8601timecat	slurm_xiso8601timecat
#define	_xrfc5424timecat	slurm_xrfc5424timecat
#define	_xstrfmtcat		slurm_xstrfmtcat
#define	_xmemcat		slurm_xmemcat
#define	xstrdup			slurm_xstrdup
#define	xstrdup_printf		slurm_xstrdup_printf
#define	xbasename		slurm_xbasename
#define	_xstrsubstitute		slurm_xstrsubstitute
#define	xshort_hostname		slurm_xshort_hostname
#define xstring_is_whitespace   slurm_xstring_is_whitespace
#define	xstrtolower		slurm_xstrtolower
#define xstrchr			slurm_xstrchr
#define xstrrchr		slurm_xstrrchr
#define xstrcmp			slurm_xstrcmp
#define xstrncmp		slurm_xstrncmp
#define xstrcasecmp		slurm_xstrcasecmp
#define xstrncasecmp		slurm_xstrncasecmp
#define xstrcasestr		slurm_xstrcasestr

/* slurm_protocol_api.[ch] functions */
#define convert_num_unit2       slurm_convert_num_unit2
#define convert_num_unit        slurm_convert_num_unit
#define revert_num_unit         slurm_revert_num_unit
#define get_convert_unit_val    slurm_get_convert_unit_val
#define get_unit_type           slurm_get_unit_type

/* slurm_protocol_defs.[ch] functions */
#define preempt_mode_string	slurm_preempt_mode_string
#define preempt_mode_num	slurm_preempt_mode_num
#define job_reason_string	slurm_job_reason_string
#define job_state_string	slurm_job_state_string
#define job_state_string_compact slurm_job_state_string_compact
#define job_state_num		slurm_job_state_num
#define node_state_string	slurm_node_state_string
#define node_state_string_compact slurm_node_state_string_compact
#define private_data_string	slurm_private_data_string
#define accounting_enforce_string slurm_accounting_enforce_string
#define reservation_flags_string slurm_reservation_flags_string
#define print_multi_line_string slurm_print_multi_line_string

/* slurmdbd_defs.[ch] functions */
#define slurmdbd_defs_init      slurm_slurmdbd_defs_init
#define slurmdbd_defs_fini      slurm_slurmdbd_defs_fini
#define slurmdbd_free_buffer	slurm_slurmdbd_free_buffer
#define slurmdbd_free_list_msg	slurm_slurmdbd_free_list_msg
#define slurmdbd_free_usage_msg slurm_slurmdbd_free_usage_msg
#define slurmdbd_free_id_rc_msg slurm_slurmdbd_free_id_rc_msg

/* slurmdbd_pack.[ch] functions */
#define pack_slurmdbd_msg	slurm_pack_slurmdbd_msg
#define unpack_slurmdbd_msg	slurm_unpack_slurmdbd_msg

/* plugin.[ch] functions */
#define plugin_get_syms         slurm_plugin_get_syms
#define plugin_load_and_link    slurm_plugin_load_and_link
#define plugin_strerror         slurm_plugin_strerror
#define plugin_unload           slurm_plugin_unload

/* plugrack.[ch] functions */
#define plugrack_create         slurm_plugrack_create
#define plugrack_destroy        slurm_plugrack_destroy
#define plugrack_read_dir       slurm_plugrack_read_dir
#define plugrack_set_major_type slurm_plugrack_set_major_type
#define plugrack_use_by_type    slurm_plugrack_use_by_type

#define jobacctinfo_pack	slurm_jobacctinfo_pack
#define jobacctinfo_unpack	slurm_jobacctinfo_unpack
#define jobacctinfo_create      slurm_jobacctinfo_create
#define jobacctinfo_destroy     slurm_jobacctinfo_destroy

/* parse_config.[ch] functions */
#define s_p_hashtbl_create	slurm_s_p_hashtbl_create
#define s_p_hashtbl_destroy	slurm_s_p_hashtbl_destroy
#define s_p_parse_file		slurm_s_p_parse_file
#define s_p_parse_pair		slurm_s_p_parse_pair
#define s_p_parse_line		slurm_s_p_parse_line
#define s_p_hashtbl_merge 	slurm_s_p_hashtbl_merge
#define s_p_get_string		slurm_s_p_get_string
#define s_p_get_long		slurm_s_p_get_long
#define s_p_get_uint16		slurm_s_p_get_uint16
#define s_p_get_uint32		slurm_s_p_get_uint32
#define s_p_get_uint64		slurm_s_p_get_uint64
#define s_p_get_pointer		slurm_s_p_get_pointer
#define s_p_get_array		slurm_s_p_get_array
#define s_p_get_boolean		slurm_s_p_get_boolean
#define s_p_dump_values		slurm_s_p_dump_values
#define transfer_s_p_options	slurm_transfer_s_p_options

/* slurm_step_layout.[ch] functions */
#define pack_slurm_step_layout          slurm_pack_slurm_step_layout
#define unpack_slurm_step_layout        slurm_unpack_slurm_step_layout

/* slurm_step_route.[ch] functions */
#define route_split_hostlist_treewidth	slurm_route_split_hostlist_treewidth


#define eio_handle_create		slurm_eio_handle_create
#define eio_handle_destroy		slurm_eio_handle_destroy
#define eio_handle_mainloop		slurm_eio_handle_mainloop
#define eio_message_socket_accept	slurm_eio_message_socket_accept
#define eio_message_socket_readable	slurm_eio_message_socket_readable
#define eio_new_obj			slurm_eio_new_obj
#define eio_new_initial_obj		slurm_eio_new_initial_obj
#define eio_obj_create			slurm_eio_obj_create
#define eio_obj_destroy			slurm_eio_obj_destroy
#define eio_remove_obj			slurm_eio_remove_obj
#define eio_signal_shutdown		slurm_eio_signal_shutdown
#define eio_signal_wakeup		slurm_eio_signal_wakeup

/* callerid.[ch] functions */
#define callerid_get_own_netinfo	slurm_callerid_get_own_netinfo

/* some stepd_api.[ch] functions */
#define stepd_available			slurm_stepd_available
#define stepd_connect			slurm_stepd_connect
#define stepd_get_uid			slurm_stepd_get_uid
#define stepd_add_extern_pid		slurm_stepd_add_extern_pid
#define stepd_get_x11_display		slurm_stepd_get_x11_display
#define stepd_get_info			slurm_stepd_get_info
#define stepd_getpw			slurm_stepd_getpw
#define xfree_struct_passwd		slurm_xfree_struct_passwd
#define stepd_getgr			slurm_stepd_getgr
#define xfree_struct_group_array	slurm_xfree_struct_group_array

/* xcgroup_read_config.[ch] */
#define xcgroup_config_read_mutex       slurm_xcgroup_config_read_mutex
#define xcgroup_get_slurm_cgroup_conf   slurm_xcgroup_get_slurm_cgroup_conf
#define xcgroup_fini_slurm_cgroup_conf  slurm_xcgroup_fini_slurm_cgroup_conf

#endif /* USE_ALIAS */

/* Include the function definitions after redefining their names. */
#include "src/common/bitstring.h"
#include "src/common/callerid.h"
#include "src/common/eio.h"
#include "src/common/env.h"
#include "src/common/hostlist.h"
#include "src/common/list.h"
#include "src/common/log.h"
#include "src/common/macros.h"
#include "src/common/node_select.h"
#include "src/common/pack.h"
#include "src/common/parse_config.h"
#include "src/common/read_config.h"
#include "src/common/slurm_auth.h"
#include "src/common/slurm_jobacct_gather.h"
#include "src/common/slurm_route.h"
#include "src/common/slurm_step_layout.h"
#include "src/common/strlcpy.h"
#include "src/common/stepd_api.h"
#include "src/common/switch.h"
#include "src/common/working_cluster.h"
#include "src/common/xassert.h"
#include "src/common/xcgroup_read_config.h"
#include "src/common/xmalloc.h"
#include "src/common/xsignal.h"
#include "src/common/xstring.h"

#endif /*__SLURM_XLATOR_H__*/
