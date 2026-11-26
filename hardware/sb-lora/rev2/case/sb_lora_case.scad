// SB-LoRa rev 2 case: a two-part 3D-printed Springboard module shell (base + lid) for FDM (PLA/PETG,
// 0.4 mm nozzle). Draft: the values marked MEASURE are guesses until checked against a Visor and the parts.
//
// The in-slot part follows Handspring's Springboard Standard Module Inspection Document 80-0075-00 Rev B
// (docs/reference), checked against a Memplug module: 56.83 long, 53.83 wide at the connector end and
// 54.60 at the outer end, 5.96 thick, with the connector end 0.98 thinner on the label face for 12.1 mm,
// a keying slot in each side there, and the connector flush with the end. Numbers in brackets below
// are the drawing's balloon numbers.
//
// Frame: x across the slot, y along it with y = 0 at the connector face and the slot opening at y = slot_l,
// z into the Visor with z = 0 at the label face (flush with the Visor's back) and the inner face at mod_t.
//
// The board's top (component) side faces out (towards z = 0). The case splits at the board's top face:
// the base holds the board and the LiPo; the lid covers the parts and the radio boards and carries the
// antenna pocket. Both print flat on the split face.
//
// Render one part with: openscad -D 'part="base"' -o base.stl sb_lora_case.scad  (part = base|lid|both)

part = "both";
$fs = 0.2; $fa = 2;
e = 0.01;

// ---- module envelope (80-0075-00) ----
slot_l = 56.83;     // [31] length; the outer end is flush with the slot opening
mod_w0 = 53.83;     // [32] width at the connector end (plus draft)
mod_w1 = 54.60;     // [26] width at the outer end (minus draft)
slot_w = mod_w1;    // overall width, for the bump and x positions
mod_t = 5.96;       // [25] thickness (6.20 max with the optional fit pad)
end_t = 4.98;       // [14] thickness at the connector end, recessed on the label face
end_l = 12.1;       // [15] length of the thinner connector end
end_step = mod_t - end_t;
corner_r = 2.75;    // [5] plan radius of the connector-end corners
key_d = 2.18;       // [7][8] keying slot depth into each side
key_l = 7.75;       // [6][9] keying slot length from the connector face
key_h = 1.51;       // [12] keying slot height
key_z = end_step + 1.54;   // [11] slot from the recessed label face (the 1.93 [13] wall is on the inner face)
key_ch = 1.0;       // [4][10] 45-degree chamfer at the slot's inner end
lip_w0 = 49.35;     // [17] label face width at the connector end, inside the slot's lips
lip_w1 = 51.51;     // [20] label face width at the outer end
lip_h = 1.6;        // MEASURE: depth of the narrower label face (not on the drawing; nkorth's cover uses 1.6)
// Outer-end latch features [27]-[30]: a recess at each corner and a groove for the Visor's latch spring.
// The drawing doesn't say which face or how deep; MEASURE on a Memplug.
latch_x = 4.70;     // [28] groove centre from each side
latch_w = 3.58;     // [30] groove width (+4.00 allowed)
latch_l = 19;       // MEASURE: groove length from the outer end (about 19 mm on the drawing)
latch_r = [12.5, 0.90];  // corner recess width (MEASURE) and depth [29]
latch_d = 1.0;      // MEASURE: depth of the groove and recess into the inner face

// ---- board (from rev2/kicad/visormesh.kicad_pcb) ----
pcb_w = 46.8;                   // J1's ear width less the tab clearances, centred on J1
pcb_t = 0.8;                    // = BOARD_T in build_board.py (the MCS-068SMA1 gap)
pcb_in = 51.6;                  // in-slot section: J1's face 56.8 mm in from the opening, as on a Memplug
pcb_ext = 24.6;                 // extension past the slot opening
pcb_x = (slot_w - pcb_w) / 2;
pcb_y0 = slot_l - pcb_in;       // connector edge; J1's face is j1_d in front of it, flush with the end
pcb_y1 = slot_l + pcb_ext;      // far end, where the XIAO's USB-C is
clr = 0.25;                     // fit clearance around the board

// ---- heights ----
conn_z = end_step + 2.80;       // [3] contact centre line, 2.80 from the recessed label face
                                // The straddle J1 centres its contacts on the board's mid-plane.
lid_t = 1.0;        // lid over the in-slot parts
part_h = 1.25;      // tallest in-slot part (README: all 1.25 mm or less)
parts_y = 14;       // in-slot parts start this far from the connector face (C1 at about 15 mm)
radio_h = 5.0;      // MEASURE: tallest of the XIAO (with USB-C) and the Wio (with u.FL plug) above the board
under_h = 1.5;      // MEASURE: XIAO pads / Wio pin stubs below the board
lipo = [45, 22, 5]; // MEASURE: LiPo pouch x, y, thickness (REV2.md: 500-600 mAh is about 4 x 25 x 50)
wall = 1.6;         // bump walls and floors (4 perimeters at 0.4 mm)

pcb_top = conn_z - pcb_t / 2;   // split plane
pcb_bot = conn_z + pcb_t / 2;
assert(pcb_top - lid_t >= part_h + 0.2, "conn_z leaves no room for the in-slot parts under the lid");
assert(pcb_bot + clr <= mod_t - 0.8, "less than 0.8 mm of floor under the board");
assert(parts_y >= end_l, "in-slot parts reach the thinner connector end");

