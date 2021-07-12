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
import datetime
import time
import os
import re
from typing import NamedTuple, Callable, Tuple

import yottadb
from conftest import lock_value, str2zwr_tests, set_ci_environment, reset_ci_environment


def test_ci_table():
    cur_dir = os.getcwd()
    previous = set_ci_environment(cur_dir, "")
    cur_handle = yottadb.open_ci_table(cur_dir + "/tests/calltab.ci")
    yottadb.switch_ci_table(cur_handle)

    # Ensure no errors from basic/expected usage
    assert "3241" == yottadb.ci("HelloWorld2", ["1", "24", "3"], has_retval=True)
    # Test updating of IO parameter (second parameter) with a shorter value,
    # i.e. set arg_list[1] = arg_list[0]
    arg_list = ["1", "24", "3"]
    assert "3241" == yottadb.ci("HelloWorld2", arg_list, has_retval=True)
    assert arg_list[1] == "1"
    # Test None returned from routine with no return value,
    # also test update of output only parameter
    outarg = ""
    outargs = [outarg]
    assert yottadb.ci("NoRet", outargs) is None
    assert outargs[0] == "testeroni"
    # Test routine with no parameters
    assert "entry called" == yottadb.ci("HelloWorld1", has_retval=True)
    # Test routine with output parameter succeeds where the length of
    # the passed argument is equal to the length needed to store the result
    assert "1234567890" == yottadb.ci("StringExtend", [9876543210], has_retval=True)
    # Test routine with output parameter succeeds where the length of
    # the passed argument is greater than the length needed to store the result
    assert "1234567890" == yottadb.ci("StringExtend", [98765432101234], has_retval=True)
    # Test routine with output parameter succeeds when passed the empty string
    assert "1234567890" == yottadb.ci("StringExtend", [""], has_retval=True)

    old_handle = cur_handle
    cur_handle = yottadb.open_ci_table(cur_dir + "/tests/testcalltab.ci")
    last_handle = yottadb.switch_ci_table(cur_handle)
    assert last_handle == old_handle
    assert "entry was called" == yottadb.ci("HelloWorld99", has_retval=True)

    # Reset call-in table for other tests
    cur_handle = yottadb.open_ci_table(cur_dir + "/tests/calltab.ci")
    yottadb.switch_ci_table(cur_handle)

    reset_ci_environment(previous)


# Test ci() call using ydb_ci environment variable to specify call-in table
# location. This is the default usage.
def test_ci_default():
    cur_dir = os.getcwd()
    previous = set_ci_environment(cur_dir, cur_dir + "/tests/calltab.ci")

    # Ensure no errors from basic/expected usage
    assert "-1" == yottadb.ci("Passthrough", [-1], has_retval=True)
    assert "3241" == yottadb.ci("HelloWorld2", [1, 24, 3], has_retval=True)
    assert "3241" == yottadb.ci("HelloWorld2", ["1", "24", "3"], has_retval=True)
    # Test updating of IO parameter (second parameter) with a shorter value,
    # i.e. set arg_list[1] = arg_list[0]
    arg_list = ["1", "24", "3"]
    assert "3241" == yottadb.ci("HelloWorld2", arg_list, has_retval=True)
    assert arg_list[1] == "1"
    # Test None returned from routine with no return value,
    # also test update of output only parameter
    outarg = ""
    outargs = [outarg]
    assert yottadb.ci("NoRet", outargs) is None
    assert outargs[0] == "testeroni"
    # Test routine with no parameters
    assert "entry called" == yottadb.ci("HelloWorld1", has_retval=True)

    reset_ci_environment(previous)


