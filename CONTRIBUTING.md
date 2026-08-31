# Contributing

Open an issue before large architectural changes. Format C++ with the repository `.clang-format`; keep project targets warning-clean; add tests; preserve privacy projections; bind SQL parameters; and never commit `.env`, databases, rendered output, or credentials.

Pull requests should pass:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

