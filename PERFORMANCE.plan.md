# Performance regression fixes

Benchmark-tallene etter struct-implementasjonen viser to typer regresjoner:

- **AST defined functions**: 7–19x tregere (665→5147ns, 905→8329ns)
- **IR + AST basis**: 1.3–1.7x tregere for alle case, inkludert enkel aritmetikk

---

## Rot-årsaker

### 1. `cxpr_context_overlay_new` gjør én ekstra allokering per kall

`cxpr_context_new()` initialiserer nå tre hashmaps (variables, params, structs). For scalar
defined functions, trenger overlayet aldri struct-mappet — men det allokeres og destrueres uansett.

Kostnad per overlay (for scalar defined functions):
- 1 ekstra `calloc(32, 16)` = 512 bytes
- `cxpr_struct_map_destroy` looper over 32 tomme slots og kaller `free(NULL)` +
  `cxpr_struct_value_free(NULL)` per slot = **64 ekstra funksjonskall**

`hyp2(a,b) + hyp2(c,d) - sq(e)` trigger 7 rekursive defined-fn-kall (hyp2→sq×2, hyp2→sq×2,
sq direkte). Det gir 7 × 64 = 448 ekstra funksjonskall + 7 ekstra allokering/fri-sykluser per
evaluering. Det forklarer 665→5147ns.

### 2. IR inliner, AST gjør ikke det

IR-kompilatoren inliner scalar defined functions via `cxpr_ir_subst_frame` (ir.c:1648–1658).
Dermed lager IR null kontekst-overlays for disse kallene ved kjøretid — de kompileres til rene
aritmetikkoperasjoner. AST lager alltid ett overlay per kall.

Resultatet: IR er bare 1.6x tregere for defined_fn, mens AST er 7.7x tregere.

### 3. IR basis-regresjon (simple_arith: 119→198ns)

Selv enkle uttrykk uten defined functions eller structs er tregere i IR (1.7x). Mulige årsaker:
- `sizeof(cxpr_ir_instr)` kan ha vokst (sjekk feltrekkefølge og padding)
- `cxpr_ir_infer_fast_result_kind` treffer nye node-typer (FIELD_ACCESS, CHAIN_ACCESS,
  PRODUCER_ACCESS) som returnerer UNKNOWN, noe som sender uttrykk til `cxpr_ir_exec_typed`
  i stedet for `cxpr_ir_exec_scalar_fast`

---

## Fix 1 — Lazy `cxpr_struct_map`-initialisering (høyest prioritet)

**Fil:** `src/context.c`

Endre `cxpr_struct_map_init` til å ikke allokere:

```c
void cxpr_struct_map_init(cxpr_struct_map* map) {
    map->capacity = 0;
    map->count = 0;
    map->entries = NULL;
}
```

Legg til lazy init i `cxpr_context_set_struct`, rett før load-factor-sjekken:

```c
void cxpr_context_set_struct(cxpr_context* ctx, const char* name,
                             const cxpr_struct_value* value) {
    ...
    if (!ctx->structs.entries) {
        ctx->structs.capacity = CXPR_HASHMAP_INITIAL_CAPACITY;
        ctx->structs.entries = calloc(ctx->structs.capacity,
                                     sizeof(cxpr_struct_map_entry));
        if (!ctx->structs.entries) return;
    }
    ...
}
```

Alle andre funksjoner håndterer allerede `entries == NULL`:
- `cxpr_struct_map_find_slot`: har `if (!map->entries || map->capacity == 0) return NULL`
- `cxpr_struct_map_destroy`: har `if (!map->entries) return`
- `cxpr_context_get_struct` → `cxpr_struct_map_get` → `cxpr_struct_map_find_slot`

**Forventet effekt:** Overlay-opprettelse og destruksjon for scalar defined functions blir O(1)
i stedet for O(32). Bør bringe defined_fn-tallene nær pre-struct-nivå.

---

## Fix 2 — AST eval av scalar defined functions: bruk IR-backend

**Fil:** `src/eval.c`, funksjon `cxpr_eval_defined_function`

For scalar-only defined functions (ingen struct-parametre) finnes det allerede et lazily
kompilert `entry->defined_program`. Bruk `cxpr_ir_exec_with_locals` med en stack-allokert
`double`-array i stedet for å opprette et kontekst-overlay:

```c
// Scalar-only path — unngå heap-allokering av overlay
if (scalar_only) {
    double locals[32];
    for (size_t i = 0; i < entry->defined_param_count; i++) {
        if (scalar_args[i].type != CXPR_FIELD_DOUBLE) { /* error */ }
        locals[i] = scalar_args[i].d;
    }

    // Lazy-kompiler body til IR hvis ikke gjort
    if (!entry->defined_program && !entry->defined_program_failed) {
        /* kompiler entry->defined_body til entry->defined_program */
    }

    if (entry->defined_program) {
        double result = cxpr_ir_exec_with_locals(
            &entry->defined_program->ir, ctx, reg,
            locals, entry->defined_param_count, err);
        return cxpr_fv_double(result);
    }

    // Fallback: opprett overlay som nå
    ...
}
```

**Forventet effekt:** Eliminerer overlay-allokeringen helt for scalar defined functions i AST-eval.
Bør bringe AST defined_fn nær IR-ytelse (minus IR-inlining-fordelen).

---

## Fix 3 — Verifiser IR fast-path og `cxpr_ir_instr`-størrelse

**Steg 1:** Sjekk at `fast_result_kind` faktisk settes til `CXPR_IR_RESULT_DOUBLE` for
simple_arith. Legg midlertidig til en assert eller fprintf i `cxpr_ir_exec`:

```c
// Debug: verifiser at fast-path brukes
fprintf(stderr, "fast_result_kind=%d for case\n", program->fast_result_kind);
```

**Steg 2:** Sjekk størrelsen på `cxpr_ir_instr`:

```c
static_assert(sizeof(cxpr_ir_instr) <= 56, "cxpr_ir_instr for stor");
```

Hvis strukturen er vokst (f.eks. til 64 bytes), reorganiser feltene slik at de mest brukte
(`op`, `value`, `name`, `hash`) kommer først og passer i én cache-linje.

**Steg 3:** Sjekk om `cxpr_ir_infer_fast_result_kind` mangler case for nye node-typer. Mangler
én case → returnerer implisitt 0 (`CXPR_IR_RESULT_UNKNOWN`) → hele uttrykket sendes til
`cxpr_ir_exec_typed`. Sjekk at alle node-typer som kan produsere double er dekket.

---

## Prioritert rekkefølge

| # | Fix | Kompleksitet | Forventet gevinst |
|---|-----|-------------|-------------------|
| 1 | Lazy struct_map-init | Lav | Stor (defined_fn AST) |
| 2 | AST scalar defined → IR backend | Middels | Stor (defined_fn AST) |
| 3 | IR fast-path verifisering | Lav (debug) | Middels (basis IR) |
| 4 | `cxpr_ir_instr` field-reordering | Lav | Liten–middels |

Fix 1 alene bør gi tilbake mesteparten av ytelsen. Fix 2 er mer aggressiv og gir AST-eval
nesten IR-kvalitet for scalar defined functions.
