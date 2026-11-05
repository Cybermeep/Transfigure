# Tools/blender/zombie_body_gen.py
#
# Generates one continuous, skinned zombie body from the UE5 Mannequin.
#
#   blender --background --factory-startup --python zombie_body_gen.py -- \
#       --fbx input/SKM_Manny_Simple.fbx --archetype Bloat --seed 3 --out output
#   optional: --set arm_L=claws --set head=crown   (force templates)
#
# Each body region rolls a TEMPLATE (weighted per archetype), e.g. arm_L=claws,
# arm_R=machine, leg_L=spiked, head=cluster_eyes, torso=hump. Templates reshape the
# flesh and/or add features:
#   - organic features (sacs, spikes, claws, tendrils, blades) are blobs that the
#     voxel remesh fuses into ONE surface; each is LOCKED to the bone it grows from
#     so it deforms rigidly with that bone instead of stretching.
#   - machine parts are crisp hard-surface meshes (sleeves, collars, pistons,
#     plates, lenses), added after the remesh with 100% weight to their bone and
#     their own "Metal" material slot.
#
# Outputs: <name>.blend (sculptable), <name>.fbx, <name>.json (templates), previews.
#
# UNITS: the imported mesh is in CENTIMETRES under a 0.01-scale parent empty. All work
# happens in that local cm space (hierarchy intact). Numbers below are in metres * U.

import bpy, bmesh, sys, os, math, random, argparse, time, json
from mathutils import Vector, Matrix, noise
from mathutils.bvhtree import BVHTree
from mathutils.geometry import barycentric_transform
from mathutils.kdtree import KDTree

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))   # zombie_machine, preview_render

_T0 = time.time()
def stage(msg):
    print("[ZGEN %6.1fs] %s" % (time.time() - _T0, msg), flush=True)

# ----------------------------------------------------------------------------- args
argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
ap = argparse.ArgumentParser()
ap.add_argument("--fbx", required=True)
ap.add_argument("--archetype", default="Husk")
ap.add_argument("--seed", type=int, default=1)
ap.add_argument("--out", default="output")
ap.add_argument("--voxel", type=float, default=0.003, help="remesh voxel size (metres)")
ap.add_argument("--tris", type=int, default=60000, help="target triangle count (organic surface)")
ap.add_argument("--max-voxels", type=float, default=2e9, help="refuse to remesh above this many grid cells")
ap.add_argument("--set", action="append", default=[], help="force a template, e.g. arm_L=claws")
ap.add_argument("--no-render", action="store_true")
args = ap.parse_args(argv)

os.makedirs(args.out, exist_ok=True)
name = "%s_%03d" % (args.archetype, args.seed)
# Deterministic: Python hash() of a string changes every process, so use a stable checksum.
import zlib
rng = random.Random(args.seed * 7919 + zlib.crc32(args.archetype.encode()) % 1000)
SEED_OFS = Vector((args.seed * 3.17, args.seed * 1.31, args.seed * 2.71))
FORWARD = Vector((0, -1, 0))   # the Mannequin faces -Y after FBX import
BACK = -FORWARD
UP = Vector((0, 0, 1))

# ----------------------------------------------------------------------------- archetypes
# Base reshaping (metres along the normal) + template weights per region.
ARCH = {
    "Husk": dict(
        torso=0.000, belly=0.010, head=0.000, arms=0.000, legs=0.000, ribs=0.002,
        lump=0.007, wrinkle=0.0025, sacs=(2, 5), sac_r=(0.018, 0.040), lesions=(4, 7), robot=0.30,
        arm=dict(normal=5, withered=2, swollen=1, tumorous=2, claws=2, tendrils=1, blade=0.5),
        leg=dict(normal=6, withered=2, swollen=1, tumorous=1, spiked=1),
        head_t=dict(normal=4, bulbous=1, crown=1, cluster_eyes=1),
        torso_t=dict(normal=4, hump=1, spine_spikes=1, open_ribs=1, back_tendrils=1)),
    "Bloat": dict(
        torso=0.018, belly=0.075, head=-0.006, arms=0.008, legs=0.014, ribs=0.000,
        lump=0.010, wrinkle=0.0030, sacs=(8, 14), sac_r=(0.025, 0.065), lesions=(3, 5), robot=0.30,
        arm=dict(normal=2, swollen=5, tumorous=3, claws=1),
        leg=dict(normal=3, swollen=4, tumorous=2),
        head_t=dict(normal=3, bulbous=3, cluster_eyes=1),
        torso_t=dict(normal=2, hump=4, back_tendrils=1, open_ribs=1)),
    "Gaunt": dict(
        torso=-0.012, belly=-0.010, head=-0.004, arms=-0.010, legs=-0.009, ribs=0.006,
        lump=0.005, wrinkle=0.0035, sacs=(1, 3), sac_r=(0.012, 0.025), lesions=(5, 9), robot=0.30,
        arm=dict(withered=2, blade=5, claws=3, tendrils=2),
        leg=dict(normal=3, withered=3, spiked=3),
        head_t=dict(normal=2, crown=3, cluster_eyes=2),
        torso_t=dict(normal=1, spine_spikes=4, open_ribs=3, back_tendrils=3)),
}
A = ARCH.get(args.archetype, ARCH["Husk"])

