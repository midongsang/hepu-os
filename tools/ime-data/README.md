# IME 词库数据来源

`ime_data_table.c` 由 `tools/gen_ime_data.py` 生成，输入为本目录下的
`gb2312-level1-pinyin.txt` 以及现有主数据表。

- 字集：GB2312 一级汉字 3,755 字（16–55 区）；现有表保留「呃/鞥」等 14 个
  表外生僻字，因此去重后为 3,769 字。
- 读音：`mozillazg/pinyin-data` v0.16.0，MIT License，
  见 `LICENSE-pinyin-data.txt`。
- 归一化：去掉声调，`ü` 统一写成 `v`（如 `lüè → lve`）。
- 排序：保留现有常用表的组内顺序；新增字按 GB2312 顺序追加。词组表和简拼表
  不由此脚本生成。

重新生成：

```bash
python3 tools/gen_ime_data.py
python3 tools/check_ime_glyphs.py --ttf kernel/arch/x86_64/cjk_font_data.c
```

## 首屏命中率样本

- `common-500.tsv`：`378fac9`（扩表前）常用单字表按拼音组顺序去重后的前 500 字，每行 `拼音<TAB>汉字`；每字取旧表首次出现的读音。
- `common-100-phrases.tsv`：当前 `ime_phrase_table` 的前 100 个词组，每行 `拼音<TAB>词组`。

这两个文件只用于 `tools/host-tests/ime_firstpage_test.c` 的回归测量，不参与内核词表生成。
