# M3 常用样本首屏命中率

## 样本

- **500 字**：取 `378fac9`（M3 扩表前）常用单字表按拼音组顺序去重后的前 500 个汉字；每个字取它在旧表中首次出现的读音。数据在 `tools/ime-data/common-500.tsv`。这不是独立频率表，目的是验证“扩表没有把原来的常用字挤出首屏”。
- **100 词组**：取当前 `ime_phrase_table` 的前 100 个词组；数据在 `tools/ime-data/common-100-phrases.tsv`。

## 方法

`tools/host-tests/ime_firstpage_test.c` 直接调用 IME 核心：`ime_reset()` 后逐字母输入拼音，检查 `ime_candidate_get()` 返回的第一页（默认每页 9 个）是否包含目标字/词组。测试不加载 `/data` 词频，避免历史频率干扰。

## 结果

| 样本 | 首屏命中 | 说明 |
|---|---:|---|
| 常用 500 字 | **472/500 = 94.4%** | 28 字只在第 2 页及以后；全部仍在词表/字库中 |
| 常用 100 词组 | **100/100** | 词组优先顺序未被扩表破坏 |

28 个未进首屏的字：

```text
察(cha) 第(di) 低(di) 弟(di) 底(di) 递(di) 帝(di) 敌(di) 滴(di)
点(dian) 电(dian) 店(dian) 典(dian) 垫(dian) 殿(dian)
杜(du) 犯(fan) 扶(fu) 符(fu) 浮(fu) 妇(fu)
恭(gong) 谷(gu) 鼓(gu) 估(gu) 姑(gu)
罐(guan) 惯(guan)
```

这些未命中不是缺字：`di`、`dian`、`gu` 等组候选超过 9 个，首屏只放前 9 个；字表和 `font_cjk_big` 仍覆盖全部 3,769 个去重字。需要继续提升首屏命中时，应做“常用字优先”排序，而不是再扩字表。

## 复现

```bash
bash tools/host-tests/run_host_tests.sh
cat /tmp/hepu_host_tests/ime_firstpage.run.log
```

本次结果同时记录在 `/tmp/m3-host7.log`，host 套件 22/22。
