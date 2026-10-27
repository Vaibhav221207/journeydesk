# Journey Desk C++ rebuild implementation plan

> For agentic workers: use subagent-driven-development; preserve the ownership and interface contracts below.

**Goal:** Deliver the approved attractive passenger booking site with a C++ backend and the required four structures.

**Architecture:** A C++20 HTTP process serves the static browser interface and JSON APIs. A thread-safe reservation engine owns custom structures and atomic JSON persistence. Build source and native Windows release separately from the prior Python project.

**Tech stack:** C++20, cpp-httplib (HTTP), nlohmann/json 3.12.0 (JSON), HTML/CSS/vanilla JavaScript, CMake, Docker.

**Spec:** `DESIGN.md` and `API_CONTRACT.md` in this directory.

## Global constraints

- All reservation, allocation, validation, search and persistence logic is C++.
- No normal journey is populated by sample passengers.
- Show fictional timetable/fare context honestly; preserve all seven original operations.
- Do not expose passenger manifests publicly; desk key protects real records, isolated demo remains inspectable.
- Larger accessible controls, phone layout without page overflow, no dashboard cards, no third-party runtime/frontend dependencies.

## Review focus

- Concurrent calls cannot double-allocate a seat or produce partial saves.
- Wrong contact, internal authorization flags and desk keys cannot expose/cancel other bookings.
- Failed loads cannot leave records from a previous route under new headings.
- Search/back/confirmation preserve coherent journey selection; search filters cannot hide confirmation.
- Empty, unavailable, full, waiting, cancelled and persisted records are distinct and understandable.

## Task 1 — Reservation engine and data structures

Owner: engine agent. Files `include/structures.hpp`, `include/engine.hpp`, `src/engine.cpp`, `tests/engine_tests.cpp`.

- [ ] Write behavioral C++ tests first and observe missing engine failure.
- [ ] Implement the exact public interface in API_CONTRACT.md, including custom structures, empty initial state, catalogue/search, booking/ticket/cancellation and isolated demo.
- [ ] Add service/date isolation, validation, duplicate rejection, concurrent allocations, persistence reload/corruption and failed-save rollback tests.
- [ ] Compile and run tests, self-review, report actual evidence.

## Task 2 — Attractive browser interface

Owner: interface agent. Files `static/index.html`, `static/styles.css`, `static/app.js`, `static/scene.svg`.

- [ ] Implement journey-first search and flat timetable result rows, passenger/seat step, review, ticket, lookup/cancel and desk/demo views using the API contract.
- [ ] Create an original restrained SVG scene suitable for travel; distinct bus/train seats; readable fonts, focus/error/empty/loading states, keyboard and reduced-motion support.
- [ ] Keep fetch request sequencing safe, clear stale content, allow Back without losing entered passenger details, disable duplicate mutations.
- [ ] Verify JavaScript syntax and inspect all required states before handoff.

## Task 3 — C++ transport, build and runnable package

Owner: root. Files `include/web.hpp`, `src/web.cpp`, `src/main.cpp`, `tests/web_tests.cpp`, `CMakeLists.txt`, scripts and vendored headers/licenses.

- [ ] Write transport tests first: public manifest denied, health/static, content validation, same-origin rules, internal flag rejection, public journey contains no personal data, desk cancellation access.
- [ ] Implement request routing, request size/media/origin validation, error mapping, CSP/static whitelist, desk access key and lifecycle.
- [ ] Compile MinGW build; run the complete CTest suite and JavaScript syntax check.
- [ ] Create portable Windows release and Docker build instructions. Keep the old published Python app distinct until replacement deployment is verified.

## Task 4 — Integration, review and delivery

Owner: root with independent reviewer. Files docs, screenshots and submission ZIP.

- [ ] Browser-test empty start, route validation, service results, preferred-seat booking, review/Back, ticket lookup/cancellation, full demo FIFO promotion and reset isolation.
- [ ] Verify desktop and 375px/768px layout, keyboard, print styling and console logs; fix material findings.
- [ ] Independent review against the approved spec and security/edge-case behavior; fix and reverify findings.
- [ ] Write README, report/viva and verification evidence; package source and tested executable with no booking data or keys. Verify extracted ZIP runs/tests. Complete authenticated hosting when available; clearly state any hosting handoff.

No Git repository is present, so do not introduce commits/worktrees purely for this rebuild. The existing Python deliverable remains available for comparison; the new folder is the revised submission.
