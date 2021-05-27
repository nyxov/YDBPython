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
import pytest  # type: ignore # ignore due to pytest not having type annotations

import multiprocessing
import os
import datetime
import time
from decimal import Decimal
from typing import NamedTuple, Sequence, Tuple, Optional, Callable

from conftest import lock_value, str2zwr_tests

import _yottadb
import yottadb
from yottadb import KeyTuple

YDB_INSTALL_DIR = os.environ["ydb_dist"]
TEST_DATA_DIRECTORY = "/tmp/test_yottadb/"
TEST_GLD = TEST_DATA_DIRECTORY + "test_db.gld"
TEST_DAT = TEST_DATA_DIRECTORY + "test_db.dat"


def test_get(simple_data):
    # Handling of positional arguments
    assert _yottadb.get("^test1") == b"test1value"
    assert _yottadb.get("^test2", ["sub1"]) == b"test2value"
    assert _yottadb.get("^test3") == b"test3value1"
    assert _yottadb.get("^test3", ["sub1"]) == b"test3value2"
    assert _yottadb.get("^test3", ["sub1", "sub2"]) == b"test3value3"
    # Using bytes arguments
    assert _yottadb.get(b"^test3", [b"sub1", b"sub2"]) == b"test3value3"

    # Handling of keyword arguments
    assert _yottadb.get(varname="^test1") == b"test1value"
    assert _yottadb.get(varname="^test2", subsarray=["sub1"]) == b"test2value"
    assert _yottadb.get(varname="^test3") == b"test3value1"
    assert _yottadb.get(varname="^test3", subsarray=["sub1"]) == b"test3value2"
    assert _yottadb.get(varname="^test3", subsarray=["sub1", "sub2"]) == b"test3value3"
    # Using bytes arguments
    assert _yottadb.get(varname=b"^test3", subsarray=[b"sub1", b"sub2"]) == b"test3value3"

    # Error handling
    with pytest.raises(_yottadb.YDBGVUNDEFError):
        _yottadb.get("^testerror")
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get("testerror")
    with pytest.raises(_yottadb.YDBGVUNDEFError):
        _yottadb.get("^testerror", ["sub1"])
    # Using bytes arguments
    with pytest.raises(_yottadb.YDBGVUNDEFError):
        _yottadb.get(b"^testerror")
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get(b"testerror")
    with pytest.raises(_yottadb.YDBGVUNDEFError):
        _yottadb.get(b"^testerror", [b"sub1"])

    # Handling of large values
    _yottadb.set(varname="testlong", value=("a" * _yottadb.YDB_MAX_STR))
    assert _yottadb.get(varname="testlong") == b"a" * _yottadb.YDB_MAX_STR
    # Using bytes arguments
    _yottadb.set(varname=b"testlong", value=(b"a" * _yottadb.YDB_MAX_STR))
    assert _yottadb.get(varname=b"testlong") == b"a" * _yottadb.YDB_MAX_STR


def test_set():
    # Positional arguments
    _yottadb.set("test4", value="test4value")
    assert _yottadb.get("test4") == b"test4value"
    _yottadb.set("test6", ("sub1",), "test6value")
    assert _yottadb.get("test6", ("sub1",)) == b"test6value"
    # Using bytes arguments
    _yottadb.set(b"test4", value=b"test4value")
    assert _yottadb.get(b"test4") == b"test4value"
    _yottadb.set(b"test6", (b"sub1",), "test6value")
    assert _yottadb.get(b"test6", (b"sub1",)) == b"test6value"

    # Keyword arguments
    _yottadb.set(varname="test5", value="test5value")
    assert _yottadb.get("test5") == b"test5value"
    _yottadb.set(varname="test7", subsarray=("sub1",), value="test7value")
    assert _yottadb.get("test7", ("sub1",)) == b"test7value"
    # Using bytes arguments
    _yottadb.set(varname=b"test5", value=b"test5value")
    assert _yottadb.get(b"test5") == b"test5value"
    _yottadb.set(varname=b"test7", subsarray=(b"sub1",), value=b"test7value")
    assert _yottadb.get(b"test7", (b"sub1",)) == b"test7value"

    # Unicode/i18n value
    _yottadb.set(varname="testchinese", value="你好世界")
    assert _yottadb.get("testchinese") == bytes("你好世界", encoding="utf-8")
    # Using bytes arguments
    # (Value is in Unicode and so cannot be passed as bytes)
    _yottadb.set(varname=b"testchinese", value="你好世界".encode())
    assert _yottadb.get(b"testchinese") == bytes("你好世界", encoding="utf-8")


