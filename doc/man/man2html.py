#!/usr/bin/env python3

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

first_header_pat = r'(<[h|H]1>\s*([a-zA-Z0-9_ ()\'/-\\.]+)\s*</[h|H]1>)'
first_header_regex = re.compile(first_header_pat)

version_pat = r'(@SLURM_VERSION@)'
version_regex = re.compile(version_pat)

ids = {}

title = ''
dirname = ''

# Insert tags for options
#   Two styles are processed.
#       <DT><B>pppppp</B><DD>
#           has tag <a id="OPT_pppppp"></a>
#       <DT><B>--pppppp</B> or <DT><B>-P</B>, <B>--pppppp</B>
#           has tag <a id="OPT_pppppp"></a>
#   <H2>hhhh</h2> also has tag has tag <a id="SECTION_hhhh"></a> inserted
def insert_tag(lineIn):
    lineOt = ""
    if lineIn[0:2] == "<H" and lineIn[3] == ">":
        header_num = lineIn[2]
        # Don't make links to h1 tags - no need, since already at top of page
        if header_num == "1":
            return lineIn;

        posEnd = lineIn.find("</H%s>" % header_num)
        if posEnd == -1:
            return lineIn;

        contents = lineIn[4:posEnd]
        id_name = contents.replace(' ','-')
        if id_name in ids:
            ids[id_name] += 1
            id_name += "_" + str(ids[id_name])
        else:
            ids[id_name] = 0
        id_name = "SECTION_%s" % (id_name)
        lineOt = '<h%s>%s<a class="slurm_link" id="%s" href="#%s"></a></h%s>' % (header_num, contents, id_name, id_name, header_num)
        return lineOt

    if lineIn[0:4] != "<DT>":
        return lineIn
    if lineIn[4:7] == "<B>":
        bold_tag = True
    elif lineIn[4:7] == "<I>":
        bold_tag = False
    else:
        return lineIn
    contentsBgn = 4
    contentsEnd = lineIn.find("<DD>")
    contents = lineIn[contentsBgn:contentsEnd]
    posBgn = lineIn.find("--")
    if posBgn == -1:
        # 1st form
        posBgn = 7
    else:
        posBgn += 2

    if bold_tag:
        posEnd = lineIn.find("</B>",posBgn)
    else:
        posEnd = lineIn.find("</I>",posBgn)

    if posEnd == -1:
        # poorly constructed
        return lineIn

    id_name = lineIn[posBgn:posEnd]
    id_name = id_name.replace(' ','-')
    if id_name in ids:
        ids[id_name] += 1
        id_name += "_" + str(ids[id_name])
    else:
        ids[id_name] = 0
    id_name = "OPT_%s" % (id_name)
    link_and_contents = '%s<a class="slurm_link" id="%s" href="#%s"></a>' % (contents, id_name, id_name)
    lineOt = "<dt>" + link_and_contents + "</dt><dd>"
    return lineOt


