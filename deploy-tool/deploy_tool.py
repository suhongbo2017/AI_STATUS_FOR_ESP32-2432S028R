#!/usr/bin/env python3
"""
AI 状态看板 · 统一部署工具
==================================
一个脚本搞定：固件烧录、环境安装、MQTT 发消息、串口日志、网络配置。

用法：
    cd <项目目录>
    python deploy_tool.py <子命令> [选项]

子命令：
    install      安装 PC 端依赖（paho-mqtt）并检查 pio-cli
    flash        编译固件 + 上传 ESP32
    flash-spi    仅上传 SPIFFS 数据分区（config.json）
    monitor      打开串口监视器查看实时日志
    send         通过 MQTT 发送状态到看板（CLI）
    listen       监听 MQTT topic 并解析输出
    config       交互式编辑 WiFi/MQTT 配置
    all          一键完成：install → flash → flash-spi → monitor
"""

import argparse
import json
import os
import platform
import subprocess
import sys
import webbrowser

# ── 默认常量 ──────────────────────────────────────────────────────
PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))
DATA_CONFIG = os.path.join(PROJECT_ROOT, "data", "config.json")
DEFAULT_BROKER = "broker.emqx.io"
DEFAULT_PORT = 1883
SYSTEM = platform.system().lower()

# ── 辅助函数 ──────────────────────────────────────────────────────

def run(cmd, cwd=None, check=True):
    """运行命令并打印过程日志。"""
    print(f"[deploy] $ {' '.join(cmd)}")
    return subprocess.run(cmd, cwd=cwd or PROJECT_ROOT, capture_output=False, check=check)


def ensure_pio_cli():
    """验证 PlatformIO Core CLI 是否可用。"""
    try:
        result = subprocess.run(
            ["pio", "--version"],
            capture_output=True, text=True, timeout=15
        )
        if result.returncode == 0:
            version = result.stdout.strip().split("\n")[0]
            print(f"[deploy] PlatformIO Core {version}")
            return True
    except FileNotFoundError:
        pass
    except Exception:
        pass
    return False


# ── 子命令实现 ────────────────────────────────────────────────────

def cmd_install(args):
    """安装 / 验证 PC 端运行环境。"""
    print("[deploy] === 环境安装 ===\n")

    # 1) pip install paho-mqtt
    print("[1/3] 安装 paho-mqtt ...")
    try:
        import paho.mqtt.client  # noqa: F401
        print("      ✓ paho-mqtt 已安装")
    except ImportError:
        try:
            run([sys.executable or "python", "-m", "pip", "install", "paho-mqtt"])
            print("      ✓ paho-mqtt 已安装")
        except Exception as e:
            print(f"      ✗ 安装失败: {e}", file=sys.stderr)
            sys.exit(1)

    # 2) 检查 pio-cli
    print("\n[2/3] 检查 PlatformIO Core ...")
    if ensure_pio_cli():
        print("      ✓ pio-cli 可用")
    else:
        print("      ⚠ pio-cli 未找到")
        print("      请访问 https://platformio.org/install/cli 安装")
        if SYSTEM in ("windows", "darwin"):
            print("      Windows 推荐使用: scoop install platformio")
            print("      macOS 推荐使用:   brew install platformio")

    # 3) 检查 Python
    print("\n[3/3] Python 版本 ...")
    print(f"      ✓ {platform.python_version()} ({sys.executable})")

    print("\n[deploy] 环境就绪 ✅")


def cmd_flash(args):
    """编译并上传固件到 ESP32。"""
    port = getattr(args, 'port', None) or os.environ.get("ESP32_PORT", "")
    env = args.env if hasattr(args, 'env') and args.env else "esp32-cyd"

    print(f"[deploy] === 固件烧录 (env={env}) ===\n")

    # 编译
    print("[1/3] 编译固件 ...")
    run(["pio", "run", "-e", env])

    # 上传
    print("\n[2/3] 上传到 ESP32 ...")
    cmd = ["pio", "run", "-e", env, "-t", "upload"]
    if port:
        cmd.extend(["--upload-port", port])
    run(cmd)

    # 上传 SPIFFS（可选，默认一起做）
    do_spi = getattr(args, 'spi', False) or True
    if do_spi:
        print("\n[3/3] 上传 SPIFFS 数据分区 ...")
        cmd = ["pio", "run", "-e", env, "-t", "uploadfs"]
        if port:
            cmd.extend(["--upload-port", port])
        run(cmd)

    print("\n[deploy] 烧录完成 ✅")
    if not args.no_monitor:
        print("[deploy] 建议接下来运行:  python deploy_tool.py monitor")


