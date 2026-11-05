# Tools/blender/zombie_machine.py
#
# Hard-surface machine parts for "robot-stuffed" corpses: bevelled sleeves with collars
# and pistons around limbs, a face plate with a glowing lens, a chest core. They are
# NOT remeshed (they stay crisp), every vertex is weighted 100% to the bone it rides on,
# and they use their own "Metal" material slot. Vertex colours: G = rust, A = glow.

import bpy, bmesh, math
from mathutils import Vector, Matrix, noise
from mathutils.geometry import intersect_point_line
from mathutils.bvhtree import BVHTree


def _oriented(axis):
    """Rotation matrix taking +Z to `axis`."""
    return axis.normalized().to_track_quat('Z', 'Y').to_matrix().to_4x4()


class _Builder:
    def __init__(self):
        self.verts, self.faces, self.bones, self.glow = [], [], [], []
        self.face_bone = []

    def cylinder(self, centre, axis, radius, length, bone, sides=8, bevel=0.12, glow=False, radius2=None):
        tmp = bmesh.new()
        bmesh.ops.create_cone(tmp, cap_ends=True, cap_tris=False, segments=sides,
                              radius1=radius, radius2=radius2 if radius2 is not None else radius, depth=length,
                              matrix=Matrix.Translation(centre) @ _oriented(axis))
        if bevel > 0.0:
            bmesh.ops.bevel(tmp, geom=list(tmp.edges), offset=min(bevel, radius * 0.3, length * 0.3),
                            offset_type='OFFSET', segments=1, profile=0.5, affect='EDGES', clamp_overlap=True)
        base = len(self.verts)
        for v in tmp.verts:
            self.verts.append(v.co.copy())
            self.bones.append(bone)
            self.glow.append(1.0 if glow else 0.0)
        for f in tmp.faces:
            self.faces.append(tuple(base + v.index for v in f.verts))
            self.face_bone.append(bone)
        tmp.free()


def _limb_radius(seg_a, seg_b, bones, sm, src_dom):
    """85th-percentile distance of the limb's original vertices to its bone segment."""
    d = []
    for i, v in enumerate(sm.vertices):
        if src_dom[i] in bones:
            p, t = intersect_point_line(v.co, seg_a, seg_b)
            if 0.0 <= t <= 1.0:
                d.append((v.co - p).length)
    if not d:
        return None
    d.sort()
    return d[int(len(d) * 0.85)]


def _sleeve(b, a_name, b_name, sm, src_dom, bone_pos, U, rng):
    a, c = bone_pos(a_name), bone_pos(b_name)
    axis = c - a
    length = axis.length
    prefix = a_name.rsplit("_", 1)[0].split("_")[0]          # "upperarm", "lowerarm", "thigh", "calf"
    side = a_name.rsplit("_", 1)[1]
    bones = {n for n in set(src_dom) if n.startswith(prefix) and n.endswith("_" + side)}
    r = _limb_radius(a, c, bones, sm, src_dom) or 0.05 * U
    r = r + 0.006 * U
    mid = (a + c) * 0.5
    n = axis.normalized()
    sides = 8
    # main housing, slightly tapered
    b.cylinder(mid, n, r, length * 0.78, a_name, sides=sides, radius2=r * rng.uniform(0.85, 0.95))
    # collars at both ends and a mid rib
    for t in (0.1, 0.5, 0.9):
        b.cylinder(a + axis * t, n, r * 1.13, 0.022 * U, a_name, sides=sides)
    # two pistons along the outside
    perp = n.cross(Vector((0, 0, 1)))
    if perp.length < 0.1:
        perp = n.cross(Vector((1, 0, 0)))
    perp.normalize()
    for sgn in (-1.0, 1.0):
        off = perp * sgn * r * 1.12
        b.cylinder(mid + off, n, 0.009 * U, length * 0.62, a_name, sides=6, bevel=0.05)
        b.cylinder(mid + off + n * length * 0.18, n, 0.014 * U, length * 0.22, a_name, sides=6, bevel=0.06)
    # status light on the upper collar
    b.cylinder(a + axis * 0.1 + perp.cross(n) * r * 1.13, perp.cross(n), 0.008 * U, 0.006 * U, a_name,
               sides=8, bevel=0.0, glow=True)


