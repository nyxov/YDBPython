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
from typing import Optional, List, Union, Generator, Sequence, AnyStr, Any, Callable, NewType, Tuple
import enum
import copy
import struct
from builtins import property

import _yottadb
from _yottadb import *


Key = NewType("Key", object)

# Get the maximum number of arguments accepted by ci()/cip()
# based on whether the CPU architecture is 32-bit or 64-bit
arch_bits = 8 * struct.calcsize("P")
max_ci_args = 34 if 64 == arch_bits else 33


# Get the YottaDB numeric error code for the given
# YDBError by extracting it from the exception message.
def get_error_code(YDBError):
    error_code = int(YDBError.args[0].split(",")[0])  # Extract error code from between parentheses in error message
    if 0 < error_code:
        error_code *= -1  # Multiply by -1 for conformity with negative YDB error codes
    return error_code


# Note that the following setattr() call is done due to how the PyErr_SetObject()
# Python C API function works. That is, this function calls the constructor of a
# specified Exception type, in this case YDBError, and sets Python's
# internal error indicator causing the exception mechanism to fire and
# raise an exception visible at the Python level. Since both of these things
# are done by this single function, there is no opportunity for the calling
# C code to modify the created YDBError object instance and append the YDB
# error code.
#
# Moreover, it is not straightforward (and perhaps not possible) to define
# a custom constructor for custom exceptions defined in C code, e.g. YDBError.
# Such might allow for an error code integer to be set on the YDBError object
# when it is created by PyErr_SetObject() without the need for returning control
# to the calling C code to update the object.
#
# Attach error code lookup function to the YDBError class
# as a method for convenience.
setattr(YDBError, "code", get_error_code)


class SearchSpace(enum.Enum):
    LOCAL = enum.auto()
    GLOBAL = enum.auto()
    BOTH = enum.auto()


