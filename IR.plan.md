# IR / Compiled Plan TODO

Mål: innføre en liten intern IR eller "compiled plan" for raskere evaluering, uten å endre hva `cxpr` er ment å være. AST skal fortsatt være primær representasjon for parsing, analyse, introspeksjon og feilmeldinger.

## Arbeidsregel

- [ ] Hvert punkt krysses av først når det faktisk er ferdig.
- [ ] Ingen nytt implementasjonssteg startes før testpunktet i forrige steg er kjørt og bestått.
- [ ] Hvis et steg viser seg for stort til å testes meningsfullt, skal det deles i mindre steg før arbeidet fortsetter.

## Sekvensiell TODO

### 1. Kartlegg dagens semantikk som må bevares

- [ ] Les [src/eval.c](/home/evoldoc/_code/cxpr/src/eval.c) og skriv ned hvilke regler IR-evaluering må matche:
  - [ ] boolean-konvensjon
  - [ ] division/modulo by zero
  - [ ] unknown identifier / unknown function
  - [ ] short-circuit for `and` og `or`
  - [ ] ternary-semantikk
  - [ ] funksjonskall, inkludert struct-aware og expression-defined functions
- [ ] Skriv kort beslutning i denne filen om at AST fortsatt er source of truth, og IR kun er runtime-optimalisering.
- [ ] Test før neste steg:
  - [ ] kjør eksisterende tester som baseline
  - [ ] bekreft at repoet er grønt før IR-arbeid starter

### 2. Bestem minimal v1-scope

- [ ] Definer at første IR-versjon kun skal støtte:
  - [ ] tall
  - [ ] identifiers
  - [ ] `$params`
  - [ ] `+`
  - [ ] `-`
  - [ ] `*`
  - [ ] `/`
- [ ] Definer hva som eksplisitt utsettes:
  - [ ] field access
  - [ ] comparisons
  - [ ] logical operators
  - [ ] ternary
  - [ ] function calls
  - [ ] `FormulaEngine`-integrasjon
  - [ ] public API
- [ ] Velg representasjon for v1:
  - [ ] stack-basert instruksjonsliste
- [ ] Test før neste steg:
  - [ ] verifiser i planen at alle v1-elementer kan testes isolert

### 3. Legg til interne IR-typer

- [ ] Oppdater [src/internal.h](/home/evoldoc/_code/cxpr/src/internal.h) med:
  - [ ] opcode enum for v1
  - [ ] instruksjonsstruktur
  - [ ] intern program/plan-struktur
- [ ] Hold typene interne; ikke legg til public API ennå.
- [ ] Test før neste steg:
  - [ ] bygg prosjektet
  - [ ] bekreft at eksisterende tester fortsatt passerer

### 4. Opprett minimal IR-modul

- [ ] Opprett [src/ir.c](/home/evoldoc/_code/cxpr/src/ir.c).
- [ ] Legg inn tomme eller minimale interne funksjoner for:
  - [ ] compile AST -> IR
  - [ ] eval IR
  - [ ] free IR
- [ ] Koble filen inn i [CMakeLists.txt](/home/evoldoc/_code/cxpr/CMakeLists.txt) uten å endre eksisterende public API.
- [ ] Test før neste steg:
  - [ ] bygg prosjektet
  - [ ] kjør testene og bekreft at bare wiring er innført, uten regresjon

### 5. Implementer compiler for tall

- [ ] Emit `PUSH_CONST` for number-noder.
- [ ] Emit `RETURN` for ferdig program.
- [ ] Håndter compile-feil med `cxpr_error` der det er relevant.
- [ ] Test før neste steg:
  - [ ] legg til [tests/ir.test.c](/home/evoldoc/_code/cxpr/tests/ir.test.c)
  - [ ] test at et rent talluttrykk kompileres
  - [ ] test at IR-eval av talluttrykk matcher AST-eval

### 6. Implementer compiler for identifiers

