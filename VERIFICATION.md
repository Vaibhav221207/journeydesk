# Verification — C++ revision, 27 October 2026

## Verified

- Native C++20 build succeeds with MinGW-w64 GCC 15.2.0, CMake 4.1.0 and Ninja on Windows.
- **33 engine behavioral tests passed.** They exercise custom structure copying/order/heap repair, clean initial journeys, route search, automatic/preferred seats, duplicate rejection, contact-authorized ticket lookup, cancelled history, exact-seat FIFO promotion, waiting cancellation, filtered queue positions, protected desk scope, service/date/demo isolation, date/passenger validation, persistence reload, failed-save rollback, corrupt-file rejection and concurrent allocation.
- **12 HTTP integration tests passed.** They exercise C++ health identification, static allowlist/security headers, absence of public passenger data, desk key enforcement, JSON/media validation, same-origin/foreign-origin/opaque-origin checks, request-size limit, internal flag rejection, demo isolation, ticket-contact authorization and desk cancellation scope.
- The complete CTest run passed both suites: **45 behavioral cases, zero failures** (1.23 seconds in the final integrated run).
- A separate smoke check launched the actual executable and verified empty normal startup, multiple matching bus services, frontend asset serving, preferred-seat booking, persistence across an actual process stop/restart, lookup, cancellation and history. It loaded the full fictional demo, cancelled seat 2, confirmed promotion of P108 Neha Patel into exactly seat 2, and confirmed normal journeys remained unchanged.
- JavaScript syntax check passed. The interface implementer checked DOM references, CSS/SVG structure and async race behavior; independent source review found no remaining release-blocking issue. Skip-link navigation and stale aborted-response handling were corrected during review.
- Native Windows executable uses system DLLs only: KERNEL32, msvcrt, SHELL32 and WS2_32. It requires no Python, Node.js, compiler or separately installed C++ runtime to run.
- The JSON library checksum matches its official pinned release; HTTP/JSON headers and their MIT licences are bundled.

## Development evidence

Engine tests were observed failing against unimplemented behaviors before implementation (0/28), then passing (28/28). Additional loader regressions failed before their fixes and the final suite reached 33/33. HTTP tests were observed failing against empty routes (0/12); implementation reached 11/12, then the bounded-body request-hook correction produced 12/12. The HTTP checks ran with localhost networking enabled because the development sandbox initially denied socket connections.

Independent review also identified two launcher issues: source builds could launch the older bundled binary, and Unix-socket startup could remove a live endpoint. The launcher now prefers the source build, and Unix-socket startup checks for an active listener before removing only a stale socket.

## Pending checks and deployment

Browser access is disabled in the current Work settings. **Desktop/mobile visual inspection and live browser interaction checks have not been completed.** Responsive and print styles are implemented and source-reviewed; this is not a claim that screenshots or rendered layouts were verified.

The Windows package and native HTTP behavior are verified locally. Linux/Docker builds and Unix-socket hosting are provided but have not been executed in this environment. PythonAnywhere's existing website still serves the earlier Python version; the native C++ replacement has **not** been deployed. Updating that public URL requires restored browser access and confirmation of the account's native-socket hosting support.

Use `ctest --test-dir build --output-on-failure` after building to rerun the included suites. See README.md for local startup and HOSTING.md for hosting requirements.
