all: compile install

compile:
	cd ./clingo-4.5.1-source && $(MAKE) compile

install:
	python2 setup.py sdist
	pip install --upgrade --user dist/gringo-*.tar.gz
