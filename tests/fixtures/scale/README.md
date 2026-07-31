# Host-neutral scale fixture

`large_host_neutral.cxpr` is a standalone CXPR graph extracted from the graph
shapes exercised by Dynasty, without Dynasty headers, callbacks, or trading
vocabulary. It intentionally provides:

- more than 50 named expressions;
- multiple imported helper modules and nested imported-function calls;
- state updates, input and named-result history, and multiple window sites;
- numeric, boolean, and structured output;
- named parameters with deterministic defaults.

The fixture is the shared input for standalone engine/generated-C parity and
benchmark coverage. Keep domain integration assertions in Dynasty instead of
adding host concepts here.
