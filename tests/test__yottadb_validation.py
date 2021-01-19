#################################################################
#                                                               #
# Copyright (c) 2020-2021 Peter Goss All rights reserved.       #
#                                                               #
# Copyright (c) 2020-2021 YottaDB LLC and/or its subsidiaries.  #
# All rights reserved.                                          #
#                                                               #
#   This source code contains the intellectual property         #
#   of its copyright holder(s), and is made available           #
#   under a license.  If you do not know the terms of           #
#   the license, please stop and do not read further.           #
#                                                               #
#################################################################
"""
    This file is for tests of the validation of input from Python. Most tests are named by the function
    being called and the parameter that is having its data validation tested.

    Some background: originally the plan was to have all validation be done by the underlying YottaDB C API,
    however a bug arose in the transition from using 'int' to 'Py_size_t' that meant that length as well as type
    must be validated (see documentation for `test_unsigned_int_length_bytes_overflow()` below for additional
    detail). Since we were needing to test the length anyway the decision was to make that length equal to
    YottaDB's limitations. This also has the benefit of making these types of input errors raise the normal
    'TypeError' and 'ValueError' exceptions as is expected in Python.

    Note: Many functions have "varname" and "subsarray" parameters which have the same rules for valid input.
    Each of these functions are passed to "varname_invalid" and "subsarray_invalid" for testing.
"""
import pytest  # type: ignore # ignore due to pytest not having type annotations
import _yottadb
import psutil


def varname_invalid(function):
    """
    This function is used to test the function passed does correct validation of a variable name.
    It tests that the function will:
        1) Raise a TypeError when the varname is not of type bytes
        2) That a varname may be as long as _yottadb.YDB_MAX_IDENT without raising ValueError
        3) That if a varname is longer than _yottadb.YDB_MAX_IDENT it will raise a ValueError

    :param function: Any function that takes "varname" as a parameter.
    """
    # Case 1: varname must be bytes type, if not raise TypeError
    with pytest.raises(TypeError):
        function(varname="b")

    # Case 2: almost too long so should not raise ValueError
    try:
        function(varname=b"b" * (_yottadb.YDB_MAX_IDENT))
    except _yottadb.YDBError:  # testing c-extentions validation not YottaDB's
        pass

    # Case 3:  varname must not be longer than _yottadb.YDB_MAX_IDENT. if not, raise ValueError
    with pytest.raises(ValueError):
        function(varname=b"b" * (_yottadb.YDB_MAX_IDENT + 1))


def subsarray_invalid(function):
    """
    This function is used to test the function passed does correct validation of a list of subscripts.
    A subsarray must be a sequence (such as list or tuple but not bytes) of bytes objects.
    It tests that the function will:
        1) Raise a TypeError if the subsarray parameter is a bytes object.
        2) Not raise a ValueError when it has a sequence that is equal to _yottadb.YDB_MAX_SUBS in length
        3) Raises a ValueError when the sequence's length exceeds _yottab.YDB_MAX_SUBS
        4) Raises a TypeError when a value in the sequence is not of type bytes
        5) Not raise a ValueError when a bytes object is of length _yottadb.YDB_MAX_STR
        6) Raises a ValueError when a bytes object exceeds _yottadb.YDB_MAX_STR

    :param function: Any function that takes "varname"  and "subsarray" as a parameters.
    """
    # Case 1: must raise TypeError if subsarray is a bytes object
    with pytest.raises(TypeError):
        function(varname=b"test", subsarray=b"this is the wrong kind of sequence")

    # Case 2: almost too many so will not raise ValueError
    try:
        function(varname=b"test", subsarray=(b"b",) * (_yottadb.YDB_MAX_SUBS))
    except _yottadb.YDBError:  # testing c-extentions validation not YottaDB's
        pass

    # Case 3: too many items in subsarray parameter so raise ValueError
    with pytest.raises(ValueError):
        function(varname=b"test", subsarray=(b"b",) * (_yottadb.YDB_MAX_SUBS + 1))

    # Case 4: items in subsarray must be bytes so raise TypeError if not
    with pytest.raises(TypeError):
        function(varname=b"test", subsarray=("not a bytes object",))

    # Case 5: almost too long so do not raise ValueError
    try:
        function(b"test", (b"b" * (_yottadb.YDB_MAX_STR),))
    except _yottadb.YDBError:  # testing c-extentions validation not YottaDB's
        pass

    # Case 6: item in subsarray sequence is too long so raise ValueError
    with pytest.raises(ValueError):
        function(b"test", (b"b" * (_yottadb.YDB_MAX_STR + 1),))


