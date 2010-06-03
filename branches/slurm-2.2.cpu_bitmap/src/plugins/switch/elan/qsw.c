/*****************************************************************************\
 *  qsw.c - Library routines for initiating jobs on QsNet.
 *  $Id$
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Jim Garlick <garlick@llnl.gov>
 *  CODE-OCEC-09-009. All rights reserved.
 *
 *  This file is part of SLURM, a resource management program.
 *  For details, see <https://computing.llnl.gov/linux/slurm/>.
 *  Please also read the included file: DISCLAIMER.
 *
 *  SLURM is free software; you can redistribute it and/or modify it under
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
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
\*****************************************************************************/

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#ifdef WITH_PTHREADS
#  include <pthread.h>
#endif /* WITH_PTHREADS */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>
#include <paths.h>
#include <stdarg.h>
#include <ctype.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>	/* INT_MAX */
#include <stdio.h>

#if HAVE_LIBELANCTRL
# include <elan/elanctrl.h>
# include <elan/capability.h>

/* These are taken from elan3/elanvp.h, which we don't
 *  want to include here since we are using the new
 *  version-nonspecific libelanctrl.
 *  (XXX: What is the equivalent in libelanctrl?)
 *
 * slurm/482: the elan USER context range is now split
 *  into two segments, regular user context and RMS
 *  context ranges. Do not allow a context range
 *  (lowcontext -- highcontext) to span these two segments,
 *  as this will generate and elan initialization error
 *  when MPI tries to attach to the capability. For now,
 *  restrict SLURM's range to the RMS one (starting at 0x400)
 *
 */
# define ELAN_USER_BASE_CONTEXT_NUM    0x400 /* act. RMS_BASE_CONTEXT_NUM */
# define ELAN_USER_TOP_CONTEXT_NUM     0x7ff

# define Version      cap_version
# define HighNode     cap_highnode
# define LowNode      cap_lownode
# define HighContext  cap_highcontext
# define LowContext   cap_lowcontext
# define MyContext    cap_mycontext
# define Bitmap       cap_bitmap
# define Type         cap_type
# define UserKey      cap_userkey
# define RailMask     cap_railmask
# define Values       key_values
#elif HAVE_LIBELAN3
# include <elan3/elan3.h>
# include <elan3/elanvp.h>
#else
# error "Must have either libelan3 or libelanctrl to compile this module!"
#endif /* HAVE_LIBELANCTRL */

#include <rms/rmscall.h>
#include <elanhosts.h>

#include <slurm/slurm_errno.h>

#include "src/common/slurm_xlator.h"

#include "src/plugins/switch/elan/qsw.h"

/*
 * Definitions local to this module.
 */
#define _DEBUG			0
#define QSW_JOBINFO_MAGIC 	0xf00ff00e
#define QSW_LIBSTATE_MAGIC 	0xf00ff00f

/* we will allocate program descriptions in this range */
/* XXX note: do not start at zero as libelan shifts to get unique shm id */
#define QSW_PRG_START  		1
#define QSW_PRG_END    		INT_MAX
#define QSW_PRG_INVAL		(-1)

/* we allocate elan hardware context numbers in this range */
#define QSW_CTX_START		ELAN_USER_BASE_CONTEXT_NUM

/* XXX: Temporary workaround for slurm/222 (qws sw-kernel/5478)
 *      (sys_validate_cap does not allow ELAN_USER_TOP_CONTEXT_NUM)
 */
#define QSW_CTX_END		ELAN_USER_TOP_CONTEXT_NUM - 1
#define QSW_CTX_INVAL		(-1)


/*
 * We are going to some trouble to keep these defs private so slurm
 * hackers not interested in the interconnect details can just pass around
 * the opaque types.  All use of the data structure internals is local to this
 * module.
 */
struct step_ctx {
	uint32_t  st_prognum;
	uint32_t  st_low;
	uint32_t  st_high;
	uint16_t  st_low_node;
	uint16_t  st_high_node;
};

struct qsw_libstate {
	uint32_t ls_magic;
	uint32_t ls_prognum;
	List     step_ctx_list;
};

struct qsw_jobinfo {
	uint32_t        j_magic;
	uint32_t        j_prognum;
	ELAN_CAPABILITY j_cap;
};

/* Lock on library state */
#define _lock_qsw() do {				\
	int err;					\
	err = pthread_mutex_lock(&qsw_lock);		\
	assert(err == 0);				\
} while (0)
#define _unlock_qsw() do {				\
	int err;					\
	err = pthread_mutex_unlock(&qsw_lock);		\
	assert(err == 0);				\
} while (0)

/*
 * Globals
 */
static inline void _dump_step_ctx(const char *head,
		struct step_ctx *step_ctx_p);
static qsw_libstate_t qsw_internal_state = NULL;
static pthread_mutex_t qsw_lock = PTHREAD_MUTEX_INITIALIZER;
static elanhost_config_t elanconf = NULL;
static int shmid = -1;


static inline void _step_ctx_del(void *ptr)
{
	struct step_ctx *step_ctx_p = (struct step_ctx *) ptr;

	xfree(step_ctx_p);
}

/*
 * Allocate a qsw_libstate_t.
 *   lsp (IN)		store pointer to new instantiation here
 *   RETURN		0 on success, -1 on failure (sets errno)
 */
int
qsw_alloc_libstate(qsw_libstate_t *lsp)
{
	qsw_libstate_t new;

	assert(lsp != NULL);
	new = (qsw_libstate_t)xmalloc(sizeof(struct qsw_libstate));
	if (!new)
		slurm_seterrno_ret(ENOMEM);
	new->ls_magic = QSW_LIBSTATE_MAGIC;
	new->step_ctx_list = list_create(_step_ctx_del);
	if (!new->step_ctx_list) {
		qsw_free_libstate(new);
		slurm_seterrno_ret(ENOMEM);
	}
	*lsp = new;
	return 0;
}

/*
 * Free a qsw_libstate_t.
 *   ls (IN)		qsw_libstate_t to free
 */
