FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Install dependencies
RUN apt-get update && apt-get install -y \
    curl \
    git \
    xz-utils \
    unzip \
    python3 python3-pip python3-venv \
    build-essential \
    cmake ninja-build ccache \
    libusb-1.0-0 \
    gosu \
    wget \
    && rm -rf /var/lib/apt/lists/* \
    && ln -s /usr/bin/python3 /usr/bin/python
	
# 2. Download and extract the NiosII compiler
# We'll install it into /opt/nios2
RUN mkdir -p /opt/nios2 && \
    curl -L https://newos.org/toolchains/nios2-elf-7.5.0-Linux-x86_64.tar.xz \
    | tar -xvJC /opt/nios2 --strip-components=1 && \
    chmod 755 /opt && \
    chmod -R 755 /opt/nios2
    
# 3. Download Newlib source
RUN wget ftp://sourceware.org/pub/newlib/newlib-3.3.0.tar.gz && \
    tar -xf newlib-3.3.0.tar.gz

# 4. Add to PATH
ENV PATH="/opt/nios2/bin:${PATH}"

# 5. Build and install into your existing toolchain path
RUN mkdir build-newlib && cd build-newlib && \
    ../newlib-3.3.0/configure \
        --target=nios2-elf \
        --prefix=/opt/nios2 \
        --enable-newlib-multithread \
        --disable-newlib-supplied-syscalls && \
    make -j$(nproc) && \
    make install

# # 6. Install ESP-IDF
# ENV IDF_PATH=/opt/esp/esp-idf
# 
# # 7. Install the Espressif toolchain
# RUN mkdir -p /opt/esp && cd /opt/esp && \
#     git clone --recursive --branch v5.3.1 https://github.com/espressif/esp-idf  && \
#     $IDF_PATH/install.sh esp32s3 && \
#     rm -rf /root/.espressif/dist/* && \
#     rm -rf $IDF_PATH/.git && \
#     chmod -R 755 /opt/esp && \
#     chmod 755 /root && \
#     chmod -R 755 /root/.espressif
#     
# #    $IDF_PATH/install.sh --features=esp-idf-build esp32s3 && \
# 
# ENV IDF_TOOLS_PATH=/root/.espressif
# 

ARG CACHEBUST=1
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
