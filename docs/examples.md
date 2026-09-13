# Cross-domain examples

The following examples use the same language and runtime boundary. Each host
supplies its own inputs and consumes the outputs; the calculations stay
domain-independent.

## Scientific calculation

```cxpr
model orbital_escape

in { gravitational_constant, mass, radius }

escape_velocity =
    sqrt(2 * gravitational_constant * mass / radius)

out escape_velocity
```

## Service-level objective

```cxpr
model slo_guard

in { latency_ms, error_rate, sample_valid }

$latency_budget_ms = 200
$max_error_rate = 0.01

healthy =
    sample_valid and
    within(latency_ms, 0, $latency_budget_ms) and
    error_rate <= $max_error_rate

out healthy
```

## Stateful game entity

```cxpr
model health_component

in { damage, healing, reset }
$maximum = 100

health := (
    reset
        ? $maximum
        : clamp(health - damage + healing, 0, $maximum)
) initial $maximum

alive = health > 0
out { health, alive }
```

## Robotics safety gate

```cxpr
model attitude_guard

in { roll, pitch, imu_ok }
$max_roll = 0.45
$max_pitch = 0.45

stable =
    imu_ok and
    abs(roll) <= $max_roll and
    abs(pitch) <= $max_pitch

out stable
```

## Trading signal

```cxpr
model threshold_cross

in { close, trend }
$entry_margin = 0.01

entry = close > trend * (1 + $entry_margin)
exit = close < trend

out { entry, exit }
```

These examples deliberately keep data acquisition and actions outside the
model. A scientific host supplies constants, an operations host supplies
telemetry, a game supplies entity-local inputs, a robot supplies sensor
values, and a trading host supplies market series. Each host decides how to
consume the outputs.

Larger tested examples are available in:

- [`examples/`](../examples) for integration-oriented C and Markdown examples;
- [`tests/fixtures/`](../tests/fixtures) for `.cxpr` models covering robotics,
  games, scientific calculations, imports, records, state, and trading.
