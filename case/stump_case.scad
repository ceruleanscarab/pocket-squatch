// Pocket Squatch desk case for the Waveshare ESP32-S3-Touch-LCD-4B (Smart 86 Box version).
//
// Two prints:
//   part = "cradle" : the 86 Box slides in from the top; a pocket behind it holds a
//                     103450 LiPo (about 50 x 34 x 10 mm). Print it lying on its back.
//   part = "stump"  : a tree-stump base the cradle drops into, tilted back by `tilt`.
//   part = "both"   : the two together, for a look.
// Coordinates: x across the screen (0 = left edge seen from the front), y up, z back
// from the front face. Board sizes are from Waveshare's drawing (86.5 x 86.5 x 14 mm).

part = "both";

// The board
board = 86.5;  boardT = 14.0;
fit   = 0.3;                // clearance on each side; raise it if your printer runs tight
// The battery
batW = 52; batH = 36; batT = 10.5;   // a 103450 plus a little room
// The case
wall = 2.4; lip = 3.0; face = 2.0; back = 2.0;
tilt = 15;                  // degrees back from upright

inW = board + 2 * fit;
W   = inW + 2 * wall;
bayT = batT + 0.5;
D   = face + boardT + 2 * fit + bayT + back;
H   = inW + wall;           // open at the top

module cradle() {
    // Seen from the front, +x runs to the left, so mirror to put the buttons on the right.
    translate([W, 0, 0]) mirror([1, 0, 0]) cradleBody();
}

module cradleBody() {
    difference() {
        cube([W, H, D]);
        // The board slot, open at the top.
        translate([wall, wall, face]) cube([inW, H, boardT + 2 * fit]);
        // The window: the front lip covers just the bezel.
        translate([wall + lip, wall + lip, -1]) cube([inW - 2 * lip, H, face + 2]);
        // Right side: the two buttons, two USB-C ports and the mic/LED holes
        // (12 to 74 mm down from the board's top edge).
        translate([W - wall - 1, wall + board - 74, face - 0.5]) cube([wall + 2, 62, boardT + 2 * fit + 1]);
        // The tab at the middle of the board's bottom edge.
        translate([W / 2 - 8, wall - 1.2, face]) cube([16, 1.3, boardT + 2 * fit]);
        // Behind the board: the battery bay, reaching across to the battery socket
        // (low on the button side, seen from the front) so the plug and lead have room.
        bz = face + boardT + 2 * fit - 0.01;
        translate([wall + 10, wall + 10, bz]) cube([inW - 18, 46, bayT]);
        // Room behind the 20-pin header near the top.
        translate([wall + 24, wall + board - 18, bz]) cube([26, 9, 3]);
        // A finger notch at the top of the back, to push the board out.
        translate([W / 2, H, D]) rotate([0, 0, 0]) scale([1.6, 1, 1]) sphere(r = 9, $fn = 48);
    }
}

// The stump: a fat ring of bark with growth rings on top and a slanted slot.
stumpR = 66; stumpH = 34; slotDepth = 14;

module bark(r, h) {
    n = 40;
    linear_extrude(height = h, convexity = 4)
        polygon([for (i = [0:n * 4 - 1])
            let(a = 360 * i / (n * 4), bump = (i % 4 == 0) ? -2.2 : (i % 4 == 2 ? 0.6 : 0))
            [(r + bump + 1.4 * sin(a * 3)) * cos(a), (r + bump + 1.4 * sin(a * 3)) * sin(a)]]);
}

module stump() {
    difference() {
        union() {
            bark(stumpR, stumpH);
            translate([0, 0, -0.01]) bark(stumpR + 5, 4);   // a root flare at the foot
        }
        // Growth rings.
        for (r = [12:9:stumpR - 8])
            translate([0, 0, stumpH - 0.6]) difference() {
                cylinder(r = r + 0.7, h = 1, $fn = 96);
                cylinder(r = r - 0.7, h = 1, $fn = 96);
            }
        // The slot, tilted back.
        translate([0, 8, stumpH]) rotate([tilt, 0, 0])
            translate([-(W + 0.8) / 2, -(D + 0.8) / 2, -slotDepth]) cube([W + 0.8, D + 0.8, slotDepth + 40]);
    }
}

module placedCradle() {
    translate([0, 8, stumpH]) rotate([tilt, 0, 0]) translate([-W / 2, D / 2, -slotDepth])
        rotate([90, 0, 0]) cradle();
}

if (part == "cradle") translate([0, 0, D]) mirror([0, 0, 1]) cradle();   // back down on the bed
else if (part == "stump") stump();
else { color("#7a5232") stump(); color("#c9b28a") placedCradle(); }
