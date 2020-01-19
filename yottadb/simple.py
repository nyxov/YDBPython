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
from typing import Optional, Sequence
import _yottadb

from _yottadb import delete_s as delete
from _yottadb import delete_excel_s as delete_excel
from _yottadb import get_s as get
from _yottadb import incr_s as incr
from _yottadb import lock_s as lock
from _yottadb import lock_decr_s as lock_decr
from _yottadb import lock_incr_s as lock_incr
from _yottadb import node_next_s as node_next
from _yottadb import node_previous_s as node_previous
from _yottadb import set_s as set
from _yottadb import str2zwr_s as str2zwr
from _yottadb import subscript_next_s as subscript_next
from _yottadb import subscript_previous_s as subscript_previous
from _yottadb import tp_s as tp
from _yottadb import zwr2str_s as zwr2str

def data(varname: bytes, subsarray:Optional[Sequence[bytes]] = None, tp_token: int = _yottadb.YDB_NOTTP) -> int:
    return _yottadb.data(False, varname, subsarray, tp_token)