def cmd_flash_spi(args):
    """仅上传 SPIFFS 分区。"""
    print("[deploy] === 上传 SPIFFS 数据分区 ===\n")
    cmd = ["pio", "run", "-e", "esp32-cyd", "-t", "uploadfs"]
    port = getattr(args, 'port', None) or ""
    if port:
        cmd.extend(["--upload-port", port])
    run(cmd)
    print("\n[deploy] SPIFFS 上传完成 ✅")


def cmd_monitor(args):
    """打开串口监视器。"""
    port = getattr(args, 'port', None) or os.environ.get("ESP32_PORT", "")
    if not port:
        print("[error] 请指定串口号", file=sys.stderr)
        print("  python deploy_tool.py monitor -p COM5    (Windows)")
        print("  python deploy_tool.py monitor -p /dev/ttyUSB0  (Linux)")
        print("  python deploy_tool.py monitor -p /dev/cu.usbserial*  (macOS)")
        sys.exit(1)

    baud = getattr(args, 'baud', 115200)
    print(f"[deploy] 打开串口监视器 {port} @ {baud} bps (Ctrl+C 退出)\n")
    run(["pio", "device", "monitor", "-p", port, "-b", str(baud)])


def cmd_send(args):
    """通过 MQTT 发送状态到看板。"""
    try:
        import paho.mqtt.client as mqtt_client
    except ImportError:
        print("[error] paho-mqtt 未安装，请运行:  python deploy_tool.py install", file=sys.stderr)
        sys.exit(1)

    # 加载配置文件获取 broker 信息
    broker = args.broker or DEFAULT_BROKER
    port = args.port or DEFAULT_PORT

    if os.path.exists(DATA_CONFIG):
        try:
            with open(DATA_CONFIG, encoding="utf-8") as f:
                cfg = json.load(f)
            if not args.broker and cfg.get("mqtt", {}).get("host"):
                broker = cfg["mqtt"]["host"]
                port = cfg["mqtt"].get("port", port)
        except Exception:
            pass

    qos = args.qos
    retain = args.retain

    # 单条发送模式
    if args.state:
        message = args.message or ""
        payload = json.dumps({"state": args.state, "message": message}, ensure_ascii=False)
        _publish_single(broker, port, args.topic, payload, qos, retain)
        return

    # 批量模式：从 stdin 读取 JSON 行
    if args.batch:
        print(f"[deploy] 批量模式 — 向 {broker}:{port}/{args.topic} 推送 (Ctrl+C 退出)\n")
        _publish_loop(broker, port, args.topic, qos, retain)
        return

    # 交互模式：逐条输入
    print(f"[deploy] 交互模式 — 向 {broker}:{port}/{args.topic} 推送")
    print("  输入格式:  --state <name> [--message <text>]")
    print("  输入 quit 退出\n")
    _publish_interactive(broker, port, args.topic, qos, retain)


def _publish_single(broker, port, topic, payload, qos, retain):
    """单次发布消息。"""
    client = mqtt_client.Client()
    client.connect(broker, port, 60)
    ok = client.publish(topic, payload, qos=qos, retain=retain)
    client.disconnect()
    if ok.mid > 0:
        print(f"[mqtt] 已发送 [{topic}] -> {payload}")
    else:
        print(f"[mqtt] 发送失败 mid={ok.mid}", file=sys.stderr)


def _publish_loop(broker, port, topic, qos, retain):
    """持续从 stdin 读取 JSON 行并发布。"""
    client = mqtt_client.Client()
    client.connect(broker, port, 60)
    try:
        while True:
            line = input().strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
                if "state" not in obj:
                    print("[warn] 缺少 state 字段，跳过", file=sys.stderr)
                    continue
                payload = json.dumps(obj, ensure_ascii=False)
                client.publish(topic, payload, qos=qos, retain=False)
                print(f"[mqtt] 已发送 -> {payload}")
            except json.JSONDecodeError as e:
                print(f"[warn] JSON 解析失败: {e}", file=sys.stderr)
    except KeyboardInterrupt:
        pass
    finally:
        client.disconnect()


def _publish_interactive(broker, port, topic, qos, retain):
    """交互式逐条发送。"""
    client = mqtt_client.Client()
    client.connect(broker, port, 60)
    try:
        while True:
            line = input("> ").strip()
            if not line or line.lower() in ("quit", "exit"):
                break
            try:
                parts = shlex_split(line)  # simple tokenizer
                state = None
                message = ""
                i = 0
                while i < len(parts):
                    if parts[i] == "--state" and i + 1 < len(parts):
                        state = parts[i + 1]
                        i += 2
                    elif parts[i] == "--message" and i + 1 < len(parts):
                        message = parts[i + 1]
                        i += 2
                    else:
                        i += 1
                if not state:
                    print("[warn] 缺少 --state，跳过", file=sys.stderr)
                    continue
                payload = json.dumps({"state": state, "message": message}, ensure_ascii=False)
                client.publish(topic, payload, qos=qos, retain=retain)
                print(f"[mqtt] 已发送 -> {payload}")
            except Exception as e:
                print(f"[warn] 错误: {e}", file=sys.stderr)
    except KeyboardInterrupt:
        pass
    finally:
        client.disconnect()


