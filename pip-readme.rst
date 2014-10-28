This source distributable packages clingo-4.4.0 [1]_. You will need several
libraries available for it to install correctly. You should end up with
something like this::

    <virtualenv>/lib/python2.7/site-packages
    ├── gringo-4.4.0.dev1
    │   ├── CHANGES
    │   ├── COPYING
    │   ├── INSTALL
    │   ├── NOTES
    │   └── README
    ├── gringo-4.4.0.dev1-py2.7.egg-info
    │   ├── dependency_links.txt
    │   ├── installed-files.txt
    │   ├── not-zip-safe
    │   ├── PKG-INFO
    │   ├── SOURCES.txt
    │   └── top_level.txt
    └── gringo.so

To use it, simply "import gringo". Documentation is here [2]_.

Requirements
============

- a c++11 conforming compiler
    gcc version 4.8 (earlier versions will not work)
    clang version 3.1 (using either libstdc++ provided by gcc 4.8 or libc++)
    other compilers might work
- the bison parser generator
    version 3.0 is tested (produces warnings to stay backwards-compatible)
    version 2.5 and newer should work (earlier versions will not work)
- the re2c lexer generator
    version 0.13.5 is tested
    newer versions should work
- the scons build system
    version 2.2 is tested
    version 2.1 and newer should work
- the python script language
    version 2.7 is tested
- the tbb library
    version 4.0+r233 is tested
    newer versions should work (not libtbb2)

.. [1] http://sourceforge.net/projects/potassco/files/clingo/4.4.0/
.. [2] http://potassco.sourceforge.net/gringo.html