def weighted(table):
    total = sum(table.values())
    r = rng.uniform(0, total)
    for k, w in table.items():
        r -= w
        if r <= 0:
            return k
    return next(iter(table))

T = {"arm_L": weighted(A["arm"]), "arm_R": weighted(A["arm"]),
     "leg_L": weighted(A["leg"]), "leg_R": weighted(A["leg"]),
     "head": weighted(A["head_t"]), "torso": weighted(A["torso_t"])}
# Robot-stuffed corpse: 1-3 regions become machinery.
if rng.random() < A["robot"]:
    slots = ["arm_L", "arm_R", "leg_L", "leg_R", "head", "torso"]
    for k in rng.sample(slots, rng.randint(1, 3)):
        T[k] = {"head": "machine_plate", "torso": "machine_core"}.get(k, "machine")
for kv in args.set:
    k, v = kv.split("=", 1)
    T[k.strip()] = v.strip()
stage("templates: " + ", ".join("%s=%s" % kv for kv in T.items()))

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

U = 1.0 / src.matrix_world.to_scale().x          # local units per metre (100)
local_from_arm = src.matrix_local.inverted() if src.parent == arm else Matrix.Identity(4)
def bone_pos(bname):
    return local_from_arm @ arm.data.bones[bname].head_local
def has_bone(bname):
    return bname in arm.data.bones

sm = src.data
group_names = {g.index: g.name for g in src.vertex_groups}
src_weights, src_region, src_dom = [], [], []
for v in sm.vertices:
    w = {group_names[g.group]: g.weight for g in v.groups if g.weight > 0.0}
    src_weights.append(w)
    dom = max(w.items(), key=lambda kv: kv[1])[0] if w else "pelvis"
    src_dom.append(dom)
    src_region.append(region_of(dom))

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

def gauss(x):
    return math.exp(-x * x)

# ----------------------------------------------------------------------------- 1) reshape the body
stage("reshape")
bm = bmesh.new()
bm.from_mesh(sm)
bm.verts.ensure_lookup_table()
bm.normal_update()

pelvis_z = bone_pos("pelvis").z
chest_z = bone_pos("spine_05").z

# Seam duplicates share one normal so displacement can't crack the surface.
def _key(co):
    return (round(co.x, 3), round(co.y, 3), round(co.z, 3))
seam_normal = {}
for v in bm.verts:
    seam_normal[_key(v.co)] = seam_normal.get(_key(v.co), Vector()) + v.normal
shared_normal = [seam_normal[_key(v.co)].normalized() for v in bm.verts]

# Face landmarks (eyes) on the original surface.
orig_bvh = BVHTree.FromBMesh(bm)
head = bone_pos("head")
eye_pts = []
for side in (-1, 1):
    hit, _, _, _ = orig_bvh.ray_cast(head + Vector((0.033 * side, 0.0, 0.035)) * U, FORWARD)
    if hit:
        eye_pts.append(hit)
eye_mid = (eye_pts[0] + eye_pts[1]) * 0.5 if len(eye_pts) == 2 else None
eye_gap = (eye_pts[0] - eye_pts[1]).length if len(eye_pts) == 2 else 0.0

# Lesions (shallow craters with glowing cracks).
lesions = []
_lesion_src = [i for i, r in enumerate(src_region) if r in ("Torso", "UpperArmL", "UpperArmR", "ThighL", "ThighR", "CalfL", "CalfR")]
for _ in range(rng.randint(*A["lesions"])):
    i = rng.choice(_lesion_src)
    lesions.append((bm.verts[i].co.copy(), rng.uniform(0.02, 0.05) * U))
