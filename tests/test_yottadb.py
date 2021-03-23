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
from typing import NamedTuple, Callable, Tuple

import yottadb


def test_key_object(ydb, simple_data):
    # Key creation, varname only
    key = ydb["^test1"]
    assert key.value == b"test1value"
    assert key.name == "^test1"
    assert key.varname_key == key
    assert key.varname == "^test1"
    assert key.subsarray == []
    # Using bytes argument
    key = ydb[b"^test1"]
    assert key.value == b"test1value"
    assert key.name == b"^test1"
    assert key.varname_key == key
    assert key.varname == b"^test1"
    assert key.subsarray == []

    # Key creation, varname and subscript
    key = ydb["^test2"]["sub1"]
    assert key.value == b"test2value"
    assert key.name == "sub1"
    assert key.varname_key == ydb["^test2"]
    assert key.varname == "^test2"
    assert key.subsarray == ["sub1"]
    # Using bytes arguments
    key = ydb[b"^test2"][b"sub1"]
    assert key.value == b"test2value"
    assert key.name == b"sub1"
    assert key.varname_key == ydb[b"^test2"]
    assert key.varname == b"^test2"
    assert key.subsarray == [b"sub1"]

    # Key creation and value update, varname and subscript
    key = ydb["test3local"]["sub1"]
    key.value = "smoketest3local"
    assert key.value == b"smoketest3local"
    assert key.name == "sub1"
    assert key.varname_key == ydb["test3local"]
    assert key.varname == "test3local"
    assert key.subsarray == ["sub1"]
    # Using bytes arguments
    key = ydb[b"test3local"][b"sub1"]
    key.value = b"smoketest3local"
    assert key.value == b"smoketest3local"
    assert key.name == b"sub1"
    assert key.varname_key == ydb[b"test3local"]
    assert key.varname == b"test3local"
    assert key.subsarray == [b"sub1"]


def test_Key_construction_error(ydb):
    with pytest.raises(TypeError):
        yottadb.Key("^test1", "not a Key object")


def test_Key__str__(ydb):
    assert str(ydb["test"]) == "test"
    assert str(ydb["test"]["sub1"]) == "test(sub1)"
    assert str(ydb["test"]["sub1"]["sub2"]) == "test(sub1,sub2)"


def test_Key_get_value1(ydb, simple_data):
    assert ydb["^test1"].value == b"test1value"


def test_Key_get_value2(ydb, simple_data):
    assert ydb["^test2"]["sub1"].value == b"test2value"


def test_Key_get_value3(ydb, simple_data):
    assert ydb["^test3"].value == b"test3value1"
    assert ydb["^test3"]["sub1"].value == b"test3value2"
    assert ydb["^test3"]["sub1"]["sub2"].value == b"test3value3"


def test_Key_subsarray(ydb, simple_data):
    assert ydb["^test3"].subsarray == []
    assert ydb["^test3"]["sub1"].subsarray == ["sub1"]
    assert ydb["^test3"]["sub1"]["sub2"].subsarray == ["sub1", "sub2"]
    # Confirm no UnboundLocalError when a Key has no subscripts
    for subscript in ydb["^test3"].subscripts:
        pass


def test_Key_varname(ydb, simple_data):
    assert ydb["^test3"].varname == "^test3"
    assert ydb["^test3"]["sub1"].varname == "^test3"
    assert ydb["^test3"]["sub1"]["sub2"].varname == "^test3"


def test_Key_set_value1(ydb):
    testkey = ydb["test4"]
    testkey.value = "test4value"
    assert testkey.value == b"test4value"


def test_Key_set_value2(ydb):
    testkey = ydb["test5"]["sub1"]
    testkey.value = "test5value"
    assert testkey.value == b"test5value"
    assert ydb["test5"]["sub1"].value == b"test5value"


def test_Key_delete_node(ydb):
    testkey = ydb["test6"]
    subkey = testkey["sub1"]
    testkey.value = "test6value"
    subkey.value = "test6 subvalue"

    assert testkey.value == b"test6value"
    assert subkey.value == b"test6 subvalue"

    testkey.delete_node()

    assert testkey.value == None
    assert subkey.value == b"test6 subvalue"


