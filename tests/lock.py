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
import _yottadb
import time
import argparse


def key_tuple_to_str(self) -> str:
    ret_str = f'{str(self.varname, encoding="utf-8")}'
    if self.subsarray != None:
        ret_str += f' {str(b" ".join(self.subsarray), encoding="utf-8")}'
    return ret_str


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Lock a value in the database.")
    parser.add_argument("keys", metavar="N", type=str, nargs="+")
    parser.add_argument("-t", "--time", type=int, default=2, help="time in seconds that the script will wait to unlock the key.")
    parser.add_argument(
        "-T",
        "--locktimeout",
        type=int,
        default=1,
        help="lock call timeout (in seconds, converted to nanoseconds for call by this script).",
    )

    args = parser.parse_args()
    varname = bytes(args.keys[0], encoding="utf-8")
    subsarray = args.keys[1:]
    if len(subsarray) == 0:
        subsarray = None
    subsarray_bytes = None
    if subsarray != None:
        subsarray_bytes = []
        for sub in subsarray:
            subsarray_bytes.append(bytes(sub, encoding="utf-8"))

    has_lock = False
    try:
        _yottadb.lock_incr(varname, subsarray_bytes, timeout_nsec=(args.locktimeout * 1_000_000_000))
    except _yottadb.YDBTimeoutError as e:
        print("Lock Failed")
    except Exception as e:
        print(f"Lock Error: {repr(e)}")
    else:
        has_lock = True
        print("Lock Success")

    if has_lock:
        time.sleep(args.time)
        _yottadb.lock_decr(varname, subsarray_bytes)
        if args.locktimeout != 0 or args.time != 0:
            print("Lock Released")
