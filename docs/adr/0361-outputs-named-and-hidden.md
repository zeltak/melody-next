# ADR-0361: Outputs named, hidden, ordered and given icons by the user

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
- **Their order is the engine's discovery order.** Agents by name,
  renderers by UDN: the speaker played every day can be last.
- **A list of names reads slowly.** A picture of a bath or a sofa finds
  the speaker before its name is read.
- **All of it is about the engine's outputs, not one client's view.** A name
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
5. **The engine keeps the user's order** (`outputs.order.v1`):
   `outputs.list` and `outputs.changed` give the outputs in it, and those
   not in it -- a speaker found since -- after, as they were.
   `outputs.arrange {ids}` sets it; outputs it does not name keep their
   place after those it does, so an offline one is not lost.
6. **An output can be given an icon, by name**, from a fixed set: speaker,
   headphones, tv, computer, laptop, phone, car, radio, living-room,
   kitchen, bath, bedroom, office, garden, music. The engine keeps only the
   name (`outputs.configure {icon}`, empty for none) and gives it as
   `icon`; each client draws its own picture for it -- the names are the
   protocol, not the pictures. With none chosen, a client shows its own for
   what the output is. Melody draws them from Lucide icons built into the
   app (ISC; `packaging/icons/outputs`), the same on Linux and the Mac,
   which has no icon theme of its own, in the palette's text colour.
7. **Melody: Settings › Engines › Outputs of** the engine chosen there
   lists its outputs in the engine's order -- dragged, or moved up and
   down, into another -- each with **Show**, a name to edit, an **Icon**
   to choose, and what it calls itself and what it is (this engine, agent,
   UPnP speaker). The playback menu shows each speaker's icon.

## Consequences

- Clients that list outputs -- Melody, melody-cli, the phones, the
  DankMaterialShell widget -- show the names and leave out hidden outputs
  with no change of their own.
- An older client than level 37 shows the names and the order too: `name`
  was always what to show, in the order given.
- A client that draws no icons yet -- the phones -- shows none until it
  maps the names to its own pictures (SF Symbols, Material icons).

## Verification

- `output-names`: the rules alone -- a name given, a hidden output left
  out, one playing kept, `all` keeping every one; and against the engine's
  own methods: named, hidden from the list and the news but there for
  `all`, refused for an unknown output, too long a name or nothing to
  change, kept across a restart, and an empty name giving the output its
  own back with nothing left stored.
- `output-names` also: the user's order, with a speaker found since after
  it and an id no longer found passed over; an icon given, refused when
  unknown, kept across a restart, cleared with an empty name; the order kept
  across a restart.
- `output-names-widget`: against an engine with two outputs, Settings moves
  one down and up and drags one into first place -- the engine lists them
  so each time -- and gives one an icon from its picker; every icon name
  the engine accepts is drawn.
- `window-settings`: Settings › Engines shows this computer's engine's
  outputs; one renamed and unticked there is renamed and hidden by the
  engine, and given back its own name and shown again.
