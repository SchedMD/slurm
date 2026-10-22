############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""Test slurmrestd keeps endpoint connections alive across requests.

Drives a listening slurmrestd over a raw socket so that each response is read
off the wire before the next request is written. That ordering is the whole
point of this module (Issue 51078): writing both requests at once cannot tell
the behaviors apart, because http_con's _on_data() hands the entire input
buffer to the parser in one call, so both responses are queued before conmgr
ever actions the deferred close. Reading first forces the close to have
happened, so a socket that is still usable proves the connection was kept.

The keep-alive cases are paired with controls that must still be closed -- an
HTTP/1.0 client that never negotiated persistence, a client that asked for
"Connection: close", and a refused CONNECT -- so that a passing keep-alive case
cannot be a raw client that is simply unable to notice a close. An HTTP/1.0
client that did negotiate persistence covers the other direction of the same
decision, which a closing control on its own cannot distinguish from a server
that closes every HTTP/1.0 connection.

Responses are matched on their status code, never on the reason phrase, and
connection options are compared case insensitively: RFC9112-4 tells clients to
ignore the reason phrase and RFC9110-7.6.1 makes the connection option a
case-insensitive token, so pinning either to the bytes Slurm happens to emit
today would make the implementation its own specification.
"""

import pathlib
import socket
from urllib.parse import urlsplit

import pytest

import atf

# slurmrestd answers a probe in milliseconds over loopback and its listen
# sockets are created with conmgr_timeouts_disabled, so nothing on the server
# side will ever make a round trip take this long. The timeout only exists so
# that "the connection stayed open but no response ever came" fails the test
# quickly instead of hanging the suite.
SOCKET_TIMEOUT = 30

# In 26.11+, /healthz and /livez answer 200 with status text once the probes
# report at least PROBE_RC_ONLINE, and /readyz at the stricter PROBE_RC_READY.
# probe_run() takes the minimum, so a quiesced daemon answers 500 instead.
# Every case here takes a healthy cluster as its premise.
OK = 200

# A method the server does not implement is refused by name. Distinct from the
# 404 below on purpose: a request refused for its method never had its path
# looked at.
METHOD_NOT_ALLOWED = 405

# slurmrestd has no endpoint index: "/" is routed to a 404 error page, and any
# unrouted path reaches the router's catch-all, which is also a 404. Both are
# kept alive, which is what makes them worth testing as a pair.
NOT_FOUND = 404

# slurmrestd is optional (configure --disable-slurmrestd). Checked here rather
# than in setup() because atf.require_version() below shells out to
# "slurmrestd -V" fatally rather than skipping, so a build without it would
# fail the module instead of passing it over.
pytestmark = pytest.mark.skipif(
    not pathlib.Path(f"{atf.properties['slurm-sbin-dir']}/slurmrestd").exists(),
    reason="Issue 51078: slurmrestd is not built",
)


@pytest.fixture(scope="module", autouse=True)
def setup():
    # 26.05 has these endpoints, but as OpenAPI paths dispatched after
    # authentication, so an unauthenticated probe is answered 401 there. They
    # only became reachable without credentials when they moved to http_router
    atf.require_version(
        (26, 11),
        "sbin/slurmrestd",
        reason="Issue 51078: slurmrestd began serving /healthz, /livez and /readyz unauthenticated from http_router in 26.11",
    )
    # require_slurmrestd() starts slurmrestd with -a jwt, which needs the
    # scontrol token that atf hands it to work
    atf.require_config_parameter_includes("AuthAltTypes", "auth/jwt")
    # No data_parser is pinned: none of these endpoints parses or emits a
    # payload, so naming a version would only add one to retire later
    atf.require_slurmrestd("slurmctld", None)
    # Must come before the URL check: require_slurmrestd() only sets a flag,
    # and the daemon that publishes slurmrestd_url is spawned by start_slurm()
    atf.require_slurm_running()

    # Only auto-config builds slurmrestd_url itself; in local-config it is
    # whatever the operator exported. urlsplit() does not infer a port from the
    # scheme, and create_connection() would turn a missing one into a
    # connection refused against port 0, which reads as "slurmrestd is not
    # running". Declared here rather than in the fixture so the requirement is
    # visible with the others.
    url = urlsplit(atf.properties["slurmrestd_url"])
    if url.scheme != "http" or url.port is None:
        pytest.skip(
            f"Issue 51078: SLURM_TESTSUITE_SLURMRESTD_URL must be http with an"
            f" explicit port to be driven over a raw socket, got"
            f" {atf.properties['slurmrestd_url']!r}"
        )


def _request(path, version="1.1", headers=(), method="GET"):
    """Return the raw bytes of one request for path.

    Host is always sent because RFC9112-3.2 requires it of an HTTP/1.1 client.
    The method, the version and the extra headers are what each test varies to
    negotiate, or refuse, a persistent connection.

    path is whatever target the method takes: an origin-form path for GET, and
    an authority-form "host:port" for CONNECT (RFC9112-3.2.3).
    """

    lines = [f"{method} {path} HTTP/{version}", "Host: localhost"]
    lines.extend(headers)
    return ("\r\n".join(lines) + "\r\n\r\n").encode()


def _status(head):
    """Return (version, code) from a response head, or (b"", None) if closed.

    Only the version and the integer code are returned. RFC9112-4 says a client
    should ignore the reason phrase, so nothing here may depend on it.
    """

    if not head:
        return b"", None

    fields = head.split(b"\r\n", 1)[0].split(b" ")

    return fields[0], int(fields[1])


def _header(head, name):
    """Return the named header's value from a response head, or None.

    Field names are case insensitive per RFC9110-5.1, so the caller never has
    to know which capitalization Slurm emits.
    """

    for line in head.split(b"\r\n")[1:]:
        field, sep, value = line.partition(b":")
        if sep and field.strip().lower() == name.lower():
            return value.strip()

    return None


def _content_length(head):
    """Return the Content-Length declared in a response head, or 0 if absent.

    The readers consume the declared body and reject surplus bytes rather
    than leaving them to corrupt the next response.
    """

    value = _header(head, b"content-length")

    return int(value) if value is not None else 0


def _requests_close(head):
    """Return True if the response asked for the connection to be closed.

    RFC9110-7.6.1 makes the connection option a case-insensitive token, so this
    matches "close" in any casing rather than the exact bytes Slurm writes.
    """

    value = _header(head, b"connection")

    return value is not None and b"close" in value.lower()


def _round_trip(sock, request):
    """Send one request on sock and return the response head, or b"" if closed.

    The head is every byte up to and including the blank line that ends the
    headers, so that every header line in it is CRLF terminated and can be
    matched as a whole line. Read any declared body, except for HEAD, and
    reject bytes beyond its boundary.

    A closed connection reaches python two different ways and both mean the
    same thing here, so both are folded into b"": recv() returns b"" once the
    peer's FIN has been read, while send() and recv() raise BrokenPipeError or
    ConnectionResetError when the peer had already discarded the socket and the
    kernel answered with an RST. Which of the two happens is a race between the
    FIN and the next write, so a test cannot assert on one of them. Returning
    a sentinel instead of letting them propagate also keeps the failure
    reported by the caller's assertion message rather than as a bare traceback.

    A socket timeout is deliberately not caught: a stall is a different bug
    from a close and should surface as one.
    """

    buf = b""

    try:
        sock.sendall(request)

        while b"\r\n\r\n" not in buf:
            data = sock.recv(4096)
            if not data:
                return b""
            buf += data

        head, _, body = buf.partition(b"\r\n\r\n")
        head += b"\r\n\r\n"

        length = 0 if request.startswith(b"HEAD ") else _content_length(head)
        while len(body) < length:
            data = sock.recv(4096)
            if not data:
                return b""
            body += data
        assert len(body) == length, f"Bytes beyond response framing: {body!r}"
    except (BrokenPipeError, ConnectionResetError):
        return b""

    return head


def _read_responses(sock, count):
    """Return count (head, body) responses and any buffered trailing bytes.

    Preserve the remainder so callers can reject an unframed final body.
    """

    buf = b""
    responses = []

    while len(responses) < count:
        head, sep, rest = buf.partition(b"\r\n\r\n")
        if sep:
            head += b"\r\n\r\n"
            length = _content_length(head)
            if len(rest) >= length:
                responses.append((head, rest[:length]))
                buf = rest[length:]
                continue

        data = sock.recv(4096)
        if not data:
            break
        buf += data

    return responses, buf


@pytest.fixture
def connect():
    """Return a factory opening raw TCP connections to the running slurmrestd.

    Every socket handed out is closed at teardown so the tests need no
    try/finally of their own. The address is read back out of
    slurmrestd_url, the only property require_slurmrestd() sets on both the
    auto-config and the SLURM_TESTSUITE_SLURMRESTD_URL paths.

    Raw sockets rather than requests/urllib3: a connection pool transparently
    reconnects after a close, which is exactly the difference under test.
    """

    url = urlsplit(atf.properties["slurmrestd_url"])
    opened = []

    def _connect():
        # create_connection() applies the timeout to the returned socket, so
        # every later send/recv on it is bounded too
        sock = socket.create_connection((url.hostname, url.port), SOCKET_TIMEOUT)
        opened.append(sock)
        return sock

    yield _connect

    for sock in opened:
        sock.close()


@pytest.mark.parametrize(
    "path, code",
    [
        ("/healthz", OK),
        ("/livez", OK),
        ("/readyz", OK),
        ("/", NOT_FOUND),
        ("/not-a-slurmrestd-endpoint", NOT_FOUND),
    ],
)
def test_endpoint_keeps_connection_alive(connect, path, code):
    """Verify slurmrestd answers a second request on the same connection.

    The second request is written only after the first response has been read
    off the wire. An older slurmrestd answers the first request with
    "Connection: Close" and queues the connection closed at the same moment,
    so by the time the second request is written the socket is already gone
    and the second round trip comes back empty.

    The two 404 paths are both included on purpose: "/" is routed to its own
    handler while the other reaches the router's catch-all, and the two used to
    disagree about the connection despite being indistinguishable to a client.
    """

    sock = connect()

    first = _round_trip(sock, _request(path))
    assert _status(first) == (b"HTTP/1.1", code), (
        f"GET {path} on an HTTP/1.1 connection should be answered {code}, got "
        f"head {first!r} (b'' means slurmrestd closed the connection instead "
        f"of answering)"
    )
    assert not _requests_close(first), (
        f"GET {path} should not tell an HTTP/1.1 client the connection is "
        f"closing, got head {first!r}"
    )
    second = _round_trip(sock, _request(path, headers=["Connection: close"]))
    assert _status(second) == (b"HTTP/1.1", code), (
        f"A second GET {path}, written only after the first response had been "
        f"read, should be answered {code} on the same connection, got head "
        f"{second!r} (b'' means slurmrestd closed the connection after the "
        f"first response)"
    )
    assert sock.recv(1) == b"", "Expected EOF without unframed response content"


def test_head_then_get_is_framed(connect):
    """Verify HEAD declares a representation length without emitting its body."""

    sock = connect()
    head = _round_trip(sock, _request("/", method="HEAD"))
    assert _status(head) == (
        b"HTTP/1.1",
        NOT_FOUND,
    ), f"Expected HEAD / to return {NOT_FOUND}, got {head!r}"
    assert (
        _content_length(head) > 0
    ), f"Expected HEAD / to declare its GET representation length, got {head!r}"
    assert not _requests_close(head), f"HEAD / unexpectedly closes: {head!r}"

    response = _round_trip(sock, _request("/", headers=["Connection: close"]))
    assert _status(response) == (
        b"HTTP/1.1",
        NOT_FOUND,
    ), f"GET / after HEAD should start with a valid response, got {response!r}"
    assert sock.recv(1) == b"", "Expected EOF without unframed response content"


def test_pipelined_responses_are_each_framed(connect):
    """Verify two requests written at once come back individually delimited.

    True pipelining per RFC9112-9.3.2: both requests go out in one write with
    no wait in between. Unlike test_endpoint_keeps_connection_alive this cannot
    tell the connection lifetime apart, because _on_data() hands the whole
    input buffer to the parser in one call and both responses are queued before
    conmgr actions any deferred close. What it does pin is that each response
    declares a Content-Length matching its body, which is the only thing that
    lets a client find where the first response ends and the second begins.

    "/" carries an error page, so it exercises framing beyond the short
    status text returned by the probes.
    """

    sock = connect()

    # EOF supplies an independent boundary after the final declared body.
    sock.sendall(_request("/") + _request("/", headers=["Connection: close"]))
    responses, trailing = _read_responses(sock, 2)

    assert len(responses) == 2, (
        f"Two pipelined GET / requests should be answered with two responses, "
        f"got {responses!r}"
    )

    for index, (head, body) in enumerate(responses):
        assert _status(head) == (b"HTTP/1.1", NOT_FOUND), (
            f"Pipelined response {index + 1} should be answered {NOT_FOUND}, "
            f"got head {head!r}"
        )
        declared = _header(head, b"content-length")
        assert declared is not None, (
            f"Pipelined response {index + 1} must declare a Content-Length so "
            f"the next response can be found, got head {head!r}"
        )
        assert int(declared) == len(body), (
            f"Pipelined response {index + 1} declared Content-Length "
            f"{declared!r} but its body is {len(body)} bytes, so a client "
            f"would mis-frame the response that follows it"
        )

    assert not trailing, f"Unframed trailing bytes: {trailing!r}"
    assert sock.recv(1) == b"", "Expected EOF after final framed response"


def test_http_10_without_keep_alive_closes(connect):
    """Verify an HTTP/1.0 probe that never negotiated persistence is closed.

    RFC9112-C.2.2 makes a persistent connection opt-in before HTTP/1.1, so
    this client must still be told the connection is closing and must still
    find it gone. This is a control for test_endpoint_keeps_connection_alive:
    it proves this raw client really does observe a close, so a passing
    keep-alive case cannot be a client that is unable to tell the difference.
    """

    sock = connect()

    first = _round_trip(sock, _request("/healthz", version="1.0"))
    assert _status(first) == (b"HTTP/1.0", OK), (
        f"GET /healthz on an HTTP/1.0 connection should be answered "
        f"{OK} echoing the request version, got head {first!r}"
    )
    assert _requests_close(first), (
        f"An HTTP/1.0 GET /healthz that did not ask for keep-alive should be "
        f"told the connection is closing, got head {first!r}"
    )

    second = _round_trip(sock, _request("/healthz", version="1.0"))
    assert second == b"", (
        f"slurmrestd should have closed the connection after answering an "
        f"HTTP/1.0 GET /healthz, but a second request on it was answered "
        f"{second!r}"
    )


def test_http_10_with_keep_alive_is_kept(connect):
    """Verify an HTTP/1.0 client that negotiated persistence keeps the socket.

    The mirror of test_http_10_without_keep_alive_closes. RFC9112-C.2.2 makes
    persistence opt-in before HTTP/1.1 and both directions of that choice are
    derived from the same request state, so testing only the closing half
    would pass just as well against a server that closed every HTTP/1.0
    connection whatever the client asked for.

    RFC9112-C.2.2 also requires the response to confirm the persistence with
    "Connection: keep-alive". Without it a compliant HTTP/1.0 client assumes
    the server is closing and never reuses the socket.
    """

    sock = connect()

    keep_alive = _request("/healthz", version="1.0", headers=["Connection: keep-alive"])

    first = _round_trip(sock, keep_alive)
    assert _status(first) == (b"HTTP/1.0", OK), (
        f"GET /healthz on an HTTP/1.0 connection should be answered "
        f"{OK} echoing the request version, got head {first!r}"
    )
    assert not _requests_close(first), (
        f"An HTTP/1.0 GET /healthz that asked for keep-alive should not be "
        f"told the connection is closing, got head {first!r}"
    )

    assert (_header(first, b"connection") or b"").lower() == b"keep-alive", (
        f"An HTTP/1.0 response that keeps the connection should confirm it "
        f"with 'Connection: keep-alive', got head {first!r}"
    )

    second = _round_trip(sock, keep_alive)
    assert _status(second) == (b"HTTP/1.0", OK), (
        f"A second HTTP/1.0 GET /healthz, written only after the first "
        f"response had been read, should be answered {OK} on the same "
        f"connection the client negotiated, got head {second!r} (b'' means "
        f"slurmrestd closed it despite the keep-alive request)"
    )


def test_connect_is_refused_by_method(connect):
    """Verify CONNECT is refused for its method, not for its target.

    No Slurm daemon implements the tunnel CONNECT asks for, so it is refused
    outright, and refused before the target is examined. That ordering is what
    makes the status 405 rather than a 404: RFC9112-3.2.3 gives CONNECT an
    authority-form target that carries no path, so a router asked to match one
    would turn a method it does not implement into a path it cannot find. The
    status is matched, never the reason phrase.

    A refused request is still closed, so this is also a third control for
    test_endpoint_keeps_connection_alive.
    """

    sock = connect()

    first = _round_trip(sock, _request("localhost:443", method="CONNECT"))
    assert _status(first) == (b"HTTP/1.1", METHOD_NOT_ALLOWED), (
        f"CONNECT should be refused {METHOD_NOT_ALLOWED} for its method "
        f"rather than {NOT_FOUND} for a path it never carried, got head "
        f"{first!r}"
    )
    assert _requests_close(first), (
        f"A refused CONNECT should be told the connection is closing, got "
        f"head {first!r}"
    )

    second = _round_trip(sock, _request("/healthz"))
    assert second == b"", (
        f"slurmrestd should have closed the connection after refusing a "
        f"CONNECT, but a second request on it was answered {second!r}"
    )


def test_connection_close_request_closes(connect):
    """Verify a client that asks to close is closed even on a kept-alive path.

    The client asking is reason enough on its own, independent of what the
    handler requests, so /healthz must honor it on an HTTP/1.1 connection.
    This is the second control: it closes a connection the same request would
    otherwise have kept, so it isolates the client's ask from the endpoint.
    """

    sock = connect()

    first = _round_trip(sock, _request("/healthz", headers=["Connection: close"]))
    assert _status(first) == (b"HTTP/1.1", OK), (
        f"GET /healthz should still be answered {OK} when the client "
        f"asks to close, got head {first!r}"
    )
    assert _requests_close(first), (
        f"A client sending Connection: close should be told the connection is "
        f"closing, got head {first!r}"
    )

    second = _round_trip(sock, _request("/healthz"))
    assert second == b"", (
        f"slurmrestd should have closed the connection the client asked to "
        f"close, but a second request on it was answered {second!r}"
    )
