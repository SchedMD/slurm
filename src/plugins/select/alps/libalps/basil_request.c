/*
 * Fork apbasil process as co-process, parse output.
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Portions Copyright (C) 2011 SchedMD <http://www.schedmd.com>.
 * Licensed under the GPLv2.
 */
#include "../basil_interface.h"
#include "parser_internal.h"
#include <stdarg.h>
#include <unistd.h>

int   log_sel = -1;
char *xml_log_loc = NULL;
char  xml_log_file_name[256] = "";

pthread_cond_t  timer_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * Function: _write_xml
 * Purpose:  Intercepts SLURM's ALPS BASIL XML requests so that it can
 *           logged it as well as pass to ALPS BASIL.
 * Use:  Logging is controlled by environmental variables:
 *       0) XML_LOG set to enable logging
 *       1) XML_LOG_LOC not set   => Log to generic "slurm_basil_xml.log" file
 *       2) XML_LOG_LOC="SLURM"   => Log to the common slurmctld.log file
 *       3) XML_LOG_LOC=<path>    => Log to file specified by the path here
 *
 * Note: Any change in environmental variables requires re-start of slurmctld
 * to take effect.
 */
static int _write_xml(FILE* fp, const char* format, ...)
{
	static int buff_len = 512 * 1024;
	char *buff = NULL;
	va_list ap;
	int rc;
	FILE* fplog = NULL;

	/* Write to ALPS BASIL itself as we would have done without logging. */
	buff = xmalloc(buff_len);
	va_start(ap, format);
	while ((rc = vsnprintf(buff, buff_len, format, ap)) >= buff_len) {
		buff_len = rc + 1024;
		xfree(buff);
		buff = xmalloc(buff_len);
	}
	va_end(ap);
	rc = fprintf(fp, "%s", buff);

	if (log_sel >= 1) {
		/* Perform the appropriate logging. */
		if (xml_log_file_name[0] != '\0') {
			/* If we have a specific file name, try to open it. */
			fplog = fopen(xml_log_file_name, "a+");
			if (fplog == NULL) {
				error("Problem with fdopen() of %s: %m",
				      xml_log_file_name);
			}
		}
		if (fplog) {
			fprintf(fplog, "%s", buff);
			fclose(fplog);
		} else
			info("%s", buff);
	}
	xfree(buff);

	return rc;
}

static void _init_log_config(void)
{
	if (getenv("XML_LOG"))
		log_sel = 1;
	else
		log_sel = 0;
	xml_log_loc = getenv("XML_LOG_LOC");
	if (xml_log_loc && strcmp(xml_log_loc, "SLURM") &&
	    (strlen(xml_log_loc) < sizeof(xml_log_file_name))) {
		strcpy(xml_log_file_name, xml_log_loc);
	} else {
		sprintf(xml_log_file_name, "slurm_basil_xml.log");
	}
}

static void _rsvn_write_reserve_xml(FILE *fp, struct basil_reservation *r,
				    enum basil_version version)
{
	struct basil_rsvn_param *param;

	_write_xml(fp, " <ReserveParamArray user_name=\"%s\"", r->user_name);
	if (*r->batch_id != '\0')
		_write_xml(fp, " batch_id=\"%s\"", r->batch_id);
	if (*r->account_name != '\0')
		_write_xml(fp, " account_name=\"%s\"", r->account_name);
	_write_xml(fp, ">\n");

