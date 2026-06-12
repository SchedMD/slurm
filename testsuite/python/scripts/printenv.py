#!/usr/bin/env python3
############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import os
import sys
import json


def main():
    """
    Print the os.environ in a JSON parsable format
    """
    args = sys.argv[1:]
    env = dict(os.environ)

    if args:
        filtered = {k: env[k] for k in args if k in env}
        # If any requested vars are missing, match printenv behavior (non-zero exit)
        missing = [k for k in args if k not in env]
        if missing:
            print(
                f"Missing environment variables: {', '.join(missing)}",
                file=sys.stderr,
            )
            sys.exit(1)
        env = filtered

    print(json.dumps(env))


if __name__ == "__main__":
    main()