# data()
def test_data_varname():
    varname_invalid(_yottadb.data)


def test_data_subsarray():
    subsarray_invalid(_yottadb.data)


# delete()
def test_delete_varname():
    varname_invalid(_yottadb.delete)


def test_delete_subsarray():
    subsarray_invalid(_yottadb.delete)


# delete_excel()
def test_delete_excel_varnames():
    """
    This function tests the validation of the delete_excel function's varnames parameter.
    It tests that the delete_excel function:
        1) Raises a TypeError if the varnames parameter is not a proper Sequence (list or tuple)
        2) Raises a TypeError if the contents of the varname list or tuple is not a bytes object
        3) Accepts up to _yottadb.YDB_MAX_NAMES without raising an exception
        4) Raise a ValueError if varnames is longer than _yottadb.YDB_MAX_NAMES
        5) Accept item in varnames up to _yottadb.YDB_MAX_IDENT without raising exception
        6) Raises a ValueError if an item in the varnames list or tuple is longer than _yottadb.YDB_MAX_IDENT
    """
    # Case 1: Raises a TypeError if varnames is not a list or tuple
    with pytest.raises(TypeError):
        _yottadb.delete_excel(varnames="not a sequence")

    # Case 2: Raises a TypeError if the contents of the varname list or tuple is not a bytes object
    with pytest.raises(TypeError):
        _yottadb.delete_excel(varnames=("not a sequence of bytes",))

    # Case 3: Accepts up to _yottadb.YDB_MAX_NAMES without raising an exception
    _yottadb.delete_excel(varnames=[b"test" + bytes(str(x), encoding="utf-8") for x in range(0, _yottadb.YDB_MAX_NAMES)])

    # Case 4: Raise a ValueError if varnames is longer than _yottadb.YDB_MAX_NAMES
    with pytest.raises(ValueError):
        _yottadb.delete_excel(varnames=[b"test" + bytes(str(x), encoding="utf-8") for x in range(0, _yottadb.YDB_MAX_NAMES + 1)])

    # Case 5: Accept item in varnames up to _yottadb.YDB_MAX_IDENT without raising exception
    _yottadb.delete_excel(varnames=[b"b" * (_yottadb.YDB_MAX_IDENT)])

    # Case 6: Raises a ValueError if an item in the varnames list or tuple is longer than _yottadb.YDB_MAX_IDENT
    with pytest.raises(ValueError):
        _yottadb.delete_excel(varnames=[b"b" * (_yottadb.YDB_MAX_IDENT + 1)])


# get()
def test_get_varname():
    varname_invalid(_yottadb.get)


def test_get_subsarray():
    subsarray_invalid(_yottadb.get)


# incr()
def test_incr_varname():
    varname_invalid(_yottadb.incr)


def test_incr_subsarray():
    subsarray_invalid(_yottadb.incr)


def test_incr_increment():
    """
    This function tests the validation of the incr function's increment parameter.
    It tests that the incr function:
        1) Raises a TypeError if the value that is passed to it is not a bytes object
        2) Accepts a value up to _yottadb.YDB_MAX_STR in length without raising an exception
        3) Raises a ValueError if the value is longer than _yottadb.YDB_MAX_STR
    """
    key = {"varname": b"test", "subsarray": (b"b",)}
    # Case 1: Raises a TypeError if the value that is passed to it is not a bytes object
    with pytest.raises(TypeError):
        _yottadb.incr(**key, increment="not bytes")

    # Case 2: Accepts a value up to _yottadb.YDB_MAX_STR in length without raising an exception
    try:
        _yottadb.incr(**key, increment=b"1" * (_yottadb.YDB_MAX_STR))
    except _yottadb.YDBError:  # testing c-extentions validation not YottaDB's
        pass

    # Case 3: Raises a ValueError if the value is longer than _yottadb.YDB_MAX_STR
    with pytest.raises(ValueError):
        _yottadb.incr(**key, increment=b"1" * (_yottadb.YDB_MAX_STR + 1))


