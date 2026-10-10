# ============================================================
# Sri Mart backend — C++20 + Drogon + libpq (PostgreSQL).
# Used by Render/Fly/etc. Vercel cannot run this server, so the
# static UI (vercel.json) goes to Vercel and this image runs the API.
# Drogon is built from source against Ubuntu system libs (fast);
# vcpkg is deliberately NOT used (too slow for hosted builds).
# PORT and PG* come from the environment.
# ============================================================

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
# CPATH lets the compiler find libpq-fe.h from libpq-dev
# (/usr/include/postgresql) without touching CMakeLists.txt.
ENV CPATH=/usr/include/postgresql

# ca-certificates is REQUIRED: without it, git cannot verify TLS
# (github.com clone fails with "server certificate verification failed").
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake git pkg-config ca-certificates \
    libssl-dev libpq-dev zlib1g-dev libjsoncpp-dev uuid-dev \
    libc-ares-dev libbrotli-dev \
    && rm -rf /var/lib/apt/lists

# Drogon from source (trantor ships as a submodule); all other
# dependencies come from the apt packages above.
# NOTE: -j2 on purpose. Unbounded -j$(nproc) OOM-kills cc1plus on small
# builders halfway through these heavy translation units.
RUN echo "builder: $(nproc) cores, $(free -m | awk '/Mem:/{print $2}') MB RAM" \
    && git clone --depth 1 --recurse-submodules \
        https://github.com/drogonframework/drogon.git /opt/drogon \
    && cmake -S /opt/drogon -B /opt/drogon/build \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTING=OFF \
        -DBUILD_EXAMPLES=OFF \
        -DBUILD_ORM=ON \
    && cmake --build /opt/drogon/build -j2 \
    && cmake --install /opt/drogon/build \
    && test -f /usr/local/lib/cmake/Drogon/DrogonConfig.cmake \
    && echo DROGON_CONFIG_OK \
    && rm -rf /opt/drogon

WORKDIR /app
COPY . .

# find_package(Drogon) resolves via /usr/local from `cmake --install` above.
RUN cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j2

ENV PORT=8080
EXPOSE 8080

CMD ["./build/sri_mart"]
