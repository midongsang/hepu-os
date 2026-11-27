#!/bin/bash
# ============================================================================
# ACPI 语料兼容性探针 —— 可复现运行脚本（只读审计，不 commit / 不改内核源码）。
#
# 步骤：
#   1. 记录工作树状态自证（kernel/ 源码未被本审计改动）；
#   2. 从 HEAD 快照导出 aml64.c/.h、ec64.c/.h（不使用工作树里并行改动的版本）；
#   3. 编译探针（-DAML_HOST_TEST，ASan/UBSan）；
#   4. 遍历语料目录逐表建库 + 枚举 + 求值，原始日志落证据目录；
#   5. 跑离线结构普查脚本，输出 opcode / Field 粒度统计表。
#
# 用法:
#   bash tools/regress/run_aml_corpus_probe.sh
#
# 环境变量:
#   CORPUS_DIR   真实/还原/固件语料目录（默认 scratch/dl）
#   FIXTURE_DIR  合成夹具目录（默认证据目录/fixtures）
#   EVIDENCE_DIR 证据输出目录
# ============================================================================
set -u
cd "$(dirname "$0")/../.."

WT="$(pwd)"
HEAD_SHA="$(git rev-parse HEAD)"
echo "[probe] HEAD=$HEAD_SHA"

EVIDENCE_DIR="${EVIDENCE_DIR:-$WT/docs/status/evidence-aml-corpus-2026-11-27}"
CORPUS_DIR="${CORPUS_DIR:-/home/user/.doubao/agent_mode/workspace/.sessions/38452875115179010/agents/s_000K1zmR5nS/scratch/dl}"
FIXTURE_DIR="${FIXTURE_DIR:-$EVIDENCE_DIR/fixtures}"
WORK="${WORK:-/home/user/.doubao/agent_mode/workspace/.sessions/38452875115179010/agents/s_000K1zmR5nS/scratch/probe-work}"

mkdir -p "$WORK" "$EVIDENCE_DIR/logs"

# 1) 工作树状态自证（前后各留一份；本脚本不写 kernel/ 与 docs 既有文件）
{
  echo "HEAD=$HEAD_SHA"
  echo "--- git status --short ---"
  git status --short
} > "$EVIDENCE_DIR/logs/git-status-before.txt"

# 2) HEAD 快照导出
git show "HEAD:kernel/arch/x86_64/aml64.c" > "$WORK/aml64_snapshot.c"
git show "HEAD:kernel/arch/x86_64/aml64.h" > "$WORK/aml64.h"
git show "HEAD:kernel/arch/x86_64/ec64.c"  > "$WORK/ec64.c"
git show "HEAD:kernel/arch/x86_64/ec64.h"  > "$WORK/ec64.h"

# 3) 编译探针
CC="${CC:-gcc}"
CFLAGS="-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -fno-common -w"
if ! $CC $CFLAGS -DAML_HOST_TEST -I"$WORK" -I"$WT/kernel/arch/x86_64" \
    -o "$WORK/aml_corpus_probe" \
    "$WT/tools/host-tests/aml_corpus_probe.c" \
    "$WORK/aml64_snapshot.c" "$WORK/ec64.c" \
    2> "$EVIDENCE_DIR/logs/probe-build.log"; then
  echo "[probe] 编译失败，见 logs/probe-build.log"
  exit 1
fi
echo "[probe] 编译成功 -> $WORK/aml_corpus_probe"

# 4) 收集语料清单（真实 + 还原 + 固件 + 合成）
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
echo "[probe] 待跑表数: $(wc -l < "$WORK/table-list.txt")"

: > "$EVIDENCE_DIR/logs/probe-raw.log"
while IFS= read -r t; do
  [ -z "$t" ] && continue
  echo "[probe] $t"
  "$WORK/aml_corpus_probe" "$t" >> "$EVIDENCE_DIR/logs/probe-raw.log" 2>&1
done < "$WORK/table-list.txt"

# 5) 离线结构普查 + 探针结果汇总
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

echo "[probe] 完成。日志: $EVIDENCE_DIR/logs/"
