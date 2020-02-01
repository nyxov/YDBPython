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
from typing import Optional, List, Union, Generator, Sequence, NamedTuple, cast, Tuple
import enum
from builtins import property

import _yottadb

from _yottadb import YottaDBError
from _yottadb import YottaDBLockTimeout

from _yottadb import YDB_NOTTP as NOTTP
from _yottadb.errors import YDB_ERR_NODEEND as NODEEND

from _yottadb.errors import YDB_ERR_GVUNDEF as GLOBAL_VAR_UNDEF
from _yottadb.errors import YDB_ERR_LVUNDEF as LOCAL_VAR_UNDEF

from _yottadb import YDB_DEL_NODE as DEL_NODE
from _yottadb import YDB_DEL_TREE as DEL_TREE

from _yottadb import YDB_DATA_NO_DATA as DATA_NO_DATA
from _yottadb import YDB_DATA_HAS_VALUE_NO_TREE as DATA_HAS_VALUE_NO_TREE
from _yottadb import YDB_DATA_HAS_VALUE_HAS_TREE as DATA_HAS_VALUE_HAS_TREE
from _yottadb import YDB_DATA_NO_VALUE_HAS_TREE as DATA_NO_VALUE_HAS_TREE


Data = Union[bytes, str]

ASCII = 'ascii'
UTF8 = 'utf-8'
DEFAULT_ENCODING = UTF8
CONTEXT_ENCODING = 'CONTEXT'


class SearchSpace(enum.Enum):
    LOCAL = enum.auto()
    GLOBAL = enum.auto()
    BOTH = enum.auto()

class KeyTuple(NamedTuple):
    varname:Data
    subsarray:Sequence[Data] = ()

    def __str__(self) -> str:
        return_value = str(self.varname)
        if len(self.subsarray) > 0:
            return_value += f'("{self.subsarray[0]}"'
            for sub in self.subsarray[1:]:
                return_value += f',"{sub}"'
            return_value += ')'
        return return_value


def array_str_to_bytes(array:Sequence[Data], encoding:Optional[str]) -> Sequence[bytes]:
    if encoding is None:
        for item in array:
            if not isinstance(item, bytes):
                raise TypeError("'encoding' not set with sequence of non-bytes")
        return cast(Sequence[bytes], array)

    ret_array:List[bytes] = list()
    for item in array:
        if isinstance(item, bytes):
            ret_array.append(item)
        elif isinstance(item, str):
            ret_array.append(bytes(item, encoding=encoding))
        else:
            raise TypeError("'array' must be Sequence[union[data,str]]")

    return ret_array


