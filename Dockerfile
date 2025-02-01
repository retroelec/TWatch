FROM alpine:latest

RUN apk add --no-cache wget bash make python3 py3-pip py3-pyserial libstdc++ gcc g++ libc6-compat

RUN wget https://downloads.arduino.cc/arduino-cli/arduino-cli_latest_Linux_64bit.tar.gz && \
    wget https://github.com/Xinyuan-LilyGO/T-Watch-Deps/archive/refs/heads/master.zip -O T-Watch-Deps.zip && \
    wget https://github.com/Xinyuan-LilyGO/TTGO_TWatch_Library/archive/refs/heads/t-watch-s3.zip -O TTGO_TWatch_Library.zip && \
    tar -xzf arduino-cli_latest_Linux_64bit.tar.gz && \
    mv arduino-cli /usr/local/bin/ && \
    chmod +x /usr/local/bin/arduino-cli && \
    rm arduino-cli_latest_Linux_64bit.tar.gz && \
    mkdir /workspace && \
    unzip T-Watch-Deps.zip && \
    rm T-Watch-Deps.zip && \
    mv T-Watch-Deps-master /workspace/T-Watch-Deps && \
    unzip TTGO_TWatch_Library.zip && \
    rm TTGO_TWatch_Library.zip && \
    mv TTGO_TWatch_Library-t-watch-s3 /workspace/TTGO_TWatch_Library && \
    mkdir /workspace/TWatch && \
    arduino-cli config init --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json --overwrite && \
    arduino-cli core update-index && \
    arduino-cli core install esp32:esp32@2.0.9

WORKDIR /workspace/TWatch

ENTRYPOINT ["arduino-cli"]

