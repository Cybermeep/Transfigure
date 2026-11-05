# Tools/blender/zombie_body_gen.py
#
# Generates one continuous, skinned zombie body from the UE5 Mannequin.
#
#   blender --background --factory-startup --python zombie_body_gen.py -- \
#       --fbx input/SKM_Manny_Simple.fbx --archetype Bloat --seed 3 --out output
#
# Steps:
#   1. Import the Mannequin (armature + weighted reference mesh).
#   2. Reshape per body region along the surface normals (belly, big arm, starved
#      limbs, ribs) + layered noise (lumps, wrinkles).
#   3. Add growth sacs, eyes (and Gaunt blades) as blobs on the surface.
#   4. Voxel-remesh everything into ONE watertight surface (sacs fuse into the flesh).
#   5. Transfer skin weights from the original mesh (nearest surface, barycentric).
#   6. Paint vertex colours: R veins, G mottling, B creases, A growth glow mask.
#   7. Material slot per body region (Head, Torso, UpperArmL, ...) for severing.
#   8. Save <name>.blend (open it and sculpt!), export <name>.fbx, render previews.
#
# UNITS: the imported mesh stores vertices in CENTIMETRES; a parent empty scales the
# hierarchy by 0.01 to metres. All geometry work here happens in that local cm space
# (the hierarchy is left intact). Archetype numbers are written in metres and
# converted with U.

import bpy, bmesh, sys, os, math, random, argparse, time
from mathutils import Vector, Matrix, noise
from mathutils.bvhtree import BVHTree
from mathutils.geometry import barycentric_transform

_T0 = time.time()
def stage(msg):
    """Timed progress line (flushed so it shows up even if a later step hangs)."""
    print("[ZGEN %6.1fs] %s" % (time.time() - _T0, msg), flush=True)

# ----------------------------------------------------------------------------- args
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ap = argparse.ArgumentParser()
ap.add_argument("--fbx", required=True)
ap.add_argument("--archetype", default="Husk")
ap.add_argument("--seed", type=int, default=1)
ap.add_argument("--out", default="output")
ap.add_argument("--voxel", type=float, default=0.0045, help="remesh voxel size (metres)")
ap.add_argument("--tris", type=int, default=36000, help="target triangle count")
ap.add_argument("--max-voxels", type=float, default=4e8, help="refuse to remesh above this many grid cells")
ap.add_argument("--no-render", action="store_true")
args = ap.parse_args(argv)

os.makedirs(args.out, exist_ok=True)
name = "%s_%03d" % (args.archetype, args.seed)
rng = random.Random(args.seed)
SEED_OFS = Vector((args.seed * 3.17, args.seed * 1.31, args.seed * 2.71))
FORWARD = Vector((0, -1, 0))   # the Mannequin faces -Y after FBX import
UP = Vector((0, 0, 1))

# ----------------------------------------------------------------------------- archetypes
# Displacements in metres along the surface normal, per region. Mirrors the UE archetypes.
ARCH = {
    "Husk":  dict(torso=0.000, belly=0.010, head=0.000, arms=0.000, legs=0.000, big_arm=0.000,
                  ribs=0.002, lump=0.007, wrinkle=0.0025, sacs=(3, 6), sac_r=(0.018, 0.040), blades=False,
                  lesions=(4, 7)),
    "Bloat": dict(torso=0.018, belly=0.075, head=-0.006, arms=0.008, legs=0.014, big_arm=0.040,
                  ribs=0.000, lump=0.010, wrinkle=0.0030, sacs=(9, 15), sac_r=(0.025, 0.065), blades=False,
                  lesions=(3, 5)),
    "Gaunt": dict(torso=-0.012, belly=-0.010, head=-0.004, arms=-0.010, legs=-0.009, big_arm=0.000,
                  ribs=0.006, lump=0.005, wrinkle=0.0035, sacs=(1, 3), sac_r=(0.012, 0.025), blades=True,
                  lesions=(5, 9)),
}
A = ARCH.get(args.archetype, ARCH["Husk"])

