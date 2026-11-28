# ADR-0361: Outputs named as the user names them, and hidden

## Status

Accepted, 2026-11-28. Extends ADR-0228 (outputs), ADR-0235 (UPnP renderers)
and ADR-0325 (what each output was last set to).

## Context

- **An output is listed under the name it gives itself.** An agent's
  `--name`, the engine's own name, and for a UPnP renderer its friendly
  name -- "LS50 Wireless II-c4e3", "[TV] Samsung 7 Series (55)",
  "WiiM Mini-8A2F". Nothing in the protocol let the user call the living
  room's speakers "Living room".
- **An engine on a home network finds every renderer there.** A TV, a
  soundbar nobody plays music on, a guest's speaker: each is offered in
  every client's output menu, on every computer and phone, and the one
  wanted is hard to find among them.
- **Both are about the engine's outputs, not one client's view.** A name
  given on the desktop and lost on the phone would be two names for one
  speaker.

## Decision

1. **The engine keeps, per output, a name of the user's own and whether
   it is hidden** -- with what ADR-0325 keeps, in its state
   (`outputs.custom.v1`), by output id, so a renderer offline or not yet
   found keeps both.
2. **Every client is given the name.** `outputs.list` and `outputs.changed`
   give `name` as the user named it, `reported_name` as the output calls
   itself, and `hidden`. A name is at most 100 bytes, spaces around it
   trimmed; an empty one gives the output its own back.
3. **A hidden output is left out of `outputs.list` and `outputs.changed`,
   unless it is the one playing**: a client must always be able to show
   what plays. `outputs.list {all: true}` gives every output, for whoever
   names and hides them. A hidden output can still be chosen by its id.
4. **`outputs.configure {id, name?, hidden?}`** changes them, at once, and
   every client is told with `outputs.changed`.
5. **Melody: Settings › Engines › Outputs of** the engine chosen there
   lists its outputs, each with **Show** and a name to edit, beside what it
   calls itself and what it is (this engine, agent, UPnP speaker).

## Consequences

- Clients that list outputs -- Melody, melody-cli, the phones, the
  DankMaterialShell widget -- show the names and leave out hidden outputs
  with no change of their own.
- An older client than level 37 shows the names too: `name` was always
  what to show.

## Verification

- `output-names`: the rules alone -- a name given, a hidden output left
  out, one playing kept, `all` keeping every one; and against the engine's
  own methods: named, hidden from the list and the news but there for
  `all`, refused for an unknown output, too long a name or nothing to
  change, kept across a restart, and an empty name giving the output its
  own back with nothing left stored.
- `window-settings`: Settings › Engines shows this computer's engine's
  outputs; one renamed and unticked there is renamed and hidden by the
  engine, and given back its own name and shown again.
