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
import pytest # type: ignore

import subprocess
import os
import shlex
import datetime
import time
from decimal import Decimal
from typing import NamedTuple, Sequence, Optional

from conftest import execute
from lock import key_tuple_to_str

import _yottadb
from yottadb import KeyTuple, NOTTP

YDB_INSTALL_DIR = os.environ['ydb_dist']
TEST_DATA_DIRECTORY = '/tmp/test_yottadb/'
TEST_GLD = TEST_DATA_DIRECTORY + 'test_db.gld'
TEST_DAT = TEST_DATA_DIRECTORY + 'test_db.dat'


@pytest.fixture(scope="function")
def bank():
    account1 = b'acc#1234'
    account2 = b'acc#5678'
    account1_balance = 1234
    account2_balance = 5678
    transfer_amount = 10
    _yottadb.set(varname=b'^account', subsarray=(account1, b'balance'), value=bytes(str(account1_balance), encoding='utf-8'))
    _yottadb.set(varname=b'^account', subsarray=(account2, b'balance'), value=bytes(str(account2_balance), encoding='utf-8'))

    yield [{'account#':account1, 'account_balance':account1_balance}, {'account#':account2, 'account_balance':account2_balance}]

    _yottadb.delete(varname=b'^account', delete_type=_yottadb.YDB_DEL_TREE)

def test_get_1_positional(simple_data):
    assert _yottadb.get(b'^test1') == b'test1value'
    
def test_get_1_keywords(simple_data):
    assert _yottadb.get(varname=b'^test1') == b'test1value'
    assert _yottadb.get(varname=b'^test1', tp_token=0) == b'test1value'
    
def test_get_2_positional(simple_data):
    assert _yottadb.get(b'^test2', [b'sub1']) == b'test2value'

def test_get_2_keywords(simple_data):
    assert _yottadb.get(varname=b'^test2', subsarray=[b'sub1']) == b'test2value'

def test_get_3_positional(simple_data):
    assert _yottadb.get(b'^test3') == b'test3value1'
    assert _yottadb.get(b'^test3', [b'sub1']) == b'test3value2'
    assert _yottadb.get(b'^test3', [b'sub1', b'sub2']) == b'test3value3'

def test_get_3_keywords(simple_data):
    assert _yottadb.get(varname=b'^test3') == b'test3value1'
    assert _yottadb.get(varname=b'^test3', subsarray=[b'sub1']) == b'test3value2'
    assert _yottadb.get(varname=b'^test3', subsarray=[b'sub1', b'sub2']) == b'test3value3'
    
def test_get_YDBErrors(simple_data):
    with pytest.raises(_yottadb.YDBError):
        _yottadb.get(b'^testerror')
    with pytest.raises(_yottadb.YDBError):
        _yottadb.get(b'testerror')
    with pytest.raises(_yottadb.YDBError):
        _yottadb.get(b'^testerror', [b'sub1'])

def test_get_long_value():
    _yottadb.set(varname=b'testlong', value=(b'a'*_yottadb.YDB_MAX_STR))
    assert _yottadb.get(varname=b'testlong') == b'a'*_yottadb.YDB_MAX_STR

def test_set_1_positional():
    _yottadb.set(b'test4', value=b'test4value')
    assert _yottadb.get(b'test4') == b'test4value'

def test_set_1_keywords():
    _yottadb.set(varname=b'test5', value=b'test5value')
    assert _yottadb.get(b'test5') == b'test5value'

def test_set_2_positional():
    _yottadb.set(b'test6', (b'sub1',), b'test6value')
    assert _yottadb.get(b'test6', (b'sub1',)) == b'test6value'

def test_set_2_keywords():
    _yottadb.set(varname=b'test7', subsarray=(b'sub1',), value=b'test7value')
    assert _yottadb.get(b'test7', (b'sub1',)) == b'test7value'

def test_set_YDBErrors():
    with pytest.raises(_yottadb.YDBError):
        _yottadb.set(b'a'*32, value=b"some_value")

def test_set_i18n():
    _yottadb.set(varname=b'testchinese', value=bytes('你好世界', encoding='utf-8'))
    assert _yottadb.get(b'testchinese') == bytes('你好世界', encoding='utf-8')

def test_delete_1_positional():
    _yottadb.set(varname=b'test8', value=b'test8value')
    assert _yottadb.get(b'test8') == b'test8value'
    _yottadb.delete(b'test8')
    with pytest.raises(_yottadb.YDBError):
        _yottadb.get(b'test8')

def test_delete_1_keywords():
    _yottadb.set(varname=b'test9', value=b'test9value')
    assert _yottadb.get(b'test9') == b'test9value'
    _yottadb.delete(varname=b'test9', delete_type=_yottadb.YDB_DEL_NODE)
    with pytest.raises(_yottadb.YDBError):
        _yottadb.get(b'test9')

def test_delete_2_positional():
    _yottadb.set(varname=b'test10', subsarray=(b'sub1',), value=b'test10value')
    assert _yottadb.get(b'test10', (b'sub1',)) == b'test10value'
    _yottadb.delete(b'test10', (b'sub1',))
    with pytest.raises(_yottadb.YDBError):
        _yottadb.get(b'test10', (b'sub1',))

