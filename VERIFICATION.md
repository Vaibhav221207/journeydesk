# Verification — C++ revision, 27 October 2026

## Verified

- Native C++20 build succeeds with MinGW-w64 GCC 15.2.0, CMake 4.1.0 and Ninja on Windows.
- **55 engine behavioral tests passed.** They exercise custom structure copying/order/heap repair, clean initial journeys, route search, automatic/preferred seats, duplicate rejection, contact-authorized ticket lookup, cancelled history, exact-seat FIFO promotion, waiting cancellation, filtered queue positions, protected desk scope, service/date/demo isolation, date/passenger validation, persistence reload, failed-save rollback, corrupt-file rejection, concurrent allocation and atomic group booking.
- **15 HTTP integration tests passed.** They exercise C++ health identification, static allowlist/security headers, absence of public passenger data, desk key enforcement, JSON/media validation, same-origin/foreign-origin/opaque-origin checks, request-size limit, internal flag rejection, demo isolation, ticket-contact authorization, desk cancellation scope and multi-passenger responses/errors/promotion.
- **19 Windows desk-key tests passed.** They cover saved-key reuse, private generation, environment precedence, per-data-directory isolation, failed writes, malformed/nonregular key files, bounded reads, key alphabet and concurrent exclusive creation.
- The complete Windows CTest run passed all three suites: **89 behavioral cases, zero failures** (2.56 seconds in the final stable-key verification run). Linux includes two additional POSIX permission/symlink cases.
- Route coverage tests exercise all 24 ordered city-and-transport combinations and booking, cancellation and retained ticket history on every one of the 25 services. Service IDs/numbers are unique, and arrival times/day offsets agree with journey durations. The original ten services retain their details and identifiers.
- The updated bundled Windows executable passed an HTTP smoke check for all 24 route searches and a new Pune–Ahmedabad train reservation, lookup and cancellation. The smoke booking was cancelled and availability restored.
- The group-booking Windows executable passed an HTTP smoke for six-person submission, indexed invalid-member rollback, shared contact, unique references, later-member preferred-seat priority, mixed confirmed/waiting tickets, exact-seat promotion and independent cancellation. All test seats were released afterwards.
- A separate smoke check launched the actual executable and verified empty normal startup, multiple matching bus services, frontend asset serving, preferred-seat booking, persistence across an actual process stop/restart, lookup, cancellation and history. It loaded the full fictional demo, cancelled seat 2, confirmed promotion of P108 Neha Patel into exactly seat 2, and confirmed normal journeys remained unchanged.
- JavaScript syntax check passed. The interface implementer checked DOM references, CSS/SVG structure and async race behavior; independent source review found no remaining release-blocking issue. Skip-link navigation and stale aborted-response handling were corrected during review.
- Native Windows executable uses system DLLs only: KERNEL32, msvcrt, SHELL32 and WS2_32. It requires no Python, Node.js, compiler or separately installed C++ runtime to run.
- The JSON library checksum matches its official pinned release; HTTP/JSON headers and their MIT licences are bundled.

## Live native C++ deployment

