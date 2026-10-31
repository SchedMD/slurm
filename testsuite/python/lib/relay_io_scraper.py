############################################################################
# Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
############################################################################
"""A stand in for the site's log scraper, for the relay_io tests.

Every relay_io test needs the same thing: something bound to the datagram
socket the daemons are configured to write to, collecting the JSON records
it receives, one per datagram. Shared so that the modules cannot drift
apart on what counts as a record worth failing a test over.

relay_io/uds_json refuses to relay to a socket, or a directory holding one,
that is owned by anyone but root or SlurmdUser: whoever is bound there
receives the output of every relayed job on the node. The testsuite runs as
an ordinary user, so the socket lives in a directory made as root and the
scraper itself is a root process, relay_io_scraper_helper.py. It writes the
records to a file in the test's own directory, which this class reads back.
"""

import base64
import json
import os

import pytest

import atf

# One datagram is one record, so this only has to be larger than the largest
# record a test can provoke. A module that raises max_log_kbytes passes its
# own value.
DEFAULT_RECV_SIZE = 1 << 20

HELPER = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "relay_io_scraper_helper.py"
)


def make_socket_dir(tag):
    """Make a directory relay_io/uds_json will trust, and return its path.

    Owned by root and writable by no one else, which is what the plugin
    requires of the directory holding the socket. Kept directly under /tmp
    and named for the module rather than taken from tmp_path, both to stay
    inside the length a sockaddr_un allows and because the test user owns
    everything under tmp_path.

    IN tag - short name of the test module, to tell concurrent runs apart
    RET the directory, to pass to remove_socket_dir() when the module ends
    """
    directory = f"/tmp/relay-io-{tag}-{os.getpid()}"

    atf.run_command(f"rm -rf {directory}", user="root", quiet=True)
    atf.run_command(f"mkdir -m 0755 {directory}", user="root", fatal=True)

    return directory


def remove_socket_dir(directory):
    """Remove a directory made by make_socket_dir(), and the socket in it."""
    atf.run_command(f"rm -rf {directory}", user="root", quiet=True)


class Scraper:
    """Listens on the relay socket and collects records, as a site scraper would."""

    def __init__(self, path, recv_size=DEFAULT_RECV_SIZE, rcvbuf=None, owner=-1):
        """IN owner - uid to give the socket, -1 to leave it owned by root"""
        self.path = path
        self._owner = owner
        self.records = []
        self.errors = []
        self.sizes = []
        self._recv_size = recv_size
        self._rcvbuf = rcvbuf
        # In the test's own directory rather than next to the socket, so
        # that this user can read it back. The helper makes both writable by
        # anyone, since it is root and the test is not.
        self._out_path = os.path.abspath(f"{os.path.basename(path)}.records")
        self._pid_path = f"{self._out_path}.pid"
        # Where the helper's own output goes. Nothing reads it while it runs,
        # but a helper that never binds has to say why.
        self._log_path = f"{self._out_path}.log"
        self._offset = 0
        self._pid = None
        self._process = None

        self.start()

    def start(self):
        """Bind the socket and begin collecting.

        Also used to bring the scraper back after stop(), which is how a
        test stands in for a scraper that was down and came back. The
        records already collected are kept, since the file is appended to
        rather than replaced.
        """
        if os.path.exists(self._pid_path):
            os.unlink(self._pid_path)

        rcvbuf = self._rcvbuf or 0
        # To a file rather than left on a pipe: nothing drains the pipe while
        # the helper runs, and a full one would stop it.
        self._process = atf.run_command(
            f"python3 {HELPER} {self.path} {self._out_path} {self._pid_path}"
            f" {self._recv_size} {rcvbuf} {self._owner}"
            f" >>{self._log_path} 2>&1",
            user="root",
            background=True,
        )["process"]

        # The helper writes its pid once the socket is bound, so this waits
        # for the relay to have somewhere to go rather than for a process.
        for _ in atf.timer():
            if os.path.exists(self._pid_path):
                break
        else:
            pytest.fail(f"the scraper did not bind {self.path}: {self._helper_log()}")

        with open(self._pid_path) as f:
            self._pid = int(f.read().strip())

    def _helper_log(self):
        """RET whatever the helper wrote to its output, for a failure message."""
        if not os.path.exists(self._log_path):
            return ""

        with open(self._log_path) as f:
            return f.read()

    def _drain(self):
        """Read the records the helper has written since the last call."""
        with open(self._out_path, "rb") as f:
            f.seek(self._offset)
            data = f.read()

        # A record is only whole once its newline is there. Anything after
        # the last one is left for the next call.
        end = data.rfind(b"\n")
        if end < 0:
            return
        self._offset += end + 1

        for line in data[:end].split(b"\n"):
            raw = base64.b64decode(line)

            # Decode strictly: a record must be valid UTF-8 JSON. A
            # malformed one is recorded rather than raised, so it fails the
            # test that is waiting instead of being passed over in silence.
            try:
                record = json.loads(raw.decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as error:
                self.errors.append(f"{error}: {raw!r}")
                continue

            self.sizes.append(len(raw))
            self.records.append(record)

    def clear(self):
        self._drain()
        self.records = []
        self.errors = []
        self.sizes = []

    def get(self):
        self._drain()
        return list(self.records)

    def get_sizes(self):
        self._drain()
        return list(self.sizes)

    def raise_for_errors(self):
        """Fail if any record did not parse as UTF-8 JSON."""
        self._drain()
        assert (
            not self.errors
        ), f"every relayed record should be valid JSON: {self.errors}"

    def _settle(self):
        """Wait until no further records arrive."""
        previous = -1
        for _ in atf.timer(fatal=True):
            current = len(self.get())
            if current == previous:
                break
            previous = current

    def wait_for(self, event_type):
        """Wait for a record of event_type, then return every record collected.

        job_end is the last record a node emits for a job, so waiting for it
        is what tells us the job's records have all arrived.
        """
        for _ in atf.timer(fatal=True):
            if any(r["type"] == event_type for r in self.get()):
                break

        # a record emitted after event_type would still be in flight
        self._settle()

        self.raise_for_errors()

        return self.get()

    def wait_for_quiet(self):
        """Return the records after giving any in flight ones time to arrive.

        Used by the tests that assert nothing was relayed, where there is no
        record to wait for.
        """
        self._settle()

        self.raise_for_errors()

        return self.get()

    def remove_socket(self):
        """Unlink the socket, standing in for a scraper that never bound.

        Root owns the directory it is in, so this cannot be done with
        os.unlink() the way the records file can.
        """
        atf.run_command(f"rm -f {self.path}", user="root", fatal=True)

    def stop(self):
        """Stop collecting and give up the socket.

        The path is left behind, as it would be by a scraper that exited.
        start() unlinks whatever is there, and remove_socket() stands in for
        a scraper that never bound at all.
        """
        if not self._pid:
            return

        atf.run_command(f"kill {self._pid}", user="root", quiet=True)

        for _ in atf.timer(fatal=True):
            if not os.path.exists(f"/proc/{self._pid}"):
                break

        # sudo is still there until the helper it started is reaped
        self._process.wait(timeout=5)

        self._pid = None
        self._process = None
