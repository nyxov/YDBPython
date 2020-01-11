#################################################################
#                                                               #
# Copyright (c) 2019 Peter Goss All rights reserved.            #
#                                                               #
# Copyright (c) 2019 YottaDB LLC and/or its subsidiaries.       #
# All rights reserved.                                          #
#                                                               #
#	This source code contains the intellectual property         #
#	of its copyright holder(s), and is made available           #
#	under a license.  If you do not know the terms of           #
#	the license, please stop and do not read further.           #
#                                                               #
#################################################################
from typing import Sequence
import os
import subprocess
import shlex
import pytest # type: ignore

YDB_INSTALL_DIR = os.environ['ydb_dist']
TEST_DATA_DIRECTORY = '/tmp/test_yottadb/'
TEST_GLD = TEST_DATA_DIRECTORY + 'test_db.gld'
TEST_DAT = TEST_DATA_DIRECTORY + 'test_db.dat'

import yottadb
from yottadb import api as api
from yottadb import KeyTuple


API: api.API
try:
    if os.environ['test_ydb_api'] == "SIMPLE":
        API = api.SimpleAPI()
    elif os.environ['test_ydb_api'] == "SIMPLE_THREADED":
        API = api.SimpleThreadedAPI()
except KeyError as e:
    raise KeyError('test_ydb_api envionment valiable must be set to either "SIMPLE" OR "SIMPLE THREADED".')




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
        execute(f'rm {TEST_GLD}')
        execute(f'rm {TEST_DAT}')
        execute(f'rmdir {TEST_DATA_DIRECTORY}')

    os.mkdir(TEST_DATA_DIRECTORY)
    os.environ["ydb_gbldir"] = TEST_GLD
    execute(f'{YDB_INSTALL_DIR}/mumps -run GDE change -segment default -allocation=1000 -file={TEST_DAT}')
    execute(f'{YDB_INSTALL_DIR}/mupip create')


    yield yottadb.Context(api=API)
    print(f'\nTesting was done using using {API.__class__.__name__}')

    #teardown
    execute(f'rm {TEST_GLD}')
    execute(f'rm {TEST_DAT}')
    execute(f'rmdir {TEST_DATA_DIRECTORY}')


SIMPLE_DATA = (
    (KeyTuple('^test1'), 'test1value'),
    (KeyTuple('^test2', ('sub1',)), 'test2value'),
    (KeyTuple('^test3'), 'test3value1'),
    (KeyTuple('^test3', ('sub1',)), 'test3value2'),
    (KeyTuple('^test3', ('sub1', 'sub2')), 'test3value3'),
    (KeyTuple('^test4'), 'test4'),
    (KeyTuple('^test4', ('sub1',)), 'test4sub1'),
    (KeyTuple('^test4', ('sub1', 'subsub1')), 'test4sub1subsub1'),
    (KeyTuple('^test4', ('sub1', 'subsub2')), 'test4sub1subsub2'),
    (KeyTuple('^test4', ('sub1', 'subsub3')), 'test4sub1subsub3'),
    (KeyTuple('^test4', ('sub2',)), 'test4sub2'),
    (KeyTuple('^test4', ('sub2', 'subsub1')), 'test4sub2subsub1'),
    (KeyTuple('^test4', ('sub2', 'subsub2')), 'test4sub2subsub2'),
    (KeyTuple('^test4', ('sub2', 'subsub3')), 'test4sub2subsub3'),
    (KeyTuple('^test4', ('sub3',)), 'test4sub3'),
    (KeyTuple('^test4', ('sub3', 'subsub1')), 'test4sub3subsub1'),
    (KeyTuple('^test4', ('sub3', 'subsub2')), 'test4sub3subsub2'),
    (KeyTuple('^test4', ('sub3', 'subsub3')), 'test4sub3subsub3'),
    (KeyTuple('^Test5'), 'test5value'),
    (KeyTuple('^test6', ('sub6', 'subsub6')), 'test6value'),
)

@pytest.fixture(scope='function')
def simple_data(ydb):
    for key, value in SIMPLE_DATA:
        ydb.set(*key, value=value)

    yield

    for key, value in SIMPLE_DATA:
        ydb.delete_tree(*key)


TREE_DATA = (
    (KeyTuple('^tree1', ('sub1'),), 'tree1.sub1'),
    (KeyTuple('^tree1', ('sub2'),), 'tree1.sub2'),
    (KeyTuple('^tree1', ('sub3'),), 'tree1.sub3'),

    (KeyTuple('^tree2', ('sub1', 'sub1sub1')), 'tree2.sub1.sub1sub1'),
    (KeyTuple('^tree2', ('sub1', 'sub1sub2')), 'tree2.sub1.sub1sub2'),
    (KeyTuple('^tree2', ('sub1', 'sub1sub3')), 'tree2.sub1.sub1sub3'),
    (KeyTuple('^tree2', ('sub2', 'sub2sub1')), 'tree2.sub2.sub2sub1'),
    (KeyTuple('^tree2', ('sub2', 'sub2sub2')), 'tree2.sub2.sub2sub2'),
    (KeyTuple('^tree2', ('sub2', 'sub2sub3')), 'tree2.sub2.sub2sub3'),
    (KeyTuple('^tree2', ('sub3', 'sub3sub1')), 'tree2.sub3.sub3sub1'),
    (KeyTuple('^tree2', ('sub3', 'sub3sub2')), 'tree2.sub3.sub3sub2'),
    (KeyTuple('^tree2', ('sub3', 'sub3sub3')), 'tree2.sub3.sub3sub3'),

    (KeyTuple('^tree3'), 'tree3'),
    (KeyTuple('^tree2', ('sub1',)), 'tree3.sub1'),
    (KeyTuple('^tree2', ('sub1', 'sub1sub1')), 'tree3.sub1.sub1sub1'),
    (KeyTuple('^tree3'), 'tree3'),
    (KeyTuple('^tree2', ('sub2',)), 'tree3.sub2'),
    (KeyTuple('^tree2', ('sub2', 'sub2sub1')), 'tree3.sub2.sub2sub1'),
    (KeyTuple('^tree3'), 'tree3'),
    (KeyTuple('^tree2', ('sub3',)), 'tree3.sub3'),
    (KeyTuple('^tree2', ('sub3', 'sub3sub1')), 'tree3.sub3.sub3sub1'),
)

@pytest.fixture(scope='function')
def tree_data(ydb):

    for key, value in TREE_DATA:
        ydb.set(*key, value=value)

    yield

    for key, value in TREE_DATA:
        ydb.delete_tree(*key)