# ----------------------------------------------------------------------------- regions
FINGERS = ("index", "middle", "ring", "pinky", "thumb", "weapon")

def region_of(bone):
    b = bone.lower()
    side = "L" if b.endswith("_l") else ("R" if b.endswith("_r") else "")
    if b in ("head", "neck_02") or b.startswith("facial"):
        return "Head"
    if b.startswith("upperarm"):
        return "UpperArm" + side
    if b.startswith("lowerarm") or b.startswith("hand") or b.startswith(FINGERS) or b.startswith("wrist"):
        return "LowerArm" + side
    if b.startswith("thigh"):
        return "Thigh" + side
    if b.startswith("calf") or b.startswith("foot") or b.startswith("ball"):
        return "Calf" + side
    return "Torso"

REGIONS = ["Torso", "Head", "UpperArmL", "LowerArmL", "UpperArmR", "LowerArmR", "ThighL", "CalfL", "ThighR", "CalfR"]

# ----------------------------------------------------------------------------- import
stage("import")
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=args.fbx, automatic_bone_orientation=False)
arm = next(o for o in bpy.data.objects if o.type == 'ARMATURE')
src = next(o for o in bpy.data.objects if o.type == 'MESH')

# Local units per metre (100 for the cm mesh). Everything below works in src's local space.
U = 1.0 / src.matrix_world.to_scale().x
local_from_arm = src.matrix_local.inverted() if src.parent == arm else Matrix.Identity(4)
def bone_pos(bname):
    """Bone head in the mesh's local (cm) space."""
    return local_from_arm @ arm.data.bones[bname].head_local
stage("  units: %.1f local units per metre" % U)

sm = src.data
group_names = {g.index: g.name for g in src.vertex_groups}
src_weights, src_region = [], []
for v in sm.vertices:
    w = {group_names[g.group]: g.weight for g in v.groups if g.weight > 0.0}
    src_weights.append(w)
    dom = max(w.items(), key=lambda kv: kv[1])[0] if w else "pelvis"
    src_region.append(region_of(dom))

# Cache region of each bone once (this lookup runs ~millions of times otherwise).
BONE_REGION = {g.name: region_of(g.name) for g in src.vertex_groups}
HAND_BONES = {g.name for g in src.vertex_groups if g.name.lower().startswith(FINGERS) or g.name.lower().startswith("hand")}

def region_weights(w):
    out = {}
    for bone, val in w.items():
        r = BONE_REGION.get(bone, "Torso")
        out[r] = out.get(r, 0.0) + val
    return out

def fbm(p, octaves=3):
    return noise.fractal(p, 0.5, 2.0, octaves, noise_basis='PERLIN_ORIGINAL')

# ----------------------------------------------------------------------------- 1) reshape the body
stage("reshape")
bm = bmesh.new()
bm.from_mesh(sm)
bm.verts.ensure_lookup_table()
bm.normal_update()

big_side = "L" if rng.random() < 0.5 else "R"
pelvis_z = bone_pos("pelvis").z
chest_z = bone_pos("spine_05").z

# Face landmarks on the original surface: eyes found by casting forward from inside the head.
orig_bvh = BVHTree.FromBMesh(bm)
head = bone_pos("head")
eye_pts = []
for side in (-1, 1):
    hit, _, _, _ = orig_bvh.ray_cast(head + Vector((0.033 * side, 0.0, 0.035)) * U, FORWARD)
    if hit:
        eye_pts.append(hit)
if len(eye_pts) == 2:
    eye_mid = (eye_pts[0] + eye_pts[1]) * 0.5
    eye_gap = (eye_pts[0] - eye_pts[1]).length
else:
    eye_mid, eye_gap = None, 0.0

