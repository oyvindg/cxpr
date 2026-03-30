# IR / Compiled Plan TODO

Mål: innføre en liten intern IR eller "compiled plan" for raskere evaluering, uten å endre hva `cxpr` er ment å være. AST skal fortsatt være primær representasjon for parsing, analyse, introspeksjon og feilmeldinger.

## Arbeidsregel

- [ ] Hvert punkt krysses av først når det faktisk er ferdig.
- [ ] Ingen nytt implementasjonssteg startes før testpunktet i forrige steg er kjørt og bestått.
- [ ] Hvis et steg viser seg for stort til å testes meningsfullt, skal det deles i mindre steg før arbeidet fortsetter.

## Sekvensiell TODO

### 1. Kartlegg dagens semantikk som må bevares

- [x] Les [src/eval.c](/home/evoldoc/_code/cxpr/src/eval.c) og skriv ned hvilke regler IR-evaluering må matche:
  - [x] boolean-konvensjon
    Dagens evaluator bruker `1.0` og `0.0` for logiske resultater. `cxpr_ast_eval_bool(...)` tolker alle ikke-null-resultater som `true`.
  - [x] division/modulo by zero
    Ved `/ 0.0` settes `CXPR_ERR_DIVISION_BY_ZERO` med melding `"Division by zero"` og resultatet blir `NAN`. Ved `% 0.0` brukes samme feilkode med melding `"Modulo by zero"` og resultatet blir `NAN`.
  - [x] unknown identifier / unknown function
    Ukjent identifier gir `CXPR_ERR_UNKNOWN_IDENTIFIER` med melding `"Unknown identifier"`. Ukjent `$param` gir samme feilkode med melding `"Unknown parameter variable"`. Ukjent field access gir samme feilkode med melding `"Unknown field access"`. Ukjent funksjon gir `CXPR_ERR_UNKNOWN_FUNCTION` med melding `"Unknown function"`.
  - [x] short-circuit for `and` og `or`
    `and` evaluerer høyresiden bare hvis venstresiden er ikke-null. `or` evaluerer høyresiden bare hvis venstresiden er null. Resultatet normaliseres til `1.0` eller `0.0`.
  - [x] ternary-semantikk
    Kun condition evalueres først. Hvis condition er ikke-null, evalueres bare true-grenen. Ellers evalueres bare false-grenen.
  - [x] funksjonskall, inkludert struct-aware og expression-defined functions
    Vanlige funksjoner validerer arity mot registry før kall. Argumenter evalueres venstre-til-høyre. Struct-aware functions krever at argumentene er identifiers og slår opp felter i context. Expression-defined functions kloner context, binder parametre og evaluerer definert body derfra.
- [x] Skriv kort beslutning i denne filen om at AST fortsatt er source of truth, og IR kun er runtime-optimalisering.
  Beslutning: AST beholdes som source of truth for parsing, validering, referanseuttrekk, dependency-analyse og feilmeldinger. IR er kun en intern runtime-optimalisering for raskere evaluering av allerede validerte uttrykk.
- [x] Test før neste steg:
  - [x] kjør eksisterende tester som baseline
  - [x] bekreft at repoet er grønt før IR-arbeid starter
  Baseline verifisert med `/usr/bin/ctest --test-dir build --verbose`: 10 av 10 tester passerte.

### 2. Bestem minimal v1-scope

- [x] Definer at første IR-versjon kun skal støtte:
  - [x] tall
  - [x] identifiers
  - [x] `$params`
  - [x] `+`
  - [x] `-`
  - [x] `*`
  - [x] `/`
  V1 skal være bevisst liten: nok til å bevise arkitektur, semantikk og testoppsett før mer komplekse noder legges til.
- [x] Definer hva som eksplisitt utsettes:
  - [x] field access
  - [x] comparisons
  - [x] logical operators
  - [x] ternary
  - [x] function calls
  - [x] `FormulaEngine`-integrasjon
  - [x] public API
  Alt dette utsettes for å holde første leveranse liten nok til at AST->IR-compiler og IR-evaluator kan verifiseres i isolasjon.
- [x] Velg representasjon for v1:
  - [x] stack-basert instruksjonsliste
  V1 bruker en enkel lineær instruksjonsliste med stack-maskin-semantikk. Det er den minst invasive modellen for dagens evaluator og krever ikke nytt typesystem eller kompleks registerallokering.