void
qsw_free_libstate(qsw_libstate_t ls)
{
	assert(ls->ls_magic == QSW_LIBSTATE_MAGIC);
	if (ls->step_ctx_list)
		list_destroy(ls->step_ctx_list);
	ls->ls_magic = 0;
	xfree(ls);
}

static inline void _dump_step_ctx(const char *head, struct step_ctx *step_ctx_p)
{
#if _DEBUG
	info("%s: prog:%u context:%u:%u nodes:%u:%u", head,
		step_ctx_p->st_prognum, step_ctx_p->st_low, step_ctx_p->st_high,
		step_ctx_p->st_low_node, step_ctx_p->st_high_node);
#endif
}

static void
_pack_step_ctx(struct step_ctx *step_ctx_p, Buf buffer)
{
	_dump_step_ctx("_pack_step_ctx", step_ctx_p);
	pack32(step_ctx_p->st_prognum,	buffer);
	pack32(step_ctx_p->st_low,	buffer);
	pack32(step_ctx_p->st_high,	buffer);
	pack16(step_ctx_p->st_low_node,	buffer);
	pack16(step_ctx_p->st_high_node,buffer);
}

/*
 * Pack libstate structure in a format that can be shipped over the
 * network and unpacked on a different architecture.
 *   ls (IN)		libstate structure to be packed
 *   buffer (IN/OUT)	where to store packed data
 *   RETURN		#bytes unused in 'data'
 */
int
qsw_pack_libstate(qsw_libstate_t ls, Buf buffer)
{
	int offset;
	uint16_t step_ctx_cnt;
	ListIterator iter;
	struct step_ctx *step_ctx_p;

	assert(ls->ls_magic == QSW_LIBSTATE_MAGIC);
	offset = get_buf_offset(buffer);

	pack32(ls->ls_magic,	buffer);
	pack32(ls->ls_prognum,	buffer);

	if (ls->step_ctx_list)
		step_ctx_cnt = list_count(ls->step_ctx_list);
	else
		step_ctx_cnt = 0;
	pack16(step_ctx_cnt, buffer);
	if (step_ctx_cnt) {
		iter = list_iterator_create(ls->step_ctx_list);
		while ((step_ctx_p = list_next(iter)))
			_pack_step_ctx(step_ctx_p, buffer);
		list_iterator_destroy(iter);
	}

	return (get_buf_offset(buffer) - offset);
}


static int
_unpack_step_ctx(struct step_ctx *step_ctx_p, Buf buffer)
{
	safe_unpack32(&step_ctx_p->st_prognum,	buffer);
	safe_unpack32(&step_ctx_p->st_low,	buffer);
	safe_unpack32(&step_ctx_p->st_high,	buffer);
	safe_unpack16(&step_ctx_p->st_low_node,	buffer);
	safe_unpack16(&step_ctx_p->st_high_node,	buffer);
	_dump_step_ctx("_unpack_step_ctx", step_ctx_p);
	return 0;
unpack_error:
        return -1;
}


/*
 * Unpack libstate packed by qsw_pack_libstate.
 *   ls (IN/OUT)	where to put libstate structure
 *   buffer (IN/OUT)	where to get packed data
 *   RETURN		#bytes unused or -1 on error (sets errno)
 */
int
qsw_unpack_libstate(qsw_libstate_t ls, Buf buffer)
{
	int offset, i;
	uint16_t step_ctx_cnt;
	struct step_ctx *step_ctx_p;

	assert(ls->ls_magic == QSW_LIBSTATE_MAGIC);
	offset = get_buf_offset(buffer);

	safe_unpack32(&ls->ls_magic,	buffer);
	safe_unpack32(&ls->ls_prognum,	buffer);
	safe_unpack16(&step_ctx_cnt,	buffer);

	for (i=0; i<step_ctx_cnt; i++) {
		step_ctx_p = xmalloc(sizeof(struct step_ctx));
		if (_unpack_step_ctx(step_ctx_p, buffer) == -1) {
			_step_ctx_del(step_ctx_p);
			goto unpack_error;
		}
		list_push(ls->step_ctx_list, step_ctx_p);
	}

	if (ls->ls_magic != QSW_LIBSTATE_MAGIC)
		goto unpack_error;

	return SLURM_SUCCESS;

    unpack_error:
	slurm_seterrno_ret(EBADMAGIC_QSWLIBSTATE); /* corrupted libstate */
	return SLURM_ERROR;
}

/*
 * Seed the random number generator.  This can be called multiple times,
 * but srand48 will only be called once per program invocation.
 */
static void
_srand_if_needed(void)
{
	static int done = 0;

	if (!done) {
		srand48(getpid());
		done = 1;
	}
}

static void
_copy_libstate(qsw_libstate_t dest, qsw_libstate_t src)
{
	ListIterator iter;
	struct step_ctx *src_step_ctx_p, *dest_step_ctx_p;

	assert(src->ls_magic  == QSW_LIBSTATE_MAGIC);
	assert(dest->ls_magic == QSW_LIBSTATE_MAGIC);
	dest->ls_prognum = src->ls_prognum;
	iter = list_iterator_create(src->step_ctx_list);
	while ((src_step_ctx_p = list_next(iter))) {
		dest_step_ctx_p = xmalloc(sizeof(struct step_ctx));
		dest_step_ctx_p->st_prognum   = src_step_ctx_p->st_prognum;
		dest_step_ctx_p->st_low       = src_step_ctx_p->st_low;
		dest_step_ctx_p->st_high      = src_step_ctx_p->st_high;
		dest_step_ctx_p->st_low_node  = src_step_ctx_p->st_low_node;
		dest_step_ctx_p->st_high_node = src_step_ctx_p->st_high_node;
		list_push(dest->step_ctx_list, dest_step_ctx_p);
	}
	list_iterator_destroy(iter);
}

/*
 * Initialize this library, optionally restoring a previously saved state.
 *   oldstate (IN)	old state retrieved from qsw_fini() or NULL
 *   RETURN		0 on success, -1 on failure (sets errno)
 */
