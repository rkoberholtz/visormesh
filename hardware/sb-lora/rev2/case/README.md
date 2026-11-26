# SB-LoRa rev 2 case

A two-part 3D-printed Springboard module shell for the rev 2 board, for FDM printing (PLA or PETG, 0.4 mm nozzle). **Draft:** it hasn't been printed, and the values marked MEASURE in `sb_lora_case.scad` are guesses.

![Exploded view: lid above, base below](case-exploded.png)

## Parts

- `sb_lora_case.scad`: the parametric model (OpenSCAD).
- `sb-lora-case-base.stl`: holds the board, the LiPo and the slot latches.
- `sb-lora-case-lid.stl`: covers the parts and the radio boards, with a pocket for a flexible antenna.

Rebuild the STLs after changing a parameter:

```sh
openscad -D 'part="base"' -o sb-lora-case-base.stl sb_lora_case.scad
openscad -D 'part="lid"' -o sb-lora-case-lid.stl sb_lora_case.scad
```

## Shape

- **In the slot:** the outside is nkorth's public-domain Springboard slot cover ([Thingiverse 4776750](https://www.thingiverse.com/thing:4776750), CC0), so the rails, side latches and end tabs are taken from a part that is known to fit. Inside, the board sits with its contacts on `conn_z`, and J1's body is open at the connector end.
- **The bump:** past the slot opening, a rounded box holds the XIAO and the Wio above the board and the LiPo below it. Assembled, the case is 54 x 77 x 17.4 mm. The bump stands 4.7 mm proud of the Visor's back and reaches 5.3 mm past the slot floor.
- **Split:** the case splits at the board's top face. Both parts print flat on that face, with no supports. The lid bears on the board's long edges, and two M2 x 8 self-tapping screws at the far corners hold the parts together. The XIAO's USB-C is reached through the far end.
- **Antenna:** a pocket for a 42 x 10 mm 915 MHz FPC antenna on the lid's ceiling, over the radio boards, fed from the Wio's u.FL.

## Before printing the real case

1. **Print nkorth's blank first** (the files are on PalmDB and Thingiverse). If it doesn't fit the slot, this case won't either.
2. **Measure in the Visor's slot:** how deep the connector's contact centre line sits below the back surface (`conn_z`, now 3.35 mm), and where the connector face sits relative to the slot's end (the board's connector edge is 2.67 mm past the shell, from springSD's outline).
3. **J1 measured** (2026-11-26): the MCS-068SMA1 body is 47.5 mm wide with its ears, 5.2 mm deep and 2.8 mm thick, and sits wholly in front of the board edge, so with the edge 2.67 mm past the shell its front face is about 7.9 mm past the shell. The dry fit (J1 taped to a rev 1 board) seats in the slot, so the shell now has a nose that reaches forward and encloses J1, flush with its face, as on a Memplug module (56.8 mm long, connector face flush with its end, 5.2 mm thick there). Still to measure on the Memplug: the slots in the sides of its connector end (`notch`, now a 2.0 x 1.2 mm guess).
4. **Measure the radio boards and the LiPo:** the height of the XIAO with its USB-C and of the Wio with its u.FL plug above the carrier (`radio_h`), the pin stubs below it (`under_h`), and the cell (`lipo`).
5. **Check the Visor's top end:** the bump sits against it, so make sure it clears the IR window and the stylus.

## Print settings

0.2 mm layers, 4 perimeters (the walls are 1.6 mm), 20% infill. PETG gives the side latches more flex than PLA. Each part prints split face down.
