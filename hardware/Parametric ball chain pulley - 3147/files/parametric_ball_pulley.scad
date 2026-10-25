// Parametric ball chain pulley
// Based on "Parametric Ball Pulley" by zignig (http://www.thingiverse.com/thing:1322)
// Adapted from Unaimed's Thing 3147 for the measured curtain chain.
// Units: mm. Trial dimensions; verify bore, cord and chain connector fit.

/* [Chain] */
ball_diameter = 4.0;
// Actual centre-to-centre distance, measured on a straight chain.
ball_spacing = 6.0;
// Select the largest integer bead count within the measured load torque limit.
max_pull_kgf = 0.85;
load_torque_limit = 0.15; // N.m; excludes acceleration and unmeasured friction.
max_pull_force = max_pull_kgf * 9.80665;
max_pitch_diameter = 2000 * load_torque_limit / max_pull_force;
ball_count = floor(180 / asin(ball_spacing / max_pitch_diameter));
// Clearance changes the pocket, NOT the chain pitch.
ball_clearance = 0.60;
// Axial pocket height is independent of bead diameter and centre spacing.
// Extra headroom tolerates the observed printed roof sag.
ball_pocket_height = 5.2;
// Measured cord diameter with independent printing clearance.
link_diameter = 1.0;
link_clearance = 0.20;
link_channel_diameter = link_diameter + link_clearance;

/* [Wheel] */
// Trim 0.5 mm from the flange OD so the M3 bore clears the wheel at 15 mm total height.
flange_diameter = ceil(ball_spacing / sin(180 / ball_count) + 6.0) - 0.5;
flange_thickness = 1.0;
channel_width = 4.4;
// Angle of the continuous annular cord groove, measured from horizontal.
cord_roof_angle = 30;

/* [Shaft and hub] */
// Nominal single-flat D bore. Screw points at the +X shaft flat.
shaft_diameter = 5.0;
// Distance from the shaft flat to the opposite round surface, per motor drawing.
shaft_flat_height = 4.5;
// Zero preserves the motor nominal profile; tune only after a fit coupon.
shaft_clearance = 0.0;
boss_diameter = 20.0;
overall_height_limit = 15.0;
boss_height_target = 6.8; // Keep the short hub; do not grow it when wheel becomes thinner.
// Hub height is calculated after wheel dimensions.
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
estimated_load_torque = max_pull_force * pitch_radius / 1000;
assert(estimated_load_torque <= load_torque_limit, "Load torque exceeds requested limit");
pocket_diameter = ball_diameter + ball_clearance;
pocket_height = ball_pocket_height;
// Continuous 30-degree cord groove reaches the outer flange edge around 360 degrees.
// Keep the flanges flat; preserve their thickness by moving bead centres upwards
// and retaining a symmetric wheel, instead of cutting through either flange.
cord_chord_radius = pitch_radius*cos(180/ball_count);
cord_root_radius = cord_chord_radius - link_channel_diameter/2;
cord_exit_radius = flange_diameter/2 + eps;
cord_half_height_root = link_channel_diameter/2/cos(cord_roof_angle)
    + (cord_root_radius-cord_chord_radius)*tan(cord_roof_angle);
cord_half_height_exit = link_channel_diameter/2/cos(cord_roof_angle)
    + (cord_exit_radius-cord_chord_radius)*tan(cord_roof_angle);
bead_z = max(flange_thickness+max(channel_width,pocket_height)/2,
             flange_thickness+cord_half_height_exit);
wheel_height = 2*bead_z;
boss_height = min(boss_height_target, overall_height_limit - wheel_height);
pulley_height = wheel_height + boss_height;
assert(boss_height >= 3.0, "Wheel leaves insufficient space for hub");
shaft_flat_x = shaft_flat_height - shaft_diameter/2;
nut_outer_wall = boss_diameter/2 - nut_center_radius - nut_height/2;
// Orient the hex nut with flat faces at top/bottom; embed its lower part in wheel.
nut_top_recess = 0.30;
screw_z = pulley_height - nut_across_flats/2 - nut_top_recess;
nut_corner_diameter = nut_across_flats / cos(30);

