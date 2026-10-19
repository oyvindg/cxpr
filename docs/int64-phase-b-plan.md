# cxpr int64 Fase B — kilde-typing og inferens

Maalet er at heltall beholder `int64` fra kildekode til resultat uten at hvert
mellomledd maa annoteres. Eksterne data har fortsatt en eksplisitt kontrakt.

## Laaste semantikkvalg

- Usuffikserte heltall over doubles eksakte heltallsgrense (`2^53`), men innenfor
  `INT64_MAX`, infereres som `int64`. Mindre literaler beholder `number` av
  kompatibilitets- og faststi-hensyn.
- `42i64` er fortsatt den eksplisitte literalformen og gir range-feil ved overflow.
- `42.0` og `42e0` er `number`.
- `int64` er kanonisk deklarasjonsnavn; `int` beholdes som kompatibelt alias.
- Inputs og `$param` kan deklareres `int64`; interne uttrykk infererer videre.
- Eksterne, utypede inputs infereres ikke fra én runtime-verdi fordi den kompilerte
  modellens type maa vaere stabil mellom ticks.
- Eksplisitt `number` overstyrer inferert literaltype ved deklarasjonsgrensen.

## Leveranse

- [x] Inferer usuffikserte heltallsliteraler innenfor int64-området.
- [x] Godta `int64` i deklarasjoner og behold `int` som alias.
- [x] Propager deklarerte input- og paramtyper gjennom modellens resultatinferens.
- [x] Verifiser AST, IR, modell-runtime og generert C med type- og verdiparitet.
- [x] Oppdater ende-til-ende-oversikten med faktisk status og gjenstaaende gap.

## Verifikasjon

- Full build fullfoert.
- `ctest --test-dir build --output-on-failure`: 134/134 bestatt, 1 forventet skip.
- Modelltest dekker `in { count: int64, $step: int64 = 1 }` og inferert int64
  gjennom binding, output og generert C.
- IR-paritet dekker automatisk int64 for usuffiksert verdi over `2^53`.
