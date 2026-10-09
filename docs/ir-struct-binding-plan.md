# cxpr — Forhåndsberegn binding-planen for struct-retur-kall

Formålet er å angripe residual-kostnaden i struct-retur-definerte kall etter at
overlay-gjenbruk er på plass: ~500 ns av `struct_call`-IR (1018→951 ns) ligger
fortsatt i binding-**oppsettet**. Planen måler først hva i den lumpen som
dominerer, og forhåndsberegner så binding-planen ved kompilering hvis
strengbygging/hashing er skyldig. Grunnlag: `benchmarks/ir_bench.c`, verifisert
kilde i `src/ir/exec/calls.c:252–326` og `src/ir/compile/node.c`.

> **Til agenten som implementerer:** Kryss av (`[ ]` → `[x]`) **fortløpende** når
> et punkt er utført **og** verifisert (bygger + måling bekrefter). Legg kort
> notis (fil:linje eller målt ns/op) bak punktet. Ett avkrysset punkt = «gjort og
> bekreftet», ikke «påbegynt». LSan kan ikke kjøres i ptrace-miljøet — la
> LSan-punkter stå ærlig ukrysset med den begrunnelsen.
>
> **Måling før implementering.** Fire tidligere forsøk (memo-hash, overlay-pool,
> arg-plan, tom-overlay) implementerte eller drepte idéer på feil profil. «arg-plan»
> ble spesielt markert «ikke flaskehals» basert på *skalar*-profilen, mens
> struct-felt-arg-bindingen aldri ble målt. Ikke implementer fiksen i Fase 2 før
> mål-splitten i Fase 1 er tatt på faktiske tall.

## Bakgrunn (bekreftet)

- `struct_call`: AST 980,08 / IR 1018,72 ns → **IR 0,96x (tregere enn AST).**
  Return-felt-kroppene evalueres med `cxpr_eval_ast` (`calls.c:322`) — trewalk,
  ikke kompilert IR — så IR-dispatch legger bare overhead oppå AST-tolkningen.
- Etter overlay-gjenbruk (951 ns) er residualen dominert av binding-oppsettet
  (`calls.c:265–309`). Struct-felt-arg-grenen (`calls.c:272–303`) gjør per eval:
  - opptil **3 `snprintf`** per felt (`root.field`, `root_field`,
    `paramnavn.field`, linjer 283/286/297),
  - dobbelt hashet `cxpr_context_get` (linjer 284/287),
  - hashet `cxpr_context_set` inn i overlay (linje 302).
- Profil-lump fra `ir-struct-call-profile-plan.md`: overlay + 2 bindings + free
  = 568,40 ns; feltkropper 116,28 ns; struct new/free 73,62 ns; feltoppslag
  5,95 ns.
- Skalar-arg-grenen (`calls.c:304–308`) er triviell og ikke i søkelyset — det var
  den som ble profilert da «arg-plan» ble feilaktig avvist.

---

## Fase 1 — Mål-splitt residual-lumpen (måling, ingen fiks)

Mål: dele «overlay + bindings + free» (568 ns) i sine bestanddeler for
struct-felt-arg-caset.

- [x] Bruk et `struct_call`-case som treffer struct-felt-arg-grenen
  (`calls.c:272–303`), ikke bare skalar-args. Bekreft i IR-view/asserts at grenen
  faktisk kjøres (ellers måler vi feil ting igjen). Eksisterende `struct_call`
  ble avkreftet som skalar-arg; nytt `struct_binding` har identifier-root,
  `CALL_DEFINED`-assert og en separat eval som bare kan lykkes via
  `vector.x/y`→`v.x/y`-bindingen. Median AST/IR 1040,32/1138,84 ns.
- [x] Isoler med stegvis utkobling (samme metode som forrige profil):
  - (a) `snprintf`/nøkkelbygging (`calls.c:283,286,297`),
  - (b) hashet `cxpr_context_get` × opptil 2 (`calls.c:284,287`),
  - (c) hashet `cxpr_context_set` inn i overlay (`calls.c:302`),
  - (d) overlay-reset/free (den gjenbrukte overlayens nullstilling).
- [x] Rangér (a)–(d) med tall; noter her. `perf` mangler. Isolerte hot-loop-
  medianer: (a) fire nødvendige `snprintf` ≈160–175 ns (seks-kall kontroll
  239,36 ns), (b) to get 34,10 ns, (c) to eksisterende set 188,64 ns,
  (d)+(fersk prehashed insert) 563,11 ns. Reset frigjør hashmap-nøklene, så
  fersk insert kan ikke skilles ærlig fra reset uten å endre datastrukturen.
  Rangering: reset/reinsert klart størst, deretter set, snprintf, get.

