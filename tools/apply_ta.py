"""回填 TextAsset 译文: scenario 整段替换、terms_ar 整段替换、end_roll 标签替换。"""
import os, json, re
import UnityPy

UnityPy.config.FALLBACK_UNITY_VERSION = "2020.3.30f1"

W = r"E:\AQbot\NoneBo2\fgoapk\work"
SRC_DIRS = [os.path.join(W, "zh"), os.path.join(W, "uc"), W]
DST = os.path.join(W, "zh_ta")
os.makedirs(DST, exist_ok=True)

scen = {}
for f in ("tr_scen1.json", "tr_scen2.json"):
    scen.update(json.load(open(os.path.join(W, f), encoding="utf-8")))
other = json.load(open(os.path.join(W, "tr_other.json"), encoding="utf-8"))
TAGS = other["tags"]
TERMS = other["terms_ar"]

SKIP = ("poc_out.bundle", "r2s_final.bin", "r2s_patched.bin", "r2s_font.bin",
        "font_out.bundle", "global-metadata-zh.dat")

# 收集所有 bundle,优先用已改过 UI 的版本
files = {}
for d in (os.path.join(W, "zh"), os.path.join(W, "uc")):
    if not os.path.isdir(d):
        continue
    for n in os.listdir(d):
        p = os.path.join(d, n)
        if not os.path.isfile(p) or n in SKIP:
            continue
        try:
            with open(p, "rb") as fh:
                if fh.read(7) != "UnityFS".encode():
                    continue
        except Exception:
            continue
        files.setdefault(n, p)

print("bundle:", len(files))

# 先确保所有 UI 已改的 bundle 都进入输出目录(即使本次没有 TextAsset 改动)
import shutil
ui_dir = os.path.join(W, "zh")
for n in os.listdir(ui_dir) if os.path.isdir(ui_dir) else []:
    if n.endswith(".json"):
        continue
    src = os.path.join(ui_dir, n)
    dst = os.path.join(DST, n)
    if os.path.isfile(src) and not os.path.exists(dst):
        shutil.copy2(src, dst)

stats = {"scenario": 0, "terms": 0, "tags": 0}
for name, p in sorted(files.items()):
    env = UnityPy.load(p)
    changed = False
    for o in env.objects:
        if o.type.name != "TextAsset":
            continue
        try:
            dd = o.read()
            nm = dd.m_Name
            s = dd.m_Script
            if isinstance(s, bytes):
                s = s.decode("utf-8")
        except Exception:
            continue
        new = s
        if nm in scen:
            new = scen[nm]
            if new != s:
                stats["scenario"] += 1
        elif nm == "terms_ar":
            new = TERMS
            if new != s:
                stats["terms"] += 1
        elif nm == "end_roll":
            def rep(m):
                t = m.group(1)
                return "【%s】" % TAGS.get(t, t)
            new = re.sub(r"【([^】]+)】", rep, s)
            if new != s:
                stats["tags"] += 1
        if new != s:
            dd.m_Script = new
            dd.save()
            changed = True
    if changed:
        with open(os.path.join(DST, name), "wb") as fh:
            fh.write(env.file.save(packer="original"))

print("scenario 替换 %d, terms_ar %d, end_roll 标签 %d" %
      (stats["scenario"], stats["terms"], stats["tags"]))
# 统计未命中的 scenario
hit = set()
for name in files:
    pass
print("scenario 目标 %d 个" % len(scen))
print("输出目录:", DST)
