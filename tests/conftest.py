#################################################################
#                                                               #
# Copyright (c) 2019-2021 Peter Goss All rights reserved.       #
#                                                               #
# Copyright (c) 2019-2021 YottaDB LLC and/or its subsidiaries.  #
# All rights reserved.                                          #
#                                                               #
#   This source code contains the intellectual property         #
#   of its copyright holder(s), and is made available           #
#   under a license.  If you do not know the terms of           #
#   the license, please stop and do not read further.           #
#                                                               #
#################################################################
import os
import shutil
import subprocess
import shlex
import time
import sys
import pytest  # type: ignore # ignore due to pytest not having type annotations
from typing import Union

import yottadb
from yottadb import KeyTuple

YDB_INSTALL_DIR = os.environ["ydb_dist"]
TEST_DATA_DIRECTORY = "/tmp/test_yottadb/"
TEST_GLD = TEST_DATA_DIRECTORY + "test_db.gld"
TEST_DAT = TEST_DATA_DIRECTORY + "test_db.dat"

SIMPLE_DATA = (
    (KeyTuple("^test1"), "test1value"),
    (KeyTuple("^test2", ("sub1",)), "test2value"),
    (KeyTuple("^test3"), "test3value1"),
    (KeyTuple("^test3", ("sub1",)), "test3value2"),
    (KeyTuple("^test3", ("sub1", "sub2")), "test3value3"),
    (KeyTuple("^test4"), "test4"),
    (KeyTuple("^test4", ("sub1",)), "test4sub1"),
    (KeyTuple("^test4", ("sub1", "subsub1")), "test4sub1subsub1"),
    (KeyTuple("^test4", ("sub1", "subsub2")), "test4sub1subsub2"),
    (KeyTuple("^test4", ("sub1", "subsub3")), "test4sub1subsub3"),
    (KeyTuple("^test4", ("sub2",)), "test4sub2"),
    (KeyTuple("^test4", ("sub2", "subsub1")), "test4sub2subsub1"),
    (KeyTuple("^test4", ("sub2", "subsub2")), "test4sub2subsub2"),
    (KeyTuple("^test4", ("sub2", "subsub3")), "test4sub2subsub3"),
    (KeyTuple("^test4", ("sub3",)), "test4sub3"),
    (KeyTuple("^test4", ("sub3", "subsub1")), "test4sub3subsub1"),
    (KeyTuple("^test4", ("sub3", "subsub2")), "test4sub3subsub2"),
    (KeyTuple("^test4", ("sub3", "subsub3")), "test4sub3subsub3"),
    (KeyTuple("^Test5"), "test5value"),
    (KeyTuple("^test6", ("sub6", "subsub6")), "test6value"),
)


str2zwr_tests = [
    (b"X\0ABC", b'"X"_$C(0)_"ABC"', b'"X"_$C(0)_"ABC"'),
    (
        bytes("你好世界", encoding="utf-8"),
        b'"\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8"_$C(150)_"\xe7"_$C(149,140)',
        b'"\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8\x96\xe7\x95\x8c"',
    ),
]


# Lock a value in the database
def lock_value(key: Union[yottadb.Key, KeyTuple], interval: int = 2, timeout: int = 1):
    if isinstance(key, yottadb.Key):
        varname = key.varname
        subsarray = key.subsarray
    else:
        varname = key[0]
        subsarray = key[1]
    if len(key.subsarray) == 0:
        subsarray = None

    has_lock = False
    try:
        yottadb.lock_incr(varname, subsarray, timeout_nsec=(timeout * 1_000_000_000))
        print("Lock Success")
        has_lock = True
    except yottadb.YDBTimeoutError:
        print("Lock Failed")
        sys.exit(1)
    except Exception as e:
        print(f"Lock Error: {repr(e)}")
        sys.exit(2)

    if has_lock:
        time.sleep(interval)
        yottadb.lock_decr(varname, subsarray)
        if timeout != 0 or interval != 0:
            print("Lock Released")

    sys.exit(0)


def execute(command: str, stdin: str = "") -> str:
    """
    A utility function to simplify the running of shell commands.

    :param command: the command to run
    :param stdin: optional text that may be piped to the command
    :return: returns standard out decoded as a string.
    """
    process = subprocess.Popen(shlex.split(command), stdout=subprocess.PIPE, stdin=subprocess.PIPE, stderr=subprocess.PIPE)
    return process.communicate(stdin.encode())[0].decode().strip()


@pytest.fixture(scope="function")
def simple_data():
    """
    A pytest fixture that creates a test database, adds the above SIMPLE_DATA tuple to that
    database, and deletes the data and database after testing is complete. This fixture is
    in function scope so it will be executed for each test that uses it.

    Note that pytest runs tests sequentially, so it is safe for the test database to be
    updated for each test function.
    """

    # setup
    if os.path.exists(TEST_DATA_DIRECTORY):  # clean up previous test if it failed to do so previously
        shutil.rmtree(TEST_DATA_DIRECTORY)

    os.mkdir(TEST_DATA_DIRECTORY)
    os.environ["ydb_gbldir"] = TEST_GLD
    execute(f"{YDB_INSTALL_DIR}/mumps -run GDE change -segment default -allocation=1000 -file={TEST_DAT} -null_subscripts=always")
    execute(f"{YDB_INSTALL_DIR}/mupip create")

    for key, value in SIMPLE_DATA:
        yottadb.set(*key, value=value)

    yield

    for key, value in SIMPLE_DATA:
        yottadb.delete_tree(*key)

    # teardown
    shutil.rmtree(TEST_DATA_DIRECTORY)