def build(T, body, src, sm, src_weights, src_dom, BONE_REGION, bone_pos, has_bone, eye_pts, FORWARD, UP, U, rng, stage,
          region_of=None):
    b = _Builder()
    regions = []

    for s in ("L", "R"):
        sl = s.lower()
        if T.get("arm_" + s) == "machine":
            _sleeve(b, "upperarm_" + sl, "lowerarm_" + sl, sm, src_dom, bone_pos, U, rng)
            _sleeve(b, "lowerarm_" + sl, "hand_" + sl, sm, src_dom, bone_pos, U, rng)
            regions += ["UpperArm" + s, "LowerArm" + s]
        if T.get("leg_" + s) == "machine":
            _sleeve(b, "thigh_" + sl, "calf_" + sl, sm, src_dom, bone_pos, U, rng)
            _sleeve(b, "calf_" + sl, "foot_" + sl, sm, src_dom, bone_pos, U, rng)
            regions += ["Thigh" + s, "Calf" + s]

    body_bvh = BVHTree.FromObject(body, bpy.context.evaluated_depsgraph_get())

    if T.get("head") == "machine_plate" and eye_pts:
        ep = eye_pts[-1]                       # one eye becomes the lens
        side = 1.0 if ep.x > 0 else -1.0
        plate_c = ep + Vector((side * 0.012 * U, 0, 0.004 * U)) + FORWARD * 0.004 * U
        b.cylinder(plate_c, FORWARD + Vector((side * 0.35, 0, 0)), 0.042 * U, 0.012 * U, "head", sides=6, bevel=0.15)
        b.cylinder(ep + FORWARD * 0.012 * U, FORWARD, 0.013 * U, 0.012 * U, "head", sides=12, bevel=0.06)
        b.cylinder(ep + FORWARD * 0.019 * U, FORWARD, 0.009 * U, 0.004 * U, "head", sides=12, bevel=0.0, glow=True)
        for k in range(3):
            a = math.radians(90 + k * 120)
            bolt = plate_c + Vector((math.cos(a) * 0.03 * U, 0, math.sin(a) * 0.03 * U)) + FORWARD * 0.008 * U
            b.cylinder(bolt, FORWARD, 0.004 * U, 0.005 * U, "head", sides=6, bevel=0.0)
        regions.append("Head")

    if T.get("torso") == "machine_core":
        hit, hn, _, _ = body_bvh.ray_cast(bone_pos("spine_04"), FORWARD)
        if hit:
            b.cylinder(hit + hn * 0.002 * U, hn, 0.07 * U, 0.016 * U, "spine_04", sides=6, bevel=0.2)
            b.cylinder(hit + hn * 0.012 * U, hn, 0.032 * U, 0.014 * U, "spine_04", sides=12, bevel=0.08)
            b.cylinder(hit + hn * 0.02 * U, hn, 0.022 * U, 0.008 * U, "spine_04", sides=12, bevel=0.0, glow=True)
            for k in range(6):
                a = math.radians(k * 60)
                t1 = hn.cross(Vector((0, 0, 1))).normalized()
                t2 = hn.cross(t1).normalized()
                bolt = hit + (t1 * math.cos(a) + t2 * math.sin(a)) * 0.055 * U + hn * 0.011 * U
                b.cylinder(bolt, hn, 0.005 * U, 0.006 * U, "spine_04", sides=6, bevel=0.0)
            regions.append("Torso")

    if not b.verts:
        stage("  no machine parts")
        return regions

    me = bpy.data.meshes.new("machine")
    me.from_pydata(b.verts, [], b.faces)
    me.update()
    for p in me.polygons:
        p.use_smooth = False
    ob = bpy.data.objects.new("machine", me)
    bpy.context.scene.collection.objects.link(ob)
    ob.parent = body.parent
    ob.matrix_parent_inverse = body.matrix_parent_inverse.copy()
    ob.matrix_basis = body.matrix_basis.copy()

    groups = {}
    for i, bone in enumerate(b.bones):
        if bone not in groups:
            groups[bone] = ob.vertex_groups.new(name=bone)
        groups[bone].add([i], 1.0, 'REPLACE')

    col = me.color_attributes.new("Col", 'BYTE_COLOR', 'POINT')
    for i, v in enumerate(me.vertices):
        rust = noise.noise(v.co / U * 14.0 + Vector((3, 7, 1)), noise_basis='PERLIN_ORIGINAL') * 0.5 + 0.5
        col.data[i].color = (0.0, max(0.0, min(1.0, (rust - 0.55) * 3.0)), 0.0, b.glow[i])
    # One metal slot PER REGION ("UpperArmR_Metal"), so severing a region hides only its own metal.
    slot_names = []
    for bone in b.face_bone:
        n = "%s_Metal" % (region_of(bone) if region_of else "Torso")
        if n not in slot_names:
            slot_names.append(n)
    for n in slot_names:
        me.materials.append(bpy.data.materials.get(n) or bpy.data.materials.new(n))
    for i, poly in enumerate(me.polygons):
        poly.material_index = slot_names.index("%s_Metal" % (region_of(b.face_bone[i]) if region_of else "Torso"))

    with bpy.context.temp_override(active_object=body, object=body, selected_objects=[body, ob],
                                   selected_editable_objects=[body, ob]):
        bpy.ops.object.join()
    stage("  machine parts: %d verts on %s" % (len(b.verts), ", ".join(regions)))
    return regions
