# cxpr — Korrekthets-søsken: fiks-plan

Fiks-plan for søsken av de tre bug-klassene fra `improvements.txt` /
`improvements-plan.md` (opprinnelig funnet og fikset i `src/context/values.c`).
Funnene kommer fra et systematisk søk på tvers av alle `src/`-subsystemer; hvert
funn er klassifisert med kontrollflyt-begrunnelse, reachability og foreslått fiks.

> **Til agenten som implementerer:** Kryss av (`[ ]` → `[x]`) **fortløpende** når
> et punkt er implementert **og** verifisert. **Re-verifiser hvert funn mot koden
> først** — fil:linje kan ha flyttet seg, og klassifiseringen (reell/latent) skal
> bekreftes før endring. Legg kort notis (fil:linje eller testnavn) bak punktet når
> det avviker. Ett avkrysset punkt = «gjort og bekreftet», ikke «påbegynt». LSan
> kan ikke kjøres i ptrace-miljøet — la LSan-punkter stå ærlig ukrysset med den
> begrunnelsen, men behold ASan+UBSan som port.

## Bug-klasser (til referanse)

- **Klasse 1** — valgfri out-param styrer returverdien feil: `if (found && *found)`
  kortslutter når `found == NULL`, returnerer default selv om verdien finnes.
  Fiks: lokal `bool local_found`, styr retur på den, kopier til `*found` kun når
  ikke-NULL.
- **Klasse 2** — typebytte etterlater gammel binding: en setter/innsetter fjerner
  noen, men ikke alle, konkurrerende bindinger for samme nøkkel.
- **Klasse 3** — eierskaps-lekkasje: en eid STRUCT/ARRAY-klone (typisk fra
  `cxpr_context_get_typed` / `cxpr_value_clone`) frigjøres ikke på en
  type-avvisnings- eller fall-gjennom-gren. (STRING via `cxpr_string` er borrow og
  lekker ikke.)

---

## Gruppe A — Reelle, reachable Klasse-3-lekkasjer (høy prioritet)

Felles rotmønster: eid STRUCT/ARRAY-klone fra `get_typed` som ikke frigjøres.
Alle bør få ASan+LSan-regresjonsdekning (LSan der miljøet tillater).

### A1 — `model/session.c:1146` — fused-IR input, lekker hver tick
Leser kun skalar (`.b`/`.d`) fra en eid `get_typed`-verdi uten
`cxpr_value_free`; lekker når et fused input er bundet til ARRAY/STRUCT. Størst
effekt (hot tick-path, ubundet vekst i lang backtest).

- [ ] Re-verifiser at `typed` er eid for ARRAY/STRUCT på denne grenen og at ingen
  `cxpr_value_free(&typed)` finnes i blokken.
- [ ] Legg `cxpr_value_free(&typed);` etter at skalarverdien er lest ut.
- [ ] Regresjonstest: tick med et fused input bundet til array/struct skal ikke
  lekke (ASan+LSan).

### A2 — `model/session.c:976-1001` — barnemodell navnearvet input
`else`-grenen henter input via `cxpr_context_get_typed` (eid ARRAY/STRUCT-klone),
men `owns_value` settes ikke → `cxpr_value_free` kjøres aldri; `set_value`
dyp-kopierer, så originalklonen lekker.

- [ ] Re-verifiser `owns_value`-logikken og at `set_value`/`store_array`
  dyp-kopierer.
- [ ] Sett `owns_value = true` også på `get_typed`-grenen (klonen er alltid eid),
  eller frigjør ubetinget etter `set_value`.
- [ ] Regresjonstest: barnemodell-produsentkall med navnearvet array/struct-input.

### A3 — `ir/exec/lookup.c:249` — `cxpr_ir_load_chain_value`
Eid `root` fra `get_typed` lekker på alle utganger av segment-løkka
(felt-ikke-funnet, terminal-suksess, ikke-struct-mellomledd, malformert). Søster-
mønsteret i `typed.c` frigjør korrekt — asymmetrien er buggen.

- [ ] Re-verifiser at `root` er eid (STRUCT/ARRAY/STRING-klone) og at ingen
  exit-gren frigjør den; bekreft reachability via chain-load-opcode
  (`typed.c:508`) for `a.b.c` der `a` er typed struct-var utenfor struct-map.
- [ ] Frigjør `root` på hver exit-gren (etter evt. `cxpr_value_clone(&value)`),
  inkl. `else`-grenen når `found` men typen ikke er STRUCT.
- [ ] Regresjonstest: chain-access mot typed struct-var som ikke er i struct-map.

### A4 — `eval/calls/producer.c:94` — dobbel-klone
`get_field` (helpers/base.c) returnerer allerede en dyp eid klone; linje 94
re-kloner og overskriver pekeren → første klone lekker. Den ekstra klonen er
unødvendig. Søster-stien `eval/calls.c:48-52` returnerer `result` direkte.

- [ ] Re-verifiser at `get_field` eier klonen og at linje 94 er redundant.
- [ ] Fjern re-klonen (linje 94); behold retur av `result` direkte.
- [ ] Regresjonstest: brukerprodusent med struct/array return-felt.