def test_cip():
    cur_dir = os.getcwd()
    previous = set_ci_environment(cur_dir, cur_dir + "/tests/calltab.ci")

    # Ensure no errors from basic/expected usage
    assert "-1" == yottadb.cip("Passthrough", [-1], has_retval=True)
    assert "3241" == yottadb.cip("HelloWorld2", [1, 24, 3], has_retval=True)
    assert "3241" == yottadb.cip("HelloWorld2", ["1", "24", "3"], has_retval=True)
    # Test updating of IO parameter (second parameter) with a shorter value,
    # i.e. set arg_list[1] = arg_list[0]
    arg_list = ["1", "24", "3"]
    assert "3241" == yottadb.cip("HelloWorld2", arg_list, has_retval=True)
    assert arg_list[1] == "1"
    # Test None returned from routine with no return value,
    # also test update of output only parameter
    outarg = ""
    outargs = [outarg]
    assert yottadb.cip("NoRet", outargs) is None
    assert outargs[0] == "testeroni"
    outargs[0] = "new value"
    assert yottadb.cip("NoRet", outargs) is None
    assert outargs[0] == "testeroni"
    # Test routine with no parameters
    assert "entry called" == yottadb.cip("HelloWorld1", has_retval=True)
    # Test routine with output parameter succeeds where the length of
    # the passed argument is equal to the length needed to store the result
    assert "1234567890" == yottadb.cip("StringExtend", [9876543210], has_retval=True)
    # Test routine with output parameter succeeds where the length of
    # the passed argument is greater than the length needed to store the result
    assert "1234567890" == yottadb.cip("StringExtend", [98765432101234], has_retval=True)
    # Test routine with output parameter succeeds when passed the empty string
    assert "1234567890" == yottadb.ci("StringExtend", [""], has_retval=True)

    reset_ci_environment(previous)


def test_message():
    assert yottadb.message(yottadb.YDB_ERR_INVSTRLEN) == "%YDB-E-INVSTRLEN, Invalid string length !UL: max !UL"
    with pytest.raises(yottadb.YDBError):
        yottadb.message(0)  # Raises unknown error number error


def test_release():
    release = yottadb.release()
    assert re.match("YottaDB.*", release) is not None
    print(f"Release: {release}\n")


def test_key_object(simple_data):
    # Key creation, varname only
    key = yottadb.Key("^test1")
    assert key == b"test1value"
    assert key.name == "^test1"
    assert key.varname_key == key
    assert key.varname == "^test1"
    assert key.subsarray == []
    # Using bytes argument
    key = yottadb.Key(b"^test1")
    assert key == b"test1value"
    assert key.name == b"^test1"
    assert key.varname_key == key
    assert key.varname == b"^test1"
    assert key.subsarray == []

    # Key creation, varname and subscript
    key = yottadb.Key("^test2")["sub1"]
    assert key == b"test2value"
    assert key.name == "sub1"
    assert key.varname_key == yottadb.Key("^test2")
    assert key.varname == "^test2"
    assert key.subsarray == ["sub1"]
    # Using bytes arguments
    key = yottadb.Key(b"^test2")[b"sub1"]
    assert key == b"test2value"
    assert key.name == b"sub1"
    assert key.varname_key == yottadb.Key(b"^test2")
    assert key.varname == b"^test2"
    assert key.subsarray == [b"sub1"]

    # Key creation and value update, varname and subscript
    key = yottadb.Key("test3local")["sub1"]
    key.value = "smoketest3local"
    assert key == b"smoketest3local"
    assert key.name == "sub1"
    assert key.varname_key == yottadb.Key("test3local")
    assert key.varname == "test3local"
    assert key.subsarray == ["sub1"]
    # Using bytes arguments
    key = yottadb.Key(b"test3local")[b"sub1"]
    key.value = b"smoketest3local"
    assert key == b"smoketest3local"
    assert key.name == b"sub1"
    assert key.varname_key == yottadb.Key(b"test3local")
    assert key.varname == b"test3local"
    assert key.subsarray == [b"sub1"]

    # YDBLVUNDEFError and YDBGVUNDEFError for Key.value return None
    key = yottadb.Key("^nonexistent")  # Undefined global
    assert key.value is None
    key = yottadb.Key("nonexistent")  # Undefined local
    assert key.value is None

    # Key incrementation
    key = yottadb.Key("localincr")
    assert key.incr() == b"1"
    assert key.incr(-1) == b"0"
    assert key.incr("2") == b"2"
    assert key.incr("testeroni") == b"2"  # Not a canonical number, leaves value unchanged
    with pytest.raises(TypeError):
        key.incr(None)  # Must be int, str, float, or bytes
    # Using bytes argument for Key (carrying over previous node value)
    key = yottadb.Key(b"localincr")
    assert key.incr() == b"3"
    assert key.incr(-1) == b"2"
    assert key.incr(b"2") == b"4"
    with pytest.raises(TypeError):
        key.incr([])  # Must be int, str, float, or bytes

    # Key comparison via __eq__ (Key/value comparisons tested in various other
    # test cases below)
    key = yottadb.Key("testeroni")["sub1"]
    key_copy = yottadb.Key("testeroni")["sub1"]
    key2 = yottadb.Key("testeroni")["sub2"]
    # Same varname/subscripts, but different object should be equal
    assert key == key_copy
    # Different varname/subscripts should not be equal
    assert key != key2

    # Incrementation/decrementation using += and -= syntax (__iadd__ and __isub__ methods)
    key = yottadb.Key("iadd")
    key += 1
    assert int(key.value) == 1
    key -= 1
    assert int(key.value) == 0
    key += "2"
    assert int(key.value) == 2
    key -= "3"
    assert int(key.value) == -1
    key += 0.5
    assert float(key.value) == -0.5
    key -= -1.5
    assert int(key.value) == 1
    key += 0.5
    assert float(key.value) == 1.5
    key += "testeroni"  # Not a canonical number, leaves value unchanged
    assert float(key.value) == 1.5
    with pytest.raises(TypeError):
        key += ("tuple",)  # Must be int, str, float, or bytes


