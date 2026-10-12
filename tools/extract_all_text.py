"""全量提取所有 bundle 内 MonoBehaviour 的文本字段(m_text/m_Text/...)与 TextAsset。"""
import os, sys, json, collections
import UnityPy

UnityPy.config.FALLBACK_UNITY_VERSION = "2020.3.30f1"

DIRS = [r"E:\AQbot\NoneBo2\fgoapk\work\uc", r"E:\AQbot\NoneBo2\fgoapk\work"]
OUT = r"E:\AQbot\NoneBo2\fgoapk\work\all_text.json"

files = []
for base in DIRS:
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

print("bundle 文件:", len(files))
items = []


def add(kind, bundle, pathid, field, text):
    items.append({"kind": kind, "bundle": os.path.basename(bundle),
                  "pathid": pathid, "field": field, "text": text})


def try_str(o, names):
    try:
        dd = o.read()
    except Exception:
        return None, None
    for f in names:
        v = getattr(dd, f, None)
        if isinstance(v, str):
            return dd, f
    return dd, None


for p in sorted(files):
    bn = os.path.basename(p)
    try:
        env = UnityPy.load(p)
    except Exception as e:
        print("  load fail", bn[:50], e)
        continue
    for o in env.objects:
        if o.type.name == "MonoBehaviour":
            dd, f = try_str(o, ("m_text", "m_Text", "text"))
            if f:
                v = getattr(dd, f)
                if v and v.strip():
                    add("ui", bn, o.path_id, f, v)
        elif o.type.name == "TextAsset":
            try:
                dd = o.read()
                nm = dd.m_Name
                s = dd.m_Script
                if isinstance(s, bytes):
                    s = s.decode("utf-8", "replace")
                add("textasset", bn, nm, "m_Script", s)
            except Exception:
                pass

print("提取条目:", len(items))
c = collections.Counter(i["kind"] for i in items)
print(dict(c))
with open(OUT, "w", encoding="utf-8") as f:
    json.dump(items, f, ensure_ascii=False, indent=1)
print("saved", OUT)
