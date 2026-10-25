// Parametric ball chain pulley
// Based on "Parametric Ball Pulley" by zignig (http://www.thingiverse.com/thing:1322)
// Adapted from Unaimed's Thing 3147 for the measured curtain chain.
// Units: mm. Trial dimensions; verify bore, cord and chain connector fit.

/* [Chain] */
ball_diameter = 4.0;
// Actual centre-to-centre distance, measured on a straight chain.
ball_spacing = 6.0;
// Reference wheel bead-centre circle is approximately 60 mm.
reference_pitch_diameter = 60.0;
// Nearest integer count; exact 1:1 requires counting the original wheel slots.
ball_count = round(180 / asin(ball_spacing / reference_pitch_diameter));
// Clearance changes the pocket, NOT the chain pitch.
ball_clearance = 0.30;
// Provisional: replace with measured cord diameter + 0.3 to 0.5 mm.
link_channel_diameter = 1.8;

/* [Wheel] */
wheel_body_diameter = 62.0;
flange_diameter = 66.0;
flange_thickness = 1.5;
channel_width = 4.4;

/* [Shaft and hub] */
// Nominal single-flat D bore. Screw points at the +X shaft flat.
shaft_diameter = 5.0;
// Distance from the shaft flat to the opposite round surface, per motor drawing.
shaft_flat_height = 4.5;
// Zero preserves the motor nominal profile; tune only after a fit coupon.
shaft_clearance = 0.0;
boss_diameter = 20.0;
boss_height = 7.0;
bore_chamfer = 0.5;

/* [M3 captive nut] */
screw_diameter = 3.2;
nut_across_flats = 5.7;
nut_height = 2.6;
// Nut centre radius: 3.7 mm plastic from nut outer face to hub rim.
// Insert nut from the free end of the hub.
nut_center_radius = 5.0;

/* [Hidden] */
$fn = 96;
eps = 0.02;
// Pitch is a chord between adjacent seated bead centres.
wheel_diameter = ball_spacing / sin(180 / ball_count);
pitch_radius = wheel_diameter / 2;
pocket_diameter = ball_diameter + ball_clearance;
wheel_height = 2 * flange_thickness + channel_width;
pulley_height = wheel_height + boss_height;
shaft_flat_x = shaft_flat_height - shaft_diameter/2;
nut_outer_wall = boss_diameter/2 - nut_center_radius - nut_height/2;
bead_z = flange_thickness + channel_width / 2;
screw_z = wheel_height + boss_height / 2;
nut_corner_diameter = nut_across_flats / cos(30);

assert(ball_count >= 6 && ball_count == floor(ball_count), "Use an integer bead count >= 6");
assert(channel_width >= pocket_diameter, "Channel must fit the bead pockets");
assert(wheel_body_diameter > wheel_diameter, "Wheel body must extend beyond bead centres");
assert(wheel_body_diameter < wheel_diameter + pocket_diameter, "Pockets must open at the rim");
assert(flange_diameter > wheel_diameter + ball_diameter, "Flanges must extend beyond bead envelope");
assert(nut_center_radius - nut_height/2 > shaft_flat_x + shaft_clearance + 1, "Insufficient wall between nut and bore");
assert(nut_outer_wall >= 3, "Insufficient wall outside nut");

module bead_pockets() {
    for (i = [0:ball_count-1])
        rotate([0,0,i*360/ball_count])
            translate([pitch_radius,0,bead_z])
                union() {
                    // Spherical seating surface on the inner side.
                    sphere(d=pocket_diameter, $fn=32);
                    // Full-diameter radial opening eliminates the narrow entry lip.
                    rotate([0,90,0])
                        cylinder(h=wheel_body_diameter/2-pitch_radius+eps,
                                 d=pocket_diameter,$fn=32);
                }
}
// Chord-following cord groove, open to the rim; no broad swept wedges.
module cord_channels() {
    inner_radius = pitch_radius - link_channel_diameter/2/cos(180/ball_count);
    translate([0,0,bead_z-link_channel_diameter/2])
        difference() {
            cylinder(h=link_channel_diameter,r=wheel_body_diameter/2+eps);
            translate([0,0,-eps])
                cylinder(h=link_channel_diameter+2*eps,r=inner_radius,$fn=ball_count);
        }
}

module d_profile(clearance=shaft_clearance) {
    intersection() {
        circle(d=shaft_diameter+2*clearance);
        // Keep x <= flat position. The screw approaches from +X.
        translate([-shaft_diameter-clearance,-shaft_diameter-clearance])
            square([shaft_diameter+shaft_flat_x+2*clearance,2*(shaft_diameter+clearance)]);
    }
}

module shaft_hole() {
    translate([0,0,-eps])
        linear_extrude(height=pulley_height+2*eps) d_profile();
    // Conical lead-ins are clipped to the same D profile with a relieved flat.
    for (top=[false,true]) {
        translate([0,0,top ? pulley_height-bore_chamfer : -eps])
            intersection() {
                cylinder(h=bore_chamfer+eps,
                    d1=shaft_diameter+2*shaft_clearance+(top ? 0 : 2*bore_chamfer),
                    d2=shaft_diameter+2*shaft_clearance+(top ? 2*bore_chamfer : 0));
                linear_extrude(height=bore_chamfer+2*eps)
                    d_profile(shaft_clearance+bore_chamfer);
            }
    }
}
module captive_nut_and_screw() {
    translate([0,0,screw_z]) rotate([0,90,0])
        cylinder(h=boss_diameter/2+eps,d=screw_diameter);
    translate([nut_center_radius,0,screw_z]) rotate([0,90,0])
        cylinder(h=nut_height,d=nut_corner_diameter,center=true,$fn=6);
    // Open loading slot; nut is inserted parallel to the shaft from the hub end.
    translate([nut_center_radius,0,screw_z+(pulley_height-screw_z+eps)/2])
        cube([nut_height,nut_across_flats,pulley_height-screw_z+eps],center=true);
}

module pulley() {
    difference() {
        union() {
            cylinder(h=wheel_height,d=wheel_body_diameter);
            cylinder(h=flange_thickness,d=flange_diameter);
            translate([0,0,wheel_height-flange_thickness])
                cylinder(h=flange_thickness,d=flange_diameter);
            translate([0,0,wheel_height-eps])
                cylinder(h=boss_height+eps,d=boss_diameter);
        }
        bead_pockets();
        cord_channels();
        shaft_hole();
        captive_nut_and_screw();
    }
}

// Print flat with the shaft vertical and the hub facing upwards.
pulley();
echo(str("Pitch diameter: ",wheel_diameter," mm"));
echo(str("Overall diameter: ",flange_diameter," mm; height: ",pulley_height," mm"));
echo(str("Chain advance per revolution: ",ball_count*ball_spacing," mm"));


echo(str("D bore: diameter ",shaft_diameter,", flat-to-opposite-round ",shaft_flat_height," mm; clearance ",shaft_clearance));
echo(str("Plastic outside captive nut: ",nut_outer_wall," mm"));


echo(str("Bead count: ",ball_count,"; reference pitch diameter: ",reference_pitch_diameter," mm"));

