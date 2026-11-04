# Real knobs: after the firmware is built, also makes knob-os.bin in the build folder: the bootloader, the partition
# table, boot_app0 and the firmware in one image that starts at 0x0, made with the same esptool settings
# `pio run -t upload` uses. Gaps between the parts are 0xFF. It's what a release ships and what Knob Hub installs on a
# knob (skipping the nvs partition, where the knob keeps its pairing key and Wi-Fi network).
#
# The image is also copied to firmware/knob-os-<board id>.bin in the workspace folder (%USERPROFILE%\.cache\knob-os),
# with firmware/firmware.json saying each board's file and Knob OS version: Knob Hub's build takes them from there.
import json
import os
import re
import shutil

Import("env")  # noqa: F821 (provided by PlatformIO)


def board_id(env):
    """The board's id (KNOB_BOARD_ID in its file in src/hal/esp32/boards/)."""
    flags = " ".join(env.GetProjectOption("build_flags"))
    header = re.search(r'KNOB_BOARD=\\?"([^"\\]+)', flags).group(1)
    with open(os.path.join(env.subst("$PROJECT_SRC_DIR"), "hal", "esp32", header), encoding="utf-8") as f:
        return re.search(r'#define KNOB_BOARD_ID "([^"]+)"', f.read()).group(1)


def os_version(env):
    with open(os.path.join(env.subst("$PROJECT_SRC_DIR"), "core", "knob_os.h"), encoding="utf-8") as f:
        return re.search(r'#define KNOB_OS_VERSION "([^"]+)"', f.read()).group(1)


def merge(source, target, env):
    board = env.BoardConfig()
    parts = [(offset, env.subst(image)) for offset, image in env.get("FLASH_EXTRA_IMAGES", [])]
    parts.append((env.subst("$ESP32_APP_OFFSET"), str(target[0])))
    out = env.subst("$BUILD_DIR/knob-os.bin")
    command = [
        '"%s"' % env.subst("$OBJCOPY"), "--chip", board.get("build.mcu"), "merge-bin", "-o", '"%s"' % out,
        "--flash-mode", env.subst("${__get_board_flash_mode(__env__)}"),
        "--flash-freq", env.subst("${__get_board_f_image(__env__)}"),
        "--flash-size", board.get("upload.flash_size", "4MB"),
    ]
    for offset, image in sorted(parts, key=lambda p: int(p[0], 16)):
        command += [offset, '"%s"' % image]
    result = env.Execute(" ".join(command))
    if result:
        return result

    # The build folder is <workspace>/build/<env>.
    folder = os.path.join(os.path.dirname(os.path.dirname(env.subst("$BUILD_DIR"))), "firmware")
    os.makedirs(folder, exist_ok=True)
    board_name = board_id(env)
    name = "knob-os-%s.bin" % board_name
    shutil.copyfile(out, os.path.join(folder, name))
    index = os.path.join(folder, "firmware.json")
    boards = {}
    if os.path.isfile(index):
        with open(index, encoding="utf-8") as f:
            boards = json.load(f)
    boards[board_name] = {"file": name, "version": os_version(env)}
    with open(index, "w", encoding="utf-8") as f:
        json.dump(boards, f, indent=2)
    print("Knob OS %s for %s: %s" % (boards[board_name]["version"], board_name, os.path.join(folder, name)))
    return 0


env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", merge)  # noqa: F821
