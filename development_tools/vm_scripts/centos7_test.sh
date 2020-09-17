# scp to freshly installed CentOS 7 VM
# after installation use `nmtui` to activate network connection.

# run this script with bash

# update system
sudo yum check-update
sudo yum update -y

# install YottaDB
sudo yum install -y wget
mkdir /tmp/tmp ; cd /tmp/tmp
wget https://gitlab.com/YottaDB/DB/YDB/raw/master/sr_unix/ydbinstall.sh
chmod +x ydbinstall.sh
sudo ./ydbinstall.sh --force-install --utf8 default --verbose
source $(pkg-config --variable=prefix yottadb)/ydb_env_set

# get YDBPython code
sudo yum install -y git
cd ~
git clone https://gitlab.com/gossrock/YDBPython.git

# install YDBPython
sudo yum install -y python3 gcc python3-devel libffi-devel
cd YDBPython
python3 setup.py install --user


# test YDBPython
python3 -m pip install --user pytest psutil
python3 -m pytest tests/