# lock()
"""
This function tests the lock function`s key parameter.
It tests that the lock function:
    1)  Raises a Type Error if the value is a not a list or tuple.
    2)  Accepts a list of keys as long as _yottadb.YDB_LOCK_MAX_KEYS without raising a exception
    3)  Raises a ValueError if the list passed to it is longer than _yottadb.YDB_LOCK_MAX_KEYS
    4)  Raises a TypeError if the first element of a key is not a bytes object
    5)  Raises a ValueError if a key doesn't have any element
    6)  Raise a ValueError if a key has more than 2 elements
    7)  Raises a TypeError if the first element of a key (representing a varname) is not a bytes object
    8)  The first element of a key (varname) may be up to _yottadb.YDB_MAX_IDENT in length without raising an exception
    9)  Raises a TypeError if the second element of a key is not a list or tuple
    10) Accepts a subsarray list of bytes up to _yottadb.YDB_MAX_SUBS without raising an exception
    11) Raises a ValueError if a subsarray is longer than _yottadb.YDB_MAX_SUBS
    12) Raises a TypeError if an element of a subsarray is not a bytes object
    13) Accepts an item in a subsarray of length _yottadb.YDB_MAX_STR without raising an exception
    14) Raises a Value Error if a subsarray has an element that is longer than _yottadb.YDB_MAX_STR
    """


def test_lock_keys_case1():
    # Case 1: Raises a Type Error if the value is a not a list or tuple.
    with pytest.raises(TypeError):
        _yottadb.lock("not list or tuple")


def test_lock_keys_case2():
    # Case 2: Accepts a list of keys as long as _yottadb.YDB_LOCK_MAX_KEYS without raising a exception
    keys = [[b"test" + bytes(str(x), encoding="utf-8")] for x in range(0, _yottadb.YDB_LOCK_MAX_KEYS)]
    _yottadb.lock(keys)


def test_lock_keys_case3():
    # Case 3: Raises a ValueError if the list passed to it is longer than _yottadb.YDB_LOCK_MAX_KEYS
    with pytest.raises(ValueError):
        keys = [[b"test" + bytes(str(x), encoding="utf-8")] for x in range(0, _yottadb.YDB_LOCK_MAX_KEYS + 1)]
        _yottadb.lock(keys)

    # Case 4: Raises a type Error if the first element of a key is not a bytes object


def test_lock_keys_case4():
    with pytest.raises(TypeError):
        _yottadb.lock(("not list or tuple",))

    # Case 5: Raises a ValueError if a key doesn't have any element


def test_lock_keys_case5():
    with pytest.raises(ValueError):
        _yottadb.lock(([],))

    # Case 6: Raise a ValueError if a key has more than 2 elements


def test_lock_keys_case6():
    with pytest.raises(ValueError):
        _yottadb.lock(([b"varname", [b"subscript"], b"extra"],))  # too many

    # Case 7: Raises a TypeError if the first element of a key (representing a varname) is not a bytes object


def test_lock_keys_case7():
    with pytest.raises(TypeError):
        _yottadb.lock((("test",),))

    # Case 8: The first element of a key (varname) may be up to _yottadb.YDB_MAX_IDENT in length without raising an exception


def test_lock_keys_case8():
    _yottadb.lock(((b"a" * (_yottadb.YDB_MAX_IDENT),),))
    with pytest.raises(ValueError):
        _yottadb.lock(((b"a" * (_yottadb.YDB_MAX_IDENT + 1),),))

    # Case 9: Raises a TypeError if the second element of a key is not a list or tuple