def test_Key_delete_tree(ydb):
    testkey = ydb["test7"]
    subkey = testkey["sub1"]
    testkey.value = "test7value"
    subkey.value = "test7 subvalue"

    assert testkey.value == b"test7value"
    assert subkey.value == b"test7 subvalue"

    testkey.delete_tree()

    assert testkey.value == None
    assert subkey.value == None


def test_Key_data(ydb, simple_data):
    assert ydb["nodata"].data == yottadb.DATA_UNDEF
    assert ydb["^test1"].data == yottadb.DATA_VALUE_NODESC
    assert ydb["^test2"].data == yottadb.DATA_NOVALUE_DESC
    assert ydb["^test2"]["sub1"].data == yottadb.DATA_VALUE_NODESC
    assert ydb["^test3"].data == yottadb.DATA_VALUE_DESC
    assert ydb["^test3"]["sub1"].data == yottadb.DATA_VALUE_DESC
    assert ydb["^test3"]["sub1"]["sub2"].data == yottadb.DATA_VALUE_NODESC


def test_Key_has_value(ydb, simple_data):
    assert ydb["nodata"].has_value == False
    assert ydb["^test1"].has_value == True
    assert ydb["^test2"].has_value == False
    assert ydb["^test2"]["sub1"].has_value == True
    assert ydb["^test3"].has_value == True
    assert ydb["^test3"]["sub1"].has_value == True
    assert ydb["^test3"]["sub1"]["sub2"].has_value == True


def test_Key_has_tree(ydb, simple_data):
    assert ydb["nodata"].has_tree == False
    assert ydb["^test1"].has_tree == False
    assert ydb["^test2"].has_tree == True
    assert ydb["^test2"]["sub1"].has_tree == False
    assert ydb["^test3"].has_tree == True
    assert ydb["^test3"]["sub1"].has_tree == True
    assert ydb["^test3"]["sub1"]["sub2"].has_tree == False


# transaction decorator smoke tests
@yottadb.transaction
def simple_transaction(key1: yottadb.Key, value1: str, key2: yottadb.Key, value2: str, context: yottadb.Context) -> None:
    key1.value = value1
    key2.value = value2


def test_transaction_smoke_test1(ydb) -> None:
    test_base_key = ydb["^TransactionDecoratorTests"]["smoke test 1"]
    test_base_key.delete_tree()
    key1 = test_base_key["key1"]
    value1 = b"v1"
    key2 = test_base_key["key2"]
    value2 = b"v2"
    assert key1.data == yottadb.DATA_UNDEF
    assert key2.data == yottadb.DATA_UNDEF

    simple_transaction(key1, value1, key2, value2, ydb)

    assert key1.value == value1
    assert key2.value == value2
    test_base_key.delete_tree()


@yottadb.transaction
def simple_rollback_transaction(key1: yottadb.Key, value1: str, key2: yottadb.Key, value2: str, context: yottadb.Context) -> None:
    key1.value = value1
    key2.value = value2
    raise yottadb.YDBTPRollback("rolling back transaction.")


def test_transaction_smoke_test2(ydb) -> None:
    test_base_key = ydb["^TransactionDecoratorTests"]["smoke test 2"]
    test_base_key.delete_tree()
    key1 = test_base_key["key1"]
    value1 = "v1"
    key2 = test_base_key["key2"]
    value2 = "v2"
    assert key1.data == yottadb.DATA_UNDEF
    assert key2.data == yottadb.DATA_UNDEF

    try:
        simple_rollback_transaction(key1, value1, key2, value2, ydb)
    except yottadb.YDBTPRollback as e:
        print(str(e))
        assert str(e) == "rolling back transaction."

    assert key1.data == yottadb.DATA_UNDEF
    assert key2.data == yottadb.DATA_UNDEF
    test_base_key.delete_tree()


