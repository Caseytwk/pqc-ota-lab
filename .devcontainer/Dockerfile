# PQC OTA lab environment: OpenSSL 3.5 (native ML-DSA) and Espressif's ESP32 QEMU.
# Used by GitHub Codespaces (devcontainer.json), the CI end to end test, and the offline fallback image.

# ---- Stage 1: OpenSSL 3.5 from source ----
FROM ubuntu:24.04 AS openssl
ARG OPENSSL_VERSION=3.5.9
RUN apt-get update \
 && apt-get install -y --no-install-recommends build-essential perl curl ca-certificates \
 && rm -rf /var/lib/apt/lists/*
RUN curl -fsSL "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz" -o /tmp/openssl.tar.gz \
 && tar -xzf /tmp/openssl.tar.gz -C /tmp \
 && cd "/tmp/openssl-${OPENSSL_VERSION}" \
 && ./Configure --prefix=/opt/openssl --libdir=lib -Wl,-rpath,/opt/openssl/lib no-tests no-docs \
 && make -j"$(nproc)" \
 && make install_sw

# ---- Stage 2: lab image ----
FROM ubuntu:24.04
ARG TARGETARCH
# Espressif QEMU release pinned to the version ESP-IDF v5.5.6 uses (tools/tools.json)
ARG QEMU_RELEASE=esp-develop-9.2.2-20260417
ARG QEMU_BUILD=esp_develop_9.2.2_20260417
ARG QEMU_SHA256_AMD64=0eecb2a34a5586c0e59110f77b9343b7b336e82fdb0e1a30e1dc1bab8a547e35
ARG QEMU_SHA256_ARM64=00de5985094c14e47d1b38464a006b8ed64fd0fa7a289c56da24f3a329f65339

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      python3 curl ca-certificates xz-utils procps sudo git less nano \
      libgcrypt20 libglib2.0-0t64 libpixman-1-0 libsdl2-2.0-0 libslirp0 \
 && rm -rf /var/lib/apt/lists/*

RUN set -eux; \
    case "${TARGETARCH:-amd64}" in \
      amd64) triple=x86_64-linux-gnu;  sum="${QEMU_SHA256_AMD64}" ;; \
      arm64) triple=aarch64-linux-gnu; sum="${QEMU_SHA256_ARM64}" ;; \
      *) echo "unsupported architecture ${TARGETARCH}" >&2; exit 1 ;; \
    esac; \
    curl -fsSL "https://github.com/espressif/qemu/releases/download/${QEMU_RELEASE}/qemu-xtensa-softmmu-${QEMU_BUILD}-${triple}.tar.xz" -o /tmp/qemu.tar.xz; \
    echo "${sum}  /tmp/qemu.tar.xz" | sha256sum -c -; \
    mkdir -p /opt/qemu-dl; \
    tar -xJf /tmp/qemu.tar.xz -C /opt/qemu-dl; \
    mv /opt/qemu-dl/qemu /opt/qemu; \
    rm -rf /opt/qemu-dl /tmp/qemu.tar.xz

COPY --from=openssl /opt/openssl /opt/openssl
RUN ln -s /opt/openssl/bin/openssl /usr/local/bin/openssl
ENV PATH=/opt/qemu/bin:${PATH}

# Non root workshop user
RUN userdel -r ubuntu 2>/dev/null || true \
 && useradd -m -s /bin/bash -u 1000 lab \
 && echo "lab ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/lab \
 && chmod 0440 /etc/sudoers.d/lab
USER lab
WORKDIR /home/lab

# Fail the build early if either tool is missing or too old
RUN openssl version | grep -q "OpenSSL 3\.[5-9]" \
 && openssl list -signature-algorithms | grep -q ML-DSA-44 \
 && qemu-system-xtensa --version