- [x] Test før neste steg:
  - [x] verifiser i planen at alle v1-elementer kan testes isolert
  Verifisering: alle v1-elementer kan testes med små, uavhengige uttrykk og direkte sammenligning mellom AST-eval og IR-eval:
  - tall: `42`
  - identifier: `price`
  - `$param`: `$threshold`
  - add/sub: `a + b - c`
  - mul/div: `a * b / c`
  - blandet precedens via parser: `a + b * c`

### 3. Legg til interne IR-typer

- [x] Oppdater [src/internal.h](/home/evoldoc/_code/cxpr/src/internal.h) med:
  - [x] opcode enum for v1
  - [x] instruksjonsstruktur
  - [x] intern program/plan-struktur
- [x] Hold typene interne; ikke legg til public API ennå.
- [x] Test før neste steg:
  - [x] bygg prosjektet
  - [x] bekreft at eksisterende tester fortsatt passerer
  Verifisering: `cmake --build build` og `/usr/bin/ctest --test-dir build --verbose` passerte etter innføring av interne IR-typer.

### 4. Opprett minimal IR-modul

- [x] Opprett [src/ir.c](/home/evoldoc/_code/cxpr/src/ir.c).
- [x] Legg inn tomme eller minimale interne funksjoner for:
  - [x] compile AST -> IR
  - [x] eval IR
  - [x] free IR
- [x] Koble filen inn i [CMakeLists.txt](/home/evoldoc/_code/cxpr/CMakeLists.txt) uten å endre eksisterende public API.
- [x] Test før neste steg:
  - [x] bygg prosjektet
  - [x] kjør testene og bekreft at bare wiring er innført, uten regresjon
  Verifisering: `cmake --build build` og `/usr/bin/ctest --test-dir build --verbose` passerte etter at `src/ir.c` ble lagt til og koblet inn.

### 5. Implementer compiler for tall

- [x] Emit `PUSH_CONST` for number-noder.
- [x] Emit `RETURN` for ferdig program.
- [x] Håndter compile-feil med `cxpr_error` der det er relevant.
- [x] Test før neste steg:
  - [x] legg til [tests/ir.test.c](/home/evoldoc/_code/cxpr/tests/ir.test.c)
  - [x] test at et rent talluttrykk kompileres
  - [x] test at IR-eval av talluttrykk matcher AST-eval
  Verifisering: `cmake --build build`, direkte kjøring av `./build/tests/test_ir`, og `/usr/bin/ctest --test-dir build --verbose` passerte. Testsettet er nå 11 av 11 grønt.

### 6. Implementer compiler for identifiers

- [x] Emit `LOAD_VAR` for `CXPR_NODE_IDENTIFIER`.
- [x] Sørg for at runtime-oppslag følger samme feiloppførsel som i dagens evaluator.
- [x] Test før neste steg:
  - [x] test gyldig identifier-oppslag
  - [x] test ukjent identifier
  - [x] test at IR-eval matcher AST-eval
  Verifisering: `cmake --build build`, `./build/tests/test_ir`, og `/usr/bin/ctest --test-dir build --verbose` passerte. IR-testene dekker nå compile/eval for identifier og feilen `CXPR_ERR_UNKNOWN_IDENTIFIER`.

### 7. Implementer compiler for `$params`

- [x] Emit `LOAD_PARAM` for `CXPR_NODE_VARIABLE`.
- [x] Sørg for samme feiloppførsel som i dagens evaluator.
- [x] Test før neste steg:
  - [x] test gyldig parameteroppslag
  - [x] test ukjent parameter
  - [x] test at IR-eval matcher AST-eval
  Verifisering: `cmake --build build`, direkte kjøring av `./build/tests/test_ir`, og `/usr/bin/ctest --test-dir build --verbose` passerte. IR-testene dekker nå compile/eval for `$params` og feilen `CXPR_ERR_UNKNOWN_IDENTIFIER` med meldingen `"Unknown parameter variable"`.

### 8. Implementer compiler for `+` og `-`

