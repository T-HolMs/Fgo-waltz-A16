"""把 bundle 里的 Font 资源替换为指定 TTF。"""
import os, sys, json
import UnityPy

UnityPy.config.FALLBACK_UNITY_VERSION = "2020.3.30f1"

SRC = sys.argv[1]
DST = sys.argv[2]
NEWTTF = sys.argv[3]
ONLY = sys.argv[4].split(",") if len(sys.argv) > 4 and sys.argv[4] else None

new = open(NEWTTF, "rb").read()
print("新字体: %s (%d bytes)" % (os.path.basename(NEWTTF), len(new)))

# m_FontData 为空的是数字/图标字库,不能替换
MIN_OLD = 512

env = UnityPy.load(SRC)
n = 0
for o in env.objects:
    if o.type.name != "Font":
        continue
    dd = o.read()
    nm = getattr(dd, "m_Name", "?")
    if ONLY and nm not in ONLY:
        print("  skip   %-28s" % nm)
        continue
    fd = dd.m_FontData
    old = len(fd) if fd is not None else 0
    if old < MIN_OLD:
        print("  keep   %-28s %d (空字库,跳过)" % (nm, old))
        continue
    dd.m_FontData = new
    dd.save()
    n += 1
    print("  replace %-28s %d -> %d" % (nm, old, len(new)))

print("replaced:", n)
with open(DST, "wb") as fh:
    fh.write(env.file.save(packer="original"))
print("saved %s (%d bytes, src %d)" % (DST, os.path.getsize(DST), os.path.getsize(SRC)))
