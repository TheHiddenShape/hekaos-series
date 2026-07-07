FROM ubuntu:latest

RUN apt-get update && apt-get install -y \
    wget \
    curl \
    ca-certificates \
    build-essential \
    xorriso \
    qemu-system-x86 \
    libgmp3-dev \
    libmpc-dev \
    libmpfr-dev \
    grub-pc-bin \
    m4

ENV HOME="/root"
ENV PREFIX="${HOME}/opt/cross"
ENV TARGET="i686-elf"
ENV RUSTUP_HOME="${HOME}/.rustup"
ENV CARGO_HOME="${HOME}/.cargo"
ENV PATH="${PREFIX}/bin:${CARGO_HOME}/bin:${PATH}"

RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y --profile minimal && \
    rustup target add i686-unknown-linux-gnu

RUN mkdir -p $HOME/src && cd $HOME/src && \
    wget https://ftp.gnu.org/gnu/binutils/binutils-2.44.tar.xz && \
    tar -xf binutils-2.44.tar.xz && \
    mkdir build-binutils && cd build-binutils && \
    ../binutils-2.44/configure --target=$TARGET --prefix="$PREFIX" --with-sysroot --disable-nls --disable-werror && \
    make -j$(nproc) && make install

RUN cd $HOME/src && \
    wget https://ftp.gnu.org/gnu/gcc/gcc-14.2.0/gcc-14.2.0.tar.xz && \
    tar -xf gcc-14.2.0.tar.xz && \
    mkdir build-gcc && cd build-gcc && \
    ../gcc-14.2.0/configure --target=$TARGET --prefix="$PREFIX" --disable-nls --enable-languages=c,c++ --without-headers --disable-hosted-libstdcxx && \
    make -j$(nproc) all-gcc && \
    make -j$(nproc) all-target-libgcc && \
    make -j$(nproc) all-target-libstdc++-v3 && \
    make install-gcc && \
    make install-target-libgcc && \
    make install-target-libstdc++-v3

COPY include/ $HOME/include/
COPY src/ $HOME/src/
COPY grub.cfg $HOME/
COPY Makefile $HOME/

RUN cd $HOME && make all

CMD ["/bin/bash"]