- [x] Støtt binær `+`
- [x] Støtt binær `-`
- [x] Støtt unary `-` hvis den trengs for v1-uttrykk, ellers flytt den til eget steg
- [x] Test før neste steg:
  - [x] test enkel addisjon og subtraksjon
  - [x] test nestede uttrykk
  - [x] test AST-eval vs IR-eval
  Verifisering: `cmake --build build`, direkte kjøring av `./build/tests/test_ir`, og `/usr/bin/ctest --test-dir build --verbose` passerte. IR-testene dekker nå enkel addisjon, nestet `a + b - $c`, og unary `-price` med likt resultat som AST-eval.

### 9. Implementer compiler for `*` og `/`

- [x] Støtt binær `*`
- [x] Støtt binær `/`
- [x] Sørg for samme division-by-zero-feil som i dagens evaluator.
- [x] Test før neste steg:
  - [x] test multiplikasjon og divisjon
  - [x] test operatorprecedens via AST som allerede parser korrekt
  - [x] test division-by-zero
  - [x] test AST-eval vs IR-eval
  Verifisering: `cmake --build build`, direkte kjøring av `./build/tests/test_ir`, og `/usr/bin/ctest --test-dir build --verbose` passerte. IR-testene dekker nå `6 * 7`, `a + b * c / $d` og division-by-zero med `CXPR_ERR_DIVISION_BY_ZERO`.

### 10. Implementer minimal IR-evaluator

- [x] Kjør instruksjoner lineært med en liten stack.
- [x] Implementer støtte for:
  - [x] `PUSH_CONST`
  - [x] `LOAD_VAR`
  - [x] `LOAD_PARAM`
  - [x] `ADD`
  - [x] `SUB`
  - [x] `MUL`
  - [x] `DIV`
  - [x] `RETURN`
- [x] Sørg for samme resultat og feilmodell som AST-eval for v1-scope.
- [x] Test før neste steg:
  - [x] kjør dedikert IR-testsuite
  - [x] sammenlign AST og IR på et lite sett representative uttrykk
  Verifisering: `./build/tests/test_ir` og `/usr/bin/ctest --test-dir build --verbose` passerte med representative uttrykk for konstanter, identifiers, `$params`, `+`, `-`, `*`, `/`, unary `-` og division-by-zero.

### 11. Rydd opp og stabiliser intern API

- [x] Gå gjennom [src/ir.c](/home/evoldoc/_code/cxpr/src/ir.c) og [src/internal.h](/home/evoldoc/_code/cxpr/src/internal.h).
- [x] Fjern åpenbare hull, duplisering og dårlig navngivning.
- [x] Bekreft at minnehåndtering er tydelig og fri-funksjoner er på plass.
- [x] Test før neste steg:
  - [x] bygg
  - [x] kjør alle tester
  Verifisering: `cmake --build build`, `./build/tests/test_ir`, og `/usr/bin/ctest --test-dir build --verbose` passerte etter at stack- og feilhåndtering i `src/ir.c` ble ryddet opp i egne hjelpefunksjoner.

### 12. Utvid v1 med field access

- [x] Legg til `LOAD_FIELD` eller tilsvarende støtte for flat key-lookup.
- [x] Bruk samme semantikk som dagens `field access`.
- [x] Test før neste steg:
  - [x] test gyldig field access
  - [x] test ukjent field access
  - [x] test AST-eval vs IR-eval
  Verifisering: `cmake --build build`, direkte kjøring av `./build/tests/test_ir`, og `/usr/bin/ctest --test-dir build --verbose` passerte. IR-testene dekker `body.vx` og ukjent field access med meldingen `"Unknown field access"`.

### 13. Utvid med comparisons

- [x] Legg til opcodes og compiler-støtte for:
  - [x] `==`
  - [x] `!=`
  - [x] `<`
  - [x] `<=`
  - [x] `>`
  - [x] `>=`
- [x] Test før neste steg:
  - [x] test hver sammenligning
  - [x] test AST-eval vs IR-eval
  Verifisering: `cmake --build build`, direkte kjøring av `./build/tests/test_ir`, og `/usr/bin/ctest --test-dir build --verbose` passerte. IR-testene dekker `==`, `!=`, `<`, `<=`, `>`, `>=` og matcher AST-resultater.

### 14. Utvid med logiske operatorer

