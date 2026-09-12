# Independent cross-endpoint fixtures

The 41 fixed `.bin` files and manifest came from the completed protocol review task
`01a097b7-5bc5-7833-bb99-92a881a685ba`. They were generated separately from firmware and
manager production codecs, then validated against real production C and Mbed TLS.
The frozen manifest SHA-256 is `ac52520a59c8275d35c356c88914915f447f6fda08f9e08e6e29b7cd285102c5`.
All 41 file hashes were checked when copied and are checked before each test run.

`review_vectors.c` is the unmodified frozen C runner, SHA-256 (LF source)
`95a8d9b9259bd16df4270924362b6f914a953ba5cce3f65ad3baa24ce920b238`.
Integration supplies a small CMake entry linking the existing `configuration_model` and
`management_frame_host` targets; it does not import alternate model, framing, or crypto code.
`verify_c.py` came from the same frozen runner and adds only file-length/SHA validation here.
Results are written to `cross_endpoint/c_results.json` in the active host build directory.
The root test runners include this suite as `cross_endpoint_vectors`.

The separate runner package manifest SHA-256 was
`c90dc09d267ee4ee03da7b6d1cb135162f73253d5488c229f65cf6843218f34b`.
Its standalone CMake/platform adapter copies were deliberately unnecessary in the integrated
build, which uses the common real-library targets.

Coverage includes 48-byte defaults, a valid 21-byte minimum, 1/255/256-byte credentials,
8475-byte maximum configuration, 8492-byte PUT, 8494-byte GET, schema rejection, 257-byte
fields, invalid ASCII/control/NUL cases, sentinels, spare bytes, short encode buffers,
11 fragment sizes, glued frames, bad CRC and oversized-header recovery.
The 5000 ms point-timeout and 60000 ms RTU-timeout files must remain rejected: both existing
firmware limits are 50..3000 ms. They exposed a real manager validation mismatch during review.

This maximum fixture uses a self-signed RSA-2048/SHA-256 CA accepted by firmware Mbed TLS.
It differs intentionally from the core suite's independently constructed maximum fixture.
The earlier manager Ed25519 maximum was superseded; see the final integration report.
No serial device or broker is accessed by this suite.