class Context:
    tp_token: int
    subs_encoding: str
    val_encoding: str
    threaded: bool

    def __init__(self, tp_token=NOTTP, subs_encoding=DEFAULT_ENCODING, val_encoding=DEFAULT_ENCODING, threaded:bool = False):
        self.tp_token = tp_token
        self.subs_encoding = subs_encoding
        self.val_encoding = val_encoding
        self.threaded = threaded

    def __getitem__(self, item:Data) -> 'Key':
        return Key(name=item, context=self)

    def _setup(self, varname, subsarray, subs_encoding=CONTEXT_ENCODING, val_encoding=CONTEXT_ENCODING) -> Tuple[bytes, Sequence[bytes], str, str]:
        if subs_encoding == CONTEXT_ENCODING:
            subs_encoding = self.subs_encoding
        if val_encoding == CONTEXT_ENCODING:
            val_encoding = self.val_encoding
        if not isinstance(varname, bytes):
            varname = bytes(varname, encoding=ASCII)
        if subsarray is not None:
            subsarray = array_str_to_bytes(subsarray, subs_encoding)
        return (varname, subsarray, subs_encoding, val_encoding)

    def data(self, varname:Data, subsarray:Sequence[Data]=(), subs_encoding:Optional[str]=CONTEXT_ENCODING) -> int:
        varname, subsarray, subs_encoding, _ = self._setup(varname, subsarray, subs_encoding)
        return _yottadb.data(varname, subsarray, self.tp_token)

    def delete_node(self, varname:Data, subsarray:Sequence[Data]=(), subs_encoding:Optional[str]=CONTEXT_ENCODING) -> None:
        varname, subsarray, subs_encoding, _ = self._setup(varname, subsarray, subs_encoding)
        _yottadb.delete(varname, subsarray, DEL_NODE, self.tp_token)

    def delete_tree(self, varname:Data, subsarray:Sequence[Data]=(), subs_encoding:Optional[str]=CONTEXT_ENCODING) -> None:
        varname, subsarray, subs_encoding, _ = self._setup(varname, subsarray, subs_encoding)
        _yottadb.delete(varname, subsarray, DEL_TREE, self.tp_token)

    def get(self, varname:Data, subsarray:Sequence[Data]=(), subs_encoding:Optional[str]=CONTEXT_ENCODING, val_encoding:Optional[str]=CONTEXT_ENCODING) -> Optional[Data]:
        varname, subsarray, subs_encoding, val_encoding = self._setup(varname, subsarray, subs_encoding, val_encoding)
        val = _yottadb.get(varname, subsarray, self.tp_token)
        if val_encoding == None:
            return val
        else:
            return str(val, encoding=val_encoding)

    '''
    def incr(self): ...
    def lock_decr(self): ...
    def lock_incr(self): ...
    def node_next(self): ...
    def node_previous(self): ...
    '''
    def set(self, varname:Data, subsarray:Sequence[Data]=(), value:Data='', subs_encoding:Optional[str]=CONTEXT_ENCODING, val_encoding:Optional[str]=CONTEXT_ENCODING) -> None:
        varname, subsarray, subs_encoding, val_encoding = self._setup(varname, subsarray, subs_encoding, val_encoding)
        if val_encoding is not None and isinstance(value, str):
            value = bytes(value, encoding=val_encoding)
        _yottadb.set(varname, subsarray, value, self.tp_token)


    def subscript_next(self, varname:Data, subsarray:Sequence[Data]=(), subs_encoding:Optional[str]=CONTEXT_ENCODING) -> Data:
        varname, subsarray, subs_encoding, _ = self._setup(varname, subsarray, subs_encoding)
        sub = _yottadb.subscript_next(varname, subsarray, self.tp_token)
        if subs_encoding == None:
            return sub
        else:
            return str(sub, encoding=subs_encoding)

    '''
    def subscript_previous(self): ...
    def tp(self): ...

    def delete_excel(self): ...
    def lock(self): ...

    def str2zwr(self): ...
    def zwr2str(self): ...
    '''

    def _varnames(self, first:str = '^') -> Generator[Data, None, None]:
        var_next:Data = f'{first}%'
        if self.data(var_next) != 0:
            yield var_next

        while True:
            try:
                var_next = self.subscript_next(var_next)
                yield var_next
            except YottaDBError as e:
                if e.code == NODEEND:
                    return

    @property
    def local_varnames(self) -> Generator[Data, None, None]:
        for var in self._varnames(first=''):
            yield var

    @property
    def local_varname_keys(self) -> Generator['Key', None, None]:
        for var in self.local_varnames:
            yield self[var]

    @property
    def global_varnames(self) -> Generator[Data, None, None]:
        for var in self._varnames(first='^'):
            yield var

    @property
    def global_varname_keys(self) -> Generator['Key', None, None]:
        for var in self.global_varnames:
            yield self[var]

    @property
    def all_varnames(self) -> Generator[Data, None, None]:
        for var in self.global_varnames:
            yield var
        for var in self.local_varnames:
            yield var

    @property
    def all_varname_keys(self) -> Generator['Key', None, None]:
        for var in self.all_varnames:
            yield self[var]



