/*****************************************************************************\
 *  update_tres.c - TRES drain/resume update function for scontrol.
 *****************************************************************************
 *  Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "slurm/slurm.h"

#include "src/common/proc_args.h"

#include "src/scontrol/scontrol.h"

/*
 * Node-local TRES live on a node and so must name one. Cluster-scoped TRES
 * (licenses) do not, so key the requirement off the TRES rather than making
 * NodeName unconditionally mandatory. Cluster scope then needs no new rule
 * here when it arrives.
 */
static bool _tres_is_node_local(char *tres_name)
{
	return (!xstrcasecmp(tres_name, "cpu") ||
		!xstrcasecmp(tres_name, "mem") ||
		!xstrcasecmp(tres_name, "memory") ||
		!xstrncasecmp(tres_name, "gres/", 5));
}

/*
 * Every selector (Index, Uuid, Count) accepts ALL, which selects everything
 * TresName covers on each named node.
 *
 * Record that once here so each selector only has to handle its own value form.
 * RET true if val was ALL, and so no selector-specific value remains to parse
 */
static bool _selector_is_all(char *val, update_tres_msg_t *tres_msg)
{
	if (xstrcasecmp(val, "ALL"))
		return false;

	tres_msg->select_all = 1;

	return true;
}

extern int scontrol_update_tres(int argc, char **argv)
{
	update_tres_msg_t tres_msg;
	char *reason_str = NULL;
	char *tag, *val;
	int tag_len;
	int rc = 0;
	int sel_cnt;
	bool index_given = false, uuid_given = false, count_given = false;
	bool action_given = false;
	bool name_given = false, node_given = false, reason_given = false;

	slurm_init_update_tres_msg(&tres_msg);

	/*
	 * TresName is the entity, so the remaining fields need no prefix to
	 * say what they qualify: Index, Uuid and Count select TRES of it, and
	 * Action says what to do with them. Everything the controller would
	 * reject is caught here so the admin gets a specific message rather
	 * than a generic error after a round trip.
	 */
	for (int i = 0; i < argc; i++) {
		tag = argv[i];
		val = xstrchr(argv[i], '=');
		if (val) {
			tag_len = val - argv[i];
			val++;
		} else {
			exit_code = 1;
			error("Invalid input: %s  Request aborted", argv[i]);
			goto done;
		}

		if (!xstrncasecmp(tag, "TresName", MAX(tag_len, 4))) {
			if (name_given)
				goto repeated;
			name_given = true;
			tres_msg.name = val;
		} else if (!xstrncasecmp(tag, "NodeName", MAX(tag_len, 1))) {
			if (node_given)
				goto repeated;
			node_given = true;
			tres_msg.node_names = val;
		} else if (!xstrncasecmp(tag, "Index", MAX(tag_len, 1))) {
			if (index_given)
				goto repeated;
			if (!val[0])
				goto no_value;
			index_given = true;
			if (!_selector_is_all(val, &tres_msg))
				tres_msg.index = val;
		} else if (!xstrncasecmp(tag, "Uuid", MAX(tag_len, 1))) {
			if (uuid_given)
				goto repeated;
			if (!val[0])
				goto no_value;
			uuid_given = true;
			if (!_selector_is_all(val, &tres_msg))
				tres_msg.uuid = val;
		} else if (!xstrncasecmp(tag, "Count", MAX(tag_len, 1))) {
			if (count_given)
				goto repeated;
			if (!val[0])
				goto no_value;
			count_given = true;
			if (!_selector_is_all(val, &tres_msg) &&
			    parse_uint64(val, &tres_msg.count)) {
				exit_code = 1;
				error("Invalid value %s for Count", argv[i]);
				goto done;
			}
		} else if (!xstrncasecmp(tag, "Action", MAX(tag_len, 1))) {
			if (action_given)
				goto repeated;
			if (!xstrcasecmp(val, "SetDrain")) {
				tres_msg.action = UPDATE_SET;
			} else if (!xstrcasecmp(val, "Drain")) {
				tres_msg.action = UPDATE_ADD;
			} else if (!xstrcasecmp(val, "Resume")) {
				tres_msg.action = UPDATE_REMOVE;
			} else {
				exit_code = 1;
				fprintf(stderr,
					"Invalid Action: %s\nValid actions are: Drain Resume SetDrain\n",
					val);
				goto done;
			}
			action_given = true;
		} else if (!xstrncasecmp(tag, "Reason", MAX(tag_len, 1))) {
			if (reason_given)
				goto repeated;
			reason_given = true;
			reason_str = scontrol_strip_quotes(val);
			tres_msg.reason = reason_str;
		} else {
			exit_code = 1;
			fprintf(stderr,
				"Update of this parameter is not supported: %s\n",
				argv[i]);
			fprintf(stderr, "Request aborted\n");
			goto done;
		}
	}

	if (!tres_msg.name || !tres_msg.name[0]) {
		exit_code = 1;
		fprintf(stderr, "TresName must be specified. Request denied\n");
		goto done;
	}
	if (_tres_is_node_local(tres_msg.name) && !tres_msg.node_names) {
		exit_code = 1;
		fprintf(stderr,
			"TresName=%s requires NodeName. Request denied\n",
			tres_msg.name);
		goto done;
	}
	sel_cnt = index_given + uuid_given + count_given;
	if (!sel_cnt) {
		exit_code = 1;
		fprintf(stderr,
			"TresName requires Index, Uuid or Count. Request denied\n");
		goto done;
	}
	/*
	 * Index and Uuid may be given together, naming one device two ways, so
	 * that a line from "scontrol show drainedtres" can be handed back as-is;
	 * the controller refuses the pair if they disagree. Count measures
	 * rather than names, so it cannot be combined with either, and ALL
	 * already covers everything TresName names, which leaves nothing for
	 * another selector to add.
	 */
	if (tres_msg.select_all && (sel_cnt > 1)) {
		exit_code = 1;
		fprintf(stderr,
			"ALL already selects everything TresName covers and cannot be combined with another selector. Request denied\n");
		goto done;
	}
	if (count_given && (sel_cnt > 1)) {
		exit_code = 1;
		fprintf(stderr,
			"Count cannot be combined with Index or Uuid. Request denied\n");
		goto done;
	}
	if (!action_given) {
		exit_code = 1;
		fprintf(stderr,
			"TresName requires Action=Drain, Resume or SetDrain. Request denied\n");
		goto done;
	}
	/*
	 * Draining needs a reason; resuming has nothing to record one against,
	 * so silently accepting one there would mislead.
	 */
	if (tres_msg.action == UPDATE_REMOVE) {
		if (tres_msg.reason && tres_msg.reason[0]) {
			exit_code = 1;
			fprintf(stderr,
				"Reason is not used with Action=Resume. Request denied\n");
			goto done;
		}
	} else if (!tres_msg.reason || !tres_msg.reason[0]) {
		exit_code = 1;
		fprintf(stderr,
			"You must specify a reason when draining TRES. Request denied\n");
		goto done;
	}

	rc = slurm_update_tres(&tres_msg);
	goto done;

no_value:
	/* An empty selector is most likely an unset shell variable. */
	exit_code = 1;
	fprintf(stderr, "%.*s requires a value. Request denied\n", tag_len,
		tag);
	goto done;

repeated:
	exit_code = 1;
	fprintf(stderr, "%.*s specified more than once. Request denied\n",
		tag_len, tag);

done:
	xfree(reason_str);
	if (rc) {
		exit_code = 1;
		return errno;
	}

	return 0;
}
