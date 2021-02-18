#################################################################
#                                                               #
# Copyright (c) 2021 YottaDB LLC and/or its subsidiaries.       #
# All rights reserved.                                          #
#                                                               #
#   This source code contains the intellectual property         #
#   of its copyright holder(s), and is made available           #
#   under a license.  If you do not know the terms of           #
#   the license, please stop and do not read further.           #
#                                                               #
#################################################################
import pytest  # type: ignore # ignore due to pytest not having type annotations
import os
import sys
from typing import NamedTuple, Callable, Tuple

import yottadb
from yottadb import YDBError, YDBNODEENDError


# Constants for loading varnames and other things with
MAX_VARNAME_LEN = 8
MAX_WORDS_SUBS = 1
MAX_INDEX_SUBS = 2
MAX_WORD_LEN = 128  # Size increased from the C version which uses 64 here
TP_TOKEN = yottadb.NOTTP


def test_wordfreq(ydb):
    # Initialize all array variable names we are planning to use. Randomly use locals vs globals to store word frequencies.
    # If using globals, delete any previous contents
    if os.getpid() % 2:
        # words_var = ydb[b"words"]
        # index_var = ydb[b"index"]
        words_var = b"words"
        index_var = b"index"
    else:
        # words_var = ydb[b"^words"]
        # words_var.delete_tree()
        # index_var = ydb[b"^index"]
        # index_var.delete_tree()
        words_var = b"^words"
        ydb.delete_tree(words_var)
        index_var = b"^index"
        ydb.delete_tree(index_var)

    # Read lines from input file and count the number of words in each
    with open("tests/wordfreq_input.txt", "r") as input_file:
        lines = input_file.readlines()
        for line in lines:
            line = line.lower()  # Lowercase the current line
            words = line.split()  # Breakup line into words on whitespace
            for word in words:
                ydb.incr(words_var, (word.encode(),))

    for subscript, discard in ydb.subscripts(words_var, (b"",)):
        count = ydb.get(words_var, (subscript,))
        ydb.set(index_var, (count, subscript), b"")

    for word, discard in reversed(ydb.subscripts(index_var, (b"",))):
        for count, discard in ydb.subscripts(index_var, (word, b"")):
            print("{}\t{}".format(word, count))