def test_delete():
    # Positional arguments
    _yottadb.set(varname="test8", value="test8value")
    assert _yottadb.get("test8") == b"test8value"
    _yottadb.delete("test8")
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get("test8")
    # Using bytes arguments
    _yottadb.set(varname=b"test8", value=b"test8value")
    assert _yottadb.get(b"test8") == b"test8value"
    _yottadb.delete(b"test8")
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get(b"test8")

    _yottadb.set(varname="test10", subsarray=("sub1",), value="test10value")
    assert _yottadb.get("test10", ("sub1",)) == b"test10value"
    _yottadb.delete("test10", ("sub1",))
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get("test10", ("sub1",))
    # Using bytes arguments
    _yottadb.set(varname=b"test10", subsarray=(b"sub1",), value=b"test10value")
    assert _yottadb.get(b"test10", (b"sub1",)) == b"test10value"
    _yottadb.delete(b"test10", (b"sub1",))
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get(b"test10", (b"sub1",))

    # Delete tree
    _yottadb.set(varname="test12", value="test12 node value")
    _yottadb.set(varname="test12", subsarray=("sub1",), value="test12 subnode value")
    assert _yottadb.get("test12") == b"test12 node value"
    assert _yottadb.get("test12", ("sub1",)) == b"test12 subnode value"
    _yottadb.delete("test12", (), _yottadb.YDB_DEL_TREE)
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get("test12")
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get("test12", ("sub1",))
    # Using bytes arguments
    _yottadb.set(varname=b"test12", value=b"test12 node value")
    _yottadb.set(varname=b"test12", subsarray=(b"sub1",), value=b"test12 subnode value")
    assert _yottadb.get(b"test12") == b"test12 node value"
    assert _yottadb.get(b"test12", (b"sub1",)) == b"test12 subnode value"
    _yottadb.delete(b"test12", (), _yottadb.YDB_DEL_TREE)
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get(b"test12")
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get(b"test12", (b"sub1",))

    # Keyword arguments
    _yottadb.set(varname="test9", value="test9value")
    assert _yottadb.get("test9") == b"test9value"
    _yottadb.delete(varname="test9", delete_type=_yottadb.YDB_DEL_NODE)
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get("test9")
    # Using bytes arguments
    _yottadb.set(varname=b"test9", value=b"test9value")
    assert _yottadb.get(b"test9") == b"test9value"
    _yottadb.delete(varname=b"test9", delete_type=_yottadb.YDB_DEL_NODE)
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get(b"test9")

    _yottadb.set(varname="test11", subsarray=("sub1",), value="test11value")
    assert _yottadb.get("test11", ("sub1",)) == b"test11value"
    _yottadb.delete(varname="test11", subsarray=("sub1",), delete_type=_yottadb.YDB_DEL_NODE)
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get("test11", ("sub1",))
    # Using bytes arguments
    _yottadb.set(varname=b"test11", subsarray=(b"sub1",), value="test11value")
    assert _yottadb.get(b"test11", (b"sub1",)) == b"test11value"
    _yottadb.delete(varname=b"test11", subsarray=(b"sub1",), delete_type=_yottadb.YDB_DEL_NODE)
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get(b"test11", (b"sub1",))

    # Delete tree
    _yottadb.set(varname="test13", value="test13 node value")
    _yottadb.set(varname="test13", subsarray=("sub1",), value="test13 subnode value")
    assert _yottadb.get("test13") == b"test13 node value"
    assert _yottadb.get("test13", ("sub1",)) == b"test13 subnode value"
    _yottadb.delete(varname="test13", delete_type=_yottadb.YDB_DEL_TREE)
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get("test13")
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get("test13", ("sub1",))
    # Using bytes arguments
    _yottadb.set(varname=b"test13", value=b"test13 node value")
    _yottadb.set(varname=b"test13", subsarray=(b"sub1",), value="test13 subnode value")
    assert _yottadb.get(b"test13") == b"test13 node value"
    assert _yottadb.get(b"test13", (b"sub1",)) == b"test13 subnode value"
    _yottadb.delete(varname=b"test13", delete_type=_yottadb.YDB_DEL_TREE)
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get(b"test13")
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get(b"test13", (b"sub1",))


def test_data(simple_data):
    assert _yottadb.data("^nodata") == _yottadb.YDB_DATA_UNDEF
    assert _yottadb.data("^test1") == _yottadb.YDB_DATA_VALUE_NODESC
    assert _yottadb.data("^test2") == _yottadb.YDB_DATA_NOVALUE_DESC
    assert _yottadb.data("^test2", ("sub1",)) == _yottadb.YDB_DATA_VALUE_NODESC
    assert _yottadb.data("^test3") == _yottadb.YDB_DATA_VALUE_DESC
    assert _yottadb.data("^test3", ("sub1",)) == _yottadb.YDB_DATA_VALUE_DESC
    assert _yottadb.data("^test3", ("sub1", "sub2")) == _yottadb.YDB_DATA_VALUE_NODESC
    # Using bytes arguments
    assert _yottadb.data(b"^test3", (b"sub1", b"sub2")) == _yottadb.YDB_DATA_VALUE_NODESC


def test_lock_incr_varname_only():
    # Varname only
    t1 = datetime.datetime.now()
    _yottadb.lock_incr("test1")
    t2 = datetime.datetime.now()
    time_elapse = t2.timestamp() - t1.timestamp()
    assert time_elapse < 0.01
    _yottadb.lock_decr("test1")
    # Using bytes arguments
    t1 = datetime.datetime.now()
    _yottadb.lock_incr(b"test1")
    t2 = datetime.datetime.now()
    time_elapse = t2.timestamp() - t1.timestamp()
    assert time_elapse < 0.01
    _yottadb.lock_decr(b"test1")


def test_lock_incr_varname_and_subscript():
    # Varname and subscript
    t1 = datetime.datetime.now()
    _yottadb.lock_incr("test2", ("sub1",))
    t2 = datetime.datetime.now()
    time_elapse = t2.timestamp() - t1.timestamp()
    assert time_elapse < 0.01
    _yottadb.lock_decr("test2", ("sub1",))
    # Using bytes arguments
    t1 = datetime.datetime.now()
    _yottadb.lock_incr(b"test2", (b"sub1",))
    t2 = datetime.datetime.now()
    time_elapse = t2.timestamp() - t1.timestamp()
    assert time_elapse < 0.01
    _yottadb.lock_decr(b"test2", (b"sub1",))


def test_lock_incr_timeout_error_varname_only():
    # Timeout error, varname only
    key = KeyTuple("^test1")
    process = multiprocessing.Process(target=lock_value, args=(key,))
    process.start()
    time.sleep(0.5)
    with pytest.raises(_yottadb.YDBTimeoutError):
        _yottadb.lock_incr("^test1")
    process.join()
    # Using bytes arguments
    process = multiprocessing.Process(target=lock_value, args=(key,))
    process.start()
    time.sleep(0.5)
    with pytest.raises(_yottadb.YDBTimeoutError):
        _yottadb.lock_incr(b"^test1")
    process.join()


