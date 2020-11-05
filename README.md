# YDBPython

YDBPython provides a Pythonic API for accessing YottaDB databases.

# Requirements
1. Ubuntu Server 18.04 (or similar)
2. Python > 3.6 (f-string and type annotation used)
    1. including the 'python3-dev' package that contains `Python.h`
3. libffi
3. YottaDB

# Installation
0. Install Ubuntu Server 18.04

1. Install YottaDB per the [Quick Start](https://docs.yottadb.com/MultiLangProgGuide/MultiLangProgGuide.html#quick-start) guide instructions or from [source](https://gitlab.com/YottaDB/DB/YDB)

    Note: Ubuntu Server 20.04 will require YottaDB 1.29 or later. 

2. Get the code: `git clone https://gitlab.com/gossrock/YDBPython.git`
3. Install code:
    1. Install prerequisites: python3-dev and libffi-dev package: `sudo apt install python3-dev libffi-dev`

    2. Set YottaDB environment variables: `source $(pkg-config --variable=prefix yottadb)/ydb_env_set`
    
    3. Enter code directory `cd YDBPython/` 

    4. Run setup.py to install:
           
        1. Option 1: install in venv
            1. Install the python3-venv package: `sudo apt install python3-venv`
            2. Create venv: `python3 -m venv .venv`
            3. Activate venv: `source .venv/bin/activate`
            4. Install into venv: `python setup.py install`

        2. Option 2: install to user
            1. This method requires setuptools: `sudo apt install python3-setuptools`
            2. Install for use by user: `python setup.py install --user`
                        
        3. Install globally (not suggested):
            1. This method also requires setuptools `sudo apt install python3-setuptools`
            2. Install package globally: `sudo -E python3 setup.py install`
            
    5. Run tests:
        1. Install `pytest` and `psutil`
            1. If `pip` for `python3` is not installed do so: `sudo apt install python3-pip`
            2. Use `pip` to install `pytest` and `psutil`
                1. Option 1: install into venv
                    1. Activate `venv` if it is not already: `source .venv/bin/activate`
                    2. Install: `pip install pytest psutil`
                2. Option 2: install for user: `pip3 install --user pytest`
                3. Option 3: install globally (not suggested): `sudo pip3 install pytest`
                      
    5. TODO: add to pypi

5. Enjoy.

# Basic Example Usage

```python
import yottadb

# create a Context object that will help with keeping track of tp_tokens and other database related settings.
db = yottadb.Context() 

key1 = db['^hello'] # create a key that has the global varname '^hello'
print(key1, key1.value) 
key1.value = 'Hello world!'
print(key1, key1.value)

key2 = db['^hello']['cowboy']
key2.value = 'Howdy partner!'
print(key2, key2.value)

key3 = db['^hello']['chinese']
key3.value = '你好世界!'
print(key3, key3.value)

for subscript in key1.subscripts:
    sub_key = key1[subscript]
    print(sub_key, sub_key.value) 

key1.delete_node()

print(key1, key1.value)
for subscript in key1.subscripts:
    sub_key = key1[subscript]
    print(sub_key, sub_key.value)

key1.delete_tree()
for subscript in key1.subscripts:
    sub_key = key1[subscript]
    print(sub_key, sub_key.value)
```
