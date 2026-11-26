# SB-LoRa rev 2 case

A two-part 3D-printed Springboard module shell for the rev 2 board, for FDM printing (PLA or PETG, 0.4 mm nozzle). **Draft:** it hasn't been printed, and the values marked MEASURE in `sb_lora_case.scad` are guesses.

![Exploded view: lid above, base below](case-exploded.png)

## Parts

- `sb_lora_case.scad`: the parametric model (OpenSCAD).
- `sb-lora-case-base.stl`: holds the board and the LiPo.
- `sb-lora-case-lid.stl`: covers the parts and the radio boards, with a pocket for a flexible antenna.

Rebuild the STLs after changing a parameter:

```sh
openscad -D 'part="base"' -o sb-lora-case-base.stl sb_lora_case.scad
openscad -D 'part="lid"' -o sb-lora-case-lid.stl sb_lora_case.scad
```

## Shape

- **In the slot:** the outside follows Handspring's *Springboard Standard Module Inspection Document* 80-0075-00 Rev B (`docs/reference/handspring_80-0075-00_RevB_standard_module_inspection.pdf`), which agrees with a Memplug module. It is 56.83 mm long, 53.83 mm wide at the connector end and 54.60 mm at the outer end, and 5.96 mm thick. For its last 12.1 mm the connector end is 4.98 mm thick, recessed 0.98 mm on the label face. There it has R2.75 corners and a keying slot in each side: 2.18 deep, 1.51 high, 7.75 long, 1.54 below the recessed label face, with a 1 mm lead-in. The label face narrows (49.35 to 51.51 mm) for the slot's lips. J1's face is flush with the end, with its contacts 2.80 mm below the recessed label face (3.78 mm below the Visor's back), on the board's mid-plane.
- **The bump:** past the slot opening, a box, flat against the Visor and rounded beyond, holds the XIAO and the Wio above the board and the LiPo below it. Assembled, the case is 54.6 x 86.2 x 16.6 mm. The bump stands 3.8 mm proud of the Visor's back.
- **Split:** the case splits at the board's top face. Both parts print flat on that face, with no supports. The lid bears on the board's long edges, and two M2 x 8 self-tapping screws at the far corners hold the parts together. The XIAO's USB-C is reached through the far end.
- **Antenna:** a pocket for a 42 x 10 mm 915 MHz FPC antenna on the lid's ceiling, over the radio boards, fed from the Wio's u.FL.

## Before printing the real case

1. **Check against a Memplug** the parts the drawing leaves open: how deep the narrower label face is (`lip_h`, 1.6 mm from nkorth's cover), and the outer-end latch features: which face the 3.58 mm grooves 4.70 mm from each side and the 0.90 mm corner recesses are on, and how deep and long they are (`latch_*`, now 1.0 mm deep into the inner face).
2. **Thin walls:** at the connector end the wall under J1 is about 0.7 mm, and between J1 and each keying slot about 0.7 mm. Print a test of the first 15 mm to check they come out and that J1 fits.
3. **Measure the radio boards and the LiPo:** the height of the XIAO with its USB-C and of the Wio with its u.FL plug above the carrier (`radio_h`), the pin stubs below it (`under_h`), and the cell (`lipo`).
4. **Check the Visor's top end:** the bump sits against it, so make sure it clears the IR window and the stylus.

## Print settings

0.2 mm layers, 4 perimeters (the bump walls are 1.6 mm), 20% infill. Each part prints split face down.
