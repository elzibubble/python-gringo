python-gringo
=============

Download, activate the virtualenv of your choice, then::

  python2 setup.py sdist
  pip2 install dist/gringo-*.tar.gz

This solution includes the whole clingo source [1]_. It should be fairly simple
to swap in a new version, just be sure to set up build/release.py correctly.

There is an exemple of release.py that compile clingo with multithread (TBB downloadable [here](http://www.threadingbuildingblocks.org/) if not in your repository) and python2.7 support:

        CXX = 'g++'
        CXXFLAGS = ['-std=c++11', '-O3', '-Wall']
        CPPPATH = ['/usr/include/python2.7', '/path/to/tbb/tbb43_20150316oss/include']
        # path depends of your system.
        CPPDEFINES = {'NDEBUG': 1}
        LIBS = ['dl']
        LIBPATH = ['/path/to/tbb/tbb43_20150316oss/lib/intel64/gcc4.4']
        LINKFLAGS = ['-std=c++11', '-O3']
        RPATH = []
        AR = 'ar'
        ARFLAGS = ['rc']
        RANLIB = 'ranlib'
        BISON = 'bison'
        RE2C = 're2c'
        WITH_PYTHON = 'python2.7'
        WITH_LUA = None
        WITH_TBB = 'tbb'
        WITH_CPPUNIT = None


.. [1] http://sourceforge.net/projects/potassco/files/clingo/4.5.0/
