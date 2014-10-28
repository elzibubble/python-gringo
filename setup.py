import setuptools
import subprocess
import sys

CLINGO_SRC = 'clingo-4.4.0-source'
VERSION = "4.4.0.dev1"


def readme():
    with open('pip-readme.rst') as f:
        return f.read()

if "--compile" in sys.argv:
    print subprocess.check_output(
        ["scons", "--build=release", "pyclingo"],
        cwd=CLINGO_SRC)

setuptools.setup(
    name="gringo",
    version=VERSION,
    description="Answer Set Programming for Python",
    long_description=readme(),
    url="http://potassco.sourceforge.net/gringo.html",
    maintainer="Alexis Lee",
    maintainer_email="python-gringo@lxsli.co.uk",
    license="GPL v3+",
    data_files=[('lib/python2.7/site-packages',
                [CLINGO_SRC + '/build/release/python/gringo.so']),
                ('lib/python2.7/site-packages/gringo-' + VERSION,
                [(CLINGO_SRC + "/" + f) for f in
                    ['CHANGES', 'COPYING', 'INSTALL', 'NOTES', 'README']])],
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Programming Language :: C++",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: " +
            "GNU General Public License v3 or later (GPLv3+)",
        "Operating System :: POSIX :: Linux",
        "Topic :: Software Development :: Libraries :: Python Modules",
        ],
    keywords='answer set programming asp gringo clasp clingo',
    zip_safe=False,
)
