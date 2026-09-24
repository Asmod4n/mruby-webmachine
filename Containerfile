# The compilers, as signed packages from an apt source and nothing
# built here: g++ 16 from Debian sid, clang 23 from apt.llvm.org.
# Both build against the libstdc++ of g++ 16. bench/matrix.sh and
# bench/measure run inside; bench/results is mounted from the host so
# the numbers leave the container.
#
# Behind a proxy that terminates TLS, mount its certificate and name
# it: --network=host -v /path/ca.crt:/proxy-ca.crt:ro --build-arg CA=/proxy-ca.crt
FROM debian:sid
ARG CA=
RUN set -e; \
    if [ -n "$CA" ]; then \
        sed -i 's|http://deb.debian.org|https://deb.debian.org|' /etc/apt/sources.list.d/debian.sources; \
        APT="-o Acquire::https::CAInfo=$CA"; \
    fi; \
    apt-get $APT update; \
    DEBIAN_FRONTEND=noninteractive apt-get $APT install -y --no-install-recommends ca-certificates curl gnupg; \
    if [ -n "$CA" ]; then cat "$CA" >> /etc/ssl/certs/ca-certificates.crt; fi; \
    curl -sS https://apt.llvm.org/llvm-snapshot.gpg.key | gpg --dearmor -o /etc/apt/keyrings/llvm.gpg; \
    echo "deb [signed-by=/etc/apt/keyrings/llvm.gpg] https://apt.llvm.org/unstable/ llvm-toolchain-23 main" \
        > /etc/apt/sources.list.d/llvm.list; \
    apt-get update; \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        g++-16 clang-23 make ruby rake bison git pkg-config libbenchmark-dev libssl-dev zlib1g-dev; \
    rm -rf /var/lib/apt/lists/*; \
    update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-16 100; \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-16 100; \
    update-alternatives --install /usr/bin/cc cc /usr/bin/gcc-16 100; \
    update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-16 100; \
    update-alternatives --install /usr/bin/clang clang /usr/bin/clang-23 100; \
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-23 100
WORKDIR /work