	for (param = r->params; param; param = param->next) {
		if (version >= BV_5_1)
			_write_xml(fp, "  <ReserveParam architecture=\"%s\" "
				   "width=\"%ld\" depth=\"%ld\" nppn=\"%ld\""
				   " nppcu=\"%d\"",
				   nam_arch[param->arch], param->width,
				   param->depth, param->nppn, param->nppcu);
		else
			_write_xml(fp, "  <ReserveParam architecture=\"%s\" "
				   "width=\"%ld\" depth=\"%ld\" nppn=\"%ld\"",
				   nam_arch[param->arch],
				   param->width, param->depth, param->nppn);

		if (param->memory || param->labels ||
		    param->nodes  || param->accel) {
			_write_xml(fp, ">\n");
		} else {
			_write_xml(fp, "/>\n");
			continue;
		}

		if (param->memory) {
			struct basil_memory_param  *mem;

			_write_xml(fp, "   <MemoryParamArray>\n");
			for (mem = param->memory; mem; mem = mem->next) {
				_write_xml(fp, "    <MemoryParam type=\"%s\""
					   " size_mb=\"%u\"/>\n",
					   nam_memtype[mem->type],
					   mem->size_mb ? : 1);
			}
			_write_xml(fp, "   </MemoryParamArray>\n");
		}

		if (param->labels) {
			struct basil_label *label;

			_write_xml(fp, "   <LabelParamArray>\n");
			for (label = param->labels; label; label = label->next)
				_write_xml(fp, "    <LabelParam name=\"%s\""
					   " type=\"%s\" "
					   "disposition=\"%s\"/>\n",
					   label->name,
					   nam_labeltype[label->type],
					   nam_ldisp[label->disp]);

			_write_xml(fp, "   </LabelParamArray>\n");
		}

		if (param->nodes && *param->nodes) {
			/*
			 * The NodeParamArray is declared within ReserveParam.
			 * If the list is spread out over multiple NodeParam
			 * elements, an
			 *   "at least one command's user NID list is short"
			 * error results. Hence more than 1 NodeParam element
			 * is probably only meant to be used when suggesting
			 * alternative node lists to ALPS. This was confirmed
			 * by repeating an identical same NodeParam 20 times,
			 * which had the same effect as supplying it once.
			 * Hence the array expression is actually not needed.
			 */
			_write_xml(fp, "   <NodeParamArray>\n"
				   "    <NodeParam>%s</NodeParam>\n"
				   "   </NodeParamArray>\n", param->nodes);
		}

		if (param->accel) {
			struct basil_accel_param *accel;

			_write_xml(fp, "   <AccelParamArray>\n");
			for (accel = param->accel; accel; accel = accel->next) {
				_write_xml(fp, "    <AccelParam type=\"%s\"",
					   nam_acceltype[accel->type]);

				if (accel->memory_mb)
					_write_xml(fp, " memory_mb=\"%u\"",
						   accel->memory_mb);
				_write_xml(fp, "/>\n");
			}
			_write_xml(fp, "   </AccelParamArray>\n");
		}

		_write_xml(fp, "  </ReserveParam>\n");
	}
	_write_xml(fp, " </ReserveParamArray>\n"
		   "</BasilRequest>\n");
}

static void *_timer_func(void *raw_data)
{
	pid_t child = *(pid_t *)raw_data;
	int time_out;
	struct timespec ts;
	struct timeval now;

	time_out = cray_conf->apbasil_timeout;
	debug2("This is a timer thread for process: %d (slurmctld)--"
	       "timeout: %d, apbasil pid: %d\n", getpid(), time_out, child);

	pthread_mutex_lock(&timer_lock);
	gettimeofday(&now, NULL);
	ts.tv_sec = now.tv_sec + time_out;
	ts.tv_nsec = now.tv_usec * 1000;
	if (pthread_cond_timedwait(&timer_cond, &timer_lock, &ts) == ETIMEDOUT){
		info("Apbasil taking too long--terminating apbasil pid: %d",
		     child);
		kill(child, SIGKILL);
		debug2("Exiting timer thread, apbasil pid had been: %d", child);
	}
	pthread_mutex_unlock(&timer_lock);
	pthread_exit(NULL);
}

/*
 * basil_request - issue BASIL request and parse response
 * @bp:	method-dependent parse data to guide the parsing process
 *
 * Returns 0 if ok, a negative %basil_error otherwise.
 */