def llnl_references(line):
        manStr = "Refer to mc_support.html"
        htmlStr = 'Refer to <a href="mc_support.html">mc_support</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="https://slurm.schedmd.com/mc_support.html">https://slurm.schedmd.com/mc_support.html</A>'
        htmlStr = 'the <a href="mc_support.html">mc_support</a> document'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="https://slurm.schedmd.com/dist_plane.html.">https://slurm.schedmd.com/dist_plane.html.</A>'
        htmlStr = 'the <a href="dist_plane.html">dist_plane</a> document'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '&lt;<A HREF="https://slurm.schedmd.com/mpi_guide.html">https://slurm.schedmd.com/mpi_guide.html</A>&gt;'
        htmlStr = '<a href="mpi_guide.html">mpi_guide</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '(<A HREF="https://slurm.schedmd.com/power_save.html).">https://slurm.schedmd.com/power_save.html).</A>'
        htmlStr = '<a href="power_save.html">power_save</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="https://slurm.schedmd.com/cons_res.html">https://slurm.schedmd.com/cons_res.html</A>'
        htmlStr = '<a href="cons_res.html">cons_res</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="https://slurm.schedmd.com/cons_res_share.html">https://slurm.schedmd.com/cons_res_share.html</A>'
        htmlStr = '<a href="cons_res_share.html">cons_res_share</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="https://slurm.schedmd.com/gang_scheduling.html">https://slurm.schedmd.com/gang_scheduling.html</A>'
        htmlStr = '<a href="gang_scheduling.html">gang_scheduling</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="https://slurm.schedmd.com/preempt.html">https://slurm.schedmd.com/preempt.html</A>'
        htmlStr = '<a href="preempt.html">preempt</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="/cgi-bin/man/man2html?8+logrotate">logrotate</A>'
        htmlStr = 'logrotate'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        return line

def relative_reference(lineIn):
    fullRef = "/cgi-bin/man/man2html"
    lenRef = len(fullRef)
    refAnchor="<A HREF=";
    lenRefAnchor = len(refAnchor)
    lineOt = ""
    cursor = 0

    posHREF = lineIn.find(fullRef,cursor)
    if posHREF == -1:
        return lineIn
    if lineIn[posHREF+lenRef] != "?":
        pos = lineIn.find("Return to Main Contents",cursor)
        if pos != -1:
            return ""
        return "<i>man2html</i> "
    while posHREF != -1:
        posRefAnchor = lineIn.find(refAnchor,cursor)
        lineOt = lineOt + lineIn[cursor:posRefAnchor+lenRefAnchor]
        cursor = posHREF + lenRef + 3
        lineOt = lineOt + '"'
        posQuote = lineIn.find('"',cursor)
        lineOt = lineOt + lineIn[cursor:posQuote] + ".html"
        cursor = posQuote
        posHREF = lineIn.find(fullRef,cursor)
    lineOt = lineOt + lineIn[cursor:]
    return lineOt


def include_virtual(matchobj):
    global dirname
    if dirname:
        filename = dirname + '/' + matchobj.group(2)
    else:
        filename = matchobj.group(2)

    if os.access(filename, os.F_OK):
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
        return matchobj.group(1) + newname + matchobj.group(4)
    else:
        return matchobj.group(0)

def version_rewrite(matchobj):
    global version
    return version

files = []
version = sys.argv[1]
for f in sys.argv[4:]:
    posLastDot = f.rfind(".")
    mhtmlname = f[:posLastDot] + ".mhtml"
    cmd = "man2html < " + f + "> " + mhtmlname
    os.system(cmd)
    print(">>>>>>> " + mhtmlname)
    files.append(mhtmlname)

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

    lines = open(sys.argv[2], 'r').read()
    lines = lines.replace(".shtml",".html")
    lines = version_regex.sub(version_rewrite, lines)
    lines = page_title_regex.sub(page_title_rewrite, lines)
    lines = canonical_regex.sub(canonical_rewrite, lines)
    html.write(lines)
#    html.write(<!--#include virtual="header.txt"-->)
    for line in shtml.readlines():
        # Remove html header/footer created by man2html
        if line == "Content-type: text/html\n":
            continue
        if line == "Content-type: text/html; charset=UTF-8\n":
            continue
        if line[:6] == "<HTML>":
            continue
        if line[:7] == "</HEAD>":
            continue
        if line[:7] == "</HTML>":
            continue
        if line[:7] == "</BODY>":
            continue
        line = include_regex.sub(include_virtual, line)
        # Special case some html references
        line = llnl_references(line)
        #insert tags for some options
        line = insert_tag(line)
        # Make man2html links relative ones
        line = relative_reference(line)

        line = url_regex.sub(url_rewrite, line)
        html.write(line)
    lines = open(sys.argv[3], 'r').read()
    lines = lines.replace(".shtml",".html")
    lines = version_regex.sub(version_rewrite, lines)
    html.write(lines)
#    html.write(<!--#include virtual="footer.txt"-->)
    html.close()
    shtml.close()
    os.remove(filename)
