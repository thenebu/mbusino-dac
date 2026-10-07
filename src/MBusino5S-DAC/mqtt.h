PubSubClient client(espClient);

// connection diagnostics, shown at /status
unsigned long mqttAttempts = 0;
unsigned long mqttLastAttempt = 0;
int mqttLastState = 0;

void dacPublishAll();
void dacHandleCommand(uint8_t ch, byte* payload, unsigned int length);

void reconnect() {
  // Loop until we're reconnected
  char lwBuffer[40] = {0};
  snprintf(lwBuffer, sizeof(lwBuffer), "%s/status", userData.mbusinoName);
  mqttAttempts++;
  mqttLastAttempt = millis();
  bool mqttOk = client.connect(userData.mbusinoName,userData.mqttUser,userData.mqttPswrd,lwBuffer,1,true,"offline");
  mqttLastState = client.state();
  if (mqttOk) {
    // Once connected, publish an announcement...
    #if defined(ESP32)
    Serial.println("MQTT connected to server");
    #endif    
    conCounter++;
    if(conCounter == 1){
      client.publish(String(String(userData.mbusinoName) + "/start").c_str(), "Bin hochgefahren, WLAN und MQTT steht");
    }
    else{
      client.publish(String(String(userData.mbusinoName) + "/reconnect").c_str(), "Online again!");
      adMbusMessageCounter = 2;
      adSensorMessageCounter = 2;
    }
    // publish online status for HA availability (retained)
    client.publish(lwBuffer, "online", true);
    // ... and resubscribe
    client.subscribe(String(String(userData.mbusinoName) + "/calibrateAverage").c_str());
    client.subscribe(String(String(userData.mbusinoName) + "/calibrateSensor").c_str());
    client.subscribe(String(String(userData.mbusinoName) + "/calibrateValue").c_str());
    client.subscribe(String(String(userData.mbusinoName) + "/calibrateBME").c_str());
    client.subscribe(String(String(userData.mbusinoName) + "/calibrateSet0").c_str());
    client.subscribe(String(String(userData.mbusinoName) + "/mbusPolling").c_str());    
    dacPublishAll();
  }
  else{
    #if defined(ESP32)
    Serial.print("MQTT unable to connect server, state ");
    Serial.println(client.state()); // -2 no TCP, 2 bad client id, 4 bad credentials, 5 not authorized
    #endif
    pulseInterval = 400;
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  if(userData.extension > 0){
    if (strcmp(topic,String(String(userData.mbusinoName) + "/calibrateAverage").c_str())==0){  
      calibrationAverage();
    }
    if (strcmp(topic,String(String(userData.mbusinoName) + "/calibrateSensor").c_str())==0){  
      calibrationSensor(atoi((char*)payload)-1);
    }
    if (strcmp(topic,String(String(userData.mbusinoName) + "/calibrateValue").c_str())==0){  
      calibrationValue(atof((char*)payload));
    }  
    if(userData.extension == 5){
      if (strcmp(topic,String(String(userData.mbusinoName) + "/calibrateBME").c_str())==0){  
        calibrationBME();
      }
    }
    if (strcmp(topic,String(String(userData.mbusinoName) + "/calibrateSet0").c_str())==0){  
      calibrationSet0();
    } 
  }
  if (strcmp(topic,String(String(userData.mbusinoName) + "/dac/1/set").c_str())==0){
    dacHandleCommand(0, payload, length);
  }
  if (strcmp(topic,String(String(userData.mbusinoName) + "/dac/2/set").c_str())==0){
    dacHandleCommand(1, payload, length);
  }
  if (strcmp(topic,String(String(userData.mbusinoName) + "/mbusPolling").c_str())==0){  
    pollingAddress = atoi((char*)payload);
    polling = true;
  }   
}   