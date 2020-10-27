#!/usr/bin/env bash
#################################################################
#                                                               #
# Copyright (c) 2020 Peter Goss All rights reserved.            #
#                                                               #
# Copyright (c) 2020 YottaDB LLC and/or its subsidiaries.       #
# All rights reserved.                                          #
#                                                               #
#   This source code contains the intellectual property         #
#   of its copyright holder(s), and is made available           #
#   under a license.  If you do not know the terms of           #
#   the license, please stop and do not read further.           #
#                                                               #
#################################################################

files_to_check=$(find -name '*.c' -o -name '*.h' -o  -name '*.py' -o -name '*.pyi')
echo "Checking code format ..."
for file in $files_to_check ; do
  exit_code=0
  if [[ "$file" == *".c" ]] || [[ "$file" == *".h" ]]; then
    if ! clang-format --dry-run --Werror -style=file "$file" &>/dev/null; then
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