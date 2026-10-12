"""扫描 IL2CPP metadata 的 string literal,支持查找/替换(原地,等长或更短)。"""
import struct, os, sys, json

META = r"E:\AQbot\NoneBo2\fgoapk\work\global-metadata.dat"
SL_OFF, SL_SIZE = 0x110, 55440
SLD_OFF, SLD_SIZE = 0xd9a0, 187480


class Meta:
    def __init__(self, path=META):
        self.d = bytearray(open(path, "rb").read())
        self.n = SL_SIZE // 8
        self.lits = []
        for i in range(self.n):
            ln, di = struct.unpack_from("<Ii", self.d, SL_OFF + i * 8)
            self.lits.append((ln, di))

    def text(self, i):
        ln, di = self.lits[i]
        return bytes(self.d[SLD_OFF + di: SLD_OFF + di + ln])

    def find(self, s):
        """返回值等于 s 的 literal 表项索引。"""
        b = s.encode("utf-8")
        return [i for i in range(self.n) if self.lits[i][0] == len(b) and self.text(i) == b]

    def contains(self, s):
        b = s.encode("utf-8")
        return [(i, self.lits[i][0]) for i in range(self.n) if b in self.text(i)]

    def replace(self, i, new):
        """原地替换为 new(字节数必须 <= 原长度),同步更新 length。"""
        nb = new.encode("utf-8")
        ln, di = self.lits[i]
        if len(nb) > ln:
            raise ValueError("too long: %d > %d" % (len(nb), ln))
        self.d[SLD_OFF + di: SLD_OFF + di + ln] = nb + b"\x00" * (ln - len(nb))
        struct.pack_into("<I", self.d, SL_OFF + i * 8, len(nb))
        self.lits[i] = (len(nb), di)

    def save(self, path):
        open(path, "wb").write(self.d)


if __name__ == "__main__":
    m = Meta()
    print("literal 表项:", m.n)
    for t in ("撤退する", "やり直す", "再開する", "閉じる", "次へ", "達成率",
              "利用規約", "同意する", "プレイヤー", "ホーム", "設定"):
        exact = m.find(t)
        if exact:
            print("  %-12s 精确匹配 表项%s len=%d" % (t, exact, m.lits[exact[0]][0]))
        else:
            c = m.contains(t)
            print("  %-12s 无精确匹配,含子串的 %d 项: %s" %
                  (t, len(c), [(i, m.lits[i][1], m.text(i)[:40]) for i, _ in c[:3]]))