def test_lock_incr_timeout_error_varname_and_subscript():
    # Timeout error, varname and subscript
    key = KeyTuple("^test2", ("sub1",))
    process = multiprocessing.Process(target=lock_value, args=(key,))
    process.start()
    time.sleep(0.5)
    with pytest.raises(_yottadb.YDBTimeoutError):
        _yottadb.lock_incr("^test2", ("sub1",))
    process.join()
    # Using bytes arguments
    process = multiprocessing.Process(target=lock_value, args=(key,))
    process.start()
    time.sleep(0.5)
    with pytest.raises(_yottadb.YDBTimeoutError):
        _yottadb.lock_incr(b"^test2", (b"sub1",))
    process.join()

    key2 = KeyTuple("^test2", ("sub1",))
    process = multiprocessing.Process(target=lock_value, args=(key2,))
    process.start()
    time.sleep(0.5)
    with pytest.raises(_yottadb.YDBTimeoutError):
        _yottadb.lock_incr("^test2", ("sub1",))
    process.join()
    # Using bytes arguments
    process = multiprocessing.Process(target=lock_value, args=(key2,))
    process.start()
    time.sleep(0.5)
    with pytest.raises(_yottadb.YDBTimeoutError):
        _yottadb.lock_incr(b"^test2", (b"sub1",))
    process.join()


def test_lock_incr_no_timeout():
    # No timeout
    key = KeyTuple("^test2", ("sub1",))
    process = multiprocessing.Process(target=lock_value, args=(key,))
    process.start()
    time.sleep(0.5)
    t1 = datetime.datetime.now()
    _yottadb.lock_incr("test2")
    t2 = datetime.datetime.now()
    time_elapse = t2.timestamp() - t1.timestamp()
    assert time_elapse < 0.01
    _yottadb.lock_decr("^test2", ("sub1",))
    time.sleep(0.5)
    process.join()
    # Using bytes arguments
    process = multiprocessing.Process(target=lock_value, args=(key,))
    time.sleep(0.5)
    t1 = datetime.datetime.now()
    _yottadb.lock_incr(b"test2")
    t2 = datetime.datetime.now()
    time_elapse = t2.timestamp() - t1.timestamp()
    assert time_elapse < 0.01
    _yottadb.lock_decr(b"^test2", (b"sub1",))
    time.sleep(0.5)


# The following functions are Python function wrappers
# for C calls for use in recursive transaction tests


def no_action() -> None:
    pass


def set_key(key: KeyTuple, value: str) -> None:
    _yottadb.set(*key, value=value)


def incr_key(key: KeyTuple, increment: str) -> None:
    _yottadb.incr(*key, increment=increment)


def conditional_set_key(key1: KeyTuple, key2: KeyTuple, value: str, traker_key: KeyTuple) -> None:
    if _yottadb.data(*traker_key) == _yottadb.YDB_DATA_UNDEF:
        _yottadb.set(*key1, value=value)
    else:
        _yottadb.set(*key2, value=value)


def raise_YDBError(undefined_key: KeyTuple) -> None:
    _yottadb.get(*undefined_key)


def raise_standard_python_exception() -> None:
    1 / 0


# Class to hold transaction information for use in
# transaction tests
class TransactionData(NamedTuple):
    action: Callable = no_action
    action_arguments: Tuple = ()
    varnames: Optional[Sequence[str]] = None
    restart_key: KeyTuple = KeyTuple("tptests", ("process_transation", "default"))
    return_value: int = _yottadb.YDB_OK
    restart_timeout: float = -1
    restart_timeout_return_value: int = _yottadb.YDB_OK


# Utility function for handling various transaction scenarios
# within transaction test cases
def process_transaction(nested_transaction_data: Tuple[TransactionData], start_time: Optional[datetime.datetime] = None) -> int:
    # 'current_data' is used to control the actions of the current transaction.
    #     It is set by the caller; it should not change.
    current_data = nested_transaction_data[0]

    current_data.action(*current_data.action_arguments)

    sub_data = nested_transaction_data[1:]
    if len(sub_data) > 0:
        try:
            _yottadb.tp(
                process_transaction,
                kwargs={"nested_transaction_data": sub_data, "start_time": datetime.datetime.now()},
                varnames=current_data.varnames,
            )
        except _yottadb.YDBTPRestart:
            return _yottadb.YDB_TP_RESTART

    if current_data.return_value == _yottadb.YDB_TP_RESTART:
        if current_data.restart_timeout >= 0 and (datetime.datetime.now() - start_time) > datetime.timedelta(
            seconds=current_data.restart_timeout
        ):
            return current_data.restart_timeout_return_value
        elif _yottadb.data(*current_data.restart_key) == _yottadb.YDB_DATA_UNDEF:
            _yottadb.incr(*current_data.restart_key)
            return _yottadb.YDB_TP_RESTART
        else:
            return _yottadb.YDB_OK

    return current_data.return_value


# Transaction/TP tests


def test_tp_return_YDB_OK():
    key = KeyTuple(varname="^tptests", subsarray=("test_tp_return_YDB_OK",))
    value = b"return YDB_OK"
    transaction_data = TransactionData(action=set_key, action_arguments=(key, value), return_value=_yottadb.YDB_OK)
    _yottadb.delete(*key)
    assert _yottadb.data(*key) == _yottadb.YDB_DATA_UNDEF

    _yottadb.tp(process_transaction, kwargs={"nested_transaction_data": (transaction_data,)})

    assert _yottadb.get(*key) == value
    _yottadb.delete("^tptests", delete_type=_yottadb.YDB_DEL_TREE)


def test_tp_nested_return_YDB_OK():
    key1 = KeyTuple(varname="^tptests", subsarray=("test_tp_nested_return_YDB_OK", "outer"))
    value1 = b"return_YDB_OK"
    outer_transaction = TransactionData(action=set_key, action_arguments=(key1, value1), return_value=_yottadb.YDB_OK)
    key2 = KeyTuple(varname="^tptests", subsarray=("test_tp_nested_return_YDB_OK", "nested"))
    value2 = b"nested return_YDB_OK"
    inner_transaction = TransactionData(action=set_key, action_arguments=(key2, value2), return_value=_yottadb.YDB_OK)

    _yottadb.tp(process_transaction, kwargs={"nested_transaction_data": (outer_transaction, inner_transaction)})

    assert _yottadb.get(*key1) == value1
    assert _yottadb.get(*key2) == value2
    _yottadb.delete("^tptests", delete_type=_yottadb.YDB_DEL_TREE)