def shlex_split(s):
    """简易分词器（处理双引号）。"""
    import re
    return re.findall(r'"[^"]*"|\'[^\']*\'|\S+', s)


def cmd_listen(args):
    """订阅并解析 MQTT topic，将收到的消息格式化输出。"""
    try:
        import paho.mqtt.client as mqtt_client
    except ImportError:
        print("[error] paho-mqtt 未安装，请运行:  python deploy_tool.py install", file=sys.stderr)
        sys.exit(1)

    broker = args.broker or DEFAULT_BROKER
    port = args.port or DEFAULT_PORT
    topic = args.topic
    count = getattr(args, 'count', 0)

    received = 0
    colors = {
        "running": "\033[93m",     # yellow
        "waiting": "\033[96m",     # cyan
        "idle": "\033[94m",        # blue
        "done": "\033[92m",        # green
        "error": "\033[91m",       # red
        "critical": "\033[1;91m",  # bold red
    }
    reset = "\033[0m"

    def on_message(client, userdata, msg):
        nonlocal received
        received += 1
        try:
            obj = json.loads(msg.payload.decode())
            state = obj.get("state", "?")
            message = obj.get("message", "")
            color = colors.get(state, "")
            label = f"{state:>12}"
            print(f"{color}[{label}] {message}{reset}")
            if count and received >= count:
                client.stop_loop()
                client.disconnect()
        except Exception as e:
            print(f"[raw] {msg.payload.decode()!r} ({e})")

    client = mqtt_client.Client()
    client.on_message = on_message
    client.connect(broker, port, 60)
    client.subscribe(topic, qos=1)
    print(f"[listen] 正在订阅 {broker}:{port}/{topic} ... (Ctrl+C 退出)")
    client.loop_forever()


def cmd_config(args):
    """交互式编辑 WiFi/MQTT 配置。"""
    os.makedirs(os.path.dirname(DATA_CONFIG), exist_ok=True)

    # 读取现有配置
    if os.path.exists(DATA_CONFIG):
        with open(DATA_CONFIG, encoding="utf-8") as f:
            cfg = json.load(f)
    else:
        cfg = {"wifi": {}, "mqtt": {}}

    wifi = cfg.setdefault("wifi", {})
    mqtt = cfg.setdefault("mqtt", {})

    print("[deploy] === 网络配置向导 ===\n")
    print(f"配置文件: {DATA_CONFIG}\n")

    # WiFi SSID
    ssid = input(f"  WiFi 名称 (SSID) [{wifi.get('ssid', '')}]: ").strip()
    if ssid:
        wifi["ssid"] = ssid

    # WiFi Password
    pw = input(f"  WiFi 密码 [{wifi.get('password', '****')}]: ").strip()
    if pw:
        wifi["password"] = pw

    # MQTT Host
    host = input(f"  MQTT Broker [{mqtt.get('host', DEFAULT_BROKER)}]: ").strip()
    if host:
        mqtt["host"] = host

    # MQTT Port
    port_s = input(f"  MQTT 端口 [{mqtt.get('port', DEFAULT_PORT)}]: ").strip()
    if port_s:
        mqtt["port"] = int(port_s)

    # User / Pass (optional)
    user = input(f"  MQTT 用户名 [{mqtt.get('user', '')}]: ").strip()
    if user:
        mqtt["user"] = user
    pw2 = input(f"  MQTT 密码 [{mqtt.get('pass', '****')}]: ").strip()
    if pw2:
        mqtt["pass"] = pw2

    # 写回
    with open(DATA_CONFIG, "w", encoding="utf-8") as f:
        json.dump(cfg, f, indent=4, ensure_ascii=False)
        f.write("\n")

    print(f"\n[deploy] 配置已保存到 {DATA_CONFIG}")
    print("[deploy] 下次烧录时会生效，也可以现在直接上传 SPIFFS:")
    print(f"         python deploy_tool.py flash-spi")


