# Hosting the C++ version

The C++ executable serves the API and frontend itself. Uploading the HTML alone will not provide bookings.

## Live Render deployment

The native C++20 application is live at **[https://journeydesk.onrender.com/](https://journeydesk.onrender.com/)**. It runs as a Docker web service on Render's **free** compute plan in **Ohio**. No Python or Node.js process implements the backend.

The service was configured through the official Render CLI and API. Updates deploy automatically from GitHub; the latest release was monitored through the Render connector. The group-booking release runs source commit `c0031c57e2062915705cae9fe9b5ff9c211353c1` and reached `live` on 27 October 2026. The Docker build passed both CTest suites. The catalogue has 25 services across all 24 directed city-and-transport combinations. Group requests use `/api/book/group`, while the original single-ticket API remains compatible. See `VERIFICATION.md` for the detailed test record and remaining visual checks.

The service listens on `0.0.0.0` using `PORT`. Configure `RESERVATION_PUBLIC_HOST` as the exact public hostname, `journeydesk.onrender.com`, without a scheme or path. The app's hostname allowlist also applies to the HTTP health check at `/api/health`. If the assigned hostname changes, update this setting before redeploying. A start wrapper can obtain Render's assigned hostname from `RENDER_EXTERNAL_HOSTNAME` at runtime.

**This deployment is a temporary demo, not durable booking storage.** The current JSON file is `/app/data/bookings.json` inside the container. Render's free service uses an ephemeral filesystem: restarting, redeploying or spinning down deletes local booking changes. Free services sleep after 15 minutes without inbound traffic and usually take about one minute to wake on the next request. A persistent disk requires a paid service; adding an external datastore would require changes to this app. No persistent disk or external datastore is configured for the free demo.

Official references: [Docker deployment](https://render.com/docs/docker), [health checks](https://render.com/docs/health-checks), [free service limits](https://render.com/docs/free).

## PythonAnywhere native socket hosting

PythonAnywhere's experimental async hosting supports **any HTTP server that listens on a Unix domain socket**, including a native C++ server. This project supports that through `DOMAIN_SOCKET` or `--unix-socket PATH`; no Python gateway is part of the application. This differs from a traditional WSGI Web-tab app.

Official documentation: https://help.pythonanywhere.com/pages/ASGICommandLine/

Account eligibility and domain availability must be checked before cutover. The beta requires the PythonAnywhere management CLI and an account API token. Token creation belongs to the account owner; never add tokens to source code or this submission. The management CLI may be Python-based, but the running application backend is the native C++ binary.

Upload the project, build on the Linux host, and run its tests:

```sh
cd /home/journeydesk/journeydesk-cpp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 1
ctest --test-dir build --output-on-failure
```

Compiler must support C++20 calendar facilities. If the host's `cmake` is unavailable, the equivalent native build is:

```sh
mkdir -p build
g++ -std=c++20 -O1 -pthread -Iinclude -Ivendor src/engine.cpp src/web.cpp src/main.cpp -o build/journeydesk
```

Configure the native server command for the hosting platform:

```sh
env RESERVATION_PUBLIC_HOST=journeydesk.pythonanywhere.com RESERVATION_DATA_FILE=/home/journeydesk/journeydesk-cpp/data/bookings.json /home/journeydesk/journeydesk-cpp/build/journeydesk --static /home/journeydesk/journeydesk-cpp/static
```

The platform supplies `DOMAIN_SOCKET`, which the executable detects. The server prints a generated desk access key to its private server log at startup; use an environment variable `RESERVATION_DESK_KEY` with a long private value for a stable key. Avoid printing that key in public screenshots or sharing it with passengers.

PythonAnywhere's documented registration shape is `pa website create --domain DOMAIN --command 'COMMAND'`. An existing WSGI registration on the same domain needs a controlled cutover. Preserve the old files and configuration until the native replacement works. The beta has unsettled long-term pricing and may change its CLI/API; consult current official instructions.

## Docker-capable hosting

The supplied `Dockerfile` builds and tests the C++ app, then runs it as an unprivileged user. Configure:

- `PORT` to the platform's HTTP port (default 8080).
- `RESERVATION_PUBLIC_HOST` to the exact public hostname, without scheme or path.
- `RESERVATION_DESK_KEY` to a private value of at least 12 characters.
- `RESERVATION_DATA_FILE` to a file in a writable directory. To preserve bookings across container replacement, that directory must use durable storage; the current free Render deployment does not.

Local example:

```sh
docker build -t journeydesk .
docker run --rm -p 8080:8080 -v journey-data:/app/data journeydesk
```

The local Docker example uses a named volume for local persistence. That volume is not part of the Render deployment.

## Status

The Render URL above serves the verified native C++20 backend. The earlier PythonAnywhere website is a separate deployment; no native Unix-socket cutover was performed there. Desktop/mobile visual inspection remains pending and is tracked in `VERIFICATION.md`.
