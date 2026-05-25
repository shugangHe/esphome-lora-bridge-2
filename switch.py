import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import switch
from esphome.const import CONF_ID, CONF_NAME
from esphome.core import CORE, ID
from . import lora_bridge_ns, LoRaBridge, CONF_MAX_NODES

LoRaSwitch = lora_bridge_ns.class_("LoRaSwitch", switch.Switch)

CONFIG_SCHEMA = cv.Schema({
    cv.Required("bridge_id"): cv.use_id(LoRaBridge),
})

async def to_code(config):
    parent = await cg.get_variable(config["bridge_id"])
    full_config = CORE.config
    max_nodes = full_config["lora_bridge"][CONF_MAX_NODES]

    # 创建一个能填充默认值的验证器
    # 注意：这里传入 LoRaSwitch 类来初始化基础架构
    base_schema = switch.switch_schema(LoRaSwitch)

    for i in range(max_nodes):
        # HA 显示的名称依然用 i+1，让用户看到 01-32
        friendly_index = i + 1
        node_id_str = f"lora_dev_{friendly_index:02d}_valve"
        node_id = ID(node_id_str, is_declaration=True, type=LoRaSwitch)
        
        # 修正：直接像函数一样调用 base_schema
        conf = base_schema({
            CONF_ID: node_id,
            CONF_NAME: f"LoRa Dev {friendly_index:02d} Valve",
        })
        
        var = cg.new_Pvariable(node_id)
        await switch.register_switch(var, conf)
        
        cg.add(var.set_bridge(parent))
        # 这里传给 C++ 的 index 变为 0-31
        cg.add(var.set_index(i))
        cg.add(parent.register_valve(i, var))