def test_Key_construction_error():
    with pytest.raises(TypeError):
        yottadb.Key("^test1", "not a Key object")


def test_Key__str__():
    assert str(yottadb.Key("test")) == "test"
    assert str(yottadb.Key("test")["sub1"]) == "test(sub1)"
    assert str(yottadb.Key("test")["sub1"]["sub2"]) == "test(sub1,sub2)"


def test_Key_get_value1(simple_data):
    assert yottadb.Key("^test1") == b"test1value"


def test_Key_get_value2(simple_data):
    assert yottadb.Key("^test2")["sub1"] == b"test2value"


def test_Key_get_value3(simple_data):
    assert yottadb.Key("^test3") == b"test3value1"
    assert yottadb.Key("^test3")["sub1"] == b"test3value2"
    assert yottadb.Key("^test3")["sub1"]["sub2"] == b"test3value3"


def test_Key_subsarray(simple_data):
    assert yottadb.Key("^test3").subsarray == []
    assert yottadb.Key("^test3")["sub1"].subsarray == ["sub1"]
    assert yottadb.Key("^test3")["sub1"]["sub2"].subsarray == ["sub1", "sub2"]
    # Confirm no UnboundLocalError when a Key has no subscripts
    for subscript in yottadb.Key("^test3").subscripts:
        pass


def test_Key_varname(simple_data):
    assert yottadb.Key("^test3").varname == "^test3"
    assert yottadb.Key("^test3")["sub1"].varname == "^test3"
    assert yottadb.Key("^test3")["sub1"]["sub2"].varname == "^test3"


def test_Key_set_value1():
    testkey = yottadb.Key("test4")
    testkey.value = "test4value"
    assert testkey == b"test4value"


def test_Key_set_value2():
    testkey = yottadb.Key("test5")["sub1"]
    testkey.value = "test5value"
    assert testkey == b"test5value"
    assert yottadb.Key("test5")["sub1"] == b"test5value"


def test_Key_set_value3():
    yottadb.Key("test5")["sub1"] = "test5value"
    assert yottadb.Key("test5")["sub1"] == b"test5value"


def test_Key_delete_node():
    testkey = yottadb.Key("test6")
    subkey = testkey["sub1"]
    testkey.value = "test6value"
    subkey.value = "test6 subvalue"

    assert testkey == b"test6value"
    assert subkey == b"test6 subvalue"

    testkey.delete_node()

    assert testkey.value is None
    assert subkey == b"test6 subvalue"


