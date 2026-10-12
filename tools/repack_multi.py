"""通用 APK 重打包:按 "APK内路径 本地文件" 列表替换条目。

用法:
  python repack_multi.py <源APK> <输出APK> <替换清单.json>
清单 JSON: {"assets/...": "D:\\path\\file", ...}
"""
import zipfile, shutil, sys, os, json

SRC = sys.argv[1]
DST = sys.argv[2]
MAP = json.load(open(sys.argv[3], encoding="utf-8"))

src = zipfile.ZipFile(SRC, "r")
dst = zipfile.ZipFile(DST, "w", zipfile.ZIP_DEFLATED)

done = set()
n = 0
for info in src.infolist():
    name = info.filename
    if name.startswith("META-INF/"):
        continue
    zi = zipfile.ZipInfo(name, date_time=info.date_time)
    zi.external_attr = info.external_attr
    zi.internal_attr = info.internal_attr
    zi.create_system = info.create_system
    zi.compress_type = info.compress_type

    if name in MAP:
        with open(MAP[name], "rb") as f:
            data = f.read()
        dst.writestr(zi, data)
        done.add(name)
        print("  replaced: %-70s %d bytes" % (name, len(data)))
    else:
        with src.open(name) as inp, dst.open(zi, "w") as out:
            shutil.copyfileobj(inp, out, 8 << 20)
    n += 1

dst.close()
src.close()
missing = set(MAP) - done
if missing:
    print("!! 未在 APK 中找到:", missing)
print("entries: %d -> %s (%d bytes)" % (n, DST, os.path.getsize(DST)))
