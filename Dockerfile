FROM ubuntu:20.04
RUN apt update && apt install -y \
        build-essential \
        dos2unix \
        g++ \
        git \
        libnuma-dev \
        make \
        numactl \
        parallel \
        python3 \
        python3-pip \
        time \
        zip \
        bc \
        micro

RUN apt update && apt install -y \
        linux-tools-generic \
        linux-tools-common \
        && echo alias perf=$(find / -wholename "*-generic/perf") > ~/.bash_aliases

RUN pip3 install \
        numpy \
        matplotlib \
        pandas \
        seaborn \
        ipython \
        ipykernel \
        jinja2 \
        colorama

COPY . /pop_setbench/
WORKDIR /pop_setbench/

CMD bash -C 'myscript.sh';'bash'
