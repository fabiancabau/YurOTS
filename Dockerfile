FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ cmake make pkg-config libxml2-dev liblua5.1-0-dev libboost-regex-dev && rm -rf /var/lib/apt/lists/*
WORKDIR /build
COPY ots/source/ ./source/
RUN cmake -S source -B out -DCMAKE_BUILD_TYPE=Release && cmake --build out -j4
FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends libxml2 liblua5.1-0 libboost-regex1.74.0 && rm -rf /var/lib/apt/lists/*
COPY --from=build /build/out/yurots /usr/local/bin/yurots
COPY ots /opt/yurots-seed
COPY scripts/start-server.sh /usr/local/bin/start-server
RUN mkdir -p /var/lib/yurots && chown 1000:1000 /var/lib/yurots
USER 1000:1000
WORKDIR /var/lib/yurots
EXPOSE 7171
ENTRYPOINT ["start-server"]
