FROM ubuntu:18.04 AS builder

ARG USER=ece257a
ARG USERDIR=/home/${USER}

RUN adduser --disabled-password --gecos "" ${USER}

RUN apt -y update && apt -y install \
    build-essential \ 
    software-properties-common

RUN add-apt-repository ppa:ubuntu-toolchain-r/test

RUN apt -y update && apt -y install \
    g++-11 \
    cmake \
    autotools-dev \
    autoconf \
    libzmq3-dev \
    libfftw3-dev \
    libmbedtls-dev \
    libboost-program-options-dev \
    libconfig++-dev \
    libsctp-dev 

COPY --chown=${USER} . ${USERDIR}/srsran/

RUN cd ${USERDIR}/srsran/ && mkdir build_docker && cd build_docker && cmake .. && make -j 4 && make install && ldconfig
USER ${USER}
RUN cd ${USERDIR}/srsran/build_docker && ./srsran_install_configs.sh user
