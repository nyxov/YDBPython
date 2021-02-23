#!/usr/bin/env bash
#################################################################
#                                                               #
# Copyright (c) 2020-2021 Peter Goss All rights reserved.       #
#                                                               #
# Copyright (c) 2020-2021 YottaDB LLC and/or its subsidiaries.  #
# All rights reserved.                                          #
#                                                               #
#   This source code contains the intellectual property         #
#   of its copyright holder(s), and is made available           #
#   under a license.  If you do not know the terms of           #
#   the license, please stop and do not read further.           #
#                                                               #
#################################################################
exit_code=0

files_to_check=$(find . '(' -path ./.git -o -path ./.eggs ')' -type d -prune -false -o -name '*.c' -o '(' -name '*.h' -a ! -path ./_yottadbexceptions.h ')' -o  -name '*.py' -o -name '*.pyi')
echo "Checking code format ..."
for file in $files_to_check ; do
  if [[ "$file" == *".c" ]] || [[ "$file" == *".h" ]]; then
    if ! clang-format-10 --dry-run --Werror -style=file "$file" &>/dev/null; then
      echo "    $file needs formatting with \"clang-format\"."
      exit_code=1
    fi
  elif [[ "$file" == *".py" ]] || [[ "$file" == *".pyi" ]]; then
    if ! black -q --check "$file"; then
      echo "    $file needs formatting with \"black\"."

      exit_code=1
    fi
  fi
done
exit $exit_code

# `clang-tidy` is not available on CentOS 7, and YDB tests on 7 to ensure backwards-compatibility.
if ! [ -x "$(command -v yum)" ]; then
	echo "# Check for unexpected warning(s) from clang-tidy ..."
	tools/ci/clang-tidy-all.sh > clang_tidy_warnings.txt 2>/dev/null
	tools/ci/sort_warnings.sh clang_tidy_warnings.txt
		compare tools/ci/clang_tidy_warnings.ref sorted_warnings.txt
fi
