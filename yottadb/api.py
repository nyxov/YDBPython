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
from typing import Optional, Sequence, Tuple, Callable, Dict
from types import ModuleType
from abc import ABC, abstractmethod


from yottadb import simple
from yottadb import simple_threaded

from yottadb import NOTTP
from _yottadb import YDB_DEL_NODE as DEL_NODE
from _yottadb import YDB_DEL_TREE as DEL_TREE

Key = Tuple[bytes, Optional[Sequence[bytes]]]

class API(ABC):
    @staticmethod
    @abstractmethod
    def data(varname: bytes, subsarray:Optional[Sequence[bytes]] = ..., tp_token: int = ...) -> int: ...

    @staticmethod
    @abstractmethod
    def delete(varname: bytes, subsarray:Optional[Sequence[bytes]] = ..., delete_type: int = ...,tp_token: int = ...) -> None: ...

    @staticmethod
    @abstractmethod
    def delete_excel(varnames: Optional[Sequence[bytes]] = ..., tp_token: int = ...) -> None: ...

    @staticmethod
    @abstractmethod
    def get(varname: bytes, subsarray:Optional[Sequence[bytes]] = ..., tp_token: int = ...) -> bytes: ...

    @staticmethod
    @abstractmethod
    def incr(varname: bytes, subsarray:Optional[Sequence[bytes]] = ..., increment: bytes = ...,
               tp_token: int = ...) -> None: ...

    @staticmethod
    @abstractmethod
    def lock(keys: Sequence[Key]=(), timeout_nsec: int = ..., tp_token: int = ...) -> None: ...

    @staticmethod
    @abstractmethod
    def lock_decr(varname: bytes, subsarray:Optional[Sequence[bytes]] = ..., tp_token: int = ...) -> None: ...

    @staticmethod
    @abstractmethod
    def lock_incr(varname: bytes, subsarray:Optional[Sequence[bytes]] = ..., timeout_nsec: int = ...,
                    tp_token: int = ...) -> None: ...

    @staticmethod
    @abstractmethod
    def node_next(varname: bytes, subsarray:Optional[Sequence[bytes]] = ..., tp_token: int = ...) -> Tuple[bytes]: ...

    @staticmethod
    @abstractmethod
    def node_previous(varname: bytes, subsarray:Optional[Sequence[bytes]] = ..., tp_token: int = ...) -> Tuple[bytes]: ...

    @staticmethod
    @abstractmethod
    def set(varname: bytes, subsarray:Optional[Sequence[bytes]] = ..., value: bytes = ..., tp_token: int = ...) -> None: ...

    @staticmethod
    @abstractmethod
    def str2zwr(input: bytes, tp_token: int = ...) -> bytes: ...

    @staticmethod
    @abstractmethod
    def subscript_next(varname: bytes, subsarray:Optional[Sequence[bytes]] = ..., tp_token: int = ...) -> bytes: ...

    @staticmethod
    @abstractmethod
    def subscript_previous(varname: bytes, subsarray:Optional[Sequence[bytes]] = ..., tp_token: int = ...) -> bytes: ...

    @staticmethod
    @abstractmethod
    def tp(callback: Callable, args: Tuple = ..., kwargs: Dict = ..., transid: str = ...,
             tp_token: int = ...) -> int: ...

    @staticmethod
    @abstractmethod
    def zwr2str(input: bytes, tp_token: int = ...) -> bytes: ...