int
qsw_init(qsw_libstate_t oldstate)
{
	qsw_libstate_t new;

	assert(qsw_internal_state == NULL);
	_srand_if_needed();
	if (qsw_alloc_libstate(&new) < 0)
		return -1; /* errno set by qsw_alloc_libstate */
	if (oldstate)
		_copy_libstate(new, oldstate);
	else {
		new->ls_prognum = QSW_PRG_START +
			(lrand48() % (QSW_PRG_END - QSW_PRG_START + 1));
	}
	qsw_internal_state = new;
	return 0;
}

/*
 * Finalize use of this library.  If 'savestate' is non-NULL, final
 * state is copied there before it is destroyed.
 *   savestate (OUT)	place to put state
 */
void
qsw_fini(qsw_libstate_t savestate)
{
	assert(qsw_internal_state != NULL);
	_lock_qsw();
	if (savestate)
		_copy_libstate(savestate, qsw_internal_state);
	qsw_free_libstate(qsw_internal_state);
	qsw_internal_state = NULL;
	if (elanconf) {
		elanhost_config_destroy(elanconf);
		elanconf = NULL;
	}
	_unlock_qsw();
}

int
qsw_clear(void)
{
	int rc = 0;

	_lock_qsw();
	assert(qsw_internal_state);
	assert(qsw_internal_state->ls_magic == QSW_LIBSTATE_MAGIC);
	if (qsw_internal_state->step_ctx_list)
		list_destroy(qsw_internal_state->step_ctx_list);
	qsw_internal_state->step_ctx_list = list_create(_step_ctx_del);
	if (elanconf)
		elanhost_config_destroy(elanconf);
	if (!(elanconf = elanhost_config_create ())) {
		rc = -1;
		goto done;
	}
	qsw_internal_state->ls_prognum = QSW_PRG_START +
		 (lrand48() % (QSW_PRG_END - QSW_PRG_START + 1));
done:	_unlock_qsw();
	return rc;
}

/*
 * Allocate a qsw_jobinfo_t.
 *   jp (IN)		store pointer to new instantiation here
 *   RETURN		0 on success, -1 on failure (sets errno)
 */
int
qsw_alloc_jobinfo(qsw_jobinfo_t *jp)
{
	qsw_jobinfo_t new;

	assert(jp != NULL);
	new = (qsw_jobinfo_t)xmalloc(sizeof(struct qsw_jobinfo));
	if (!new)
		slurm_seterrno_ret(ENOMEM);
	new->j_magic = QSW_JOBINFO_MAGIC;

	*jp = new;
	return 0;
}

/*
 * Make a copy of a qsw_jobinfo_t.
 *   j (IN)		qsw_jobinfo_t to be copied
 *   RETURN		qsw_jobinfo_t on success, NULL on failure
 */
qsw_jobinfo_t
qsw_copy_jobinfo(qsw_jobinfo_t j)
{
	qsw_jobinfo_t new;
	if (qsw_alloc_jobinfo(&new))
		return NULL;
	memcpy(new, j, sizeof(struct qsw_jobinfo));

	return new;
}

/*
 * Free a qsw_jobinfo_t.
 *   ls (IN)		qsw_jobinfo_t to free
 */
void
qsw_free_jobinfo(qsw_jobinfo_t j)
{
	if (j == NULL)
		return;
	assert(j->j_magic == QSW_JOBINFO_MAGIC);
	j->j_magic = 0;
	xfree(j);
}

/*
 * Pack jobinfo structure in a format that can be shipped over the
 * network and unpacked on a different architecture.
 *   j (IN)		jobinfo structure to be packed
 *   buffer (OUT)		where to store packed data
 *   RETURN		#bytes unused in 'data' or -1 on error (sets errno)
 * NOTE: Keep in sync with QSW_PACK_SIZE above
 */
int
qsw_pack_jobinfo(qsw_jobinfo_t j, Buf buffer)
{
	int i, offset;

	assert(j->j_magic == QSW_JOBINFO_MAGIC);
	offset = get_buf_offset(buffer);

	pack32(j->j_magic, 		buffer);
	pack32(j->j_prognum, 		buffer);
	for (i = 0; i < 4; i++)
		pack32(j->j_cap.UserKey.Values[i], buffer);
	pack16(j->j_cap.Type, 		buffer);
#if HAVE_LIBELANCTRL
#  ifdef ELAN_CAP_ELAN3
	pack16(j->j_cap.cap_elan_type,  buffer);
#  else
	j->j_cap.cap_spare = ELAN_CAP_UNINITIALISED;
	pack16(j->j_cap.cap_spare,      buffer);
#  endif
#endif
#if HAVE_LIBELAN3
	pack16(j->j_cap.padding, 	buffer);
#endif
	pack32(j->j_cap.Version,	buffer);
	pack32(j->j_cap.LowContext, 	buffer);
	pack32(j->j_cap.HighContext, 	buffer);
	pack32(j->j_cap.MyContext, 	buffer);
	pack32(j->j_cap.LowNode, 	buffer);
	pack32(j->j_cap.HighNode, 	buffer);
#if HAVE_LIBELAN3
	pack32(j->j_cap.Entries, 	buffer);
#endif
	pack32(j->j_cap.RailMask, 	buffer);
	for (i = 0; i < ELAN_BITMAPSIZE; i++)
		pack32(j->j_cap.Bitmap[i], buffer);

	return (get_buf_offset(buffer) - offset);
}

/*
 * Unpack jobinfo structure packed by qsw_pack_jobinfo.
 *   j (IN/OUT)		where to store libstate structure
 *   buffer (OUT)		where to load packed data
 *   RETURN		#bytes unused in 'data' or -1 on error (sets errno)
 */
