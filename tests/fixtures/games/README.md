# Game engine cxpr fixtures

These fixtures exercise entity-style state identity, where two objects may have
identical physical properties but must not share runtime state.

- `player_entity.cxpr` is a standalone stateful player model. It codegenerates
  with the current C backend.
- `projectile_entity.cxpr` is a standalone stateful projectile model. It
  codegenerates with the current C backend.
- `two_players_same_spawn.cxpr` is a parent scene fixture where two scoped
  players have the same spawn position but different `entity_id` values.
- `twin_projectiles_same_origin.cxpr` is a parent scene fixture where two scoped
  projectiles share origin and velocity but have different `projectile_id`
  values.

The scene fixtures exercise named per-instance inputs on child model calls and
`lifecycle = "scoped"` state routing. They verify independent state for
equal-position/equal-configuration entities while keeping `singleton` as the
implicit default lifecycle for existing models.
