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
from typing import Optional, List, Generator, Sequence, NamedTuple, AnyStr, Union, Callable
import enum
from builtins import property

import _yottadb

from _yottadb import YDBError
from _yottadb import YDBTimeoutError

from _yottadb import YDB_NOTTP as NOTTP

from _yottadb import YDB_DEL_NODE as DEL_NODE
from _yottadb import YDB_DEL_TREE as DEL_TREE

from _yottadb import YDB_DATA_UNDEF as DATA_UNDEF
from _yottadb import YDB_DATA_VALUE_NODESC as DATA_VALUE_NODESC
from _yottadb import YDB_DATA_NOVALUE_DESC as DATA_NOVALUE_DESC
from _yottadb import YDB_DATA_VALUE_DESC as DATA_VALUE_DESC

from _yottadb import YDB_OK, YDBTPRollback, YDBTPRestart
from _yottadb import YDBNODEENDError


class SearchSpace(enum.Enum):
    LOCAL = enum.auto()
    GLOBAL = enum.auto()
    BOTH = enum.auto()


class KeyTuple(NamedTuple):
    varname: AnyStr
    subsarray: Sequence[AnyStr] = ()

    def __str__(self) -> str:
        return_value = str(self.varname)
        if len(self.subsarray) > 0:
            return_value += f'("{self.subsarray[0]}"'
            for sub in self.subsarray[1:]:
                return_value += f',"{sub}"'
            return_value += ")"
        return return_value


def get(varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> Optional[bytes]:
    try:
        return _yottadb.get(varname, subsarray)
    except (_yottadb.YDBLVUNDEFError, _yottadb.YDBGVUNDEFError):
        return None


def set(varname: Union[AnyStr, KeyTuple], subsarray: Sequence[AnyStr] = (), value: AnyStr = "") -> None:
    # Derive call-in arguments from KeyTuple if passed,
    # otherwise use arguments as is
    if isinstance(varname, KeyTuple):
        subsarray = varname.subsarray
        varname = varname.varname
    _yottadb.set(varname, subsarray, value)


def data(varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> int:
    return _yottadb.data(varname, subsarray)


def delete_node(varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> None:
    _yottadb.delete(varname, subsarray, DEL_NODE)


def delete_tree(varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> None:
    _yottadb.delete(varname, subsarray, DEL_TREE)


def incr(varname: AnyStr, subsarray: Sequence[bytes] = (), increment: Union[int, str] = "1") -> int:
    if isinstance(increment, int):
        # Implicitly convert integers to string for passage to API
        increment = str(increment)
    return int(_yottadb.incr(varname, subsarray, increment))


def subscript_next(varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> AnyStr:
    return _yottadb.subscript_next(varname, subsarray)


def subscript_previous(varname: bytes, subsarray: Sequence[bytes] = ()) -> bytes:
    return _yottadb.subscript_previous(varname, subsarray)


def tp(
    callback: object,
    args: tuple = None,
    transid: str = "BATCH",
    varnames: Sequence[AnyStr] = None,
    **kwargs,
):
    return _yottadb.tp(callback, args, kwargs, transid, varnames)


class SubscriptsIter:
    def __init__(self, varname: bytes, subsarray: Sequence[bytes] = ()):
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
        except _yottadb.YDBNODEENDError:
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
            except _yottadb.YDBNODEENDError:
                break
        return result


def subscripts(varname: bytes, subsarray: Sequence[bytes] = ()) -> SubscriptsIter:
    return SubscriptsIter(varname, subsarray)


class Key:
    name: AnyStr
    parent: Optional["Key"]

    def __init__(self, name: AnyStr, parent: Optional["Key"] = None) -> None:
        if isinstance(name, str) or isinstance(name, bytes):
            self.name = name
        else:
            raise TypeError("'name' must be an instance of str or bytes")

        if parent is not None and not isinstance(parent, Key):
            raise TypeError("'parent' must be of type Key")
        self.parent = parent

    def __str__(self) -> str:
        subscripts = ",".join([sub for sub in self.subsarray])
        if subscripts == "":
            return self.varname
        else:
            return f"{self.varname}({subscripts})"

    def __repr__(self) -> str:
        return f"{self.__class__.__name__}:{self}"

    def __setitem__(self, item, value):
        Key(name=item, parent=self).value = value

    def __getitem__(self, item):
        return Key(name=item, parent=self)

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
            except YDBNODEENDError:
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
            except YDBNODEENDError:
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

    def incr(self, increment: Union[int, str] = "1") -> int:
        return int(incr(self.varname, self.subsarray, increment))

    def subscript_next(self, varname: AnyStr = None, subsarray: Sequence[AnyStr] = ()) -> AnyStr:
        if varname is None:
            varname = self.varname
        return subscript_next(varname, subsarray)

    def subscript_previous(self, varname: AnyStr = None, subsarray: Sequence[bytes] = ()) -> bytes:
        if varname is None:
            varname = self.varname
        return subscript_previous(varname, subsarray)

    def tp(
        self,
        callback: Callable,
        args: tuple = None,
        transid: str = "BATCH",
        varnames: Sequence[AnyStr] = None,
        **kwargs,
    ):
        return tp(callback, args, kwargs, transid, varnames)

    @property
    def varname_key(self) -> "Key":
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
    def value(self) -> Optional[AnyStr]:
        return get(self.varname, self.subsarray)

    @value.setter
    def value(self, value: AnyStr) -> None:
        # Value must be str or bytes
        set(self.varname, self.subsarray, value)

    @property
    def has_value(self):
        if self.data == DATA_VALUE_NODESC or self.data == DATA_VALUE_DESC:
            return True
        else:
            return False

    @property
    def has_tree(self):
        if self.data == DATA_NOVALUE_DESC or self.data == DATA_VALUE_DESC:
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
            except YDBNODEENDError:
                return

    @property
    def subscript_keys(self) -> Generator:
        for sub in self.subscripts:
            yield self[sub]

    """
    def lock_decr(self): ...
    def lock_incr(self): ...
    def node_next(self): ...
    def node_previous(self): ...

    def delete_excel(self): ...
    def lock(self): ...

    def str2zwr(self): ...
    def zwr2str(self): ...
    """


def transaction(function):
    def wrapper(*args, **kwargs):
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