# Infected lesions: sunken patches that get glowing cracks (painted later).
lesions = []   # (centre cm, radius cm)
_lesion_src = [i for i, r in enumerate(src_region) if r in ("Torso", "UpperArmL", "UpperArmR", "ThighL", "ThighR", "CalfL", "CalfR")]
for _ in range(rng.randint(*A["lesions"])):
    i = rng.choice(_lesion_src)
    lesions.append((bm.verts[i].co.copy(), rng.uniform(0.02, 0.05) * U))

# The imported mesh duplicates vertices along UV seams, each copy with its own normal.
# Displacing them separately cracks the seams open and the remesh then shreds the body.
# So every set of co-located vertices shares one averaged normal and moves together.
def _key(co):
    return (round(co.x, 3), round(co.y, 3), round(co.z, 3))
seam_normal = {}
for v in bm.verts:
    k = _key(v.co)
    seam_normal[k] = seam_normal.get(k, Vector()) + v.normal
shared_normal = [seam_normal[_key(v.co)].normalized() for v in bm.verts]
stage("  %d vertices, %d unique positions (seam duplicates welded for displacement)" % (len(bm.verts), len(seam_normal)))

for v in bm.verts:
    w = src_weights[v.index]
    rw = region_weights(w)
    p = v.co                    # cm
    pm = p / U                  # metres, for noise frequencies
    n = shared_normal[v.index]
    d = 0.0                     # metres

    torso = rw.get("Torso", 0.0)
    hands = sum(val for bone, val in w.items() if bone in HAND_BONES)
    d += A["torso"] * torso
    d += A["head"] * rw.get("Head", 0.0)
    arms = sum(rw.get(r, 0.0) for r in ("UpperArmL", "LowerArmL", "UpperArmR", "LowerArmR"))
    d += A["arms"] * max(arms - hands, 0.0)
    legs = sum(rw.get(r, 0.0) for r in ("ThighL", "CalfL", "ThighR", "CalfR"))
    d += A["legs"] * legs

    if torso > 0.2:
        # Belly: front of the lower torso, peaking between pelvis and chest.
        t = (p.z - pelvis_z) / max(chest_z - pelvis_z, 0.01)
        belly_shape = math.exp(-((t - 0.35) / 0.35) ** 2)
        front = max(n.dot(FORWARD), 0.0) ** 0.7
        d += A["belly"] * torso * belly_shape * (0.35 + 0.65 * front)
        # Ribs: horizontal ridges on the chest/sides.
        if A["ribs"] > 0.0 and 0.45 < t < 1.0:
            side = 1.0 - abs(n.dot(UP))
            d -= A["ribs"] * torso * side * (0.5 + 0.5 * math.cos(pm.z * 2 * math.pi / 0.032))

    if A["big_arm"] > 0.0:
        # Keep the fist big but the fingers readable (hands get a fraction of the swelling).
        lower = max(rw.get("LowerArm" + big_side, 0.0) - 0.65 * hands, 0.0)
        d += A["big_arm"] * (rw.get("UpperArm" + big_side, 0.0) + 0.8 * lower)

    # Face: sunken eye sockets, brow ridge, hollow cheeks, mouth slit (relative to the eyes).
    headw = rw.get("Head", 0.0)
    if headw > 0.3 and eye_mid is not None and n.dot(FORWARD) > 0.1:
        rel = p - eye_mid
        g = eye_gap
        for ep in eye_pts:
            d -= 0.008 * headw * math.exp(-((p - ep).length / (0.42 * g)) ** 2)            # sockets
        brow = math.exp(-((rel.z - 0.55 * g) / (0.22 * g)) ** 2) * math.exp(-(rel.x / (1.1 * g)) ** 2)
        d += 0.004 * headw * brow                                                        # brow ridge
        cheek = math.exp(-((rel.z + 0.75 * g) / (0.3 * g)) ** 2) * math.exp(-((abs(rel.x) - 0.62 * g) / (0.25 * g)) ** 2)
        d -= 0.004 * headw * cheek                                                       # hollow cheeks
        mouth = math.exp(-((rel.z + 1.4 * g) / (0.07 * g)) ** 2) * math.exp(-(rel.x / (0.55 * g)) ** 4)
        d -= 0.005 * headw * mouth                                                       # mouth slit

    # Lesions: shallow craters.
    for lc, lr in lesions:
        dist = (p - lc).length
        if dist < lr * 1.5:
            d -= 0.005 * math.exp(-(dist / lr) ** 2)

    # Organic surface: lumps + fine wrinkles (smaller on hands so fingers stay fingers).
    detail = 1.0 - 0.75 * min(hands, 1.0)
    d += A["lump"] * detail * fbm(pm * 9.0 + SEED_OFS, 3)
    d += A["wrinkle"] * detail * noise.noise(pm * 38.0 + SEED_OFS * 1.7, noise_basis='PERLIN_ORIGINAL')

    v.co = p + n * (d * U)

