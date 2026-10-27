# Verification — C++ revision, 27 October 2026

## Verified

- Native C++20 build succeeds with MinGW-w64 GCC 15.2.0, CMake 4.1.0 and Ninja on Windows.
- **36 engine behavioral tests passed.** They exercise custom structure copying/order/heap repair, clean initial journeys, route search, automatic/preferred seats, duplicate rejection, contact-authorized ticket lookup, cancelled history, exact-seat FIFO promotion, waiting cancellation, filtered queue positions, protected desk scope, service/date/demo isolation, date/passenger validation, persistence reload, failed-save rollback, corrupt-file rejection and concurrent allocation.
- **12 HTTP integration tests passed.** They exercise C++ health identification, static allowlist/security headers, absence of public passenger data, desk key enforcement, JSON/media validation, same-origin/foreign-origin/opaque-origin checks, request-size limit, internal flag rejection, demo isolation, ticket-contact authorization and desk cancellation scope.
- The complete CTest run passed both suites: **48 behavioral cases, zero failures** (1.21 seconds in the route-expansion verification run).
- Route coverage tests exercise all 24 ordered city-and-transport combinations and booking, cancellation and retained ticket history on every one of the 25 services. Service IDs/numbers are unique, and arrival times/day offsets agree with journey durations. The original ten services retain their details and identifiers.
- The updated bundled Windows executable passed an HTTP smoke check for all 24 route searches and a new Pune–Ahmedabad train reservation, lookup and cancellation. The smoke booking was cancelled and availability restored.
- A separate smoke check launched the actual executable and verified empty normal startup, multiple matching bus services, frontend asset serving, preferred-seat booking, persistence across an actual process stop/restart, lookup, cancellation and history. It loaded the full fictional demo, cancelled seat 2, confirmed promotion of P108 Neha Patel into exactly seat 2, and confirmed normal journeys remained unchanged.
- JavaScript syntax check passed. The interface implementer checked DOM references, CSS/SVG structure and async race behavior; independent source review found no remaining release-blocking issue. Skip-link navigation and stale aborted-response handling were corrected during review.
- Native Windows executable uses system DLLs only: KERNEL32, msvcrt, SHELL32 and WS2_32. It requires no Python, Node.js, compiler or separately installed C++ runtime to run.
- The JSON library checksum matches its official pinned release; HTTP/JSON headers and their MIT licences are bundled.

## Live native C++ deployment

**[Journey Desk on Render](https://journeydesk.onrender.com/)** runs the native C++20 executable in a Linux Docker container. Deployment was performed through the official Render CLI and API; Render MCP was not used.

- Service: `srv-dbgc7h8m7kps73c11cpg`, free compute plan, Ohio region.
- Live deploy: `dep-dbgcjl2jnfac739vp14g`, source commit `1c52c2a6a51bbe3976cd5d6c26fa473754b403ed`, verified on 27 October 2026.
- Linux Docker build logs confirmed `100% tests passed, 0 tests failed out of 2` at `2026-10-27 15:58:00 UTC`. These are the engine and HTTP CTest suites for the expanded timetable.
- The public `/api/health` endpoint returned `{"backend":"C++20","ok":true}`.
- Live HTTPS checks confirmed 25 services and nonempty search results for all 24 directed city-and-transport combinations. A new Pune–Ahmedabad train booking, authorized lookup and cancellation passed, restoring its prior availability. Earlier HTTPS smoke checks also covered frontend/static assets, preferred seat 7 booking and rejection of a mismatched contact.
- The normal reservation desk returned `401` without its key, and requesting a source file returned `404`.
- The initial fictional demo smoke promoted P108 Neha Patel into exactly seat 2 after cancellation, advanced the waiting queue and reset its original sample afterwards. This route expansion preserves that demo service and tests its behavior in the engine suite.

These live checks exercise HTTP behavior and reservation rules; they do not establish that the browser layout was visually inspected.

The free Render deployment uses **ephemeral local JSON storage**. Booking changes are lost when the service restarts, redeploys or spins down. It sleeps after 15 minutes without inbound traffic, and the next request can take about one minute to wake it. No persistent disk or external datastore is configured. Local Windows persistence across a process restart was verified separately and does not imply durability on Render.

## Development evidence

Engine tests were observed failing against unimplemented behaviors before implementation (0/28), then passing (28/28). Additional loader regressions failed before their fixes and the final suite reached 33/33. HTTP tests were observed failing against empty routes (0/12); implementation reached 11/12, then the bounded-body request-hook correction produced 12/12. The HTTP checks ran with localhost networking enabled because the development sandbox initially denied socket connections.

For the route expansion, the original catalogue produced 33 passes and three expected failures: a missing Pune–Ahmedabad service, incomplete city/mode coverage, and booking on a new route. After adding 15 services, all 36 engine cases passed. The final lifecycle test was broadened to every service, and the complete 48-case suite passed again.

Independent review also identified two launcher issues: source builds could launch the older bundled binary, and Unix-socket startup could remove a live endpoint. The launcher now prefers the source build, and Unix-socket startup checks for an active listener before removing only a stale socket.

## Pending checks

The browser automation tool continues to report that local browser use is disabled. **Rendered desktop/mobile visual inspection and live browser interaction checks have not been completed.** Responsive and print styles are implemented and source-reviewed; screenshots and rendered layouts remain unverified.

The Windows package and Render's Linux Docker deployment are verified as described above. Native Unix-socket hosting on PythonAnywhere has not been exercised or deployed; the earlier PythonAnywhere website remains separate from the live Render C++ application.

Use `ctest --test-dir build --output-on-failure` after building to rerun the included suites. See README.md for local startup and HOSTING.md for hosting requirements.
