# -*- coding: utf-8 -*-

from distutils.core import setup

setup(name         = "python-hostlist",
      version      = "1.3", # Change in hostlist{,.py,.1}, python-hostlist.spec too!
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
                      ],
      py_modules   = ["hostlist"],
      scripts      = ["hostlist"],
      data_files   = [("share/man/man1", ["hostlist.1"])],
      )
