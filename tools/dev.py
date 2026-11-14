#!/usr/bin/env python3
"""Cardputer-Adv 串口调试工具

用法:
  python dev.py boot               # 打开串口，收集启动日志
  python dev.py cmd "stat"         # 发一条命令并打印响应
  python dev.py snap out.png       # 抓屏幕，存成 2x 放大的 PNG
  python dev.py seq "demo" "stat"  # 同一条连接连发多条命令（不做复位）
"""
import base64
import os
import struct
import sys
import time
import zlib

import serial

PORT = os.environ.get("CAP_PORT", "COM7")
BAUD = 115200


def open_port(settle=0.2):
    ser = serial.Serial()
    ser.port = PORT
    ser.baudrate = BAUD
    ser.timeout = 1
    # 必须在 open() 之前就把 DTR/RTS 状态置 False。pyserial 的 dtr setter 在
    # 未打开时会抛 PortNotOpenError，只能改内部状态；open() 里的
    # _update_dtr_state() 读的正是这个字段。否则窗口一开 ESP32 的自动复位
    # 电路立刻重启芯片，clone/emu 现场全丢。
    try:
        ser._dtr_state = False
        ser._rts_state = False
    except Exception:
        pass
    ser.open()
    try:
        ser.dtr = False
        ser.rts = False
    except Exception:
        pass
    time.sleep(settle)
    return ser


def send(ser, cmd, wait=0.45):
    ser.reset_input_buffer()
    ser.write((cmd + "\n").encode())
    time.sleep(wait)
    return ser.read_all().decode("utf-8", "replace")


def read_line(ser, timeout=8.0, size=200000):
    ser.timeout = timeout
    data = ser.read_until(b"\n", size)
    return data.rstrip(b"\r\n")


def snap(ser, out_path):
    ser.reset_input_buffer()
    ser.write(b"snap\n")

    # 跳过无关日志，直到 SNAP 头
    w = h = runs = 0
    deadline = time.time() + 12
    while time.time() < deadline:
        line = read_line(ser).decode("utf-8", "replace")
        if line.startswith("SNAP "):
            parts = line.split()
            w, h, runs = int(parts[1]), int(parts[2]), int(parts[4])
            break
    if not w:
        raise RuntimeError("no SNAP header")

    b64 = read_line(ser, timeout=30).decode("ascii", "replace").strip()
    raw = base64.b64decode(b64 + "=" * (-len(b64) % 4))
    if len(raw) < runs * 3:
        raise RuntimeError(f"short payload {len(raw)} < {runs*3}")

    # 展开 RLE -> 像素数组（bytes, 小端 uint16）
    pixels = bytearray()
    for i in range(runs):
        cnt = raw[i * 3]
        pixels += raw[i * 3 + 1:i * 3 + 3] * cnt
    if len(pixels) != w * h * 2:
        raise RuntimeError(f"bad pixel count {len(pixels)//2} != {w*h}")

    # 画布是字节交换的 RGB565 -> 按大端解释才是正确颜色
    rgb = bytearray()
    for i in range(0, len(pixels), 2):
        v = struct.unpack(">H", pixels[i:i + 2])[0]
        r = (v >> 11) & 0x1F
        g = (v >> 5) & 0x3F
        b = v & 0x1F
        rgb += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    write_png_2x(out_path, w, h, rgb)
    print(f"wrote {out_path}")


def write_png_2x(path, w, h, rgb):
    W, H = w * 2, h * 2
    rows = []
    for y in range(h):
        row = rgb[y * w * 3:(y + 1) * w * 3]
        doubled = bytearray()
        for x in range(w):
            px = row[x * 3:x * 3 + 3]
            doubled += px + px
        rows.append(bytes(doubled))
        rows.append(bytes(doubled))
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", W, H, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def tour(ser, outdir="shot"):
    os.makedirs(outdir, exist_ok=True)
    print(send(ser, "selftest", 0.4))
    # 开机会自动从 NVS 恢复上次采集，先清掉，采集页截图才是真正的空状态
    print(send(ser, "clear", 0.4))
    for name in ("menu", "capture", "scan", "nfc", "info"):
        print(send(ser, f"ui {name}", 1.2))
        snap(ser, os.path.join(outdir, f"{name}.png"))
    # 模拟页分两个状态：关着（TAG OFF）和跑着（TAG ON），后者才是有意义的展示
    print(send(ser, "ui emu", 0.6))
    snap(ser, os.path.join(outdir, "emu_off.png"))
    print(send(ser, "emu", 1.0))
    snap(ser, os.path.join(outdir, "emu_on.png"))
    print(send(ser, "emu off", 0.5))
    # 有数据时的采集页
    print(send(ser, "ui capture", 0.5))
    print(send(ser, "demo", 0.6))
    snap(ser, os.path.join(outdir, "capture_data.png"))
    print(send(ser, "clear", 0.3))
    print(send(ser, "ui menu", 0.4))


def seq(ser, cmds, wait=0.9):
    """在同一条串口连接上连发多条命令，避免每次开端口把板子复位。"""
    for c in cmds:
        ser.reset_input_buffer()
        ser.write((c + "\n").encode())
        time.sleep(wait)
        out = ser.read_all().decode("utf-8", "replace").strip()
        print(f"$ {c}")
        if out:
            print(out)
        print()


def shot(ser, screen, out_path, pre=None, settle=1.6):
    """切到指定屏 -> 等状态稳定 -> 抓图。必须在同一条连接里完成，否则开端口会复位。"""
    for c in (pre or []):
        send(ser, c, 0.8)
    send(ser, f"ui {screen}", settle)
    snap(ser, out_path)


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return
    op = sys.argv[1]

    if op == "boot":
        ser = open_port(1.2)
        data = ser.read_all().decode("utf-8", "replace")
        print(data)
        time.sleep(0.5)
        print(send(ser, "stat", 0.6))
    elif op == "cmd":
        ser = open_port(0.3)
        print(send(ser, sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 0.5))
    elif op == "snap":
        ser = open_port(0.3)
        snap(ser, sys.argv[2] if len(sys.argv) > 2 else "snap.png")
    elif op == "tour":
        ser = open_port(0.5)
        tour(ser)
    elif op == "shot":
        # python dev.py shot <screen> <out.png> [pre-cmd ...]
        ser = open_port(0.6)
        shot(ser, sys.argv[2], sys.argv[3], sys.argv[4:])
    elif op == "seq":
        wait = 0.9
        cmds = sys.argv[2:]
        if cmds and cmds[0].startswith("w="):
            wait = float(cmds[0][2:])
            cmds = cmds[1:]
        ser = open_port(0.6)
        seq(ser, cmds, wait)
    else:
        print(__doc__)


if __name__ == "__main__":
    main()
