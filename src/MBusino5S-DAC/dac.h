// DFR0971 (GP8403) 2-channel 0-10V DAC on I2C, used to throttle a heat pump.
// Channels are 1 and 2 in MQTT/HA, 0 and 1 internally. Voltages are stored in mV.
// Auto-detected at DAC_I2C_ADDR; disabled with extension 7 (GPIO33/35 are OneWire buses there).

#define DAC_I2C_ADDR 0x58
#define DAC_SDA 33
#define DAC_SCL 35
#define DAC_EEPROM_ADDR 600
#define DAC_MAGIC 0xDA

struct dacSettings {
  uint8_t magic;
  uint16_t mv[2];        // last commanded output
  uint16_t failsafeMv;   // output when MQTT is lost for failsafeMin minutes
  uint16_t failsafeMin;  // 0 = fail-safe off, keep last value
} dacCfg = {DAC_MAGIC, {0, 0}, 0, 0};

static_assert(100 + sizeof(settings) <= DAC_EEPROM_ADDR, "userData overlaps the DAC EEPROM block");

bool dacFound = false;
bool dacApplyPending = false;
bool dacSavePending = false;
unsigned long dacChangeTimer = 0;
unsigned long dacMqttLostSince = 0;
bool dacFailsafeActive = false;

const char adValueDac[] PROGMEM = R"rawliteral({"unique_id":"%s_dac%u","default_entity_id":"number.%s_dac%u","name":"Ausgang %u","command_topic":"%s/dac/%u/set","state_topic":"%s/dac/%u/state","min":0,"max":10,"step":0.1,"unit_of_measurement":"V","device_class":"voltage","mode":"slider","device":{"ids": ["%s"],"name":"%s","manufacturer": "MBusino","mdl":"V%s"},"availability_topic":"%s/status","payload_available":"online","payload_not_available":"offline","availability_mode":"all"})rawliteral";
const char adTopicDac[] PROGMEM = R"rawliteral(homeassistant/number/%s/dac%u/config)rawliteral";

#if defined(ESP32)
static bool dacWrite(uint8_t reg, uint8_t b1, uint8_t b2, bool two){
  Wire.beginTransmission(DAC_I2C_ADDR);
  Wire.write(reg);
  Wire.write(b1);
  if(two) Wire.write(b2);
  return Wire.endTransmission() == 0;
}

static void dacOutput(uint8_t ch, uint16_t mv){
  if(!dacFound) return;
  uint16_t raw = ((uint32_t)mv * 4095) / 10000;  // 12 bit
  raw <<= 4;                                      // data is left aligned in 16 bit
  dacWrite(ch == 0 ? 0x02 : 0x04, raw & 0xFF, raw >> 8, true);
}

// probe the bus; on success set the 0-10V range and restore the last output
static void dacProbe(){
  Wire.beginTransmission(DAC_I2C_ADDR);
  if(Wire.endTransmission() != 0) return;
  dacFound = true;
  dacWrite(0x01, 0x11, 0, false);  // output range 0-10V
  dacOutput(0, dacCfg.mv[0]);
  dacOutput(1, dacCfg.mv[1]);
}

void dacBegin(){
  EEPROM.get(DAC_EEPROM_ADDR, dacCfg);
  if(dacCfg.magic != DAC_MAGIC){
    dacCfg = {DAC_MAGIC, {0, 0}, 0, 0};
  }
  if(dacCfg.mv[0] > 10000) dacCfg.mv[0] = 0;
  if(dacCfg.mv[1] > 10000) dacCfg.mv[1] = 0;
  if(userData.extension == 7) return;
  Wire.begin(DAC_SDA, DAC_SCL);
  dacProbe();
  Serial.print("DAC (0x58) found: ");
  Serial.println(dacFound);
}
#else
void dacBegin(){}
static void dacProbe(){}
static void dacOutput(uint8_t, uint16_t){}
#endif

