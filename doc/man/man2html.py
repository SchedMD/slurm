#!/usr/bin/env python

import re
import sys
import os
include_pat = r'(<!--\s*#include\s*virtual\s*=\s*"([^"]+)"\s*-->)'
include_regex = re.compile(include_pat)

url_pat = r'(\s+href\s*=\s*")([^"#]+)(#[^"]+)?(")'
url_regex = re.compile(url_pat)

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
        manStr = "Refer to the mc_support.html document"
        htmlStr = 'Refer to the <a href="mc_support.html">mc_support</a> document'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="https://computing.llnl.gov/linux/slurm/mc_support.html">https://computing.llnl.gov/linux/slurm/mc_support.html</A>'
        htmlStr = 'the <a href="mc_support.html">mc_support</a> document'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="https://computing.llnl.gov/linux/slurm/dist_plane.html.">https://computing.llnl.gov/linux/slurm/dist_plane.html.</A>'
        htmlStr = 'the <a href="dist_plane.html">dist_plane</a> document'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '&lt;<A HREF="https://computing.llnl.gov/linux/slurm/mpi_guide.html">https://computing.llnl.gov/linux/slurm/mpi_guide.html</A>&gt;'
        htmlStr = '<a href="mpi_guide.html">mpi_guide</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '(<A HREF="https://computing.llnl.gov/linux/slurm/power_save.html).">https://computing.llnl.gov/linux/slurm/power_save.html).</A>'
        htmlStr = '<a href="power_save.html">power_save</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="https://computing.llnl.gov/linux/slurm/cons_res.html">https://computing.llnl.gov/linux/slurm/cons_res.html</A>'
        htmlStr = '<a href="cons_res.html">cons_res</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="https://computing.llnl.gov/linux/slurm/cons_res_share.html">https://computing.llnl.gov/linux/slurm/cons_res_share.html</A>'
        htmlStr = '<a href="cons_res_share.html">cons_res_share</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="https://computing.llnl.gov/linux/slurm/gang_scheduling.html">https://computing.llnl.gov/linux/slurm/gang_scheduling.html</A>'
        htmlStr = '<a href="gang_scheduling.html">gang_scheduling</a>'
        lineFix = line.replace(manStr,htmlStr)
        if lineFix != line:
            return lineFix
        manStr = '<A HREF="https://computing.llnl.gov/linux/slurm/preempt.html">https://computing.llnl.gov/linux/slurm/preempt.html</A>'
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
  
files = []
for f in sys.argv[3:]:
    posLastDot = f.rfind(".")
    mhtmlname = f[:posLastDot] + ".mhtml"
    cmd = "man2html " + f + "> " + mhtmlname
    os.system(cmd)
    print ">>>>>>> " + mhtmlname
    files.append(mhtmlname)

for filename in files:
    dirname, basefilename = os.path.split(filename)
#    newfilename = basefilename[:-6] + '.html'
    newfilename = filename[:-6] + '.html'
    print 'Converting', filename, '->', newfilename
    shtml = file(filename, 'r')
    html = file(newfilename, 'w')

    lines = file(sys.argv[1], 'r').read()
    lines = lines.replace(".shtml",".html")
    html.write(lines)
#    html.write(<!--#include virtual="header.txt"-->)
    for line in shtml.readlines():
        # Remove html header/footer created by man2html
        if line == "Content-type: text/html\n":
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
    lines = file(sys.argv[2], 'r').read()
    html.write(lines)
#    html.write(<!--#include virtual="footer.txt"-->)
    html.close()
    shtml.close()
    os.remove(filename)
