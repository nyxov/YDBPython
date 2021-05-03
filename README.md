# YDBPython

*Note:* YDBPython is currently in Alpha testing and is *not* advised for production use.

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

2. Get the code: `git clone https://gitlab.com/YottaDB/Lang/YDBPython.git`
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

# create a Context object that controls access to the database (you should only use one)
db = yottadb.Context()

key1 = db[b'^hello'] # create a key that has the global variable '^hello'

print(f"{key1}: {key1.value}") # display current value of '^hello'
key1.value = b'Hello world!' # set '^hello' to 'Hello world!'
print(f"{key1}: {key1.value}")

key2 = db[b'^hello'][b'cowboy'] # add a subscript 'cowboy' to the global variable '^hello'
key2.value = b'Howdy partner!' # set '^hello('cowboy') to 'Howdy partner!'
print(f"{key2}: {key2.value}")

key3 = db[b'^hello'][b'chinese'] # add a second subscript to '^hello'
key3.value = bytes('你好世界!', encoding="utf-8") # the value can be set to anything that can be encoded to bytes
print(key3, str(key3.value, encoding="utf-8")) # at this time you will need to handle the encoding and decoding


for subscript in key1.subscripts: # you can loop through all the subscripts of a key
    sub_key = key1[subscript]
    print(f"{sub_key}: {sub_key.value}")

key1.delete_node() # delete the value of '^hello' but not any of its subscripts

print(f"{key1}: {key1.value}") # should show nothing
for subscript in key1.subscripts: # the values of the subscripts should still be in the database
    sub_key = key1[subscript]
    print(f"{sub_key}: {sub_key.value}")


key1.value = b'Hello world!'
print(f"{key1}: {key1.value}")
key1.delete_tree() # delete both the value at the '^hello' node and all of it's subscripts
print(f"{key1}: {key1.value}") # show nothing in the value
for subscript in key1.subscripts: # displays no subscripts
    sub_key = key1[subscript]
    print(sub_key, sub_key.value)

#transactions are also available
@yottadb.transaction
def simple_transaction(value, context): # the final argument of a transaction is the current context
    context[b'test1'].value = value
    context[b'test2'].value = value
    some_condition_a = False
    some_condition_b = False
    if some_condition_a:
        # When yottadb.YDBTPRollback is raised YottaDB will rollback the transaction
        # and then propagate the exception to the calling code.
        raise yottadb.YDBTPRollback("reason for the rollback")
    elif some_condition_b:
        # When yottadb.YDBTPRestart is raised YottaDB will call the transaction again.
        # Warning: This code is intentionally simplistic. An infinite loop will occur
        #           if yottadb.YDBTPRestart is continually raised
        raise yottadb.YDBTPRestart()
    else:
        return yottadb.YDB_OK # indicates success, transaction will be committed


simple_transaction(b'test', db)
print(f"{db[b'test1']}: {db[b'test1'].value}")
print(f"{db[b'test2']}: {db[b'test2'].value}")
```