// ---- J1 body (measured MCS-068SMA1: 47.5 mm with ears, 44.5 without, 5.2 deep, 2.8 thick) ----
j1_w = 47.5 + 2 * clr; // body width with the mounting ears
j1_d = 5.2;         // body depth, all of it in front of the board edge (the board butts against its back face)
j1_tail = 2.0;      // tails reaching back over the board (rev 1's 1.83 mm pads)
j1_h = 2.8 + 0.2;   // body thickness, centred on the contacts (0.1 clearance: the inner wall is about 0.6 mm)

// ---- extension bump ----
bump_y1 = pcb_y1 + clr + 4.5;                       // room for the screw bosses past the board end
bump_top = pcb_top - radio_h - 0.6 - wall;          // outermost z (negative: stands proud of the Visor)
bump_bot = pcb_bot + under_h + lipo[2] + 0.5 + wall;
// USB-C on the XIAO, in the board end wall (x from the board, z from the XIAO's height)
usb_x = 14.58;
usb_z = pcb_top - 2.8;
usb_open = [12.6, 6.8];                             // plug overmould clearance
// M2 screws through the lid into the base, at the bump's far corners
screw_xy = [[3.6, bump_y1 - 3.0], [slot_w - 3.6, bump_y1 - 3.0]];
// flex 915 MHz FPC antenna, stuck to the lid's ceiling over the radio boards (MEASURE the one bought)
ant = [42, 10, 0.5];
ant_xy = [(slot_w - 42) / 2, pcb_y1 - 12];

module taper(w0, w1, z0, z1) {   // a slab across the slot, w0 wide at y = 0 and w1 at slot_l, centred
	hull() {
		translate([(slot_w - w0) / 2, 0, z0]) cube([w0, e, z1 - z0]);
		translate([(slot_w - w1) / 2, slot_l - e, z0]) cube([w1, e, z1 - z0]);
	}
}

module envelope() {
	difference() {
		union() {
			taper(mod_w0, mod_w1, lip_h, mod_t);
			taper(lip_w0, lip_w1, 0, lip_h + e);
		}
		// thinner connector end, on the label face
		translate([-1, -e, -1]) cube([slot_w + 2, end_l + e, end_step + 1]);
		for (s = [0, 1]) {
			xo = s == 0 ? (slot_w - mod_w0) / 2 : (slot_w + mod_w0) / 2;   // outer side at the connector end
			dx = s == 0 ? 1 : -1;
			// rounded connector-end corner
			translate([xo, 0, -1]) mirror([s, 0, 0]) difference() {
				translate([-1, -1, 0]) cube([corner_r + 1, corner_r + 1, mod_t + 2]);
				translate([corner_r, corner_r, -1]) cylinder(r = corner_r, h = mod_t + 4);
			}
			// keying slot, with its 45-degree lead-in at the inner end
			translate([xo, -1, key_z]) mirror([s, 0, 0]) translate([-1, 0, 0]) cube([key_d + 1, key_l + 1, key_h]);
			translate([xo, 0, key_z]) mirror([s, 0, 0])
				linear_extrude(height = key_h) polygon([[-1, key_l - key_ch - 1], [-1, key_l + key_ch + 1], [key_ch, key_l]]);
		}
		// outer-end latch groove and corner recess, on the inner face (MEASURE)
		for (s = [0, 1]) {
			xs = s == 0 ? 0 : slot_w;
			translate([xs, 0, 0]) mirror([s, 0, 0]) {
				translate([latch_x - latch_w / 2, slot_l - latch_l, mod_t - latch_d]) cube([latch_w, latch_l + 1, latch_d + 1]);
				translate([-1, slot_l - latch_r[1], mod_t - latch_d]) cube([latch_r[0] + 1, latch_r[1] + 1, latch_d + 1]);
			}
		}
	}
}

module bump() {               // the part past the slot opening: flat against the Visor, rounded beyond
	r = 2;
	hull() {
		translate([0, slot_l, bump_top]) cube([slot_w, r, bump_bot - bump_top]);
		for (x = [r, slot_w - r], z = [bump_top + r, bump_bot - r]) translate([x, bump_y1 - r, z]) sphere(r = r);
	}
}

module outer() {
	union() {
		envelope();
		bump();
	}
}

module cavities() {
	// board, with clearance
	translate([pcb_x - clr, pcb_y0 - clr, pcb_top - e]) cube([pcb_w + 2 * clr, pcb_y1 - pcb_y0 + 2 * clr, pcb_t + clr + e]);
	// in-slot parts under the lid; the lid bears on the board's long edges (0.8 mm). None sit in the
	// thinner connector end, which keeps its full lid there.
	translate([pcb_x + 0.8, parts_y, lid_t]) cube([pcb_w - 1.6, slot_l - parts_y + e, pcb_top - lid_t + e]);
	// radio boards (they start about 2.4 mm past the slot opening)
	translate([pcb_x + 0.8, slot_l + 0.5, bump_top + wall]) cube([pcb_w - 1.6, pcb_y1 - slot_l - 0.5 + clr, pcb_top - bump_top - wall + e]);
	// pins under the board, then the LiPo
	translate([pcb_x + 0.8, slot_l + 0.5, pcb_bot - e]) cube([pcb_w - 1.6, pcb_y1 - slot_l - 0.5 - 0.8, under_h + e]);
	translate([(slot_w - lipo[0]) / 2 - 0.3, pcb_y1 - lipo[1] - 0.8, pcb_bot + under_h - e]) cube([lipo[0] + 0.6, lipo[1] + 0.6, lipo[2] + 0.5 + e]);
	// J1 body and tails at the connector end, open to the Visor's connector
	translate([(slot_w - j1_w) / 2, -1, conn_z - j1_h / 2]) cube([j1_w, pcb_y0 + j1_tail + clr + 1, j1_h]);
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
