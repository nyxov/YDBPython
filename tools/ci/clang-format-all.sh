#!/bin/sh
#################################################################
#								#
# Copyright (c) 2019-2021 YottaDB LLC and/or its subsidiaries.	#
# All rights reserved.						#
#								#
#	This source code contains the intellectual property	#
#	of its copyright holder(s), and is made available	#
#	under a license.  If you do not know the terms of	#
#	the license, please stop and do not read further.	#
#								#
#################################################################
if [ -x "$(which "$1")" ]; then
	CLANG_FORMAT="$1"
else
	CLANG_FORMAT=clang-format
fi

find . -name '*.c' -o -name '*.h' | xargs "$CLANG_FORMAT" -i
find . -name '*.py' | grep -v venv | xargs black
if ! [ $(git diff --stat | wc -l) = 0 ]; then
  echo " -> Formatting differences found!"
  git diff
  exit 1
fi