int
qsw_unpack_jobinfo(qsw_jobinfo_t j, Buf buffer)
{
	int i, offset;

	assert(j->j_magic == QSW_JOBINFO_MAGIC);
	offset = get_buf_offset(buffer);

	safe_unpack32(&j->j_magic, 		buffer);
	safe_unpack32(&j->j_prognum, 		buffer);
	for (i = 0; i < 4; i++)
		safe_unpack32(&j->j_cap.UserKey.Values[i], buffer);
	safe_unpack16(&j->j_cap.Type,		buffer);
#if HAVE_LIBELANCTRL
#  ifdef ELAN_CAP_ELAN3
	safe_unpack16(&j->j_cap.cap_elan_type,  buffer);
#  else
	safe_unpack16(&j->j_cap.cap_spare,      buffer);
#  endif
#endif
#if HAVE_LIBELAN3
	safe_unpack16(&j->j_cap.padding, 	buffer);
#endif
{
	uint32_t tmp32;
	safe_unpack32(&tmp32,	buffer);
	j->j_cap.Version	= (int) tmp32;
	safe_unpack32(&tmp32,	buffer);
	j->j_cap.LowContext	= (int) tmp32;
	safe_unpack32(&tmp32,	buffer);
	j->j_cap.HighContext	= (int) tmp32;
	safe_unpack32(&tmp32,	buffer);
	j->j_cap.MyContext	= (int) tmp32;
	safe_unpack32(&tmp32,	buffer);
	j->j_cap.LowNode	= (int) tmp32;
	safe_unpack32(&tmp32,	buffer);
	j->j_cap.HighNode	= (int) tmp32;
}
#if HAVE_LIBELAN3
	safe_unpack32(&j->j_cap.Entries, 	buffer);
#endif
	safe_unpack32(&j->j_cap.RailMask, 	buffer);
	for (i = 0; i < ELAN_BITMAPSIZE; i++)
		safe_unpack32(&j->j_cap.Bitmap[i], buffer);

	if (j->j_magic != QSW_JOBINFO_MAGIC)
		goto unpack_error;

	return SLURM_SUCCESS;

    unpack_error:
	slurm_seterrno_ret(EBADMAGIC_QSWJOBINFO);
	return SLURM_ERROR;
}

/*
 * Allocate a program description number.  Program descriptions, which are the
 * key abstraction maintained by the rms.o kernel module, must not be used
 * more than once simultaneously on a single node.  We allocate one to each
 * parallel job which more than meets this requirement.  A program description
 * can be compared to a process group, except there is no way for a process to
 * disassociate itself or its children from the program description.
 * If the library is initialized, we allocate these consecutively, otherwise
 * we generate a random one, assuming we are being called by a transient
 * program like pdsh.  Ref: rms_prgcreate(3).
 */
static int
_generate_prognum(void)
{
	int new;

	if (qsw_internal_state) {
		_lock_qsw();
		new = qsw_internal_state->ls_prognum;
		if (new == QSW_PRG_END)
			qsw_internal_state->ls_prognum = QSW_PRG_START;
		else
			qsw_internal_state->ls_prognum++;
		_unlock_qsw();
	} else {
		_srand_if_needed();
		new = lrand48() % (QSW_PRG_END - QSW_PRG_START + 1);
		new += QSW_PRG_START;
	}
	return new;
}

/*
 * Elan hardware context numbers are an adapter resource that must not be used
 * more than once on a single node.  One is allocated to each process on the
 * node that will be communicating over Elan.  In order for processes on the
 * same node to communicate with one another and with other nodes across QsNet,
 * they must use contexts in the hi-lo range of a common capability.
 * If the library state is initialized, we allocate/free these, otherwise
 * we generate a random one, assuming we are being called by a transient
 * program like pdsh.  Ref: rms_setcap(3).
 *
 * Returns -1 on allocation error.
 */
static int
_alloc_hwcontext(bitstr_t *nodeset, uint32_t prognum, int num)
{
	int new = -1;
	static int seed = 0;

	assert(nodeset);
	if (qsw_internal_state) {
		bitoff_t bit;
		ListIterator iter;
		uint16_t low_node  = bit_ffs(nodeset);
		uint16_t high_node = bit_fls(nodeset);
		struct step_ctx *step_ctx_p;
		bitstr_t *busy_context = bit_alloc(QSW_CTX_END -
				QSW_CTX_START + 1);

		assert(busy_context);
		_lock_qsw();
		iter = list_iterator_create(qsw_internal_state->step_ctx_list);
		while ((step_ctx_p = list_next(iter))) {
			if ((high_node < step_ctx_p->st_low_node)
			||  (low_node  > step_ctx_p->st_high_node))
				continue;
			bit_nset(busy_context, step_ctx_p->st_low,
						step_ctx_p->st_high);
		}
		list_iterator_destroy(iter);
		bit = bit_noc(busy_context, num, seed);
		if (bit != -1) {
			seed = bit + num;
			step_ctx_p = xmalloc(sizeof(struct step_ctx));
			step_ctx_p->st_prognum   = prognum;
			step_ctx_p->st_low       = bit;
			step_ctx_p->st_high      = bit + num - 1;
			step_ctx_p->st_low_node  = low_node;
			step_ctx_p->st_high_node = high_node;
			_dump_step_ctx("_alloc_hwcontext", step_ctx_p);
			list_push(qsw_internal_state->step_ctx_list, step_ctx_p);
			new = bit + QSW_CTX_START;
		}
		_unlock_qsw();
		bit_free(busy_context);
	} else {
		_srand_if_needed();
		new = lrand48() %
		      (QSW_CTX_END - (QSW_CTX_START + num - 1) - 1);
		new +=  QSW_CTX_START;
	}
	assert(new == -1 || (new >= QSW_CTX_START && new <= QSW_CTX_END));
	return new;
}

extern int qsw_restore_jobinfo(struct qsw_jobinfo *jobinfo)
{
	struct step_ctx *step_ctx_p;
	ListIterator iter;
	int duplicate = 0;

	assert(qsw_internal_state);
	if (!jobinfo)
		return 0;

	assert(jobinfo->j_magic == QSW_JOBINFO_MAGIC);
	_lock_qsw();

	/* check for duplicate */
	iter = list_iterator_create(qsw_internal_state->step_ctx_list);
	while ((step_ctx_p = list_next(iter)))  {
		if (jobinfo->j_prognum == step_ctx_p->st_prognum) {
			duplicate = 1;
			break;
		}
	}
	list_iterator_destroy(iter);
	if (!duplicate) {		/* need new record */
		step_ctx_p = xmalloc(sizeof(struct step_ctx));
		step_ctx_p->st_prognum    = jobinfo->j_prognum;
	}
	step_ctx_p->st_low        = jobinfo->j_cap.LowContext  - QSW_CTX_START;
	step_ctx_p->st_high       = jobinfo->j_cap.HighContext - QSW_CTX_START;
	step_ctx_p->st_low_node   = jobinfo->j_cap.LowNode;
	step_ctx_p->st_high_node  = jobinfo->j_cap.HighNode;
	_dump_step_ctx("qsw_restore_jobinfo", step_ctx_p);
	if (!duplicate)
		list_push(qsw_internal_state->step_ctx_list, step_ctx_p);
	_unlock_qsw();
	return 0;
}