- [ ] Emit `LOAD_VAR` for `CXPR_NODE_IDENTIFIER`.
- [ ] Sørg for at runtime-oppslag følger samme feiloppførsel som i dagens evaluator.
- [ ] Test før neste steg:
  - [ ] test gyldig identifier-oppslag
  - [ ] test ukjent identifier
  - [ ] test at IR-eval matcher AST-eval

### 7. Implementer compiler for `$params`

- [ ] Emit `LOAD_PARAM` for `CXPR_NODE_VARIABLE`.
- [ ] Sørg for samme feiloppførsel som i dagens evaluator.
- [ ] Test før neste steg:
  - [ ] test gyldig parameteroppslag
  - [ ] test ukjent parameter
  - [ ] test at IR-eval matcher AST-eval

### 8. Implementer compiler for `+` og `-`

- [ ] Støtt binær `+`
- [ ] Støtt binær `-`
- [ ] Støtt unary `-` hvis den trengs for v1-uttrykk, ellers flytt den til eget steg
- [ ] Test før neste steg:
  - [ ] test enkel addisjon og subtraksjon
  - [ ] test nestede uttrykk
  - [ ] test AST-eval vs IR-eval

### 9. Implementer compiler for `*` og `/`

- [ ] Støtt binær `*`
- [ ] Støtt binær `/`
- [ ] Sørg for samme division-by-zero-feil som i dagens evaluator.
- [ ] Test før neste steg:
  - [ ] test multiplikasjon og divisjon
  - [ ] test operatorprecedens via AST som allerede parser korrekt
  - [ ] test division-by-zero
  - [ ] test AST-eval vs IR-eval

### 10. Implementer minimal IR-evaluator

- [ ] Kjør instruksjoner lineært med en liten stack.
- [ ] Implementer støtte for:
  - [ ] `PUSH_CONST`
  - [ ] `LOAD_VAR`
  - [ ] `LOAD_PARAM`
  - [ ] `ADD`
  - [ ] `SUB`
  - [ ] `MUL`
  - [ ] `DIV`
  - [ ] `RETURN`
- [ ] Sørg for samme resultat og feilmodell som AST-eval for v1-scope.
- [ ] Test før neste steg:
  - [ ] kjør dedikert IR-testsuite
  - [ ] sammenlign AST og IR på et lite sett representative uttrykk

### 11. Rydd opp og stabiliser intern API

- [ ] Gå gjennom [src/ir.c](/home/evoldoc/_code/cxpr/src/ir.c) og [src/internal.h](/home/evoldoc/_code/cxpr/src/internal.h).
- [ ] Fjern åpenbare hull, duplisering og dårlig navngivning.
- [ ] Bekreft at minnehåndtering er tydelig og fri-funksjoner er på plass.
- [ ] Test før neste steg:
  - [ ] bygg
  - [ ] kjør alle tester

### 12. Utvid v1 med field access

- [ ] Legg til `LOAD_FIELD` eller tilsvarende støtte for flat key-lookup.
- [ ] Bruk samme semantikk som dagens `field access`.
- [ ] Test før neste steg:
  - [ ] test gyldig field access
  - [ ] test ukjent field access
  - [ ] test AST-eval vs IR-eval

### 13. Utvid med comparisons

- [ ] Legg til opcodes og compiler-støtte for:
  - [ ] `==`
  - [ ] `!=`
  - [ ] `<`
  - [ ] `<=`
  - [ ] `>`
  - [ ] `>=`
- [ ] Test før neste steg:
  - [ ] test hver sammenligning
  - [ ] test AST-eval vs IR-eval

### 14. Utvid med logiske operatorer

- [ ] Legg til støtte for `not`
- [ ] Legg til støtte for `and`
- [ ] Legg til støtte for `or`
- [ ] Bevar short-circuit-semantikk.
- [ ] Test før neste steg:
  - [ ] regression-test for short-circuit
  - [ ] test AST-eval vs IR-eval

### 15. Utvid med ternary

- [ ] Implementer nødvendig jump- eller select-mekanisme.
- [ ] Sørg for at bare korrekt gren evalueres.
- [ ] Test før neste steg:
  - [ ] test enkle ternary-uttrykk
  - [ ] test nestede ternary-uttrykk
  - [ ] test AST-eval vs IR-eval

