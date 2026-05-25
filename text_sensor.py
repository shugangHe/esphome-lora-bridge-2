import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import text_sensor
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
        ts_id_str = f"lora_dev_{friendly_index:02d}_last_time"
        ts_id = ID(ts_id_str, is_declaration=True, type=text_sensor.TextSensor)
        ts_conf = text_sensor.text_sensor_schema()({
            CONF_ID: ts_id,
            CONF_NAME: f"LoRa Dev {friendly_index:02d} Last Seen",
            "entity_category": ENTITY_CATEGORY_DIAGNOSTIC,
        })
        var = cg.new_Pvariable(ts_id)
        await text_sensor.register_text_sensor(var, ts_conf)
        # 这里传给 C++ 的 index 变为 0-31
        cg.add(parent.register_last_time(i, var))
        