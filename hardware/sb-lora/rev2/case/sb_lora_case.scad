// SB-LoRa rev 2 case: a two-part 3D-printed Springboard module shell (base + lid) for FDM (PLA/PETG,
// 0.4 mm nozzle). Draft: the values marked MEASURE are guesses until checked against a Visor and the parts.
//
// Frame (the same as the slot cover this is based on): x across the slot, y along it with y = 0 at the
// connector end and the slot opening at y = slot_l, z into the Visor with z = 0 at the Visor's back surface
// and the slot floor at z = slot_h. The in-slot envelope (rails, latches, end tabs) is nkorth's public-domain
// "Handspring Visor Springboard slot cover" (Thingiverse 4776750, CC0), unchanged except for the grip.
//
// The board's top (component) side faces out (towards z = 0). The case splits at the board's top face:
// the base holds the board, the LiPo and the slot latches; the lid covers the parts and the radio boards
// and carries the antenna pocket. Both print flat on the split face.
//
// Render one part with: openscad -D 'part="base"' -o base.stl sb_lora_case.scad  (part = base|lid|both)

part = "both";
$fs = 0.2; $fa = 2;
e = 0.01;

// ---- slot envelope (nkorth) ----
slot_w = 54;        // width below the slot's lips
slot_l = 47.8;      // depth of the slot from the connector end to the opening
slot_h = 7.5;       // Visor back surface to slot floor
lip_h = 1.6;        // the slot is narrower (lip_w) for its first lip_h
lip_w = 49.8;

// ---- board (from rev2/kicad/visormesh.kicad_pcb) ----
pcb_w = 50.43;
pcb_t = 0.8;                    // = BOARD_T in build_board.py (the MCS-068SMA1 gap)
pcb_in = 50.47;                 // in-slot section
pcb_ext = 24.6;                 // extension past the slot opening
pcb_x = (slot_w - pcb_w) / 2;
pcb_y0 = slot_l - pcb_in;       // connector edge (negative: J1 reaches past the shell into the Visor's connector)
pcb_y1 = slot_l + pcb_ext;      // far end, where the XIAO's USB-C is
clr = 0.25;                     // fit clearance around the board

// ---- heights (MEASURE) ----
conn_z = 3.35;      // MEASURE: depth of the Visor connector's contact centre line below the back surface.
                    // The straddle J1 centres its contacts on the board's mid-plane.
lid_t = 1.0;        // lid over the in-slot parts
part_h = 1.25;      // tallest in-slot part (README: all 1.25 mm or less)
radio_h = 5.0;      // MEASURE: tallest of the XIAO (with USB-C) and the Wio (with u.FL plug) above the board
under_h = 1.5;      // MEASURE: XIAO pads / Wio pin stubs below the board
lipo = [45, 22, 5]; // MEASURE: LiPo pouch x, y, thickness (REV2.md: 500-600 mAh is about 4 x 25 x 50)
wall = 1.6;         // bump walls and floors (4 perimeters at 0.4 mm)

pcb_top = conn_z - pcb_t / 2;   // split plane
pcb_bot = conn_z + pcb_t / 2;
assert(pcb_top - lid_t >= part_h + 0.2, "conn_z leaves no room for the in-slot parts under the lid");
assert(pcb_bot <= slot_h - 0.8, "conn_z leaves less than 0.8 mm of floor under the board");

// ---- J1 body (measured MCS-068SMA1: 47.5 mm with ears, 44.5 without, 5.2 deep, 2.8 thick) ----
j1_w = 47.5 + 2 * clr; // body width with the mounting ears
j1_d = 5.2;         // body depth, all of it in front of the board edge (the board butts against its back face)
j1_tail = 2.0;      // tails reaching back over the board (rev 1's 1.83 mm pads)
j1_h = 2.8 + 2 * clr;  // body thickness, centred on the contacts

// ---- extension bump ----
bump_y1 = pcb_y1 + clr + 4.5;                       // room for the screw bosses past the board end
bump_top = pcb_top - radio_h - 0.6 - wall;          // outermost z (negative: stands proud of the Visor)
bump_bot = pcb_bot + under_h + lipo[2] + 0.5 + wall;
// USB-C on the XIAO, in the board end wall (x from the board, z from the XIAO's height)
usb_x = 13.25;
usb_z = pcb_top - 2.8;
usb_open = [12.6, 6.8];                             // plug overmould clearance
// M2 screws through the lid into the base, at the bump's far corners
screw_xy = [[3.6, bump_y1 - 3.0], [slot_w - 3.6, bump_y1 - 3.0]];
// flex 915 MHz FPC antenna, stuck to the lid's ceiling over the radio boards (MEASURE the one bought)
ant = [42, 10, 0.5];
ant_xy = [(slot_w - 42) / 2, pcb_y1 - 12];