def test_Key_delete_tree():
    testkey = yottadb.Key("test7")
    subkey = testkey["sub1"]
    testkey.value = "test7value"
    subkey.value = "test7 subvalue"

    assert testkey == b"test7value"
    assert subkey == b"test7 subvalue"

    testkey.delete_tree()

    assert testkey.value is None
    assert subkey.value is None
    assert testkey.data == 0


def test_Key_data(simple_data):
    assert yottadb.Key("nodata").data == yottadb.YDB_DATA_UNDEF
    assert yottadb.Key("^test1").data == yottadb.YDB_DATA_VALUE_NODESC
    assert yottadb.Key("^test2").data == yottadb.YDB_DATA_NOVALUE_DESC
    assert yottadb.Key("^test2")["sub1"].data == yottadb.YDB_DATA_VALUE_NODESC
    assert yottadb.Key("^test3").data == yottadb.YDB_DATA_VALUE_DESC
    assert yottadb.Key("^test3")["sub1"].data == yottadb.YDB_DATA_VALUE_DESC
    assert yottadb.Key("^test3")["sub1"]["sub2"].data == yottadb.YDB_DATA_VALUE_NODESC


def test_Key_has_value(simple_data):
    assert not yottadb.Key("nodata").has_value
    assert yottadb.Key("^test1").has_value
    assert not yottadb.Key("^test2").has_value
    assert yottadb.Key("^test2")["sub1"].has_value
    assert yottadb.Key("^test3").has_value
    assert yottadb.Key("^test3")["sub1"].has_value
    assert yottadb.Key("^test3")["sub1"]["sub2"].has_value


def test_Key_has_tree(simple_data):
    assert not yottadb.Key("nodata").has_tree
    assert not yottadb.Key("^test1").has_tree
    assert yottadb.Key("^test2").has_tree
    assert not yottadb.Key("^test2")["sub1"].has_tree
    assert yottadb.Key("^test3").has_tree
    assert yottadb.Key("^test3")["sub1"].has_tree
    assert not yottadb.Key("^test3")["sub1"]["sub2"].has_tree


# transaction decorator smoke tests
@yottadb.transaction
def simple_transaction(key1: yottadb.Key, value1: str, key2: yottadb.Key, value2: str) -> None:
    key1.value = value1
    key2.value = value2


def test_transaction_smoke_test1() -> None:
    test_base_key = yottadb.Key("^TransactionDecoratorTests")["smoke test 1"]
    test_base_key.delete_tree()
    key1 = test_base_key["key1"]
    value1 = b"v1"
    key2 = test_base_key["key2"]
    value2 = b"v2"
    assert key1.data == yottadb.YDB_DATA_UNDEF
    assert key2.data == yottadb.YDB_DATA_UNDEF

    simple_transaction(key1, value1, key2, value2)

    assert key1 == value1
    assert key2 == value2
    test_base_key.delete_tree()


@yottadb.transaction
def simple_rollback_transaction(key1: yottadb.Key, value1: str, key2: yottadb.Key, value2: str) -> None:
    key1.value = value1
    key2.value = value2
    raise yottadb.YDBTPRollback("rolling back transaction.")


def test_transaction_smoke_test2() -> None:
    test_base_key = yottadb.Key("^TransactionDecoratorTests")["smoke test 2"]
    test_base_key.delete_tree()
    key1 = test_base_key["key1"]
    value1 = "v1"
    key2 = test_base_key["key2"]
    value2 = "v2"
    assert key1.data == yottadb.YDB_DATA_UNDEF
    assert key2.data == yottadb.YDB_DATA_UNDEF

    try:
        simple_rollback_transaction(key1, value1, key2, value2)
    except yottadb.YDBTPRollback as e:
        print(str(e))
        assert str(e) == "rolling back transaction."

    assert key1.data == yottadb.YDB_DATA_UNDEF
    assert key2.data == yottadb.YDB_DATA_UNDEF
    test_base_key.delete_tree()


