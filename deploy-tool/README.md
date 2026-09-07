# 部署工具 · AI 状态看板

一个脚本搞定固件烧录、环境安装、MQTT 发消息、串口日志、网络配置。

## 快速开始

```bash
cd <项目根目录>   # 即包含 platformio.ini 的目录

python deploy_tool.py install   # 安装依赖 + 检查环境
python deploy_tool.py flash     # 编译并上传固件到 ESP32
python deploy_tool.py monitor   # 打开串口监视器
python deploy_tool.py send --state running --message "测试"
```

或者一键完成所有：

```bash
python deploy_tool.py all -p COM5    # Windows
python deploy_tool.py all -p /dev/ttyUSB0  # Linux
```

---

## 子命令一览

| 命令 | 说明 | 示例 |
|------|------|------|
| `install` | 安装 Python 依赖（paho-mqtt），检查 pio-cli | `python deploy_tool.py install` |
| `flash` | 编译固件 + 上传 ESP32 + 自动上传 SPIFFS | `python deploy_tool.py flash -p COM5` |
| `flash-spi` | 仅上传 SPIFFS 数据分区 | `python deploy_tool.py flash-spi` |
| `monitor` | 打开串口监视器查看实时日志 | `python deploy_tool.py monitor -p COM5` |
| `send` | 通过 MQTT 发送状态到看板 | `python deploy_tool.py send --state running -m "keepalive"` |
| `listen` | 监听 MQTT topic 并格式化输出 | `python deploy_tool.py listen` |
| `config` | 交互式编辑 WiFi/MQTT 配置 | `python deploy_tool.py config` |
| `all` | 一键全流程：install → flash → monitor | `python deploy_tool.py all -p COM5` |

### send 子命令详细用法

```bash
# 发送单条状态
python deploy_tool.py send --state idle
python deploy_tool.py send --state running -m "数据处理中..."
python deploy_tool.py send --state done --message "任务完成" --retain

# 交互模式 — 逐条输入
python deploy_tool.py send
> --state running --message "第一步"
> --state waiting --message "等反馈"
> quit

# 批量模式 — 从 stdin 读取 JSON
echo '{"state":"running","message":"a"}' | python deploy_tool.py send --batch
```

### listen 子命令详细用法

```bash
# 持续监听
python deploy_tool.py listen

# 只收 5 条就退出
python deploy_tool.py listen -c 5
```

---

## 作为 Python 库调用

在 Python 项目中直接导入使用：

```bash
pip install paho-mqtt
```

```python
from deploy_tool.mqtt_sender import publish_state

# 单次推送（自动从 data/config.json 读 broker）
publish_state("running", "数据处理中...")
publish_state("done", "已完成")
publish_state("error", "连接超时")

# 指定自定义 Broker
publish_state("idle", "", broker="192.168.1.100", port=1883)

# 上下文管理器（长连接，适合多次发送）
from deploy_tool.mqtt_sender import StatePublisher

with StatePublisher() as pub:
    pub.send("running", "步骤一")
    pub.send("running", "步骤二")
    pub.send("done", "全部完成")

# 批量发送
from deploy_tool.mqtt_sender import batch_send

batch_send([
    {"state": "waiting", "message": "等待确认"},
    {"state": "running", "message": "处理中"},
    {"state": "done", "message": "完成"},
])
```

### CLI 独立运行

```bash
python -m deploy_tool.mqtt_sender --state running -m "来自命令行"
```

---

## 配置管理

```bash
# 交互式编辑 config.json
python deploy_tool.py config

# 完成后自动上传到 SPIFFS
python deploy_tool.py flash-spi
```

---

## 文件结构

```
deploy-tool/
├── deploy_tool.py       # 主 CLI 工具（子命令入口）
├── mqtt_sender.py       # MQTT 客户端库（可导入 / 独立运行）
├── __init__.py          # 包初始化
└── README.md            # 本文件
```

## 系统要求

- **Python** ≥ 3.7（自带 argparse, json, subprocess）
- **PlatformIO Core CLI**（需先安装: https://platformio.org/install/cli）
- **paho-mqtt**（首次 `install` 自动安装）
- **ESP32 驱动**（CH340/CP2102，操作系统需提供）