def test_delete_2_keywords():
    _yottadb.set(varname=b'test11', subsarray=(b'sub1',),  value=b'test11value')
    assert _yottadb.get(b'test11', (b'sub1',)) == b'test11value'
    _yottadb.delete(varname=b'test11', subsarray=(b'sub1',), delete_type=_yottadb.YDB_DEL_NODE)
    with pytest.raises(_yottadb.YDBError):
        _yottadb.get(b'test11', (b'sub1',))

def test_delete_3_positional():
    _yottadb.set(varname=b'test12', value=b'test12 node value')
    _yottadb.set(varname=b'test12', subsarray=(b'sub1',), value=b'test12 subnode value')
    assert _yottadb.get(b'test12') == b'test12 node value'
    assert _yottadb.get(b'test12', (b'sub1',)) == b'test12 subnode value'

    _yottadb.delete(b'test12', (), _yottadb.YDB_DEL_TREE)

    with pytest.raises(_yottadb.YDBError):
        _yottadb.get(b'test12')
    with pytest.raises(_yottadb.YDBError):
        _yottadb.get(b'test12', (b'sub1',))

def test_delete_3_keywords():
    _yottadb.set(varname=b'test13', value=b'test13 node value')
    _yottadb.set(varname=b'test13', subsarray=(b'sub1',), value=b'test13 subnode value')
    assert _yottadb.get(b'test13') == b'test13 node value'
    assert _yottadb.get(b'test13', (b'sub1',)) == b'test13 subnode value'

    _yottadb.delete(varname=b'test13', delete_type=_yottadb.YDB_DEL_TREE)

    with pytest.raises(_yottadb.YDBError):
        _yottadb.get(b'test13')
    with pytest.raises(_yottadb.YDBError):
        _yottadb.get(b'test13', (b'sub1',))


def test_data_positional(simple_data):
    assert _yottadb.data(b'^nodata') == _yottadb.YDB_DATA_NO_DATA
    assert _yottadb.data(b'^test1') == _yottadb.YDB_DATA_HAS_VALUE_NO_TREE
    assert _yottadb.data(b'^test2') == _yottadb.YDB_DATA_NO_VALUE_HAS_TREE
    assert _yottadb.data(b'^test2', (b'sub1',)) == _yottadb.YDB_DATA_HAS_VALUE_NO_TREE
    assert _yottadb.data(b'^test3') == _yottadb.YDB_DATA_HAS_VALUE_HAS_TREE
    assert _yottadb.data(b'^test3', (b'sub1',)) == _yottadb.YDB_DATA_HAS_VALUE_HAS_TREE
    assert _yottadb.data(b'^test3', (b'sub1', b'sub2')) == _yottadb.YDB_DATA_HAS_VALUE_NO_TREE

def test_lock_incr_1():
    t1 = datetime.datetime.now()
    _yottadb.lock_incr(b'test1')
    t2 = datetime.datetime.now()
    time_elapse = t2.timestamp() - t1.timestamp()
    assert time_elapse < 0.01
    _yottadb.lock_decr(b'test1')

def test_lock_incr_2():
    t1 = datetime.datetime.now()
    _yottadb.lock_incr(b'test2', (b'sub1',))
    t2 = datetime.datetime.now()
    time_elapse = t2.timestamp() - t1.timestamp()
    assert time_elapse < 0.01
    _yottadb.lock_decr(b'test2', (b'sub1',))

def test_lock_incr_timeout_1():
    subprocess.Popen(shlex.split('python YDBPython/tests/lock.py -t 2 ^test1'))
    time.sleep(1)
    with pytest.raises(_yottadb.YDBTimeoutError):
        _yottadb.lock_incr(b'^test1')
    time.sleep(1)

def test_lock_incr_timeout_2():
    subprocess.Popen(shlex.split('python YDBPython/tests/lock.py -t 2 ^test2 sub1'))
    time.sleep(1)
    with pytest.raises(_yottadb.YDBTimeoutError):
        _yottadb.lock_incr(b'^test2', (b'sub1',))
    time.sleep(1)

def test_lock_incr_timeout_3():
    subprocess.Popen(shlex.split('python YDBPython/tests/lock.py -t 2 ^test2'))
    time.sleep(1)
    with pytest.raises(_yottadb.YDBTimeoutError):
        _yottadb.lock_incr(b'^test2', (b'sub1',))
    time.sleep(1)

def test_lock_incr_timeout_4():
    subprocess.Popen(shlex.split('python YDBPython/tests/lock.py -t 2 ^test2 sub1'))
    time.sleep(1)
    t1 = datetime.datetime.now()
    _yottadb.lock_incr(b'test2')
    t2 = datetime.datetime.now()
    time_elapse = t2.timestamp() - t1.timestamp()
    assert time_elapse < 0.01
    _yottadb.lock_decr(b'test2')
    time.sleep(1.1)

def simple_function(param, tp_token=NOTTP):
    print(tp_token)
    print(param)
    return _yottadb.YDB_OK

# new tp() tests
def return_YDB_OK_transaction(key:KeyTuple, value:bytes, tp_token:int = NOTTP) -> int:
    _yottadb.set(*key, value, tp_token=tp_token)
    return _yottadb.YDB_OK