def test_tp_return_YDB_ROLLBACK():
    key = KeyTuple(varname="^tptests", subsarray=("test_tp_return_YDB_ROLLBACK",))
    value = "return YDB_ROLLBACK"
    transation_data = TransactionData(action=set_key, action_arguments=(key, value), return_value=_yottadb.YDB_TP_ROLLBACK)
    _yottadb.delete(*key)
    assert _yottadb.data(*key) == _yottadb.YDB_DATA_UNDEF

    with pytest.raises(_yottadb.YDBTPRollback):
        _yottadb.tp(process_transaction, kwargs={"nested_transaction_data": (transation_data,)})

    assert _yottadb.data(*key) == _yottadb.YDB_DATA_UNDEF
    _yottadb.delete("^tptests", delete_type=_yottadb.YDB_DEL_TREE)


def test_nested_return_YDB_ROLLBACK():
    key1 = KeyTuple(varname="^tptests", subsarray=("test_nested_return_YDB_ROLLBACK", "outer"))
    value1 = "return YDB_ROLLBACK"
    outer_transaction = TransactionData(action=set_key, action_arguments=(key1, value1), return_value=_yottadb.YDB_TP_ROLLBACK)
    key2 = KeyTuple(varname="^tptests", subsarray=("test_nested_return_YDB_ROLLBACK", "nested"))
    value2 = "nested return YDB_ROLLBACK"
    inner_transaction = TransactionData(action=set_key, action_arguments=(key2, value2), return_value=_yottadb.YDB_TP_ROLLBACK)
    _yottadb.delete(*key1)
    _yottadb.delete(*key2)
    assert _yottadb.data(*key1) == _yottadb.YDB_DATA_UNDEF
    assert _yottadb.data(*key2) == _yottadb.YDB_DATA_UNDEF

    with pytest.raises(_yottadb.YDBTPRollback):
        _yottadb.tp(process_transaction, kwargs={"nested_transaction_data": (outer_transaction, inner_transaction)})

    assert _yottadb.data(*key1) == _yottadb.YDB_DATA_UNDEF
    assert _yottadb.data(*key2) == _yottadb.YDB_DATA_UNDEF
    _yottadb.delete("^tptests", delete_type=_yottadb.YDB_DEL_TREE)


def test_tp_return_YDB_TP_RESTART():
    key1 = KeyTuple(varname="^tptests", subsarray=("test_tp_return_YDB_TP_RESTART", "key1"))
    _yottadb.delete(*key1)
    key2 = KeyTuple(varname="^tptests", subsarray=("test_tp_return_YDB_TP_RESTART", "key2"))
    _yottadb.delete(*key2)
    value = b"restart once"
    tracker = KeyTuple(varname="tptests", subsarray=("test_tp_return_YDB_RESET", "reset count"))
    transaction_data = TransactionData(
        action=conditional_set_key,
        action_arguments=(key1, key2, value, tracker),
        restart_key=tracker,
        return_value=_yottadb.YDB_TP_RESTART,
    )

    _yottadb.tp(process_transaction, kwargs={"nested_transaction_data": (transaction_data,)})

    with pytest.raises(_yottadb.YDBGVUNDEFError):
        _yottadb.get(*key1)
    assert _yottadb.get(*key2) == value
    assert int(_yottadb.get(*tracker)) == 1

    _yottadb.delete("^tptests", delete_type=_yottadb.YDB_DEL_TREE)


def test_nested_tp_return_YDB_TP_RESTART():
    key1_1 = KeyTuple(varname="^tptests", subsarray=("test_nested_tp_return_YDB_TP_RESTART", "outer", "key1"))
    _yottadb.delete(*key1_1)
    key1_2 = KeyTuple(varname="^tptests", subsarray=("test_nested_tp_return_YDB_TP_RESTART", "outer", "key2"))
    _yottadb.delete(*key1_2)
    value1 = b"outer restart once"
    tracker1 = KeyTuple(varname="tptests", subsarray=("test_nested_tp_return_YDB_TP_RESTART", "outer reset count"))
    outer_transaction = TransactionData(
        action=conditional_set_key,
        action_arguments=(key1_1, key1_2, value1, tracker1),
        restart_key=tracker1,
        return_value=_yottadb.YDB_TP_RESTART,
    )

    key2_1 = KeyTuple(varname="^tptests", subsarray=("test_nested_tp_return_YDB_TP_RESTART", "inner", "key1"))
    _yottadb.delete(*key2_1)
    key2_2 = KeyTuple(varname="^tptests", subsarray=("test_nested_tp_return_YDB_TP_RESTART", "inner", "key2"))
    _yottadb.delete(*key2_2)
    value2 = b"inner restart once"
    tracker2 = KeyTuple(varname="tptests", subsarray=("test_nested_tp_return_YDB_TP_RESTART", "inner reset count"))
    inner_transaction = TransactionData(
        action=conditional_set_key,
        action_arguments=(key2_1, key2_2, value2, tracker2),
        restart_key=tracker2,
        return_value=_yottadb.YDB_TP_RESTART,
    )

    _yottadb.tp(process_transaction, kwargs={"nested_transaction_data": (outer_transaction, inner_transaction)})

    with pytest.raises(_yottadb.YDBGVUNDEFError):
        _yottadb.get(*key1_1)
    assert _yottadb.get(*key1_2) == value1
    assert int(_yottadb.get(*tracker1)) == 1
    with pytest.raises(_yottadb.YDBGVUNDEFError):
        _yottadb.get(*key2_1)
    assert _yottadb.get(*key2_2) == value2
    assert int(_yottadb.get(*tracker2)) == 1

    _yottadb.delete(*key1_1)
    _yottadb.delete(*key1_2)
    _yottadb.delete(*tracker1)
    _yottadb.delete(*key2_1)
    _yottadb.delete(*key2_2)
    _yottadb.delete(*tracker2)
    _yottadb.delete("^tptests", delete_type=_yottadb.YDB_DEL_TREE)