static void
_free_hwcontext(uint32_t prog_num)
{
	ListIterator iter;
	struct step_ctx *step_ctx_p;

	if (qsw_internal_state) {
		_lock_qsw();
		iter = list_iterator_create(qsw_internal_state->step_ctx_list);
		while ((step_ctx_p = list_next(iter)))  {
			if (prog_num != step_ctx_p->st_prognum)
				continue;
			_dump_step_ctx("_free_hwcontext", step_ctx_p);
			list_delete_item(iter);
			break;
		}
		if (!step_ctx_p) {
			error("_free_hwcontext could not find prognum %u",
				prog_num);
		}
		list_iterator_destroy(iter);
		_unlock_qsw();
	}
}

/*
 * Initialize the elan capability for this job.
 * Returns -1 on failure to allocate hw context.
 */
static int
_init_elan_capability(ELAN_CAPABILITY *cap, uint32_t prognum, int ntasks,
		int nnodes, bitstr_t *nodeset, uint16_t *tasks_per_node,
		int cyclic_alloc, int max_tasks_per_node)
{
	int i, node_index;

	_srand_if_needed();

	/* start with a clean slate */
#if HAVE_LIBELANCTRL
	elan_nullcap(cap);
#else
	elan3_nullcap(cap);
#endif

	/* initialize for single rail and either block or cyclic allocation */
	if (cyclic_alloc)
		cap->Type = ELAN_CAP_TYPE_CYCLIC;
	else
		cap->Type = ELAN_CAP_TYPE_BLOCK;
	cap->Type |= ELAN_CAP_TYPE_MULTI_RAIL;
	cap->RailMask = 1;

#if HAVE_LIBELANCTRL
#  ifdef ELAN_CAP_ELAN3
	cap->cap_elan_type = ELAN_CAP_ELAN3;
#  else
	cap->cap_spare = ELAN_CAP_UNINITIALISED;
#  endif
#endif

	/* UserKey is 128 bits of randomness which should be kept private */
        for (i = 0; i < 4; i++)
		cap->UserKey.Values[i] = lrand48();

	/* set up hardware context range */
	cap->LowContext = _alloc_hwcontext(nodeset, prognum, max_tasks_per_node);
	if (cap->LowContext == -1)
		return -1;
	cap->HighContext = cap->LowContext + max_tasks_per_node - 1;
	/* Note: not necessary to initialize cap->MyContext */

	/* set the range of nodes to be used and number of processes */
	cap->LowNode = bit_ffs(nodeset);
	assert(cap->LowNode != -1);
	cap->HighNode = bit_fls(nodeset);
	assert(cap->HighNode != -1);

#if HAVE_LIBELAN3
	cap->Entries = ntasks;
#endif

#if USE_OLD_LIBELAN
	/* set the hw broadcast bit if consecutive nodes */
	if (abs(cap->HighNode - cap->LowNode) == nnodes - 1)
		cap->Type |= ELAN_CAP_TYPE_BROADCASTABLE;
#else
	/* set unconditionally per qsw gnat sw-elan/4334 */
	/* only time we don't want this is unsupported rev A hardware */
	cap->Type |= ELAN_CAP_TYPE_BROADCASTABLE;
#endif
	/*
	 * Set up cap->Bitmap, which describes the mapping of processes to
	 * the nodes in the range of cap->LowNode - cap->Highnode.
	 * There are (ntasks * nnodes) significant bits in the mask, each
 	 * representing a process slot.  Bits are off for process slots
	 * corresponding to unallocated nodes.  For example, if nodes 4 and 6
	 * are running two processes per node, bits 0,1 (corresponding to the
	 * two processes on node 4) and bits 4,5 (corresponding to the two
	 * processes running on node 6) are set.
	 */
	node_index = 0;
	for (i = cap->LowNode; i <= cap->HighNode; i++) {
		if (bit_test(nodeset, i)) {
			int j, bit, task_cnt;
			task_cnt = tasks_per_node[node_index++];

			for (j = 0; j < task_cnt; j++) {
				if (cyclic_alloc)
					bit = (i-cap->LowNode) + ( j *
					 (cap->HighNode - cap->LowNode + 1));
				else
					bit = ((i-cap->LowNode)
					       * max_tasks_per_node) + j;

				assert(bit < (sizeof(cap->Bitmap) * 8));
				BT_SET(cap->Bitmap, bit);
			}
		}
	}

	return 0;
}

/*
 * Create all the QsNet related information needed to set up a QsNet parallel
 * program and store it in the qsw_jobinfo struct.
 * Call this on the "client" process, e.g. pdsh, srun, slurmctld, etc..
 */
int
qsw_setup_jobinfo(qsw_jobinfo_t j, int ntasks, bitstr_t *nodeset,
		uint16_t *tasks_per_node, int cyclic_alloc)
{
	int i, max_tasks_per_node = 0;
	int nnodes = bit_set_count(nodeset);

	assert(j != NULL);
	assert(j->j_magic == QSW_JOBINFO_MAGIC);
	assert(nodeset);
	assert(tasks_per_node);

	/* sanity check on args */
	if ((ntasks <= 0) || (nnodes <= 0))
		slurm_seterrno_ret(EINVAL);
	for (i = 0; i < nnodes; i++) {
		if (tasks_per_node[i] > max_tasks_per_node)
			max_tasks_per_node = tasks_per_node[i];
	}
	/* Note: ELAN_MAX_VPS is 512 on "old" Elan driver, 16384 on new. */
	if ((max_tasks_per_node * nnodes) > ELAN_MAX_VPS)
		slurm_seterrno_ret(EINVAL);

	/* initialize jobinfo */
	j->j_prognum = _generate_prognum();
	if (_init_elan_capability(&j->j_cap, j->j_prognum, ntasks, nnodes,
			nodeset, tasks_per_node, cyclic_alloc,
			max_tasks_per_node) == -1) {
		slurm_seterrno_ret(EAGAIN); /* failed to allocate hw ctx */
	}

	return 0;
}

