FROM registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest

WORKDIR /app

# Force IPv4: CI runners sometimes resolve apt.llvm.org to an IPv6-only
# answer while having no IPv6 route, which makes the key download fail.
RUN wget -4 --tries=5 --waitretry=5 -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
      | gpg --dearmor -o /etc/apt/trusted.gpg.d/llvm.gpg
RUN echo "deb http://apt.llvm.org/bullseye/ llvm-toolchain-bullseye-18 main" >> /etc/apt/sources.list.d/llvm.list
RUN echo "deb-src http://apt.llvm.org/bullseye/ llvm-toolchain-bullseye-18 main" >> /etc/apt/sources.list.d/llvm.list
# Debian 11 left LTS in August 2026 and the bullseye-security Release file
# the SDK image still points at is no longer refreshed, so its Valid-Until
# has lapsed and apt refuses it. Nothing we install comes from there; accept
# the stale index instead of failing on it.
RUN printf '%s\n' 'Acquire::ForceIPv4 "true";' 'Acquire::Check-Valid-Until "false";' \
      > /etc/apt/apt.conf.d/99source2mod
RUN apt update && apt install -y clang-18 lld-18 libc++-18-dev libc++abi-18-dev
RUN ln -sf /usr/bin/clang-18 /usr/bin/clang
RUN ln -sf /usr/bin/clang++-18 /usr/bin/clang++

RUN apt update && apt install -y \
    build-essential \
    cmake \
    ninja-build \
    git \
    zlib1g \
    zlib1g-dev \
    libssl-dev \
    libprotobuf-dev \
    protobuf-compiler \
    pkg-config \
    libcurl4-openssl-dev \
    curl

RUN git config --system --add safe.directory '*'

COPY docker/docker-entrypoint.sh ./docker-entrypoint.sh
RUN sed -i 's/\r$//' ./docker-entrypoint.sh && chmod +x ./docker-entrypoint.sh

WORKDIR /app/source

CMD ["/bin/bash", "../docker-entrypoint.sh"]