"""检查游戏字体对简体中文的覆盖情况。"""
import os, sys
from fontTools.ttLib import TTFont

FD = r"E:\AQbot\NoneBo2\fgoapk\work\fonts"

# 只出现在简体中文、日文字形通常缺失的字
SIMPLIFIED_ONLY = "关闭说听发门从务达继设备购确择时数据录游戏条预约谈让读认识铁银钱车东买卖"


def cmap_of(path):
    f = TTFont(path, fontNumber=0, lazy=True)
    cm = set()
    for t in f["cmap"].tables:
        try:
            cm |= set(t.cmap.keys())
        except Exception:
            pass
    f.close()
    return cm


files = [f for f in os.listdir(FD) if f.endswith(".ttf")]
print("%-24s %8s %8s %8s" % ("font", "glyphs", "汉字总数", "缺简体字"))
for fn in sorted(files):
    p = os.path.join(FD, fn)
    try:
        cm = cmap_of(p)
    except Exception as e:
        print("%-24s 读取失败 %s" % (fn, e))
        continue
    han = [c for c in cm if 0x4E00 <= c <= 0x9FFF]
    miss = [c for c in SIMPLIFIED_ONLY if ord(c) not in cm]
    print("%-24s %8d %8d %8d" % (fn, len(cm), len(han), len(miss)))

print()
# 细看最主要的中文字体
for fn in sorted(files):
    if "Skip" not in fn and "TSUKUMIN" not in fn:
        continue
    p = os.path.join(FD, fn)
    try:
        cm = cmap_of(p)
    except Exception:
        continue
    miss = [c for c in SIMPLIFIED_ONLY if ord(c) not in cm]
    have = [c for c in SIMPLIFIED_ONLY if ord(c) in cm]
    print("%s" % fn)
    print("  已有: %s" % "".join(have))
    print("  缺失: %s" % "".join(miss))
