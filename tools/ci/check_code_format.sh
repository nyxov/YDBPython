#!/usr/bin/env bash
#################################################################
#                                                               #
# Copyright (c) 2020-2021 Peter Goss All rights reserved.       #
#                                                               #
# Copyright (c) 2020-2022 YottaDB LLC and/or its subsidiaries.  #
# All rights reserved.                                          #
#                                                               #
#   This source code contains the intellectual property         #
#   of its copyright holder(s), and is made available           #
#   under a license.  If you do not know the terms of           #
#   the license, please stop and do not read further.           #
#                                                               #
#################################################################

set -e # Fail script if any command fails
set -u # Enable detection of uninitialized variables
set -o pipefail	# this way $? is set to zero only if ALL commands in a pipeline succeed. Else only last command determines $?

exit_code=0
files_to_check=$(find . '(' -path ./.git -o -path ./.eggs -o -path ./.venv -o -path ./tools/ci ')' -type d -prune -false -o -name '*.c' -o '(' -name '*.h' -a ! -path ./_yottadbexceptions.h ')' -o  -name '*.py' -o -name '*.pyi')

echo "Checking code format ..."
for file in $files_to_check ; do
  if [[ "$file" == *".c" ]] || [[ "$file" == *".h" ]] && [[ "$file" == "_yottadbconstants.h" ]]; then
    if ! clang-format-10 --dry-run --Werror -style=file "$file" &>/dev/null; then
      echo "    $file needs formatting with \"clang-format\"."
      exit_code=1
    fi
  elif [[ "$file" == *".py" ]] || [[ "$file" == *".pyi" ]]; then
    if ! black -l 132 -q --diff --check "$file"; then
      echo "    $file needs formatting with \"black\"."

      exit_code=1
    fi
  fi
done

# `clang-tidy` is not available on CentOS 7, and YDB tests on 7 to ensure backwards-compatibility.
if ! [ -x "$(command -v yum)" ]; then
	echo "# Run clang-tidy on all .c/.h files ..."
	tools/ci/clang-tidy-all.sh
	echo "# Sort warnings ..."
	tools/ci/sort_warnings.sh clang_tidy_warnings.txt
	echo "# Check for unexpected warning(s) from clang-tidy ..."
	diff tools/ci/clang_tidy_warnings.ref sorted_warnings.txt
fi
exit $exit_code
