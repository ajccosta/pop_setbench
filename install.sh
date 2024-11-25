#!/bin/sh
LIST_OF_APPS="build-essential dos2unix g++ libnuma-dev make numactl parallel python3 python3-pip time zip micro bc"

echo "############################################"
echo "installing required ubuntu packages... $LIST_OF_APPS"
echo "############################################"

sudo apt-get update
sudo apt-get install -y $LIST_OF_APPS

LIST_PYTHON_PACKAGES="numpy matplotlib pandas seaborn ipython ipykernel jinja2 colorama"
echo "############################################"
echo "installing python3 packages... $LIST_PYTHON_PACKAGES"
echo "############################################"

pip3 install $LIST_PYTHON_PACKAGES