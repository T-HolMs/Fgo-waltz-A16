"""从 APK 内嵌的 data.zip(STORED)里按前缀提取文件。

data.zip 在 APK 内未压缩,所以可以给 zipfile 一个"窗口视图"来随机访问,
不必把 800MB 读进内存。
"""
import zipfile, struct, io, os, sys


class Window(io.RawIOBase):
    """把 [base, base+size) 呈现为一个支持 seek 的只读文件对象。"""

    def __init__(self, f, base, size):
        self.f = f
        self.base = base
        self.size = size
        self.pos = 0

    def readable(self):
        return True

    def seekable(self):
        return True

    def seek(self, off, whence=0):
        if whence == 0:
            self.pos = off
        elif whence == 1:
            self.pos += off
        elif whence == 2:
            self.pos = self.size + off
        return self.pos

    def tell(self):
        return self.pos

    def read(self, n=-1):
        if n is None or n < 0:
            n = self.size - self.pos
        n = min(n, self.size - self.pos)
        if n <= 0:
            return b""
        self.f.seek(self.base + self.pos)
        d = self.f.read(n)
        self.pos += len(d)
        return d

    def readinto(self, b):
        d = self.read(len(b))
        b[:len(d)] = d
        return len(d)


def open_datazip(apk_path, name="assets/fgowboot/data.zip"):
    z = zipfile.ZipFile(apk_path)
    i = z.getinfo(name)
    f = open(apk_path, "rb")
    f.seek(i.header_offset)
    h = f.read(30)
    nlen, elen = struct.unpack_from("<HH", h, 26)
    base = i.header_offset + 30 + nlen + elen
    return zipfile.ZipFile(Window(f, base, i.file_size)), f


if __name__ == "__main__":
    apk = r"E:\AQbot\NoneBo2\fgoapk\FGO_W_hook.apk"
    prefix = sys.argv[1] if len(sys.argv) > 1 else "com.aniplex.fgowaltz/files/UnityCache/"
    outdir = sys.argv[2] if len(sys.argv) > 2 else r"E:\AQbot\NoneBo2\fgoapk\work\uc"
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else 0

    os.makedirs(outdir, exist_ok=True)
    z2, f = open_datazip(apk)
    names = [n for n in z2.namelist() if n.startswith(prefix) and not n.endswith("/")]
    names.sort(key=lambda n: -z2.getinfo(n).file_size)
    if limit:
        names = names[:limit]
    print("extracting %d files -> %s" % (len(names), outdir))
    for n in names:
        rel = n[len(prefix):].replace("/", "__")
        out = os.path.join(outdir, rel)
        with z2.open(n) as s, open(out, "wb") as d:
            while True:
                b = s.read(4 << 20)
                if not b:
                    break
                d.write(b)
    print("done")