def test_lock_keys_case9():
    with pytest.raises(TypeError):
        _yottadb.lock(((b"test", "not list or tuple"),))

    # Case 10: Accepts a subsarray list of bytes up to _yottadb.YDB_MAX_SUBS without raising an exception


def test_lock_keys_case10():
    subsarray = [b"test" + bytes(str(x), encoding="utf-8") for x in range(0, _yottadb.YDB_MAX_SUBS)]
    _yottadb.lock(
        (
            (
                b"test",
                subsarray,
            ),
        )
    )

    # Case 11: Raises a ValueError if a subsarray is longer than _yottadb.YDB_MAX_SUBS


def test_lock_keys_case11():
    with pytest.raises(ValueError):
        subsarray = [b"test" + bytes(str(x), encoding="utf-8") for x in range(0, _yottadb.YDB_MAX_SUBS + 1)]
        _yottadb.lock(
            (
                (
                    b"test",
                    subsarray,
                ),
            )
        )

    # Case 12: Raises a TypeError if an element of a subsarray is not a bytes object


def test_lock_keys_case12():
    with pytest.raises(TypeError):
        _yottadb.lock(((b"test", ["not bytes"]),))

    # Case 13: Accepts an item in a subsarray of length _yottadb.YDB_MAX_STR without raising an exception


def test_lock_keys_case13():
    try:
        # _yottadb.lock(((b"test", [b"a" * (256)]),))
        _yottadb.lock(((b"test", [b"a" * (_yottadb.YDB_MAX_STR)]),))
    except _yottadb.YDBError:  # testing c-extentions validation not YottaDB's
        pass

    # Case 14: Raises a Value Error if a subsarray has an element that is longer than _yottadb.YDB_MAX_STR


def test_lock_keys_case14():
    with pytest.raises(ValueError):
        _yottadb.lock(((b"test", [b"a" * (_yottadb.YDB_MAX_STR + 1)]),))


# lock_decr()
def test_decr_varname():
    varname_invalid(_yottadb.lock_decr)


def test_decr_subsarray():
    subsarray_invalid(_yottadb.lock_decr)


# lock_incr()
def test_lock_incr_varname():
    varname_invalid(_yottadb.lock_incr)


def test_lock_incr_subsarray():
    subsarray_invalid(_yottadb.lock_incr)


# node_next()
def test_node_next_varname():
    varname_invalid(_yottadb.node_next)


def test_node_next_subsarray():
    subsarray_invalid(_yottadb.node_next)


# node_previous()
def test_node_previous_varname():
    varname_invalid(_yottadb.node_previous)


def test_node_previous_subsarray():
    subsarray_invalid(_yottadb.node_previous)


# set()
def test_set_varname():
    varname_invalid(_yottadb.set)


def test_set_subsarray():
    subsarray_invalid(_yottadb.set)


def test_set_value():
    """
    This function tests the validation of the set function's value parameter.
    It tests that the set function:
        1) Raises a TypeError if the value that is passed to it is not a bytes object
        2) Accepts a value up to _yottadb.YDB_MAX_STR in length without raising an exception
        3) Raises a ValueError if the value is longer than _yottadb.YDB_MAX_STR
    """
    key = {"varname": b"test", "subsarray": (b"b",)}
    # Case 1: Raises a TypeError if the value that is passed to it is not a bytes object
    with pytest.raises(TypeError):
        _yottadb.set(**key, value="not bytes")

    # Case 2: Accepts a value up to _yottadb.YDB_MAX_STR in length without raising an exception
    _yottadb.set(**key, value=b"b" * (_yottadb.YDB_MAX_STR))

    # Case 3: Raises a ValueError if the value is longer than _yottadb.YDB_MAX_STR
    with pytest.raises(ValueError):
        _yottadb.set(**key, value=b"b" * (_yottadb.YDB_MAX_STR + 1))


