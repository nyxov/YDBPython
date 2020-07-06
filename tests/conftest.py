#################################################################
#                                                               #
# Copyright (c) 2019 Peter Goss All rights reserved.            #
#                                                               #
# Copyright (c) 2019 YottaDB LLC and/or its subsidiaries.       #
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
import pytest # type: ignore

YDB_INSTALL_DIR = os.environ['ydb_dist']
TEST_DATA_DIRECTORY = '/tmp/test_yottadb/'
TEST_GLD = TEST_DATA_DIRECTORY + 'test_db.gld'
TEST_DAT = TEST_DATA_DIRECTORY + 'test_db.dat'

import yottadb
from yottadb import KeyTuple



def execute(command: str, stdin: str = "") -> str:
    process = subprocess.Popen(shlex.split(command), stdout=subprocess.PIPE, stdin=subprocess.PIPE, stderr= subprocess.PIPE)
    return process.communicate(stdin.encode())[0].decode().strip()

@pytest.fixture(scope="session")
def ydb():
    #setup

    # for some strange reason ydb will not want to run with
    #   both ydb_gbldir set to /tmp/test_yottadb/test_db.gld
    #   and being run from the same directory as the project.
    os.chdir("..") # so changing to the parent directory to run the tests

    if os.path.exists(TEST_DATA_DIRECTORY): # clean up previous test if it failed to do so previously
        shutil.rmtree(TEST_DATA_DIRECTORY)

    os.mkdir(TEST_DATA_DIRECTORY)
    os.environ["ydb_gbldir"] = TEST_GLD
    execute(f'{YDB_INSTALL_DIR}/mumps -run GDE change -segment default -allocation=1000 -file={TEST_DAT}')
    execute(f'{YDB_INSTALL_DIR}/mupip create')


    yield yottadb.Context()

    #teardown
    shutil.rmtree(TEST_DATA_DIRECTORY)


SIMPLE_DATA = (
    (KeyTuple(b'^test1'), b'test1value'),
    (KeyTuple(b'^test2', (b'sub1',)), b'test2value'),
    (KeyTuple(b'^test3'), b'test3value1'),
    (KeyTuple(b'^test3', (b'sub1',)), b'test3value2'),
    (KeyTuple(b'^test3', (b'sub1', b'sub2')), b'test3value3'),
    (KeyTuple(b'^test4'), b'test4'),
    (KeyTuple(b'^test4', (b'sub1',)), b'test4sub1'),
    (KeyTuple(b'^test4', (b'sub1', b'subsub1')), b'test4sub1subsub1'),
    (KeyTuple(b'^test4', (b'sub1', b'subsub2')), b'test4sub1subsub2'),
    (KeyTuple(b'^test4', (b'sub1', b'subsub3')), b'test4sub1subsub3'),
    (KeyTuple(b'^test4', (b'sub2',)), b'test4sub2'),
    (KeyTuple(b'^test4', (b'sub2', b'subsub1')), b'test4sub2subsub1'),
    (KeyTuple(b'^test4', (b'sub2', b'subsub2')), b'test4sub2subsub2'),
    (KeyTuple(b'^test4', (b'sub2', b'subsub3')), b'test4sub2subsub3'),
    (KeyTuple(b'^test4', (b'sub3',)), b'test4sub3'),
    (KeyTuple(b'^test4', (b'sub3', b'subsub1')), b'test4sub3subsub1'),
    (KeyTuple(b'^test4', (b'sub3', b'subsub2')), b'test4sub3subsub2'),
    (KeyTuple(b'^test4', (b'sub3', b'subsub3')), b'test4sub3subsub3'),
    (KeyTuple(b'^Test5'), b'test5value'),
    (KeyTuple(b'^test6', (b'sub6', b'subsub6')), b'test6value'),
)

@pytest.fixture(scope='function')
def simple_data(ydb):
    for key, value in SIMPLE_DATA:
        ydb.set(*key, value=value)

    yield

    for key, value in SIMPLE_DATA:
        ydb.delete_tree(*key)


TREE_DATA = (
    (KeyTuple(b'^tree1', (b'sub1'),), b'tree1.sub1'),
    (KeyTuple(b'^tree1', (b'sub2'),), b'tree1.sub2'),
    (KeyTuple(b'^tree1', (b'sub3'),), b'tree1.sub3'),

    (KeyTuple(b'^tree2', (b'sub1', b'sub1sub1')), b'tree2.sub1.sub1sub1'),
    (KeyTuple(b'^tree2', (b'sub1', b'sub1sub2')), b'tree2.sub1.sub1sub2'),
    (KeyTuple(b'^tree2', (b'sub1', b'sub1sub3')), b'tree2.sub1.sub1sub3'),
    (KeyTuple(b'^tree2', (b'sub2', b'sub2sub1')), b'tree2.sub2.sub2sub1'),
    (KeyTuple(b'^tree2', (b'sub2', b'sub2sub2')), b'tree2.sub2.sub2sub2'),
    (KeyTuple(b'^tree2', (b'sub2', b'sub2sub3')), b'tree2.sub2.sub2sub3'),
    (KeyTuple(b'^tree2', (b'sub3', b'sub3sub1')), b'tree2.sub3.sub3sub1'),
    (KeyTuple(b'^tree2', (b'sub3', b'sub3sub2')), b'tree2.sub3.sub3sub2'),
    (KeyTuple(b'^tree2', (b'sub3', b'sub3sub3')), b'tree2.sub3.sub3sub3'),

    (KeyTuple(b'^tree3'), b'tree3'),
    (KeyTuple(b'^tree2', (b'sub1',)), b'tree3.sub1'),
    (KeyTuple(b'^tree2', (b'sub1', b'sub1sub1')), b'tree3.sub1.sub1sub1'),
    (KeyTuple(b'^tree3'), b'tree3'),
    (KeyTuple(b'^tree2', (b'sub2',)), b'tree3.sub2'),
    (KeyTuple(b'^tree2', (b'sub2', b'sub2sub1')), b'tree3.sub2.sub2sub1'),
    (KeyTuple(b'^tree3'), b'tree3'),
    (KeyTuple(b'^tree2', (b'sub3',)), b'tree3.sub3'),
    (KeyTuple(b'^tree2', (b'sub3', b'sub3sub1')), b'tree3.sub3.sub3sub1'),
)

@pytest.fixture(scope='function')
def tree_data(ydb):

    for key, value in TREE_DATA:
        ydb.set(*key, value=value)

    yield

    for key, value in TREE_DATA:
        ydb.delete_tree(*key)

@pytest.fixture
def simple_reset_test_data(ydb):
    ydb.set(b'resetattempt', value=b'0')
    ydb.set(b'resetvalue', value=b'0')
    yield
    ydb.delete_node(b'resetattempt')
    ydb.delete_node(b'resetvalue')
