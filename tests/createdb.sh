#!/bin/bash
#################################################################
#								#
# Copyright (c) 2021-2022 YottaDB LLC and/or its subsidiaries.	#
# All rights reserved.						#
#								#
#	This source code contains the intellectual property	#
#	of its copyright holder(s), and is made available	#
#	under a license.  If you do not know the terms of	#
#	the license, please stop and do not read further.	#
#								#
#################################################################

# If YottaDB was compiled with clang and ASAN, LD_PRELOAD must be unset
# to prevent ASAN from aborting with the following error:
#   `ASan runtime does not come first in initial library list; you should either link runtime to your application or manually preload it with LD_PRELOAD.`
if [[ $(strings $ydb_dist/libyottadb.so | grep -c clang) -ne 0 ]]; then
    unset LD_PRELOAD
fi

$1/yottadb -run ^GDE <<FILE
change -r DEFAULT -key_size=1019 -record_size=1048576
change -segment DEFAULT -file_name=$2
change -r DEFAULT -NULL_SUBSCRIPTS=true
exit
FILE

$1/mupip create
