#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <slurm/slurm_errno.h>

#include <src/common/log.h>
#include <src/common/pack.h>
#include <src/common/slurm_protocol_api.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/slurm_protocol_pack.h>
#include <src/common/xmalloc.h>

#include <check.h>


START_TEST(test_pack)
{
	buf_t *buffer;
	uint16_t test16 = 1234, out16;
	uint32_t test32 = 5678, out32, byte_cnt;
	char testbytes[] = "TEST BYTES", *outbytes;
	char teststring[] = "TEST STRING",  *outstring = NULL;
	char *nullstr = NULL;
	char *data;
	int data_size;
	long double test_double = 1340664754944.2132312, test_double2;
	uint64_t test64;

	buffer = init_buf (0);
        pack16(test16, buffer);
        pack32(test32, buffer);
	pack64((uint64_t)test_double, buffer);

        packstr(testbytes, buffer);
        packstr(teststring, buffer);
	packstr(nullstr, buffer);

	packstr("literal", buffer);
	packstr("", buffer);

        data_size = get_buf_offset(buffer);
        printf("wrote %d bytes\n", data_size);

	/* Pull data off old buffer, destroy it, and create a new one */
	data = xfer_buf_data(buffer);
	buffer = create_buf(data, data_size);

        unpack16(&out16, buffer);
	info("out16 =%d", out16);
	info("test16=%d", test16);
	ck_assert_msg(out16 == test16, "un/pack16");

        unpack32(&out32, buffer);
	ck_assert_msg(out32 == test32, "un/pack32");

  	unpack64(&test64, buffer);
	test_double2 = (long double)test64;
	ck_assert_msg((uint64_t)test_double2 == (uint64_t)test_double, "un/pack double as a uint64");
	/* info("Original\t %Lf", test_double); */
	/* info("uint64\t %ld", test64); */
	/* info("converted LD\t %Lf", test_double2); */

	unpackmem_ptr(&outbytes, &byte_cnt, buffer);
	ck_assert_msg( ( strcmp(testbytes, outbytes) == 0 ) , "un/packstr_ptr");

	unpackstr_xmalloc(&outstring, &byte_cnt, buffer);
	ck_assert_msg(strcmp(teststring, outstring) == 0, "un/packstr_xmalloc");
	xfree(outstring);

	unpackstr_xmalloc(&nullstr, &byte_cnt, buffer);
	ck_assert_msg(nullstr == NULL, "un/packstr of null string.");

	unpackstr_xmalloc(&outstring, &byte_cnt, buffer);
	ck_assert_msg(strcmp("literal", outstring) == 0,
			"un/packstr of string literal");
	xfree(outstring);

	unpackstr_xmalloc(&outstring, &byte_cnt, buffer);
	ck_assert_msg(strcmp("", outstring) == 0, "un/packstr of string \"\" ");

	xfree(outstring);
	free_buf(buffer);
}
END_TEST

START_TEST(test_packmem_array)
{
	char bytes[] = { 'a', '\0', 'b', '\0', 'c' };
	char *chunk = xmalloc(BUF_SIZE);
	buf_t *buf = init_buf(16);

	/* packmem_array() writes the bytes with no length prefix */
	ck_assert_int_eq(packmem_array(bytes, sizeof(bytes), buf),
			 SLURM_SUCCESS);
	ck_assert_int_eq(get_buf_offset(buf), sizeof(bytes));
	ck_assert_int_eq(size_buf(buf), 16);
	ck_assert_msg(!memcmp(get_buf_data(buf), bytes, sizeof(bytes)),
		      "packed bytes do not match the source");

	/* packing nothing is a no-op success */
	ck_assert_int_eq(packmem_array(bytes, 0, buf), SLURM_SUCCESS);
	ck_assert_int_eq(get_buf_offset(buf), sizeof(bytes));

	/* successive packs are contiguous */
	ck_assert_int_eq(packmem_array(bytes, sizeof(bytes), buf),
			 SLURM_SUCCESS);
	ck_assert_int_eq(get_buf_offset(buf), (2 * sizeof(bytes)));
	ck_assert_msg(!memcmp((get_buf_data(buf) + sizeof(bytes)), bytes,
			      sizeof(bytes)),
		      "second pack is not contiguous with the first");

	free_buf(buf);

	memset(chunk, 'a', BUF_SIZE);
	buf = init_buf(BUF_SIZE);

	/* an exact fit must not grow the buffer */
	ck_assert_int_eq(packmem_array(chunk, BUF_SIZE, buf), SLURM_SUCCESS);
	ck_assert_int_eq(size_buf(buf), BUF_SIZE);
	ck_assert_int_eq(remaining_buf(buf), 0);

	/* a full buffer must grow to take another byte */
	ck_assert_int_eq(packmem_array(chunk, 1, buf), SLURM_SUCCESS);
	ck_assert_int_eq(size_buf(buf), (2 * BUF_SIZE));
	ck_assert_int_eq(get_buf_offset(buf), (BUF_SIZE + 1));
	/* the contents must survive the implicit grow */
	ck_assert_msg(!memcmp(get_buf_data(buf), chunk, BUF_SIZE),
		      "packmem_array() did not preserve the contents");

	free_buf(buf);
	xfree(chunk);
}

