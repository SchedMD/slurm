#!/usr/bin/env python

import re
import sys
import os

include_pat = r'(<!--\s*#include\s*virtual\s*=\s*"([^"]+)"\s*-->)'
include_regex = re.compile(include_pat)

url_pat = r'(\s+href\s*=\s*")([^"#]+)(#[^"]+)?(")'
url_regex = re.compile(url_pat)

dirname = ''

def include_virtual(matchobj):
    global dirname
    if dirname:
        filename = dirname + '/' + matchobj.group(2)
    else:
        filename = matchobj.group(2)

    if os.access(filename, os.F_OK):
        #print 'Including file', filename
        lines = file(filename, 'r').read()
        return lines
    else:
        return matchobj.group(0)

def url_rewrite(matchobj):
    global dirname
    if dirname:
        localpath = dirname + '/' + matchobj.group(2)
    else:
        localpath = matchobj.group(2)

    if matchobj.group(2)[-6:] == '.shtml' and os.access(localpath, os.F_OK):
        location = matchobj.group(2)
        if matchobj.group(3) is None:
            newname = location[:-6] + '.html'
        else:
            newname = location[:-6] + '.html' + matchobj.group(3)
        #print 'Rewriting', location, 'to', newname
        return matchobj.group(1) + newname + matchobj.group(4)
    else:
        return matchobj.group(0)

# Make sure all of the files on the command line have the .shtml extension.
files = []
for f in sys.argv[1:]:
    if f[-6:] == '.shtml':
        files.append(f)
    else:
        #print 'Skipping file %s (extension is not .shtml)' % f
        pass

for filename in files:
    dirname, basefilename = os.path.split(filename)
    newfilename = basefilename[:-6] + '.html'
    print 'Converting', filename, '->', newfilename
    shtml = file(filename, 'r')
    html = file(newfilename, 'w')

    for line in shtml.readlines():
        line = include_regex.sub(include_virtual, line)
        line = url_regex.sub(url_rewrite, line)
        html.write(line)

    html.close()
    shtml.close()
