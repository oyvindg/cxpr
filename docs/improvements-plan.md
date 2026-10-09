# cxpr — utbedringsplan

Konkret sjekkliste basert på gjennomgangen i `improvements.txt`, verifisert mot
koden på grenen `feature/cxpr-model-language-mvp`. Hvert funn nedenfor er
kontrollert mot faktisk kilde (fil:linje oppgitt). Rekkefølgen er tre avgrensede
PR-er: korrekthet → testpålitelighet → integrasjon/dokumentasjon.

Tester registreres automatisk via `GLOB_RECURSE` i `tests/CMakeLists.txt`, så
nye `assert`-er kan legges i eksisterende `tests/context_values.test.c` uten
CMake-endringer.

---

## PR 1 — Korrekthet (høy prioritet)

### 1.1 `cxpr_context_get(found == NULL)` returnerer alltid 0.0
Bekreftet: `src/context/values.c:337` bruker `if (found && *found)`, som
kortslutter til usann når `found == NULL` — funksjonen returnerer 0.0 selv når
variabelen finnes. Headeren dokumenterer `found` som valgfri.

- [x] Innfør lokal `bool local_found` i `cxpr_context_get`; kall
  `cxpr_context_get_typed(ctx, name, &local_found)` og styr returverdien på
  `local_found`, ikke på `found`-pekeren.
- [x] Kopier `local_found` til `*found` kun når `found != NULL`.
- [x] Regresjonstest i `tests/context_values.test.c`:
  - `set(ctx,"x",42.0)` → `get(ctx,"x",NULL) == 42.0`
  - bool-binding via `get(...,NULL)` gir 1.0/0.0
  - manglende variabel via `get(...,NULL)` gir 0.0

### 1.2 Typebytte etterlater gammel binding (int64 ikke ryddet)
Bekreftet inkonsistente fjerningslister:
- `cxpr_context_set_string` (`values.c:275`) rydder bools/arrays/structs men
  **ikke** `int64s`.
- `cxpr_context_set_param_string` (`values.c:449`) rydder ikke `int64_params`.
- `cxpr_context_set_param_int64` (`values.c:442`) rydder ikke `array_params`
  eller `structs` (mens `set_int64` på `266` gjør begge).

- [x] Lag én intern hjelper, f.eks.
  `cxpr_context_clear_variable_bindings(ctx, name, keep)` /
  `..._param_bindings(...)`, som fjerner **alle** andre typebindinger for navnet.
- [x] Rut alle `set_*`/`set_param_*`-settere gjennom hjelperen så listene ikke
  lenger vedlikeholdes per setter.
- [x] Verifiser at `set_value`/`set_param_value` (`values.c:283`/`457`) bruker
  samme opprydding (i dag delvis dupliserte `remove`-kall).
- [x] Tabellstyrt regresjonstest: for hvert par (A, B) av
  {number, bool, int64, string, array, struct} — sett A, sett B, les tilbake →
  forvent B sin type/verdi og at `get_typed` ikke returnerer A.

### 1.3 Numerisk oppslag kan lekke kopierte array-/struct-verdier
Påstand: `cxpr_context_get_typed` lager eide kopier av array/struct;
`cxpr_context_get` frigjør dem ikke når den avviser typen (`values.c:336-344`
returnerer 0.0 uten å frigjøre `typed`).

- [x] Verifiser eierskapet til `cxpr_value` returnert fra `cxpr_context_get_typed`
  (lånt vs. eid) i `src/context/`.
- [x] Hvis eid: frigjør `typed` i `cxpr_context_get` på alle grener som avviser
  ARRAY/STRUCT før retur.
- [x] LeakSanitizer-test: numerisk `get` på en array-binding og på en
  struct-binding skal ikke lekke.
- [x] Dokumentér skillet lånt/eid/typekonvertert i header-kommentarene for
  `cxpr_context_get*`.

### 1.4 Sanitizer instrumenterer ikke bibliotekskoden
Bekreftet: `CMakeLists.txt:78/80` oppretter `add_library(cxpr …)` **før**
`add_compile_options(${CXPR_SANITIZER_FLAGS})` på linje 130. `add_compile_options`
er katalog-scoped og treffer bare targets opprettet etterpå → `cxpr`-kildene
bygges uten `-fsanitize=…`, mens testene får det. (Coverage-flagget på linje 59
er OK — det ligger før `add_library`.)

- [x] Bytt `add_compile_options(${CXPR_SANITIZER_FLAGS})` til eksplisitt
  `target_compile_options(cxpr PRIVATE ${CXPR_SANITIZER_FLAGS})` **og** tilsvarende
  `target_link_options`, eller flytt sanitizer-oppsettet før `add_library`.
- [x] Bekreft at både `src/`-kilder og testtargets får flagget (se 2.4).

---

## PR 2 — Testenes pålitelighet (høy / middels-høy)

### 2.1 Aktiver lekkasjedeteksjon i egen jobb
Bekreftet: `CMakePresets.json:120` setter `ASAN_OPTIONS=detect_leaks=0`. Sjekk om
testegenskapene også setter det (må endres begge steder).

- [x] Legg til CI-jobb / preset som kjører ASan **med** LeakSan aktiv.
- [x] Fjern `detect_leaks=0` der det ikke trengs; behold kun smale, dokumenterte
  unntak (suppression-fil) om nødvendig.

