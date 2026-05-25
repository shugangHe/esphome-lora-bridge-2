import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.core import CORE, ID
from esphome.const import CONF_ID, CONF_NAME, ENTITY_CATEGORY_DIAGNOSTIC
from . import lora_bridge_ns, LoRaBridge, CONF_MAX_NODES

CONFIG_SCHEMA = cv.Schema({
    cv.Required("bridge_id"): cv.use_id(LoRaBridge),
})

async def to_code(config):
    parent = await cg.get_variable(config["bridge_id"])
    full_config = CORE.config
    max_nodes = full_config["lora_bridge"][CONF_MAX_NODES]

    for i in range(max_nodes):
        # HA 显示的名称依然用 i+1，让用户看到 01-32
        friendly_index = i + 1
        bs_id_str = f"lora_dev_{friendly_index:02d}_delay_flag"
        bs_id = ID(bs_id_str, is_declaration=True, type=binary_sensor.BinarySensor)
        bs_conf = binary_sensor.binary_sensor_schema()({
            CONF_ID: bs_id,
            CONF_NAME: f"LoRa Dev {friendly_index:02d} Busy Status",
            "entity_category": ENTITY_CATEGORY_DIAGNOSTIC,
            "device_class": "running",
        })
        var = cg.new_Pvariable(bs_id)
        await binary_sensor.register_binary_sensor(var, bs_conf)
        # 这里传给 C++ 的 index 变为 0-31
        cg.add(parent.register_delay_flag(i, var))
        