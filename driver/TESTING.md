# Spacer.driver — testing

- Automated (CI, Linux + macOS): protocol and receiver tests in
  `tests/vdev/` and `tests/engine/VirtualDeviceTests.cpp`; on macOS also
  `tests/driver/DriverHarnessTests.cpp`, which loads the built bundle
  in-process with a fake AudioServerPlugIn host.
- Manual checklist on a real Mac (coreaudiod, a sender app, the installer,
  listening): [`docs/mac-validation/WP5.md`](../docs/mac-validation/WP5.md).
