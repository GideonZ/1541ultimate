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
    && rm -rf /var/lib/apt/lists/*
	
# 2. Download and extract the xPack RISC-V GCC 11.3.0-1 
# We'll install it into /opt/riscv
RUN mkdir -p /opt/riscv && \
    curl -L https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v11.3.0-1/xpack-riscv-none-elf-gcc-11.3.0-1-linux-x64.tar.gz \
    | tar -xzC /opt/riscv --strip-components=1 && \
    for file in /opt/riscv/bin/riscv-none-elf-*; do \
    ln -s "$file" "/usr/local/bin/riscv32-unknown-elf-${file##*riscv-none-elf-}"; \
    done && \
    chmod 755 /opt && \
    chmod -R 755 /opt/riscv
    
# 4. Add to PATH
ENV PATH="/opt/riscv/bin:${PATH}"

# 6. Install ESP-IDF
ENV IDF_PATH=/opt/esp/esp-idf

# 7. Install the Espressif toolchain
RUN mkdir -p /opt/esp && cd /opt/esp && \
    git clone --recursive --branch v5.3.1 https://github.com/espressif/esp-idf  && \
    $IDF_PATH/install.sh esp32s3 && \
    rm -rf /root/.espressif/dist/* && \
    rm -rf $IDF_PATH/.git && \
    chmod -R 755 /opt/esp && \
    chmod 755 /root && \
    chmod -R 755 /root/.espressif
    
#    $IDF_PATH/install.sh --features=esp-idf-build esp32s3 && \

ENV IDF_TOOLS_PATH=/root/.espressif

# Fix Python and install gosu
RUN ln -s /usr/bin/python3 /usr/bin/python && \
    apt-get update && apt-get install -y gosu && rm -rf /var/lib/apt/lists/*

ARG CACHEBUST=1
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
ENTRYPOINT ["/entrypoint.sh"]
