#!/usr/bin/env python

import re
import sys
import os
import codecs

canonical_url = 'https://slurm.schedmd.com/'

include_pat = r'(<!--\s*#include\s*virtual\s*=\s*"([^"]+)"\s*-->)'
include_regex = re.compile(include_pat)

canonical_pat = r'(<!--\s*#canonical\s*-->)'
canonical_regex = re.compile(canonical_pat)

page_title_pat = r'(<!--\s*#pagetitle\s*-->)'
page_title_regex = re.compile(page_title_pat)

url_pat = r'(\s+href\s*=\s*")([^"#]+)(#[^"]+)?(")'
url_regex = re.compile(url_pat)

first_header_pat = r'(<[h|H]1>\s*[<a name="top">]*\s*([a-zA-Z0-9_ ()\'/-]+)[:]*.*\s*[</a>]*\s*</[h|H]1>)'
first_header_regex = re.compile(first_header_pat)

version_pat = r'(@SLURM_VERSION@)'
version_regex = re.compile(version_pat)

title = ''
dirname = ''
newfilename = ''

def include_virtual(matchobj):
    global dirname
    if dirname:
        filename = dirname + '/' + matchobj.group(2)
    else:
        filename = matchobj.group(2)

    if os.access(filename, os.F_OK):
        #print 'Including file', filename
        lines = open(filename, 'r').read()
        return lines
    else:
        return matchobj.group(0)

def canonical_rewrite(matchobj):
    global newfilename
    return '<link rel="canonical" href="' + canonical_url + newfilename + '" />'

def page_title_rewrite(matchobj):
    global title
    return '<title>Slurm Workload Manager - ' + title + '</title>'

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

def version_rewrite(matchobj):
    global version
    return version

# Make sure all of the files on the command line have the .shtml extension.
version = sys.argv[1]

files = []
for f in sys.argv[2:]:
    if f[-6:] == '.shtml':
        files.append(f)
    else:
        #print 'Skipping file %s (extension is not .shtml)' % f
        pass

for filename in files:
    dirname, basefilename = os.path.split(filename)
    newfilename = basefilename[:-6] + '.html'
    print('Converting', filename, '->', newfilename)
    shtml = codecs.open(filename, 'r', encoding='utf-8')
    html = codecs.open(newfilename, 'w', encoding='utf-8')

    for line in shtml.readlines():
        result = first_header_regex.match(line)
        if result:
            title = result.group(2)
            break

    shtml.seek(0)
    for line in shtml.readlines():
        line = include_regex.sub(include_virtual, line)
        line = page_title_regex.sub(page_title_rewrite, line)
        line = version_regex.sub(version_rewrite, line)
        line = canonical_regex.sub(canonical_rewrite, line)
        line = url_regex.sub(url_rewrite, line)
        html.write(line)


    html.close()
    shtml.close()
