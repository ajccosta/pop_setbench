#!/bin/bash
if [ ! -d "pytvenv" ]; then
  python3 -m venv ./pytvenv
  ./pytvenv/bin/python3 -m pip install numpy pandas matplotlib seaborn colorama IPython
fi
export VIRTUAL_ENV=$(realpath ./pytvenv/);
export PATH=$(realpath ./pytvenv/bin/):$PATH;
