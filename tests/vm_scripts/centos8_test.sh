# scp to freshly installed CentOS 8 VM
# run with bash

# update system
sudo yum check-update
sudo yum update -y

# install nessisary packages
sudo yum install -y wget git python3 python3-devel gcc libffi-devel python3-pip
python3 -m pip install --user pytest psutil


# install YottaDB
mkdir /tmp/tmp ; cd /tmp/tmp
wget https://gitlab.com/YottaDB/DB/YDB/raw/master/sr_unix/ydbinstall.sh
chmod +x ydbinstall.sh
sudo ./ydbinstall.sh --utf8 default --verbose
source $(pkg-config --variable=prefix yottadb)/ydb_env_set

# install YDBPython
cd ~
git clone https://gitlab.com/gossrock/YDBPython.git
cd YDBPython
python3 setup.py install --user

# test
python3 -m pytest tests/