- [x] Legg til støtte for `not`
- [x] Legg til støtte for `and`
- [x] Legg til støtte for `or`
- [x] Bevar short-circuit-semantikk.
- [x] Test før neste steg:
  - [x] regression-test for short-circuit
  - [x] test AST-eval vs IR-eval
  Verifisering: `cmake --build build`, direkte kjøring av `./build/tests/test_ir`, og `/usr/bin/ctest --test-dir build --verbose` passerte. IR-testene dekker `not`, `and` short-circuit og `or` short-circuit uten at høyresiden evalueres unødvendig.

### 15. Utvid med ternary

- [x] Implementer nødvendig jump- eller select-mekanisme.
- [x] Sørg for at bare korrekt gren evalueres.
- [x] Test før neste steg:
  - [x] test enkle ternary-uttrykk
  - [x] test nestede ternary-uttrykk
  - [x] test AST-eval vs IR-eval
  Verifisering: `cmake --build build`, direkte kjøring av `./build/tests/test_ir`, og `/usr/bin/ctest --test-dir build --verbose` passerte. IR-testene dekker både `flag ? 10 : 20` og et nestet uttrykk med aritmetikk i grenene.

### 16. Utvid med vanlige funksjonskall

- [x] Legg til `CALL_FUNC` eller tilsvarende.
- [x] Gjenbruk registry-validering der det er mulig.
- [x] Start med vanlige registry-funksjoner.
- [x] Test før neste steg:
  - [x] test builtins som `sqrt`, `abs`, `pow`
  - [x] test arity-feil
  - [x] test unknown function
  - [x] test AST-eval vs IR-eval
  Verifisering: funksjonskall støttes nå via en bevisst AST-fallback-instruksjon i IR-evaluatoren. `./build/tests/test_ir` og `/usr/bin/ctest --test-dir build --verbose` passerte, inkludert builtin-funksjoner.

### 17. Utvid med struct-aware functions

- [x] Sørg for at IR-path støtter samme oppførsel som dagens evaluator for struct-aware functions.
- [x] Hvis dette blir for komplekst i første omgang, bruk eksplisitt fallback til eksisterende AST-path og dokumenter det.
- [x] Test før neste steg:
  - [x] test minst én struct-aware function
  - [x] test AST-eval vs IR-eval
  Verifisering: `distance3(goal, pose)` testes i IR via fallback til eksisterende evaluator og matcher AST-resultatet.

### 18. Utvid med expression-defined functions

- [x] Bestem om v1 skal:
  - [x] bruke fallback til AST for disse
  - [ ] eller kompilere dem videre til egne interne programmer
- [x] Implementer enkleste forsvarlige løsning først.
- [x] Test før neste steg:
  - [x] test minst én defined function
  - [x] test AST-eval vs IR-eval
  Verifisering: en `sum2(a, b) => a + b`-funksjon definert via `cxpr_registry_define_fn(...)` evalueres likt via AST og IR-fallback.

### 19. Legg til enkel konstantfolding

- [x] Implementer lavrisiko konstantfolding før IR-kompilering.
- [x] Ikke legg til avansert optimizer.
- [x] Test før neste steg:
  - [x] test at konstantuttrykk fortsatt gir samme resultat
  - [x] test at optimaliseringen ikke endrer feilsemantikk
  Verifisering: `./build/tests/test_ir` og `/usr/bin/ctest --test-dir build --verbose` passerte. IR-testene bekrefter både at `2 + 3 * 4` foldes til ett `PUSH_CONST`-steg, og at `10 / 0` fortsatt feiler ved runtime med samme `CXPR_ERR_DIVISION_BY_ZERO`.

### 20. Legg til public API

- [x] Oppdater [include/cxpr/cxpr.h](/home/evoldoc/_code/cxpr/include/cxpr/cxpr.h) med ny opaque type:
  - [x] `cxpr_program`
- [x] Legg til public API for:
  - [x] compile
  - [x] eval
  - [x] eval_bool
  - [x] free
- [x] Ikke endre eller deprekér `cxpr_ast_eval(...)`.
- [x] Test før neste steg:
  - [x] legg til public API-tester
  - [x] bekreft at eksisterende API fortsatt oppfører seg likt
  Verifisering: `./build/tests/test_program` og `/usr/bin/ctest --test-dir build --verbose` passerte. Public API-testene bruker kun `cxpr.h` og bekrefter compile/eval/eval_bool/free.

### 21. Integrer forsiktig med FormulaEngine

