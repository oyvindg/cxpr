Plan: Fase ut CXPR_OP_CALL_AST                                           
                                                                           
  Steg 1 — Case 1 & 2: Ukjent funksjon/producer = kompileringsfeil         
                                                                           
  I cxpr_ir_compile_node:                                                  
  - PRODUCER_ACCESS uten entry → sett CXPR_ERR_UNKNOWN_FUNCTION, returner  
  false                                                                    
  - FUNCTION_CALL uten entry → samme                                       
                                                                           
  Ingen endring i cxpr_ast_eval — den gjør allerede dette korrekt.         
                                                                           
  ---                                                                      
  Steg 2 — Case 3: Kompiler struct-param defined functions til IR          
                                                                           
  I dag faller defined_body med struct-params gjennom til CALL_AST.
  Løsningen er å utvide inline-mekanismen som allerede finnes for          
  scalar-only defined functions:
                                                                           
  Når body-en kompileres og treffer CXPR_NODE_FIELD_ACCESS med object = et 
  param-navn, slås param-navnet opp i substitusjonssrammen → det mappes til
   call-site-argumentet (alltid et CXPR_NODE_IDENTIFIER). Resultatet       
  kompileres som LOAD_FIELD "callsite_arg.field".
           
  Siden denne nøkkelen er syntetisert (ikke eid av AST-en), trenger den    
  eget opcode:
                                                                           
  Nytt opcode: CXPR_OP_LOAD_FIELD_SUBST — identisk med LOAD_FIELD i        
  semantikk, men name er heap-allokert og frigjøres i
  cxpr_ir_program_reset. (Samme mønster som CALL_PRODUCER_CONST allerede   
  gjør.)          
           
  Utenfor inline-dybdegrensen (≥8): fail med tydelig feil istedenfor       
  CALL_AST.
                                                                           
  Validering ved kompileringstid: struct-param-argumenter må være          
  CXPR_NODE_IDENTIFIER — ellers CXPR_ERR_SYNTAX.
                                                                           
  ---             
  Steg 3 — Fjern CXPR_OP_CALL_AST
                                                                           
  - Fjern fra enum i internal.h
  - Fjern handler i cxpr_ir_exec_typed                                     
  - Fjern ast-feltet fra cxpr_ir_instr-union og cxpr_ir_program            
  - Oppdater opcode_name, scalar_stack_effect, infer_fast_result_kind      
                                                                           
  ---                                                                      
  Steg 4 — Tester                                                          
                                                                           
  Legge til:
  - test_ir_compile_unknown_function_fails — ukjent funksjon gir           
  CXPR_ERR_UNKNOWN_FUNCTION                                                
  - test_ir_compile_unknown_producer_fails — ukjent producer gir samme     
  - test_ir_eval_struct_param_defined_function_matches_ast — f.eks.        
  dist2(p, q) gir samme resultat via IR og AST                             
  - test_ir_eval_struct_param_field_substitution — kall med annet          
  argumentnavn enn parameternavn                                           
                                                                           
  ---             
  Rekkefølge                                                               
                                                                           
  1. Skriv tester for de nye feiltilstandene (steg 4 delvis)
  2. Implementer case 1 & 2 som kompileringsfeil                           
  3. Legg til CXPR_OP_LOAD_FIELD_SUBST + struct-param inlining             
  4. Legg til IR-tester for struct-param defined functions                 
  5. Fjern CXPR_OP_CALL_AST og rydd opp                                    
                                                                           
  ---                                                                      
  Ting å passe på                                                          
                                                                           
  - cxpr_formula_compile antar at cxpr_compile kan returnere NULL —
  allerede håndtert, men bekreft at feilkoden propageres riktig            
  - Flat-key fallback i LOAD_FIELD-handleren i executor må samsvare med hva
   cxpr_eval_defined_function i eval.c gjør                                
  - cxpr_ir_infer_fast_result_kind: fjern
  cxpr_ir_defined_is_scalar_only-sjekken — etter steg 2 kompileres alle    
  defined functions, og result kind avgjøres av body-en