@yottadb.transaction
def simple_restart_transaction(key1: yottadb.Key, key2: yottadb.Key, value: str, restart_tracker: yottadb.Key) -> None:
    if restart_tracker.data == yottadb.YDB_DATA_UNDEF:
        key1.value = value
        restart_tracker.value = "1"
        raise yottadb.YDBTPRestart("restating transaction")
    else:
        key2.value = value


def test_transaction_smoke_test3() -> None:
    test_base_global_key = yottadb.Key("^TransactionDecoratorTests")["smoke test 3"]
    test_base_local_key = yottadb.Key("TransactionDecoratorTests")["smoke test 3"]
    test_base_global_key.delete_tree()
    test_base_local_key.delete_tree()

    key1 = test_base_global_key["key1"]
    key2 = test_base_global_key["key2"]
    value = b"val"
    restart_tracker = test_base_local_key["restart tracker"]

    assert key1.data == yottadb.YDB_DATA_UNDEF
    assert key2.data == yottadb.YDB_DATA_UNDEF
    assert restart_tracker.data == yottadb.YDB_DATA_UNDEF

    simple_restart_transaction(key1, key2, value, restart_tracker)

    assert key1.value is None
    assert key2 == value
    assert restart_tracker == b"1"

    test_base_global_key.delete_tree()
    test_base_local_key.delete_tree()


def no_action() -> None:
    pass


def set_key(key: yottadb.Key, value: str) -> None:
    key.value = value


def conditional_set_key(key1: yottadb.Key, key2: yottadb.Key, value: str, traker_key: yottadb.Key) -> None:
    if traker_key.data != yottadb.DATA_NO_DATA:
        key1.value = value
    else:
        key2.value = value


def raise_standard_python_exception() -> None:
    1 / 0


class TransactionData(NamedTuple):
    action: Callable = no_action
    action_arguments: Tuple = ()
    restart_key: yottadb.Key = None
    return_value: int = yottadb._yottadb.YDB_OK


@yottadb.transaction
def process_transaction(nested_transaction_data: Tuple[TransactionData]) -> int:
    current_data = nested_transaction_data[0]
    current_data.action(*current_data.action_arguments)
    sub_data = nested_transaction_data[1:]
    if len(sub_data) > 0:
        process_transaction(sub_data)

    if current_data.return_value == yottadb._yottadb.YDB_TP_RESTART:
        if current_data.restart_key.data == yottadb.DATA_NO_DATA:
            current_data.restart_key.value = "restarted"
            return yottadb._yottadb.YDB_TP_RESTART
        else:
            return yottadb._yottadb.YDB_OK

    return current_data.return_value


def test_transaction_return_YDB_OK():
    key = yottadb.Key("^transactiontests")["test_transaction_return_YDB_OK"]
    value = b"return YDB_OK"
    transaction_data = TransactionData(action=set_key, action_arguments=(key, value), return_value=yottadb._yottadb.YDB_OK)

    key.delete_tree()
    assert key.value is None
    assert key.data == 0

    process_transaction((transaction_data,))
    assert key == value
    key.delete_tree()


def test_nested_transaction_return_YDB_OK():
    key1 = yottadb.Key("^transactiontests")["test_transaction_return_YDB_OK"]["outer"]
    value1 = b"return YDB_OK"
    outer_transaction = TransactionData(action=set_key, action_arguments=(key1, value1), return_value=yottadb._yottadb.YDB_OK)
    key2 = yottadb.Key("^transactiontests")["test_transaction_return_YDB_OK"]["inner"]
    value2 = b"neseted return YDB_OK"
    inner_transaction = TransactionData(action=set_key, action_arguments=(key2, value2), return_value=yottadb._yottadb.YDB_OK)

    process_transaction((outer_transaction, inner_transaction))

    assert key1 == value1
    assert key2 == value2
    key1.delete_tree()
    key2.delete_tree()


YDB_MAX_TP_DEPTH = 126


