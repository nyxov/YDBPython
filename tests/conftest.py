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
from typing import Sequence
import os
import shutil
import subprocess
import shlex
import pytest  # type: ignore # ignore due to pytest not having type annotations

import yottadb
from yottadb import KeyTuple

YDB_INSTALL_DIR = os.environ["ydb_dist"]
TEST_DATA_DIRECTORY = "/tmp/test_yottadb/"
TEST_GLD = TEST_DATA_DIRECTORY + "test_db.gld"
TEST_DAT = TEST_DATA_DIRECTORY + "test_db.dat"

SIMPLE_DATA = (
    (KeyTuple(b"^test1"), b"test1value"),
    (KeyTuple(b"^test2", (b"sub1",)), b"test2value"),
    (KeyTuple(b"^test3"), b"test3value1"),
    (KeyTuple(b"^test3", (b"sub1",)), b"test3value2"),
    (KeyTuple(b"^test3", (b"sub1", b"sub2")), b"test3value3"),
    (KeyTuple(b"^test4"), b"test4"),
    (KeyTuple(b"^test4", (b"sub1",)), b"test4sub1"),
    (KeyTuple(b"^test4", (b"sub1", b"subsub1")), b"test4sub1subsub1"),
    (KeyTuple(b"^test4", (b"sub1", b"subsub2")), b"test4sub1subsub2"),
    (KeyTuple(b"^test4", (b"sub1", b"subsub3")), b"test4sub1subsub3"),
    (KeyTuple(b"^test4", (b"sub2",)), b"test4sub2"),
    (KeyTuple(b"^test4", (b"sub2", b"subsub1")), b"test4sub2subsub1"),
    (KeyTuple(b"^test4", (b"sub2", b"subsub2")), b"test4sub2subsub2"),
    (KeyTuple(b"^test4", (b"sub2", b"subsub3")), b"test4sub2subsub3"),
    (KeyTuple(b"^test4", (b"sub3",)), b"test4sub3"),
    (KeyTuple(b"^test4", (b"sub3", b"subsub1")), b"test4sub3subsub1"),
    (KeyTuple(b"^test4", (b"sub3", b"subsub2")), b"test4sub3subsub2"),
    (KeyTuple(b"^test4", (b"sub3", b"subsub3")), b"test4sub3subsub3"),
    (KeyTuple(b"^Test5"), b"test5value"),
    (KeyTuple(b"^test6", (b"sub6", b"subsub6")), b"test6value"),
)


def execute(command: str, stdin: str = "") -> str:
    """
    A utility function to simplify the running of shell commands.

    :param command: the command to run
    :param stdin: optional text that may be piped to the command
    :return: returns standard out decoded as a string.
    """
    process = subprocess.Popen(shlex.split(command), stdout=subprocess.PIPE, stdin=subprocess.PIPE, stderr=subprocess.PIPE)
    return process.communicate(stdin.encode())[0].decode().strip()


@pytest.fixture(scope="session")
def ydb():
    """
    A pytest fixture that sets up a test database, yields a yottadb.Context object for
    the test to use to access that database and then deletes the database when the
    testing session is complete.
    """
    # setup
    if os.path.exists(TEST_DATA_DIRECTORY):  # clean up previous test if it failed to do so previously
        shutil.rmtree(TEST_DATA_DIRECTORY)

    os.mkdir(TEST_DATA_DIRECTORY)
    os.environ["ydb_gbldir"] = TEST_GLD
    execute(f"{YDB_INSTALL_DIR}/mumps -run GDE change -segment default -allocation=1000 -file={TEST_DAT} -null_subscripts=always")
    execute(f"{YDB_INSTALL_DIR}/mupip create")

    yield yottadb.Context()

    # teardown
    shutil.rmtree(TEST_DATA_DIRECTORY)


@pytest.fixture(scope="function")
def simple_data(ydb):
    """
    A pytest fixture that adds the above SIMPLE_DATA tuple to the testing database and then
    deletes that data. This fixture is in function scope so it will be deleted after each
    test that uses it.
    """
    for key, value in SIMPLE_DATA:
        ydb.set(*key, value=value)

    yield

    for key, value in SIMPLE_DATA:
        ydb.delete_tree(*key)
