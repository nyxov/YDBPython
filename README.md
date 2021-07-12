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

		0. Setup environment:
			1. Set the `$ydb_dist` environment variable, e.g. `export ydb_dist=*path_to_my_ydb_installation*`

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
        1. Install `pytest`, `pytest-order`, and `psutil`
            1. If `pip` for `python3` is not installed do so: `sudo apt install python3-pip`
            2. Use `pip` to install `pytest`, `pytest-order`, `psutil`
                1. Option 1: install into venv
                    1. Activate `venv` if it is not already: `source .venv/bin/activate`
                    2. Install: `pip install pytest pytest-order psutil`
                2. Option 2: install for user: `pip3 install --user pytest pytest-order psutil`
                3. Option 3: install globally (not suggested): `sudo pip3 install pytest pytest-order psutil`

    5. TODO: add to pypi

5. Enjoy.

# Basic Example Usage

```python
import yottadb

# Create Key objects for conveniently accessing and manipulating database nodes
key1 = yottadb.Key('^hello')  # Create a key referencing the global variable '^hello'

print(f"{key1}: {key1.value}")  # Display current value of '^hello'
key1.value = b'Hello world!'  # Set '^hello' to 'Hello world!'
print(f"{key1}: {key1.value}")

key2 = yottadb.Key('^hello')['cowboy']  # Add a 'cowboy' subscript to the global variable '^hello', creating a new key
key2.value = 'Howdy partner!'  # Set '^hello('cowboy') to 'Howdy partner!'
print(f"{key2}: {key2.value}")

key3 = yottadb.Key('^hello')['chinese']  # Add a second subscript to '^hello', creating a third key
key3.value = bytes('你好世界!', encoding="utf-8")  # The value can be set to anything that can be encoded to `bytes`
print(key3, str(key3.value, encoding="utf-8"))  # Returned values are `bytes` objects, and so may need to be encoded

for subscript in key1.subscripts:  # Loop through all the subscripts of a key
    sub_key = key1[subscript]
    print(f"{sub_key}: {sub_key.value}")

key1.delete_node()  # Delete the value of '^hello', but not any of its child nodes

print(f"{key1}: {key1.value}")  # No value is printed
for subscript in key1.subscripts:  # The values of the child nodes are still in the database
    sub_key = key1[subscript]
    print(f"{sub_key}: {sub_key.value}")

key1.value = 'Hello world!'   # Reset the value of '^hello'
print(f"{key1}: {key1.value}")  # Prints the value
key1.delete_tree() # Delete both the value at the '^hello' node and all of it's children
print(f"{key1}: {key1.value}")  # Prints no value
for subscript in key1.subscripts:  # Loop terminates immediately and displays no subscripts
    sub_key = key1[subscript]
    print(sub_key, sub_key.value)

# Database transactions are also available
@yottadb.transaction
def simple_transaction(value):
	# Set values directly with the set() function
    yottadb.set('test1', value=value)  # Set the local variable 'test1' to the given value
    yottadb.set('test2', value=value)  # Set the local variable 'test2' to the given value
    condition_a = False
    condition_b = False
    if condition_a:
        # When a yottadb.YDBTPRollback exception is raised YottaDB will rollback the transaction
        # and then propagate the exception to the calling code.
        raise yottadb.YDBTPRollback("reason for the rollback")
    elif condition_b:
        # When a yottadb.YDBTPRestart exception is raised YottaDB will call the transaction again.
        # Warning: This code is intentionally simplistic. An infinite loop will occur
        #           if yottadb.YDBTPRestart is continually raised
        raise yottadb.YDBTPRestart()
    else:
        return yottadb.YDB_OK  # Success, transaction will be committed


simple_transaction(b'test', db)
print(f"{db[b'test1']}: {db[b'test1'].value}")
print(f"{db[b'test2']}: {db[b'test2'].value}")
```

# Frequently Asked Questions

## Does YDBPython support multi-threading?

No, YDBPython does not support multithreading. This is due to the limitations of the Python Global Interpreter Lock for CPU-intensive multithreading. For background, see the following resources:
+ Python documentation: [Thread State and the Global Interpreter Lock](https://docs.python.org/3/c-api/init.html#thread-state-and-the-global-interpreter-lock)
+ [Python's GIL - A Hurdle to Multithreaded Program](https://medium.com/python-features/pythons-gil-a-hurdle-to-multithreaded-program-d04ad9c1a63)
+ [Grok the GIL: How to write fast and thread-safe Python](https://opensource.com/article/17/4/grok-gil)
+ YDBPython GitLab discussion: [Issue #7](https://gitlab.com/YottaDB/Lang/YDBPython/-/issues/7)

Accordingly, the Python `threading` and `multithreading` should be avoided when developing applications with YDBPython. However, YDBPython does support multiprocessing and may be safely used with the Python `multiprocessing` library for parallelism. For an example of `multiprocessing` usage, see `tests/test_threeenp1.py`.