END_TEST

START_TEST(test_packmem_array_rejected)
{
	char data[] = "shadowed";
	/* distinct from data so a write through it is detectable */
	char replacement[] = "X";
	/* buffer with no memory to avoid allocating MAX_BUF_SIZE bytes */
	buf_t full_buf = {
		.magic = BUF_MAGIC,
		.size = MAX_BUF_SIZE,
		.processed = MAX_BUF_SIZE,
	};
	buf_t stack_buf = SHADOW_BUF_INITIALIZER(data, (sizeof(data) - 1));
	buf_t *shadow_buf = create_shadow_buf(data, (sizeof(data) - 1));
	buf_t *buf = init_buf(16);

	/*
	 * A size past MAX_BUF_SIZE is rejected before valp or the buffer are
	 * touched, so NULL is safe here and nothing has to be allocated
	 */
	ck_assert_int_eq(packmem_array(NULL, (MAX_BUF_SIZE + 1), buf),
			 ESLURM_DATA_TOO_LARGE);
	/* a rejected pack must not advance the offset or grow the buffer */
	ck_assert_int_eq(get_buf_offset(buf), 0);
	ck_assert_int_eq(size_buf(buf), 16);

	/* a buffer that can not grow any further must report the failure */
	ck_assert_int_eq(packmem_array(replacement, 1, &full_buf),
			 ESLURM_DATA_TOO_LARGE);
	ck_assert_int_eq(get_buf_offset(&full_buf), MAX_BUF_SIZE);
	/* a rejected grow must not claim capacity the buffer does not have */
	ck_assert_int_eq(size_buf(&full_buf), MAX_BUF_SIZE);

	/*
	 * A buffer that does not own its memory must be rejected whether or
	 * not it still has room. try_grow_buf_remaining() only rejects it when
	 * it has to grow, so with room remaining only packmem_array()'s own
	 * check stops the memcpy() writing through to the shadowed memory
	 */
	ck_assert_int_eq(remaining_buf(&stack_buf), 0);
	ck_assert_int_eq(packmem_array(replacement, 1, &stack_buf), EINVAL);
	ck_assert_int_eq(get_buf_offset(&stack_buf), (sizeof(data) - 1));

	ck_assert_int_eq(remaining_buf(shadow_buf), (sizeof(data) - 1));
	ck_assert_int_eq(packmem_array(replacement, 1, shadow_buf), EINVAL);
	ck_assert_int_eq(get_buf_offset(shadow_buf), 0);
	/* the shadowed memory must be untouched, not overwritten with "X" */
	ck_assert_str_eq(data, "shadowed");

	free_buf(shadow_buf);
	free_buf(buf);

	{
		/*
		 * An mmap()ed buffer must be rejected the same way a shadow
		 * buffer is: room remains in the mapping, so only
		 * packmem_array()'s own mmaped check -- not
		 * try_grow_buf_remaining() -- stops the memcpy() writing
		 * through the PROT_READ mapping
		 */
		const char contents[] = "mmap()ed buffer contents";
		const size_t bytes = (sizeof(contents) - 1);
		char path[PATH_MAX];
		int fd;
		buf_t *mmap_buf;

		snprintf(path, sizeof(path), "%s/pack-test-XXXXXX",
			 (getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp"));
		fd = mkstemp(path);
		ck_assert_int_ge(fd, 0);
		ck_assert_int_eq(write(fd, contents, bytes), bytes);
		ck_assert_int_eq(close(fd), 0);

		mmap_buf = create_mmap_buf(path);
		/* the mapping outlives the file, so unlink before asserting */
		ck_assert_int_eq(unlink(path), 0);
		ck_assert(mmap_buf != NULL);
		ck_assert_int_eq(remaining_buf(mmap_buf), bytes);

		ck_assert_int_eq(packmem_array(replacement, 1, mmap_buf),
				 EINVAL);
		ck_assert_int_eq(get_buf_offset(mmap_buf), 0);
		ck_assert_msg(
			!memcmp(get_buf_data(mmap_buf), contents, bytes),
			"a rejected pack must not modify the mapped contents");

		free_buf(mmap_buf);
	}
}

END_TEST

START_TEST(test_pack_msg_buf_msg)
{
	char body[] = "packed job info";
	const uint32_t bytes = (sizeof(body) - 1);
	buf_t payload = SHADOW_BUF_INITIALIZER(body, bytes);
	/* buffer with no memory to avoid allocating MAX_BUF_SIZE bytes */
	buf_t full_buf = {
		.magic = BUF_MAGIC,
		.size = MAX_BUF_SIZE,
		.processed = MAX_BUF_SIZE,
	};
	buf_t *buf = init_buf(BUF_SIZE);
	slurm_msg_t msg;

	/*
	 * RESPONSE_JOB_INFO is one of the message types pack_msg() dispatches
	 * to _pack_buf_msg(), which is the only caller of packmem_array()
	 */
	slurm_msg_t_init(&msg);
	msg.msg_type = RESPONSE_JOB_INFO;
	msg.protocol_version = SLURM_PROTOCOL_VERSION;
	msg.data = &payload;

	/* the body is packed verbatim, with no length prefix */
	ck_assert_int_eq(pack_msg(&msg, buf), SLURM_SUCCESS);
	ck_assert_int_eq(get_buf_offset(buf), bytes);
	ck_assert_msg(!memcmp(get_buf_data(buf), body, bytes),
		      "pack_msg() did not pack the message body");

	/*
	 * A packmem_array() failure has to come back out of pack_msg() rather
	 * than being reported as a successfully packed but empty body
	 */
	ck_assert_int_eq(pack_msg(&msg, &full_buf), ESLURM_DATA_TOO_LARGE);
	ck_assert_int_eq(get_buf_offset(&full_buf), MAX_BUF_SIZE);

	free_buf(buf);
}

END_TEST

START_TEST(test_slurm_buffers_pack_msg)
{
	return_code_msg_t rc_msg = { .return_code = SLURM_SUCCESS };
	msg_bufs_t buffers = { 0 };
	slurm_msg_t msg;

	/*
	 * SLURM_NO_AUTH_CRED is what slurm_resp_msg_init() sets on every
	 * reply, so this is the shape the send path sees, and it skips the
	 * credential rather than needing the auth plugin layer up.
	 */
	slurm_msg_t_init(&msg);
	msg.protocol_version = SLURM_PROTOCOL_VERSION;
	msg.flags |= SLURM_NO_AUTH_CRED;
	slurm_msg_set_r_uid(&msg, SLURM_AUTH_UID_ANY);
	msg.msg_type = RESPONSE_SLURM_RC;
	msg.data = &rc_msg;

	/* a body that packs leaves the buffers ready to send */
	ck_assert_int_eq(slurm_buffers_pack_msg(&msg, &buffers, false),
			 SLURM_SUCCESS);
	ck_assert_msg((buffers.body != NULL), "no body was left to send");
	ck_assert_msg((buffers.header != NULL), "no header was left to send");
	FREE_NULL_BUFFER(buffers.header);
	FREE_NULL_BUFFER(buffers.auth);
	FREE_NULL_BUFFER(buffers.body);

	/*
	 * pack_msg() has no method for NO_VAL16, so the body can not be
	 * packed. The send has to fail rather than hand the caller a header
	 * stamped over a body that was never written, and it reports that as
	 * -1 with errno set because slurm_send_node_msg() returns this value
	 * as a byte count.
	 */
	memset(&buffers, 0, sizeof(buffers));
	msg.msg_type = NO_VAL16;
	msg.data = NULL;

	ck_assert_msg((slurm_buffers_pack_msg(&msg, &buffers, false) < 0),
		      "the send did not fail");
	ck_assert_int_eq(errno, SLURM_COMMUNICATIONS_SEND_ERROR);
	/* nothing may be left for the caller to send */
	ck_assert_msg((buffers.body == NULL), "a body was left to send");
	ck_assert_msg((buffers.header == NULL), "a header was left to send");
}

END_TEST

int main(void)
{
	int number_failed;

	log_options_t log_opts = LOG_OPTS_INITIALIZER;
	log_opts.stderr_level = LOG_LEVEL_DEBUG5;
	log_init("pack-test", log_opts, 0, NULL);

	Suite *s = suite_create("pack");
	TCase *tc_core = tcase_create("pack");

	tcase_add_test(tc_core, test_pack);
	tcase_add_test(tc_core, test_packmem_array);
	tcase_add_test(tc_core, test_packmem_array_rejected);
	tcase_add_test(tc_core, test_pack_msg_buf_msg);
	tcase_add_test(tc_core, test_slurm_buffers_pack_msg);

	suite_add_tcase(s, tc_core);

	SRunner *sr = srunner_create(s);

	srunner_run_all(sr, CK_ENV);
	number_failed = srunner_ntests_failed(sr);
	srunner_free(sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