# Open ribs: one big glowing chest wound.
if T["torso"] == "open_ribs":
    hit, _, _, _ = orig_bvh.ray_cast(bone_pos("spine_03") + BACK * 0.0, FORWARD)
    if hit:
        lesions.append((hit + UP * 0.03 * U, 0.09 * U))

# Template anchors that need a point on the body.
hump_c = None
if T["torso"] == "hump":
    hit, _, _, _ = orig_bvh.ray_cast(bone_pos("spine_04"), BACK)
    hump_c = (hit or bone_pos("spine_04") + BACK * 0.1 * U) + UP * 0.02 * U + Vector((rng.uniform(-0.05, 0.05) * U, 0, 0))
bulb_c = None
if T["head"] == "bulbous":
    d = Vector((rng.choice((-1, 1)) * 0.6, 0.4, 0.7)).normalized()
    hit, _, _, _ = orig_bvh.ray_cast(head + UP * 0.08 * U, d)
    bulb_c = hit or head + d * 0.09 * U

for v in bm.verts:
    w = src_weights[v.index]
    rw = region_weights(w)
    p = v.co
    pm = p / U
    n = shared_normal[v.index]
    d = 0.0
    hands = sum(val for bone, val in w.items() if bone in HAND_BONES)
    torso = rw.get("Torso", 0.0)
    headw = rw.get("Head", 0.0)

    # Archetype base proportions.
    d += A["torso"] * torso + A["head"] * headw
    arms = sum(rw.get(r, 0.0) for r in ("UpperArmL", "LowerArmL", "UpperArmR", "LowerArmR"))
    d += A["arms"] * max(arms - hands, 0.0)
    legs = sum(rw.get(r, 0.0) for r in ("ThighL", "CalfL", "ThighR", "CalfR"))
    d += A["legs"] * legs

    if torso > 0.2:
        t = (p.z - pelvis_z) / max(chest_z - pelvis_z, 0.01)
        front = max(n.dot(FORWARD), 0.0) ** 0.7
        d += A["belly"] * torso * gauss((t - 0.35) / 0.35) * (0.35 + 0.65 * front)
        ribs = A["ribs"] + (0.006 if T["torso"] == "open_ribs" else 0.0)
        if ribs > 0.0 and 0.45 < t < 1.0:
            d -= ribs * torso * (1.0 - abs(n.dot(UP))) * (0.5 + 0.5 * math.cos(pm.z * 2 * math.pi / 0.032))
        if hump_c is not None:
            d += 0.075 * torso * gauss((p - hump_c).length / (0.09 * U))

    # Per-side limb templates.
    for s in ("L", "R"):
        upper, lower = rw.get("UpperArm" + s, 0.0), rw.get("LowerArm" + s, 0.0)
        lower_nohand = max(lower - 0.65 * hands, 0.0)
        ta = T["arm_" + s]
        if ta == "withered":
            d -= 0.009 * (upper + lower_nohand)
        elif ta == "swollen":
            d += 0.032 * upper + 0.026 * lower_nohand
        elif ta == "machine":
            d -= 0.004 * (upper + lower)          # slimmer flesh under the metal sleeves
        thigh, calf = rw.get("Thigh" + s, 0.0), rw.get("Calf" + s, 0.0)
        tl = T["leg_" + s]
        if tl == "withered":
            d -= 0.008 * (thigh + calf)
        elif tl == "swollen":
            d += 0.022 * thigh + 0.016 * calf
        elif tl == "machine":
            d -= 0.004 * (thigh + calf)

    # Head templates + face sculpt.
    if bulb_c is not None and headw > 0.2:
        d += 0.035 * headw * gauss((p - bulb_c).length / (0.055 * U))
    if headw > 0.3 and eye_mid is not None and n.dot(FORWARD) > 0.1:
        rel, g = p - eye_mid, eye_gap
        for ep in eye_pts:
            d -= 0.008 * headw * gauss((p - ep).length / (0.42 * g))
        d += 0.004 * headw * gauss((rel.z - 0.55 * g) / (0.22 * g)) * gauss(rel.x / (1.1 * g))
        d -= 0.004 * headw * gauss((rel.z + 0.75 * g) / (0.3 * g)) * gauss((abs(rel.x) - 0.62 * g) / (0.25 * g))
        d -= 0.005 * headw * gauss((rel.z + 1.4 * g) / (0.07 * g)) * math.exp(-(rel.x / (0.55 * g)) ** 4)

    for lc, lr in lesions:
        dist = (p - lc).length
        if dist < lr * 1.5:
            d -= 0.005 * gauss(dist / lr)

    detail = 1.0 - 0.75 * min(hands, 1.0)
    d += A["lump"] * detail * fbm(pm * 9.0 + SEED_OFS, 3)
    d += A["wrinkle"] * detail * noise.noise(pm * 38.0 + SEED_OFS * 1.7, noise_basis='PERLIN_ORIGINAL')
    v.co = p + n * (d * U)