module slot_envelope() {      // nkorth's slot cover, minus its grip
	tab_w = 12.4; tab_l = 1.4; tab_h = 1.5; relief_l = 12; flex_th = 0.8;
	difference() {
		union() {
			difference() {
				union() {
					translate([2, 0, 0]) cube([lip_w, slot_l - 2, slot_h]);
					translate([0, 0, lip_h]) cube([slot_w, slot_l, slot_h - lip_h]);
					difference() {
						translate([0, 0, slot_h - e]) {
							cube([tab_w, tab_l, tab_h + e]);
							translate([slot_w - tab_w, 0, 0]) cube([tab_w, tab_l, tab_h + e]);
						}
						translate([tab_w, -e, slot_h + tab_h]) rotate([-90, 0, 0]) cylinder(r = tab_h, h = tab_l + 2 * e);
						translate([slot_w - tab_w, -e, slot_h + tab_h]) rotate([-90, 0, 0]) cylinder(r = tab_h, h = tab_l + 2 * e);
					}
					translate([0, 4.7, 4.9]) {
						translate([1, 3, 0]) cylinder(r = 1.5, h = 2.6);
						translate([slot_w - 1, 3, 0]) cylinder(r = 1.5, h = 2.6);
					}
				}
				translate([2, -e, lip_h]) rotate([0, -135, 0]) cube([3, slot_l + 2 * e, 3]);
				translate([slot_w - 2, -e, lip_h]) rotate([0, 45, 0]) cube([3, slot_l + 2 * e, 3]);
			}
			translate([2 + e, 0, 0]) linear_extrude(height = 3) polygon([[0, 0], [-1, 0], [0, 8]]);
			translate([lip_w + 2 - e, 0, 0]) linear_extrude(height = 3) polygon([[0, 0], [1, 0], [0, 8]]);
		}
		translate([-e, 0, slot_h + tab_h]) rotate([183, 0, 0]) cube([slot_w + 2 * e, 2, slot_h + tab_h + 1]);
		translate([flex_th, 1.4, slot_h - 4]) cube([1, relief_l, 4 + e]);
		translate([-e, 1.4, slot_h - 4]) cube([1 + flex_th + e, relief_l, 1]);
		translate([slot_w - flex_th - 1, 1.4, slot_h - 4]) cube([1, relief_l, 4 + e]);
		translate([slot_w - 1 - flex_th + e, 1.4, slot_h - 4]) cube([1 + flex_th + e, relief_l, 1]);
		translate([0, 6.2, slot_h - 2.6]) rotate([0, -135, 0]) cube([1, 3, 1]);
		translate([slot_w, 6.2, slot_h - 2.6]) rotate([0, 45, 0]) cube([1, 3, 1]);
	}
}

module bump() {               // the part past the slot opening, rounded on its outer edges
	r = 2;
	hull() for (x = [r, slot_w - r], y = [slot_l, bump_y1 - r], z = [bump_top + r, bump_bot - r])
		translate([x, y, z]) sphere(r = r);
}

module outer() {
	union() {
		slot_envelope();
		// fill between the envelope and the bump so the two parts join
		translate([0, slot_l - 2, bump_top + 2]) cube([slot_w, 2.5, bump_bot - bump_top - 4]);
		bump();
	}
}

module cavities() {
	// board, with clearance
	translate([pcb_x - clr, pcb_y0 - 5, pcb_top - e]) cube([pcb_w + 2 * clr, pcb_y1 - pcb_y0 + clr + 5, pcb_t + clr + e]);
	// in-slot parts under the lid; the lid bears on the board's long edges (0.8 mm)
	translate([pcb_x + 0.8, pcb_y0 - 5, lid_t]) cube([pcb_w - 1.6, slot_l - pcb_y0 + 5, pcb_top - lid_t + e]);
	// radio boards (they start about 2.4 mm past the slot opening)
	translate([pcb_x + 0.8, slot_l + 0.5, bump_top + wall]) cube([pcb_w - 1.6, pcb_y1 - slot_l - 0.5 + clr, pcb_top - bump_top - wall + e]);
	// pins under the board, then the LiPo
	translate([pcb_x + 0.8, slot_l + 0.5, pcb_bot - e]) cube([pcb_w - 1.6, pcb_y1 - slot_l - 0.5 - 0.8, under_h + e]);
	translate([(slot_w - lipo[0]) / 2 - 0.3, pcb_y1 - lipo[1] - 0.8, pcb_bot + under_h - e]) cube([lipo[0] + 0.6, lipo[1] + 0.6, lipo[2] + 0.5 + e]);
	// J1 body and tails at the connector end, open to the Visor's connector
	translate([(slot_w - j1_w) / 2, pcb_y0 - j1_d - clr, conn_z - j1_h / 2]) cube([j1_w, j1_d + j1_tail + 2 * clr, j1_h]);
	// USB-C opening in the far wall
	translate([usb_x - usb_open[0] / 2, pcb_y1, usb_z - usb_open[1] / 2]) cube([usb_open[0], bump_y1 - pcb_y1 + 1, usb_open[1]]);
	// antenna pocket in the lid's ceiling
	translate([ant_xy[0], ant_xy[1], bump_top + wall - ant[2]]) cube([ant[0], ant[1], ant[2] + e]);
}

module screws(lid) {          // M2 x 8 self-tappers: clearance in the lid, pilot in the base
	for (p = screw_xy) translate([p[0], p[1], 0]) {
		if (lid) {
			translate([0, 0, bump_top - e]) cylinder(d = 2.3, h = pcb_top - bump_top + 2 * e);
			translate([0, 0, bump_top - e]) cylinder(d = 4.0, h = 1.2 + e);   // head counterbore
		} else
			translate([0, 0, pcb_top - e]) cylinder(d = 1.7, h = bump_bot - wall - pcb_top);
	}
}

module half(lid) {
	intersection() {
		difference() { outer(); cavities(); screws(lid); }
		if (lid) translate([-10, -20, -50]) cube([slot_w + 20, 150, 50 + pcb_top]);
		else translate([-10, -20, pcb_top]) cube([slot_w + 20, 150, 50]);
	}
}

if (part == "base") half(false);
else if (part == "lid") half(true);
else { half(false); translate([0, 0, -12]) half(true); }   // exploded view
