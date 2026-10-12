"""把译文回填进 global-metadata.dat(改 length + 补 NUL,文件大小不变)。"""
import json, struct, os, sys
sys.path.insert(0, r"E:\AQbot\NoneBo2\fgoapk")
from meta_tool import Meta, SL_OFF, SLD_OFF

SRC = r"E:\AQbot\NoneBo2\fgoapk\work\global-metadata.dat"
TR = r"E:\AQbot\NoneBo2\fgoapk\work\tr_meta.json"
OUT = r"E:\AQbot\NoneBo2\fgoapk\work\global-metadata-zh.dat"

tr = json.load(open(TR, encoding="utf-8"))
m = Meta(SRC)
ok = miss = toolong = 0
problems = []
for k, zh in tr.items():
    i = int(k)
    old_b = m.text(i)
    try:
        old_s = old_b.decode("utf-8")
    except Exception:
        problems.append((k, "原文字节非 UTF-8"))
        miss += 1
        continue
    if old_b.rstrip(b"\x00") != old_s.encode("utf-8"):
        pass
    nb = zh.encode("utf-8")
    ln = m.lits[i][0]
    if len(nb) > ln:
        toolong += 1
        problems.append((k, "译文过长 %d > %d: %s" % (len(nb), ln, old_s[:30])))
        continue
    m.d[SLD_OFF + m.lits[i][1]: SLD_OFF + m.lits[i][1] + ln] = nb + b"\x00" * (ln - len(nb))
    struct.pack_into("<I", m.d, SL_OFF + i * 8, len(nb))
    ok += 1

print("成功 %d, 过长 %d, 其他失败 %d" % (ok, toolong, miss))
for p in problems[:15]:
    print("  ", p)
m.save(OUT)
print("输出", OUT, os.path.getsize(OUT), "bytes (源", os.path.getsize(SRC), ")")
