#!/bin/bash

# === 路径配置 ===
out_file="$1"
srctree="$2"

overlay_dir=$(echo "$srctree" | sed 's|//|/|g')

if [[ "$overlay_dir" == ..* ]]; then
    overlay_dir="$overlay_dir"
else
    overlay_dir=$(echo "$overlay_dir" | sed 's|\(.*\)\1|\1|')
fi

overlay_dir=$(echo "$overlay_dir" | sed 's|/modules$||; s|$|/modules|')

> "$out_file"

# === 写入文件头 ===
cat <<'EOF' > "$out_file"
#include "overlay_files.h"
#include <linux/stddef.h>
#include <linux/zstd.h>

EOF

# === 关联数组存储信息 ===
declare -A name_map        # name -> array_name
declare -A count_map       # name -> element_count
declare -A orig_size_map   # name -> original_size

# === 临时文件 ===
tmp_xxd="/tmp/overlay_xxd_$$.c"
tmp_comp="/tmp/overlay_comp_$$.ko"

# === 尺寸协议配置 ===
ALIGN_BLOCK=8192   # 8KB 对齐
RESERVE_SIZE=8192  # 额外预留 8KB

# === 处理所有 .ko 文件 ===
shopt -s nullglob
ko_files=("$overlay_dir"/*.ko)
file_idx=0

echo "=== Module Overlay Build Info ==="

for ko in "${ko_files[@]}"; do
    [ -f "$ko" ] || continue

    base=$(basename "$ko")
    name="${base%.ko}"                    # 去掉 .ko
    array_name="${name//[^a-zA-Z0-9]/_}_data"  # 合法 C 标识符

    # 先获取原始大小
    orig_size=$(stat -c%s "$ko")
    
    # 使用 zstd 进行最大压缩
    if ! /usr/bin/zstd -22 -f "$ko" -o "$tmp_comp" >/dev/null 2>&1; then
        echo "zstd compression failed: $ko" >&2
        continue
    fi

    # 计算最终槽位大小 (Slot Size) :
    # 逻辑: (压缩大小 + 8KB预留) -> 向上取整到 8KB 倍数
    raw_comp_size=$(stat -c%s "$tmp_comp")
    
    # 增加预留空间
    size_with_reserve=$(( raw_comp_size + RESERVE_SIZE ))
    
    # 向上取整对齐
    # 公式: (size + align - 1) / align * align
    remainder=$(( size_with_reserve % ALIGN_BLOCK ))
    if [ $remainder -eq 0 ]; then
        final_size=$size_with_reserve
    else
        final_size=$(( size_with_reserve + ALIGN_BLOCK - remainder ))
    fi

    # 日志输出
    echo "  -> Embedded '$name':"
    echo "       Orig Size: $orig_size bytes"
    echo "       ZSTD Raw : $raw_comp_size bytes"
    echo "       Final Slot: $final_size bytes (Aligned to 8KB)"

    # 物理填充 (使用 truncate)
    truncate -s "$final_size" "$tmp_comp"

    # 生成字节数组
    if ! /usr/bin/xxd -i "$tmp_comp" > "$tmp_xxd.raw" 2>/dev/null; then
        echo "xxd failed: $ko" >&2
        continue
    fi

    len_line=$(grep -E 'unsigned int[[:space:]]+.*_len[[:space:]]*=' "$tmp_xxd.raw")
    count=$(echo "$len_line" | awk -F'=' '{print $2}' | awk -F';' '{print $1}' | tr -d ' ')

    if [ -z "$count" ]; then
        echo "Failed to extract _len from xxd output: $ko" >&2
        rm -f "$tmp_xxd.raw"
        continue
    fi

    # 替换：
    # 1. 替换数组定义行
    # 2. 转换 0x... 为 0x...U
    # 3. 删除 _len 定义行
    sed -E \
        -e "s/unsigned char[[:space:]]+([^[]+)[[:space:]]*\[([^]]*)\]/const unsigned char ${array_name}[\2]/" \
        -e 's/0x([0-9a-fA-F]{2})/0x\1U/g' \
        -e 's/-/_/g' \
        -e '/unsigned int[[:space:]]+.*_len[[:space:]]*=/d' \
        "$tmp_xxd.raw" > "$tmp_xxd" || { echo "sed failed: $ko" >&2; rm -f "$tmp_xxd.raw"; continue; }

    rm -f "$tmp_xxd.raw" "$tmp_comp"
    ((file_idx++))

    # 记录
    name_map["$name"]="$array_name"
    count_map["$name"]=$count
    orig_size_map["$name"]=$orig_size

    # 追加数组定义到输出文件
    cat "$tmp_xxd" >> "$out_file"
    echo >> "$out_file"
done

echo "==============================="

# === 生成 overlay_file_list 数组 ===
cat <<EOF >> "$out_file"
// 所有 overlay 模块的描述表
const struct overlay_file overlay_file_list[] = {
EOF

if (( file_idx == 0 )); then
    echo "No .ko files found in $overlay_dir"
else
    for name in "${!name_map[@]}"; do
        array_name="${name_map[$name]}"
        count="${count_map[$name]}"
        orig_size="${orig_size_map[$name]}"
        printf '    { .name = "%s", .data = %s, .len = %d, .orig_size = %d },\n' \
               "$name" "${name_map[$name]}" "${count_map[$name]}" "${orig_size_map[$name]}" >> "$out_file"
    done
fi

cat <<EOF >> "$out_file"
};

const int overlay_file_list_count = $file_idx;

EOF

# === 清理临时文件 ===
rm -f "$tmp_xxd"

# === 统一换行符为 LF ===
if command -v dos2unix >/dev/null 2>&1; then
    dos2unix "$out_file" 2>/dev/null || true
else
    tr -d '\r' < "$out_file" > "${out_file}.tmp" 2>/dev/null && \
        mv "${out_file}.tmp" "$out_file"
fi

echo "Generated $out_file: $file_idx overlay file(s) processed."