def test_tp_return_YDB_OK():
    key = KeyTuple(varname=b'^tptests', subsarray=(b'test_tp_return_YDB_OK',))
    value = b'return YDB_OK'
    _yottadb.delete(*key)
    assert _yottadb.data(*key) == _yottadb.YDB_DATA_NO_DATA

    _yottadb.tp(return_YDB_OK_transaction, args=(key, value))

    assert _yottadb.get(*key) == value
    _yottadb.delete(*key)

def nested_return_YDB_OK_transaction(key1:KeyTuple, value1:bytes, key2:KeyTuple, value2:bytes, tp_token:int = NOTTP):
    _yottadb.set(*key1, value1, tp_token=tp_token)
    _yottadb.tp(return_YDB_OK_transaction, args=(key2, value2), tp_token=tp_token)
    return _yottadb.YDB_OK

def test_tp_nested_return_YDB_OK():
    key1 = KeyTuple(varname=b'^tptests', subsarray=(b'test_tp_nested_return_YDB_OK', b'outer'))
    value1 = b'return_YDB_OK'
    key2 = KeyTuple(varname=b'^tptests', subsarray=(b'test_tp_nested_return_YDB_OK', b'nested'))
    value2 = b'nested return_YDB_OK'

    _yottadb.tp(nested_return_YDB_OK_transaction, args=(key1, value1, key2, value2))

    assert _yottadb.get(*key1) == value1
    assert _yottadb.get(*key2) == value2
    _yottadb.delete(*key1)
    _yottadb.delete(*key2)


def return_YDB_ROLLBACK_transaction(key:KeyTuple, value:bytes, tp_token:int = NOTTP) -> int:
    _yottadb.set(*key, value, tp_token=tp_token)
    return _yottadb.YDB_TP_ROLLBACK


def test_tp_return_YDB_ROLLBACK():
    key = KeyTuple(varname=b'^tptests', subsarray=(b'test_tp_return_YDB_ROLLBACK',))
    value = b'return YDB_ROLLBACK'
    _yottadb.delete(*key)
    assert _yottadb.data(*key) == _yottadb.YDB_DATA_NO_DATA

    with pytest.raises(_yottadb.YDBTPRollback):
        _yottadb.tp(return_YDB_ROLLBACK_transaction, args=(key, value))

    assert _yottadb.data(*key) == _yottadb.YDB_DATA_NO_DATA


def nested_return_YDB_ROLLBACK_transaction(key1:KeyTuple, value1:bytes, key2:KeyTuple, value2:bytes, tp_token:int = NOTTP):
    _yottadb.set(*key1, value1, tp_token=tp_token)

    try:
        _yottadb.tp(return_YDB_ROLLBACK_transaction, args=(key2, value2), tp_token=tp_token)
    except _yottadb.YDBTPRollback:
        return _yottadb.YDB_TP_ROLLBACK


def test_nested_return_YDB_ROLLBACK():
    key1 = KeyTuple(varname=b'^tptests', subsarray=(b'test_nested_return_YDB_ROLLBACK', b'outer'))
    value1 = b'return YDB_ROLLBACK'
    key2 = KeyTuple(varname=b'^tptests', subsarray=(b'test_nested_return_YDB_ROLLBACK', b'nested'))
    value2 = b'nested return YDB_ROLLBACK'


    with pytest.raises(_yottadb.YDBTPRollback):
        _yottadb.tp(nested_return_YDB_ROLLBACK_transaction, args=(key1, value1, key2, value2))

    assert _yottadb.data(*key1) == _yottadb.YDB_DATA_NO_DATA
    assert _yottadb.data(*key2) == _yottadb.YDB_DATA_NO_DATA



def return_YDB_TP_RESTART_transaction(key1: KeyTuple, key2: KeyTuple, value:bytes, tracker:KeyTuple, tp_token:int = NOTTP) -> int:
    if int(_yottadb.get(*tracker, tp_token=tp_token)) == 0:
        _yottadb.set(*key1, value=value, tp_token = tp_token)
    else:
        _yottadb.set(*key2, value=value, tp_token=tp_token)

    if int(_yottadb.get(*tracker, tp_token=tp_token)) == 0:
        _yottadb.incr(*tracker, tp_token=tp_token)
        return _yottadb.YDB_TP_RESTART
    else:
        return _yottadb.YDB_OK


def test_tp_return_YDB_TP_RESTART():
    # Given:
    key1 = KeyTuple(varname=b'^tptests', subsarray=(b'test_tp_return_YDB_TP_RESTART', b'key1'))
    _yottadb.delete(*key1)
    key2 = KeyTuple(varname=b'^tptests', subsarray=(b'test_tp_return_YDB_TP_RESTART', b'key2'))
    _yottadb.delete(*key2)
    value = b'restart once'
    tracker = KeyTuple(varname=b'tptests', subsarray=(b'test_tp_return_YDB_RESET', b'reset count'))
    _yottadb.set(*tracker, value=b'0')
    # When:
    _yottadb.tp(return_YDB_TP_RESTART_transaction, args=(key1, key2, value, tracker))
    # Then:
    with pytest.raises(_yottadb.YDBGVUNDEFError):
        _yottadb.get(*key1)
    assert _yottadb.get(*key2) == value
    assert int(_yottadb.get(*tracker)) == 1
    # clean up
    _yottadb.delete(*key1)
    _yottadb.delete(*key2)
    _yottadb.delete(*tracker)


