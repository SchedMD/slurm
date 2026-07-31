/*****************************************************************************\
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

#include <check.h>
#include <signal.h>
#include <stdlib.h>

#include "src/common/slurm_protocol_defs.h"

START_TEST(exited_zero)
{
	uint16_t status = NO_VAL16, sig = NO_VAL16;

	exit_code_decode(0, &status, &sig);
	ck_assert_int_eq(status, 0);
	ck_assert_int_eq(sig, 0);
}

END_TEST

START_TEST(exited_nonzero)
{
	uint16_t status = NO_VAL16, sig = NO_VAL16;

	/* wait() encodes a plain exit status in the high byte. */
	exit_code_decode(7 << 8, &status, &sig);
	ck_assert_int_eq(status, 7);
	ck_assert_int_eq(sig, 0);
}

END_TEST

START_TEST(signaled)
{
	uint16_t status = NO_VAL16, sig = NO_VAL16;

	exit_code_decode(SIGKILL, &status, &sig);
	ck_assert_int_eq(status, 0);
	ck_assert_int_eq(sig, SIGKILL);
}

END_TEST

START_TEST(signaled_with_core_dump)
{
	uint16_t status = NO_VAL16, sig = NO_VAL16;

	/* 0x80 is the core-dump flag; it must not leak into the signal. */
	exit_code_decode(SIGSEGV | 0x80, &status, &sig);
	ck_assert_int_eq(status, 0);
	ck_assert_int_eq(sig, SIGSEGV);
}

END_TEST

START_TEST(signal_ignores_high_byte)
{
	uint16_t status = NO_VAL16, sig = NO_VAL16;

	/*
	 * A signal encoding whose high byte is also set must report the
	 * signal with exit_status left at 0; the high byte must not leak
	 * into exit_status.
	 */
	exit_code_decode((7 << 8) | SIGKILL, &status, &sig);
	ck_assert_int_eq(status, 0);
	ck_assert_int_eq(sig, SIGKILL);
}

END_TEST

START_TEST(no_val_decodes_to_zeros)
{
	uint16_t status = NO_VAL16, sig = NO_VAL16;

	/*
	 * The raw macros would report 0:126 here: NO_VAL's low byte (0xfe)
	 * makes WIFSIGNALED true, so WTERMSIG yields 126. The 0:0 below comes
	 * from the explicit NO_VAL guard in exit_code_decode(). Do not drop
	 * that guard.
	 */
	exit_code_decode(NO_VAL, &status, &sig);
	ck_assert_int_eq(status, 0);
	ck_assert_int_eq(sig, 0);
}

END_TEST

START_TEST(null_outputs_are_optional)
{
	uint16_t status = NO_VAL16, sig = NO_VAL16;

	exit_code_decode(SIGTERM, &status, NULL);
	ck_assert_int_eq(status, 0);

	exit_code_decode(3 << 8, NULL, &sig);
	ck_assert_int_eq(sig, 0);

	/* Must not crash with both outputs dropped. */
	exit_code_decode(0, NULL, NULL);
}

END_TEST

Suite *suite(SRunner *sr)
{
	Suite *s = suite_create("exit_code_decode");
	TCase *tc_core = tcase_create("exit_code_decode");
	tcase_add_test(tc_core, exited_zero);
	tcase_add_test(tc_core, exited_nonzero);
	tcase_add_test(tc_core, signaled);
	tcase_add_test(tc_core, signaled_with_core_dump);
	tcase_add_test(tc_core, signal_ignores_high_byte);
	tcase_add_test(tc_core, no_val_decodes_to_zeros);
	tcase_add_test(tc_core, null_outputs_are_optional);
	suite_add_tcase(s, tc_core);
	return s;
}

/*****************************************************************************
 * TEST RUNNER                                                               *
 ****************************************************************************/

int main(void)
{
	int number_failed;
	SRunner *sr = srunner_create(NULL);

	srunner_add_suite(sr, suite(sr));

	srunner_run_all(sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return number_failed ? EXIT_FAILURE : EXIT_SUCCESS;
}