assert(ball_count >= 6 && ball_count == floor(ball_count), "Use an integer bead count >= 6");
assert(wheel_height-2*flange_thickness >= max(pocket_diameter,pocket_height)-0.001,
       "Wheel must retain flange thickness above and below enlarged pockets");
assert(flange_diameter > wheel_diameter + ball_diameter, "Flanges must extend beyond bead envelope");
assert(nut_center_radius - nut_height/2 > shaft_flat_x + shaft_clearance + 1, "Insufficient wall between nut and bore");
assert(nut_outer_wall >= 3, "Insufficient wall outside nut");

module bead_pockets() {
    for (i = [0:ball_count-1])
        rotate([0,0,i*360/ball_count])
            translate([pitch_radius,0,bead_z])
                scale([1,1,pocket_height/pocket_diameter]) union() {
                    // Ellipsoidal inner seat: extra axial height without changing bead pitch.
                    sphere(d=pocket_diameter, $fn=32);
                    // Radial entry has the same elliptical cross section as the enlarged pocket.
                    rotate([0,90,0])
                        cylinder(h=flange_diameter/2-pitch_radius+eps,
                                 d=pocket_diameter,$fn=32);
                }
}
// A continuous 360-degree annular V groove eliminates gaps between chord cutters.
// The circular inner radius is inside every chord's cord envelope. The existing
// individual spherical seats are cut separately and keep their inner seat surfaces.
module cord_channels() {
    // Equivalent to revolving the V profile, expressed with explicit frusta
    // so both OpenCSG preview and CGAL show the complete inner wheel correctly.
    slope_height = cord_half_height_exit-cord_half_height_root;
    translate([0,0,bead_z]) difference() {
        translate([0,0,-cord_half_height_exit])
            cylinder(h=2*cord_half_height_exit,r=cord_exit_radius,$fn=ball_count*16);
        union() {
            translate([0,0,-cord_half_height_root-eps])
                cylinder(h=2*cord_half_height_root+2*eps,r=cord_root_radius,$fn=ball_count*16);
            translate([0,0,cord_half_height_root])
                cylinder(h=slope_height+eps,r1=cord_root_radius,
                         r2=cord_exit_radius+eps/tan(cord_roof_angle),$fn=ball_count*16);
            translate([0,0,-cord_half_height_exit-eps])
                cylinder(h=slope_height+eps,r1=cord_exit_radius+eps/tan(cord_roof_angle),
                         r2=cord_root_radius,$fn=ball_count*16);
        }
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
    // Complete circular 45-degree lead-ins, NOT clipped to a D profile.
    // Extend to the flat tangent radius so the transition into the D bore
    // is continuous instead of leaving an internal shoulder on the flat.
    lead_depth = shaft_diameter/2 - shaft_flat_x + bore_chamfer;
    lead_small_d = 2*(shaft_flat_x + shaft_clearance);
    lead_large_d = shaft_diameter + 2*shaft_clearance + 2*bore_chamfer;
    translate([0,0,-eps])
        cylinder(h=lead_depth+eps,d1=lead_large_d+2*eps,d2=lead_small_d);
    translate([0,0,pulley_height-lead_depth])
        cylinder(h=lead_depth+eps,d1=lead_small_d,d2=lead_large_d+2*eps);
}
module captive_nut_and_screw() {
    translate([0,0,screw_z]) rotate([0,90,0])
        // The complete screw bore is above the wheel, inside the shortened hub.
        cylinder(h=boss_diameter/2+eps,d=screw_diameter);
    translate([nut_center_radius,0,screw_z]) rotate([0,90,0]) rotate([0,0,30])
        cylinder(h=nut_height,d=nut_corner_diameter,center=true,$fn=6);
    // Open loading slot; nut is inserted parallel to the shaft from the hub end.
    translate([nut_center_radius,0,screw_z+(pulley_height-screw_z+eps)/2])
        cube([nut_height,nut_corner_diameter,pulley_height-screw_z+eps],center=true);
}

module pulley() {
    difference() {
        union() {
            // Fill material out to flange edge: the local V cutter now forms a
            // continuous slope instead of exposing a vertical shoulder.
            cylinder(h=wheel_height,d=flange_diameter);

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
show_section = false;
show_notch_slice = false;
slice_through_bead = false;
if (show_notch_slice) {
    intersection() {
        rotate([0,0,slice_through_bead ? 0 : -180/ball_count]) pulley();
        translate([-flange_diameter,-0.01,-1])
            cube([2*flange_diameter,0.02,pulley_height+2]);
    }
} else if (show_section) {
    difference() {
        // Section passes through a cord notch rather than a bead centre.
        rotate([0,0,slice_through_bead ? 0 : -180/ball_count]) pulley();
        translate([-flange_diameter,-flange_diameter,-1])
            cube([2*flange_diameter,flange_diameter,pulley_height+2]);
    }
} else pulley();
echo(str("Pitch diameter: ",wheel_diameter," mm"));
echo(str("Overall diameter: ",flange_diameter," mm; height: ",pulley_height," mm"));
echo(str("Chain advance per revolution: ",ball_count*ball_spacing," mm"));


echo(str("D bore: diameter ",shaft_diameter,", flat-to-opposite-round ",shaft_flat_height," mm; clearance ",shaft_clearance));
echo(str("Plastic outside captive nut: ",nut_outer_wall," mm"));


echo(str("Bead count: ",ball_count,"; estimated maximum load torque: ",estimated_load_torque," N.m"));
echo(str("Nominal margin relative to 0.20 N.m HOLDING torque: ",0.20-estimated_load_torque," N.m (not a running torque guarantee)"));




echo(str("Continuous annular cord groove: ",cord_roof_angle," degrees; centred at z=",bead_z));
echo(str("Upper/lower ligament at notch mouth: ",bead_z-cord_half_height_exit," mm"));
assert(cord_half_height_root > 0,"Cord notch closed at root");
assert(bead_z-cord_half_height_exit >= flange_thickness-0.000001,
       "Notch intersects lower flange: reduce angle or notch depth");
assert(wheel_height-bead_z-cord_half_height_exit >= flange_thickness-0.000001,
       "Notch intersects upper flange: reduce angle or notch depth");
// A circle of link_channel_diameter fits between the tangent sloped faces.
assert(abs(cord_half_height_root-(cord_root_radius-cord_chord_radius)*tan(cord_roof_angle))
       *cos(cord_roof_angle) >= link_channel_diameter/2-0.001,
       "Local notch intrudes into cord envelope");


echo(str("Measured cord: ",link_diameter," mm; channel envelope: ",link_channel_diameter," mm; radial notch depth: ",flange_diameter/2-cord_root_radius," mm"));



echo(str("Wheel axial height: ",wheel_height," mm; shortened hub: ",boss_height," mm"));
assert(screw_z-nut_across_flats/2 >= flange_thickness, "Captive nut extends below wheel body");
assert(screw_z+nut_across_flats/2 <= pulley_height, "Captive nut protrudes above hub");
assert(pulley_height <= overall_height_limit+0.001, "Overall height exceeds requested limit");


assert(screw_z-screw_diameter/2 >= wheel_height, "Screw bore would cut into wheel top");



assert(flange_thickness >= 1.0, "Keep the requested 1 mm flange thickness");





echo(str("Bead pocket width: ",pocket_diameter," mm; axial height: ",pocket_height," mm; bead nominal diameter: ",ball_diameter," mm"));
assert(pocket_diameter < ball_spacing, "Enlarged pockets merge with adjacent pockets");