def return_YDB_ERR_TPTIMEOUT_transaction(key:KeyTuple, value:bytes, tp_token:int = NOTTP) -> int:
    _yottadb.set(*key, value, tp_token=tp_token)
    return _yottadb.YDB_ERR_TPTIMEOUT


def test_tp_return_YDB_ERR_TPTIMEOUT():
    key = KeyTuple(varname=b'^tptests', subsarray=(b'test_tp_return_YDB_ERR_TPTIMEOUT',))
    value = b'return YDB_ERR_TPTIMEOUT'
    _yottadb.delete(*key)
    assert _yottadb.data(*key) == _yottadb.YDB_DATA_NO_DATA

    with pytest.raises(_yottadb.YDBTPTIMEOUTError):
        _yottadb.tp(return_YDB_ERR_TPTIMEOUT_transaction, args=(key, value))

    assert _yottadb.data(*key) == _yottadb.YDB_DATA_NO_DATA


def nested_return_YDB_ERR_TPTIMEOUT_transaction(key1:KeyTuple, value1:bytes, key2:KeyTuple, value2:bytes, tp_token:int = NOTTP):
    _yottadb.set(*key1, value1, tp_token=tp_token)

    try:
        _yottadb.tp(return_YDB_ERR_TPTIMEOUT_transaction, args=(key2, value2), tp_token=tp_token)
    except _yottadb.YDBTPTIMEOUTError:
        return _yottadb.YDB_ERR_TPTIMEOUT


def test_tp_nested_return_YDB_ERR_TPTIMEOUT():
    key1 = KeyTuple(varname=b'^tptests', subsarray=(b'test_nested_return_YDB_ERR_TPTIMEOUT', b'outer'))
    value1 = b'return YDB_ERR_TPTIMEOUT'
    key2 = KeyTuple(varname=b'^tptests', subsarray=(b'test_nested_return_YDB_ERR_TPTIMEOUT', b'nested'))
    value2 = b'nested return YDB_ERR_TPTIMEOUT'

    with pytest.raises(_yottadb.YDBTPTIMEOUTError):
        _yottadb.tp(nested_return_YDB_ERR_TPTIMEOUT_transaction, args=(key1, value1, key2, value2))

    assert _yottadb.data(*key1) == _yottadb.YDB_DATA_NO_DATA
    assert _yottadb.data(*key2) == _yottadb.YDB_DATA_NO_DATA


def raise_YDBError_transaction(key:KeyTuple, tp_token:int = NOTTP) -> int:
    _yottadb.get(*key, tp_token=tp_token)


def test_tp_raise_YDBError():
    key = KeyTuple(varname=b'^tptests', subsarray=(b'test_tp_raise_YDBError',))
    assert _yottadb.data(*key) == _yottadb.YDB_DATA_NO_DATA

    with pytest.raises(_yottadb.YDBError):
        _yottadb.tp(raise_YDBError_transaction, args=(key,))


def nested_raise_YDBError_transaction(key:KeyTuple, tp_token:int = NOTTP) -> int:
    _yottadb.tp(raise_YDBError_transaction, args=(key,), tp_token=tp_token)


def test_tp_nested_raise_YDBError():
    key = KeyTuple(varname=b'^tptests', subsarray=(b'test_nested_tp_raise_YDBError',))
    assert _yottadb.data(*key) == _yottadb.YDB_DATA_NO_DATA

    with pytest.raises(_yottadb.YDBError):
        _yottadb.tp(nested_raise_YDBError_transaction, args=(key,))


def raise_standard_python_exception_transaction(tp_token:int = NOTTP) -> int:
    1/0
    return _yottadb.YDB_OK  # this line shouldn't execute


def test_tp_raise_standard_python_exception():
    with pytest.raises(ZeroDivisionError):
        _yottadb.tp(raise_standard_python_exception_transaction)


def nested_raise_standard_python_exception_transaction(tp_token:int = NOTTP) -> int:
    _yottadb.tp(raise_standard_python_exception_transaction, tp_token=tp_token)
    return _yottadb.YDB_OK  # this line shouldn't execute


def test_tp_nested_raise_standard_python_exception():
    with pytest.raises(ZeroDivisionError):
        _yottadb.tp(nested_raise_standard_python_exception_transaction)

# old tp() tests
def test_tp_0():
    _yottadb.tp(simple_function, args=('test0',))

def simple_functional_function(tp_token=NOTTP):
    _yottadb.set(varname=b'^testtp1', value = b'after', tp_token=tp_token)
    return _yottadb.YDB_OK

def test_tp_1():
    _yottadb.set(varname=b'^testtp1', value=b'before')
    _yottadb.tp(simple_functional_function, kwargs={})
    assert _yottadb.get(varname=b'^testtp1') == b'after'
    _yottadb.delete(varname=b'^testtp1', delete_type=_yottadb.YDB_DEL_TREE)



