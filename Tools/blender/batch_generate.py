# Tools/blender/batch_generate.py
#
# Generates many zombie bodies in parallel (one headless Blender per body) and builds a
# labelled contact sheet (one row per archetype). Run with system Python (needs Pillow):
#
#   python Tools/blender/batch_generate.py --archetypes Husk Bloat Gaunt --count 6 --jobs 6
#
# Outputs per body (Tools/blender/output/): <Arch>_<NNN>.blend / .fbx / .json + previews/,
# logs in output/logs/, and output/previews/batch_<view>.png.

import argparse, json, os, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor, as_completed

HERE = os.path.dirname(os.path.abspath(__file__))
BLENDER = r"C:\Program Files\Blender Foundation\Blender 5.0\blender.exe"

ap = argparse.ArgumentParser()
ap.add_argument("--archetypes", nargs="+", default=["Husk", "Bloat", "Gaunt"])
ap.add_argument("--count", type=int, default=6, help="bodies per archetype")
ap.add_argument("--start-seed", type=int, default=1)
ap.add_argument("--jobs", type=int, default=max(1, min(8, (os.cpu_count() or 4) // 2)))
ap.add_argument("--voxel", type=float, default=0.003)
ap.add_argument("--tris", type=int, default=60000)
ap.add_argument("--view", default="threequarter", choices=["front", "threequarter", "side"])
ap.add_argument("--no-render", action="store_true")
args = ap.parse_args()

out_dir = os.path.join(HERE, "output")
log_dir = os.path.join(out_dir, "logs")
os.makedirs(log_dir, exist_ok=True)
fbx = os.path.join(HERE, "input", "SKM_Manny_Simple.fbx")


def run(arch, seed):
    name = "%s_%03d" % (arch, seed)
    log = os.path.join(log_dir, name + ".log")
    cmd = [BLENDER, "--background", "--factory-startup", "--python", os.path.join(HERE, "zombie_body_gen.py"), "--",
           "--fbx", fbx, "--archetype", arch, "--seed", str(seed), "--out", out_dir,
           "--voxel", str(args.voxel), "--tris", str(args.tris)]
    if args.no_render:
        cmd.append("--no-render")
    t0 = time.time()
    with open(log, "w", encoding="utf-8", errors="replace") as f:
        rc = subprocess.call(cmd, stdout=f, stderr=subprocess.STDOUT)
    return name, rc, time.time() - t0, log


jobs = [(a, s) for a in args.archetypes for s in range(args.start_seed, args.start_seed + args.count)]
print("[BATCH] %d bodies, %d at a time" % (len(jobs), args.jobs), flush=True)
t_all = time.time()
results = {}
with ThreadPoolExecutor(max_workers=args.jobs) as pool:
    futs = [pool.submit(run, a, s) for a, s in jobs]
    for fut in as_completed(futs):
        name, rc, dt, log = fut.result()
        results[name] = rc
        status = "ok" if rc == 0 and os.path.exists(os.path.join(out_dir, name + ".json")) else "FAILED (see %s)" % log
        print("[BATCH] %-10s %5.1fs  %s" % (name, dt, status), flush=True)
print("[BATCH] all done in %.0fs" % (time.time() - t_all), flush=True)

if args.no_render:
    sys.exit(0)

# ---------------------------------------------------------------- contact sheet
from PIL import Image, ImageDraw

SHORT = {"normal": "", "machine": "MACH", "machine_plate": "MACH", "machine_core": "MACH"}
cell_w, cell_h, label_h = 360, 480, 58
rows = args.archetypes
cols = args.count
sheet = Image.new("RGB", (cell_w * cols, (cell_h + label_h) * len(rows)), (18, 20, 28))
draw = ImageDraw.Draw(sheet)
for r, arch in enumerate(rows):
    for c in range(cols):
        name = "%s_%03d" % (arch, args.start_seed + c)
        x, y = c * cell_w, r * (cell_h + label_h)
        img = os.path.join(out_dir, "previews", "%s_%s.png" % (name, args.view))
        if os.path.exists(img):
            sheet.paste(Image.open(img).convert("RGB").resize((cell_w, cell_h)), (x, y + label_h))
        meta = os.path.join(out_dir, name + ".json")
        lines = [name]
        if os.path.exists(meta):
            T = json.load(open(meta))["templates"]
            parts = ["%s:%s" % (k, SHORT.get(v, v)) for k, v in T.items() if SHORT.get(v, v)]
            lines += [", ".join(parts[:3]), ", ".join(parts[3:])]
        else:
            lines.append("FAILED")
        for i, line in enumerate(lines):
            draw.text((x + 8, y + 4 + i * 17), line, fill=(235, 235, 245) if i == 0 else (175, 180, 200))
sheet_path = os.path.join(out_dir, "previews", "batch_%s.png" % args.view)
sheet.save(sheet_path)
print("[BATCH] contact sheet: %s (%dx%d)" % (sheet_path, sheet.width, sheet.height), flush=True)
