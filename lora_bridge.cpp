#include "lora_bridge.h"

namespace esphome {
namespace lora_bridge {

static const char *TAG = "lora_bridge";
// 使用常量代替魔法数字，方便维护。 该数值，是存储抽屉的id。
static const uint32_t LORA_STORAGE_VERSION = 2026051901UL;
//统一定义ESPHome下的lora子设备的secret
static const char *ESPHOME_DEV_SECRET = "00000000000000000000000000000000";

static const char *default_gtwId = "000000000000";
static const char default_channel = 1;


void LoRaBridge::setup() {
	ESP_LOGI(TAG, "LoRa Bridge Initialized");
	//从 Flash 读取 active_device_count_
	this->pref_ = global_preferences->make_preference<GtwStorage>(LORA_STORAGE_VERSION);
	if (this->pref_.load(&(this->storage_data_))) {
		ESP_LOGI("storage", "Load config from Flash success!");
	} else {
		ESP_LOGW("storage", "No config found, using defaults");
		// 初始化清空
		memset(&(this->storage_data_), 0, sizeof(storage_data_));
		strcpy(storage_data_.gtw_name, default_gtwId);
		storage_data_.channel = default_channel;
	}

	// 根据加载的数据，决定哪些设备该显示
	this->refresh_entities();

	//this->apply_device_config(4); //test 3
	
	//注册
	this->register_service(&LoRaBridge::on_set_config, "set_config", {"config_str"});
}

int LoRaBridge::calc_dev_crc(std::string &calcedValue) {
    std::string full_concat_str = "";
    int max_active_idx = -1;

    for (int i = 0; i < MAX_ITEM_NUM; i++) {
        if (!storage_data_.devices[i].active) continue;

        // 记录最大索引
        max_active_idx = i;

        // 拼装符合 AT+DEVITEM 格式的字符串
        // 格式: idx,Type,DevId[,slot]
        char item_buf[128];
        if (storage_data_.devices[i].type == 'B') {
            snprintf(item_buf, sizeof(item_buf), "%d,%c,%s,%s,%d", 
                     i, storage_data_.devices[i].type, 
                     storage_data_.devices[i].device_id,
					 ESPHOME_DEV_SECRET,					 
                     storage_data_.devices[i].slot_interval);
        } else {
            snprintf(item_buf, sizeof(item_buf), "%d,%c,%s,%s", 
                     i, storage_data_.devices[i].type, 
                     storage_data_.devices[i].device_id,
					 ESPHOME_DEV_SECRET);
        }
        
        full_concat_str += std::string(item_buf);
    }

    // 计算 CRC
    uint16_t crc = calc_crc(full_concat_str);
    
    // 转换为 4 位 Hex 字符串
    char hex_buf[5];
    snprintf(hex_buf, sizeof(hex_buf), "%04X", crc);
    calcedValue = std::string(hex_buf);

    return max_active_idx + 1;
}

int LoRaBridge::parse_device_config(const std::string &input, DeviceStorage &ds) {
    // input 格式: "DEV:1,XXXXXX,B,8" 或 "DEV:1,XXXXXX,B"
    // 移除前缀 "DEV:"
    std::string data = input.substr(4);
    
    // 初始化变量
    int idx = 0;
    char dev_id[MAX_DEVID_NAME] = {0};
    char type_buf[4] = {0};
    int slot = 0; // 默认值

    // 统计逗号数量，判断是否有 slot
    int commas = std::count(data.begin(), data.end(), ',');

    if (commas == 2) {
        // 格式: 1,XXXXXX,B (没有slot)
        int parsed = sscanf(data.c_str(), "%d,%17[^,],%s", &idx, dev_id, type_buf);
		if (parsed < 2) {
			ESP_LOGW(TAG, "Malformed config string!");
			return 0; // 丢弃异常数据
		}
    } else if (commas == 3) {
        // 格式: 1,XXXXXX,B,8 (有slot)
        int parsed = sscanf(data.c_str(), "%d,%17[^,],%s,%d", &idx, dev_id, type_buf, &slot);
		if (parsed < 2) {
			ESP_LOGW(TAG, "Malformed config string!");
			return 0; // 丢弃异常数据
		}
    }

    // 逻辑处理
    if (idx >= 1 && idx <= MAX_ITEM_NUM) {
        strncpy(ds.device_id, dev_id, MAX_DEVID_NAME);
        ds.type = type_buf[0]; // 将 "B" 转为字符 'B'
        ds.slot_interval = slot;
        ds.active = true;
        
        ESP_LOGI(TAG, "parse device config: Idx=%d, ID=%s, Type=%c, Slot=%d", idx, dev_id, ds.type, slot);
    }
	return idx;
}
// 辅助函数：解析通用配置字符串
void LoRaBridge::parse_config_string(std::string config_str) {

    if (config_str.find("DEV:") == 0) {
        // 解析 DEV:1,XXXXXX,B,8
        // 格式: Prefix:Idx,DevID,Type,Slot
		DeviceStorage ds;
        int idx = parse_device_config(config_str, ds);
        if (idx == 0) { //分析失败，可能下发的参数错误
			ESP_LOGW(TAG, "parse fail");
			return;
		}
		//写入内存
        DeviceStorage &dsDest = storage_data_.devices[idx-1];
		memcpy(&dsDest, &ds, sizeof(DeviceStorage));

        // 对接 AT 指令 2: AT+DEVITEM
        char at_buf[128];
        snprintf(at_buf, sizeof(at_buf), "AT+DEVITEM=%d,%c,%s,%s,%d", idx-1, ds.type, ds.device_id, ESPHOME_DEV_SECRET, ds.slot_interval);
        send_at_command(at_buf);

    } else if (config_str.find("CHN:") == 0) {
        // 解析 CHN:1
        int chn = std::stoi(config_str.substr(4));
        storage_data_.channel = (char)chn;
        
        // 对接 AT 指令 8
        char at_buf[32];
        snprintf(at_buf, sizeof(at_buf), "AT+CHANNEL=%d", chn);
        send_at_command(at_buf);

    } else if (config_str.find("GTW:") == 0) {
        // 解析 GTW:xxxxxxx
        std::string gtw = config_str.substr(4);
        strncpy(storage_data_.gtw_name, gtw.c_str(), MAX_DEVID_NAME);
        
        // 对接 AT 指令 9
        std::string cmd = "AT+GTWID=" + gtw;
        send_at_command(cmd);
    }

    // 保存全量配置
    //this->pref_.save(&this->storage_data_);
    
	//防抖动，缓一下统一写
    config_dirty = true;
    last_dirty_time = millis(); // 刷新时间
}

// 供 HA 调用的 Service。 处理HA推送消息的总入口
void LoRaBridge::on_set_config(std::string config_str) {
    ESP_LOGI(TAG, "receive HA message: %s", config_str.c_str());
	
    this->parse_config_string(config_str);
    
	//如果是设置子设备的，还要看是否触发了三元组结束包
	if (config_str.find("DEV:") == 0) {
		// 触发三元组结束包 (AT+DEVINFO)
		// 根据需求在 DEV 配置后调用
		std::string dev_crc_hex;
		int num = calc_dev_crc(dev_crc_hex);
		
        char at_buf[32];
        snprintf(at_buf, sizeof(at_buf), "AT+DEVINFO=%d,%s", num, dev_crc_hex.c_str());
		send_at_command(at_buf);
	}
}

// 设置各个节点的情况，只有配置过的才显示，没有配置到的都隐藏
void LoRaBridge::refresh_entities() {
	for (int i = 0; i < MAX_ITEM_NUM; i++) {
		bool is_active = storage_data_.devices[i].active;
		// 调用之前写好的隐藏逻辑
		auto* dev = get_or_create_device(i);
		
        if (is_active == true) ESP_LOGI(TAG, "lora device idx(%d) is active", i);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
		if (dev->valve_switch) dev->valve_switch->set_internal(!is_active);
        if (dev->battery_sensor) dev->battery_sensor->set_internal(!is_active);
        if (dev->rssi_sensor) dev->rssi_sensor->set_internal(!is_active);
        if (dev->delay_flag) dev->delay_flag->set_internal(!is_active);
        if (dev->last_watering_time) dev->last_watering_time->set_internal(!is_active);
#pragma GCC diagnostic pop

	}
}

void LoRaBridge::loop() {
	// 1. 处理串口数据（lora-module过来的AT指令）
	while (this->available()) {
		char c = this->read();
		if (c == '\n') {
			if (!line_buffer_.empty()) {
				parse_line(line_buffer_);
				line_buffer_.clear();
			}
		} else if (c != '\r') {
			line_buffer_ += c;
		}
	}
	
    uint32_t nowtime = millis();
	// 2. 超时检测：如果等了 2000ms 还没收到 OK/ERROR，强制跳过继续发送下一条
    if (at_cmd_waiting_ack_ && (nowtime - ack_timeout_timer_ > 2000)) {
        ESP_LOGW(TAG, "AT Command Timeout, skipping...");
        at_cmd_waiting_ack_ = false;
    }
	
	// 3. AT 指令发送调度器
    if (!at_cmd_waiting_ack_ && !at_cmd_queue_.empty()) {
		std::string cmd = at_cmd_queue_.front();
		at_cmd_queue_.pop_front();
		
		// 实际执行 UART 发送
		this->write_str(cmd.c_str());
		this->write_str("\r\n"); // 假设 AT 指令以换行结束
		
		at_cmd_waiting_ack_ = true;
		last_at_send_time_ = nowtime;
		ESP_LOGI(TAG, "Sent (lora) AT: %s", cmd.c_str());
    }
	
	// 4. 防抖：配置改变后 1秒内没有新的改变，才执行物理写入
    if (config_dirty && (nowtime - last_dirty_time > 1000)) {
        this->pref_.save(&(this->storage_data_));
        config_dirty = false;
    }
	
	// 5. 定期检查网络变化。如果有变化，就发送给lora模块
    static uint32_t last_check_change = 0;
    if (nowtime - last_check_change > 5000) { // 每5秒检查一次网络
        check_link_to_notify_lora();
        last_check_change = nowtime;
    }
	
	// 6. 定期检查并清理上报消息的缓存队列
    static uint32_t last_check_cache = 0;
    if (nowtime - last_check_cache > 5000) { // 每5秒检查一次网络
        if (this->is_network_connected()) {
            this->process_cache_queue();
        }
        last_check_cache = nowtime;
    }
	
    // 7. 检查所有设备的重发状态
    for (auto const& [index, dev] : devices_) {
        if (!dev->cmd_pending) continue;

        // 检查是否超时
        if (nowtime - dev->last_send_time > dev->timeout_ms) {
            if (dev->retry_count < 3) {
                // --- 情况 1：重试 ---
                dev->retry_count++;
                dev->last_send_time = nowtime;
                ESP_LOGD(TAG, "device %d resp timeout, now try to resend %d ...", index + 1, dev->retry_count);
                this->send_valve_command(index, dev->target_valve_state);
            } 
            else {
                // --- 情况 2：3次重试全失败 ---
                ESP_LOGW(TAG, "device %d repeat send fail.", index + 1);
                dev->cmd_pending = false;
                
                // 清除忙碌标记
                if (dev->delay_flag) dev->delay_flag->publish_state(false);
                
                // 【关键】反向通知 HA 恢复原始状态
                // 假设失败了，我们将开关拨回到 target 的相反状态，或者维持旧状态
                // 这里的处理逻辑是：既然失败了，设备状态大概率没变，我们同步为反向
                if (dev->valve_switch) {
                    dev->valve_switch->publish_state(!dev->target_valve_state);
                }
            }
        }
    }
}

// 定期检查网络状况，如果网络变化，就给Lora-module发一个AT指令
void LoRaBridge::check_link_to_notify_lora() {
    bool curState = this->is_network_connected();
	if (curState != last_link_state) //状态发生变化
	{
		char cmd[32];
        // 指令格式为 AT+ONLINE=isOnline
		snprintf(cmd, sizeof(cmd), "AT+ONLINE=%d", (int)curState);
        send_at_command(cmd);
        ESP_LOGI(TAG, "set curLinkState -> %d", curState);
		
		last_link_state = curState;
	}
}

// 从 bytes 中定位到第 idx 个 KLV 的起始偏移（相对于 bytes.data()）
// 返回偏移值，如果不存在则返回 -1
int LoRaBridge::get_klv_offset(const std::vector<uint8_t>& bytes, int idx) {
    size_t offset = 0;
    for (int i = 0; i < idx; ++i) {
        if (offset + 1 >= bytes.size()) return -1;
        uint8_t valueLen = bytes[offset + 1] & 0x3F;   // KLV 长度字段（低6位）
        offset += 2 + valueLen;
        if (offset >= bytes.size()) return -1;
    }
    return static_cast<int>(offset);
}

// 处理一个 KLV 数据包，更新设备状态
void LoRaBridge::process_klv_node(LoRaDevice* dev, int pktType, int combKey, const std::vector<uint8_t>& value) {
    // 根据 combKey 处理不同数据类型
	ESP_LOGI(TAG, " get combKey: 0x%x", combKey);
    switch (combKey) {
        case 0x0301: { // 电池电压
            if (value.size() >= 1) {
                uint8_t volt = value[0];
				
				ESP_LOGI(TAG, " -get Volt: %d", volt);
                if (dev->battery_sensor) {
                    // 假设电压值需要转换，例如 0-255 对应 0-12V
                    //float battery_voltage = volt / 255.0f * 12.0f;
					float battery_voltage = volt / 10; //设备端上传的值，是放大了10倍的。直接除去10来显示就可以了。
                    //dev->battery_sensor->publish_state(battery_voltage);
					this->publish_or_cache(dev->index, ATTR_BATTERY, battery_voltage);
                }
            }
            break;
        }
        case 0x0703: { // 低电量报警
            if (value.size() >= 1) {
                bool low_power = (value[0] == 0x31);
                // 可选：通过二进制传感器发布
                // if (dev->low_power_sensor) dev->low_power_sensor->publish_state(low_power);
            }
            break;
        }
        case 0x0203: { // 开关阀
            if (value.empty()) break;
			ESP_LOGI(TAG, " -recv switch type: %d, value: %d", pktType, value[0]);
			// 收到回复了，先关闭状态机
			if (dev->cmd_pending) {
				//ESP_LOGI(TAG, "设备 %d 响应成功，结束重发监控", dev->index + 1);
				dev->cmd_pending = false;
			}
		
			//然后再处理
            uint8_t val = value[0];
			//不管是主动上报还是被动回应，这里都简化处理
            bool is_open = (val == 1);
            this->publish_or_cache(dev->index, ATTR_VALVE, is_open ? 1.0f : 0.0f);
			
			this->publish_or_cache(dev->index, ATTR_DELAY_FLAG, 0.0f); //设备端上报了开关状态，清除该标记
			
            break;
        }
        default:
            // 未知 key，忽略
            break;
    }
}

//-----------------------
// 解析完整的 KLV 数据流（二进制格式）
// pktType: 0=主动上行包, 1=下行回应包
void LoRaBridge::process_klv_data(LoRaDevice* dev, const std::vector<uint8_t>& dataWithHead) {
    int idx = 0;
	int pktType = (dataWithHead[0] & 0xF);
	//const std::vector<uint8_t>& data = dataWithHead + 2;
	std::vector<uint8_t> data(dataWithHead.begin() + 2, dataWithHead.end());
	
    while (true) {
        int offset = get_klv_offset(data, idx);
        if (offset < 0) break;
        if (offset + 1 >= (int)data.size()) break;
        
        uint8_t first = data[offset];
        uint8_t second = data[offset + 1];
        uint8_t keyType = (first >> 4) & 0x0F;
        uint8_t keyId = ((first & 0x0F) << 2) | ((second >> 6) & 0x03);
        uint8_t keyLen = second & 0x3F;
        
        if (offset + 2 + keyLen > (int)data.size()) break; // 数据不足
        
        std::vector<uint8_t> value(data.begin() + offset + 2, data.begin() + offset + 2 + keyLen);
        int combKey = (keyType << 8) + keyId;
        
        process_klv_node(dev, pktType, combKey, value);
        
        ++idx;
    }
	ESP_LOGI(TAG, "process_klv_data totNum: %d", idx);
}

int LoRaBridge::hex_string_to_bytes(const std::string& hex, uint8_t *buffer, size_t max_len) {
    int len = hex.length() / 2;
    if (len > max_len) len = max_len;
    for (int i = 0; i < len; ++i) {
        std::string byteString = hex.substr(i * 2, 2);
        buffer[i] = (uint8_t) strtol(byteString.c_str(), nullptr, 16);
    }
    return len;
}

// 辅助函数：从 AT+DUP 指令中提取设备索引和十六进制数据，并转换为字节数组
// 返回值：true 表示解析成功，false 表示格式错误
bool LoRaBridge::parse_uppkt_hex_data(const std::string &line, int &out_idx, std::vector<uint8_t> &out_binary_data) {
    // 检查前缀
    if (line.find("AT+DUP=") != 0) return false;
    
    // 去掉 "AT+DUP=" 部分
    std::string temp = line.substr(7);
    std::vector<std::string> parts;
    size_t pos = 0;
    while ((pos = temp.find(',')) != std::string::npos) {
        parts.push_back(temp.substr(0, pos));
        temp.erase(0, pos + 1);
    }
    if (!temp.empty()) parts.push_back(temp);
    
    if (parts.size() < 3) {
        ESP_LOGW(TAG, "Invalid AT+DUP format: too few parts");
        return false;
    }
    
    int idx = std::stoi(parts[0]);
    int data_len = std::stoi(parts[1]);
    std::string hex_data = parts[2];
    // parts[3] 是 CRC，可忽略或后续校验
    
	uint8_t buffer[64];
    int binary_len = hex_string_to_bytes(hex_data, buffer, 64); //lora包长不超过64
    if (binary_len*2 != static_cast<size_t>(data_len)) {
        ESP_LOGW(TAG, "Data length mismatch: expected %d, got %zu", data_len, binary_len);
        // 仍然继续解析，只是警告
    }
    
    out_idx = idx;
	out_binary_data.assign(buffer, buffer + binary_len);
    return true;
}

//分析AT+START的同步信息
//如：AT+START=00.01,470,0,3,1F20,36f98c047f4d65cb,xxxx
bool LoRaBridge::parse_sync_data(const std::string &line, int &out_chn, int &out_devNum, std::string &out_devCrc, std::string &out_gtwId) {
    // 检查前缀
    if (line.find("AT+START=") != 0) return false;
    
    // 去掉 "AT+DUP=" 部分
    std::string temp = line.substr(9);
    std::vector<std::string> parts;
    size_t pos = 0;
    while ((pos = temp.find(',')) != std::string::npos) {
        parts.push_back(temp.substr(0, pos));
        temp.erase(0, pos + 1);
    }
    if (!temp.empty()) parts.push_back(temp);
    
    if (parts.size() < 6) {
        ESP_LOGW(TAG, "Invalid AT+START format: too few parts");
        return false;
    }
    
    out_chn = std::stoi(parts[2]);
    out_devNum = std::stoi(parts[3]);
    out_devCrc = parts[4];
	out_gtwId = parts[5];
    // parts[6] 是 CRC，可忽略或后续校验
    
    return true;
}

// --- 数据解析逻辑 ---
void LoRaBridge::parse_line(const std::string &line) {
	ESP_LOGI(TAG, "Read (lora) AT: %s", line.c_str());
	if (line.find("OK") != std::string::npos || line.find("ERROR") != std::string::npos) {
        // 收到响应，清除标志位，允许发送下一条
        at_cmd_waiting_ack_ = false;
        ESP_LOGD(TAG, "ACK received: %s", line.c_str());
    }

	// 1. 响应时间同步 (GD405 启动必备)
	if (line.find("AT+TIME?") != std::string::npos) {
		// 假设发送一个固定的时间戳，offset=8 (北京时间)
		send_at_command("AT+TIME=1715065000,8,0"); 
		return;
	}
	if (line.find("AT+START=") != std::string::npos) {
		// 收到AT信息后，验证是否跟ESP32里记录的匹配。格式如下：
		//AT+START=version,freq,channel,devNum,devCrc,gtwIdStr,atCrc   //lora每次启动时同步信息给esp32
		//如：AT+START=00.01,470,0,3,1F20,36f98c047f4d65cb,xxxx
		int chn, devNum;
		std::string devCrc, gtwId;
		bool isSucc = parse_sync_data(line, chn, devNum, devCrc, gtwId);
		
		int localDevNum;
		std::string localDevCrc;
		localDevNum = calc_dev_crc(localDevCrc);
		
		//compare stored info
		// device info 
		if (devNum != localDevNum || devCrc != localDevCrc) {
			ESP_LOGD(TAG, "dev match fail. %d-%d, %s-%s", devNum, localDevNum, devCrc.c_str(), localDevCrc.c_str());
			for (int i=0; i<MAX_ITEM_NUM; i++) {
				if (storage_data_.devices[i].active) {
					DeviceStorage *ds = &storage_data_.devices[i];
					char at_buf[128];
					snprintf(at_buf, sizeof(at_buf), "AT+DEVITEM=%d,%c,%s,%s,%d", i, ds->type, ds->device_id, ESPHOME_DEV_SECRET, ds->slot_interval);
					send_at_command(at_buf);
				}
			}
			//send_at_command("AT+DEVINFO=...");
			std::string dev_crc_hex;
			int num = calc_dev_crc(dev_crc_hex);
			
			char at_buf[32];
			snprintf(at_buf, sizeof(at_buf), "AT+DEVINFO=%d,%s", num, dev_crc_hex.c_str());
			send_at_command(at_buf);
		} else if (storage_data_.channel != chn) {
			ESP_LOGD(TAG, "channel match fail. %d-%d", chn, storage_data_.channel);
			//send_at_command("AT+CHANNEL=" + ...);
			char at_buf[32];
			snprintf(at_buf, sizeof(at_buf), "AT+CHANNEL=%d", storage_data_.channel);
			send_at_command(at_buf);
		} else if (std::string(storage_data_.gtw_name) != gtwId) {
			ESP_LOGD(TAG, "gtwId match fail. %s-%s", gtwId.c_str(), storage_data_.gtw_name);
			//send_at_command("AT+GTWID=...");
			char at_buf[64];
			snprintf(at_buf, sizeof(at_buf), "AT+GTWID=%s", storage_data_.gtw_name);
			send_at_command(at_buf);
		}
		ESP_LOGI(TAG, "process START-cmd over");
		return;
	}

	// 2. 解析上行数据 (命令B: 子设备主动开灯)
	// 假设格式为: AT+DUP=idx,len,dataHexStr,atCrc 
	if (line.find("AT+DUP=") == 0) {
		int idx = 0;
		std::vector<uint8_t> binary_data;
		if (!parse_uppkt_hex_data(line, idx, binary_data)) {
			send_at_command("AT+DUP=ERROR");
			return;
		}
		
		LoRaDevice* dev = get_or_create_device(idx);
		if (!dev) {
			ESP_LOGE(TAG, "Failed to get device for idx %d", idx);
			send_at_command("AT+DUP=ERROR");
			return;
		}
		
		// 解析 KLV 数据
		process_klv_data(dev, binary_data);
		
		// 清除延迟标志（设备已回应）
		//if (dev->delay_flag) dev->delay_flag->publish_state(false);
		if (dev->rssi_sensor) dev->rssi_sensor->publish_state(12); //模拟一个信号强度
		
		send_at_command("AT+DUP=OK");
		return;
	}
	// ... 其他同步逻辑
}

void LoRaBridge::send_at_command(const std::string &cmd) {
  uint16_t crc = calc_crc(cmd);
  char full_cmd[128];
  snprintf(full_cmd, sizeof(full_cmd), "%s,%04X\r\n", cmd.c_str(), crc);
  //this->write_str(full_cmd);
  at_cmd_queue_.push_back(full_cmd);
  
  ESP_LOGI(TAG, "Sent AT: %s", cmd.c_str());
}

// 传入 index，返回对应的 delay_flag 句柄
binary_sensor::BinarySensor* LoRaBridge::get_delay_flag_by_index(int index) {
    if (devices_.count(index)) {
        return devices_[index]->delay_flag;
    }
    return nullptr;
}

//专用的下发控制函数。方便重发时的调用。
void LoRaBridge::send_valve_command(int index, bool open) {
    if (storage_data_.devices[index].active && devices_[index]->valve_switch != nullptr) {
        //获得下行idx
		int dnId = devices_[index]->valve_switch->dnPktIdx;
		
		char cmd[64];
        // 指令格式为 AT+DDN=Idx,Len,Data
        // 这里根据协议拼装，例如 "000120C101" 为开阀
		snprintf(cmd, sizeof(cmd), "AT+DDN=%d,10,00%02d20C10%d", index, dnId, (int)open);
        send_at_command(cmd);
        ESP_LOGI(TAG, "Index %d: set switch -> %d", index + 1, open);
    }
}
// --- 控制下发逻辑 ---
void LoRaSwitch::write_state(bool state) {
	auto *dev = bridge_->get_or_create_device(this->index_);
	
	// 1. 初始化重发状态机
    dev->cmd_pending = true;
    dev->target_valve_state = state;
    dev->retry_count = 0;
    dev->last_send_time = millis();
    
    // 根据 slot 设置超时，如果没有 slot 则默认 2s（没有设置slot，认为是C设备）
    int slot = bridge_->storage_data_.devices[this->index_].slot_interval;
    dev->timeout_ms = (slot > 0) ? (slot * 1000 + 2000) : 2000; 

    // 2. 设置 Busy 状态
	// 通过 bridge 查找同一 index 下的延迟标记位
	auto *delay_flag = bridge_->get_delay_flag_by_index(this->index_);
	if (delay_flag != nullptr) delay_flag->publish_state(true); // 设置为正在操作
	
    // 3. 乐观上报状态（让 HA 图标先转过去）
    this->publish_state(state);

	dnPktIdx++; //下行索引+1
    // 4. 发送第一条指令
    bridge_->send_valve_command(this->index_, state);
	
}

uint16_t LoRaBridge::calc_crc(const std::string &data) {
  uint16_t crc = 0xFFFF;
  for (char c : data) {
    crc ^= (uint8_t)c;
    for (int i = 0; i < 8; i++) {
      if (crc & 1) crc = (crc >> 1) ^ 0xA001;
      else crc >>= 1;
    }
  }
  return crc;
}

// 辅助函数：按需创建设备容器
LoRaDevice* LoRaBridge::get_or_create_device(int index) {
  if (devices_.find(index) == devices_.end()) {
    auto *dev = new LoRaDevice();
    dev->index = index;
    devices_[index] = dev;
  }
  return devices_[index];
}

//-----离线缓存数据-----------
// 检查 HA API 是否已连接
bool LoRaBridge::is_network_connected() {
    return remote_is_connected(); 
}

// 核心逻辑：有网直接发，没网存队列
void LoRaBridge::publish_or_cache(int index, AttributeId attr, float value, std::string s_value) {
    if (this->is_network_connected() && cache_queue_.empty()) {
        // 有网且没有积压缓存，直接上报
        LoRaDevice* dev = get_or_create_device(index);
        switch (attr) {
            case ATTR_VALVE: if(dev->valve_switch) dev->valve_switch->publish_state(value > 0.5); break;
            case ATTR_BATTERY: if(dev->battery_sensor) dev->battery_sensor->publish_state(value); break;
            case ATTR_RSSI: if(dev->rssi_sensor) dev->rssi_sensor->publish_state(value); break;
            case ATTR_DELAY_FLAG: if(dev->delay_flag) dev->delay_flag->publish_state(value > 0.5); break;
            case ATTR_LAST_TIME: if(dev->last_watering_time) dev->last_watering_time->publish_state(s_value); break;
            // case ATTR_TEMP...
        }
    } else {
        // 没网或有积压，入队
        if (cache_queue_.size() >= MAX_CACHE_SIZE) {
            cache_queue_.pop_front(); // 丢弃最旧的数据
        }
        cache_queue_.push_back({index, attr, value, s_value});
        ESP_LOGW(TAG, "Network offline, data cached. Queue size: %d", cache_queue_.size());
    }
}

// 恢复联网后，依次清空缓存（逐条上传）
void LoRaBridge::process_cache_queue() {
    if (cache_queue_.empty()) return;

    ESP_LOGI(TAG, "Network recovered, flushing %d cached packets...", cache_queue_.size());
    
    // 每次处理几条，避免一次性大量上报导致拥塞（可选）
    while(!cache_queue_.empty() && this->is_network_connected()) {
        DataPacket p = cache_queue_.front();
        
        // 借用分发逻辑（此时强制发送）
        LoRaDevice* dev = get_or_create_device(p.index);
        switch (p.attr_id) {
            case ATTR_VALVE: if(dev->valve_switch) dev->valve_switch->publish_state(p.float_val > 0.5); break;
            case ATTR_BATTERY: if(dev->battery_sensor) dev->battery_sensor->publish_state(p.float_val); break;
            case ATTR_RSSI: if(dev->rssi_sensor) dev->rssi_sensor->publish_state(p.float_val); break;
            case ATTR_DELAY_FLAG: if(dev->delay_flag) dev->delay_flag->publish_state(p.float_val > 0.5); break;
            case ATTR_LAST_TIME: if(dev->last_watering_time) dev->last_watering_time->publish_state(p.string_val); break;
        }
        
        cache_queue_.pop_front();
    }
}
//---------------------------


// 注册接口实现
void LoRaBridge::register_valve(int index, LoRaSwitch *obj) {
  get_or_create_device(index)->valve_switch = obj;
  //ESP_LOGI(TAG, "register_valve idx: %d", index);
}
void LoRaBridge::register_battery(int index, sensor::Sensor *obj) {
  get_or_create_device(index)->battery_sensor = obj;
  //ESP_LOGI(TAG, "register_battery idx: %d", index);
}
void LoRaBridge::register_rssi(int index, sensor::Sensor *obj) {
  get_or_create_device(index)->rssi_sensor = obj;
  //ESP_LOGI(TAG, "register_rssi idx: %d", index);
}
void LoRaBridge::register_delay_flag(int index, binary_sensor::BinarySensor *obj) {
  get_or_create_device(index)->delay_flag = obj;
  //ESP_LOGI(TAG, "register_delay_flag idx: %d", index);
}
void LoRaBridge::register_last_time(int index, text_sensor::TextSensor *obj) {
  get_or_create_device(index)->last_watering_time = obj;
  //ESP_LOGI(TAG, "register_last_time idx: %d", index);
}
// ... 其他 register 函数依此类推
}
}