class SimpleAPI(API):
    @staticmethod
    def data(varname: bytes, subsarray:Optional[Sequence[bytes]] = None, tp_token: int = NOTTP) -> int:
        return simple.data(varname, subsarray, tp_token)

    @staticmethod
    def delete(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, delete_type: int = DEL_NODE, tp_token: int = NOTTP) -> None:
        return simple.delete(varname, subsarray, delete_type, tp_token)

    @staticmethod
    def delete_excel(varnames: Optional[Sequence[bytes]] = None, tp_token: int = NOTTP) -> None:
        return simple.delete_excel(varnames, tp_token)

    @staticmethod
    def get(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, tp_token: int = NOTTP) -> bytes:
        return simple.get(varname, subsarray, tp_token)

    @staticmethod
    def incr(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, increment: bytes = b'1', tp_token: int = NOTTP) -> None:
        return simple.incr(varname, subsarray, increment, tp_token)

    @staticmethod
    def lock(keys: Sequence[Key]=(), timeout_nsec: int = 0, tp_token: int = NOTTP) -> None:
        return simple.lock(keys, timeout_nsec, tp_token)

    @staticmethod
    def lock_decr(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, tp_token: int = NOTTP) -> None:
        return simple.lock_decr(varname, subsarray, tp_token)

    @staticmethod
    def lock_incr(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, timeout_nsec: int = 0, tp_token: int = NOTTP) -> None:
        return simple.lock_incr(varname, subsarray, timeout_nsec, tp_token)

    @staticmethod
    def node_next(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, tp_token: int = NOTTP) -> Tuple[bytes]:
        return simple.node_next(varname, subsarray, tp_token)

    @staticmethod
    def node_previous(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, tp_token: int = NOTTP) -> Tuple[bytes]:
        return simple.node_previous(varname, subsarray, tp_token)

    @staticmethod
    def set(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, value: bytes = b'', tp_token: int = NOTTP) -> None:
        return simple.set(varname, subsarray, value, tp_token)

    @staticmethod
    def str2zwr(input: bytes, tp_token: int = NOTTP) -> bytes:
        return simple.str2zwr(input, tp_token)

    @staticmethod
    def subscript_next(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, tp_token: int = NOTTP) -> bytes:
        return simple.subscript_next(varname, subsarray, tp_token)

    @staticmethod
    def subscript_previous(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, tp_token: int = NOTTP) -> bytes:
        return simple.subscript_previous(varname, subsarray, tp_token)

    @staticmethod
    def tp(callback: Callable, args: Tuple = (), kwargs: Dict = {}, transid: str = "BATCH", tp_token: int = NOTTP) -> int:
        return simple.tp(callback, args, kwargs, transid, tp_token)

    @staticmethod
    def zwr2str(input: bytes, tp_token: int = NOTTP) -> bytes:
        return simple.zwr2str(input, tp_token)

class SimpleThreadedAPI(API):
    @staticmethod
    def data(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, tp_token: int = NOTTP) -> int:
        return simple_threaded.data(varname, subsarray, tp_token)

    @staticmethod
    def delete(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, delete_type: int = DEL_NODE,
               tp_token: int = NOTTP) -> None:
        return simple_threaded.delete(varname, subsarray, delete_type, tp_token)

    @staticmethod
    def delete_excel(varnames: Optional[Sequence[bytes]] = None, tp_token: int = NOTTP) -> None:
        return simple_threaded.delete_excel(varnames, tp_token)

    @staticmethod
    def get(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, tp_token: int = NOTTP) -> bytes:
        return simple_threaded.get(varname, subsarray, tp_token)

    @staticmethod
    def incr(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, increment: bytes = b'1',
             tp_token: int = NOTTP) -> None:
        return simple_threaded.incr(varname, subsarray, increment, tp_token)

    @staticmethod
    def lock(keys: Sequence[Key]=(), timeout_nsec: int = 0, tp_token: int = NOTTP) -> None:
        return simple_threaded.lock(keys, timeout_nsec, tp_token)

    @staticmethod
    def lock_decr(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, tp_token: int = NOTTP) -> None:
        return simple_threaded.lock_decr(varname, subsarray, tp_token)

    @staticmethod
    def lock_incr(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, timeout_nsec: int = 0,
                  tp_token: int = NOTTP) -> None:
        return simple_threaded.lock_incr(varname, subsarray, timeout_nsec, tp_token)

    @staticmethod
    def node_next(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, tp_token: int = NOTTP) -> Tuple[bytes]:
        return simple_threaded.node_next(varname, subsarray, tp_token)

    @staticmethod
    def node_previous(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, tp_token: int = NOTTP) -> Tuple[
        bytes]:
        return simple_threaded.node_previous(varname, subsarray, tp_token)

    @staticmethod
    def set(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, value: bytes = b'',
            tp_token: int = NOTTP) -> None:
        return simple_threaded.set(varname, subsarray, value, tp_token)

    @staticmethod
    def str2zwr(input: bytes, tp_token: int = NOTTP) -> bytes:
        return simple_threaded.str2zwr(input, tp_token)

    @staticmethod
    def subscript_next(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, tp_token: int = NOTTP) -> bytes:
        return simple_threaded.subscript_next(varname, subsarray, tp_token)

    @staticmethod
    def subscript_previous(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, tp_token: int = NOTTP) -> bytes:
        return simple_threaded.subscript_previous(varname, subsarray, tp_token)

    @staticmethod
    def tp(callback: Callable, args: Tuple = (), kwargs: Dict = {}, transid: str = "BATCH",
           tp_token: int = NOTTP) -> int:
        return simple_threaded.tp(callback, args, kwargs, transid, tp_token)

    @staticmethod
    def zwr2str(input: bytes, tp_token: int = NOTTP) -> bytes:
        return simple_threaded.zwr2str(input, tp_token)
