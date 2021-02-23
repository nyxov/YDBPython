#!/bin/bash
#################################################################
#                                                               #
# Copyright (c) 2020-2021 YottaDB LLC and/or its subsidiaries.	#
# All rights reserved.                                          #
#                                                               #
#	This source code contains the intellectual property           #
#	of its copyright holder(s), and is made available             #
#	under a license.  If you do not know the terms of             #
#	the license, please stop and do not read further.             #
#                                                               #
#################################################################

# Run Python tests using pytest with valgrind to check for memory issues

# First (and only) argument specifies a regex pattern for test names to run. For example,
# to run the threeenp1 test:
#   ./tools/pytest-valgrind.sh test_threeenp1
PYTHONMALLOC=malloc valgrind --show-leak-kinds=definite --leak-check=full --track-origins=yes --log-file=valgrind-output python -m pytest -k $1 -s -vv --valgrind --valgrind-log=valgrind-output
