#################################################################
#                                                               #
# Copyright (c) 2019 Peter Goss All rights reserved.            #
#                                                               #
# Copyright (c) 2019 YottaDB LLC and/or its subsidiaries.       #
# All rights reserved.                                          #
#                                                               #
#	This source code contains the intellectual property         #
#	of its copyright holder(s), and is made available           #
#	under a license.  If you do not know the terms of           #
#	the license, please stop and do not read further.           #
#                                                               #
#################################################################
export test_ydb_api="SIMPLE"
#export PYTEST_ADDOPTS="-s"
python setup.py test
echo "Testing was done using using SimpleAPI"