bm.normal_update()

# ----------------------------------------------------------------------------- 2) organic features
stage("features")
surface = [(v.co.copy(), v.normal.copy()) for v in bm.verts]
shaped_bvh = BVHTree.FromBMesh(bm)
glow_centres = []      # (centre, radius) glowing blobs
locked = []            # (centre, radius, bone): remeshed vertices here get 100% this bone

# One unit icosphere template, copied and deformed per blob (fast even for 1000+ blobs).
_ico = bmesh.new()
bmesh.ops.create_icosphere(_ico, subdivisions=3, radius=1.0)
ICO_V = [v.co.normalized() for v in _ico.verts]
ICO_F = [tuple(v.index for v in f.verts) for f in _ico.faces]
_ico.free()
blob_verts, blob_faces = [], []

def add_blob(centre, radius, squash=1.0, lump=0.18, bone=None, glow=False):
    s = Vector((rng.random() * 100.0,) * 3)
    base = len(blob_verts)
    for dirn in ICO_V:
        rel = dirn * radius * (1.0 + lump * noise.noise(dirn * 2.5 + s, noise_basis='PERLIN_ORIGINAL'))
        rel.z *= squash
        blob_verts.append(centre + rel)
    blob_faces.extend(tuple(base + i for i in f) for f in ICO_F)
    if bone:
        locked.append((centre.copy(), radius, bone))
    if glow:
        glow_centres.append((centre.copy(), radius))

def add_strand(start, direction, length, r0, r1, bone, curl=Vector(), steps=None, droop=0.0, glow=False,
               taper=0.9, offset=Vector()):
    """Tapering chain of blobs (spike, claw, tendril, blade) locked to one bone.

    Blobs are spaced by their OWN radius (0.55 r apart), so the thin tip is as solidly
    joined as the base - even spacing left thin ends as a dotted line after remeshing.
    `steps` is ignored (kept for call compatibility)."""
    direction = direction.normalized()
    def at(t):
        return (start + offset * (1.0 - t) + direction * (t * length) + curl * (t * t) * length
                + Vector((0, 0, -droop * t * t * length)))
    def radius(t):
        return r1 + (r0 - r1) * (1.0 - t) ** taper
    t, n = 0.0, 0
    while t <= 1.0 and n < 400:
        add_blob(at(t), radius(t), lump=0.06, bone=bone, glow=glow and t > 0.85)
        # advance along the curve by ~0.55 of the current radius
        step_len = 0.55 * radius(t)
        t += max(step_len / max(length, 1e-6), 0.002)
        n += 1
    add_blob(at(1.0), r1, lump=0.06, bone=bone, glow=glow)

def region_verts(*regions, facing=None, min_dot=0.0):
    out = []
    for i, r in enumerate(src_region):
        if r in regions:
            if facing is None or surface[i][1].dot(facing) > min_dot:
                out.append(i)
    return out

def sac_at(i, rmin, rmax):
    pos, nrm = surface[i]
    r = rng.uniform(rmin, rmax) * U
    add_blob(pos + nrm * r * 0.35, r, bone=src_dom[i], glow=True)

# General sacs (archetype density).
_sac_src = region_verts("Torso", "UpperArmL", "UpperArmR", "ThighL", "ThighR", "CalfL", "CalfR")
for _ in range(rng.randint(*A["sacs"])):
    sac_at(rng.choice(_sac_src), *A["sac_r"])

