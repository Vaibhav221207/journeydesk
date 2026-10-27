# Third-party notices

Journey Desk's reservation rules and four data structures are implemented in the project's own C++ source. Two header-only libraries handle HTTP and JSON; their MIT licences are included in `vendor/`.

| Component | Version | Source | Licence file |
|---|---|---|---|
| cpp-httplib | 0.28.0 | https://github.com/yhirose/cpp-httplib/tree/v0.28.0 | `vendor/HTTPLIB_LICENSE.txt` |
| JSON for Modern C++ | 3.12.0 | https://github.com/nlohmann/json/tree/v3.12.0 | `vendor/JSON_LICENSE.txt` |

The JSON header's SHA-256 is `aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`, matching the upstream release. Headers are bundled so normal builds need no network downloads. The SVG illustration is original project artwork. Frontend fonts use installed system fonts; no external assets are fetched.