class Key:
    context: Context
    name_bytes: bytes
    parent: Optional["Key"]
    name_encoding: str
    val_encoding: str

    def __init__(self, name:Data, parent:Optional['Key']=None, context:Context=None, name_encoding=CONTEXT_ENCODING, val_encoding=CONTEXT_ENCODING) -> None:
        if isinstance(context, Context):
            self.context = context
        elif context is None:
            self.context = Context()
        else:
            raise TypeError("'context' must be an instance of yottadb.Context")

        self.name_encoding = name_encoding
        if parent is None:  #if parent is None it is a 'varname' and must be have ASCII encoding.
            self.name_encoding = ASCII

        self.val_encoding = val_encoding

        if isinstance(name, str):
            if name_encoding == CONTEXT_ENCODING:
                self.name_bytes = bytes(name, encoding=self.context.subs_encoding)
            else:
               self.name_bytes = bytes(name, encoding=self.name_encoding)
        elif isinstance(name, bytes):
            self.name_bytes = name

        if parent is not None and not isinstance(parent, Key):
            raise TypeError("'parent' must be of type Key")
        self.parent = parent


    def __str__(self) -> str:
        return_value = self.varname
        if len(self.subsarray) > 0:
            return_value += f'("{self.subsarray[0]}"'
            for sub in self.subsarray[1:]:
                return_value += f',"{sub}"'
            return_value += ')'
        return return_value

    def __repr__(self) -> str:
        return f'{self.__class__.__name__}:{self}'

    def __getitem__(self, item):
        return Key(name=item, parent=self, context=self.context)

    def __eq__(self, other) -> bool:
        if not isinstance(other, Key):
            return False
        if self.varname_bytes == other.varname_bytes and self.subsarray_bytes == other.subsarray_bytes:
            return True
        else:
            return False

    @property
    def name(self) -> str:
        if self.name_encoding == CONTEXT_ENCODING:
            if self.context.subs_encoding == None:
                raise TypeError("Cannot encode keys name because context's 'subs_encoding' property is set to None.")
            else:
                return str(self.name_bytes, encoding=self.context.subs_encoding)
        if self.name_encoding == None:
            raise TypeError("Cannot encode keys name because key's 'name_encoding' property is set to None.")
        else:
            return str(self.name_bytes, encoding=self.name_encoding)

    @property
    def varname_key(self) -> 'Key':
        if self.parent is None:
            return self
        ansestor = self.parent
        while ansestor.parent is not None:
            ansestor = ansestor.parent
        return ansestor

    @property
    def varname(self) -> str:
        return self.varname_key.name

    @property
    def varname_bytes(self):
        return self.varname_key.name_bytes

    @property
    def subsarray_keys(self) -> List['Key']:
        if self.parent is None:
            return []
        subs_array = [self]
        ansestor = self.parent
        while ansestor.parent is not None:
            subs_array.insert(0, ansestor)
            ansestor = ansestor.parent
        return subs_array

    @property
    def subsarray(self) -> List[str]:
        ret_list = []
        for key in self.subsarray_keys:
            ret_list.append(key.name)
        return ret_list

    @property
    def subsarray_bytes(self) -> List[bytes]:
        ret_list = []
        for key in self.subsarray_keys:
            ret_list.append(key.name_bytes)
        return ret_list

    @property
    def value(self) -> Optional[str]:
        if self.val_encoding == None:
            raise TypeError("object's 'val_encoding' proprety set to None. Did you mean 'value_bytes'?")

        try:
            if self.val_encoding == CONTEXT_ENCODING:
                return cast(Optional[str], self.context.get(self.varname_bytes, self.subsarray_bytes))
            else:
                return cast(Optional[str], self.context.get(self.varname_bytes, self.subsarray_bytes, val_encoding=self.val_encoding))
        except YottaDBError as e:
            if e.code == GLOBAL_VAR_UNDEF or e.code == LOCAL_VAR_UNDEF:
                return None
            else:
                raise e

    @value.setter
    def value(self, value: Union[str, bytes]) -> None:
        bytes_value = b''
        if not isinstance(value, (str, bytes)):
            raise TypeError("'value' must be bytes or string")
        elif isinstance(value, str):
            if self.val_encoding == CONTEXT_ENCODING:
                bytes_value = bytes(value, encoding=self.context.val_encoding)
            else:
                bytes_value = bytes(value, encoding=self.val_encoding)
        else:
            bytes_value = value

        self.context.set(self.varname_bytes, self.subsarray_bytes, bytes_value, val_encoding=None)

    @property
    def value_bytes(self) -> Optional[bytes]:
        try:
            return cast(Optional[bytes], self.context.get(self.varname_bytes, self.subsarray_bytes, val_encoding=None))
        except YottaDBError as e:
            if e.code == GLOBAL_VAR_UNDEF or e.code == LOCAL_VAR_UNDEF:
                return None
            else:
                raise e

    def delete_node(self):
        self.context.delete_node(self.varname_bytes, self.subsarray_bytes)

    def delete_tree(self):
        self.context.delete_tree(self.varname_bytes, self.subsarray_bytes)

    @property
    def data(self):
        return self.context.data(self.varname_bytes, self.subsarray_bytes)

    @property
    def has_value(self):
        if self.data == DATA_HAS_VALUE_NO_TREE or self.data == DATA_HAS_VALUE_HAS_TREE:
            return True
        else:
            return False

    @property
    def has_tree(self):
        if self.data == DATA_NO_VALUE_HAS_TREE or self.data == DATA_HAS_VALUE_HAS_TREE:
            return True
        else:
            return False


    @property
    def subscripts(self) -> Generator:
        subscript_subsarray:List[Data] = []
        if len(self.subsarray) > 0:
            subscript_subsarray = list(self.subsarray_bytes)
        subscript_subsarray.append(b'')
        while True:
            try:
                sub_next = self.context.subscript_next(self.varname_bytes, subscript_subsarray)
                subscript_subsarray[-1] = sub_next
                yield sub_next
            except YottaDBError as e:
                if e.code == NODEEND:
                    return

    @property
    def subscript_keys(self) -> Generator:
        for sub in self.subscripts:
            yield self[sub]
'''
    @property
    def subscript_list(self) -> List[str]:
        return_value = []
        for sub in self.subscripts:
            return_value.append(sub)
        return return_value

    @property
    def subkeys(self) -> Generator:
        for sub in self.subscripts:
            yield self[sub]

    @property
    def subkey_list(self) -> List['Key']:
        return_value = []
        for key in self.subkeys:
            return_value.append(key)
        return return_value
'''


