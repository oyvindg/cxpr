# cxpr: kravspesifikasjon for felt-basert pathfinding (R6–R8)

> Skrevet fra en konsuments perspektiv, men **kontrakten er bevisst agnostisk** slik at
> flere konsumenter kan bygge på den. Fortsettelse av det interne kravdokumentet for
> R1–R5. Målet er å kunne
> uttrykke **flow-/distance-field pathfinding** i `.cxpr` og kjøre det i generated-C/bulk,
> slik at kostnads-, relaxerings- og retningsvalg-logikk flyttes fra C# til cxpr.
> Kravene er **additive og agnostiske** — de er nyttige langt utover pathfinding
> (pooling, sensor-fusjon, influence maps, argmax-klassifisering) og rører ikke språkets
> kjerne (ren dataflyt, determinisme, bulk/GPU, skalar ABI).

## Kontekst og valgt algoritmefamilie

A\*/Dijkstra (label-setting, mutabel prioritetskø, ubundet data-avhengig løkke) passer
**ikke** cxpr og skal **ikke** legges til — det ville bryte branchless-codegen og
CUDA-én-tråd-per-element. Den passende familien er **label-correcting relaxation**
(Bellman-Ford / distance-field / flow-field): per celle `dist = min(nabo_dist + kost)`,
iterert til fixpoint. Dette er nøyaktig grid-stencil-mønsteret i
`examples/bulk_grid/klein_gordon_cell.cxpr`.