@pytest.mark.parametrize("depth", range(1, YDB_MAX_TP_DEPTH + 1))
def test_transaction_return_YDB_OK_to_depth(depth):
    def key_at_level(level: int) -> yottadb.Key:
        sub1 = f"test_transaction_return_YDB_OK_to_depth{depth}"
        sub2 = f"level{level}"
        return yottadb.Key("^tptests")[sub1][sub2]

    def value_at_level(level: int) -> bytes:
        return bytes(f"level{level} returns YDB_OK", encoding="utf-8")

    transaction_data = []
    for level in range(1, depth + 1):
        transaction_data.append(TransactionData(action=set_key, action_arguments=(key_at_level(level), value_at_level(level))))

    process_transaction(transaction_data)

    for level in range(1, depth + 1):
        assert key_at_level(level) == value_at_level(level)

    sub1 = f"test_transaction_return_YDB_OK_to_depth{depth}"
    yottadb.Key("^tptests")[sub1].delete_tree()


# Confirm lock() works with Key objects without raising any errors.
# Due to timing issues, it's not possible to reliably test the behavior
# of lock() calls beyond simply failure detection. Accordingly,
# this test will pass so long as there are no exceptions.
def test_lock_Keys(simple_data):
    t1 = yottadb.Key("^test1")
    t2 = yottadb.Key("^test2")["sub1"]
    t3 = yottadb.Key("^test3")["sub1"]["sub2"]
    keys_to_lock = (t1, t2, t3)
    # Attempt to get locks for keys t1,t2 and t3
    yottadb.lock(keys=keys_to_lock, timeout_nsec=0)
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
    yottadb.lock()
    # Attempt to increment/decrement locks
    processes = []
    for key in keys_to_lock:
        process = multiprocessing.Process(target=lock_value, args=(key,))
        process.start()
        processes.append(process)
    for process in processes:
        process.join()
        assert process.exitcode == 0


# Lock a Key using the class lock() method instead of the module method
def test_lock_Key(simple_data):
    key = yottadb.Key("^test4")["sub1"]["sub2"]
    # Attempt to get the lock
    key.lock()
    # Attempt to increment/decrement the lock
    process = multiprocessing.Process(target=lock_value, args=(key,))
    process.start()
    process.join()
    assert process.exitcode == 1
    # Release all locks
    yottadb.lock()
    # Attempt to increment/decrement the lock
    process = multiprocessing.Process(target=lock_value, args=(key,))
    process.start()
    process.join()
    assert process.exitcode == 0


def test_lock_incr_Key():
    key = yottadb.Key("test1")["sub1"]["sub2"]
    t1 = datetime.datetime.now()
    key.lock_incr()
    t2 = datetime.datetime.now()
    time_elapse = t2.timestamp() - t1.timestamp()
    assert time_elapse < 0.01
    key.lock_decr()


def test_lock_incr_Key_timeout_error():
    # Timeout error, varname and subscript
    key = yottadb.Key("^test2")["sub1"]
    process = multiprocessing.Process(target=lock_value, args=(key,))
    process.start()
    time.sleep(0.5)
    with pytest.raises(yottadb.YDBTimeoutError):
        key.lock_incr()
    process.join()

    key2 = yottadb.Key("^test2")["sub1"]
    process = multiprocessing.Process(target=lock_value, args=(key2,))
    process.start()
    time.sleep(0.5)
    with pytest.raises(yottadb.YDBTimeoutError):
        key2.lock_incr()
    process.join()


def test_lock_incr_Key_no_timeout():
    # No timeout
    key = yottadb.Key("^test2")["sub1"]
    process = multiprocessing.Process(target=lock_value, args=(key,))
    process.start()
    time.sleep(0.5)
    t1 = datetime.datetime.now()
    yottadb.Key("test2").lock_incr()
    t2 = datetime.datetime.now()
    time_elapse = t2.timestamp() - t1.timestamp()
    assert time_elapse < 0.01
    key.lock_decr()
    time.sleep(0.5)
    process.join()


@pytest.mark.parametrize("input, output1, output2", str2zwr_tests)
def test_module_str2zwr(input, output1, output2):
    if os.environ["ydb_chset"] == "UTF-8":
        assert yottadb.str2zwr(input) == output2
    else:
        assert yottadb.str2zwr(input) == output1