int basil_request(struct basil_parse_data *bp)
{
	int to_child, from_child;
	int ec, i, rc = -BE_UNKNOWN;
	FILE *apbasil;
	pid_t pid = -1;
	pthread_t thread;
	pthread_attr_t attr;
	int time_it_out = 1;
	DEF_TIMERS;

	if (log_sel == -1)
		_init_log_config();

	if (!cray_conf->apbasil) {
		error("No alps client defined");
		return 0;
	}

	if ((cray_conf->apbasil_timeout == 0) ||
	    (cray_conf->apbasil_timeout == (uint16_t) NO_VAL)) {
		debug2("No ApbasilTimeout configured (%u)",
		       cray_conf->apbasil_timeout);
		time_it_out = 0;
	} else {
		slurm_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	}

	assert(bp->version < BV_MAX);
	assert(bp->method > BM_none && bp->method < BM_MAX);

	START_TIMER;
	for (i = 0; ((i < 10) && (pid < 0)); i++) {
		if (i)
			usleep(100000);
		pid = popen2(cray_conf->apbasil, &to_child, &from_child, true);
	}
	if (pid < 0)
		fatal("popen2(\"%s\", ...)", cray_conf->apbasil);

	if (time_it_out) {
		pthread_create(&thread, &attr, _timer_func, (void*)&pid);
	}

	/* write out request */
	apbasil = fdopen(to_child, "w");
	if (apbasil == NULL)
		fatal("fdopen(): %s", strerror(errno));
	setlinebuf(apbasil);

	_write_xml(apbasil, "<?xml version=\"1.0\"?>\n"
		   "<BasilRequest protocol=\"%s\" method=\"%s\" ",
		   bv_names[bp->version], bm_names[bp->method]);

	switch (bp->method) {
	case BM_engine:
		_write_xml(apbasil, "type=\"ENGINE\"/>");
		break;
	case BM_inventory:
		_write_xml(apbasil, "type=\"INVENTORY\"/>");
		break;
	case BM_reserve:
		_write_xml(apbasil, ">\n");
		_rsvn_write_reserve_xml(apbasil, bp->mdata.res, bp->version);
		break;
	case BM_confirm:
		if (bp->version == BV_1_0 && *bp->mdata.res->batch_id != '\0')
			_write_xml(apbasil, "job_name=\"%s\" ",
				   bp->mdata.res->batch_id);
		_write_xml(apbasil, "reservation_id=\"%u\" %s=\"%llu\"/>\n",
			   bp->mdata.res->rsvn_id,
			   bp->version >= BV_3_1 ? "pagg_id" : "admin_cookie",
			   (unsigned long long)bp->mdata.res->pagg_id);
		break;
	case BM_release:
		_write_xml(apbasil, "reservation_id=\"%u\"/>\n",
			   bp->mdata.res->rsvn_id);
		break;
	case BM_switch:
	{
		char *suspend = bp->mdata.res->suspended ? "OUT" : "IN";
		_write_xml(apbasil, ">\n");
		_write_xml(apbasil, " <ReservationArray>\n");
		_write_xml(apbasil, "  <Reservation reservation_id=\"%u\" "
			   "action=\"%s\"/>\n",
			   bp->mdata.res->rsvn_id, suspend);
		_write_xml(apbasil, " </ReservationArray>\n");
		_write_xml(apbasil, "</BasilRequest>\n");
	}
		break;
	default: /* ignore BM_none, BM_MAX, and BM_UNKNOWN covered above */
		break;
	}

	if (fclose(apbasil) < 0)	/* also closes to_child */
		error("fclose(apbasil): %s", strerror(errno));

	rc = parse_basil(bp, from_child);
	ec = wait_for_child(pid);

	if (time_it_out) {
		slurm_attr_destroy(&attr);
		debug2("Killing the timer thread.");
		pthread_mutex_lock(&timer_lock);
		pthread_cond_broadcast(&timer_cond);
		pthread_mutex_unlock(&timer_lock);
	}

	END_TIMER;
	if (ec) {
		error("%s child process for BASIL %s method exited with %d",
		      cray_conf->apbasil, bm_names[bp->method], ec);
	} else if (DELTA_TIMER > 5000000) {	/* 5 seconds limit */
		info("%s child process for BASIL %s method time %s",
		     cray_conf->apbasil, bm_names[bp->method], TIME_STR);
	}

	return rc;
}
