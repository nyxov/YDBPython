# scp to freshly installed Ubuntu Server 20.04 VM with ssh installed
# run with bash

# update system
sudo apt update
sudo apt upgrade -y

# install YottaDB
sudo apt install -y binutils pkg-config libtinfo5
mkdir /tmp/tmp ; cd /tmp/tmp
wget https://gitlab.com/YottaDB/DB/YDB/raw/master/sr_unix/ydbinstall.sh
chmod +x ydbinstall.sh
sudo ./ydbinstall.sh --utf8 default --verbose
source $(pkg-config --variable=prefix yottadb)/ydb_env_set


# get YDBPython code
cd ~
git clone https://gitlab.com/gossrock/YDBPython.git

# install YDBPython
sudo apt install -y gcc python3-dev libffi-dev
cd YDBPython
python3 setup.py install --user

# test YDBPython
sudo apt install -y python3-pip
python3 -m pip install --user pytest psutil
python3 -m pytest tests/
