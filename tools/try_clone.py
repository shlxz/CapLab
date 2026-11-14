"""把手上的卡贴上去，自动扫到就克隆。

用一次串口连接连续发命令。以前打开串口会拉 DTR 复位板子，
所以扫描和克隆必须在同一个连接里做完；现在 dev.open_port()
在 open() 之前就把 DTR/RTS 置 False，不会复位，但保持在
一个连接里仍然更省事。

用法：
    python tools/try_clone.py [等待秒数]
"""
import sys
import time

import dev

WAIT = float(sys.argv[1]) if len(sys.argv) > 1 else 40.0


def main():
    s = dev.open_port(0.4)

    def send(cmd, wait=0.5, size=6000):
        s.reset_input_buffer()
        s.write((cmd + "\n").encode())
        time.sleep(wait)
        return s.read(size).decode("utf-8", "replace")

    # 克隆前先停掉模拟，否则芯片还在被动目标模式，收不到读卡器指令
    send("emu off", 0.6)

    deadline = time.time() + WAIT
    hit = None
    while time.time() < deadline:
        out = send("nfc", 0.45)
        line = [l for l in out.splitlines() if l.startswith(("OK nfc", "ERR nfc"))]
        if line and "present=1" in line[0]:
            hit = line[0]
            break
        print(".", end="", flush=True)
    print()

    if not hit:
        print("no card within %.0fs" % WAIT)
        s.close()
        return 1

    print("card:", hit)
    out = send("clone", 2.5)
    for l in out.splitlines():
        if l.startswith(("OK clone", "ERR clone")):
            print("clone:", l)

    out = send("clone stat", 1.0)
    for l in out.splitlines():
        if l.startswith("CLONE") or l.startswith("  00") or "..." in l:
            print(l)
    s.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
