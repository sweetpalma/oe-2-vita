FROM vitasdk/vitasdk:latest

RUN apt-get update && apt-get install -y --no-install-recommends \
    cmake \
    gcc \
    make \
    pkg-config \
    libsdl2-dev \
    zlib1g-dev \
    libavcodec-dev \
    libavutil-dev \
 && rm -rf /var/lib/apt/lists/*

ENV OE2_DATA_DIR=/data
WORKDIR /work

VOLUME ["/work", "/data"]

CMD ["python3", "tools/build_vita.py"]

