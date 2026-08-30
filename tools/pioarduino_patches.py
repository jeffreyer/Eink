#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
pioarduino 平台补丁：NimBLE + custom_sdkconfig 构建所需

本项目在 platformio.ini 中使用 custom_sdkconfig（CONFIG_PM_ENABLE /
CONFIG_FREERTOS_USE_TICKLESS_IDLE / CONFIG_BT_CTRL_* 等）编译。pioarduino 平台
在该配置下会全量重建 Arduino IDF 库，期间有两个 bug 会导致编译失败：

1. lib_ignore = BLE 会把 IDF 的 bt 组件一并从库构建中剪掉，而 Arduino 自带
   BLE 库需要 Bluedroid 头文件（esp_gatt_defs.h / esp_gap_ble_api.h）；
2. 库构建子环境里读取不到项目的 lib_deps，BT/BLE 保护判断失效。

本脚本对平台文件做精确文本替换，可在干净环境（未打补丁）下重复执行。

用法：
  python tools/pioarduino_patches.py                 # 应用补丁
  python tools/pioarduino_patches.py --dry-run       # 只检查状态，不修改
  python tools/pioarduino_patches.py --revert        # 还原补丁
  python tools/pioarduino_patches.py --platform-dir DIR   # 指定平台目录

若平台版本不同导致文本不匹配，脚本会明确报错并建议手工处理。
"""

import argparse
import getpass
import os
import string
import sys
from pathlib import Path
from typing import Optional


# 补丁定义：old = 原始文本，new = 补丁后文本（LF 换行）
PATCHES = [
    {
        "file": Path("builder/frameworks/component_manager.py"),
        "old": (
            "            # Clean and normalize entries\n"
            "            cleaned_entries = []\n"
            "            for entry in lib_ignore:\n"
            "                entry = str(entry).strip()\n"
            "                if entry:\n"
            "                    # Convert library names to potential include directory names\n"
            "                    include_name = self._convert_lib_name_to_include(entry)\n"
            "                    # Use optimized set lookup for critical components check (O(1) vs O(n))\n"
            "                    if include_name not in self._critical_components:\n"
            "                        cleaned_entries.append(include_name)\n"
            "\n"
            "            return sorted(set(cleaned_entries))\n"
        ),
        "new": (
            "            # Clean and normalize entries\n"
            "            cleaned_entries = []\n"
            "            bt_ble_protected = self._has_bt_ble_dependencies()\n"
            "            for entry in lib_ignore:\n"
            "                entry = str(entry).strip()\n"
            "                if entry:\n"
            "                    # Convert library names to potential include directory names\n"
            "                    include_name = self._convert_lib_name_to_include(entry)\n"
            "                    # Use optimized set lookup for critical components check (O(1) vs O(n))\n"
            "                    if include_name not in self._critical_components:\n"
            "                        # 项目依赖 NimBLE 等 BT/BLE 库时，不剪掉 bt 组件：\n"
            "                        # 否则自定义 sdkconfig 重建 Arduino 库时，自带 BLE 库\n"
            "                        # 缺少 Bluedroid 头文件（esp_gatt_defs.h）编译失败\n"
            "                        if bt_ble_protected and self._is_bt_related_library(include_name):\n"
            "                            continue\n"
            "                        cleaned_entries.append(include_name)\n"
            "\n"
            "            return sorted(set(cleaned_entries))\n"
        ),
    },
    {
        "file": Path("builder/frameworks/espidf.py"),
        "old": (
            "        lib_ignore_entries = (\n"
            "            get_entries() if callable(get_entries) else lib_handler._get_lib_ignore_entries()\n"
            "        )\n"
        ),
        "new": (
            "        lib_ignore_entries = (\n"
            "            get_entries() if callable(get_entries) else lib_handler._get_lib_ignore_entries()\n"
            "        )\n"
            "\n"
            "        # BT/BLE 保护：库构建子环境里 GetProjectOption 读不到 lib_deps，\n"
            "        # 这里直接从项目 platformio.ini 读取，判断项目是否依赖 NimBLE 等 BT 库。\n"
            "        # 若是，则不剪掉 bt 组件，否则自定义 sdkconfig 重建 Arduino 库时\n"
            "        # 自带 BLE 库缺少 Bluedroid 头文件（esp_gatt_defs.h）编译失败。\n"
            "        try:\n"
            "            import configparser\n"
            "            bt_keywords = (\"BLE\", \"BT\", \"NIMBLE\", \"BLUETOOTH\")\n"
            "            bt_protected = False\n"
            "            parser = configparser.ConfigParser()\n"
            "            ini_path = Path(PROJECT_DIR) / \"platformio.ini\"\n"
            "            if ini_path.exists():\n"
            "                # 显式 UTF-8：中文 Windows 默认 GBK 会解析失败\n"
            "                with open(ini_path, encoding=\"utf-8\") as fp:\n"
            "                    parser.read_file(fp)\n"
            "                for section in parser.sections():\n"
            "                    if parser.has_option(section, \"lib_deps\"):\n"
            "                        deps = parser.get(section, \"lib_deps\", raw=True).upper()\n"
            "                        if any(k in deps for k in bt_keywords):\n"
            "                            bt_protected = True\n"
            "                            break\n"
            "            if bt_protected:\n"
            "                lib_ignore_entries = [\n"
            "                    e for e in lib_ignore_entries\n"
            "                    if not lib_handler._is_bt_related_library(e)\n"
            "                ]\n"
            "        except Exception:\n"
            "            pass\n"
        ),
    },
]


def find_platform_dir(override: Optional[str]) -> Path:
    """定位 pioarduino 平台目录（含 builder/frameworks 子目录）。"""
    if override:
        d = Path(override)
        if not (d / "builder" / "frameworks").is_dir():
            sys.exit(f"错误：指定的平台目录不存在或结构不符: {d}")
        return d

    candidates = []
    if os.environ.get("PLATFORMIO_CORE_DIR"):
        candidates.append(Path(os.environ["PLATFORMIO_CORE_DIR"]) / "platforms" / "espressif32")
    candidates.append(Path.home() / ".platformio" / "platforms" / "espressif32")
    candidates.append(Path(f"C:/Users/{getpass.getuser()}") / ".platformio" / "platforms" / "espressif32")
    # 常见盘符上的 .platformio（PlatformIO 可装在任意盘）
    for letter in string.ascii_uppercase:
        candidates.append(Path(f"{letter}:/.platformio/platforms/espressif32"))

    for c in candidates:
        try:
            if (c / "builder" / "frameworks").is_dir():
                return c
        except OSError:
            continue

    sys.exit(
        "错误：未找到 pioarduino 平台目录（espressif32）。\n"
        "请用 --platform-dir 显式指定，例如：\n"
        "  python tools/pioarduino_patches.py --platform-dir D:/.platformio/platforms/espressif32"
    )


def process_file(target: Path, patch: dict, dry_run: bool, revert: bool) -> bool:
    """对单个文件应用/还原/检查补丁，返回是否成功。"""
    if not target.exists():
        print(f"  [跳过] 文件不存在: {target}")
        return False

    # newline="" 保留原始换行风格，避免平台文件为 CRLF 时被整体改写
    with open(target, "r", encoding="utf-8", newline="") as fp:
        content = fp.read()
    old, new = patch["old"], patch["new"]

    if new in content:
        # 已打补丁
        if not revert:
            print(f"  [跳过] {target.name} 已打补丁")
            return True
        if dry_run:
            print(f"  [将还原] {target.name}（当前已打补丁）")
            return True
        content = content.replace(new, old, 1)
        with open(target, "w", encoding="utf-8", newline="") as fp:
            fp.write(content)
        print(f"  [已还原] {target.name}")
        return True

    if old in content:
        # 未打补丁
        if revert:
            print(f"  [跳过] {target.name} 未打补丁，无需还原")
            return True
        if dry_run:
            print(f"  [将应用] {target.name}")
            return True
        content = content.replace(old, new, 1)
        with open(target, "w", encoding="utf-8", newline="") as fp:
            fp.write(content)
        print(f"  [已应用] {target.name}")
        return True

    print(
        f"  [失败] {target.name}：未找到可匹配的原始代码段。\n"
        f"         平台版本可能与本补丁不兼容，请对照 tools/pioarduino_patches.py 手工处理。"
    )
    return False


def main() -> None:
    parser = argparse.ArgumentParser(description="pioarduino 平台补丁（NimBLE + custom_sdkconfig）")
    parser.add_argument("--platform-dir", help="pioarduino 平台目录（含 builder/frameworks）")
    parser.add_argument("--dry-run", action="store_true", help="只检查状态，不修改文件")
    parser.add_argument("--revert", action="store_true", help="还原补丁")
    args = parser.parse_args()

    platform_dir = find_platform_dir(args.platform_dir)
    print(f"平台目录: {platform_dir}")
    print(f"模式: {'还原' if args.revert else ('检查（dry-run）' if args.dry_run else '应用')}")

    ok = True
    for patch in PATCHES:
        ok = process_file(platform_dir / patch["file"], patch, args.dry_run, args.revert) and ok

    if not ok:
        sys.exit(1)
    print("完成。")


if __name__ == "__main__":
    main()