### 2.2 La UBSan-feil stoppe testen
- [x] Sett `UBSAN_OPTIONS=halt_on_error=1` (eller
  `-fno-sanitize-recover=undefined`) også i den ordinære UBSan-jobben, ikke bare
  i fuzz-jobben.

### 2.3 Fuzzer trenger coverage inne i kjernen
Påstand: `tests/fuzz/CMakeLists.txt` instrumenterer driveren, men ikke
`cxpr`-biblioteket → ingen grentilbakemelding fra parser/evaluator.

- [x] Bygg en egen instrumentert variant av kjernen med
  `-fsanitize=fuzzer-no-link,address,undefined` og link fuzz-eksekverbaren mot
  libFuzzer-runtime.
- [x] Differensiell testing i fuzz-driveren: samme uttrykk + input gjennom AST og
  IR → sammenlign resultat, type og feilkode; lik separat initialtilstand for
  stateful kjøring.

### 2.4 Coverage-kontroll kan godkjenne manglende data
Bekreftet: `scripts/check_function_coverage.sh:35` hopper over kilder uten
`.gcno`. Påstått: tom byggkatalog gir exit 0 med «0 function(s) executed».

- [x] Reproduser exit-0-tilfellet; la skriptet **feile** når (a) forventede
  `.gcno` mangler, (b) `gcov` feiler, eller (c) antall undersøkte funksjoner er 0.
- [x] Bruk `filsti + funksjonsnavn` som identitet (ikke bare navn — to `static`
  med samme navn kolliderer).
- [x] Definer eksplisitt hvilke kompilerte kilder kontrollen omfatter (i dag kun
  `src/`).
- [x] Legg til CI-sjekk av `compile_commands.json` som bekrefter at filer under
  `src/` faktisk instrumenteres (fanger 1.4 permanent).
- [ ] (Oppfølging) suppler funksjonsdekning med grendekning for feilhåndtering.

---

## PR 3 — Integrasjon og dokumentasjon (middels)

### 3.1 ABI-dokumentasjon matcher ikke generert header
Bekreftet mismatch i samme commit:
- `include/cxpr/generated.h:18` → `CXPR_GENERATED_MODEL_ABI_VERSION 5u`
- `docs/api/generated-c.md:10` → «ABI == 4», og linje 15 sier «Numbers and
  booleans cross this ABI as `double`».

- [x] Oppdater `docs/api/generated-c.md` til ABI 5 og korrekt scalar-transport
  (`cxpr_value`-arrayer, ikke `double`).
- [x] Oppdater README-eksemplet for generert C tilsvarende (sender i dag
  `double`-arrayer til `model->tick()`).
- [x] Oppdater migrasjonsveiledningen (ABI 4 → 5) samlet.
- [x] Gjør presenterte eksempler kjørbare: legg dem i kildefiler som bygges/testes
  i CI og gjenbruk i doc. Akseptansekriterium: alle «kjørbare» eksempler
  kompilerer mot headerne i samme commit uten type-inkompatibilitetsadvarsler.

### 3.2 CMake-minimumsversjon er feil
Bekreftet: `CMakeLists.txt:1` sier `VERSION 3.20`, men koden bruker
`PROJECT_IS_TOP_LEVEL` (`:31`) og `CMakePresets.json:2` har `"version": 3` — begge
krever 3.21.

- [x] Hev `cmake_minimum_required` til 3.21 i byggfiler og dokumentasjon.
- [x] La den oppgitte minimumsversjonen inngå i CI (bygg med 3.21-toolchain).

### 3.3 Test kombinasjoner av byggevalg
Påstått problematisk: `-DCXPR_BUILD_TOOLS=OFF -DCXPR_BUILD_TESTS=ON` — tester
globbes inn, mens enkelte `*.gen.c` genereres bak `if(TARGET cxpr_model_codegen)`;
`dynamic_host_e2e.test.c` inkluderer `dynamic_host.gen.c` ubetinget.

- [x] Reproduser `build-no-tools`-kombinasjonen mot hele repoet.
- [x] Registrer generatoravhengige tester betinget, eller skill interne
  testgeneratorer fra `CXPR_BUILD_TOOLS`.
- [x] Legg kombinasjonen inn i CI-matrisen.

### 3.4 Utvid CI til plattform-/lenkematrise
- [x] Legg til Linux med både statisk og delt bibliotek (delt bygg har egne
  P/Invoke-smoke-tester som ikke dekkes i dag).
- [ ] Vurder Windows/MSVC og macOS om disse er tilsiktede støtteplattformer.
- [x] Behold eksisterende installasjonstest.

---

## Ferdigkriterier

| PR | Innhold | Ferdig når |
|----|---------|-----------|
| 1  | Context-buggene (1.1–1.3) + sanitizer-instrumentering (1.4) | Regresjonstester dekker funnene, og `src/`-koden instrumenteres |
| 2  | Fuzz-instrumentering, lekkasjedeteksjon, UBSan-stopp, coverage-kontroll | Manglende data og faktiske feil gir rødt bygg |
| 3  | ABI-doc, kjørbare eksempler, CMake-minimum, byggvarianter, plattformmatrise | Dokumentert bruk kompilerer mot samme commit |