# str2zwr()
def test_str2zwr_input():
    """
    This function tests the validation of the str2zwr function's input parameter.
    It tests that the str2zwr function:
        1) Raises a TypeError if input is not of type bytes
        2) Accepts a value up to _yottadb.YDB_MAX_STR without raising an exception
        3) Raises a ValueError if input is longer than _yottadb.YDB_MAX_STR
    """
    # Case 1: Raises a TypeError if input is not of type bytes
    with pytest.raises(TypeError):
        _yottadb.str2zwr("not bytes")

    # Case 2: Accepts a value up to _yottadb.YDB_MAX_STR without raising an exception
    try:
        _yottadb.str2zwr(b"b" * _yottadb.YDB_MAX_STR)
    except _yottadb.YDBError:  # testing c-extentions validation not YottaDB's
        pass

    # Case 3: Raises a ValueError if input is longer than _yottadb.YDB_MAX_STR
    with pytest.raises(ValueError):
        _yottadb.str2zwr(b"b" * (_yottadb.YDB_MAX_STR + 1))


# subscript_next()
def test_subscript_next_varname():
    varname_invalid(_yottadb.subscript_next)


def test_subscript_next_subsarray():
    subsarray_invalid(_yottadb.subscript_next)


# subscript_previous()
def test_subscript_previous_varname():
    varname_invalid(_yottadb.subscript_previous)


def test_subscript_previous_subsarray():
    subsarray_invalid(_yottadb.subscript_previous)


# tp()
def simple_transaction(tp_token: int) -> None:
    """
    A simple callback for testing the tp function that does nothing and returns _yottadb.YDB_OK
    """
    return _yottadb.YDB_OK


def callback_that_returns_wrong_type(tp_token=None):
    """
    A simple callback for testing the tp function that returns the wrong type.
    """
    return "not an int"


def test_tp_callback_not_a_callable():
    """
    Test that tp() raises a TypeError when the callback parameter is not callable.
    """
    with pytest.raises(TypeError):
        _yottadb.tp(callback="not a callable")


def test_tp_callback_return_wrong_type():
    """
    Tests that tp() raises TypeError when a callback returns the wrong type.
    """
    with pytest.raises(TypeError):
        _yottadb.tp(callback_that_returns_wrong_type)


def test_tp_args():
    """
    Tests that tp() raises TypeError if the args parameter is not a list or tuple
    """
    with pytest.raises(TypeError):
        _yottadb.tp(callback=simple_transaction, args="not a sequence of arguments")


def test_tp_kwargs():
    """
    Tests that tp() raises a TypeError if the kwargs parameter is not a dictionary
    """
    with pytest.raises(TypeError):
        _yottadb.tp(callback=simple_transaction, kwargs="not a dictionary of keyword arguments")


def test_tp_varnames():
    """
    This function tests the validation of the tp function's varnames parameter.
    It tests that the tp function:
        1) Raises a TypeError if the varnames parameter is not a proper Sequence (list or tuple)
        2) Raises a TypeError if the contents of the varname list or tuple is not a bytes object
        3) Accepts up to _yottadb.YDB_MAX_NAMES without raising an exception
        4) Raise a ValueError if varnames is longer than _yottadb.YDB_MAX_NAMES
        5) Accept item in varnames up to _yottadb.YDB_MAX_IDENT without raising exception
        6) Raises a ValueError if an item in the varnames list or tuple is longer than _yottadb.YDB_MAX_IDENT
    """
    # Case 1: Raises a TypeError if the varnames parameter is not a proper Sequence (list or tuple)
    with pytest.raises(TypeError):
        _yottadb.tp(callback=simple_transaction, varnames="not a sequence")

    # Case 2: Raises a TypeError if the contents of the varname list or tuple is not a bytes object
    with pytest.raises(TypeError):
        _yottadb.tp(callback=simple_transaction, varnames=("not a sequence of bytes",))

    # Case 3: Accepts up to _yottadb.YDB_MAX_NAMES without raising an exception
    varnames = [b"test" + bytes(str(x), encoding="utf-8") for x in range(0, _yottadb.YDB_MAX_NAMES)]
    _yottadb.tp(callback=simple_transaction, varnames=varnames)

    # case 4: Raise a ValueError if varnames is longer than _yottadb.YDB_MAX_NAMES
    varnames = [b"test" + bytes(str(x), encoding="utf-8") for x in range(0, _yottadb.YDB_MAX_NAMES + 1)]
    with pytest.raises(ValueError):
        _yottadb.tp(callback=simple_transaction, varnames=varnames)

    # Case 5: Accept item in varnames up to _yottadb.YDB_MAX_IDENT without raising exception
    _yottadb.tp(callback=simple_transaction, varnames=[b"b" * (_yottadb.YDB_MAX_IDENT)])

    # Case 6: Raises a ValueError if an item in the varnames list or tuple is longer than _yottadb.YDB_MAX_IDENT
    with pytest.raises(ValueError):
        _yottadb.tp(callback=simple_transaction, varnames=[b"b" * (_yottadb.YDB_MAX_IDENT + 1)])