void
qsw_teardown_jobinfo(qsw_jobinfo_t j)
{
	if (j)
		_free_hwcontext(j->j_prognum);
}

/*
 * Here are the necessary steps to set up to run an Elan MPI parallel program
 * (set of processes) on a node (possibly one of many allocated to the prog):
 *
 * Process 1	Process 2	|	Process 3
 * read args			|
 * fork	-------	rms_prgcreate	|
 * waitpid 	elan3_create	|
 * 		rms_prgaddcap	|
 *		fork N procs ---+------	rms_setcap
 *		wait all	|	setup RMS_ env
 *				|	setuid, etc.
 *				|	exec mpi process
 *				|
 *		exit		|
 * rms_prgdestroy		|
 * exit				|     (one pair of processes per mpi proc!)
 *
 * - The first fork is required because rms_prgdestroy can't occur in the
 *   process that calls rms_prgcreate (since it is a member, ECHILD).
 * - The second fork is required when running multiple processes per node
 *   because each process must announce its use of one of the hw contexts
 *   in the range allocated in the capability.
 */

void
qsw_prog_fini(qsw_jobinfo_t jobinfo)
{
	if (shmid >= 0) {
		debug2("qsw_prog_fini");
		shmctl (shmid, IPC_RMID, NULL);
		debug2("qsw_prog_fini shmctl IPC_RMID complete");
	}
	/* Do nothing... apparently this will be handled by
	 *  callbacks in the kernel exit handlers ...
	 */
#if 0
	if (jobinfo->j_ctx) {
		elan3_control_close(jobinfo->j_ctx);
		jobinfo->j_ctx = NULL;
	}
#endif
}

/* Key for Elan stats shared memory segment is the
 *  rms.o program description number, left shifted 9 less 1
 *  to avoid conflicts with MPI shared memory
 */
static int elan_statkey (int prgid)
{
	return ((prgid << 9) - 1);
}

/*
 * Return the statkey to caller in keyp if shared memory was created
 * Return -1 if shared memory creation failed.
 */
int qsw_statkey (qsw_jobinfo_t jobinfo, int *keyp)
{
	if (shmid < 0)
		return (-1);
	*keyp = elan_statkey (jobinfo->j_prognum);
	return (0);
}

/*
 * Create shared memory segment for Elan stats use
 *  (ELAN_STATKEY env var is set in switch_elan.c)
 */
static int
_qsw_shmem_create (qsw_jobinfo_t jobinfo, uid_t uid)
{
	struct shmid_ds shm;
	ELAN_CAPABILITY *cap = &jobinfo->j_cap;
	key_t key = elan_statkey (jobinfo->j_prognum);
	int maxLocal = cap->HighContext - cap->LowContext + 1;
	int pgsize = getpagesize ();

	/* 8KB minimum stats page size */
	if (pgsize < 8192)
		pgsize = 8192;

	if ((shmid = shmget (key, pgsize * (maxLocal + 1), IPC_CREAT|IPC_EXCL))
	    < 0)
		return (error ("Failed to create Elan state shmem: %m"));

	/* Ensure permissions on segment allow user read/write access
	 */
	shm.shm_perm.uid  = uid;
	shm.shm_perm.mode = 0600;

	if (shmctl (shmid, IPC_SET, &shm) < 0)
		return (error ("Failed to set perms on Elan state shm: %m"));

	return (0);
}


static void
_close_all_fd_except(int fd)
{
        int openmax;
        int i;

        openmax = sysconf(_SC_OPEN_MAX);
        for (i = 0; i <= openmax; i++) {
                if (i != fd)
                        close(i);
        }
}


/*
 * Process 1: After the fork, the child process is process 1,
 *            and will call rms_prgdestroy when the parent (slurmd job
 *            manager) exits.
 */
static int
_prg_destructor_fork()
{
	pid_t pid;
	int fdpair[2];
	int prgid;
	int i;
	int dummy;

	if (pipe(fdpair) < 0) {
		error("switch/elan: failed creating pipe");
		return -1;
	}

	pid = fork();
	if (pid < 0) {
		error("switch/elan: failed to fork program destructor");
	} else if (pid > 0) {
		/* parent */
		close(fdpair[0]);
		waitpid(pid, (int *)NULL, 0);
		return fdpair[1];
	}

	/****************************************/
	/*
	 * fork again so the destructor process
	 * will not be a child of the slurmd
	 */
	pid = fork();
	if (pid < 0) {
		error("switch/elan: second fork failed");
	} else if (pid > 0) {
		exit(0);
	}

	/* child */
	close(fdpair[1]);

	/* close librmscall's internal fd to /proc/rms/control */
	rmsmod_fini();

	_close_all_fd_except(fdpair[0]);
        /* Wait for the program description id from the child */
        if (read(fdpair[0], &prgid, sizeof(prgid)) != sizeof(prgid)) {
		error("_prg_destructor_fork read failed: %m");
		exit(1);
	}

	if (prgid == -1)
		exit(1);

	/*
	 * Wait for the pipe to close, signalling that the parent
	 * has exited.
	 */
	while (read(fdpair[0], &dummy, sizeof(dummy)) > 0) {}

	/*
	 * Verify that program description is empty.  If not, send a SIGKILL.
	 */
	for (i = 0; i < 30; i++) {
		int maxids = 8;
		pid_t pids[8];
		int nids = 0;

		if (rms_prginfo(prgid, maxids, pids, &nids) < 0) {
			error("switch/elan: rms_prginfo: %m");
		}
		if (nids == 0)
			break;
		if (rms_prgsignal(prgid, SIGKILL) < 0) {
			error("switch/elan: rms_prgsignal: %m");
		}
		sleep(1);
	}

	if (rms_prgdestroy(prgid) < 0) {
		error("rms_prgdestroy");
	}
	exit(0);
}