def test_tp_return_YDB_TP_RESTART_reset_all():
    key = KeyTuple(varname="^tptests", subsarray=("test_tp_return_YDB_TP_RESTART_reset_all", "resetvalue"))
    tracker = KeyTuple(varname="tptests", subsarray=("test_tp_return_YDB_TP_RESTART_reset_all", "reset_count"))
    _yottadb.delete(*key)

    transaction_data = TransactionData(
        action=incr_key,
        action_arguments=(key, "1"),
        restart_key=tracker,
        return_value=_yottadb.YDB_TP_RESTART,
        restart_timeout=0.01,
        restart_timeout_return_value=_yottadb.YDB_OK,
    )

    _yottadb.tp(
        process_transaction,
        kwargs={"nested_transaction_data": (transaction_data,), "start_time": datetime.datetime.now()},
        varnames=("*",),
    )

    assert _yottadb.get(*key) == b"1"
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get(*tracker)
    _yottadb.delete("^tptests", delete_type=_yottadb.YDB_DEL_TREE)


def test_tp_return_YDB_ERR_TPTIMEOUT():
    key = KeyTuple(varname="^tptests", subsarray=("test_tp_return_YDB_ERR_TPTIMEOUT",))
    value = b"return YDB_ERR_TPTIMEOUT"
    transaction_data = TransactionData(action=set_key, action_arguments=(key, value), return_value=_yottadb.YDB_ERR_TPTIMEOUT)
    _yottadb.delete(*key)
    assert _yottadb.data(*key) == _yottadb.YDB_DATA_UNDEF

    with pytest.raises(_yottadb.YDBTPTimeoutError):
        _yottadb.tp(process_transaction, kwargs={"nested_transaction_data": (transaction_data,)})

    assert _yottadb.data(*key) == _yottadb.YDB_DATA_UNDEF
    _yottadb.delete("^tptests", delete_type=_yottadb.YDB_DEL_TREE)


def test_tp_nested_return_YDB_ERR_TPTIMEOUT():
    key1 = KeyTuple(varname="^tptests", subsarray=("test_nested_return_YDB_ERR_TPTIMEOUT", "outer"))
    value1 = "return YDB_ERR_TPTIMEOUT"
    outer_transaction = TransactionData(action=set_key, action_arguments=(key1, value1), return_value=_yottadb.YDB_ERR_TPTIMEOUT)
    key2 = KeyTuple(varname="^tptests", subsarray=("test_nested_return_YDB_ERR_TPTIMEOUT", "nested"))
    value2 = "nested return YDB_ERR_TPTIMEOUT"
    inner_transaction = TransactionData(action=set_key, action_arguments=(key2, value2), return_value=_yottadb.YDB_ERR_TPTIMEOUT)

    with pytest.raises(_yottadb.YDBTPTimeoutError):
        _yottadb.tp(process_transaction, kwargs={"nested_transaction_data": (outer_transaction, inner_transaction)})

    assert _yottadb.data(*key1) == _yottadb.YDB_DATA_UNDEF
    assert _yottadb.data(*key2) == _yottadb.YDB_DATA_UNDEF
    _yottadb.delete("^tptests", delete_type=_yottadb.YDB_DEL_TREE)


def test_tp_raise_YDBError():
    key = KeyTuple(varname="^tptests", subsarray=("test_tp_raise_YDBError",))
    assert _yottadb.data(*key) == _yottadb.YDB_DATA_UNDEF
    transaction_data = TransactionData(action=raise_YDBError, action_arguments=(key,))

    with pytest.raises(_yottadb.YDBGVUNDEFError):
        _yottadb.tp(process_transaction, kwargs={"nested_transaction_data": (transaction_data,)})
    _yottadb.delete("^tptests", delete_type=_yottadb.YDB_DEL_TREE)


def test_tp_nested_raise_YDBError():
    outer_transaction = TransactionData()
    key = KeyTuple(varname="^tptests", subsarray=("test_nested_tp_raise_YDBError",))
    inner_transaction = TransactionData(action=raise_YDBError, action_arguments=(key,))
    assert _yottadb.data(*key) == _yottadb.YDB_DATA_UNDEF

    with pytest.raises(_yottadb.YDBGVUNDEFError):
        _yottadb.tp(process_transaction, kwargs={"nested_transaction_data": (outer_transaction, inner_transaction)})
    _yottadb.delete("^tptests", delete_type=_yottadb.YDB_DEL_TREE)


def test_tp_raise_standard_python_exception():
    transaction_data = TransactionData(action=raise_standard_python_exception)
    with pytest.raises(ZeroDivisionError):
        _yottadb.tp(process_transaction, kwargs={"nested_transaction_data": (transaction_data,)})


def test_tp_nested_raise_standard_python_exception():
    outer_transaction = TransactionData()
    inner_transaction = TransactionData(action=raise_standard_python_exception)
    with pytest.raises(ZeroDivisionError):
        _yottadb.tp(process_transaction, kwargs={"nested_transaction_data": (outer_transaction, inner_transaction)})
    _yottadb.delete("^tptests", delete_type=_yottadb.YDB_DEL_TREE)


# YDB_MAX_TP_DEPTH is the maximum transaction recursion depth of YottaDB. Any recursive set of transactions greater
# than this depth will result in a _yottadb.YDBTPTOODEEPError
YDB_MAX_TP_DEPTH = 126


@pytest.mark.parametrize("depth", range(1, YDB_MAX_TP_DEPTH + 1))
def test_tp_return_YDB_OK_to_depth(depth):
    def key_at_level(level: int) -> KeyTuple:
        return KeyTuple(varname="^tptests", subsarray=(f"test_tp_return_YDB_to_depth{depth}", f"level{level}"))

    def value_at_level(level: int) -> bytes:
        return bytes(f"level{level} returns YDB_OK", encoding="utf-8")

    transaction_data = []
    for level in range(0, depth):
        transaction_data.append(TransactionData(action=set_key, action_arguments=(key_at_level(level), value_at_level(level))))

    _yottadb.tp(process_transaction, kwargs={"nested_transaction_data": transaction_data})

    for level in range(0, depth):
        assert _yottadb.get(*key_at_level(level)) == value_at_level(level)

    for level in range(0, depth):
        _yottadb.delete(*key_at_level(level))
    _yottadb.delete("^tptests", delete_type=_yottadb.YDB_DEL_TREE)