// request a new output, applied from the main loop (also called from web/MQTT context)
void dacSetMv(uint8_t ch, uint16_t mv){
  if(ch > 1 || !dacFound) return;
  if(mv > 10000) mv = 10000;
  dacCfg.mv[ch] = mv;
  dacApplyPending = true;
  dacSavePending = true;
  dacFailsafeActive = false;
  dacChangeTimer = millis();
}

void dacPublishState(){
  if(!dacFound) return;
  for(uint8_t ch = 0; ch < 2; ch++){
    client.publish(String(String(userData.mbusinoName) + "/dac/" + String(ch+1) + "/state").c_str(), String(dacCfg.mv[ch] / 1000.0, 2).c_str(), true);
  }
}

// called on every MQTT (re)connect, announces entities and current state
void dacPublishAll(){
  if(!dacFound) return;
  if(userData.haAutodisc == true){
    for(uint8_t ch = 1; ch <= 2; ch++){
      sprintf(adVariables.bufferValue, adValueDac, userData.mbusinoName, ch, userData.mbusinoName, ch, ch, userData.mbusinoName, ch, userData.mbusinoName, ch, userData.mbusinoName, userData.mbusinoName, MBUSINO_VERSION, userData.mbusinoName);
      sprintf(adVariables.bufferTopic, adTopicDac, userData.mbusinoName, ch);
      client.publish(adVariables.bufferTopic, adVariables.bufferValue, true);
    }
    adVariables.bufferTopic[0] = 0;
    adVariables.bufferValue[0] = 0;
  }
  dacPublishState();
  client.subscribe(String(String(userData.mbusinoName) + "/dac/1/set").c_str());
  client.subscribe(String(String(userData.mbusinoName) + "/dac/2/set").c_str());
}

// MQTT payload (volts, e.g. "5.5") -> mV
void dacHandleCommand(uint8_t ch, byte* payload, unsigned int length){
  char buf[16] = {0};
  if(length >= sizeof(buf)) length = sizeof(buf) - 1;
  memcpy(buf, payload, length);
  char* end = NULL;
  float v = strtof(buf, &end);
  if(end == buf) return;  // not a number, ignore instead of switching to 0 V
  if(v < 0) v = 0;
  dacSetMv(ch, (uint16_t)(v * 1000.0 + 0.5));
}

void dacPublishAll();

void dacLoop(){
  static unsigned long lastProbe = 0;
  if(!dacFound){
    // chip not there at boot (e.g. wired later): look again every 30 s
    if(userData.extension != 7 && (millis() - lastProbe) > 30000){
      lastProbe = millis();
      dacProbe();
      if(dacFound && client.connected()) dacPublishAll();
    }
    return;
  }
  if(dacApplyPending){
    dacApplyPending = false;
    dacOutput(0, dacCfg.mv[0]);
    dacOutput(1, dacCfg.mv[1]);
    if(client.connected()) dacPublishState();
  }
  // store at most once, 5 s after the last change (EEPROM wear)
  if(dacSavePending && (millis() - dacChangeTimer) > 5000){
    dacSavePending = false;
    EEPROM.put(DAC_EEPROM_ADDR, dacCfg);
    EEPROM.commit();
  }
  // fail-safe when MQTT is gone for too long
  if(client.connected()){
    dacMqttLostSince = 0;
    if(dacFailsafeActive){  // back online: restore the last commanded output
      dacFailsafeActive = false;
      dacApplyPending = true;
    }
  }
  else{
    if(dacMqttLostSince == 0) dacMqttLostSince = millis();
    if(dacCfg.failsafeMin > 0 && !dacFailsafeActive && (millis() - dacMqttLostSince) > (unsigned long)dacCfg.failsafeMin * 60000UL){
      dacFailsafeActive = true;
      dacOutput(0, dacCfg.failsafeMv);   // not stored, last commanded value returns after reconnect
      dacOutput(1, dacCfg.failsafeMv);
    }
  }
}