### A5 — `eval/snapshot/flow.c:366-376` — source-ref-løkke
`value = cxpr_context_get_typed(...)` (eid for struct/array); `add_value_node`
kloner argumentet (tar ikke eierskap); `value` frigjøres aldri. Søster-grenen
`add_lookback_sources` (:147) frigjør korrekt.

- [ ] Re-verifiser at `add_value_node` kloner (ikke overtar) og at `value` ikke
  frigjøres.
- [ ] Legg `cxpr_value_free(&value);` etter `free(value_text);`.
- [ ] Regresjonstest / snapshot-diagnostikk med source-ref som resolver til
  struct/array.

### A6 — `eval/snapshot/flow.c:401-411` — param-løkke
Samme mønster med `cxpr_context_get_param_typed`.

- [ ] Legg `cxpr_value_free(&value);` etter `free(value_text);` på :411.
- [ ] Dekkes av samme snapshot-regresjonstest som A5 (param-variant).

---

## Gruppe B — Latente Klasse-1-feller (middels — latent, men matcher originalbuggen eksakt)

Ingen nåværende kaller sender `found == NULL`, men begge headere/kontrakter
behandler `found` som valgfri → felle for fremtidige kallere. Lav risiko, enkel
fiks.

### B1 — `ir/exec/lookup.c:41` — `cxpr_ir_context_get_prehashed`
`if (found && *found) return value;` — når `found == NULL` forkastes en korrekt
hentet verdi og funksjonen rekurserer til parent / returnerer 0.0.

- [ ] Bekreft at `found` er ment valgfri (header/kontrakt) og at ingen kaller
  sender NULL i dag (ellers oppgrader til Gruppe A).
- [ ] Bruk lokal `bool local_found`; styr retur på den, kopier til `*found` kun
  når ikke-NULL.
- [ ] Regresjonstest: `found == NULL` på en eksisterende variabel returnerer riktig
  verdi (ikke 0.0), inkl. parent-overlay-tilfellet.

### B2 — `model/session.c:158-159` — `cxpr_model_context_get_history_value`
Både Klasse 1 (samme kortslutning) og Klasse 3 (eid array/struct-klone `value`
lekker i `found == NULL`-fall-gjennom).

- [ ] Bekreft at kallere (`session.c:244/318/1146`) sender `&found` i dag (latent).
- [ ] `local_found`-fiks for retur-logikken; frigjør `value` på fall-gjennom når
  eid.
- [ ] Regresjonstest som dekker både retur-korrekthet og ikke-lekkasje ved
  `found == NULL` på en array/struct-binding.

---

## Gruppe C — Klasse-2-asymmetri med kryss-lags-sjekk FØRST (potensielt alvorlig)

### C0 — Gate: avklar dispatch-rekkefølgen i eval-laget (gjør før C1)
`registry/define.c:335-355` (skalar `cxpr_registry_define_fn`-redefinering) nuller
ikke `ast_func`/`struct_producer`, mens søster-stien (:556) og
`cxpr_registry_reset_entry` (`helpers.c:159-160`) gjør det. De gjenværende
pekerne er **dangling-men-ikke-NULL** (backing-userdata alt frigjort av
`clear_owned_entry`).

- [ ] Fastslå om eval-/kall-laget konsulterer `ast_func` eller `struct_producer`
  **før** `defined_body` ved dispatch. Hvis ja → dette er en **use-after-free**
  (ikke bare feil verdi), og C1 er høy prioritet. Hvis nei → bekreft latent.
  Noter konklusjonen med fil:linje for dispatch-stedet.

### C1 — Fiks asymmetrien
- [ ] I `existing`-grenen (`define.c:336`) null `existing->ast_func` og
  `existing->struct_producer` (speil :556-557) — eller bedre: rut begge
  define-stiene gjennom `cxpr_registry_reset_entry`/`replace_entry` i stedet for
  håndrullede inline-reset.
- [ ] Regresjonstest: registrer et navn via `add_ast`/`add_struct`, redefinér det
  som skalar `fn`, kall det → forvent ny `defined_body`-oppførsel, ingen stale
  dispatch / UAF (kjør under ASan).

---

## Ferdigkriterier

- [ ] Hvert funn er re-verifisert mot gjeldende kode før endring; klassifisering
  bekreftet.
- [ ] Gruppe A: alle seks lekkasjene fikset med regresjonsdekning; ingen eid
  STRUCT/ARRAY-klone igjen på de berørte grenene.
- [ ] Gruppe B: `local_found`-mønster på begge; found==NULL returnerer riktig verdi
  og lekker ikke.
- [ ] Gruppe C: dispatch-rekkefølgen avklart (C0) og asymmetrien lukket (C1).
- [ ] Ingen semantisk regresjon: relevant test-suite + AST↔IR-paritet grønn under
  ASan+UBSan; LSan der miljøet tillater, ellers dokumentert ptrace-blokkering.
- [ ] Negativ-kontroll: Klasse 1 og Klasse 2 bekreftet rene ellers (kun de
  oppførte funnene), så planen ikke gir inntrykk av bredere dekning enn utført.