# somewhat realistic tp tests
@pytest.fixture(scope="function")
def bank():
    account1 = "acc#1234"
    account2 = "acc#5678"
    account1_balance = 1234
    account2_balance = 5678
    _yottadb.set(varname="^account", subsarray=(account1, "balance"), value=str(account1_balance))
    _yottadb.set(varname="^account", subsarray=(account2, "balance"), value=str(account2_balance))

    yield [{"account#": account1, "account_balance": account1_balance}, {"account#": account2, "account_balance": account2_balance}]

    _yottadb.delete(varname="^account", delete_type=_yottadb.YDB_DEL_TREE)


def transfer_transaction(from_account, to_account, amount):
    from_account_balance = int(_yottadb.get(varname="^account", subsarray=(from_account, "balance")))
    to_account_balance = int(_yottadb.get(varname="^account", subsarray=(to_account, "balance")))

    _yottadb.set(varname="^account", subsarray=(from_account, "balance"), value=str(from_account_balance - amount))
    _yottadb.set(varname="^account", subsarray=(to_account, "balance"), value=str(to_account_balance + amount))

    new_from_balance = int(_yottadb.get(varname="^account", subsarray=(from_account, "balance")))

    if new_from_balance < 0:
        return _yottadb.YDB_TP_ROLLBACK
    else:
        return _yottadb.YDB_OK


def test_tp_bank_transfer_ok(bank):
    account1 = bank[0]["account#"]
    account2 = bank[1]["account#"]
    account1_balance = bank[0]["account_balance"]
    account2_balance = bank[1]["account_balance"]
    transfer_amount = account1_balance - 1

    _yottadb.tp(transfer_transaction, args=(account1, account2, transfer_amount), kwargs={})

    assert int(_yottadb.get(varname="^account", subsarray=(account1, "balance"))) == account1_balance - transfer_amount
    assert int(_yottadb.get(varname="^account", subsarray=(account2, "balance"))) == account2_balance + transfer_amount


def test_tp_bank_transfer_rollback(bank):
    account1 = bank[0]["account#"]
    account2 = bank[1]["account#"]
    account1_balance = bank[0]["account_balance"]
    account2_balance = bank[1]["account_balance"]
    transfer_amount = account1_balance + 1
    _yottadb.set(varname="account", subsarray=(account1, "balance"), value=str(account1_balance))
    _yottadb.set(varname="account", subsarray=(account2, "balance"), value=str(account2_balance))
    with pytest.raises(_yottadb.YDBTPRollback):
        _yottadb.tp(transfer_transaction, args=(account1, account2, transfer_amount), kwargs={})

    assert int(_yottadb.get(varname="account", subsarray=(account1, "balance"))) == account1_balance
    assert int(_yottadb.get(varname="account", subsarray=(account2, "balance"))) == account2_balance


def callback_for_tp_simple_restart(start_time):
    now = datetime.datetime.now()
    _yottadb.incr("resetattempt", increment="1")
    _yottadb.incr("resetvalue", increment="1")
    if _yottadb.get("resetattempt") == b"2":
        return _yottadb.YDB_OK
    elif (now - start_time) > datetime.timedelta(seconds=0.01):
        return _yottadb.YDB_OK
    else:
        return _yottadb.YDB_TP_RESTART

    return _yottadb.YDB_TP_RESTART


def test_tp_reset_some():
    yottadb.set("resetattempt", value="0")
    yottadb.set("resetvalue", value="0")
    start_time = datetime.datetime.now()
    result = _yottadb.tp(callback_for_tp_simple_restart, args=(start_time,), varnames=("resetvalue",))
    assert result == _yottadb.YDB_OK
    assert _yottadb.get("resetattempt") == b"2"
    assert _yottadb.get("resetvalue") == b"1"
    yottadb.delete_node("resetattempt")
    yottadb.delete_node("resetvalue")


def test_subscript_next_1(simple_data):
    assert _yottadb.subscript_next(varname="^%") == "^Test5"
    assert _yottadb.subscript_next(varname="^a") == "^test1"
    assert _yottadb.subscript_next(varname="^test1") == "^test2"
    assert _yottadb.subscript_next(varname="^test2") == "^test3"
    assert _yottadb.subscript_next(varname="^test3") == "^test4"
    with pytest.raises(_yottadb.YDBNODEENDError):
        _yottadb.subscript_next(varname="^test6")

    assert _yottadb.subscript_next(varname="^test4", subsarray=("",)) == "sub1"
    assert _yottadb.subscript_next(varname="^test4", subsarray=("sub1",)) == "sub2"
    assert _yottadb.subscript_next(varname="^test4", subsarray=("sub2",)) == "sub3"
    with pytest.raises(_yottadb.YDBNODEENDError):
        _yottadb.subscript_next(varname="^test4", subsarray=("sub3",))

    assert _yottadb.subscript_next(varname="^test4", subsarray=("sub1", "")) == "subsub1"
    assert _yottadb.subscript_next(varname="^test4", subsarray=("sub1", "subsub1")) == "subsub2"
    assert _yottadb.subscript_next(varname="^test4", subsarray=("sub1", "subsub2")) == "subsub3"
    with pytest.raises(_yottadb.YDBNODEENDError):
        _yottadb.subscript_next(varname="^test4", subsarray=("sub3", "subsub3"))


def test_subscript_next_long():
    _yottadb.set(varname="testLongSubscript", subsarray=("a" * _yottadb.YDB_MAX_STR,), value="toolong")
    assert _yottadb.subscript_next(varname="testLongSubscript", subsarray=("",)) == "a" * _yottadb.YDB_MAX_STR


def test_subscript_next_i18n():
    _yottadb.set(varname="testi18n", subsarray=("中文",), value="chinese")
    assert _yottadb.subscript_next(varname="testi18n", subsarray=("",)) == "中文"


