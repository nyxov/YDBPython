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
from typing import Optional, List, Union, Generator, Sequence, NamedTuple, cast, Tuple, AnyStr
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


class Context:
    tp_token: int

    def __init__(self, tp_token=NOTTP):
        self.tp_token = tp_token

    def __getitem__(self, item: AnyStr) -> "Key":
        return Key(name=item, context=self)

    def data(self, varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> int:
        return _yottadb.data(varname, subsarray, self.tp_token)

    def delete_node(self, varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> None:
        _yottadb.delete(varname, subsarray, DEL_NODE, self.tp_token)

    def delete_tree(self, varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> None:
        _yottadb.delete(varname, subsarray, DEL_TREE, self.tp_token)

    def get(self, varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> Optional[AnyStr]:
        return _yottadb.get(varname, subsarray, self.tp_token)

    def incr(self, varname: AnyStr, subsarray: Sequence[bytes] = ()) -> int:
        return _yottadb.incr(varname, subsarray, increment=b"1", tp_token=self.tp_token)

    """
    def lock_decr(self): ...
    def lock_incr(self): ...
    def node_next(self): ...
    def node_previous(self): ...
    """

    def set(self, varname: AnyStr, subsarray: Sequence[AnyStr] = (), value: AnyStr = "") -> None:
        _yottadb.set(varname, subsarray, value, self.tp_token)

    def subscript_next(self, varname: AnyStr, subsarray: Sequence[AnyStr] = ()) -> AnyStr:
        return _yottadb.subscript_next(varname, subsarray, self.tp_token)

    def subscript_previous(self, varname: bytes, subsarray: Sequence[bytes] = ()) -> bytes:
        return _yottadb.subscript_previous(varname, subsarray, self.tp_token)

    """
    def tp(self): ...

    def delete_excel(self): ...
    def lock(self): ...

    def str2zwr(self): ...
    def zwr2str(self): ...
    """

    def _varnames(self, first: AnyStr = "^") -> Generator[AnyStr, None, None]:
        var_next: AnyStr = f"{first}%"
        if self.data(var_next) != 0:
            yield var_next

        while True:
            try:
                var_next = self.subscript_next(var_next)
                yield var_next
            except _yottadb.YDBNODEENDError as e:
                return

    class SubscriptsIter:
        def __init__(self, context, varname: bytes, subsarray: Sequence[bytes] = ()):
            self.index = 0
            self.context = context
            self.varname = varname
            self.subsarray = list(subsarray)

        def __iter__(self):
            return self

        def __next__(self):
            try:
                if len(self.subsarray) > 0:
                    sub_next = self.context.subscript_next(self.varname, self.subsarray)
                else:
                    sub_next = self.context.subscript_next(self.varname)
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
                    sub_next = self.context.subscript_previous(self.varname, self.subsarray)
                    if len(self.subsarray) != 0:
                        self.subsarray[-1] = sub_next
                    else:
                        return (sub_next, 1)
                    index += 1
                    result.append((sub_next, index))
                except _yottadb.YDBNODEENDError:
                    break
            return result

    def subscripts(self, varname: bytes, subsarray: Sequence[bytes] = ()) -> SubscriptsIter:
        return self.SubscriptsIter(self, varname, subsarray)

    @property
    def local_varnames(self) -> Generator[AnyStr, None, None]:
        for var in self._varnames(first=""):
            yield var

    @property
    def local_varname_keys(self) -> Generator["Key", None, None]:
        for var in self.local_varnames:
            yield self[var]

    @property
    def global_varnames(self) -> Generator[AnyStr, None, None]:
        for var in self._varnames(first="^"):
            yield var

    @property
    def global_varname_keys(self) -> Generator["Key", None, None]:
        for var in self.global_varnames:
            yield self[var]

    @property
    def all_varnames(self) -> Generator[AnyStr, None, None]:
        for var in self.global_varnames:
            yield var
        for var in self.local_varnames:
            yield var

    @property
    def all_varname_keys(self) -> Generator["Key", None, None]:
        for var in self.all_varnames:
            yield self[var]


class Key:
    context: Context
    name: AnyStr
    parent: Optional["Key"]

    def __init__(self, name: AnyStr, parent: Optional["Key"] = None, context: Context = None) -> None:
        if isinstance(context, Context):
            self.context = context
        elif context is None:
            self.context = Context()
        else:
            raise TypeError("'context' must be an instance of yottadb.Context")
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

    def __getitem__(self, item):
        return Key(name=item, parent=self, context=self.context)

    def __eq__(self, other) -> bool:
        if not isinstance(other, Key):
            return False
        if self.varname == other.varname and self.subsarray == other.subsarray:
            return True
        else:
            return False

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
        try:
            return self.context.get(self.varname, self.subsarray)
        except (_yottadb.YDBLVUNDEFError, _yottadb.YDBGVUNDEFError):
            return None

    @value.setter
    def value(self, value: AnyStr) -> None:
        # Value must be str or bytes
        self.context.set(self.varname, self.subsarray, value)

    def delete_node(self):
        self.context.delete_node(self.varname, self.subsarray)

    def delete_tree(self):
        self.context.delete_tree(self.varname, self.subsarray)

    @property
    def data(self):
        return self.context.data(self.varname, self.subsarray)

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
                sub_next = self.context.subscript_next(self.varname, subscript_subsarray)
                subscript_subsarray[-1] = sub_next
                yield sub_next
            except _yottadb.YDBNODEENDError:
                return

    @property
    def subscript_keys(self) -> Generator:
        for sub in self.subscripts:
            yield self[sub]


def transaction(function):
    def get_context(*args, **kwargs):
        if "context" in kwargs.keys():
            return kwargs["context"]
        else:
            return args[-1]

    def wrapper(*args, **kwargs):
        context = get_context(*args, **kwargs)

        def wrapped_transaction(*args, **kwargs):
            tp_token = kwargs["tp_token"]
            del kwargs["tp_token"]
            old_token = None
            context = get_context(*args, **kwargs)
            old_token = context.tp_token
            context.tp_token = tp_token
            ret_val = _yottadb.YDB_OK
            try:
                ret_val = function(*args, **kwargs)
                if ret_val == None:
                    ret_val = _yottadb.YDB_OK
            except _yottadb.YDBTPRestart:
                ret_val = _yottadb.YDB_TP_RESTART
            finally:
                context.tp_token = old_token
            return ret_val

        return _yottadb.tp(wrapped_transaction, args=args, kwargs=kwargs, tp_token=context.tp_token)

    return wrapper
