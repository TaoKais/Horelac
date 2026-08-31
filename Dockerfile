FROM debian:trixie AS build
RUN apt-get update && apt-get install -y --no-install-recommends build-essential cmake ninja-build git pkg-config libsqlite3-dev libcairo2-dev libssl-dev zlib1g-dev ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    && cmake --build build --target horelac

FROM debian:trixie-slim AS runtime
RUN apt-get update && apt-get install -y --no-install-recommends libsqlite3-0 libcairo2 libssl3 zlib1g ca-certificates tzdata && rm -rf /var/lib/apt/lists/* && groupadd --system horelac && useradd --system --gid horelac --home-dir /app horelac
WORKDIR /app
COPY --from=build /src/build/horelac /usr/local/bin/horelac
COPY --from=build /src/locales /app/locales
COPY --from=build /src/migrations /app/migrations
RUN mkdir -p /data /app/rendered && chown -R horelac:horelac /data /app
USER horelac
ENV DATABASE_PATH=/data/horelac.db
VOLUME ["/data"]
STOPSIGNAL SIGTERM
ENTRYPOINT ["/usr/local/bin/horelac"]