**[Journey Desk on Render](https://journeydesk.onrender.com/)** runs the native C++20 executable in a Linux Docker container. Initial configuration used the official Render CLI and API. The group-booking release deployed automatically from GitHub and was verified through the Render connector.

- Service: `srv-dbgc7h8m7kps73c11cpg`, free compute plan, Ohio region.
- Live deploy: `dep-dbgejsk9v7es73cubfbg`, source commit `7669fcc3fdf701bdff196b41c2c3f26957fc7b5d`, verified on 27 October 2026.
- Linux Docker build logs confirmed `100% tests passed, 0 tests failed out of 3` at `2026-10-27 18:16:25 UTC`. These are the desk-key, engine and HTTP CTest suites. The Linux desk-key suite includes the POSIX-only permission and symlink cases.
- The public `/api/health` endpoint returned `{"backend":"C++20","ok":true}`.
- The live group-checkout smoke passed with same-origin requests: six-person submission, indexed invalid-member rollback, shared contact and unique references, manual-seat priority, two confirmed/one waiting, exact-seat promotion and independent cancellation. All QA tickets were cancelled afterwards, restoring the test journey's original availability.
- The earlier route-release HTTPS checks confirmed 25 services and nonempty search results for all 24 directed city-and-transport combinations. A new Pune–Ahmedabad train booking, authorized lookup and cancellation passed, restoring its prior availability. Earlier HTTPS smoke checks also covered frontend/static assets, preferred seat 7 booking and rejection of a mismatched contact.
- The normal reservation desk returned `401` without its key, and requesting a source file returned `404`.
- The initial fictional demo smoke promoted P108 Neha Patel into exactly seat 2 after cancellation, advanced the waiting queue and reset its original sample afterwards. This route expansion preserves that demo service and tests its behavior in the engine suite.

These live checks exercise HTTP behavior and reservation rules; they do not establish that the browser layout was visually inspected.

The live HTML, JavaScript and CSS matched the reviewed local files. JavaScript syntax, DOM IDs/references and focused DOM/async probes passed for passenger add/remove/limits, preserved inputs, unique preferences, allocation previews, exact shared-contact payload, one mutation under repeated clicks, indexed errors and receipt/print state. Independent engine and interface source review found no remaining blocking issue. Rendered browser and actual printed-page visual QA remain unverified.

The free Render deployment uses **ephemeral local JSON storage**. Booking changes are lost when the service restarts, redeploys or spins down. It sleeps after 15 minutes without inbound traffic, and the next request can take about one minute to wake it. No persistent disk or external datastore is configured. Local Windows persistence across a process restart was verified separately and does not imply durability on Render.

## Development evidence

Engine tests were observed failing against unimplemented behaviors before implementation (0/28), then passing (28/28). Additional loader regressions failed before their fixes and the final suite reached 33/33. HTTP tests were observed failing against empty routes (0/12); implementation reached 11/12, then the bounded-body request-hook correction produced 12/12. The HTTP checks ran with localhost networking enabled because the development sandbox initially denied socket connections.

For the route expansion, the original catalogue produced 33 passes and three expected failures: a missing Pune–Ahmedabad service, incomplete city/mode coverage, and booking on a new route. After adding 15 services, all 36 engine cases passed. The final lifecycle test was broadened to every service, and the complete 48-case suite passed again.

Group-engine tests first produced 36 passes and 19 expected failures against the missing implementation, then passed 55/55 after implementation. Three new HTTP tests failed against the absent endpoint (12 passes, 3 failures), then all 15 passed after wiring the route. The final integrated 70-case run passed. Group tests cover all-member validation and ID rollback, duplicate/preferred-seat conflicts, manual-before-auto allocation, FIFO overflow/promotion, persistence/write failure and concurrent groups.

## Stable desk key

`RESERVATION_DESK_KEY` takes precedence without reading or creating a key file. Without the override, startup generates a strong 32-character key once into `desk.key` beside the selected booking data file and reuses it. Existing malformed, inaccessible or incompatible key files fail closed and are preserved. File reads are bounded; custom keys accept 12–256 header-safe ASCII letters, digits and `._~-`. POSIX key creation uses owner-only permissions. Key values are excluded from source, Docker build context and submission ZIP.

Key-resolution TDD began with five passes and nine expected failures. After implementation, fourteen cases passed. Independent review identified unbounded key reads and values incompatible with browser/header handling; both received failing regressions before their fixes. The final Windows key suite passed 19/19. An actual native-process smoke verified reuse after restart, environment override without changing the saved key, and restoration of that same default after removing the override. The private hosted key is configured through Render's environment setting; the D-drive installation is configured separately with the same value.

The D-drive executable accepted its configured key after two actual process starts without changing the saved key file or passenger bookings. The hosted key was accepted after both its environment update and the subsequent source deployment; a wrong key returned 401. Key values were checked as absent from the uploaded source and excluded from the package whitelist.

Independent review also identified two launcher issues: source builds could launch the older bundled binary, and Unix-socket startup could remove a live endpoint. The launcher now prefers the source build, and Unix-socket startup checks for an active listener before removing only a stale socket.

## Pending checks

The browser automation tool continues to report that local browser use is disabled. **Rendered desktop/mobile visual inspection and live browser interaction checks have not been completed.** Responsive and print styles are implemented and source-reviewed; screenshots and rendered layouts remain unverified.

The Windows package and Render's Linux Docker deployment are verified as described above. Native Unix-socket hosting on PythonAnywhere has not been exercised or deployed; the earlier PythonAnywhere website remains separate from the live Render C++ application.

Use `ctest --test-dir build --output-on-failure` after building to rerun the included suites. See README.md for local startup and HOSTING.md for hosting requirements.
