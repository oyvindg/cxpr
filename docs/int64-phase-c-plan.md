# cxpr int64 Fase C — parallelle typede vert-API-er

Maalet er aa faa int64 gjennom vertvendte flater uten aa bryte eksisterende
double-konsumenter. Nye typede API-er legges ved siden av dagens ABI.

## Laaste valg

- Behold eksisterende `cxpr_func_ptr`, producer-, history- og resample-API-er.
- Legg til parallelle typede varianter; ingen reinterpret-cast eller skjult ABI-endring.
- Dtype skal vaere eksplisitt ved eksterne kolonner og buffers.
- Eksisterende double-faststi skal ikke faa ekstra indireksjon i hot path.
- Kun `int64` legges til i denne fasen; timestamp/duration vurderes separat.

## Leveransebolker

### C1 — Context-lesesymmetri

- [x] Public `cxpr_context_get_int64` med parent-oppslag.
- [x] Public `cxpr_context_get_param_int64` med parent-oppslag.
- [x] Eksakt test over `2^53`, miss og overlay-shadowing.

### C2 — Typet host-funksjon/produsent-ABI

- [x] C2a: bekreft og dokumenter eksisterende `cxpr_registry_add_typed` som den
  parallelle host-funksjon-ABI-en med `cxpr_value`-argumenter.
- [x] C2a: registrering, typevalidering og AST/IR-dispatch uten endring av legacy
  double-callback.
- [x] C2a: test eksakt int64 argument og retur over `2^53`.
- [x] C2b: legg til parallell typed struct-producer-callback; dagens
  `cxpr_struct_producer_ptr` mottar fortsatt bare `double`.
- [x] C2b: test typed producer via AST og IR. AST-cachen bruker type-tagget,
  eksakt int64-nokkel; IR omgaar legacy double-cache for typed producers.

### C3 — History/lookback

- [x] Legg typet lagring paa parallelle int64 history-/lookback-kolonner via
  `cxpr_history_int64_source`, contiguous-adapter og dynamisk provider.
- [x] Bevar dagens double-layout og ytelse; eksisterende descriptors og callbacks
  er uendret, og int64 registreres som en separat typed index-capability.
- [x] Test eksakt AST- og IR-lookback over strided int64-kolonner over `2^53`,
  inkludert sammensatt history-uttrykk.

`CXPR_HISTORY_BOUNDS_LEGACY_NAN` er bevisst ikke gyldig for int64-kilder: `NaN`
har ingen eksakt int64-representasjon. Bruk error eller clamp-policy.

### C4 — Resample/timeframe

- [x] Legg `cxpr_typed_resample_view` med separate number-/int64-buffere ved
  siden av ABI v1 sin `const double* values`; rene double-modeller beholder
  eksisterende signatur og layout.
- [x] Definer aggregattyping: FIRST/LAST/MIN/MAX/SUM bevarer scalar type,
  COUNT gir int64, og MEAN promoterer til number.
- [x] Test eksakt verdi over `2^53` gjennom AST, IR og generert C, inkludert
  target-series-relativ lookback.

### C5 — Bindinger og ABI-gate

- [x] Eksponer int64 context- og parameter-get/set i managed/P/Invoke-flaten.
- [x] Legg C `dlopen`- og .NET-smoke-test for eksakt roundtrip over `2^53` mot
  delt bibliotek.
- [x] Dokumenter additiv ABI-versjonering og migrering i
  `docs/int64-abi-migration.md`.
