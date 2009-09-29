# -*- coding: utf-8 -*-

from distutils.core import setup

# Python 2/3 installation trick from .../Demo/distutils/test2to3
try:
    from distutils.command.build_py import build_py_2to3 as build_py
except ImportError:
    from distutils.command.build_py import build_py

try:
    from distutils.command.build_scripts import build_scripts_2to3 as build_scripts
except ImportError:
    from distutils.command.build_scripts import build_scripts

# Version
VERSION = "1.5"
if "#" in VERSION:
    import sys
    sys.stderr.write("Bad version %s\n" % VERSION)
    sys.exit(1)


setup(name         = "python-hostlist",
      version      = VERSION,
      description  = "Python module for hostlist handling",
      long_description = "The hostlist.py module knows how to expand and collect hostlist expressions.",
      author       = "Kent Engström",
      author_email = "kent@nsc.liu.se",
      url          = "http://www.nsc.liu.se/~kent/python-hostlist/",
      license      = "GPL2+",
      classifiers  = ['Development Status :: 5 - Production/Stable',
                      'Intended Audience :: Science/Research',
                      'Intended Audience :: System Administrators',
                      'License :: OSI Approved :: GNU General Public License (GPL)',
                      'Topic :: System :: Clustering',
                      'Topic :: System :: Systems Administration',
                      'Programming Language :: Python :: 2',
                      'Programming Language :: Python :: 3',
                      ],
      py_modules   = ["hostlist"],
      scripts      = ["hostlist", "hostgrep"],
      data_files   = [("share/man/man1", ["hostlist.1",
                                          "hostgrep.1"])],
      cmdclass     = {'build_py':build_py,
                      'build_scripts':build_scripts,
                      }
      )
