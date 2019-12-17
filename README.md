# YDBPython

YDBPython provides a Pythonic API for accessing YottaDB databases.

# Requirements
1. Ubuntu Server 18.04 (or similar)
2. Python > 3.6 (f-string and type annotation used)
    1. including the 'python3-dev' package that contains `Python.h`
3. libffi
3. YottaDB

# Installation
0. install Ubuntu Server 18.04

1. install YottaDB per the [Quick Start](https://docs.yottadb.com/MultiLangProgGuide/MultiLangProgGuide.html#quick-start) guide instructions.

    1. you will need to install the following additional packages:

        1. binutils (for `ld` during `sudo ./ydbinstall.sh --utf8 default --verbose`)

        2. pkg-config (for `source $(pkg-config --variable=prefix yottadb)/ydb_env_set` but probably covers things we will need later)

2. Get the code: `git clone https://gitlab.com/gossrock/YDBPython.git`
3. install code:
    1. Install prerequisites: python3-dev and libffi-dev package: `sudo apt install python3-dev libffi-dev`

    2. Set YottaDB environment variables: `source $(pkg-config --variable=prefix yottadb)/ydb_env_set`
    
    3. enter code directory `cd YDBPython/` 

    4. run setup.py to install:
           
        1. option 1: install in venv
            1. Install the python3-venv package: `sudo apt install python3-venv`
            2. create venv: `python3 -m venv .venv`
            3. activate venv: `source .venv/bin/activate`
            4. install into venv: `python setup.py install`
            5. test: `python setup.py test`
            
        2. option 2: install to user
            1. this method requires setuptools: `sudo apt install python3-setuptools`
            2. install for use by user: `python setup.py install --user`
            3. test: `python3 setup.py test`
            
        3. install globally (not suggested):
            1. this method also requires setuptools `sudo apt install python3-setuptools`
            2. install package globally: `sudo -E python3 setup.py install`
            3. test: `python3 setup.py test`

5. enjoy.