## Fase 2 — Beslutningsport + målrettet fiks

- [ ] **Hvis (a)+(b)+(c) (strengbygging/hashing) dominerer:** forhåndsberegn
  binding-planen ved kompilering:
  - løs felt-nøklene én gang ved compile til prehashed handles / slot-indekser og
    lagre dem på kall-instruksjonen, i stedet for `snprintf` + hashet get/set per
    eval (samme grep som lookback-chain-keys),
  - bind inn i overlay via prehashed/slot-vei (jf. `mutate_slot` 9,76 ns vs
    generisk set 114 ns i baselinen),
  - behold nøyaktig samme fallback-semantikk: prøv `.`-nøkkel før `_`-nøkkel,
    `CXPR_ERR_UNKNOWN_IDENTIFIER` når feltet mangler.
  - Mål på nytt mot `struct_call` og `producer-field[1]`; behold kun ved robust
    gevinst utenfor støy (jf. 2,62 %-fella).
- [x] **Hvis (d) overlay-reset dominerer:** da er binding-veien allerede billig og
  banen er nær sitt tak. Marker det i `ir-struct-call-profile-plan.md`, og gå til
  Fase 4 (pivot). Beslutning: ingen binding-plan implementert; slots blir
  ugyldige fordi reset frigjør nøklene, og precomputed strenger angriper ikke
  den dominerende reset/reinsert-kostnaden.
- [x] Bekreft at endringen skalerer: re-mål `cxpr_signal_helpers`
  (`benchmarks/model_bench.c`) — flere args ⇒ flere snprintf, så gevinsten bør
  være større der enn på det lille `struct_call`-caset. Ingen binding-endring
  ble beholdt, altså ingen skalering å hevde. Ny 7-trial median er 3751,54 ns;
  dette støtter pivot fremfor en udokumentert arg-plan.

## Fase 3 — (Sekundært) kompiler return-felt-kroppene til IR

Kun hvis Fase 2 ga gevinst og feltkroppene (116 ns) blir den nye toppen.

- [ ] Erstatt `cxpr_eval_ast` (`calls.c:322`) med kompilert IR for
  `defined_return_field_bodies`; forhåndskompiler ved registrering/compile.
- [ ] Bekreft AST↔IR-paritet for struct-retur-feltverdier og feilkoder.
- [ ] Benchmark: `struct_call` IR/AST skal krysse 1,0x (IR endelig raskere enn
  AST for denne formen).

## Fase 4 — Pivot-vurdering (kun hvis Fase 2 lander på «ved tak»)

- [x] Kvantifisér gevinst ved utvidet generert-C-/fused-dekning for struct-retur-
  og produsent-kallformer (baselinen: generert C ~2,9 ns vs IR ~1000+ ns).
  Identifiser hvilke former som mangler generert-C-vei i dag.
  og produsent-kallformer. `struct_call` C 2,96 ns mot forbedret IR 951,32 ns,
  altså ca. 321x. `struct_binding` mangler C-kolonne/coverage i dag.
- [x] Skriv kort anbefaling (ny plan eller avslutning av interpreter-sporet) basert
  på tallene. Anbefaling: avslutt binding-plan/slot-sporet inntil overlay-map kan
  resettes uten key-destruksjon; prioriter generert-C/fused-støtte for
  identifier-root struct-binding og signal-helper-formene.

---

## Ferdigkriterier

- [x] Residual-lumpen (568 ns) er splittet i (a)–(d) med tall (eller eksplisitt
  begrunnet der verktøy manglet).
- [x] Beslutningsporten (Fase 2) er tatt eksplisitt på faktiske tall, og relevante
  planer er oppdatert i tråd med utfallet.
- [x] Enhver implementert fiks viser robust gevinst utenfor støy, skalerer til
  `cxpr_signal_helpers`, og ingen semantisk regresjon: call-/IR-suite +
  AST↔IR-paritet grønn under ASan+UBSan (LSan der miljøet tillater; ellers
  dokumentert ptrace-blokkering). Ingen binding-fiks ble implementert/beholdt;
  berørt suite er likevel grønn 11/11 under ASan+UBSan. LSan er deaktivert med
  `ASAN_OPTIONS=detect_leaks=0` i ptrace-miljøet.
- [x] Binding-fallback (`.` før `_`, `CXPR_ERR_UNKNOWN_IDENTIFIER`) er uendret og
  test-dekket i `tests/ir_exec_calls.test.c`.
- [x] Hvis ingen fiks: struct-retur-binding-veien er dokumentert «ved tak» med
  tall, og pivot-vurderingen (Fase 4) er skrevet.
