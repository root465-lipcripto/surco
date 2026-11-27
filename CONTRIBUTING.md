# Contributing

Run Debug, Release and scalar tests before submitting changes to the codec.
For parser or storage changes, run the sanitizer preset as well. Explain
wire-format effects and provide a regression test where practical. Keep
binary fixtures small and reproducible. Benchmarks should describe the
machine, compiler, dataset, reference path and variability. Use synthetic
recordings to demonstrate changes.

Start with [the test instructions](docs/testing.md) and
[the wire format](docs/format-v1.md).