def test_subscript_previous_1(simple_data):
    assert _yottadb.subscript_previous(varname="^z") == "^test6"
    assert _yottadb.subscript_previous(varname="^a") == "^Test5"
    assert _yottadb.subscript_previous(varname="^test1") == "^Test5"
    assert _yottadb.subscript_previous(varname="^test2") == "^test1"
    assert _yottadb.subscript_previous(varname="^test3") == "^test2"
    assert _yottadb.subscript_previous(varname="^test4") == "^test3"
    with pytest.raises(_yottadb.YDBNODEENDError):
        _yottadb.subscript_previous(varname="^Test5")

    assert _yottadb.subscript_previous(varname="^test4", subsarray=("",)) == "sub3"
    assert _yottadb.subscript_previous(varname="^test4", subsarray=("sub2",)) == "sub1"
    assert _yottadb.subscript_previous(varname="^test4", subsarray=("sub3",)) == "sub2"
    with pytest.raises(_yottadb.YDBNODEENDError):
        _yottadb.subscript_previous(varname="^test4", subsarray=("sub1",))

    assert _yottadb.subscript_previous(varname="^test4", subsarray=("sub1", "")) == "subsub3"
    assert _yottadb.subscript_previous(varname="^test4", subsarray=("sub1", "subsub2")) == "subsub1"
    assert _yottadb.subscript_previous(varname="^test4", subsarray=("sub1", "subsub3")) == "subsub2"
    with pytest.raises(_yottadb.YDBNODEENDError):
        _yottadb.subscript_previous(varname="^test4", subsarray=("sub3", "subsub1"))


def test_subscript_previous_long():
    _yottadb.set(varname="testLongSubscript", subsarray=("a" * _yottadb.YDB_MAX_STR,), value="toolong")
    assert _yottadb.subscript_previous(varname="testLongSubscript", subsarray=("",)) == "a" * _yottadb.YDB_MAX_STR


def test_node_next_1(simple_data):
    assert _yottadb.node_next("^test3") == ("sub1",)
    assert _yottadb.node_next("^test3", subsarray=("sub1",)) == ("sub1", "sub2")
    with pytest.raises(_yottadb.YDBNODEENDError):
        _yottadb.node_next(varname="^test3", subsarray=("sub1", "sub2"))
    assert _yottadb.node_next("^test6") == ("sub6", "subsub6")


def test_node_next_many_subscipts():
    _yottadb.set(varname="testmanysubscripts", subsarray=("sub1", "sub2", "sub3", "sub4", "sub5", "sub6"), value="123")
    assert _yottadb.node_next("testmanysubscripts") == ("sub1", "sub2", "sub3", "sub4", "sub5", "sub6")


def test_node_next_long_subscripts():
    _yottadb.set(varname="testlong", subsarray=("a" * 1025, "a" * 1026), value="123")
    assert _yottadb.node_next("testlong") == ("a" * 1025, "a" * 1026)


def test_node_previous_1(simple_data):
    with pytest.raises(_yottadb.YDBNODEENDError):
        _yottadb.node_previous("^test3")
    assert _yottadb.node_previous("^test3", ("sub1",)) == ()
    assert _yottadb.node_previous("^test3", subsarray=("sub1", "sub2")) == ("sub1",)


def test_node_previous_long_subscripts():
    _yottadb.set(varname="testlong", subsarray=("a" * 1025, "a" * 1026), value="123")
    _yottadb.set(varname="testlong", subsarray=("a" * 1025, "a" * 1026, "a"), value="123")
    assert _yottadb.node_previous("testlong", ("a" * 1025, "a" * 1026, "a")) == ("a" * 1025, "a" * 1026)


def test_lock_blocking_other(simple_data):
    t1 = KeyTuple("^test1")
    t2 = KeyTuple("^test2", ("sub1",))
    t3 = KeyTuple("^test3", ("sub1", "sub2"))
    keys_to_lock = (t1, t2, t3)
    _yottadb.lock(keys=keys_to_lock, timeout_nsec=0)
    # Attempt to increment/decrement locks
    processes = []
    for key in keys_to_lock:
        process = multiprocessing.Process(target=lock_value, args=(key,))
        process.start()
        processes.append(process)
    for process in processes:
        process.join()
        assert process.exitcode == 1
    # Release all locks
    _yottadb.lock()
    # Attempt to increment/decrement locks
    processes = []
    for key in keys_to_lock:
        process = multiprocessing.Process(target=lock_value, args=(key,))
        process.start()
        processes.append(process)
    for process in processes:
        process.join()
        assert process.exitcode == 0


def test_lock_being_blocked():
    key = KeyTuple("^test1")
    process = multiprocessing.Process(target=lock_value, args=(key,))
    process.start()
    time.sleep(1)
    with pytest.raises(_yottadb.YDBTimeoutError):
        _yottadb.lock([key])
    process.join()


def test_delete_excel():
    _yottadb.set(varname="testdeleteexcel1", value="1")
    _yottadb.set(varname="testdeleteexcel2", subsarray=("sub1",), value="2")
    _yottadb.set(varname="testdeleteexcelexception", subsarray=("sub1",), value="3")
    _yottadb.delete_excel(varnames=("testdeleteexcelexception",))
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get("testdeleteexcel1")
    with pytest.raises(_yottadb.YDBLVUNDEFError):
        _yottadb.get("testdeleteexcel2", ("sub1",))
    assert _yottadb.get("testdeleteexcelexception", ("sub1",)) == b"3"


