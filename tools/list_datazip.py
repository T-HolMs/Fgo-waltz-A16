import zipfile, struct, sys, os, json

APK = r"E:\AQbot\NoneBo2\fgoapk\FGO_W_hook.apk"
OUT = r"E:\AQbot\NoneBo2\fgoapk\datazip_list.txt"

z = zipfile.ZipFile(APK)
i = z.getinfo("assets/fgowboot/data.zip")
f = open(APK, "rb")
f.seek(i.header_offset)
h = f.read(30)
nlen, elen = struct.unpack_from("<HH", h, 26)
base = i.header_offset + 30 + nlen + elen
size = i.file_size

# EOCD
f.seek(base + size - 22)
eocd = f.read(22)
assert eocd[:4] == b"PK\x05\x06", eocd[:4]
nent, cdsize, cdoff = struct.unpack_from("<HII", eocd, 10)
print("entries=%d cdsize=%d cdoff=%d" % (nent, cdsize, cdoff))

f.seek(base + cdoff)
cd = f.read(cdsize)

entries = []
p = 0
while p < len(cd) and cd[p:p+4] == b"PK\x01\x02":
    (sig, ver, vermade, flag, comp, ttime, tdate, crc, csz, usz,
     nlen2, elen2, clen2, disk, iattr, eattr, lho) = struct.unpack_from("<IHHHHHHIIIHHHHHII", cd, p)
    name = cd[p+46:p+46+nlen2].decode("utf-8", "replace")
    entries.append((name, csz, usz, comp, lho))
    p += 46 + nlen2 + elen2 + clen2

print("parsed %d entries" % len(entries))
with open(OUT, "w", encoding="utf-8") as o:
    for name, csz, usz, comp, lho in entries:
        o.write("%-80s %12d %12d %d\n" % (name, csz, usz, comp))
print("written", OUT)

# 汇总目录
dirs = {}
for name, csz, usz, comp, lho in entries:
    top = name.split("/")[0] if "/" in name else "(root)"
    d = dirs.setdefault(top, [0, 0])
    d[0] += 1
    d[1] += usz
print()
print("%-40s %8s %14s" % ("top-level", "count", "size"))
for k in sorted(dirs, key=lambda x: -dirs[x][1]):
    print("%-40s %8d %14d" % (k, dirs[k][0], dirs[k][1]))

# 按扩展名汇总
exts = {}
for name, csz, usz, comp, lho in entries:
    e = os.path.splitext(name)[1].lower() or "(none)"
    d = exts.setdefault(e, [0, 0])
    d[0] += 1
    d[1] += usz
print()
print("%-20s %8s %14s" % ("ext", "count", "size"))
for k in sorted(exts, key=lambda x: -exts[x][1])[:30]:
    print("%-20s %8d %14d" % (k, exts[k][0], exts[k][1]))
