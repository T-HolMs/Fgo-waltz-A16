import zipfile, shutil, sys, os

SRC = sys.argv[1] if len(sys.argv) > 1 else r"E:\AQbot\NoneBo2\成品\FGO_W_fixed.apk"
DST = sys.argv[2] if len(sys.argv) > 2 else r"E:\AQbot\NoneBo2\fgoapk\_hooked.apk"

ARM64_NEW = r"E:\AQbot\NoneBo2\fgoapk\hooksrc\libfgowspoof_arm64.so"
TARGET = "assets/fgowboot/libfgowspoof_arm64.so"

src = zipfile.ZipFile(SRC, "r")
dst = zipfile.ZipFile(DST, "w", zipfile.ZIP_DEFLATED)

n = 0
replaced = False
for info in src.infolist():
    name = info.filename
    if name.startswith("META-INF/"):
        continue

    zi = zipfile.ZipInfo(name, date_time=info.date_time)
    zi.external_attr = info.external_attr
    zi.internal_attr = info.internal_attr
    zi.create_system = info.create_system
    zi.compress_type = info.compress_type

    if name == TARGET:
        with open(ARM64_NEW, "rb") as f:
            data = f.read()
        dst.writestr(zi, data)
        replaced = True
        print("  replaced: %s (%d bytes)" % (name, len(data)))
    else:
        with src.open(name) as inp, dst.open(zi, "w") as out:
            shutil.copyfileobj(inp, out, 8 * 1024 * 1024)
    n += 1

dst.close()
src.close()
print("entries: %d -> %s" % (n, DST))
print("replaced:", replaced)
print("size:", os.path.getsize(DST))
