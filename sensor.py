import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.core import CORE, ID
from esphome.const import (
    CONF_ID, CONF_NAME, DEVICE_CLASS_BATTERY, 
    DEVICE_CLASS_SIGNAL_STRENGTH, UNIT_PERCENT, 
    UNIT_DECIBEL_MILLIWATT, ENTITY_CATEGORY_DIAGNOSTIC
)
from . import lora_bridge_ns, LoRaBridge, CONF_MAX_NODES

CONFIG_SCHEMA = cv.Schema({
    cv.Required("bridge_id"): cv.use_id(LoRaBridge),
})

async def to_code(config):
    parent = await cg.get_variable(config["bridge_id"])
    full_config = CORE.config
    max_nodes = full_config["lora_bridge"][CONF_MAX_NODES]
    
    # 预准备验证器
    base_schema = sensor.sensor_schema()

    for i in range(max_nodes):
        # HA 显示的名称依然用 i+1，让用户看到 01-32
        friendly_index = i + 1
        # Battery
        bat_id = ID(f"lora_dev_{friendly_index:02d}_battery", is_declaration=True, type=sensor.Sensor)
        bat_conf = base_schema({
            CONF_ID: bat_id,
            CONF_NAME: f"LoRa Dev {friendly_index:02d} Battery",
            "device_class": DEVICE_CLASS_BATTERY,
            "unit_of_measurement": UNIT_PERCENT,
            "entity_category": ENTITY_CATEGORY_DIAGNOSTIC,
        })
        bat_var = cg.new_Pvariable(bat_id)
        await sensor.register_sensor(bat_var, bat_conf)
        # 这里传给 C++ 的 index 变为 0-31
        cg.add(parent.register_battery(i, bat_var))

        # RSSI 
        rssi_id = ID(f"lora_dev_{friendly_index:02d}_rssi", is_declaration=True, type=sensor.Sensor)
        rssi_conf = base_schema({
            CONF_ID: rssi_id,
            CONF_NAME: f"LoRa Dev {friendly_index:02d} RSSI",
            "device_class": DEVICE_CLASS_SIGNAL_STRENGTH,
            "unit_of_measurement": UNIT_DECIBEL_MILLIWATT,
            "entity_category": ENTITY_CATEGORY_DIAGNOSTIC,
        })
        rssi_var = cg.new_Pvariable(rssi_id)
        await sensor.register_sensor(rssi_var, rssi_conf)
        # 这里传给 C++ 的 index 变为 0-31
        cg.add(parent.register_rssi(i, rssi_var))