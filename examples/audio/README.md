# Audio conformance fixtures

This directory is intentionally small. It contains audio-shaped CXPR fixtures
that exercise generic language and runtime capabilities while `libs/audio`
owns the reusable model library, renderer, and complete compositions.

Use [`../audio_dsp`](../audio_dsp/) for the executable IR/generated-C parity
example. It covers stateful oscillators, an envelope, filtering, reset, and the
generated descriptor path used by an audio host.

The fixtures here cover scalar mono and rooted stereo composition without
introducing audio semantics into CXPR. They exist so CXPR can evolve against
realistic requirements from `libs/audio` without depending on that library.