for s in ("L", "R"):
    sl = s.lower()
    ta, tl = T["arm_" + s], T["leg_" + s]
    if ta == "tumorous":
        pool = region_verts("UpperArm" + s, "LowerArm" + s)
        for _ in range(rng.randint(4, 7)):
            sac_at(rng.choice(pool), 0.015, 0.035)
    elif ta == "blade":
        lo, hand = bone_pos("lowerarm_" + sl), bone_pos("hand_" + sl)
        axis = (hand - lo).normalized()
        length = rng.uniform(0.40, 0.52) * U
        curl = FORWARD * 0.35 - UP * 0.2
        across = axis.cross(UP).normalized()          # blade width runs across the arm
        for row in (-1.0, 0.0, 1.0):
            add_strand(lo + axis * 0.06 * U, axis, length * (1.0 - 0.12 * abs(row)),
                       (0.020 if row == 0 else 0.016) * U, 0.003 * U, "lowerarm_" + sl, curl=curl,
                       taper=0.5, offset=across * row * 0.022 * U)
    elif ta == "claws":
        for f in ("index", "middle", "ring", "pinky", "thumb"):
            b3, b2 = "%s_03_%s" % (f, sl), "%s_02_%s" % (f, sl)
            if has_bone(b3) and has_bone(b2):
                tip_dir = (bone_pos(b3) - bone_pos(b2)).normalized()
                add_strand(bone_pos(b3) + tip_dir * 0.012 * U, tip_dir, rng.uniform(0.04, 0.075) * U,
                           0.0075 * U, 0.0015 * U, b3, curl=-UP * 0.25, steps=14)
    elif ta == "tendrils":
        pool = region_verts("LowerArm" + s, "UpperArm" + s)
        for _ in range(rng.randint(2, 4)):
            pos, nrm = surface[rng.choice(pool)]
            add_strand(pos, nrm * 0.4 - UP * 0.3, rng.uniform(0.22, 0.42) * U, 0.018 * U, 0.0035 * U,
                       "lowerarm_" + sl,
                       curl=Vector((rng.uniform(-0.5, 0.5), rng.uniform(-0.5, 0.5), 0)), droop=0.8, glow=True)
    if tl == "tumorous":
        pool = region_verts("Thigh" + s, "Calf" + s)
        for _ in range(rng.randint(3, 6)):
            sac_at(rng.choice(pool), 0.015, 0.04)
    elif tl == "spiked":
        pool = region_verts("Calf" + s, facing=FORWARD, min_dot=0.4) or region_verts("Calf" + s)
        for _ in range(rng.randint(4, 7)):
            i = rng.choice(pool)
            pos, nrm = surface[i]
            add_strand(pos - nrm * 0.004 * U, nrm + UP * 0.6, rng.uniform(0.05, 0.10) * U,
                       0.011 * U, 0.0015 * U, "calf_" + sl, steps=12)

if T["head"] == "crown":
    pool = region_verts("Head", facing=UP, min_dot=0.45)
    for _ in range(rng.randint(5, 9)):
        pos, nrm = surface[rng.choice(pool)]
        add_strand(pos - nrm * 0.004 * U, nrm + BACK * 0.3, rng.uniform(0.04, 0.09) * U,
                   0.009 * U, 0.0012 * U, "head", steps=12)
if T["torso"] == "spine_spikes":
    for b in ("spine_01", "spine_02", "spine_03", "spine_04", "spine_05", "neck_01"):
        hit, hn, _, _ = shaped_bvh.ray_cast(bone_pos(b), BACK)
        if hit:
            for side in ((0.0,) if rng.random() < 0.5 else (-1.0, 1.0)):
                off = Vector((side * 0.025 * U, 0, 0))
                add_strand(hit + off - hn * 0.004 * U, hn + UP * 0.7, rng.uniform(0.05, 0.11) * U,
                           0.013 * U, 0.0015 * U, b, steps=12)
if T["torso"] == "back_tendrils":
    for _ in range(rng.randint(3, 5)):
        b = rng.choice(("spine_04", "spine_05", "clavicle_l", "clavicle_r"))
        hit, hn, _, _ = shaped_bvh.ray_cast(bone_pos(b) + Vector((rng.uniform(-0.08, 0.08) * U, 0, 0)), BACK)
        if hit:
            add_strand(hit, hn * 0.45 - UP * 0.25, rng.uniform(0.3, 0.55) * U, 0.024 * U, 0.004 * U, b,
                       curl=Vector((rng.uniform(-0.6, 0.6), rng.uniform(0.1, 0.4), 0)), droop=0.9, glow=True)