/*
 * Send the prgid of the newly created program description to the process
 * forked earlier by _prg_destructor_fork(), using the file descriptor
 * "fd" which was returned by the call to _prg_destructor_fork().
 */
static void
_prg_destructor_send(int fd, int prgid)
{
	debug3("_prg_destructor_send %d", prgid);
	if (write (fd, &prgid, sizeof(prgid)) != sizeof(prgid)) {
		error ("_prg_destructor_send failed: %m");
	}
	/* Deliberately avoid closing fd.  When this process exits, it
	   will close fd signalling to the child process that it is
	   time to call rms_prgdestroy */
	/*close(fd);*/
}

/*
 * Process 2: Create the context and make capability available to children.
 */
int
qsw_prog_init(qsw_jobinfo_t jobinfo, uid_t uid)

{
	int err;
	int i, nrails;
	int fd;

	if ((fd = _prg_destructor_fork()) == -1)
		goto fail;
#if HAVE_LIBELANCTRL
	nrails = elan_nrails(&jobinfo->j_cap);

	for (i = 0; i < nrails; i++) {
		ELANCTRL_HANDLE handle;
		/*
		 *  Open up the Elan control device so we can create
		 *   a new capability.
		 */
		if (elanctrl_open(&handle) != 0) {
			slurm_seterrno(EELAN3CONTROL);
			_prg_destructor_send(fd, -1);
			goto fail;
		}

		/*  Push capability into device driver */
		if (elanctrl_create_cap(handle, &jobinfo->j_cap) < 0) {
			error("elanctrl_create_cap: %m");
			slurm_seterrno(EELAN3CREATE);
			/* elanctrl_close(handle); */
			_prg_destructor_send(fd, -1);
			goto fail;
		}

		/* elanctrl_close (handle); */
	}

#else /* !HAVE_LIBELANCTRL */
	nrails = elan3_nrails(&jobinfo->j_cap);

	for (i = 0; i < nrails; i++) {

		ELAN3_CTX *ctx;

		/* see qsw gnat sw-elan/4334: elan3_control_open can ret -1 */
		if ((ctx = elan3_control_open(i)) == NULL
				|| ctx == (void *)-1) {
			slurm_seterrno(EELAN3CONTROL);
			_prg_destructor_send(fd, -1);
			goto fail;
		}


		/* make cap known via rms_getcap/rms_ncaps to members
		 * of this prgnum */
		if (elan3_create(ctx, &jobinfo->j_cap) < 0) {
			/* XXX masking errno value better than not knowing
			 * which function failed? */
		        error("elan3_create(%d): %m", i);
			slurm_seterrno(EELAN3CREATE);
			_prg_destructor_send(fd, -1);
			goto fail;
		}
	}
#endif
	/* associate this process and its children with prgnum */
	if (rms_prgcreate(jobinfo->j_prognum, uid, 1) < 0) {
		/* translate errno values to more descriptive ones */
		switch (errno) {
			case EINVAL:
				slurm_seterrno(EINVAL_PRGCREATE);
				break;
			default:
				break;
		}
		_prg_destructor_send(fd, -1);
		goto fail;
	}
	_prg_destructor_send(fd, jobinfo->j_prognum);

	if (rms_prgaddcap(jobinfo->j_prognum, 0, &jobinfo->j_cap) < 0) {
		/* translate errno values to more descriptive ones */
		switch (errno) {
			case ESRCH:
				slurm_seterrno(ESRCH_PRGADDCAP);
				break;
			case EFAULT:
				slurm_seterrno(EFAULT_PRGADDCAP);
				break;
			default:
				break;
		}
		goto fail;
	}


	/*
 	 * Create shared memory for libelan state
	 *  Failure to create shared memory is not a fatal error.
	 */
	_qsw_shmem_create (jobinfo, uid);


	/* note: _elan3_fini() destroys context and makes capability unavail */
	/* do it in qsw_prog_fini() after app terminates */
	return 0;
fail:
	err = errno; /* presrve errno in case _elan3_fini touches it */
	qsw_prog_fini(jobinfo);
	slurm_seterrno(err);
	return -1;
}

/*
 * Process 3: Do the rms_setcap.
 */
int
qsw_setcap(qsw_jobinfo_t jobinfo, int procnum)
{
	/*
	 * Assign elan hardware context to current process.
	 * - arg1 (0 below) is an index into the kernel's list of caps for this
	 *   program desc (added by rms_prgaddcap).  There will be
	 *   one per rail.
	 * - arg2 indexes the hw ctxt range in the capability
	 *   [cap->LowContext, cap->HighContext]
	 */
	if (rms_setcap(0, procnum) < 0) {
		/* translate errno values to more descriptive ones */
		switch (errno) {
			case EINVAL:
				slurm_seterrno(EINVAL_SETCAP);
				break;
			case EFAULT:
				slurm_seterrno(EFAULT_SETCAP);
				break;
			default:
				break;
		}
		return -1;
	}
	return 0;
}


/*
 * Return the local elan address (for rail 0) or -1 on failure.
 */
int
qsw_getnodeid(void)
{
	int nodeid = -1;
#if HAVE_LIBELANCTRL
	ELAN_DEV_IDX    devidx = 0;
	ELANCTRL_HANDLE handle;
	ELAN_POSITION   position;

	if (elanctrl_open(&handle) != 0)
		slurm_seterrno_ret(EGETNODEID);

	if (elanctrl_get_position(handle, devidx, &position) != 0) {
		elanctrl_close (handle);
		slurm_seterrno_ret(EGETNODEID);
	}

	nodeid = position.pos_nodeid;

	elanctrl_close (handle);
#else
	ELAN3_CTX *ctx = _elan3_init(0); /* rail 0 */
	if (ctx) {
		nodeid = ctx->devinfo.Position.NodeId;
		elan3_control_close(ctx);
	}
#endif
	if (nodeid == -1)
		slurm_seterrno(EGETNODEID);
	return nodeid;

}

