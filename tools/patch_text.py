"""按字节等长替换 bundle 内的 UTF-8 文本(不动 length 字段,文件大小不变)。"""
import sys, os, json, re

PATH = sys.argv[1]
OUT = sys.argv[2]
PAIRS = json.load(open(sys.argv[3], encoding="utf-8"))   # {"旧": "新", ...}

b = bytearray(open(PATH, "rb").read())
total = 0
for old, new in PAIRS.items():
    ob = old.encode("utf-8")
    nb = new.encode("utf-8")
    if len(nb) != len(ob):
        print("  !! 字节长度不等,跳过: %r(%d) -> %r(%d)" % (old, len(ob), new, len(nb)))
        continue
    c = b.count(ob)
    if c == 0:
        print("  -- 未找到: %r" % old)
        continue
    b = bytearray(bytes(b).replace(ob, nb))
    total += c
    print("  ok  %-18s -> %-18s x%d" % (old, new, c))

open(OUT, "wb").write(bytes(b))
print("替换处数: %d ; 输出 %s (%d bytes, 源 %d)" % (total, OUT, os.path.getsize(OUT), os.path.getsize(PATH)))