# Eyes.
if T["head"] == "cluster_eyes" and eye_mid is not None:
    for _ in range(rng.randint(4, 7)):
        o = eye_mid + Vector((rng.uniform(-1.1, 1.1) * eye_gap, 0, rng.uniform(-0.5, 0.9) * eye_gap)) - FORWARD * 0.03 * U
        hit, _, _, _ = shaped_bvh.ray_cast(o, FORWARD)
        if hit:
            r = rng.uniform(0.004, 0.009) * U
            add_blob(hit - FORWARD * r * 0.4, r, squash=0.85, lump=0.05, bone="head", glow=True)
elif T["head"] != "machine_plate":
    for ep in eye_pts:
        add_blob(ep - FORWARD * 0.006 * U, 0.0105 * U, squash=0.8, lump=0.05, bone="head", glow=True)
elif eye_pts:
    add_blob(eye_pts[0] - FORWARD * 0.006 * U, 0.0105 * U, squash=0.8, lump=0.05, bone="head", glow=True)

stage("  %d bone-locked feature blobs, %d glowing" % (len(locked), len(glow_centres)))

body_mesh = bpy.data.meshes.new(name)
blob_mesh = bpy.data.meshes.new("blobs")
blob_mesh.from_pydata(blob_verts, [], blob_faces)
bm.from_mesh(blob_mesh)          # appends the blobs to the body geometry
bpy.data.meshes.remove(blob_mesh)
bm.to_mesh(body_mesh)
bm.free()
body = bpy.data.objects.new(name, body_mesh)
bpy.context.scene.collection.objects.link(body)
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
dims = Vector((max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)))
cells = (dims.x / voxel_local) * (dims.y / voxel_local) * (dims.z / voxel_local)
stage("remesh: voxel %.2f cm, ~%.2g grid cells" % (voxel_local, cells))
if cells > args.max_voxels:
    raise SystemExit("[ZGEN] refusing to remesh: %.3g cells > --max-voxels %.3g (check units / voxel size)" % (cells, args.max_voxels))
rem = body.modifiers.new("remesh", 'REMESH')
rem.mode = 'VOXEL'
rem.voxel_size = voxel_local
rem.use_smooth_shade = True
apply_modifier(body, rem)

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

# ----------------------------------------------------------------------------- 4) skin weights
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

# Spatial index of bone-locked feature blobs (hundreds of them).
lock_kd = KDTree(max(len(locked), 1))
for li, (lc, _, _) in enumerate(locked):
    lock_kd.insert(lc, li)
lock_kd.balance()
lock_reach = max((lr for _, lr, _ in locked), default=0.0) * 1.6
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

    # Inside (or near) a bone-locked feature: blend toward 100% that bone.
    best, best_f = None, 0.0
    for _, li, dist in (lock_kd.find_range(v.co, lock_reach) if locked else ()):
        lc, lr, lb = locked[li]
        if dist < lr * 1.6:
            f = min(1.0, (lr * 1.6 - dist) / (lr * 0.6))
            if f > best_f:
                best, best_f = lb, f
    if best:
        total = sum(weights.values()) or 1.0
        weights = {k: val / total * (1.0 - best_f) for k, val in weights.items()}
        weights[best] = weights.get(best, 0.0) + best_f

    top = sorted(weights.items(), key=lambda kv: -kv[1])[:4]
    tt = sum(val for _, val in top) or 1.0
    for bone, val in top:
        if bone not in vg:
            vg[bone] = body.vertex_groups.new(name=bone)
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

# ----------------------------------------------------------------------------- 7) hard-surface machine parts
stage("machine parts")
import zombie_machine
machine_regions = zombie_machine.build(T, body, src, sm, src_weights, src_dom, BONE_REGION, bone_pos, has_bone,
                                       eye_pts, FORWARD, UP, U, rng, stage)

# ----------------------------------------------------------------------------- 8) rig, save, export
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
me = body.data
manifest = dict(name=name, archetype=args.archetype, seed=args.seed, templates=T,
                machine_regions=machine_regions, verts=len(me.vertices),
                tris=sum(len(p.vertices) - 2 for p in me.polygons),
                materials=[m.name for m in me.materials])
with open(os.path.join(args.out, name + ".json"), "w") as f:
    json.dump(manifest, f, indent=2)
stage("done: %s (%d verts, %d tris)" % (fbx_path, manifest["verts"], manifest["tris"]))

# ----------------------------------------------------------------------------- 9) previews
if not args.no_render:
    stage("previews")
    import preview_render
    preview_render.render(body, arm, os.path.join(args.out, "previews"), name, args.archetype)
    stage("previews done")
