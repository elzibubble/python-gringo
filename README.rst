python-gringo
=============

Download, activate the virtualenv of your choice, then::

  python setup.py sdist
  pip install dist/gringo-*.tar.gz

This solution includes the whole clingo source [1]_. It should be fairly simple
to swap in a new version, just be sure to set up build/release.py correctly.

.. [1] http://sourceforge.net/projects/potassco/files/clingo/4.4.0/
