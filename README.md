# Journey Desk — Railway / Bus Reservation System

A college data-structures project with a **C++20 backend** and an HTML, CSS and JavaScript browser interface. The backend runs the HTTP API, validates passenger details, allocates seats, manages waiting queues and stores bookings. The browser displays the interface; it does not implement the reservation rules.

## Run on Windows

1. Extract the entire ZIP into a folder.
2. Double-click `start.bat`.
3. The C++ server opens `http://127.0.0.1:8080/` in your browser. Keep its terminal open while using the project.
4. Press Ctrl+C in that terminal to stop the server.

The supplied `bin/journeydesk.exe` runs without installing Python, Node.js or a compiler. The **desk access key** appears in the server terminal. Use it to open the normal passenger manifest. The fictional demo is accessible without that key.

If port 8080 is occupied, run from the project folder:

```powershell
.\bin\journeydesk.exe --port 8081 --static .\static --data .\data\bookings.json
```

Normal journeys start with **no passengers**. Bookings are saved in `data/bookings.json` after a successful mutation. Preserve that file to keep records. To run a separate clean practice database, pass a different file through `--data`.

## What to demonstrate

- Choose Bus or Train, departure city, destination city and travel date. Search the sample timetable, select a service and check its availability.
- Enter a traveller's name, contact, age and gender. Choose an available seat or automatic allocation. Review the details and confirm.
- Keep the generated booking reference. Find a ticket using its reference and the same contact number; confirmed, waiting and cancelled tickets remain searchable.
- Cancel a ticket. When the journey is full, its seat goes directly to the first waiting passenger. Otherwise the seat becomes available.
- Open **Demo** and load its fictional sample. Seat 2 belongs to P102 Priya Patel; cancelling it assigns that exact seat to P108 Neha Patel. P109 Raj Kumar and P110 Amit Patel then move forward in the queue.
- Open **Reservation desk** for the passenger list, ID/name/reference search, and waiting queue. A demo reset affects only fictional demo records.

The source includes an array for seats, a custom linked list for records, a custom FIFO queue for the waiting list, and a custom min-heap priority queue for free seats. See `PROJECT_REPORT.md` for explanations and viva preparation.

## Build from source

Requires a C++20 compiler and CMake 3.20+. Windows source builds use MinGW-w64 GCC and Ninja or MinGW Makefiles. The two header-only dependencies are bundled under `vendor/`; no dependency downloads occur during a normal build.

Windows: run `build.bat` after installing the compiler and CMake. Linux:

```sh
sh build.sh
./build/journeydesk --static ./static --data ./data/bookings.json
```

Or use CMake directly:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

For very limited build memory, reduce parallelism to one. JavaScript syntax can be checked with `node --check static/app.js`; Node.js is a development check only, not a runtime dependency.

## Scope

Schedules, fares and availability belong to this project. They are fictional and are **not connected to IRCTC or an actual bus operator**. Four cities and a small set of direct services keep the algorithms understandable. One traveller is booked per reservation. There is no payment, SMS or real train tracking integration.

Passenger manifests and cancellation controls are separated from the public seat map. The public map shows only whether a seat is available. Individual ticket lookup requires the booking reference and contact number. The normal desk requires its private access key. Use fictional personal details for classroom demonstrations.

## Files

| Path | Purpose |
|---|---|
| `include/structures.hpp` | Custom linked list, linked queue and min-heap |
| `include/engine.hpp`, `src/engine.cpp` | Booking rules, catalogue, validation, search, persistence |
| `include/web.hpp`, `src/web.cpp` | HTTP routes, request validation, protected desk, static allowlist |
| `src/main.cpp` | Native executable entry point and command-line options |
| `static/` | Browser interface and original SVG illustration |
| `tests/` | C++ behavioral and HTTP integration tests |
| `bin/journeydesk.exe` | Portable Windows build |
| `docs/` | Design, API contract and implementation plan |
| `HOSTING.md` | Native C++ deployment options |
| `VERIFICATION.md` | Verified checks and remaining limitations |

See `THIRD_PARTY_NOTICES.md` and the included licences for HTTP/JSON infrastructure.
