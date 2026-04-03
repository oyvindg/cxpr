1. Høy prioritet: gjør kvalitetssikringen strengere i bygg/CI.
     På CMakeLists.txt:22 settes i praksis bare to kompilatorflagg for biblioteket, og prosjektet bygges ikke med varsler som standard. Når jeg bygget med -Wall -Wextra -Wpedantic
     -Werror, feilet prosjektet umiddelbart på ubrukte interne funksjoner i src/lexer.c og src/eval.c:143. Det betyr at kode kan råtne uten at CI stopper det. Første utbedring bør være
     en egen CXPR_STRICT/CI-profil med warnings-as-errors og sanitizer-jobber.
  2. Høy prioritet: stabiliser sanitizer/test-oppsettet.
     build kjører grønt, men build-asan er i en ødelagt tilstand: cmake --build build-asan --target test fant ikke registrerte testbinærer. Testregistreringen i tests/CMakeLists.txt:1
     er enkel og grei, så problemet er sannsynligvis at repoet mangler en robust, reproduserbar måte å konfigurere egne ASAN/UBSAN-builds på. Dette bør formaliseres i CMake preset
     eller dokumentert CI-jobb, ellers mister dere en viktig feilsøkingslinje.
  3. Høy prioritet: reduser kompleksiteten i IR-modulen.
     src/ir.c er på 2652 linjer og håndterer både kompilering, optimalisering, cache-nøkler, kall for definerte funksjoner og runtime-evaluering. Det er et klassisk tegn på at modulen
     src/internal.h:66 er stor og inneholder lexer-, AST-, registry- og IR-nære detaljer i én header. Det øker kobling mellom moduler og gjør det lettere å introdusere skjulte
     sideeffekter. Å splitte den i mindre interne headere vil gjøre endringer tryggere og kompilering mer presis.
  5. Middels prioritet: rydd ut død eller halvferdig cachelogikk.
     I src/eval.c:143 ligger flere statiske hjelpefunksjoner for cache/oppslag som i praksis ikke brukes, noe -Werror bekreftet. Det tyder på enten ufullført optimalisering eller
     gammel kode som er blitt liggende. Slike rester gjør evaluatoren vanskeligere å stole på og vanskeligere å lese.
  6. Middels prioritet: reduser intern testkobling.
     Fem tester må inkludere src direkte via tests/CMakeLists.txt:3. Det er forståelig for lave nivåer, men det betyr også at deler av testpakken er tett koblet til intern struktur.
     Vurder å flytte noen verifikasjoner opp på offentlig API-nivå eller etablere et smalt internt testgrensesnitt i stedet for bred tilgang til src.
  7. Lavere prioritet: rydd repo-hygiene og dokumenter anbefalt testkommando.
     git status viser slettede planfiler og nye urørte filer, og direkte ctest feiler i dette miljøet fordi PATH peker til en ødelagt Python-wrapper, mens cmake --build build --target
     test fungerer. Det bør stå eksplisitt i README eller CI-dokumentasjon hvilken kommando som er autoritativ for testkjøring.

  Anbefalt rekkefølge

  1. Innfør streng buildprofil i CMake/CI.
  2. Fiks reproducerbar ASAN/UBSAN-build.
  3. Rydd død kode som blokkert av warning-build.
  4. Del opp ir.c.
  5. Del opp internal.h.
  6. Stram inn testarkitekturen der internkoblingen er unødvendig.

  Verifisering

  Jeg verifiserte at ordinær testkjøring passerer med cmake --build build --target test, at streng warning-build feiler, og at eksisterende
  build-asan ikke er pålitelig i nåværende
  repo-tilstand.