import setuptools


def readme():
    with open('README.rst') as f:
        return f.read()


class BinaryDistribution(setuptools.dist.Distribution):
    def is_pure(self):
        return False

setuptools.setup(
    name="gringo",
    version="4.4.0.dev1",
    description="Answer Set Programming for Python",
    long_description=readme(),
    url="http://potassco.sourceforge.net/gringo.html",
    maintainer="Alexis Lee",
    maintainer_email="python-gringo@lxsli.co.uk",
    license="GPL v3+",
    data_files=[('lib/python2.7/site-packages', ['gringo.so'])],
    packages=['gringo'],
    package_data={'gringo': ['*']},
    classifiers=[
        "Development Status :: 3 - Alpha",
        "Programming Language :: C++",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: " +
            "GNU General Public License v3 or later (GPLv3+)",
        "Operating System :: POSIX :: Linux",
        "Topic :: Software Development :: Libraries :: Python Modules",
        ],
    keywords='answer set programming asp gringo clingo',
    zip_safe=False,
    distclass=BinaryDistribution,
)
