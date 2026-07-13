############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test slurmrestd's http_parser plugin.

Exercises valid, malformed, binary, and pipelined HTTP requests against the
http_parser plugin slurmrestd is configured to use (Issue 50084).
"""

import base64
import re

import pytest

import atf


@pytest.fixture(scope="module", autouse=True)
def setup():
    atf.require_version(
        (26, 5),
        "sbin/slurmrestd",
        reason="Issue 50084: HttpParserType/UrlParserType configuration requires 25.11+ and patches for some tests only backported to 26.05+",
    )
    # A pinned parser cannot fall back: if a variant selects llhttp_parser on a
    # slurmrestd that predates the plugin, http_parser_g_init() fails and
    # slurmrestd exits fatally, so skip rather than reporting every case as a
    # load failure.
    if "llhttp" in atf.get_config_parameter("HttpParserType", default="", live=False):
        atf.require_version(
            (26, 11),
            "sbin/slurmrestd",
            reason="Issue 50084: http_parser/llhttp_parser plugin requires 26.11+",
        )
    atf.require_slurm_running()


@pytest.fixture
def http_parser():
    """Return the http_parser plugin slurmrestd is configured to use.

    The test environment pins HttpParserType per variant, so tests read the
    active parser instead of forcing one. Only llhttp_parser (26.11+) defines
    ESLURM_HTTP_MISSING_CR.
    """

    return atf.get_config_parameter(
        "HttpParserType", default="http_parser/libhttp_parser", live=False
    )


# expected_error is the slurmrestd error message expected in the rejection
# response body, or None where the exact error classification is not reliably
# predictable (bad version, truncated request). llhttp_only cases exercise an
# error code that only exists in llhttp_parser.
@pytest.mark.parametrize(
    "payload,expected_error,llhttp_only",
    [
        pytest.param(
            "GET /openapi HTTP/1.1\r\nHost: x\n\r\n",
            "Missing an expected CR character",
            True,
            id="missing_cr",
        ),
        pytest.param(
            "GET /openapi HTTP/1.1\r\nHost: x\rY\r\n\r\n",
            "Missing an expected LF character",
            False,
            id="missing_lf",
        ),
        pytest.param(
            "GERP / HTTP/1.1\r\n\r\n",
            "Invalid HTTP method",
            False,
            id="invalid_method_chars",
        ),
        pytest.param(
            "get / HTTP/1.1\r\n\r\n",
            "Invalid HTTP method",
            False,
            id="lowercase_method",
        ),
        pytest.param("GET / HTTP/abc\r\n\r\n", None, False, id="bad_version_syntax"),
        pytest.param("GET / HTTP/9.9\r\n\r\n", None, False, id="bad_version_number"),
        pytest.param(
            "POST / HTTP/1.1\r\nContent-Length: abc\r\n\r\n",
            "Unexpected character or unparsable number in HTTP Content-Length header",
            False,
            id="bad_content_length",
        ),
        pytest.param(
            "POST / HTTP/1.1\r\nContent-Length: -1\r\n\r\n",
            "Unexpected character or unparsable number in HTTP Content-Length header",
            False,
            id="negative_content_length",
        ),
        pytest.param(
            "GET / HTTP/1.1\r\nX\x00Y: v\r\n\r\n",
            "Unexpected character added or missing in HTTP",
            False,
            id="null_in_header_name",
        ),
        pytest.param(
            "GET / HTTP/1.1\r\n",
            None,
            False,
            id="incomplete_request_eof",
            marks=pytest.mark.xfail(
                reason="Issue 51064: slurmrestd does not run llhttp_finish at "
                "read-EOF, so a truncated request is silently accepted (exit 0)",
            ),
        ),
    ],
)
def test_bad_input(http_parser, payload, expected_error, llhttp_only):
    """Verify slurmrestd rejects malformed HTTP requests."""

    if llhttp_only and "llhttp" not in http_parser:
        pytest.skip("Issue 50084: libhttp_parser has no ESLURM_HTTP_MISSING_CR")

    result = atf.run_command("slurmrestd -a rest_auth/local", input=payload, xfail=True)
    assert (
        result["exit_code"] != 0
    ), f"slurmrestd should have rejected malformed request: {payload!r}"
    # A reintroduced xassert abort (the cleanup crash these cases guard
    # against) logs an assertion failure to stderr before SIGABRT; the exit
    # code alone cannot tell a crash from a clean rejection here.
    assert (
        "Assertion" not in result["stderr"]
    ), f"slurmrestd aborted instead of cleanly rejecting {payload!r}: {result['stderr']!r}"
    if expected_error is not None:
        assert expected_error in result["stdout"], (
            f"slurmrestd rejected {payload!r} but the response did not cite the "
            f"expected error {expected_error!r}; stdout: {result['stdout']!r}"
        )


@pytest.mark.parametrize(
    "payload",
    [
        pytest.param(b"\x00\x01\x02\xff\xfe\xfd\x7f\x80\x81\x82", id="binary_only"),
        pytest.param(b"G\xffT /openapi HTTP/1.1\r\n\r\n", id="binary_in_method"),
        pytest.param(b"GET /open\x00api HTTP/1.1\r\n\r\n", id="binary_in_url"),
        pytest.param(b"GET /openapi HTTP/1.\xff\r\n\r\n", id="binary_in_version"),
        pytest.param(
            b"GET /openapi HTTP/1.1\r\n\x01Foo: bar\r\n\r\n",
            id="binary_in_header_name",
        ),
        pytest.param(
            b"GET /openapi HTTP/1.1\r\nFoo: ba\x01r\r\n\r\n",
            id="binary_in_header_value",
        ),
        pytest.param(
            b"GET /openapi HTTP/1.1\r\nFoo: bar\r\n\xff\xfe\r\n\r\n",
            id="binary_between_headers",
        ),
        pytest.param(
            b"GET /openapi HTTP/1.1\r\n\r\n\xde\xad\xbe\xef",
            id="binary_after_headers_no_clen",
        ),
        pytest.param(
            b"GET /openapi HTTP/1.1\r\nHost: x\r\n\r\n"
            b"\xff\xfe\xfd"
            b"GET /openapi HTTP/1.1\r\n\r\n",
            id="binary_between_pipelined",
        ),
    ],
)
def test_bad_binary_input(payload):
    """Verify slurmrestd rejects requests containing arbitrary binary bytes.

    Binary payloads cannot round-trip through atf's text-mode subprocess
    pipe, so we base64-encode them and decode in the shell into slurmrestd's
    stdin. base64 is ASCII-safe and the decode happens in the same shell that
    runs slurmrestd, so the bytes survive intact even when the command runs as
    the test user.
    """

    b64 = base64.b64encode(payload).decode("ascii")
    result = atf.run_command(
        f"echo {b64} | base64 -d | slurmrestd -a rest_auth/local", xfail=True
    )
    assert (
        result["exit_code"] != 0
    ), f"slurmrestd should have rejected binary payload: {payload!r}"
    assert (
        "Assertion" not in result["stderr"]
    ), f"slurmrestd aborted instead of cleanly rejecting {payload!r}: {result['stderr']!r}"


@pytest.mark.parametrize(
    "payload",
    [
        pytest.param(
            "GET /openapi HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            id="get_with_host",
        ),
        pytest.param("GET /openapi HTTP/1.0\r\n\r\n", id="get_http_1_0"),
        pytest.param(
            "GET /openapi?key=val HTTP/1.1\r\nConnection: close\r\n\r\n",
            id="get_with_query",
        ),
        pytest.param("GET /openapi HTTP/1.1\r\n\r\n", id="get_no_headers"),
    ],
)
def test_valid_input(payload):
    """Verify slurmrestd accepts simple well-formed HTTP requests."""

    output = atf.run_command_output(
        "slurmrestd -a rest_auth/local", input=payload, fatal=True
    )
    assert any(
        line.startswith("HTTP/") and " 200 " in line for line in output.splitlines()
    ), f"slurmrestd should have returned an HTTP 200 for well-formed request {payload!r}, got stdout: {output!r}"


def test_keep_alive_two_requests():
    """Verify slurmrestd handles two pipelined requests on one connection.

    Without an explicit Connection: close, HTTP/1.1 keeps the connection
    open until stdin EOF, so slurmrestd must parse both messages and emit
    two responses.
    """

    payload = (
        "GET /openapi HTTP/1.1\r\nHost: localhost\r\n\r\n"
        "GET /openapi HTTP/1.1\r\nHost: localhost\r\n\r\n"
    )
    output = atf.run_command_output(
        "slurmrestd -a rest_auth/local", input=payload, fatal=True
    )
    # Pipelined responses are concatenated with no separator, so the second
    # status line is not newline-anchored; match it as a substring instead.
    responses = len(re.findall(r"HTTP/\d+\.\d+ 200\b", output))
    assert (
        responses >= 2
    ), f"Expected two HTTP 200 responses from pipelined requests, got stdout: {output!r}"
