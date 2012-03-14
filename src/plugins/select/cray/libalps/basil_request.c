/*
 * Fork apbasil process as co-process, parse output.
 *
 * Copyright (c) 2009-2011 Centro Svizzero di Calcolo Scientifico (CSCS)
 * Portions Copyright (C) 2011 SchedMD <http://www.schedmd.com>.
 * Licensed under the GPLv2.
 */
#include "parser_internal.h"
#include <stdarg.h>

int   log_sel = -1;
char *xml_log_loc = NULL;
char  xml_log_file_name[256] = "";

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
static int _write_xml(FILE* fp, const char* format, ...) {
	char buff[1024];
	va_list ap;
	int rc;
	FILE* fplog = NULL;

	/* Write to ALPS BASIL itself as we would have done without logging. */
	va_start(ap, format);
	vsnprintf(buff, sizeof(buff), format, ap);
	va_end(ap);
	rc = fprintf(fp, "%s", buff);
	if (log_sel < 1)
		return rc;

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

static void _rsvn_write_reserve_xml(FILE *fp, struct basil_reservation *r)
{
	struct basil_rsvn_param *param;

	_write_xml(fp, " <ReserveParamArray user_name=\"%s\"", r->user_name);
	if (*r->batch_id != '\0')
		_write_xml(fp, " batch_id=\"%s\"", r->batch_id);
	if (*r->account_name != '\0')
		_write_xml(fp, " account_name=\"%s\"", r->account_name);
	_write_xml(fp, ">\n");

	for (param = r->params; param; param = param->next) {
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
					   " type=\"%s\" disposition=\"%s\"/>\n",
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

/*
 * basil_request - issue BASIL request and parse response
 * @bp:	method-dependent parse data to guide the parsing process
 *
 * Returns 0 if ok, a negative %basil_error otherwise.
 */
int basil_request(struct basil_parse_data *bp)
{
	int to_child, from_child;
	int ec, rc = -BE_UNKNOWN;
	FILE *apbasil;
	pid_t pid;

	if (log_sel == -1)
		_init_log_config();

	if (!cray_conf->apbasil) {
		error("No alps client defined");
		return 0;
	}
	assert(bp->version < BV_MAX);
	assert(bp->method > BM_none && bp->method < BM_MAX);

	pid = popen2(cray_conf->apbasil, &to_child, &from_child, true);
	if (pid < 0)
		fatal("popen2(\"%s\", ...)", cray_conf->apbasil);

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
		_rsvn_write_reserve_xml(apbasil, bp->mdata.res);
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
	if (ec) {
		error("%s child process for BASIL %s method exited with %d",
		      cray_conf->apbasil, bm_names[bp->method], ec);
	}

	return rc;
}