bm.normal_update()

# ----------------------------------------------------------------------------- 2) growths, eyes, blades
stage("growths")
shaped_bvh = BVHTree.FromBMesh(bm)
surface = [(v.co.copy(), v.normal.copy()) for v in bm.verts]
glow_centres = []   # (centre cm, radius cm)

def add_blob(centre, radius, squash=1.0, lump=0.18):
    """Lumpy icosphere added straight into the body bmesh (remesh fuses it)."""
    ret = bmesh.ops.create_icosphere(bm, subdivisions=3, radius=radius, matrix=Matrix.Translation(centre))
    s = Vector((rng.random() * 100.0,) * 3)
    for vv in ret["verts"]:
        rel = vv.co - centre
        dirn = rel.normalized()
        rel = dirn * radius * (1.0 + lump * noise.noise(dirn * 2.5 + s, noise_basis='PERLIN_ORIGINAL'))
        rel.z *= squash
        vv.co = centre + rel

# Sacs: torso and upper limbs, never on hands or the front of the face.
candidates = [i for i, r in enumerate(src_region)
              if r in ("Torso", "UpperArmL", "UpperArmR", "ThighL", "ThighR", "CalfL", "CalfR", "Head")]
for _ in range(rng.randint(*A["sacs"])):
    i = rng.choice(candidates)
    pos, nrm = surface[i]
    if src_region[i] == "Head" and nrm.dot(FORWARD) > 0.3:
        continue
    r = rng.uniform(*A["sac_r"]) * U
    c = pos + nrm * r * 0.35
    add_blob(c, r)
    glow_centres.append((c, r))

# Eyes: glowing orbs sitting in the sunken sockets.
for ep in eye_pts:
    c = ep - FORWARD * 0.006 * U
    add_blob(c, 0.0105 * U, squash=0.8, lump=0.05)
    glow_centres.append((c, 0.0105 * U))

# Gaunt blades: long curved spikes growing out of each forearm, past the hand.
if A["blades"]:
    for s in ("l", "r"):
        lo, hand = bone_pos("lowerarm_" + s), bone_pos("hand_" + s)
        axis = (hand - lo).normalized()
        length = rng.uniform(0.38, 0.5) * U
        bend = (FORWARD * 0.6 - UP * 0.4).normalized()
        steps = 40
        for k in range(steps):
            t = k / (steps - 1)
            c = lo + axis * (0.06 * U + t * length) + bend * (t * t) * length * 0.35
            r = (0.026 * (1.0 - t) ** 1.1 + 0.0035) * U
            add_blob(c, r, lump=0.06)

body_mesh = bpy.data.meshes.new(name)
bm.to_mesh(body_mesh)
bm.free()
body = bpy.data.objects.new(name, body_mesh)
bpy.context.scene.collection.objects.link(body)
# Same parent and local transform as the source mesh, so local cm space is identical.
body.parent = src.parent
body.matrix_parent_inverse = src.matrix_parent_inverse.copy()
body.matrix_basis = src.matrix_basis.copy()

