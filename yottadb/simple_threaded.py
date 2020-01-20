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
import _yottadb

from typing import Optional, Sequence, Tuple, Callable, Dict
Key = Tuple[bytes, Optional[Sequence[bytes]]]


def data(varname: bytes, subsarray:Optional[Sequence[bytes]] = None, tp_token: int = _yottadb.YDB_NOTTP) -> int:
    return _yottadb.data(True, varname, subsarray, tp_token)


def delete(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, delete_type: int= _yottadb.YDB_DEL_NODE,
           tp_token: int = _yottadb.YDB_NOTTP) -> None:
    return _yottadb.delete(True, varname, subsarray, delete_type, tp_token)


def delete_excel(varnames: Optional[Sequence[bytes]] = None, tp_token: int = _yottadb.YDB_NOTTP) -> None:
    return _yottadb.delete_excel(True, varnames, tp_token)


def get(varname:bytes, subsarray: Optional[Sequence[bytes]] = None, tp_token: int = _yottadb.YDB_NOTTP) -> bytes:
    return _yottadb.get(True, varname, subsarray, tp_token)


def incr(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, increment: bytes = b'1', tp_token: int = _yottadb.YDB_NOTTP) -> None:
    return _yottadb.incr(True, varname, subsarray, increment, tp_token)


def lock(keys:Sequence[Key], timeout_nsec: int = 0, tp_token: int = _yottadb.YDB_NOTTP) -> None:
    return _yottadb.lock(True, keys, timeout_nsec, tp_token)


def lock_decr(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, tp_token: int = _yottadb.YDB_NOTTP) -> None:
    return _yottadb.lock_decr(True, varname, subsarray, tp_token)


def lock_incr(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, timeout_nsec: int = 0,
              tp_token: int = _yottadb.YDB_NOTTP) -> None:
    return _yottadb.lock_incr(True, varname, subsarray, timeout_nsec, tp_token)


def node_next(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, tp_token: int = _yottadb.YDB_NOTTP) -> Tuple[bytes]:
    return _yottadb.node_next(True, varname, subsarray, tp_token)


def node_previous(varname: bytes, subsarray: Optional[Sequence[bytes]] = None, tp_token: int = _yottadb.YDB_NOTTP) -> Tuple[bytes]:
    return _yottadb.node_previous(True, varname, subsarray, tp_token)


def set(varname:bytes, subsarray:Optional[Sequence[bytes]]= None, value:bytes= b'', tp_token:int= _yottadb.YDB_NOTTP) -> None:
    return _yottadb.set(True, varname, subsarray, value, tp_token)


def str2zwr(input:bytes, tp_token:int = _yottadb.YDB_NOTTP) -> bytes:
    return _yottadb.str2zwr(True, input, tp_token)


def subscript_next(varname:bytes, subsarray:Optional[Sequence[bytes]]= None, tp_token:int= _yottadb.YDB_NOTTP) -> bytes:
    return _yottadb.subscript_next(True, varname, subsarray, tp_token)


def subscript_previous(varname:bytes, subsarray:Optional[Sequence[bytes]]= None, tp_token:int= _yottadb.YDB_NOTTP) -> bytes:
    return _yottadb.subscript_previous(True, varname, subsarray, tp_token)

def tp(callback:Callable, args:Tuple = (), kwargs:Dict = {}, transid:str= "BATCH", tp_token:int= _yottadb.YDB_NOTTP) -> int:
    return _yottadb.tp(True, callback, args, kwargs, transid, tp_token)


def zwr2str(input: bytes, tp_token: int = _yottadb.YDB_NOTTP) -> bytes:
    return _yottadb.zwr2str(True, input, tp_token)