def transfer_transaction(from_account, to_account, amount, tp_token=NOTTP):

    from_account_balance = int(_yottadb.get(tp_token=tp_token, varname=b'^account', subsarray=(from_account, b'balance')))
    to_account_balance = int(_yottadb.get(tp_token=tp_token, varname=b'^account', subsarray=(to_account, b'balance')))

    _yottadb.set(tp_token=tp_token, varname=b'^account', subsarray=(from_account, b'balance'), value=bytes(str(from_account_balance - amount), encoding='utf-8'))
    _yottadb.set(tp_token=tp_token, varname=b'^account', subsarray=(to_account, b'balance'), value=bytes(str(to_account_balance + amount), encoding='utf-8'))

    new_from_balance = int(_yottadb.get(tp_token=tp_token, varname=b'^account', subsarray=(from_account, b'balance')))

    if new_from_balance < 0:
        return _yottadb.YDB_TP_ROLLBACK
    else:
        return _yottadb.YDB_OK

def test_tp_2(bank):
    account1 = bank[0]['account#']
    account2 = bank[1]['account#']
    account1_balance = bank[0]['account_balance']
    account2_balance = bank[1]['account_balance']
    transfer_amount = account1_balance - 1

    _yottadb.tp(transfer_transaction, args=(account1, account2, transfer_amount), kwargs={})

    assert int(_yottadb.get(varname=b'^account', subsarray=(account1, b'balance'))) == account1_balance - transfer_amount
    assert int(_yottadb.get(varname=b'^account', subsarray=(account2, b'balance'))) == account2_balance + transfer_amount

def test_tp_2_rollback(bank):
    account1 = bank[0]['account#']
    account2 = bank[1]['account#']
    account1_balance = bank[0]['account_balance']
    account2_balance = bank[1]['account_balance']
    transfer_amount = account1_balance + 1
    _yottadb.set(varname=b'account', subsarray=(account1, b'balance'), value=bytes(str(account1_balance), encoding='utf-8'))
    _yottadb.set(varname=b'account', subsarray=(account2, b'balance'), value=bytes(str(account2_balance), encoding='utf-8'))
    with pytest.raises(_yottadb.YDBTPRollback):
        result = _yottadb.tp(transfer_transaction, args=(account1, account2, transfer_amount), kwargs={})

    assert int(_yottadb.get(varname=b'account', subsarray=(account1, b'balance'))) == account1_balance
    assert int(_yottadb.get(varname=b'account', subsarray=(account2, b'balance'))) == account2_balance


def callback_that_returns_wrong_type(tp_token=None):
    return "not an int"

def test_tp_callback_return_wrong_type():
    with pytest.raises(TypeError):
        _yottadb.tp(callback_that_returns_wrong_type)


def callback_for_tp_simple_restart(start_time, tp_token=NOTTP):
    now = datetime.datetime.now()
    #print(start_time)
    #print(tp_token)
    _yottadb.incr(b'resetattempt', increment=b'1', tp_token=tp_token)
    _yottadb.incr(b'resetvalue', increment=b'1', tp_token=tp_token)
    if _yottadb.get(b'resetattempt', tp_token=tp_token) == b'2':
        print('standard')
        return _yottadb.YDB_OK
    elif (now - start_time) > datetime.timedelta(seconds=0.01):
        print('timeout')
        return _yottadb.YDB_OK
    else:
        return _yottadb.YDB_TP_RESTART



    return _yottadb.YDB_TP_RESTART

def test_tp_4a_reset_all(simple_reset_test_data):
    start_time = datetime.datetime.now()
    result = _yottadb.tp(callback_for_tp_simple_restart, args=(start_time,), varnames=(b'*',))
    assert result == _yottadb.YDB_OK
    assert _yottadb.get(b'resetattempt') == b'1'
    assert _yottadb.get(b'resetvalue') == b'1'

def test_tp_4b_reset_none(simple_reset_test_data):
    start_time = datetime.datetime.now()
    result = _yottadb.tp(callback_for_tp_simple_restart, args=(start_time,), varnames=())
    assert result == _yottadb.YDB_OK
    assert _yottadb.get(b'resetattempt') == b'2'
    assert _yottadb.get(b'resetvalue') == b'2'

def test_tp_4c_reset_some(simple_reset_test_data):
    start_time = datetime.datetime.now()
    result = _yottadb.tp(callback_for_tp_simple_restart, args=(start_time,), varnames=(b'resetvalue',))
    assert result == _yottadb.YDB_OK
    assert _yottadb.get(b'resetattempt') == b'2'
    assert _yottadb.get(b'resetvalue') == b'1'

def test_tp_4d_reset_default_none(simple_reset_test_data):
    start_time = datetime.datetime.now()
    result = _yottadb.tp(callback_for_tp_simple_restart, args=(start_time,))
    assert result == _yottadb.YDB_OK
    assert _yottadb.get(b'resetattempt') == b'2'
    assert _yottadb.get(b'resetvalue') == b'2'

