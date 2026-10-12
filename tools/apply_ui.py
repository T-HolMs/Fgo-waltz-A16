"""把所有 bundle 里的 UI 文本(m_text/m_Text)替换为中文并另存。"""
import os, json, shutil
import UnityPy

UnityPy.config.FALLBACK_UNITY_VERSION = "2020.3.30f1"

SRC = r"E:\AQbot\NoneBo2\fgoapk\work\uc"
SRCB = r"E:\AQbot\NoneBo2\fgoapk\work"
DST = r"E:\AQbot\NoneBo2\fgoapk\work\zh"
TRFILE = r"E:\AQbot\NoneBo2\fgoapk\work\tr_ui.json"

os.makedirs(DST, exist_ok=True)
tr = json.load(open(TRFILE, encoding="utf-8"))
# 统一换行:译文里的 \n 用真实换行
tr = {k: v.replace("\r\n", "\n") for k, v in tr.items()}

SKIP = ("poc_out.bundle", "r2s_final.bin", "r2s_patched.bin", "r2s_font.bin",
        "font_out.bundle", "global-metadata-zh.dat")

files = []
for base in (SRC, SRCB):
    if not os.path.isdir(base):
        continue
    for n in os.listdir(base):
        p = os.path.join(base, n)
        if not os.path.isfile(p) or n in SKIP:
            continue
        try:
            with open(p, "rb") as fh:
                if fh.read(7) != b"UnityFS":
                    continue
        except Exception:
            continue
        files.append((n, p))

print("bundle:", len(files))
summary = {}
total = 0
for name, p in sorted(files):
    env = UnityPy.load(p)
    hits = 0
    for o in env.objects:
        if o.type.name != "MonoBehaviour":
            continue
        try:
            dd = o.read()
        except Exception:
            continue
        for f in ("m_text", "m_Text"):
            v = getattr(dd, f, None)
            if not isinstance(v, str) or not v:
                continue
            new = tr.get(v)
            if new is not None and new != v:
                setattr(dd, f, new)
                dd.save()
                hits += 1
            break
    if hits:
        out = os.path.join(DST, name)
        with open(out, "wb") as fh:
            fh.write(env.file.save(packer="original"))
        summary[name] = hits
        total += hits
        print("  %-64s %4d 处" % (name[:64], hits))

print()
print("共修改 %d 处,涉及 %d 个 bundle" % (total, len(summary)))
json.dump(summary, open(os.path.join(DST, "_summary.json"), "w", encoding="utf-8"),
          ensure_ascii=False, indent=1)
