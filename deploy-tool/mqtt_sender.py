"""
AI 状态看板 · MQTT 客户端库
==================================
供 Pi 端（电脑）Python 程序调用的 MQTT 客户端，将工作流状态推送到 ESP32 看板。

安装依赖后直接使用：
    pip install paho-mqtt
    from deploy_tool.mqtt_sender import publish_state

示例：
    # 单次推送
    from mqtt_sender import publish_state
    publish_state("running", "数据处理中...")
    publish_state("done", "已完成")
    publish_state("error", "连接超时")

    # 自定义 Broker
    publish_state("running", "keepalive", broker="192.168.1.100")

    # 作为上下文管理器自动管理连接
    with StatePublisher() as pub:
        pub.send("running", "开始处理...")
        do_work()
        pub.send("done", "全部完成")

    # 批量发送
    from mqtt_sender import batch_send
    messages = [
        {"state": "running", "message": "第一步"},
        {"state": "running", "message": "第二步"},
        {"state": "done", "message": "完成"},
    ]
    batch_send(messages)
"""

import json
import os
from contextlib import contextmanager


# ── 默认配置 ──────────────────────────────────────────────────────
DEFAULT_BROKER = "broker.emqx.io"
DEFAULT_PORT = 1883
DEFAULT_TOPIC = "ai/status"
DATA_CONFIG_PATH = os.path.join(
    os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
    "data", "config.json"
) if "__main__" in globals() else DATA_CONFIG_PATH


def _load_config():
    """尝试从 data/config.json 读取 broker 信息。"""
    cfg = {}
    try:
        with open(DATA_CONFIG_PATH, encoding="utf-8") as f:
            data = json.load(f)
        m = data.get("mqtt", {})
        if m.get("host"):
            cfg["broker"] = m["host"]
            cfg["port"] = m.get("port", DEFAULT_PORT)
    except Exception:
        pass
    return cfg


CONFIG_CACHE = None


def get_default_broker():
    """获取默认 broker（优先从 config.json）。"""
    global CONFIG_CACHE
    if CONFIG_CACHE is None:
        CONFIG_CACHE = _load_config()
    return (
        CONFIG_CACHE.get("broker", DEFAULT_BROKER),
        CONFIG_CACHE.get("port", DEFAULT_PORT)
    )


# ── 核心 API ──────────────────────────────────────────────────────

def publish_state(
    state: str,
    message: str = "",
    broker: str | None = None,
    port: int | None = None,
    topic: str = DEFAULT_TOPIC,
    qos: int = 1,
    retain: bool = False,
):
    """
    向 MQTT Broker 发布一条状态消息到 ESP32 看板。

    Args:
        state:     状态名（running/waiting/idle/done/error/critical/cmd）
        message:   描述文字（可选）
        broker:    Broker 地址，默认读 config.json 或 broker.emqx.io
        port:      Broker 端口，默认 1883
        topic:     MQTT topic，默认 ai/status
        qos:       QoS 等级（0/1/2），默认 1
        retain:    是否保留最后一条消息
    """
    import paho.mqtt.client as mqtt_client

    default_broker, default_port = get_default_broker()
    if broker is None:
        broker = default_broker
    if port is None:
        port = default_port

    payload = json.dumps({"state": state, "message": message}, ensure_ascii=False)
    client = mqtt_client.Client()
    try:
        client.connect(broker, port, timeout=10)
        ok = client.publish(topic, payload, qos=qos, retain=retain)
        if ok.mid > 0:
            print(f"[mqtt] ✅ [{topic}] {payload}")
        else:
            print(f"[mqtt] ⚠ 发送失败 mid={ok.mid}", file=__import__("sys").stderr)
    finally:
        client.disconnect()


def batch_send(
    messages: list[dict],
    broker: str | None = None,
    port: int | None = None,
    topic: str = DEFAULT_TOPIC,
):
    """
    批量发送多条状态消息。使用长连接提高效率。

    Args:
        messages: 字典列表，每个包含 "state" 和可选 "message"
    """
    import paho.mqtt.client as mqtt_client

    default_broker, default_port = get_default_broker()
    if broker is None:
        broker = default_broker
    if port is None:
        port = default_port

    client = mqtt_client.Client()
    client.connect(broker, port, timeout=10)
    try:
        for msg in messages:
            payload = json.dumps(msg, ensure_ascii=False)
            client.publish(topic, payload, qos=1)
            print(f"[mqtt] ✅ [{topic}] {payload}")
    finally:
        client.disconnect()


@contextmanager
def StatePublisher(
    broker: str | None = None,
    port: int | None = None,
    topic: str = DEFAULT_TOPIC,
):
    """
    上下文管理器 — 保持一个 MQTT 连接，支持多次 send()。

    Example:
        with StatePublisher() as pub:
            pub.send("running", "步骤一")
            time.sleep(1)
            pub.send("done", "完成")
    """
    import paho.mqtt.client as mqtt_client

    default_broker, default_port = get_default_broker()
    if broker is None:
        broker = default_broker
    if port is None:
        port = default_port

    client = mqtt_client.Client()
    client.connect(broker, port, timeout=10)
    yield type("Publisher", (), {
        "send": lambda s, m="": client.publish(topic, json.dumps(
            {"state": s, "message": m}), qos=1),
        "_client": client,
    })()
    client.disconnect()


# ── CLI 入口（可直接运行此模块） ───────────────────────────────────

def main():
    """独立运行时：从命令行参数发送状态。"""
    import argparse

    parser = argparse.ArgumentParser(description="AI 状态看板 — MQTT 发送器")
    parser.add_argument("--state", "-s", required=True, help="状态名")
    parser.add_argument("--message", "-m", default="", help="描述文字")
    parser.add_argument("--broker", default=None, help="Broker 地址")
    parser.add_argument("--port", type=int, default=None, help="Broker 端口")
    parser.add_argument("--topic", "-t", default=DEFAULT_TOPIC, help="MQTT topic")
    args = parser.parse_args()

    publish_state(args.state, args.message, args.broker, args.port, args.topic)


if __name__ == "__main__":
    main()
