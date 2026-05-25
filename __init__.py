import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, api
from esphome.const import CONF_ID, CONF_UART_ID

CONF_MAX_NODES = "max_nodes"

DEPENDENCIES = ["uart", "api"]
AUTO_LOAD = ["sensor", "switch", "binary_sensor", "text_sensor"]


api_ns = cg.esphome_ns.namespace("api")
CustomAPIDevice = api_ns.class_("CustomAPIDevice")

lora_bridge_ns = cg.esphome_ns.namespace("lora_bridge")
#LoRaBridge = lora_bridge_ns.class_("LoRaBridge", cg.Component, uart.UARTDevice)
# 修改类定义，增加 api.CustomAPIDevice 继承
LoRaBridge = lora_bridge_ns.class_(
    "LoRaBridge", cg.Component, uart.UARTDevice, CustomAPIDevice
)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(LoRaBridge),
    cv.GenerateID(CONF_UART_ID): cv.use_id(uart.UARTComponent),
    # 允许在 YAML 中设置，默认为 32个节点
    cv.Optional(CONF_MAX_NODES, default=32): cv.int_range(min=1, max=128),
}).extend(cv.COMPONENT_SCHEMA).extend(uart.UART_DEVICE_SCHEMA)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    
    # 将此数值存入 context，以便 switch.py 等脚本读取
    cg.add_define("LORA_BRIDGE_MAX_NODES", config[CONF_MAX_NODES])

    # 注册服务的正确 Python 写法：
    # 第一个参数是 C++ 类中的成员函数指针
    # 第二个参数是服务在 HA 中显示的名称
    # 第三个参数是参数名称列表
    #cg.add(var.register_service(var.on_update_device, "update_device", ["index", "device_id"]))
    # 修正点：使用 cg.RawExpression 强制生成 C++ 成员函数指针语法
    # 这样生成的代码会是：&esphome::lora_bridge::LoRaBridge::on_update_device
    #method_ptr = cg.RawExpression(f"&{LoRaBridge}::on_update_device")
    #cg.add(var.register_service(method_ptr, "update_device", ["index", "device_id", "type", "slot"]))
    # 注册统一配置服务
    # 参数名称为 "config_str"
    #cg.add_define("USE_LORA_BRIDGE_SERVICE")
    #cg.add(var.register_service(&LoRaBridge::on_set_config, "set_config",  {"config_str": cg.std_string}))

    
