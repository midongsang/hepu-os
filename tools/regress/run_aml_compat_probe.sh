#!/bin/bash
# ============================================================================
# ACPI 语料兼容性探针 —— 工作树源码版（第二轮 after 对比）。
#
# 与 run_aml_corpus_probe.sh 的区别：
#   - 编译探针时链接【工作树当前 aml64.c/.h、ec64.c/.h】，不导出 HEAD 快照；
#   - 证据写到 docs/status/evidence-aml-compat-2026-11-27/，不覆盖审计线基线；
#   - 基线 before 指标取自 evidence-aml-corpus-2026-11-27/probe-results.tsv。
#
# 只读审计：不改内核源码、不 commit。语料目录缺失时先跑 prepare_aml_corpus.py。
#
# 用法:
#   bash tools/regress/run_aml_compat_probe.sh
#
# 环境变量:
#   CORPUS_DIR    真实/还原/固件语料目录
#   EVIDENCE_DIR  证据输出目录
#   WORK          临时编译/清单目录
# ============================================================================
set -u
cd "$(dirname "$0")/../.."

WT="$(pwd)"
HEAD_SHA="$(git rev-parse HEAD)"
echo "[compat-probe] HEAD=$HEAD_SHA"

EVIDENCE_DIR="${EVIDENCE_DIR:-$WT/docs/status/evidence-aml-compat-2026-11-27}"
CORPUS_DIR="${CORPUS_DIR:-/home/user/.doubao/agent_mode/workspace/.sessions/38452875115179010/agents/s_000K1zmR5nS/scratch/dl}"
FIXTURE_DIR="${FIXTURE_DIR:-$EVIDENCE_DIR/fixtures}"
WORK="${WORK:-/home/user/.doubao/agent_mode/workspace/.sessions/38452875115179010/agents/s_000K1zmReNJ/scratch/compat-probe-work}"

mkdir -p "$WORK" "$EVIDENCE_DIR/logs" "$FIXTURE_DIR"

# 合成夹具（离线生成，不联网）
python3 "$WT/tools/regress/make_corpus_fixtures.py" "$FIXTURE_DIR"

{
  echo "HEAD=$HEAD_SHA"
  echo "--- git status --short ---"
  git status --short
} > "$EVIDENCE_DIR/logs/git-status-before.txt"

# 编译探针（工作树源码，ASan/UBSan）
CC="${CC:-gcc}"
CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-common -w"
ARCH="$WT/kernel/arch/x86_64"
if ! $CC $CFLAGS -DAML_HOST_TEST -I"$ARCH" -I"$ARCH/include" \
    -o "$WORK/aml_corpus_probe" \
    "$WT/tools/host-tests/aml_corpus_probe.c" \
    "$ARCH/aml64.c" "$ARCH/ec64.c" \
    2> "$EVIDENCE_DIR/logs/probe-build.log"; then
  echo "[compat-probe] 编译失败，见 logs/probe-build.log"
  exit 1
fi
echo "[compat-probe] 编译成功 -> $WORK/aml_corpus_probe"

# 收集语料清单（真实 + 还原 + 固件 + 合成）
: > "$WORK/table-list.txt"
if [ -d "$CORPUS_DIR" ]; then
  find "$CORPUS_DIR" -maxdepth 1 -type f \( -name '*.aml' -o -name '*.dat' \) \
    | sort >> "$WORK/table-list.txt"
  find "$CORPUS_DIR/firmware" -type f -name '*.aml' 2>/dev/null \
    | sort >> "$WORK/table-list.txt"
fi
if [ -d "$FIXTURE_DIR" ]; then
  find "$FIXTURE_DIR" -type f -name '*.aml' | sort >> "$WORK/table-list.txt"
fi
echo "[compat-probe] 待跑表数: $(wc -l < "$WORK/table-list.txt")"

: > "$EVIDENCE_DIR/logs/probe-raw.log"
while IFS= read -r t; do
  [ -z "$t" ] && continue
  echo "[compat-probe] $t"
  "$WORK/aml_corpus_probe" "$t" >> "$EVIDENCE_DIR/logs/probe-raw.log" 2>&1
done < "$WORK/table-list.txt"

# 离线结构普查 + 探针汇总（生成 after 结果表）
# manifest 复用审计线基线清单（同一批语料，文件未变）；只取 kind/来源列。
BASE_MANIFEST="$WT/docs/status/evidence-aml-corpus-2026-11-27/corpus-manifest.tsv"
if [ ! -f "$EVIDENCE_DIR/corpus-manifest.tsv" ] && [ -f "$BASE_MANIFEST" ]; then
  cp "$BASE_MANIFEST" "$EVIDENCE_DIR/corpus-manifest.tsv"
fi

python3 "$WT/tools/regress/aml_census.py" \
  --corpus "$CORPUS_DIR" --fixtures "$FIXTURE_DIR" \
  --evidence "$EVIDENCE_DIR" \
  --manifest "$EVIDENCE_DIR/corpus-manifest.tsv"

python3 "$WT/tools/regress/summarize_probe.py" \
  --log "$EVIDENCE_DIR/logs/probe-raw.log" \
  --manifest "$EVIDENCE_DIR/corpus-manifest.tsv" \
  --out "$EVIDENCE_DIR/probe-results.tsv"

python3 "$WT/tools/regress/build_stop_check.py" \
  "$CORPUS_DIR" "$FIXTURE_DIR" \
  "$EVIDENCE_DIR/logs/build-stop-analysis.tsv"

{
  echo "HEAD=$HEAD_SHA"
  echo "--- git status --short (after) ---"
  git status --short
} > "$EVIDENCE_DIR/logs/git-status-after.txt"

echo "[compat-probe] 完成。日志: $EVIDENCE_DIR/logs/"
