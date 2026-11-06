const assert = require("node:assert/strict")
const test = require("node:test")
const { CxprFormatter } = require("../dist")

test("formats blocks and multiline expressions using legacy indentation", () => {
  const source = "model sample {   \ntype = \"strategy\"\n}\n\nentry =\nclose > open\nand volume > 0\n"
  assert.equal(new CxprFormatter().format(source),
    "model sample {\n    type = \"strategy\"\n}\n\nentry =\n    close > open\n    and volume > 0\n")
})

test("ignores delimiters inside strings and comments", () => {
  const source = "model sample {\nvalue = \"}\" # {\n/* {\n} */\nout value\n}\n"
  assert.equal(new CxprFormatter().format(source),
    "model sample {\n    value = \"}\" # {\n    /* {\n    } */\n    out value\n}\n")
})

test("preserves newlines and honors editor indentation options", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format("model x {\r\ntype = 1\r\n}"), "model x {\r\n    type = 1\r\n}")
  assert.equal(formatter.format("model x {\ntype = 1\n}\n", { tabSize: 2 }), "model x {\n  type = 1\n}\n")
  assert.equal(formatter.format("model x {\ntype = 1\n}\n", { insertSpaces: false }), "model x {\n\ttype = 1\n}\n")
})

test("record opening layout controls vertical versus inline formatting", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format([
    "shadow_pad_room = {",
    "left = 0.76 * shadow_pad_left, right = 0.76 * shadow_pad_right }",
    "|> hall(feedback=0.92)",
    ""
  ].join("\n")), [
    "shadow_pad_room = {",
    "    left = 0.76 * shadow_pad_left,",
    "    right = 0.76 * shadow_pad_right",
    "}",
    "    |> hall(feedback=0.92)",
    ""
  ].join("\n"))

  assert.equal(formatter.format([
    "shadow_pad_room = { left = 0.76 * shadow_pad_left,",
    "right = 0.76 * shadow_pad_right",
    "}",
    ""
  ].join("\n")),
  "shadow_pad_room = { left = 0.76 * shadow_pad_left, right = 0.76 * shadow_pad_right }\n")
})

test("indents pipeline continuations", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format([
    "stereo = compressed",
    "|> tape(sample_rate=$sample_rate, drive=1.18, mix=0.72)",
    "|> master(t=t, sample_rate=$sample_rate, gain=$master_gain)",
    ""
  ].join("\n")), [
    "stereo = compressed",
    "    |> tape(sample_rate=$sample_rate, drive=1.18, mix=0.72)",
    "    |> master(t=t, sample_rate=$sample_rate, gain=$master_gain)",
    ""
  ].join("\n"))
})

test("preserves a line break after a trailing pipeline operator", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format([
    "chamber_return = chamber_send |>",
    "chamber(sample_rate=$sample_rate, feedback=0.26)",
    ""
  ].join("\n")), [
    "chamber_return = chamber_send |>",
    "    chamber(sample_rate=$sample_rate, feedback=0.26)",
    ""
  ].join("\n"))
})

test("preserves explicit indentation of deeply nested records", () => {
  const formatter = new CxprFormatter()
  const source = [
    "snare = {",
    "    pattern = true,",
    "    hit = {",
    "        cycle = clock.tick % 7,",
    "        variation = cycle == 0 ? 1.12 : 0.86",
    "    },",
    "    roll = {",
    "        enabled = true,",
    "        hit = active and primary",
    "    }",
    "}",
    ""
  ].join("\n")
  assert.equal(formatter.format(source), source)
})

test("inline state records stay on one line", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format([
    "state { body_one_low = 0, body_one_band = 0,",
    "    body_two_low = 0, body_two_band = 0 }",
    ""
  ].join("\n")),
  "state { body_one_low = 0, body_one_band = 0, body_two_low = 0, body_two_band = 0 }\n")
})

test("inline parenthesized expressions stay on one line", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format([
    "hammer = hammer_contact * (0.038 * sin(2 * pi() * 7.11 * phase_now) +",
    "        0.022 * sin(2 * pi() * 10.37 * phase_now)) # hammer partials",
    ""
  ].join("\n")), [
    "hammer = hammer_contact * (0.038 * sin(2 * pi() * 7.11 * phase_now) + 0.022 * sin(2 * pi() * 10.37 * phase_now)) # hammer partials",
    ""
  ].join("\n"))
})

test("preserves multiline lists wrapped in calls", () => {
  const formatter = new CxprFormatter()
  const source = [
    "chamber_send = sum([",
    "    snare |> gain(level=0.26),",
    "    clap |> gain(level=0.18),",
    "    hats |> gain(level=0.08),",
    "    stabs |> gain(level=0.24)",
    "])",
    ""
  ].join("\n")
  assert.equal(formatter.format(source), source)
})

test("uses the first list value to select vertical call-list layout", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format([
    "gate_source = sum([ ",
    "snare |> mono(gain=0.54), clap |> mono(gain=0.81) ]) # gated layers",
    ""
  ].join("\n")), [
    "gate_source = sum([",
    "    snare |> mono(gain=0.54),",
    "    clap |> mono(gain=0.81)",
    "]) # gated layers",
    ""
  ].join("\n"))

  assert.equal(formatter.format([
    "gate_source = sum([snare |> mono(gain=0.54),",
    "clap |> mono(gain=0.81)])",
    ""
  ].join("\n")), "gate_source = sum([snare |> mono(gain=0.54), clap |> mono(gain=0.81)])\n")
})