def test_subscript_next_1(simple_data):
    assert _yottadb.subscript_next(varname=b'^%') == b'^Test5'
    assert _yottadb.subscript_next(varname=b'^a') == b'^test1'
    assert _yottadb.subscript_next(varname=b'^test1') == b'^test2'
    assert _yottadb.subscript_next(varname=b'^test2') == b'^test3'
    assert _yottadb.subscript_next(varname=b'^test3') == b'^test4'
    with pytest.raises(_yottadb.YDBNODEENDError):
        _yottadb.subscript_next(varname=b'^test6')

    assert _yottadb.subscript_next(varname=b'^test4', subsarray=(b'',)) == b'sub1'
    assert _yottadb.subscript_next(varname=b'^test4', subsarray=(b'sub1',)) == b'sub2'
    assert _yottadb.subscript_next(varname=b'^test4', subsarray=(b'sub2',)) == b'sub3'
    with pytest.raises(_yottadb.YDBNODEENDError):
        _yottadb.subscript_next(varname=b'^test4', subsarray=(b'sub3',))

    assert _yottadb.subscript_next(varname=b'^test4', subsarray=(b'sub1', b'')) == b'subsub1'
    assert _yottadb.subscript_next(varname=b'^test4', subsarray=(b'sub1',b'subsub1')) == b'subsub2'
    assert _yottadb.subscript_next(varname=b'^test4', subsarray=(b'sub1', b'subsub2')) == b'subsub3'
    with pytest.raises(_yottadb.YDBNODEENDError):
        _yottadb.subscript_next(varname=b'^test4', subsarray=(b'sub3', b'subsub3'))

def test_subscript_next_long():
    _yottadb.set(varname=b'testLongSubscript', subsarray=(b'a'*_yottadb.YDB_MAX_STR,), value=b'toolong')
    assert _yottadb.subscript_next(varname=b'testLongSubscript', subsarray=(b'',)) == b'a'*_yottadb.YDB_MAX_STR

def test_subscript_next_i18n():
    _yottadb.set(varname=b'testi18n', subsarray=(bytes('中文', encoding='utf-8'),), value=b'chinese')
    assert _yottadb.subscript_next(varname=b'testi18n', subsarray=(b'',)) == bytes('中文', encoding='utf-8')

def test_subscript_previous_1(simple_data):
    assert _yottadb.subscript_previous(varname=b'^z') == b'^test6'
    assert _yottadb.subscript_previous(varname=b'^a') == b'^Test5'
    assert _yottadb.subscript_previous(varname=b'^test1') == b'^Test5'
    assert _yottadb.subscript_previous(varname=b'^test2') == b'^test1'
    assert _yottadb.subscript_previous(varname=b'^test3') == b'^test2'
    assert _yottadb.subscript_previous(varname=b'^test4') == b'^test3'
    with pytest.raises(_yottadb.YDBNODEENDError):
        _yottadb.subscript_previous(varname=b'^Test5')

    assert _yottadb.subscript_previous(varname=b'^test4', subsarray=(b'',)) == b'sub3'
    assert _yottadb.subscript_previous(varname=b'^test4', subsarray=(b'sub2',)) == b'sub1'
    assert _yottadb.subscript_previous(varname=b'^test4', subsarray=(b'sub3',)) == b'sub2'
    with pytest.raises(_yottadb.YDBNODEENDError):
        _yottadb.subscript_previous(varname=b'^test4', subsarray=(b'sub1',))

    assert _yottadb.subscript_previous(varname=b'^test4', subsarray=(b'sub1', b'')) == b'subsub3'
    assert _yottadb.subscript_previous(varname=b'^test4', subsarray=(b'sub1', b'subsub2')) == b'subsub1'
    assert _yottadb.subscript_previous(varname=b'^test4', subsarray=(b'sub1', b'subsub3')) == b'subsub2'
    with pytest.raises(_yottadb.YDBNODEENDError):
        _yottadb.subscript_previous(varname=b'^test4', subsarray=(b'sub3', b'subsub1'))

def test_subscript_previous_long():
    _yottadb.set(varname=b'testLongSubscript', subsarray=(b'a'*_yottadb.YDB_MAX_STR,), value=b'toolong')
    assert _yottadb.subscript_previous(varname=b'testLongSubscript', subsarray=(b'',)) == b'a'*_yottadb.YDB_MAX_STR

def test_node_next_1(simple_data):
    assert _yottadb.node_next(b'^test3') == (b'sub1',)
    assert _yottadb.node_next(b'^test3', subsarray=(b'sub1',)) == (b'sub1', b'sub2')
    with pytest.raises(_yottadb.YDBNODEENDError):
        _yottadb.node_next(varname=b'^test3', subsarray=(b'sub1', b'sub2'))
    assert _yottadb.node_next(b'^test6') == (b'sub6', b'subsub6')

def test_node_next_many_subscipts():
    _yottadb.set(varname=b'testmanysubscripts', subsarray=(b'sub1', b'sub2', b'sub3', b'sub4', b'sub5', b'sub6'), value=b'123')
    assert _yottadb.node_next(b'testmanysubscripts') == (b'sub1', b'sub2', b'sub3', b'sub4', b'sub5', b'sub6')

def test_node_next_long_subscripts():
    _yottadb.set(varname=b'testlong', subsarray=(b'a' * 1025, b'a' * 1026), value=b'123')
    assert _yottadb.node_next(b'testlong') == (b'a' * 1025, b'a' * 1026)


def test_node_previous_1(simple_data):
    with pytest.raises(_yottadb.YDBNODEENDError):
        _yottadb.node_previous(b'^test3')
    assert _yottadb.node_previous(b'^test3', (b'sub1',)) == ()
    assert _yottadb.node_previous(b'^test3', subsarray=(b'sub1',b'sub2')) == (b'sub1',)

def test_node_previous_long_subscripts():
    _yottadb.set(varname=b'testlong', subsarray=(b'a' * 1025, b'a' * 1026), value=b'123')
    _yottadb.set(varname=b'testlong', subsarray=(b'a' * 1025, b'a' * 1026, b'a'), value=b'123')
    assert _yottadb.node_previous(b'testlong', (b'a' * 1025, b'a' * 1026, b'a')) == (b'a' * 1025, b'a' * 1026)



