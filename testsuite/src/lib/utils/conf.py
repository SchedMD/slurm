############################################################################
# Copyright (C) SchedMD LLC.
############################################################################
import re
from collections import OrderedDict


def get_vars_from_conf(filename, comment="#"):
    data = OrderedDict()

    with open(filename) as f:
        for line in f:
            if not line.startswith(comment):
                if re.search(r"\s*\S+\s*=\s*\S+\s*$", line):
                    name, val = line.strip().replace(" ", "").split("=")
                    data[name] = val

    return data