def apply_modifier(ob, mod):
    dg = bpy.context.evaluated_depsgraph_get()
    new = bpy.data.meshes.new_from_object(ob.evaluated_get(dg))
    old = ob.data
    ob.modifiers.remove(mod)
    ob.data = new
    bpy.data.meshes.remove(old)

# ----------------------------------------------------------------------------- 3) remesh to one surface
voxel_local = args.voxel * U
xs = [v.co.x for v in body_mesh.vertices]; ys = [v.co.y for v in body_mesh.vertices]; zs = [v.co.z for v in body_mesh.vertices]
dims = Vector((max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)))   # local extents (cm)
cells = (dims.x / voxel_local) * (dims.y / voxel_local) * (dims.z / voxel_local)
stage("remesh: voxel %.2f local units, ~%.2g grid cells" % (voxel_local, cells))
if cells > args.max_voxels:
    raise SystemExit("[ZGEN] refusing to remesh: %.3g cells > --max-voxels %.3g (check units / voxel size)" % (cells, args.max_voxels))

rem = body.modifiers.new("remesh", 'REMESH')
rem.mode = 'VOXEL'
rem.voxel_size = voxel_local
rem.use_smooth_shade = True
apply_modifier(body, rem)

stage("smooth")
sm_mod = body.modifiers.new("smooth", 'CORRECTIVE_SMOOTH')
sm_mod.factor = 0.5
sm_mod.iterations = 6
sm_mod.smooth_type = 'LENGTH_WEIGHTED'
sm_mod.use_only_smooth = True
apply_modifier(body, sm_mod)

cur_tris = sum(len(p.vertices) - 2 for p in body.data.polygons)
stage("decimate: %d verts, %d tris after remesh" % (len(body.data.vertices), cur_tris))
if cur_tris > args.tris:
    dec = body.modifiers.new("decimate", 'DECIMATE')
    dec.ratio = args.tris / cur_tris
    apply_modifier(body, dec)
stage("  final mesh: %d verts" % len(body.data.vertices))

# ----------------------------------------------------------------------------- 4) skin weights from the original
stage("weights")
src_local = [v.co.copy() for v in sm.vertices]
tri_bm = bmesh.new()
tri_bm.from_mesh(sm)
bmesh.ops.triangulate(tri_bm, faces=tri_bm.faces[:])
tris = [tuple(v.index for v in f.verts) for f in tri_bm.faces]
tri_bm.free()
src_bvh = BVHTree.FromPolygons(src_local, tris)

for g in src.vertex_groups:
    body.vertex_groups.new(name=g.name)
vg = {g.name: g for g in body.vertex_groups}

vertex_region = []
for v in body.data.vertices:
    loc, _, fi, _ = src_bvh.find_nearest(v.co)
    a, b, c = tris[fi]
    bary = barycentric_transform(loc, src_local[a], src_local[b], src_local[c],
                                 Vector((1, 0, 0)), Vector((0, 1, 0)), Vector((0, 0, 1)))
    weights = {}
    for idx, f in zip((a, b, c), (bary.x, bary.y, bary.z)):
        f = max(f, 0.0)
        for bone, val in src_weights[idx].items():
            weights[bone] = weights.get(bone, 0.0) + val * f
    top = sorted(weights.items(), key=lambda kv: -kv[1])[:4]
    tt = sum(val for _, val in top) or 1.0
    for bone, val in top:
        vg[bone].add([v.index], val / tt, 'REPLACE')
    vertex_region.append(BONE_REGION.get(top[0][0], "Torso") if top else "Torso")

# ----------------------------------------------------------------------------- 5) vertex colours
stage("vertex colours")
me = body.data
col = me.color_attributes.new("Col", 'BYTE_COLOR', 'POINT')
nbrs = [[] for _ in me.vertices]
for e in me.edges:
    i, j = e.vertices
    nbrs[i].append(j); nbrs[j].append(i)
