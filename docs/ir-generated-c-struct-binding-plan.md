# Generert C — identifier-root struct-binding og signal-helper-dekning

## Mål

Flytt neste ytelsesrunde fra interpreter-mikrooptimalisering til generert C.
Første mål er `project(vector).sum` og signal-helper-kall som mangler C-dekning.

## Bruksavklaring (2026-09-17)

`project(vector).sum`-formen brukes ikke i de faktiske strategi- eller
indikator-kildene. Den kan derfor ikke alene begrunne et codegen-prosjekt, selv
om mikrobenchmark-gapet er ca. 321x.

Produksjons- og optimizerbanene kompilerer strategier til generert C/plugin via
`dyn_strategy_runtime_compile_and_publish`; optimizer faller først tilbake til
denne banen når CUDA ikke brukes. Den relevante neste oppgaven er derfor et
**coverage-kart**, ikke hot-path-tuning: finn konkrete, brukte
`indicator(...).field`-/funksjonsformer som C-backenden avviser eller senker til
fused/interpreter-fallback. Uten en slik produksjonsforekomst er estimert 321x
ikke realiserbar verdi.

## Fase 0 — coverage-kart

- [ ] Behold `struct_binding` som coverage-fixtur, men ikke som bevis på
  produksjonsgevinst.
- [ ] Kartlegg hvorfor identifier-root struct-argument ikke senkes til flate
  inputverdier i model-codegen.
- [ ] Kartlegg hvilke signal-helper-former som fortsatt går via runtime.
- [ ] Registrer for hver reell strategi om utførelsen er direkte generert C,
  fused fallback eller interpreter, og prioriter bare observerte fallbacks.

## Fase 1 — minimal senking

- [ ] Senk kjent `param.field` til direkte input/slot-lesing ved kallstedet.
- [ ] Bevar dynamisk `.` før `_`; generert C bruker compile-time-resolverte input.
- [ ] Legg AST↔IR↔C-paritet for manglende felt, `0.0` og blandede argumenter.

## Fase 2 — beslutningsport

- [ ] Mål 9-trial median for `struct_binding`, `struct_call` og
  `cxpr_signal_helpers`.
- [ ] Behold kun ved robust gevinst i reelle model-fixtures uten semantisk avvik.

Baseline: `struct_binding` 1439,20 ns/op uten cache; eksisterende generert
`struct_call` ca. 2,96 ns/op.

**Go/no-go:** fortsett bare dersom coverage-kartet finner en brukt form som
faktisk går tolket/fused i produksjon eller søk. Hvis alle brukte former allerede
er direkte generert C, lukk planen uten implementasjon.
