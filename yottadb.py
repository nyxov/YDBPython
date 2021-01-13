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
from typing import Optional, List, Union, Generator, Sequence, NamedTuple, cast, Tuple
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


class SearchSpace(enum.Enum):
    LOCAL = enum.auto()
    GLOBAL = enum.auto()
    BOTH = enum.auto()


class KeyTuple(NamedTuple):
    varname: bytes
    subsarray: Sequence[bytes] = ()

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

    def __getitem__(self, item: bytes) -> "Key":
        return Key(name=item, context=self)

    def data(self, varname: bytes, subsarray: Sequence[bytes] = ()) -> int:
        return _yottadb.data(varname, subsarray, self.tp_token)

    def delete_node(self, varname: bytes, subsarray: Sequence[bytes] = ()) -> None:
        _yottadb.delete(varname, subsarray, DEL_NODE, self.tp_token)

    def delete_tree(self, varname: bytes, subsarray: Sequence[bytes] = ()) -> None:
        _yottadb.delete(varname, subsarray, DEL_TREE, self.tp_token)

    def get(self, varname: bytes, subsarray: Sequence[bytes] = ()) -> Optional[bytes]:
        return _yottadb.get(varname, subsarray, self.tp_token)

    """
    def incr(self): ...
    def lock_decr(self): ...
    def lock_incr(self): ...
    def node_next(self): ...
    def node_previous(self): ...
    """

    def set(self, varname: bytes, subsarray: Sequence[bytes] = (), value: bytes = b"") -> None:
        _yottadb.set(varname, subsarray, value, self.tp_token)

    def subscript_next(self, varname: bytes, subsarray: Sequence[bytes] = ()) -> bytes:
        return _yottadb.subscript_next(varname, subsarray, self.tp_token)

    """
    def subscript_previous(self): ...
    def tp(self): ...

    def delete_excel(self): ...
    def lock(self): ...

    def str2zwr(self): ...
    def zwr2str(self): ...
    """

    def _varnames(self, first: bytes = b"^") -> Generator[bytes, None, None]:
        var_next: bytes = f"{first}%"
        if self.data(var_next) != 0:
            yield var_next

        while True:
            try:
                var_next = self.subscript_next(var_next)
                yield var_next
            except _yottadb.YDBNODEENDError as e:
                return

    @property
    def local_varnames(self) -> Generator[bytes, None, None]:
        for var in self._varnames(first=b""):
            yield var

    @property
    def local_varname_keys(self) -> Generator["Key", None, None]:
        for var in self.local_varnames:
            yield self[var]

    @property
    def global_varnames(self) -> Generator[bytes, None, None]:
        for var in self._varnames(first=b"^"):
            yield var

    @property
    def global_varname_keys(self) -> Generator["Key", None, None]:
        for var in self.global_varnames:
            yield self[var]

    @property
    def all_varnames(self) -> Generator[bytes, None, None]:
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
    name: bytes
    parent: Optional["Key"]

    def __init__(self, name: bytes, parent: Optional["Key"] = None, context: Context = None) -> None:
        if isinstance(context, Context):
            self.context = context
        elif context is None:
            self.context = Context()
        else:
            raise TypeError("'context' must be an instance of yottadb.Context")
        if isinstance(name, bytes):
            self.name = name
        else:
            raise TypeError("'name' must be an instance of bytes")

        if parent is not None and not isinstance(parent, Key):
            raise TypeError("'parent' must be of type Key")
        self.parent = parent

    def __str__(self) -> str:
        def encode_sub(sub):
            try:
                return str(sub, encoding="utf-8")
            except UnicodeDecodeError:
                return str(sub)

        varname = str(self.varname, encoding="ascii")
        subscripts = ",".join([encode_sub(sub) for sub in self.subsarray])
        if subscripts == "":
            return varname
        else:
            return f"{varname}({subscripts})"

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
    def varname(self) -> bytes:
        return self.varname_key.name

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
    def subsarray(self) -> List[bytes]:
        ret_list = []
        for key in self.subsarray_keys:
            ret_list.append(key.name)
        return ret_list

    @property
    def value(self) -> Optional[str]:
        try:
            return self.context.get(self.varname, self.subsarray)
        except (_yottadb.YDBLVUNDEFError, _yottadb.YDBGVUNDEFError):
            return None

    @value.setter
    def value(self, value: bytes) -> None:
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
        subscript_subsarray: List[bytes] = []
        if len(self.subsarray) > 0:
            subscript_subsarray = list(self.subsarray)
        subscript_subsarray.append(b"")
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