def test_lock_blocking_other(simple_data):
    t1 = KeyTuple(b'^test1')
    t2 = KeyTuple(b'^test2', (b'sub1',))
    t3 = KeyTuple(b'^test3', (b'sub1', b'sub2'))
    keys_to_lock = (t1, t2, t3)
    _yottadb.lock(keys=keys_to_lock, timeout_nsec=0)
    print(t1)
    assert execute(f"python YDBPython/tests/lock.py -T 0 -t 0 {key_tuple_to_str(t1)}") == "Lock Failed"
    assert execute(f"python YDBPython/tests/lock.py -T 0 -t 0 {key_tuple_to_str(t2)}") == "Lock Failed"
    assert execute(f"python YDBPython/tests/lock.py -T 0 -t 0 {key_tuple_to_str(t3)}") == "Lock Failed"
    _yottadb.lock()
    assert execute(f"python YDBPython/tests/lock.py -T 0 -t 0 {key_tuple_to_str(t1)}") == "Lock Success"
    assert execute(f"python YDBPython/tests/lock.py -T 0 -t 0 {key_tuple_to_str(t2)}") == "Lock Success"
    assert execute(f"python YDBPython/tests/lock.py -T 0 -t 0 {key_tuple_to_str(t3)}") == "Lock Success"


def test_lock_being_blocked():
    subprocess.Popen(shlex.split('python YDBPython/tests/lock.py ^test1'))
    time.sleep(1)
    with pytest.raises(_yottadb.YDBTimeoutError):
        _yottadb.lock([KeyTuple(b'^test1')])

def test_lock_max_names():
    keys = []
    for i in range(_yottadb.YDB_MAX_NAMES):
        keys.append([bytes(f'^testlock{i}', encoding='utf-8')])
        _yottadb.lock(keys)
    keys.append([b'^JustOneMore'])
    with pytest.raises(_yottadb.YDBNAMECOUNT2HIError):
        _yottadb.lock(keys)


def test_delete_excel():
    _yottadb.set(varname=b'testdeleteexcel1', value = b"1")
    _yottadb.set(varname=b'testdeleteexcel2', subsarray=(b'sub1',), value=b"2")
    _yottadb.set(varname=b'testdeleteexcelexception', subsarray=(b'sub1',), value=b"3")
    _yottadb.delete_excel(varnames=(b'testdeleteexcelexception',))
    with pytest.raises(_yottadb.YDBLVUNDEFError) as e:
        _yottadb.get(b'testdeleteexcel1')
    with pytest.raises(_yottadb.YDBLVUNDEFError) as e:
        print("trying to get 2")
        _yottadb.get(b'testdeleteexcel2', (b'sub1',))
    assert _yottadb.get(b'testdeleteexcelexception', (b'sub1',)) == b"3"


increment_tests = [
    #string input tests
    (b"0", "1", b"1"),
    (b"0", "1E1", b"10"),
    (b"0", "1E16", b"1"+b'0'*16),
    (b"0", '9'*6, b'9'*6),
    (b"0", '9'*7+'E0', b'9'*7),
    (b"0", '9'*6+'E1', b'9'*6+b'0'),
    (b"0", '9'*18+'E0', b'9'*18),
    (b"9999998", '1', b'9'*7),
    (b"999999999999999998", '1', b'9'*18),
    (b"999999999999888888", '111111', b'9'*18),
    (b"999999999998888888", '1111111', b'9'*18),
    (b"999999999990000000", '9999999', b'9'*18),
    (b"1", "-1234567", b"-1234566"),
    (b"0", "1234567E40", b"1234567"+b"0"*40),
    (b"0", "1234567E-43", b"."+b"0"*36+b"1234567"),
    (b"0", "123456789123456789E-43", b"."+b"0"*25+b"123456789123456789"),
    (b"0", "123456789123456789E29", b"123456789123456789"+b"0"*29),
    (b"0", "1234567", b"1234567"),
    (b"0", "-1234567", b"-1234567"),
    (b"0", '100.0001', b'100.0001'),
    (b"0", '1'+'0'*6, b'1'+b'0'*6),
    (b"0", '1'*7, b'1'*7),
    (b"0", '9'*7, b'9'*7),
    (b"0", '9'*6+'0', b'9'*6+b'0'),
    (b"0", '100.001', b'100.001'),
    (b"1", '100.0001', b'101.0001'),
    (b"1", '9'*18, b"1"+b"0"*18),
    (b"0", "-1234567E0", b"-1234567"),
    (b"1", '9' * 6 + '8', b'9' * 7),
    (b"1", '9' * 6 + '0', b'9' * 6 + b'1'),
    (b"0", "1E-43", b"."+b"0"*42+b"1"),
    (b"0", ".1E47", b"1"+b"0"*46), # max magnitude
    (b"0", "-.1E47", b"-1"+b"0"*46), # max magnitude
    (b"0", "1E-43", b"."+b"0"*42+b"1"), # min magnitude
    (b"0", "-1E-43", b"-."+b"0"*42+b"1"), # min magnitude
    # int input tests
    (b"0", 1, b"1"),
    (b"0", 10, b"10"),
    (b"9999998", 1, b'9'*7),
    (b"999999999999999998", 1, b'9'*18),
    (b"999999999999888888", 111111, b'9'*18),
    (b"999999999998888888", 1111111, b'9'*18),
    (b"999999999990000000", 9999999, b'9'*18),
    (b"1", -1234567, b"-1234566"),
    (b"1", 999999999999999998, b'9'*18), #18 significant digits
    (b"0", 99999_99999_99999_999, b'9'*18), #18 significant digits
    (b"-1", -999999999999999998, b'-'+b'9'*18), #18 significant digits
    (b"0", -99999_99999_99999_999, b'-'+b'9'*18), #18 significant digits
    (b"0", 10000_00000_00000_00000_00000_00000_00000_00000_00000_00, b"1"+b'0'*46), # max int magnitude
    (b"0", -10000_00000_00000_00000_00000_00000_00000_00000_00000_00, b"-1"+b'0'*46), # max int magnitude
    # float input tests
    (b"0", 1.0, b"1"),
    (b"0", 0.1, b".1"),
    (b"0", 1E1, b"10"),
    (b"0", 1E16, b"1"+b'0'*16),
    (b"0", 1E-43, b"."+b"0"*42+b"1"), # max float magnitude
    (b"0", -1E-43, b"-."+b"0"*42+b"1"), # max float magnitude
    (b"0", .1E47, b"1"+b"0"*46), # min float magnitude
    (b"0", -.1E47, b"-1"+b"0"*46), # min float magnitude
    # Decimal input tests
    (b"0", Decimal("1"), b"1"),
    (b"0", Decimal("1E1"), b"10"),
    (b"0", Decimal("1E16"), b"1"+ b'0'*16),
    (b"0", Decimal(".1E47"), b"1"+b"0"*46), # max Decimal magnitude
    (b"0", Decimal("-.1E47"), b"-1"+b"0"*46), # max Decimal magnitude
    (b"0", Decimal(".1E47"), b"1"+b"0"*46), # min float magnitude
    (b"0", Decimal("-.1E47"), b"-1"+b"0"*46), # min float magnitude
]