# zwr2str()
def test_zwr2str_input():
    """
    This function tests the validation of the zwr2str function's input parameter.
    It tests that the zwr2str function:
        1) Raises a TypeError if input is not of type bytes
        2) Accepts a value up to _yottadb.YDB_MAX_STR without raising an exception
        3) Raises a ValueError if input is longer than _yottadb.YDB_MAX_STR
    """
    # Case 1: Raises a TypeError if input is not of type bytes
    with pytest.raises(TypeError):
        _yottadb.zwr2str("not bytes")

    # Case 2: Accepts a value up to _yottadb.YDB_MAX_STR without raising an exception
    try:
        _yottadb.zwr2str(b"b" * _yottadb.YDB_MAX_STR)
    except _yottadb.YDBError:  # testing c-extentions validation not YottaDB's
        pass

    # Case 3: Raises a ValueError if input is longer than _yottadb.YDB_MAX_STR
    with pytest.raises(ValueError):
        _yottadb.zwr2str(b"b" * (_yottadb.YDB_MAX_STR + 1))


# This test requires a lot of memory and will fail if there is not enough memory on the system that running
# the tests so this test will be skipped if the available memory is less than ((2 ** 32) + 1) bytes.
@pytest.mark.skipif(psutil.virtual_memory().available < ((2 ** 32) + 1), reason="not enough memory for this test.")
def test_unsigned_int_length_bytes_overflow():
    """
    Python bytes objects may have more bytes than can be represented by a 32-bit unsigned integer.
    Prior to validation a length that was 1 more than that length would act as if it was only a 1-byte
    long bytes object. This tests all scenarios where that could happen and that when that happens the
    function will raise a ValueError instead of continuing as if a single byte was passed to it.
    """
    BYTES_LONGER_THAN_UNSIGNED_INT_IN_LENGTH = b"1" * ((2 ** 32) + 1)  # works for python 3.8/Ubuntu 20.04
    varname_subsarray_functions = (
        _yottadb.data,
        _yottadb.delete,
        _yottadb.get,
        _yottadb.incr,
        _yottadb.lock_decr,
        _yottadb.lock_incr,
        _yottadb.node_next,
        _yottadb.node_previous,
        _yottadb.set,
        _yottadb.subscript_next,
        _yottadb.subscript_previous,
    )
    for function in varname_subsarray_functions:
        with pytest.raises(ValueError):
            function(varname=BYTES_LONGER_THAN_UNSIGNED_INT_IN_LENGTH)

        with pytest.raises(ValueError):
            function(b"test", (BYTES_LONGER_THAN_UNSIGNED_INT_IN_LENGTH,))

    key = {"varname": b"test", "subsarray": (b"b",)}
    with pytest.raises(ValueError):
        _yottadb.incr(**key, increment=BYTES_LONGER_THAN_UNSIGNED_INT_IN_LENGTH)

    with pytest.raises(ValueError):
        _yottadb.set(**key, value=BYTES_LONGER_THAN_UNSIGNED_INT_IN_LENGTH)

    with pytest.raises(ValueError):
        _yottadb.str2zwr(BYTES_LONGER_THAN_UNSIGNED_INT_IN_LENGTH)

    with pytest.raises(ValueError):
        _yottadb.zwr2str(BYTES_LONGER_THAN_UNSIGNED_INT_IN_LENGTH)