@pytest.mark.parametrize("output1, output2, input", str2zwr_tests)
def test_module_zwr2str(input, output1, output2):
    if os.environ["ydb_chset"] == "UTF-8":
        assert yottadb.zwr2str(input) == output1
    else:
        assert yottadb.zwr2str(input) == output2


def test_import():
    assert yottadb.YDBACKError == yottadb.YDBACKError
    assert yottadb.YDBCCEBADFNError == yottadb.YDBCCEBADFNError
    assert yottadb.YDBCOLLTYPVERSIONError == yottadb.YDBCOLLTYPVERSIONError
    assert yottadb.YDBJNLPOOLRECOVERYError == yottadb.YDBJNLPOOLRECOVERYError
    assert yottadb.YDBTRIGUPBADLABELError == yottadb.YDBTRIGUPBADLABELError

    assert yottadb.YDB_DEL_TREE == 1
    assert yottadb.YDB_DEL_NODE == 2

    assert yottadb.YDB_SEVERITY_WARNING == 0
    assert yottadb.YDB_SEVERITY_SUCCESS == 1
    assert yottadb.YDB_SEVERITY_ERROR == 2
    assert yottadb.YDB_SEVERITY_INFORMATIONAL == 3
    assert yottadb.YDB_SEVERITY_FATAL == 4

    assert yottadb.YDB_DATA_UNDEF == 0
    assert yottadb.YDB_DATA_VALUE_NODESC == 1
    assert yottadb.YDB_DATA_NOVALUE_DESC == 10
    assert yottadb.YDB_DATA_VALUE_DESC == 11
    assert yottadb.YDB_DATA_ERROR == 0x7FFFFF00

    assert yottadb.YDB_MAIN_LANG_C == 0
    assert yottadb.YDB_MAIN_LANG_GO == 1

    assert yottadb.YDB_RELEASE == 133

    assert yottadb.YDB_MAX_IDENT == 31
    assert yottadb.YDB_MAX_NAMES == 35
    assert yottadb.YDB_MAX_STR == (1 * 1024 * 1024)
    assert yottadb.YDB_MAX_SUBS == 31
    assert yottadb.YDB_MAX_PARMS == 32
    assert yottadb.YDB_MAX_TIME_NSEC == 2147483647000000
    assert yottadb.YDB_MAX_YDBERR == (1 << 30)
    assert yottadb.YDB_MAX_ERRORMSG == 1024
    assert yottadb.YDB_MIN_YDBERR == (1 << 27)

    assert yottadb.YDB_OK == 0

    assert yottadb.YDB_INT_MAX == 0x7FFFFFFF
    assert yottadb.YDB_TP_RESTART == (yottadb.YDB_INT_MAX - 1)
    assert yottadb.YDB_TP_ROLLBACK == (yottadb.YDB_INT_MAX - 2)
    assert yottadb.YDB_NOTOK == (yottadb.YDB_INT_MAX - 3)
    assert yottadb.YDB_LOCK_TIMEOUT == (yottadb.YDB_INT_MAX - 4)
    assert yottadb.YDB_DEFER_HANDLER == (yottadb.YDB_INT_MAX - 5)

    assert yottadb.DEFAULT_DATA_SIZE == 32
    assert yottadb.DEFAULT_SUBSCR_CNT == 2
    assert yottadb.DEFAULT_SUBSCR_SIZE == 16

    assert yottadb.YDB_NOTTP == 0

    # Selection of constants from libydberrors.h
    assert yottadb.YDB_ERR_INVSTRLEN == -150375522
    assert yottadb.YDB_ERR_VERSION == -150374082
    assert yottadb.YDB_ERR_FILENOTFND == -150374338

    # Selection of constants from libydberrors2.h
    assert yottadb.YDB_ERR_FATALERROR2 == -151027828
    assert yottadb.YDB_ERR_TIME2LONG == -151027834
    assert yottadb.YDB_ERR_VARNAME2LONG == -151027842