def get(varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> Optional[bytes]:
    if "$" == varname[0] and () != subsarray:
        raise ValueError(f"YottaDB Intrinsic Special Variable (ISV) cannot be subscripted: {varname}")
    try:
        return _yottadb.get(varname, subsarray)
    except YDBError as e:
        ecode = e.code()
        if _yottadb.YDB_ERR_LVUNDEF == ecode or _yottadb.YDB_ERR_GVUNDEF == ecode:
            return None
        else:
            raise e


def set(varname: AnyStr, subsarray: Sequence[AnyStr] = (), value: AnyStr = "") -> None:
    _yottadb.set(varname, subsarray, value)
    return None


def ci(routine: AnyStr, args: Sequence[Any] = (), has_retval: bool = False) -> Any:
    """
    Call an M routine specified in a YottaDB call-in table using the specified arguments, if any.
    If the routine has a return value, this must be indicated using the has_retval parameter by
    setting it to True if the routine has a return value, and False otherwise.

    Note that the call-in table used to derive the routine interface may be specified by either the
    ydb_ci environment variable, or via the switch_ci_table() function included in the YDBPython
    module.

    :param routine: The name of the M routine to be called.
    :param args: The arguments to pass to that routine.
    :param has_retval: Flag indicating whether the routine has a return value.
    :returns: The return value of the routine, or else None.
    """
    num_args = len(args)
    if num_args > max_ci_args:
        raise ValueError(
            f"ci(): number of arguments ({num_args}) exceeds max for a {arch_bits}-bit system architecture ({max_ci_args})"
        )
    return _yottadb.ci(routine, args, has_retval)


def message(errnum: int) -> str:
    """
    Lookup the error message string for the given error code.

    :param errnum: A valid YottaDB error code number.
    :returns: A string containing the error message for the given error code.
    """
    return _yottadb.message(errnum)


def cip(routine: AnyStr, args: Sequence[Any] = (), has_retval: bool = False) -> Any:
    """
    Call an M routine specified in a YottaDB call-in table using the specified arguments, if any,
    reusing the internal YottaDB call-in handle on subsequent calls to the same routine
    as a performance optimization.

    If the routine has a return value, this must be indicated using the has_retval parameter by
    setting it to True if the routine has a return value, and False otherwise.

    Note that the call-in table used to derive the routine interface may be specified by either the
    ydb_ci environment variable, or via the switch_ci_table() function included in the YDBPython
    module.

    :param routine: The name of the M routine to be called.
    :param args: The arguments to pass to that routine.
    :param has_retval: Flag indicating whether the routine has a return value.
    :returns: The return value of the routine, or else None.
    """
    num_args = len(args)
    if num_args > max_ci_args:
        raise ValueError(
            f"cip(): number of arguments ({num_args}) exceeds max for a {arch_bits}-bit system architecture ({max_ci_args})"
        )
    return _yottadb.cip(routine, args, has_retval)


def release() -> str:
    """
    Lookup the current YDBPython and YottaDB release numbers.

    :returns: A string containing the current YDBPython and YottaDB release numbers.
    """
    return "pywr " + "v0.10.0 " + _yottadb.release()


def open_ci_table(filename: AnyStr) -> int:
    """
    Open the YottaDB call-in table at the specified location. Once opened,
    the call-in table may be activated by passing the returned call-in table
    handle to switch_ci_table().

    :param filename: The name of the YottaDB call-in table to open.
    :returns: An integer representing the call-in table handle opened by YottaDB.
    """
    return _yottadb.open_ci_table(filename)


def switch_ci_table(handle: int) -> int:
    """
    Switch the active YottaDB call-in table to that represented by the passed handle,
    as obtained through a previous call to open_ci_table().

    :param handle: An integer value representing a call-in table handle.
    :returns: An integer value representing a the previously active call-in table handle
    """
    result = _yottadb.switch_ci_table(handle)
    if result == 0:
        return None

    return result


def data(varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> int:
    return _yottadb.data(varname, subsarray)


def delete_node(varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> None:
    _yottadb.delete(varname, subsarray, YDB_DEL_NODE)


def delete_tree(varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> None:
    _yottadb.delete(varname, subsarray, YDB_DEL_TREE)


def incr(varname: AnyStr, subsarray: Sequence[AnyStr] = (), increment: Union[int, float, str, bytes] = "1") -> bytes:
    if (
        not isinstance(increment, int)
        and not isinstance(increment, str)
        and not isinstance(increment, bytes)
        and not isinstance(increment, float)
    ):
        raise TypeError("unsupported operand type(s) for +=: must be 'int', 'float', 'str', or 'bytes'")
    # Implicitly convert integers and floats to string for passage to API
    if isinstance(increment, bytes):
        # bytes objects cast to str prepend `b'` and append `'`, yielding an invalid numeric
        # so cast to float first to guarantee a valid numeric value
        increment = float(increment)
    increment = str(increment)
    return _yottadb.incr(varname, subsarray, increment)


def subscript_next(varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> bytes:
    return _yottadb.subscript_next(varname, subsarray)


def subscript_previous(varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> bytes:
    return _yottadb.subscript_previous(varname, subsarray)


def node_next(varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> Tuple[bytes, ...]:
    return _yottadb.node_next(varname, subsarray)


def node_previous(varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> Tuple[bytes, ...]:
    return _yottadb.node_previous(varname, subsarray)


def lock_incr(varname: AnyStr, subsarray: Sequence[AnyStr] = (), timeout_nsec: int = 0) -> None:
    return _yottadb.lock_incr(varname, subsarray, timeout_nsec)


def lock_decr(varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> None:
    return _yottadb.lock_decr(varname, subsarray)


def str2zwr(string: AnyStr) -> bytes:
    return _yottadb.str2zwr(string)


def zwr2str(string: AnyStr) -> bytes:
    return _yottadb.zwr2str(string)


def tp(callback: object, args: tuple = None, transid: str = "", varnames: Sequence[AnyStr] = None, **kwargs) -> int:
    return _yottadb.tp(callback, args, kwargs, transid, varnames)


class SubscriptsIter:
    def __init__(self, varname: AnyStr, subsarray: Sequence[AnyStr] = ()):
        self.index = 0
        self.varname = varname
        self.subsarray = list(subsarray)

    def __iter__(self):
        return self

    def __next__(self):
        try:
            if len(self.subsarray) > 0:
                sub_next = subscript_next(self.varname, self.subsarray)
            else:
                sub_next = subscript_next(self.varname)
            self.subsarray[-1] = sub_next
        except YDBNodeEnd:
            raise StopIteration
        self.index += 1
        return (sub_next, self.index)

    def __reversed__(self):
        result = []
        index = 0
        while True:
            try:
                sub_next = subscript_previous(self.varname, self.subsarray)
                if len(self.subsarray) != 0:
                    self.subsarray[-1] = sub_next
                else:
                    return (sub_next, 1)
                index += 1
                result.append((sub_next, index))
            except YDBNodeEnd:
                break
        return result


def subscripts(varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> SubscriptsIter:
    return SubscriptsIter(varname, subsarray)


class Key:
    name: AnyStr
    parent: Key
    next_subsarray: List

    def __init__(self, name: AnyStr, parent: Key = None) -> None:
        if isinstance(name, str) or isinstance(name, bytes):
            self.name = name
        else:
            raise TypeError("'name' must be an instance of str or bytes")

        if parent is not None:
            if not isinstance(parent, Key):
                raise TypeError("'parent' must be of type Key")
            if "$" == parent.varname[0]:
                raise ValueError(f"YottaDB Intrinsic Special Variable (ISV) cannot be subscripted: {parent.varname}")
        self.parent = parent
        if _yottadb.YDB_MAX_SUBS < len(self.subsarray):
            raise ValueError(f"Cannot create Key with {len(self.subsarray)} subscripts (max: {_yottadb.YDB_MAX_SUBS})")

        # Initialize subsarray for use with Key.subscript_next()/Key.subscript_previous() methods
        if [] == self.subsarray:
            self.next_subsarray = [""]
        else:
            # Shallow copy the subscript array so that it is not mutated by Key.subscript_next()/Key.subscript_previous()
            self.next_subsarray = copy.copy(self.subsarray)
            self.next_subsarray.pop()
            self.next_subsarray.append("")

    def __repr__(self) -> str:
        result = f'{self.__class__.__name__}("{self.varname}")'
        for subscript in self.subsarray:
            result += f'["{subscript}"]'
        return result

    def __str__(self) -> str:
        # Convert to ZWRITE format to allow decoding of binary blobs into `str` objects
        subscripts = ",".join([str2zwr(sub).decode("ascii") for sub in self.subsarray])
        if subscripts == "":
            return self.varname
        else:
            return f"{self.varname}({subscripts})"

    def __setitem__(self, item, value):
        Key(name=item, parent=self).value = value

    def __getitem__(self, item):
        return Key(name=item, parent=self)

    def __iadd__(self, num: Union[int, float, str, bytes]) -> Key:
        self.incr(num)
        return self

    def __isub__(self, num: Union[int, float, str, bytes]) -> Key:
        if isinstance(num, float):
            self.incr(-float(num))
        else:
            self.incr(-int(num))
        return self

    def __eq__(self, other) -> bool:
        if isinstance(other, Key):
            return self.varname == other.varname and self.subsarray == other.subsarray
        else:
            return self.value == other

    def __iter__(self) -> Generator:
        if len(self.subsarray) > 0:
            subscript_subsarray = list(self.subsarray)
        else:
            subscript_subsarray: List[AnyStr] = []
        subscript_subsarray.append("")
        while True:
            try:
                sub_next = subscript_next(self.varname, subscript_subsarray)
                subscript_subsarray[-1] = sub_next
                yield Key(sub_next, self)
            except YDBNodeEnd:
                return

    def __reversed__(self) -> Generator:
        if len(self.subsarray) > 0:
            subscript_subsarray = list(self.subsarray)
        else:
            subscript_subsarray: List[AnyStr] = []
        subscript_subsarray.append("")
        while True:
            try:
                sub_next = subscript_previous(self.varname, subscript_subsarray)
                subscript_subsarray[-1] = sub_next
                yield Key(sub_next, self)
            except YDBNodeEnd:
                return

    def get(self) -> Optional[bytes]:
        return get(self.varname, self.subsarray)

    def set(self, value: AnyStr = "") -> None:
        return set(self.varname, self.subsarray, value)

    @property
    def data(self) -> int:
        return data(self.varname, self.subsarray)

    def delete_node(self) -> None:
        delete_node(self.varname, self.subsarray)

    def delete_tree(self) -> None:
        delete_tree(self.varname, self.subsarray)

    def incr(self, increment: Union[int, float, str, bytes] = "1") -> bytes:
        # incr() will enforce increment type
        return incr(self.varname, self.subsarray, increment)

    def subscript_next(self, reset: bool = False) -> AnyStr:
        if reset:
            self.next_subsarray.pop()
            self.next_subsarray.append("")

        next_sub = subscript_next(self.varname, self.next_subsarray)
        self.next_subsarray.pop()
        self.next_subsarray.append(next_sub)

        return next_sub

    def subscript_previous(self, reset: bool = False) -> bytes:
        if reset:
            self.next_subsarray.pop()
            self.next_subsarray.append("")

        prev_sub = subscript_previous(self.varname, self.next_subsarray)
        self.next_subsarray.pop()
        self.next_subsarray.append(prev_sub)

        return prev_sub

    def lock(self, timeout_nsec: int = 0) -> None:
        return lock((self,), timeout_nsec)

    def lock_incr(self, timeout_nsec: int = 0) -> None:
        return lock_incr(self.varname, self.subsarray, timeout_nsec)

    def lock_decr(self) -> None:
        return lock_decr(self.varname, self.subsarray)

    @property
    def varname_key(self) -> Key:
        if self.parent is None:
            return self
        ancestor = self.parent
        while ancestor.parent is not None:
            ancestor = ancestor.parent
        return ancestor

    @property
    def varname(self) -> AnyStr:
        return self.varname_key.name  # str or bytes

    @property
    def subsarray_keys(self) -> List["Key"]:
        if self.parent is None:
            return []
        subs_array = [self]
        ancestor = self.parent
        while ancestor.parent is not None:
            subs_array.insert(0, ancestor)
            ancestor = ancestor.parent
        return subs_array

    @property
    def subsarray(self) -> List[AnyStr]:
        ret_list = []
        for key in self.subsarray_keys:
            ret_list.append(key.name)
        return ret_list  # Returns List of str or bytes

    @property
    def value(self) -> Optional[bytes]:
        return get(self.varname, self.subsarray)

    @value.setter
    def value(self, value: AnyStr) -> None:
        # Value must be str or bytes
        set(self.varname, self.subsarray, value)

    @property
    def has_value(self):
        if self.data == YDB_DATA_VALUE_NODESC or self.data == YDB_DATA_VALUE_DESC:
            return True
        else:
            return False

    @property
    def has_tree(self):
        if self.data == YDB_DATA_NOVALUE_DESC or self.data == YDB_DATA_VALUE_DESC:
            return True
        else:
            return False

    @property
    def subscripts(self) -> Generator:
        if len(self.subsarray) > 0:
            subscript_subsarray = list(self.subsarray)
        else:
            subscript_subsarray: List[AnyStr] = []
        subscript_subsarray.append("")
        while True:
            try:
                sub_next = subscript_next(self.varname, subscript_subsarray)
                subscript_subsarray[-1] = sub_next
                yield sub_next
            except YDBNodeEnd:
                return

    """
    def delete_excel(self): ...
    """


# Defined after Key class to allow access to that class
def lock(keys: Sequence[tuple] = None, timeout_nsec: int = 0) -> None:
    if keys is not None:
        keys = [(key.varname, key.subsarray) if isinstance(key, Key) else key for key in keys]
    return _yottadb.lock(keys=keys, timeout_nsec=timeout_nsec)


def transaction(function) -> Callable[..., object]:
    def wrapper(*args, **kwargs) -> int:
        def wrapped_transaction(*args, **kwargs):
            ret_val = YDB_OK
            try:
                ret_val = function(*args, **kwargs)
                if ret_val is None:
                    ret_val = YDB_OK
            except YDBTPRestart:
                ret_val = _yottadb.YDB_TP_RESTART
            return ret_val

        return _yottadb.tp(wrapped_transaction, args=args, kwargs=kwargs)

    return wrapper