test("a newline immediately after an opening parenthesis selects vertical arithmetic layout", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format([
    "hammer = hammer_contact * (",
    "0.038 * sin(phase_now) +",
    "0.022 * sin(phase_now)",
    ")",
    ""
  ].join("\n")), [
    "hammer = hammer_contact * (",
    "    0.038 * sin(phase_now) +",
    "    0.022 * sin(phase_now)",
    ")",
    ""
  ].join("\n"))
})

test("vertical arithmetic splits top-level operands that start on the same continuation line", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format([
    "hammer = hammer_contact * (",
    "    0.038 * sin(2 * pi() * 7.11 * phase_now) + 0.022 * sin(2 * pi() * 10.37 * phase_now)) # hammer partials",
    ""
  ].join("\n")), [
    "hammer = hammer_contact * (",
    "    0.038 * sin(2 * pi() * 7.11 * phase_now) +",
    "    0.022 * sin(2 * pi() * 10.37 * phase_now)",
    ") # hammer partials",
    ""
  ].join("\n"))
})

test("assignment opening layout controls inline versus vertical arithmetic", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format([
    "mode_five = 0.023 * sin(2 * pi() * 5.031 * phase_now) *",
    "    exp(-6.4 * decay_scale * note_age) # partial",
    ""
  ].join("\n")), [
    "mode_five = 0.023 * sin(2 * pi() * 5.031 * phase_now) * exp(-6.4 * decay_scale * note_age) # partial",
    ""
  ].join("\n"))

  assert.equal(formatter.format([
    "mode_five =",
    "0.023 * sin(2 * pi() * 5.031 * phase_now) *",
    "exp(-6.4 * decay_scale * note_age) # partial",
    ""
  ].join("\n")), [
    "mode_five =",
    "    0.023 *",
    "    sin(2 * pi() * 5.031 * phase_now) *",
    "    exp(-6.4 * decay_scale * note_age) # partial",
    ""
  ].join("\n"))
})

test("vertical assignments split additive operands already written on one rhs line", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format([
    "pluck =",
    "    0.58 * fundamental + 0.16 * left_player + 0.16 * right_player + 0.10 * second # voices",
    ""
  ].join("\n")), [
    "pluck =",
    "    0.58 * fundamental +",
    "    0.16 * left_player +",
    "    0.16 * right_player +",
    "    0.10 * second # voices",
    ""
  ].join("\n"))
})

test("named argument opening layout controls inline and hanging formatting", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format([
    "very_long_binding_name = atmospheric_pad(",
    "age=seq.age, frequency=seq.frequency,",
    "trigger=seq.trigger)",
    ""
  ].join("\n")), [
    "very_long_binding_name = atmospheric_pad(",
    "                             age=seq.age,",
    "                             frequency=seq.frequency,",
    "                             trigger=seq.trigger",
    "                         )",
    ""
  ].join("\n"))

  assert.equal(formatter.format([
    "voice = atmospheric_pad(age=seq.age,",
    "frequency=seq.frequency, trigger=seq.trigger)",
    ""
  ].join("\n")),
  "voice = atmospheric_pad(age=seq.age, frequency=seq.frequency, trigger=seq.trigger)\n")
})

test("positional and mixed arguments use the same call layout", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format([
    "result = blend(",
    "dry, wet, 0.5)",
    ""
  ].join("\n")), [
    "result = blend(",
    "             dry,",
    "             wet,",
    "             0.5",
    "         )",
    ""
  ].join("\n"))

  assert.equal(formatter.format([
    "result = blend(dry,",
    "wet, amount=0.5)",
    ""
  ].join("\n")), "result = blend(dry, wet, amount=0.5)\n")
})

test("normalizes repeated whitespace after out", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format("out   something\nout      { left, right }\nout\nleft, right\n"),
    "out something\nout { left, right }\nout left, right\n")
})

test("collapses multiline output name lists on save", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format([
    "out kick,",
    "snare,",
    "clap,",
    "hat,",
    "open_hat # public drum outputs",
    ""
  ].join("\n")), "out kick, snare, clap, hat, open_hat # public drum outputs\n")
})

test("joins a bare output across any number of blank lines", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format([
    "out",
    "",
    "",
    "kick, snare, clap, hat, open_hat # public drum outputs",
    ""
  ].join("\n")), "out kick, snare, clap, hat, open_hat # public drum outputs\n")
})

test("limits consecutive blank lines to one", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format([
    "model drums",
    "",
    "",
    "",
    "in trigger",
    "",
    "",
    "out trigger",
    ""
  ].join("\n")), [
    "model drums",
    "",
    "in trigger",
    "",
    "out trigger",
    ""
  ].join("\n"))
})

test("normalizes keyword spacing without changing strings or comments", () => {
  const formatter = new CxprFormatter()
  assert.equal(formatter.format([
    "model    demo",
    "in     value",
    "state    active = true",
    "assert      active",
    "signal = not     active   or    value   in    [1, 2]",
    "use alpha, beta    from    signals   as    source",
    "label = \"and    or    not    in\" # and    or",
    "out   signal",
    ""
  ].join("\n")), [
    "model demo",
    "in value",
    "state active = true",
    "assert active",
    "signal = not active or value in [1, 2]",
    "use alpha, beta from signals as source",
    "label = \"and    or    not    in\" # and    or",
    "out signal",
    ""
  ].join("\n"))
})
