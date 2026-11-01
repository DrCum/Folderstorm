# Cache correctness batch

Base: main `e462dd1d1f`. Plan the two cache fixes independently from URL/map
routing. Firestorm sources `1ecc5eacc12a2397ede1eadcf6fbc1091b1fb6bd`
and `7eb2c061f5f63a7af698ab3de5723dccba9c17ea`.

Folderstorm uses Boost filesystem, not the later upstream std::filesystem /
fsyspath migration. Port the accounting algorithm and native UTF-8 conversions
to existing APIs; preserve prior #66 worker/cache isolation. Do not import the
unrelated filesystem migration, absent preprocessor-path header or native test
framework prerequisites. The existing file-access warnings already log their
UTF-8 string argument, so that upstream warning-format hunk is unnecessary.

Validate protected entries, failed removals, concurrent disappearance, debug
logging parity, Unicode paths and constructor watermarks. Native Windows locked
file and Unicode code-page acceptance must remain explicitly pending if not run.
Check held #160's different lldir.cpp skin-path changes during trial integration.

Implemented the current-API equivalents: protected/failed/missing accounting,
debug counters independent from deletion, wide Boost calls for UTF-8 paths,
UTF-8 file/directory listings and configured constructor thresholds. Asset scan
filters operate on filenames rather than parent directories. Old native logging
already emits its UTF-8 string; the absent preprocessor helper is inapplicable.

Validation: two checks pass. One compiles the actual constructor/purge with real
Unicode temporary paths and bounded Boost-to-std filesystem fixtures, exercising
protected floors, injected deletion failure, scan-to-delete disappearance,
debug parity, unequal asset sizes, exact first-purge thresholds and clamping.
The other checks Windows wide-path boundaries in source. Real-header Linux
syntax passes for lldiskcache.cpp and lldir.cpp. Trial merges with held #160,
#161, #162 and the routing batch are clean; #160 shares lldir.cpp in a different
method. There is no std-filesystem migration or native test-framework change.

Native acceptance: Windows Unicode install/profile/cache paths, locked cache
files, normal/static assets and first startup purge under non-default limits.
Verify logs match retained bytes with debug logging on/off. Actual Boost/Windows
sharing behavior, full viewer linking and runtime performance remain unrun.
