# Script that registers signal handlers and creates a file
# to signal readiness when registration is complete.
# It then waits for a short period, printing any signals received.

import signal
import time
import sys
import pathlib


def receiveSignal(signalNumber, frame):
    print("Received:", signalNumber)
    return


if __name__ == "__main__":
    # Check for readiness file argument
    if len(sys.argv) < 2:
        print("Error: Readiness file path argument required.", file=sys.stderr)
        sys.exit(1)

    readiness_file_path = pathlib.Path(sys.argv[1])

    # Ensure the directory exists (it should, being the test's tmp path)
    readiness_file_path.parent.mkdir(parents=True, exist_ok=True)

    # Register the signals to be caught
    signal.signal(signal.SIGHUP, receiveSignal)
    signal.signal(signal.SIGINT, receiveSignal)
    signal.signal(signal.SIGQUIT, receiveSignal)
    signal.signal(signal.SIGILL, receiveSignal)
    signal.signal(signal.SIGTRAP, receiveSignal)
    signal.signal(signal.SIGABRT, receiveSignal)
    signal.signal(signal.SIGBUS, receiveSignal)
    signal.signal(signal.SIGFPE, receiveSignal)
    # signal.signal(signal.SIGKILL, receiveSignal)
    signal.signal(signal.SIGUSR1, receiveSignal)
    signal.signal(signal.SIGSEGV, receiveSignal)
    signal.signal(signal.SIGUSR2, receiveSignal)
    signal.signal(signal.SIGPIPE, receiveSignal)
    signal.signal(signal.SIGALRM, receiveSignal)
    signal.signal(signal.SIGTERM, receiveSignal)

    # Signal readiness by creating the file
    print(f"Creating readiness file: {readiness_file_path}", flush=True)
    try:
        readiness_file_path.touch()
        print("Readiness file created.", flush=True)
    except Exception as e:
        print(f"Error creating readiness file: {e}", file=sys.stderr, flush=True)
        sys.exit(1)

    bail = 0
    # Wait in an endless loop for signals
    while bail < 15:
        time.sleep(1)
        bail += 1