def cmd_all(args):
    """一键全流程。"""
    print("[deploy] === 一键部署 ===\n")

    port = getattr(args, 'port', None) or os.environ.get("ESP32_PORT", "")
    no_spi = getattr(args, 'no_spi', False)

    # 0) 安装依赖
    cmd_install(args)

    # 1) 烧录固件
    flash_args = argparse.Namespace(
        env="esp32-cyd", port=port, spi=not no_spi, no_monitor=True
    )
    cmd_flash(flash_args)

    # 2) 打开串口
    if not getattr(args, 'no_monitor', False):
        if port:
            print(f"\n[deploy] 打开串口 {port} ...\n")
            cmd_monitor(argparse.Namespace(port=port))
        else:
            print("\n[deploy] 请手动运行:  python deploy_tool.py monitor")

    print("\n[deploy] === 全部完成！===")


# ── CLI 入口 ──────────────────────────────────────────────────────

def build_parser():
    parser = argparse.ArgumentParser(
        prog="deploy_tool",
        description="AI 状态看板 — 统一部署工具",
        epilog="示例:\n  python deploy_tool.py install\n  python deploy_tool.py flash\n  python deploy_tool.py send --state running --message \"keepalive\"\n  python deploy_tool.py config",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    sub = parser.add_subparsers(dest="command", help="子命令")

    # --- install ---
    sub.add_parser("install", help="安装 PC 端依赖并检查环境")

    # --- flash ---
    p_flash = sub.add_parser("flash", help="编译固件 + 上传到 ESP32")
    p_flash.add_argument("-e", "--env", default="esp32-cyd", help="PlatformIO 目标名")
    p_flash.add_argument("-p", "--port", help="串口号 (如 COM5, /dev/ttyUSB0)")
    p_flash.add_argument("--no-spi", action="store_true", help="不自动上传 SPIFFS")
    p_flash.add_argument("--no-monitor", action="store_true", help="完成后不自动打开串口")

    # --- flash-spi ---
    p_spi = sub.add_parser("flash-spi", help="仅上传 SPIFFS 数据分区")
    p_spi.add_argument("-p", "--port", help="串口号")

    # --- monitor ---
    p_mon = sub.add_parser("monitor", help="打开串口监视器")
    p_mon.add_argument("-p", "--port", help="串口号")
    p_mon.add_argument("-b", "--baud", type=int, default=115200, help="波特率")

    # --- send ---
    p_send = sub.add_parser("send", help="通过 MQTT 发送状态到看板")
    p_send.add_argument("--state", help="状态名 (running/waiting/idle/done/error 等)")
    p_send.add_argument("--message", "-m", help="描述文字")
    p_send.add_argument("-t", "--topic", default="ai/status", help="MQTT topic (默认 ai/status)")
    p_send.add_argument("--broker", help="Broker 地址 (默认 broker.emqx.io)")
    p_send.add_argument("--port", type=int, help="Broker 端口 (默认 1883)")
    p_send.add_argument("--qos", type=int, default=1, help="QoS (默认 1)")
    p_send.add_argument("--retain", action="store_true", help="设为 retain 消息")
    p_send.add_argument("--batch", action="store_true", help="批量模式: 从 stdin 读 JSON")

    # --- listen ---
    p_listen = sub.add_parser("listen", help="监听并解析 MQTT 消息")
    p_listen.add_argument("-t", "--topic", default="ai/status", help="监听 topic")
    p_listen.add_argument("--broker", help="Broker 地址")
    p_listen.add_argument("--port", type=int, help="Broker 端口")
    p_listen.add_argument("-c", "--count", type=int, default=0, help="收到 N 条后退出 (0=不限)")

    # --- config ---
    sub.add_parser("config", help="交互式编辑 WiFi/MQTT 配置")

    # --- all ---
    p_all = sub.add_parser("all", help="一键全流程: install → flash → monitor")
    p_all.add_argument("-p", "--port", help="串口号")
    p_all.add_argument("--no-spi", dest="no_spi", action="store_true", help="不自动上传 SPIFFS")
    p_all.add_argument("--no-monitor", dest="no_monitor", action="store_true", help="不自动打开串口")

    return parser


def main():
    parser = build_parser()
    args = parser.parse_args()

    if not args.command:
        parser.print_help()
        sys.exit(1)

    cmd_map = {
        "install": cmd_install,
        "flash": cmd_flash,
        "flash-spi": cmd_flash_spi,
        "monitor": cmd_monitor,
        "send": cmd_send,
        "listen": cmd_listen,
        "config": cmd_config,
        "all": cmd_all,
    }

    handler = cmd_map.get(args.command)
    if handler:
        try:
            handler(args)
        except KeyboardInterrupt:
            print("\n[deploy] 用户中断")
            sys.exit(0)
        except FileNotFoundError as e:
            print(f"[error] 命令不存在: {e}", file=sys.stderr)
            print("  请确认 PlatformIO CLI 已安装:  pip install platformio", file=sys.stderr)
            sys.exit(127)
        except Exception as e:
            print(f"[error] {e}", file=sys.stderr)
            sys.exit(1)


if __name__ == "__main__":
    main()
