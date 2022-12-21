#include "esp_camera.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <HardwareSerial.h> 
HardwareSerial MySerial(1); // 将串口映射到其他引脚使用

#define CAMERA_MODEL_AI_THINKER // Has PSRAM
#include "camera_pins.h" // 这个头文件的位置必须在设置摄像头的define的下方

// 设置MQTT borker信息
const char *mqtt_broker = "broker-cn.emqx.io";
const char *topic = "python_mqtt";
const char *mqtt_username = "emqx";
const char *mqtt_password = "public";
const int mqtt_port = 1883;
WiFiClient espClient;
PubSubClient client(espClient);

// 打印wifi信息的函数
void printWifiMessage() {
  Serial.println("");
  Serial.println("WIFI SmartConfig Success");
  Serial.printf("SSID:%s", WiFi.SSID().c_str());
  Serial.printf(", PSW:%s\r\n", WiFi.psk().c_str());
  Serial.print("LocalIP:");
  Serial.print(WiFi.localIP());
  Serial.print(" ,GateIP:");
  Serial.println(WiFi.gatewayIP());
}

// smartconfig配网程序
void smartconfig() {
  if (WiFi.status() != WL_CONNECTED) // 如果没有网络连接
  {
    WiFi.mode(WIFI_STA); // 设置STA网络模式
    WiFi.beginSmartConfig();
    Serial.println("WIFI have begin Smartconfig");
    int count_smartconfig = 0;
    while (count_smartconfig < 20) {
      delay(1000);
      count_smartconfig += 1;
      Serial.println(".");
      if (WiFi.smartConfigDone()) // 如果自动配网完成
      {
        Serial.println("success");
        WiFi.setAutoConnect(true);  // 设置自动连接
        // ?:如何将网络配置信息自动保存
        printWifiMessage();
        count_smartconfig = 0;
        break;
      }
    }
    if (count_smartconfig >= 20) {
      Serial.println("Smartconfig no success");
    }
  }
  else {
    printWifiMessage();
  }
}

// mqtt服务器连接函数
void mqttServer() {
  client.setServer(mqtt_broker, mqtt_port);
  client.setCallback(callback);
  if (WiFi.status() == WL_CONNECTED)
  {
    while (!client.connected()) {
      String client_id = "esp32-client-";
      client_id += String(WiFi.macAddress());
      Serial.printf("The client %s connects to the public mqtt broker\n", client_id.c_str());
      if (client.connect(client_id.c_str(), mqtt_username, mqtt_password)) {
        Serial.println("Public emqx mqtt broker connected");
      } else {
        Serial.print("failed with state ");
        Serial.print(client.state());
        delay(2000);
      }
    }
    // 连接成功后发送指定信息：publish and subscribe
    client.publish(topic, "Hi EMQX I'm ESP32 ^^");
    client.subscribe(topic);
  }
  else {
    smartconfig();
  }
}

// 怀疑是解算所消耗的时间过长所导致的系统卡顿
void hexToNum(String charPayload){
  int len = charPayload.length()/4; // 4位字符对应1个数字
  String str_num = charPayload.substring(0, 4);
  int num_list[len];
  for(int i=0; i<len; i++)
  {
    String str_num = charPayload.substring(0, 4);
    num_list[i] = strtol(str_num.c_str(), NULL, 16); // c_str()将string类型转为const str *类型
    charPayload = charPayload.substring(4, charPayload.length());
  }

  // 向STM32发送数据
  unsigned char toSTM32[5] = {126, char(num_list[0]), char(num_list[1]), char(num_list[0]+num_list[1]), 127};
  for(int i=0; i<5; i++){
    Serial.print(toSTM32[i]+" ");
  }
  Serial.println("");
  MySerial.write(toSTM32, 5);
}

//  监听到信息的回调函数
void callback(char *topic, byte *payload, unsigned int length) {
  Serial.print("Message arrived in topic: ");
  Serial.println(topic);
  Serial.print("Message:");
  String charPayload;
  for (int i = 0; i < length; i++) {
    charPayload += (char) payload[i];
  }
  Serial.println(charPayload);
  hexToNum(charPayload);
  Serial.println("-----------------------");
}

void startCameraServer();

void setup() {
  //相机闪光灯
  //  pinMode(4, OUTPUT);
  //  digitalWrite(4, HIGH);
  
  Serial.begin(115200);
  MySerial.begin(115200, SERIAL_8N1, 12, 13); //RX TX
  Serial.setDebugOutput(true);
  Serial.println();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  
  // if PSRAM IC present, init with UXGA resolution and higher JPEG quality
  //                      for larger pre-allocated frame buffer.
  if(psramFound()){
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1); // flip it back
    s->set_brightness(s, 1); // up the brightness just a bit
    s->set_saturation(s, -2); // lower the saturation
  }
  // drop down frame size for higher initial frame rate
  s->set_framesize(s, FRAMESIZE_QVGA);

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

//  WiFi.begin(ssid, password);
//
//  while (WiFi.status() != WL_CONNECTED) {
//    delay(500);
//    Serial.print(".");
//  }
//  Serial.println("");
//  Serial.println("WiFi connected");
  
  smartconfig(); // 通过smartcofig实现自动配网
  mqttServer();
  startCameraServer();

  if(WiFi.status() == WL_CONNECTED){
    Serial.print("Camera Ready! Use 'http://");
    Serial.print(WiFi.localIP());
    Serial.println("' to connect");
  }
  else{
    Serial.println("网络连接失败，通过reset重新尝试");
  }
}

void loop() {
  client.loop();
}
