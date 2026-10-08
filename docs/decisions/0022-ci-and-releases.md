# 0022: CI builds three platforms, and a tag makes a release

**Status:** accepted

## Context

The project is to be published on GitHub. Until now it was built only on the owner's Mac, with
Apple clang; the C++23 subset `PortabilityChecks.cpp` pins down was the one guard against GCC
and MSVC. PGMBuilder, the owner's converter, already publishes releases from tags.

## Decision

- `.github/workflows/ci.yml` builds and tests on every push and pull request:
  - macOS arm64 (Apple clang, macOS 13 or later), Linux x64 (GCC 14, Ubuntu 24.04), and Windows
    x64 (MSVC, its runtime linked in);
  - with warnings as errors (`PGM_WERROR`), so that what one compiler warns of is fixed before it
    is merged;
  - `scripts/format.sh --check` with clang-format 23, the version the code is formatted with.
- The tests that need ROMs, and the regression suite's golden frames, skip there: no ROM is part
  of the repository, and none is fetched. They are run where the ROMs are, before a release.
- Each platform's package is what `cmake --install <build> --component pgmemu` holds
  (`cmake/Install.cmake`): the application, `pgmemu-cli`, the licence, the credits and the
  licence of every library linked in. On macOS the application is `PGMEmu.app`, assembled at
  install time so that the build tree keeps its executable where the scripts find it.
- A tag `v*` publishes the three packages as a GitHub release, its notes made from the commits;
  the version is the tag's, as `cmake/Version.cmake` reads it.
- The macOS application is signed for itself (ad hoc), not by a developer: the owner has no
  Developer ID. macOS asks once before opening it; the README says how.

## Consequences

- A change that builds on one platform and not another fails CI before it is merged.
- clang-tidy is not run there; `scripts/tidy.sh` stays a local step.
- Moving to a notarized application needs a Developer ID, and its certificate and password as
  the repository's secrets.