### 16. Utvid med vanlige funksjonskall

- [ ] Legg til `CALL_FUNC` eller tilsvarende.
- [ ] Gjenbruk registry-validering der det er mulig.
- [ ] Start med vanlige registry-funksjoner.
- [ ] Test før neste steg:
  - [ ] test builtins som `sqrt`, `abs`, `pow`
  - [ ] test arity-feil
  - [ ] test unknown function
  - [ ] test AST-eval vs IR-eval

### 17. Utvid med struct-aware functions

- [ ] Sørg for at IR-path støtter samme oppførsel som dagens evaluator for struct-aware functions.
- [ ] Hvis dette blir for komplekst i første omgang, bruk eksplisitt fallback til eksisterende AST-path og dokumenter det.
- [ ] Test før neste steg:
  - [ ] test minst én struct-aware function
  - [ ] test AST-eval vs IR-eval

### 18. Utvid med expression-defined functions

- [ ] Bestem om v1 skal:
  - [ ] bruke fallback til AST for disse
  - [ ] eller kompilere dem videre til egne interne programmer
- [ ] Implementer enkleste forsvarlige løsning først.
- [ ] Test før neste steg:
  - [ ] test minst én defined function
  - [ ] test AST-eval vs IR-eval

### 19. Legg til enkel konstantfolding

- [ ] Implementer lavrisiko konstantfolding før IR-kompilering.
- [ ] Ikke legg til avansert optimizer.
- [ ] Test før neste steg:
  - [ ] test at konstantuttrykk fortsatt gir samme resultat
  - [ ] test at optimaliseringen ikke endrer feilsemantikk

### 20. Legg til public API

- [ ] Oppdater [include/cxpr/cxpr.h](/home/evoldoc/_code/cxpr/include/cxpr/cxpr.h) med ny opaque type:
  - [ ] `cxpr_program`
- [ ] Legg til public API for:
  - [ ] compile
  - [ ] eval
  - [ ] eval_bool
  - [ ] free
- [ ] Ikke endre eller deprekér `cxpr_eval(...)`.
- [ ] Test før neste steg:
  - [ ] legg til public API-tester
  - [ ] bekreft at eksisterende API fortsatt oppfører seg likt

### 21. Integrer forsiktig med FormulaEngine

- [ ] Vurder å kompilere hver formel én gang under `cxpr_formula_compile(...)`.
- [ ] Behold AST for dependency-analyse og evalueringsrekkefølge.
- [ ] Bytt bare evalueringsmekanisme internt.
- [ ] Test før neste steg:
  - [ ] kjør eksisterende formula-tester
  - [ ] legg til minst én test som sammenligner AST-path og IR-path i formula-engine

### 22. Dokumenter arkitekturen

- [ ] Oppdater [README.md](/home/evoldoc/_code/cxpr/README.md) med kort forklaring av:
  - [ ] AST sin rolle
  - [ ] IR/compiled plan sin rolle
  - [ ] at dette er en ytelsesoptimalisering, ikke nytt språk eller nytt scope
- [ ] Test før neste steg:
  - [ ] les dokumentasjonen mot faktisk kode og API

### 23. Mål effekt før videre utvidelser

- [ ] Lag en liten benchmark for:
  - [ ] AST-eval
  - [ ] IR-eval
- [ ] Mål minst:
  - [ ] enkel aritmetikk
  - [ ] nestede uttrykk
  - [ ] funksjonskall
  - [ ] mange repetisjoner over nye contexts
- [ ] Dokumenter resultatet.
- [ ] Test før avslutning:
  - [ ] benchmarken kan kjøres
  - [ ] resultatene er forståelige og lagret i repoet eller dokumentert i plan/README

## Ikke-mål

- [ ] `cxpr` skal ikke bli et generelt numerikkbibliotek.
- [ ] `cxpr` skal ikke eie simuleringstidssteg, treningssløyfer eller datasett.
- [ ] `cxpr` skal ikke introdusere arrays, tensorer eller komplekst runtime-typesystem.