@yottadb.transaction
def simple_restart_transaction(
    key1: yottadb.Key, key2: yottadb.Key, value: str, restart_tracker: yottadb.Key, context: yottadb.Context
) -> None:
    if restart_tracker.data == yottadb.DATA_UNDEF:
        key1.value = value
        restart_tracker.value = "1"
        raise yottadb.YDBTPRestart("restating transaction")
    else:
        key2.value = value


def test_transaction_smoke_test3(ydb) -> None:
    test_base_global_key = ydb["^TransactionDecoratorTests"]["smoke test 3"]
    test_base_local_key = ydb["TransactionDecoratorTests"]["smoke test 3"]
    test_base_global_key.delete_tree()
    test_base_local_key.delete_tree()

    key1 = test_base_global_key["key1"]
    key2 = test_base_global_key["key2"]
    value = b"val"
    restart_tracker = test_base_local_key["restart tracker"]

    assert key1.data == yottadb.DATA_UNDEF
    assert key2.data == yottadb.DATA_UNDEF
    assert restart_tracker.data == yottadb.DATA_UNDEF

    simple_restart_transaction(key1, key2, value, restart_tracker, ydb)

    assert key1.value == None
    assert key2.value == value
    assert restart_tracker.value == b"1"

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
def process_transaction(nested_transaction_data: Tuple[TransactionData], context: yottadb.Context) -> int:
    current_data = nested_transaction_data[0]
    current_data.action(*current_data.action_arguments)
    sub_data = nested_transaction_data[1:]
    if len(sub_data) > 0:
        process_transaction(sub_data, context)

    if current_data.return_value == yottadb._yottadb.YDB_TP_RESTART:
        if current_data.restart_key.data == yottadb.DATA_NO_DATA:
            current_data.restart_key.value = "restarted"
            return yottadb._yottadb.YDB_TP_RESTART
        else:
            return yottadb._yottadb.YDB_OK

    return current_data.return_value


def test_transaction_return_YDB_OK(ydb):
    key = ydb["^transactiontests"]["test_transaction_return_YDB_OK"]
    value = b"return YDB_OK"
    transaction_data = TransactionData(action=set_key, action_arguments=(key, value), return_value=yottadb._yottadb.YDB_OK)

    key.delete_tree()
    assert key.value == None

    process_transaction((transaction_data,), context=ydb)
    assert key.value == value
    key.delete_tree()


def test_nested_transaction_return_YDB_OK(ydb):
    key1 = ydb["^transactiontests"]["test_transaction_return_YDB_OK"]["outer"]
    value1 = b"return YDB_OK"
    outer_transaction = TransactionData(action=set_key, action_arguments=(key1, value1), return_value=yottadb._yottadb.YDB_OK)
    key2 = ydb["^transactiontests"]["test_transaction_return_YDB_OK"]["inner"]
    value2 = b"neseted return YDB_OK"
    inner_transaction = TransactionData(action=set_key, action_arguments=(key2, value2), return_value=yottadb._yottadb.YDB_OK)

    process_transaction((outer_transaction, inner_transaction), context=ydb)

    assert key1.value == value1
    assert key2.value == value2
    key1.delete_tree()
    key2.delete_tree()


YDB_MAX_TP_DEPTH = 126


@pytest.mark.parametrize("depth", range(1, YDB_MAX_TP_DEPTH + 1))
def test_transaction_return_YDB_OK_to_depth(ydb, depth):
    def key_at_level(level: int) -> yottadb.Key:
        sub1 = f"test_transaction_return_YDB_OK_to_depth{depth}"
        sub2 = f"level{level}"
        return ydb["^tptests"][sub1][sub2]

    def value_at_level(level: int) -> bytes:
        return bytes(f"level{level} returns YDB_OK", encoding="utf-8")

    transaction_data = []
    for level in range(1, depth + 1):
        transaction_data.append(TransactionData(action=set_key, action_arguments=(key_at_level(level), value_at_level(level))))

    process_transaction(transaction_data, context=ydb)

    for level in range(1, depth + 1):
        assert key_at_level(level).value == value_at_level(level)

    sub1 = f"test_transaction_return_YDB_OK_to_depth{depth}"
    ydb["^tptests"][sub1].delete_tree()
