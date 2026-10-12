"""扫描 bundle 的 MonoBehaviour,找出带可显示文本字段的对象(UI Text / TMP)。"""
import os, sys, json, collections
import UnityPy

UnityPy.config.FALLBACK_UNITY_VERSION = "2020.3.30f1"

D = r"E:\AQbot\NoneBo2\fgoapk\work\uc"
D2 = r"E:\AQbot\NoneBo2\fgoapk\work"

TEXTFIELDS = ("m_Text", "m_text", "text")

files = []
for base in (D, D2):
    if not os.path.isdir(base):
        continue
    for n in os.listdir(base):
        p = os.path.join(base, n)
        if not os.path.isfile(p):
            continue
        try:
            with open(p, "rb") as fh:
                if fh.read(7) != b"UnityFS":
                    continue
        except Exception:
            continue
        files.append(p)

print("files:", len(files))
hits = []
classes = collections.Counter()
for p in files:
    try:
        env = UnityPy.load(p)
    except Exception:
        continue
    for o in env.objects:
        if o.type.name != "MonoBehaviour":
            continue
        try:
            dd = o.read()
        except Exception:
            continue
        cls = None
        try:
            s = getattr(dd, "m_Script", None)
            if s is not None and hasattr(s, "m_ClassName"):
                cls = s.m_ClassName
        except Exception:
            pass
        classes[cls or "?"] += 1
        for f in TEXTFIELDS:
            v = getattr(dd, f, None)
            if isinstance(v, str) and v.strip():
                hits.append({"file": os.path.basename(p), "cls": cls, "field": f, "text": v})
                break

print()
print("=== MonoBehaviour 类 Top30 ===")
for k, v in classes.most_common(30):
    print("  %-58s %d" % (k, v))

print()
print("=== 带文本的组件: %d ===" % len(hits))
for h in hits[:40]:
    print("  [%s] %-24s %s" % (h["field"], (h["cls"] or "?")[:24], h["text"][:60].replace("\n", "\\n")))

with open(r"E:\AQbot\NoneBo2\fgoapk\work\uitext.json", "w", encoding="utf-8") as f:
    json.dump(hits, f, ensure_ascii=False, indent=1)
print("saved work/uitext.json")