static int
_read_elanhost_config (void)
{
	int rc;

	if (!(elanconf = elanhost_config_create ()))
		return (-1);

	if ((rc = elanhost_config_read (elanconf, NULL)) < 0) {
		error ("Unable to read Elan config: %s",
		       elanhost_config_err (elanconf));
		elanhost_config_destroy (elanconf);
		elanconf = NULL;
		return (-1);
	}

	return (0);
}

int
qsw_maxnodeid(void)
{
	int maxid = -1;

	_lock_qsw();
	if (!elanconf && (_read_elanhost_config() < 0))
		goto done;

	maxid = elanhost_config_maxid (elanconf);

    done:
	_unlock_qsw();
	return maxid;
}

/*
 * Given a hostname, return the elanid or -1 on error.
 *  Initializes the elanconfig from the default /etc/elanhosts
 *  config file.
 */
int
qsw_getnodeid_byhost(char *host)
{
	int id = -1;

	if (host == NULL)
		return (-1);

	_lock_qsw();
	if (!elanconf && (_read_elanhost_config() < 0))
		goto done;

	xassert (elanconf != NULL);

	id = elanhost_host2elanid (elanconf, host);

    done:
	_unlock_qsw();
	return id;
}

/*
 * Given an elanid, determine the hostname.  Returns -1 on error or the number
 * of characters copied on success.
 * XXX - assumes RMS style hostnames (see above)
 */
int
qsw_gethost_bynodeid(char *buf, int len, int id)
{
	int rc = -1;
	char *hostp;

	if (id < 0) slurm_seterrno_ret(EGETHOST_BYNODEID);

	_lock_qsw();
	if (!elanconf && (_read_elanhost_config() < 0))
		goto done;

	if (!(hostp = elanhost_elanid2host (elanconf, ELANHOST_EIP, id))) {
		slurm_seterrno (EGETHOST_BYNODEID);
		goto done;
	}

	rc = slurm_strlcpy (buf, hostp, len);

    done:
	_unlock_qsw();
	return (rc);
}

/*
 * Send the specified signal to all members of a program description.
 * Returns -1 on failure and sets errno.  Ref: rms_prgsignal(3).
 */
int
qsw_prgsignal(qsw_jobinfo_t jobinfo, int signum)
{
	if (rms_prgsignal(jobinfo->j_prognum, signum) < 0) {
		/* translate errno values to more descriptive ones */
		switch (errno) {
			case EINVAL:
				slurm_seterrno(EINVAL_PRGSIGNAL);
				break;
			case ESRCH:
				slurm_seterrno(ESRCH_PRGSIGNAL);
				break;
			default:
				break;
		}
		return -1;
	}
	return 0;
}

#define _USE_ELAN3_CAPABILITY_STRING 1

#ifndef _USE_ELAN3_CAPABILITY_STRING
#define TRUNC_BITMAP 1
static void
_print_capbitmap(FILE *fp, ELAN_CAPABILITY *cap)
{
	int bit_max = sizeof(cap->Bitmap)*8 - 1;
	int bit;
#if TRUNC_BITMAP
	bit_max = bit_max >= 64 ? 64 : bit_max;
#endif
	for (bit = bit_max; bit >= 0; bit--)
		fprintf(fp, "%c", BT_TEST(cap->Bitmap, bit) ? '1' : '0');
	fprintf(fp, "\n");
}
#endif /* !_USE_ELAN3_CAPABILITY_STRING */

char *
qsw_capability_string(struct qsw_jobinfo *j, char *buf, size_t size)
{
	ELAN_CAPABILITY *cap;

	assert(buf != NULL);
	assert(j->j_magic == QSW_JOBINFO_MAGIC);

	cap = &j->j_cap;

#if HAVE_LIBELANCTRL
	snprintf(buf, size, "prg=%d ctx=%x.%x nodes=%d.%d",
	         j->j_prognum, cap->LowContext, cap->HighContext,
		 cap->LowNode, cap->HighNode);
#else
	snprintf(buf, size, "prg=%d ctx=%x.%x nodes=%d.%d entries=%d",
	         j->j_prognum, cap->LowContext, cap->HighContext,
		 cap->LowNode, cap->HighNode,
	         cap->Entries);
#endif

	return buf;
}

void
qsw_print_jobinfo(FILE *fp, struct qsw_jobinfo *jobinfo)
{
	ELAN_CAPABILITY *cap;
	char str[8192];

	assert(jobinfo->j_magic == QSW_JOBINFO_MAGIC);

	fprintf(fp, "__________________\n");
	fprintf(fp, "prognum=%d\n", jobinfo->j_prognum);

	cap = &jobinfo->j_cap;
	/* use elan3_capability_string as a shorter alternative for now */
#if _USE_ELAN3_CAPABILITY_STRING
#  if HAVE_LIBELANCTRL
	fprintf(fp, "%s\n", elan_capability_string(cap, str));
#  else
	fprintf(fp, "%s\n", elan3_capability_string(cap, str));
#  endif
#else
	fprintf(fp, "cap.UserKey=%8.8x.%8.8x.%8.8x.%8.8x\n",
			cap->UserKey.Values[0], cap->UserKey.Values[1],
			cap->UserKey.Values[2], cap->UserKey.Values[3]);
	/*fprintf(fp, "cap.Version=%d\n", cap->Version);*/
	fprintf(fp, "cap.Type=0x%hx\n", cap->Type);
	fprintf(fp, "cap.LowContext=%d\n", cap->LowContext);
	fprintf(fp, "cap.HighContext=%d\n", cap->HighContext);
	fprintf(fp, "cap.MyContext=%d\n", cap->MyContext);
	fprintf(fp, "cap.LowNode=%d\n", cap->LowNode);
	fprintf(fp, "cap.HighNode=%d\n", cap->HighNode);
#if HAVE_LIBELAN3
	fprintf(fp, "cap.padding=%hd\n", cap->padding);
	fprintf(fp, "cap.Entries=%d\n", cap->Entries);
#endif
	fprintf(fp, "cap.Railmask=0x%x\n", cap->RailMask);
	fprintf(fp, "cap.Bitmap=");
	_print_capbitmap(fp, cap);
#endif
	fprintf(fp, "\n------------------\n");
}
