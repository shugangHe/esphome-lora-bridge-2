#pragma once
#include "esphome.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/preferences.h"
#include "esphome/components/api/custom_api_device.h"

#include <map>
#include <deque> // 引入队列


#define MAX_ITEM_NUM 32   //最大支持设备数量
#define MAX_DEVID_NAME 18 //设备deviceId。正常长度是16字节


namespace esphome {
namespace lora_bridge {
	
// 定义属性 ID 枚举，标识哪个传感器
// 用于离线时缓存上行数据包
enum AttributeId {
    ATTR_VALVE,
    ATTR_BATTERY,
    ATTR_RSSI,
    ATTR_DELAY_FLAG,
    ATTR_LAST_TIME,
    ATTR_TEMP, // 预留 B 类型
    ATTR_HUMI  // 预留 B 类型
};
// 定义缓存数据包结构
struct DataPacket {
    int index;
    AttributeId attr_id;
    float float_val;
    std::string string_val; // 用于 TextSensor
};


class LoRaBridge;
class LoRaSwitch;

//子设备信息存储
struct DeviceStorage {
    char device_id[MAX_DEVID_NAME];   // 存储设备deviceId
    char type; 				// classA/B/C类型. 存储为 字母A/B/C
	int slot_interval;		// classB使用的slot
    bool active;          	// 是否启用
} __attribute__((packed));

//网关总存储信息
struct GtwStorage {
    DeviceStorage devices[MAX_ITEM_NUM];  // 存储设备deviceId
    char channel;                         // channel id
	char gtw_name[MAX_DEVID_NAME];        // 网关名字和dev名字一样长，16字节
	uint16_t crc;                         // crc校验和，防止Flash损坏
} __attribute__((packed));


// --- 子设备管理类 ---
class LoRaDevice {
 public:
  int index;
  // 以下是该设备拥有的所有实体
  LoRaSwitch *valve_switch{nullptr};
  sensor::Sensor *battery_sensor{nullptr};
  sensor::Sensor *rssi_sensor{nullptr};
  binary_sensor::BinarySensor *delay_flag{nullptr}; // 延迟标记 (1:操作中, 0:已完成)
  text_sensor::TextSensor *last_watering_time{nullptr};

  // --- 下行命令重发控制状态机 ---
  bool cmd_pending{false};      // 是否正在等待子设备回复
  bool target_valve_state{false}; // 期望设置成的开关状态
  uint32_t last_send_time{0};   // 上一次发送指令的时间戳
  int retry_count{0};           // 已重发次数
  uint32_t timeout_ms{10000};   // 超时时间（毫秒），默认10秒
  
};

// --- 自定义开关类 ---
class LoRaSwitch : public switch_::Switch, public Component {
 public:
  void set_bridge(LoRaBridge *bridge) { bridge_ = bridge; }
  void set_index(int index) { index_ = index; }
  
  void write_state(bool state) override;

 protected:
  LoRaBridge *bridge_;
  int index_;
  
 public:
  int dnPktIdx{1}; //down packet id
  
};

// --- 主 Bridge 类 ---
class LoRaBridge : public Component, public uart::UARTDevice, public api::CustomAPIDevice {
 public:
  void setup() override;
  void loop() override;

  // 注册各种实体到对应的设备索引
  void register_valve(int index, LoRaSwitch *obj);
  void register_battery(int index, sensor::Sensor *obj);
  void register_rssi(int index, sensor::Sensor *obj);
  void register_delay_flag(int index, binary_sensor::BinarySensor *obj);
  void register_last_time(int index, text_sensor::TextSensor *obj);
  
  // 接收HA下发的推送消息
  void on_set_config(std::string config_str); // HA下发设置的通用入口
  int parse_device_config(const std::string &input, DeviceStorage &ds);
  void refresh_entities();
  
  void send_at_command(const std::string &cmd);
  binary_sensor::BinarySensor* get_delay_flag_by_index(int index);
  
  // 协议解析
  int get_klv_offset(const std::vector<uint8_t>& bytes, int idx);
  void process_klv_node(LoRaDevice* dev, int pktType, int combKey, const std::vector<uint8_t>& value);
  void process_klv_data(LoRaDevice* dev, const std::vector<uint8_t>& data);
  int hex_string_to_bytes(const std::string& hex, uint8_t *buffer, size_t max_len);
  bool parse_uppkt_hex_data(const std::string &line, int &out_idx, std::vector<uint8_t> &out_binary_data);
  bool parse_sync_data(const std::string &line, int &out_chn, int &out_devNum, std::string &out_devCrc, std::string &out_gtwId);
  void parse_config_string(std::string config_str);
  
  // 判断网络状态及数据分发/缓存接口
  bool is_network_connected();
  void publish_or_cache(int index, AttributeId attr, float value, std::string s_value = "");
  void process_cache_queue();
  
  LoRaDevice* get_or_create_device(int index);
  // 实际发送 AT 指令的私有方法，方便重复调用
  void send_valve_command(int index, bool open);

 protected:
  // 缓存队列和最大容量限制
  std::deque<DataPacket> cache_queue_;
  const size_t MAX_CACHE_SIZE = 200; // 根据内存情况调整
  
 public:
  ESPPreferenceObject pref_; // NVS 句柄
  GtwStorage storage_data_; //内存缓存
  
 private:
  // 发送AT使用队列，防止一下子发太多，对方来不及收
  std::deque<std::string> at_cmd_queue_;
  bool at_cmd_waiting_ack_{false};
  uint32_t last_at_send_time_{0};
  uint32_t ack_timeout_timer_{0};
  //const uint32_t ACK_TIMEOUT_MS = 2000; // 2000ms 间隔
 
 private:
  //存储flash时放抖动
  bool config_dirty = false;
  uint32_t last_dirty_time = 0;
  
 private:
  std::string line_buffer_;
  std::map<int, LoRaDevice*> devices_; 
  int active_device_count_ = MAX_ITEM_NUM; // 默认 MAX_ITEM_NUM 个节点全部可见，后续从 Flash 加载
  bool last_link_state{false};

  // 返回值是最大的已激活 index，calcedValue 存储计算出的 CRC 字符串
  int calc_dev_crc(std::string &calcedValue);
  void check_link_to_notify_lora();
  void parse_line(const std::string &line);
  uint16_t calc_crc(const std::string &data);
  
};

}  // namespace lora_bridge
}  // namespace esphome