Arbeidsdeling (låst): **cxpr eier** cellematten (traversal-kostnad, relaxering,
retningsvalg, movement intent). **Host (C#) eier** autoritativt grid, occupancy/collision,
buffer-lifecycle, bulk-scheduling, iterasjons-/konvergens-driveren, validering mot live
world, commit av posisjon, og konfliktløsning når flere agenter velger samme celle.

## Ufravikelige designrammer (gjelder alle krav)

1. Bevar **FP-determinisme**, **bulk/GPU-parallellisme** og **ren per-tick dataflyt**.
2. Må virke i **generert-C-/`cxpr_bulk_run`-stien**, ikke bare interpreteren.
3. **Ingen språkløkker, rekursjon, heap eller graftraversering.** Iterasjon er host-eid.
4. **Skalar generated-model/bulk-ABI beholdes** (`include/cxpr/generated.h`,
   `include/cxpr/bulk.h`). Ingen array-typede input-kolonner.

## Låste beslutninger (nye — leser dette før akseptansekriteriene)

Disse var åpne i første utkast. De er nå besluttet for å holde kravene leverbare og
agnostiske. Overstyr bevisst, ikke ved uhell.

- **B1 — argmin/argmax returnerer indeks som modellens vanlige skalar, IKKE int64.**
  R6 frikobles fra R4. `CXPR_VALUE_INT64` finnes som type-tag
  (`include/cxpr/types.h:70`) men har ingen operator-/funksjonsstøtte ennå; å kreve
  ekte heltallstype ville dratt R6 inn i uimplementert int64-plumbing. Indeks for
  arity ≤ 8 er trivielt eksakt i `double` (≪ 2^53), og host caster. Dette holder R6
  faktisk «liten». Dokumentér eksakthetsgrensen. Ekte int64-retur er en senere,
  separat oppgave hvis R4 lander.
- **B2 — Sentinel er «stor endelig verdi + metning», IKKE NaN/∞-token — og metning
  gjøres med en `fn`, IKKE en ny builtin.** NaN-basert ∞ er en landmine på GPU og i
  argmin (se R7-motivasjon). Vi velger endelig sentinel. Metning uttrykkes som en
  vanlig cxpr-funksjon `fn sadd(a, b) = min(a + b, $INF)` — **verifisert at den lowrer
  til generated-C (og dermed CUDA)** som en `static inline double`. Dette fjerner
  behovet for en `sat_add`-builtin i alle fem codegen-stiene; konsumenten definerer
  (eller importerer) helper-en selv. For `double` (B1/dagens) er `$INF = 1e18`
  trygt ≪ `DBL_MAX`, så `a + b` kan ikke overflowe før `min` klemmer. **Ekte
  int64-metning** (mot wrap i selve `a + b`) er *ikke* dekket av en `fn` og utsettes
  til R4 hvis heltallsavstander noen gang trengs — da som R4-anliggende, ikke R7.
- **B3 — argmin over kun-blokkerte naboer er udefinert retning; host må validere.**
  Hvis alle argumenter er sentinel, returnerer `argmin` fortsatt laveste indeks (0).
  Kontrakten: **host stoler kun på `dir` når `next_dist < $INF`.** Dette
  spesifiseres i interop-kontrakten, ikke overlatt til hver konsument.
- **B4 — Kanonisk naborekkefølge dokumenteres som anbefalt interop-konvensjon.**
  `dir` er en 0-basert indeks inn i argumentlisten til `argmin`; betydningen finnes
  kun i argumentrekkefølgen. Vi anbefaler **N, S, E, W** (0..3) for 4-nabo og
  **N, S, E, W, NE, NW, SE, SW** (0..7) for 8-nabo, slik at eksempler og fixtures
  er overførbare mellom konsumenter. Rekkefølgen er teknisk caller-definert, men
  referanse-descriptoren (se interop-seksjon) bruker denne.

## Allerede dekket i dag (ingen endring nødvendig)

Fast-arity `min(a, b, c, d)` med skalar-argumenter lowrer allerede til nøstet
`fmin`/`fmax` i AST-codegen (`src/codegen.c:307-319`) og IR-codegen
(`src/ir/codegen.c:571-587`), arity 1..8, med testdekning
(`tests/codegen.test.c:225-227`). En 4/8-nabo grid-relaxering
`next_dist = is_wall ? BIG : is_goal ? 0 : min(n+cost, s+cost, e+cost, w+cost)`
kan altså kompileres og bulk-kjøres **nå**, gitt at host gjør halo-gather og
buffer-swap. Sentinel-metning via `fn sadd(a, b) = min(a + b, $INF)` lowrer også i dag
(verifisert), så R7 er ren konvensjon/dokumentasjon. **Den eneste faktiske codegen-blokkeren
er retningsvalget `argmin` (R6).**

CUDA er ikke en egen uttrykks-backend: `plugins/cuda.c:318-322` gjenbruker
`cxpr_model_compiled_generate_c` og post-prosesserer. Alt som lowres via C-stien virker
derfor på CUDA «gratis». De **tre** uttrykks-stiene som må holdes i sync er derfor:
tree-eval, IR og C-codegen (CUDA arver C).

---

## P0 — Nødvendig for flow-field

### R6 — `argmin`/`argmax` (+ `sum`) som fast-arity skalar-fold

**Mål:** Nye basket-aggregater `argmin(a, b, …)` og `argmax(a, b, …)`, arity 1..8, som
returnerer **0-basert indeks** til det minste/største argumentet (som skalar, jf. **B1**);
og `sum(a, …)` som fast-arity fold (finnes i dag kun som window-form,
`src/parser/primary.c:14`). Ties: laveste indeks vinner (deterministisk). Lowres til
nøstet `?:` i codegen — ingen array-transport, ingen ABI-endring — parallelt med dagens
`min`/`max`-fold.

**Motivasjon:** Gjør et distance-field om til et **flow-field**: `dir = argmin(n+cost,
s+cost, e+cost, w+cost)` gir movement intent som en retningskode host mapper til en
celle (jf. **B4**). Agnostisk gjenbruk: argmax-klassifisering, nearest-of-N, max-pooling.

**Kritisk — kryss-backend determinisme (skjerpet):** «Bit-identisk» krever mer enn at
verdi-folden er lik. `argmin` velger indeks basert på sammenligning av *beregnede*
floats; FMA/contraction eller ulik evalueringsrekkefølge for `n+cost` kan vippe indeksen
selv når `min`-verdien er «lik nok».

- `argmin` **skal lowres fra nøyaktig samme deluttrykk** som verdien den indekserer
  (ingen separat re-evaluering av argumentene).
- Ingen fast-math/contraction på disse uttrykkene; dokumentér kravet.
- Ties brytes på **kildeposisjon i argumentlisten** (laveste indeks), ikke på
  runtime-float-likhet.

**Akseptansekriterier (tester — alle via fixtures, se teststrategi):**
- `argmin(3, 1, 2) == 1`, `argmax(3, 1, 2) == 0`, `sum(1, 2, 3) == 6` i tree-eval, IR,
  generated-C og CUDA — **bit-identisk på tvers**.
- Eksakt tie: `argmin(2, 2, 5) == 0` (laveste indeks).
- **Nær-tie** (nytt): argumenter som er float-nære etter `x+cost`, konstruert så en
  FMA-kontraksjon *ville* vippet indeksen — assert lik indeks på alle backends.
- Generated-C emitterer nøstet `?:` uten array-node; ingen `UNSUPPORTED_RESULT`.
- Indeks er eksakt for arity 1..8 (double, ≪ 2^53); dokumentert grense (**B1**).
- Arity > 8 avvises med tydelig feil (som `min`/`max` i dag).
- Argument som er sentinel/`CXPR_DIST_INF`: argmin ignorerer det korrekt så lenge minst
  én endelig finnes; alle-sentinel gir indeks 0 + `next_dist == INF` (**B3**, kobler R7).

### R7 — Sentinel/∞-konvensjon for uoppnåelige celler (ingen ny builtin)

**Mål:** Én veldefinert måte å representere «uoppnåelig/blokkert» som overlever `min`/`+`
uten stille NaN-feil. **Besluttet (jf. B2):** en endelig sentinel-konstant + metning via
`fn`, IKKE en ny builtin. R7 er dermed **ren konvensjon + dokumentasjon**, ikke
codegen-arbeid — den kan gjøres i dag.

**Motivasjon:** I dag lekser `null/NULL` som NaN (`docs/api/language.md:57`), og
`fmin(NaN, x) = x` (IEEE) *ligner* «ignorer blokkert nabo», men `NaN + cost = NaN` som
når en ekte output er en stille feil. Relaxering trenger en definert, aritmetisk stabil ∞.

**Kontrakt:**
- Sentinel er en modell-param `$INF` (anbefalt `1e18`). Konvensjonen dokumenteres i
  `docs/api/language.md`.
- Metning uttrykkes som `fn sadd(a, b) = min(a + b, $INF)` — verifisert at den lowrer
  til generated-C/CUDA. Konsumenten definerer den i modellen (eller importerer en delt
  `.cxpr`-modul).

**Akseptansekriterier (tester — via fixtures):**
- `sadd($INF, cost) == $INF` og `sadd(x, cost) == x + cost` for endelig `x`; `min($INF, x)
  == x`. Bit-identisk tree-eval / IR / generated-C / CUDA.
- En vegg-celle (`is_wall`) propagerer aldri en endelig avstand til naboer over N
  relaxeringer; test på et lite grid med korridor rundt en vegg.
- Ingen NaN i noen output når sentinelen brukes korrekt.
- Interaksjon med R6: `next_dist == $INF` ⇒ `dir` er meningsløs (host-kontrakt, **B3**).

---

## P1 — Ergonomi (valgfritt)

### R8 — Host-side bulk all-reduce for konvergens

**Mål:** En valgfri hjelper i bulk-API-et som reduserer én skalar output-kolonne (f.eks.
`residual`/`changed`) til én skalar (sum/max), slik at host kan drive
iterér-til-fixpoint-løkka uten håndrullet reduksjon. Iterasjonen forblir host-eid.

**Akseptansekriterier (tester — via fixtures):**
- `cxpr_bulk_reduce(view, column, op)` gir samme resultat som en manuell løkke; validerer
  strides/extents som resten av bulk-API-et; deterministisk reduksjonsrekkefølge.

---

## Agnostisk interop-kontrakt (flere konsumenter)

Dette er det en **andre konsument** kopierer. Den er navngitt og stabil, på samme måte som
`wellbeing`/`physiology`-descriptorene i R1–R5-dokumentet.

**Referanse-celle-descriptor `grid_relax` (anbefalt):**
- **in** (skalar-kolonner, host halo-gather per celle): `d_n, d_s, d_e, d_w`
  (nabo-avstander; `$INF` for blokkert/utenfor), `cost`, `is_wall`, `is_goal`.
- **helper:** `fn sadd(a, b) = min(a + b, $INF)` (R7-konvensjon; ikke en builtin).
- **out**: `next_dist` (relaxert avstand eller `$INF`), `dir` (0-basert retningsindeks i
  N,S,E,W-rekkefølge, jf. **B4**).
- **Gyldighet:** `dir` er kun gyldig når `next_dist < $INF` (**B3**).
- **Versjonstag:** descriptoren merkes med en kontrakt-versjon (parallelt med R3s
  snapshot-versjonering) så konsumenter kan feile tydelig ved layout-endring.

**Host-side kontrakt (typisk instans av descriptoren):**
Host fyller per tick: cost-lag (terreng + occupancy-straff), `is_wall`/`is_goal`-masker, og
halo-naboer per celle som skalar-kolonner. cxpr returnerer per celle: `next_dist` og
`dir`. Host itererer relaxeringen til konvergens, leser `dir` for hver agents celle **kun
når `next_dist < $INF`**, validerer mot live occupancy, og committer/konfliktløser. cxpr
kjenner aldri host-ens entitets- eller verdensmodell.

**8-nabo-variant** utvider til `d_ne, d_nw, d_se, d_sw` og `dir` 0..7 i rekkefølgen fra
**B4**. Samme kontrakt for øvrig.

---

## Eksplisitt utenfor scope (bevisst)

- **A\*/Dijkstra/heap, generelle løkker, rekursjon, graftraversering i språket.** Bryter
  designet; host eier iterasjon.
- **Array-typede input-kolonner / array-reduksjon over et array-*input* i codegen.** Ikke
  nødvendig for grid (host-gather til faste skalar-kolonner dekker variabel grad), og ikke
  bærbart av den skalar-permanente bulk-ABI-en. Vurderes kun hvis generell-graf-behov
  senere rettferdiggjør en ABI-utvidelse — eget krav.
- **Eksplisitt rute som cxpr-verdi / rute-scoring over variabel lengde.** Flow-field har
  ingen eksplisitt rute; feltet ER ruten.
- **Ekte int64-retur fra argmin/argmax.** Utsatt (B1); avhenger av R4.
- **Dedikert `sat_add`-builtin.** Unødvendig — `fn sadd(a, b) = min(a + b, $INF)`
  dekker double-metning og lowrer til codegen/CUDA (B2/R7). Ekte int64-metning (mot
  wrap) utsatt til R4.

---

## Teststrategi — alt via egne fixtures

Ingen inline-strenglitteraler for de nye funksjonene i ad hoc-tester. Hver ny primitiv får
en `.cxpr`-fixture som kjøres gjennom **alle tre uttrykks-stiene** (tree-eval, IR,
generated-C) pluss CUDA-kontrakt, og asserteres bit-identisk. Følg mønsteret i
`tests/generated_array_parity.test.c` (`assert_parity` kjører interp + compiled) og
`tests/cuda_index_contract.test.c` (asserter generert-C-tekst for CUDA-stien).

**Nye fixture-mapper/-filer:**
- `tests/fixtures/pathfinding/argmin_argmax.cxpr` — basis argmin/argmax/sum-uttrykk.
- `tests/fixtures/pathfinding/ties.cxpr` — eksakt- og nær-tie-tilfeller.
- `tests/fixtures/pathfinding/sentinel.cxpr` — `$INF`/`fn sadd`-oppførsel.
- `tests/fixtures/pathfinding/grid_relax_4nabo.cxpr` — referanse-descriptor (R6+R7).
- `tests/fixtures/pathfinding/grid_relax_8nabo.cxpr` — 8-nabo-variant.
- `examples/bulk_grid/distance_field_cell.cxpr` + `flow_field_cell.cxpr` — forbilde-eksempel
  som *er* interop-spesifikasjonen (halo-layout, sentinel, dir-mapping, host-løkke i
  README-pseudokode).

**Nye/utvidede testfiler (speiler eksisterende konvensjon):**
- `tests/argmin_argmax_parity.test.c` — kryss-backend bit-identitet (tree/IR/gen-C).
- `tests/pathfinding_sentinel.test.c` — R7 vegg-propagering over N iterasjoner.
- `tests/cuda_index_contract.test.c` — utvid: assert argmin/argmax emitterer nøstet `?:`,
  ingen array-node, ingen `UNSUPPORTED_RESULT`.
- `tests/bulk_reduce.test.c` — R8, manuell løkke vs `cxpr_bulk_reduce`.

**Determinisme-matrise (må asserteres, ikke bare basis):**
| Tilfelle | tree-eval | IR | gen-C | CUDA-kontrakt |
|---|---|---|---|---|
| basis argmin/argmax/sum | ✓ | ✓ | ✓ | ✓ |
| eksakt tie → laveste indeks | ✓ | ✓ | ✓ | ✓ |
| nær-tie (FMA-følsom) | ✓ | ✓ | ✓ | ✓ |
| sentinel-argument ignoreres | ✓ | ✓ | ✓ | ✓ |
| alle-sentinel → dir=0, dist=INF | ✓ | ✓ | ✓ | ✓ |
| arity 1 og arity 8 | ✓ | ✓ | ✓ | ✓ |
| arity > 8 → tydelig feil | ✓ | ✓ | ✓ | ✓ |

---

## Utførings-checklist

Verifiserte integrasjonspunkter fra kodebasen. Kryss av per steg.

### R6 — argmin/argmax/sum (5 codegen-stier + tester)
- [ ] **Registry/impl:** `cxpr_argmin_n`/`cxpr_argmax_n`/`cxpr_sum_n` + registrering i
      `src/registry/defaults.c` (jf. `cxpr_min_n`/`cxpr_max_n` ~L440-448, L585-586).
- [ ] **Tree-eval/basket:** legg til i `cxpr_basket_is_builtin` og `cxpr_registry_add_ast`
      (`src/basket.c:42-50, 445-447`), arity 1..8.
- [ ] **AST-codegen:** ny special-case som lowrer til nøstet `?:` som *akkumulerer indeks*
      (`src/codegen.c:307-319`, parallelt med min/max-blokken).
- [ ] **IR-compile:** special-case for arity 1..8 (`src/ir/compile/node.c:735-754`).
- [ ] **IR-codegen:** emitter nøstet ternær-kjede som sporer indeks
      (`src/ir/codegen.c:571-587`).
- [ ] **`sum` fast-arity:** samme fem steg (i dag kun window-form,
      `src/parser/primary.c:14`).
- [ ] **Samme-uttrykk-lowering:** verifiser at argmin bruker samme deluttrykk som verdien
      (ingen re-eval); ingen fast-math/contraction.
- [ ] **Feil:** arity > 8 gir tydelig feil (speil min/max).
- [ ] **Fixtures + parity-tester** per matrisen over.

### R7 — sentinel/∞-konvensjon (ingen ny builtin — kan gjøres i dag)
- [ ] **Konvensjon:** `$INF = 1e18` + `fn sadd(a, b) = min(a + b, $INF)` i
      referanse-descriptor og eksempel. **Ingen codegen-endring.**
- [ ] **Dokumentasjon:** `docs/api/language.md` (ved/erstatt L57 null→NaN-notatet) —
      spesifiser `$INF`-konvensjonen, `min($INF,x)=x`, `sadd`-helper, ingen NaN.
- [ ] **Fixtures:** `sentinel.cxpr` + vegg-propagerings-test over N iterasjoner.
- [ ] **Kobling til R6:** alle-sentinel → dir=0, `next_dist==$INF`.

### Eksempel (interop-spesifikasjon)
- [ ] `examples/bulk_grid/distance_field_cell.cxpr` + `flow_field_cell.cxpr`.
- [ ] README med halo-gather-layout, sentinel-bruk, dir-mapping (B4), host-konvergensløkke
      som pseudokode, og descriptor-versjonstag.

### R8 — bulk all-reduce (kun hvis håndrullet konvergens blir en byrde)
- [ ] `cxpr_bulk_reduce(view, column, op)` i bulk-API; strides/extents-validering;
      deterministisk rekkefølge.
- [ ] `tests/bulk_reduce.test.c` mot manuell løkke.

### Avslutning
- [ ] Interop-kontrakt-seksjonen speilet inn i konsument-vendt doc (for flere konsumenter).
- [ ] Konsument-spike (se rekkefølge, punkt 5) — perf + determinisme mot dagens BFS.

---

## Foreslått rekkefølge

1. **R7** (sentinel-konvensjon `$INF` + `fn sadd`) — **først, men gratis**: ren
   dokumentasjon + fixture-konvensjon, ingen codegen. Låser sentinelen R6-testene
   antar (sentinel-ignorering, alle-sentinel-tilfellet).
2. **R6** (argmin/argmax/sum) — **den eneste faktiske codegen-jobben**; låser opp
   flow-field-retning.
3. **Eksempel** i `examples/bulk_grid/` — distance-field + flow-field-celle som forbilde
   og interop-spesifikasjon.
4. **R8** (bulk all-reduce) — kun hvis håndrullet konvergens blir en byrde.
5. Konsument-spike: distance-field mot ett mål for N agenter, målt mot dagens BFS på
   perf + determinisme, før noen faktisk migrering.

> Merk: R7 er nedgradert fra codegen-krav til ren konvensjon/dokumentasjon etter at
> `fn sadd(a, b) = min(a + b, $INF)` ble verifisert å lowre til generated-C/CUDA. Det
> fjerner en `sat_add`-builtin fra alle fem codegen-stiene. R6 (argmin) er nå den
> eneste språkendringen som faktisk kreves for flow-field.