increment_test_ids = [f'"{initial}" | "{type(increment).__name__}({increment})" | {result}' for initial, increment, result in increment_tests]

increment_keys = [
    KeyTuple(b'testincrparamaterized'),
    KeyTuple(b'^testincrparamaterized'),
    KeyTuple(b'^testincrparamaterized', (b'sub1',)),
    KeyTuple(b'testincrparamaterized', (b'sub1',)),
]

increment_key_test_ids = [f'"{key.varname}({key.subsarray})' for key in increment_keys]

def number_to_bytes(number):
    number_str = number
    if not isinstance(number, str):
        number_str = f'{number}'.upper().replace('+','')
    if len(number_str) >= 7 and 'E' not in number_str: # bug workaround
        number_str += 'E0'
    return bytes(number_str, encoding='ascii')


@pytest.mark.parametrize('initial, increment, result', increment_tests, ids=increment_test_ids)
@pytest.mark.parametrize('key', increment_keys, ids=increment_key_test_ids)
def test_incr(key, initial, increment, result):

    _yottadb.set(*key, value=initial)
    returned_value = _yottadb.incr(*key, increment=number_to_bytes(increment))

    assert returned_value == result
    assert _yottadb.get(*key) == result
    _yottadb.delete(*key, _yottadb.YDB_DEL_TREE)

increment_error_test = [
    (b"0", "1E47", _yottadb.YDBNUMOFLOWError),
    (b"0", "-1E47", _yottadb.YDBNUMOFLOWError),
    #("0", "1E-47", _yottadb.YDBNUMOFLOWError),
]
increment_test_ids = [f'"{initial}" | "{type(increment).__name__}({increment})" | {error_type}' for initial, increment, error_type in increment_error_test]
@pytest.mark.parametrize('initial, increment, error_type', increment_error_test, ids=increment_test_ids)
@pytest.mark.parametrize('key', increment_keys, ids=increment_key_test_ids)
def test_incr_errors(key, initial, increment, error_type):
    _yottadb.set(*key, value=initial)

    with pytest.raises(error_type):
        _yottadb.incr(*key, increment=number_to_bytes(increment))
        assert _yottadb.get(*key) == initial
    _yottadb.delete(*key, _yottadb.YDB_DEL_TREE)


str2zwr_tests = [
    (b'X\0ABC', b'"X"_$C(0)_"ABC"'),
    (bytes('你好世界', encoding='utf-8'), b'"\xe4\xbd\xa0\xe5\xa5\xbd\xe4\xb8"_$C(150)_"\xe7"_$C(149,140)')
]
str2zwr_test_ids = [f'{input} -> {output}' for input, output in str2zwr_tests]
zwr2str_test_ids = [f'{input} -> {output}' for output, input in str2zwr_tests]

@pytest.mark.parametrize('input, output', str2zwr_tests, ids=str2zwr_test_ids)
def test_str2zwr(input, output):
    assert _yottadb.str2zwr(input) == output

@pytest.mark.parametrize('output, input', str2zwr_tests, ids=zwr2str_test_ids)
def test_zwr2str(input, output):
    assert _yottadb.zwr2str(input) == output