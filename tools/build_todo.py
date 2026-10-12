"""生成最终待译清单: 去重、排除测试产物、标注来源。"""
import json, re, os, collections

SRC = r"E:\AQbot\NoneBo2\fgoapk\work\all_text.json"
OUT_JSON = r"E:\AQbot\NoneBo2\fgoapk\work\to_translate.json"
OUT_TXT = r"E:\AQbot\NoneBo2\fgoapk\work\to_translate.txt"

SKIP_BUNDLES = ("poc_out.bundle", "r2s_final.bin", "r2s_patched.bin",
                "r2s_font.bin", "font_out.bundle")

kana = re.compile(r"[\u3040-\u309F\u30A0-\u30FF]")
cjk = re.compile(r"[\u4E00-\u9FFF]")
latin = re.compile(r"[A-Za-z]")

items = json.load(open(SRC, encoding="utf-8"))
items = [i for i in items if i["bundle"] not in SKIP_BUNDLES]

ui = [i for i in items if i["kind"] == "ui"]
ta = [i for i in items if i["kind"] == "textasset"]

# UI: 含假名或汉字的才需要翻译;排除单字符(逐字动画)
ui_need = [i for i in ui if (kana.search(i["text"]) or cjk.search(i["text"]))
           and len(i["text"].strip()) > 1]

by_text = collections.OrderedDict()
for i in ui_need:
    by_text.setdefault(i["text"], []).append(i)

# TextAsset 分类
scen = [i for i in ta if i["pathid"].endswith("_scenario")]
lyr = [i for i in ta if i["pathid"].endswith("_lyrics")]
score = [i for i in ta if re.match(r"^\d+_score", i["pathid"])]
other = [i for i in ta if i not in scen and i not in lyr and i not in score]

out = {
    "ui": [{"jp": k, "count": len(v), "bundles": sorted({x["bundle"] for x in v})}
           for k, v in by_text.items()],
    "scenario": [{"name": i["pathid"], "bundle": i["bundle"], "text": i["text"]} for i in scen],
    "lyrics": [{"name": i["pathid"], "bundle": i["bundle"], "text": i["text"]} for i in lyr],
    "other": [{"name": i["pathid"], "bundle": i["bundle"], "chars": len(i["text"]),
               "text": i["text"]} for i in other],
    "score_count": len(score),
}
json.dump(out, open(OUT_JSON, "w", encoding="utf-8"), ensure_ascii=False, indent=1)

with open(OUT_TXT, "w", encoding="utf-8") as f:
    f.write("=== UI 文本 (%d 条去重, 含日文/汉字) ===\n" % len(out["ui"]))
    for e in out["ui"]:
        f.write("%s\n" % e["jp"].replace("\n", "\\n"))
    f.write("\n\n=== scenario (%d) ===\n" % len(out["scenario"]))
    for e in out["scenario"]:
        f.write("\n----- %s -----\n%s\n" % (e["name"], e["text"]))
    f.write("\n\n=== lyrics (%d) ===\n" % len(out["lyrics"]))
    for e in out["lyrics"]:
        f.write("\n----- %s -----\n%s\n" % (e["name"], e["text"]))
    f.write("\n\n=== other (%d) ===\n" % len(out["other"]))
    for e in out["other"]:
        f.write("\n----- %s (%d chars) -----\n%s\n" % (e["name"], e["chars"], e["text"][:3000]))

print("UI 待译(去重): %d 条, 字符 %d" % (len(out["ui"]), sum(len(e["jp"]) for e in out["ui"])))
print("scenario: %d 个, 字符 %d" % (len(scen), sum(len(e["text"]) for e in scen)))
print("lyrics:   %d 个, 字符 %d" % (len(lyr), sum(len(e["text"]) for e in lyr)))
print("other:    %d 个, 字符 %d" % (len(other), sum(len(e["text"]) for e in other)))
print("score:    %d 个(不翻译)" % len(score))
print()
print("清单:", OUT_TXT)
print("JSON:", OUT_JSON)
