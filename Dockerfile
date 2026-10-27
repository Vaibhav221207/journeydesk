FROM debian:bookworm-slim AS build
RUN apt-get update && apt-get install -y --no-install-recommends g++ cmake make \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /source
COPY CMakeLists.txt ./
COPY include ./include
COPY src ./src
COPY tests ./tests
COPY vendor ./vendor
RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --parallel 2 \
    && ctest --test-dir build --output-on-failure

FROM debian:bookworm-slim
RUN apt-get update && apt-get install -y --no-install-recommends libstdc++6 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --create-home --uid 10001 journey \
    && mkdir -p /app/data && chown -R journey:journey /app
WORKDIR /app
COPY --from=build /source/build/journeydesk /app/journeydesk
COPY static /app/static
USER journey
ENV PORT=8080
ENV RESERVATION_DATA_FILE=/app/data/bookings.json
EXPOSE 8080
CMD ["/app/journeydesk", "--static", "/app/static"]
