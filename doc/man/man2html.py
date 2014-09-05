#!/usr/bin/env python

import re
import sys
import os
import codecs

include_pat = r'(<!--\s*#include\s*virtual\s*=\s*"([^"]+)"\s*-->)'
include_regex = re.compile(include_pat)

url_pat = r'(\s+href\s*=\s*")([^"#]+)(#[^"]+)?(")'
url_regex = re.compile(url_pat)

version_pat = r'(@SLURM_VERSION@)'
version_regex = re.compile(version_pat)

dirname = ''

# Instert tags for options
#   Two styles are processed.
#       <DT><B>pppppp</B><DD>
#           has tag <a id="OPT_pppppp"></a>
#       <DT><B>--pppppp</B> or <DT><B>-P</B>, <B>--pppppp</B>
#           has tag <a id="OPT_pppppp"></a>
#   <H2>hhhh</h2> also has tag has tag <a id="SECTION_hhhh"></a> inserted
def insert_tag(html, lineIn):
    if lineIn[0:4] == "<H2>":
        posEnd = lineIn.find("</H2>")
        if posEnd != -1:
            html.write('<a id="SECTION_' + lineIn[4:posEnd] + '"></a>\n')
            return

    if lineIn[0:7] != "<DT><B>":
        return
    posBgn = lineIn.find("--")
    if posBgn == -1:
        # 1st form
        posBgn = 5
    posBgn = posBgn + 2
    posEnd = lineIn.find("</B>",posBgn)
    if posEnd == -1:
        # poorly constructed
        return
    html.write('<a id="OPT_' + lineIn[posBgn:posEnd] + '"></a>\n')
    return


def llnl_references(line):
        manStr = "Refer to mc_support.html"
        htmlStr = 'Refer to <a href="mc_support.html">mc_support</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="http://slurm.schedmd.com/mc_support.html">http://slurm.schedmd.com/mc_support.html</A>'
        htmlStr = 'the <a href="mc_support.html">mc_support</a> document'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="http://slurm.schedmd.com/dist_plane.html.">http://slurm.schedmd.com/dist_plane.html.</A>'
        htmlStr = 'the <a href="dist_plane.html">dist_plane</a> document'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '&lt;<A HREF="http://slurm.schedmd.com/mpi_guide.html">http://slurm.schedmd.com/mpi_guide.html</A>&gt;'
        htmlStr = '<a href="mpi_guide.html">mpi_guide</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '(<A HREF="http://slurm.schedmd.com/power_save.html).">http://slurm.schedmd.com/power_save.html).</A>'
        htmlStr = '<a href="power_save.html">power_save</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="http://slurm.schedmd.com/cons_res.html">http://slurm.schedmd.com/cons_res.html</A>'
        htmlStr = '<a href="cons_res.html">cons_res</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="http://slurm.schedmd.com/cons_res_share.html">http://slurm.schedmd.com/cons_res_share.html</A>'
        htmlStr = '<a href="cons_res_share.html">cons_res_share</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="http://slurm.schedmd.com/gang_scheduling.html">http://slurm.schedmd.com/gang_scheduling.html</A>'
        htmlStr = '<a href="gang_scheduling.html">gang_scheduling</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="http://slurm.schedmd.com/preempt.html">http://slurm.schedmd.com/preempt.html</A>'
        htmlStr = '<a href="preempt.html">preempt</a>'
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
        #print 'Including file', filename
        lines = open(filename, 'r').read()
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

def version_rewrite(matchobj):
    global version
    return version

files = []
version = sys.argv[1]
for f in sys.argv[4:]:
    posLastDot = f.rfind(".")
    mhtmlname = f[:posLastDot] + ".mhtml"
    cmd = "man2html " + f + "> " + mhtmlname
    os.system(cmd)
    print(">>>>>>> " + mhtmlname)
    files.append(mhtmlname)

for filename in files:
    dirname, basefilename = os.path.split(filename)
    newfilename = basefilename[:-6] + '.html'
    print('Converting', filename, '->', newfilename)
    shtml = codecs.open(filename, 'r', encoding='utf-8')
    html = codecs.open(newfilename, 'w', encoding='utf-8')

    lines = open(sys.argv[2], 'r').read()
    lines = lines.replace(".shtml",".html")
    lines = version_regex.sub(version_rewrite, lines)
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
        insert_tag(html, line)
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
