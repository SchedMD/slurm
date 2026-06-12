############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
PREFIX = "run-tests: "
BAR = "=" * 5


# Logging
def log(text, prefix=PREFIX):
    print(f"{prefix}{text}")


def log_new_line(text):
    log(text, f"\n{PREFIX}")


def log_header(header):
    log(f"\n{BAR} {PREFIX}< {header} > {BAR}\n", "")


def log_footer(footer):
    log(f"\n{BAR} {PREFIX}</ {footer} > {BAR}\n", "")


def log_if_exists(var):
    if var is not None and len(var) > 0:
        log(var)


def print_pretty_dict(d):
    print("\n{")
    for k, v in d.items():
        print(f"  {k}: {v}")
    print("}\n")
