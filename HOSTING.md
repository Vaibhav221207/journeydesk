# Hosting the C++ version

The C++ executable serves the API and frontend itself. Uploading the HTML alone will not provide bookings. The previous Python WSGI deployment must not be described as the C++ backend.

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
- `RESERVATION_DATA_FILE` to a file on a persistent mounted directory.

Local example:

```sh
docker build -t journeydesk .
docker run --rm -p 8080:8080 -v journey-data:/app/data journeydesk
```

Render supports Docker web services: https://render.com/docs/docker . Its free web services have **ephemeral files**: bookings saved only to local JSON disappear on restart, redeploy or spin-down. Persistent disks require a paid service. A free deployment must be labelled as a temporary demo unless durable storage is added. Official limits: https://render.com/docs/free . Do not purchase a hosting plan automatically.

## Status

Deployment of the revised native C++ version has not yet been verified. Local compilation, testing and visual verification are tracked separately in `VERIFICATION.md`. The existing PythonAnywhere URL currently belongs to the earlier Python version until a successful native cutover is recorded.