concav = []
for v in me.vertices:
    if not nbrs[v.index]:
        concav.append(0.0); continue
    avg = sum((me.vertices[j].co for j in nbrs[v.index]), Vector()) / len(nbrs[v.index])
    concav.append((avg - v.co).dot(v.normal))
cmax = max(abs(c) for c in concav) or 1.0

for v in me.vertices:
    pm = v.co / U
    q = pm * 11.0 + SEED_OFS
    vein = max(0.0, 1.0 - abs(noise.noise(q * 2.4 + Vector((5, 5, 5)), noise_basis='PERLIN_ORIGINAL')) / 0.05) * 0.85
    mottle = min(max(fbm(q * 0.6, 3) * 0.9 + 0.5, 0.0), 1.0)
    crease = min(max(concav[v.index] / cmax * 3.0, 0.0), 1.0)
    glow = 0.0
    for lc, lr in lesions:
        dist = (v.co - lc).length
        if dist < lr:
            crack = max(0.0, 1.0 - abs(noise.noise(pm * 60.0 + SEED_OFS, noise_basis='PERLIN_ORIGINAL')) / 0.13)
            glow = max(glow, crack * (1.0 - dist / lr) ** 0.5)
            crease = max(crease, 0.6 * (1.0 - dist / lr))
    # Mouth: darken the slit.
    if eye_mid is not None:
        rel = v.co - eye_mid
        if abs(rel.z + 1.4 * eye_gap) < 0.12 * eye_gap and abs(rel.x) < 0.55 * eye_gap and v.normal.dot(FORWARD) > 0.2:
            crease = max(crease, 0.9)
    for c, r in glow_centres:
        dd = (v.co - c).length
        if dd < r * 1.4:
            glow = max(glow, min(1.0, (r * 1.4 - dd) / (r * 0.6)))
    col.data[v.index].color = (vein, mottle, crease, glow)

# ----------------------------------------------------------------------------- 6) region material slots
stage("region slots")
for r in REGIONS:
    me.materials.append(bpy.data.materials.get(r) or bpy.data.materials.new(r))
slot = {r: i for i, r in enumerate(REGIONS)}
for poly in me.polygons:
    votes = {}
    for vi in poly.vertices:
        votes[vertex_region[vi]] = votes.get(vertex_region[vi], 0) + 1
    poly.material_index = slot[max(votes.items(), key=lambda kv: kv[1])[0]]

# ----------------------------------------------------------------------------- 7) rig, save, export
stage("save + export")
mod = body.modifiers.new("Armature", 'ARMATURE')
mod.object = arm
bpy.data.objects.remove(src)
for m in [m for m in bpy.data.meshes if m.users == 0]:
    bpy.data.meshes.remove(m)

blend_path = os.path.abspath(os.path.join(args.out, name + ".blend"))
fbx_path = os.path.abspath(os.path.join(args.out, name + ".fbx"))
bpy.ops.wm.save_as_mainfile(filepath=blend_path)
bpy.ops.object.select_all(action='DESELECT')
arm.select_set(True)
body.select_set(True)
bpy.ops.export_scene.fbx(filepath=fbx_path, use_selection=True, object_types={'ARMATURE', 'MESH'},
                         add_leaf_bones=False, bake_anim=False, use_armature_deform_only=False,
                         mesh_smooth_type='FACE', colors_type='LINEAR', primary_bone_axis='Y', secondary_bone_axis='X')
stage("done: %s (%d verts, %d tris)" % (fbx_path, len(me.vertices), sum(len(p.vertices) - 2 for p in me.polygons)))

# ----------------------------------------------------------------------------- 8) preview renders
if not args.no_render:
    stage("previews")
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import preview_render
    preview_render.render(body, arm, os.path.join(args.out, "previews"), name, args.archetype)
    stage("previews done")