increment_tests = [
    # string input tests
    ("0", "1", "1"),
    ("0", "1E1", "10"),
    ("0", "1E16", "1" + "0" * 16),
    ("0", "9" * 6, "9" * 6),
    ("0", "9" * 7 + "E0", "9" * 7),
    ("0", "9" * 6 + "E1", "9" * 6 + "0"),
    ("0", "9" * 18 + "E0", "9" * 18),
    ("9999998", "1", "9" * 7),
    ("999999999999999998", "1", "9" * 18),
    ("999999999999888888", "111111", "9" * 18),
    ("999999999998888888", "1111111", "9" * 18),
    ("999999999990000000", "9999999", "9" * 18),
    ("1", "-1234567", "-1234566"),
    ("0", "1234567E40", "1234567" + "0" * 40),
    ("0", "1234567E-43", "." + "0" * 36 + "1234567"),
    ("0", "123456789123456789E-43", "." + "0" * 25 + "123456789123456789"),
    ("0", "123456789123456789E29", "123456789123456789" + "0" * 29),
    ("0", "1234567", "1234567"),
    ("0", "-1234567", "-1234567"),
    ("0", "100.0001", "100.0001"),
    ("0", "1" + "0" * 6, "1" + "0" * 6),
    ("0", "1" * 7, "1" * 7),
    ("0", "9" * 7, "9" * 7),
    ("0", "9" * 6 + "0", "9" * 6 + "0"),
    ("0", "100.001", "100.001"),
    ("1", "100.0001", "101.0001"),
    ("1", "9" * 18, "1" + "0" * 18),
    ("0", "-1234567E0", "-1234567"),
    ("1", "9" * 6 + "8", "9" * 7),
    ("1", "9" * 6 + "0", "9" * 6 + "1"),
    ("0", "1E-43", "." + "0" * 42 + "1"),
    ("0", ".1E47", "1" + "0" * 46),  # max magnitude
    ("0", "-.1E47", "-1" + "0" * 46),  # max magnitude
    ("0", "1E-43", "." + "0" * 42 + "1"),  # min magnitude
    ("0", "-1E-43", "-." + "0" * 42 + "1"),  # min magnitude
    # int input tests
    ("0", 1, "1"),
    ("0", 10, "10"),
    ("9999998", 1, "9" * 7),
    ("999999999999999998", 1, "9" * 18),
    ("999999999999888888", 111111, "9" * 18),
    ("999999999998888888", 1111111, "9" * 18),
    ("999999999990000000", 9999999, "9" * 18),
    ("1", -1234567, "-1234566"),
    ("1", 999999999999999998, "9" * 18),  # 18 significant digits
    ("0", 99999_99999_99999_999, "9" * 18),  # 18 significant digits
    ("-1", -999999999999999998, "-" + "9" * 18),  # 18 significant digits
    ("0", -99999_99999_99999_999, "-" + "9" * 18),  # 18 significant digits
    ("0", 10000_00000_00000_00000_00000_00000_00000_00000_00000_00, "1" + "0" * 46),  # max int magnitude
    ("0", -10000_00000_00000_00000_00000_00000_00000_00000_00000_00, "-1" + "0" * 46),  # max int magnitude
    # float input tests
    ("0", 1.0, "1"),
    ("0", 0.1, ".1"),
    ("0", 1e1, "10"),
    ("0", 1e16, "1" + "0" * 16),
    ("0", 1e-43, "." + "0" * 42 + "1"),  # max float magnitude
    ("0", -1e-43, "-." + "0" * 42 + "1"),  # max float magnitude
    ("0", 0.1e47, "1" + "0" * 46),  # min float magnitude
    ("0", -0.1e47, "-1" + "0" * 46),  # min float magnitude
    # Decimal input tests
    ("0", Decimal("1"), "1"),
    ("0", Decimal("1E1"), "10"),
    ("0", Decimal("1E16"), "1" + "0" * 16),
    ("0", Decimal(".1E47"), "1" + "0" * 46),  # max Decimal magnitude
    ("0", Decimal("-.1E47"), "-1" + "0" * 46),  # max Decimal magnitude
    ("0", Decimal(".1E47"), "1" + "0" * 46),  # min float magnitude
    ("0", Decimal("-.1E47"), "-1" + "0" * 46),  # min float magnitude
]

increment_test_ids = [
    f'"{initial}" | "{type(increment).__name__}({increment})" | {result}' for initial, increment, result in increment_tests
]

increment_keys = [
    KeyTuple("testincrparamaterized"),
    KeyTuple("^testincrparamaterized"),
    KeyTuple("^testincrparamaterized", ("sub1",)),
    KeyTuple("testincrparamaterized", ("sub1",)),
]

increment_key_test_ids = [f'"{key.varname}({key.subsarray})' for key in increment_keys]


def number_to_str(number):
    number_str = number
    if not isinstance(number, str):
        number_str = f"{number}".upper().replace("+", "")
    if len(number_str) >= 7 and "E" not in number_str:  # bug workaround
        number_str += "E0"
    return number_str


@pytest.mark.parametrize("initial, increment, result", increment_tests, ids=increment_test_ids)
@pytest.mark.parametrize("key", increment_keys, ids=increment_key_test_ids)
def test_incr(key, initial, increment, result):
    _yottadb.set(*key, value=initial)
    returned_value = _yottadb.incr(*key, increment=number_to_str(increment))

    assert returned_value == bytes(result, encoding="utf8")
    assert _yottadb.get(*key) == bytes(result, encoding="ascii")
    _yottadb.delete(*key, _yottadb.YDB_DEL_TREE)


increment_error_test = [
    ("0", "1E47", _yottadb.YDBNUMOFLOWError),
    ("0", "-1E47", _yottadb.YDBNUMOFLOWError),
    # ("0", "1E-47", _yottadb.YDBNUMOFLOWError),
]

increment_test_ids = [
    f'"{initial}" | "{type(increment).__name__}({increment})" | {error_type}'
    for initial, increment, error_type in increment_error_test
]


@pytest.mark.parametrize("initial, increment, error_type", increment_error_test, ids=increment_test_ids)
@pytest.mark.parametrize("key", increment_keys, ids=increment_key_test_ids)
def test_incr_errors(key, initial, increment, error_type):
    _yottadb.set(*key, value=initial)

    with pytest.raises(error_type):
        _yottadb.incr(*key, increment=number_to_str(increment))
        assert _yottadb.get(*key) == initial
    _yottadb.delete(*key, _yottadb.YDB_DEL_TREE)


@pytest.mark.parametrize("input, output1, output2", str2zwr_tests)
def test_str2zwr(input, output1, output2):
    if os.environ["ydb_chset"] == "UTF-8":
        assert _yottadb.str2zwr(input) == output2
    else:
        assert _yottadb.str2zwr(input) == output1


@pytest.mark.parametrize("output1, output2, input", str2zwr_tests)
def test_zwr2str(input, output1, output2):
    if os.environ["ydb_chset"] == "UTF-8":
        assert _yottadb.zwr2str(input) == output1
    else:
        assert _yottadb.zwr2str(input) == output2