- [x] Vurder å kompilere hver formel én gang under `cxpr_formula_compile(...)`.
- [x] Behold AST for dependency-analyse og evalueringsrekkefølge.
- [x] Bytt bare evalueringsmekanisme internt.
- [x] Test før neste steg:
  - [x] kjør eksisterende formula-tester
  - [x] legg til minst én test som sammenligner AST-path og IR-path i formula-engine
  Verifisering: `./build/tests/test_formula_ir` og `/usr/bin/ctest --test-dir build --verbose` passerte. `FormulaEngine` beholder AST for dependency-analyse, kompilerer hver formel én gang under `cxpr_formula_compile(...)`, og evaluerer via `cxpr_ir_eval(...)` når compiled program finnes.

### 22. Dokumenter arkitekturen

- [x] Oppdater [README.md](/home/evoldoc/_code/cxpr/README.md) med kort forklaring av:
  - [x] AST sin rolle
  - [x] IR/compiled plan sin rolle
  - [x] at dette er en ytelsesoptimalisering, ikke nytt språk eller nytt scope
- [x] Test før neste steg:
  - [x] les dokumentasjonen mot faktisk kode og API
  Verifisering: README er oppdatert med egen seksjon for execution model, compiled program API og FormulaEngine-integrasjon. Teksten matcher public API i `cxpr.h` og dagens implementasjon i `src/ir.c` og `src/formula.c`.

### 23. Mål effekt før videre utvidelser

- [x] Lag en liten benchmark for:
  - [x] AST-eval
  - [x] IR-eval
- [x] Mål minst:
  - [x] enkel aritmetikk
  - [x] nestede uttrykk
  - [x] funksjonskall
  - [x] mange repetisjoner over nye contexts
- [x] Dokumenter resultatet.
- [x] Test før avslutning:
  - [x] benchmarken kan kjøres
  - [x] resultatene er forståelige og lagret i repoet eller dokumentert i plan/README
  Verifisering: benchmarken kan bygges som `./build/benchmarks/cxpr_bench_ir` og validerer nå AST vs IR per iterasjon, ikke bare på totalsum. Resultater fra en senere lokal kjøring etter inline/substitusjon, builtin-spesialisering og fused square-loads:
  - `simple_arith`: AST `115.07 ns/eval`, IR `97.63 ns/eval`, speedup `1.18x`
  - `nested_expr`: AST `173.76 ns/eval`, IR `153.09 ns/eval`, speedup `1.14x`
  - `function_call`: AST `362.91 ns/eval`, IR `140.75 ns/eval`, speedup `2.58x`
  - `defined_fn`: AST `814.11 ns/eval`, IR `134.82 ns/eval`, speedup `6.04x`
  - `native_fn`: AST `522.54 ns/eval`, IR `120.84 ns/eval`, speedup `4.32x`
  - `defined_chain`: AST `873.38 ns/eval`, IR `171.71 ns/eval`, speedup `5.09x`
  - `native_chain`: AST `594.22 ns/eval`, IR `151.69 ns/eval`, speedup `3.92x`
  - `mixed_chain`: AST `803.10 ns/eval`, IR `171.12 ns/eval`, speedup `4.69x`
  - `deep_defined`: AST `711.08 ns/eval`, IR `186.06 ns/eval`, speedup `3.82x`
  - `deep_native`: AST `503.42 ns/eval`, IR `222.95 ns/eval`, speedup `2.26x`
  - `context_churn`: AST `333.78 ns/eval`, IR `273.40 ns/eval`, speedup `1.22x`
  Tolkning: IR-pathen gir nå tydelig gevinst også for nested expression-defined og native scalar callbacks. I de beste tilfellene kan `defined_fn` fortsatt bli raskere enn tilsvarende native callback-path, fordi compile-time inlining og mønstergjenkjenning kan eliminere mer dispatch enn det som er mulig for en generell callback. Samtidig er native fast-pathen nå betydelig forbedret uten nye offentlige API-er.

## Ikke-mål

- [x] `cxpr` skal ikke bli et generelt numerikkbibliotek.
- [x] `cxpr` skal ikke eie simuleringstidssteg, treningssløyfer eller datasett.
- [x] `cxpr` skal ikke introdusere arrays, tensorer eller komplekst runtime-typesystem.
