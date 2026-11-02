# Changelog: PUMP for Digitone

**This is the Digitone mk1 / Digitone Keys version** (`pump`). For the Digitakt, see
[PUMP for Digitakt](https://github.com/ClaidArremer/digitakt-pump).

Each version's files are in `elemods/` (`pump-<version>-os1.44.elemod` and `-os1.43`); the newest is there,
older ones are in the history of this repo.

## 2.11 (2026-11-01)

- **REL reads the other way**: more is slower, as the stock compressor's REL (0 PUMP, fast; 127 LEVEL, slow),
  as on PUMP for Digitakt 1.4. Patterns saved before sound as they did; their REL now shows as 127 minus its
  old number.

## 2.10 (2026-10-23)

- One step a detent; press and turn moves from multiple to multiple of 8, as the stock pages do.

## 2.9 (2026-10-19)

- The page animates at 20 frames a second again (2.8 drew at 15, and 5 when idle).

## 2.8 (2026-10-18)

- The first release: an SP-303 Vinyl Sim-style levelling compressor on the Digitone's master output, with its
  own master page: PMP, ATK, OUT, RAT, CMP, REL, WRM and MIX, the turning record and tonearm, the
  gain-reduction meter and graph, settings per pattern, and SETTINGS > PUMP ANIMATION.
- Versions 1.0 to 2.7 were development builds, not released.
