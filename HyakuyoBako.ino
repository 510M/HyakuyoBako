extern "C" {
#include <user_interface.h>
};
#include <ESP8266WiFi.h>
#include <time.h>
#include "define.h" // Git管理対象外とする！

//AM2321
#include <Wire.h>

#define JST   3600*9

WiFiClientSecure client;

void setup() {


  Serial.begin(115200);
  delay(500);
  bool first;

  //External System
  //Deep-Sleep Wake

  if (ESP.getResetReason() == "Deep-Sleep Wake") {
    first = false;
  } else {
    first = true;
  }

  //String resetReason = ESP.getResetReason();
  //Serial.println("\n********** " + resetReason + " **********");

  static const uint32_t USER_DATA_ADDR = 66; // uint32_t => 4バイトの符号なし整数
  struct {
    // haykuyo_data
    uint32_t hash;
    uint16_t count;
    uint8_t  send;
    uint16_t etc2;
  } haykuyo_data;

  if (first) {
    haykuyo_data.count = 1;
  } else {
    if (system_rtc_mem_read(USER_DATA_ADDR, &haykuyo_data, sizeof(haykuyo_data))) {
      //Serial.println("system_rtc_mem_read success");

    } else {
      Serial.println("<<< system_rtc_mem_read faild>>>");
    }
    if (haykuyo_data.count == 10) {
      haykuyo_data.count = 1;
    } else {
      haykuyo_data.count++;
    }
  }
  Serial.println("\n<<< " + String(haykuyo_data.count) + " >>>");
      
  if (system_rtc_mem_write(USER_DATA_ADDR, &haykuyo_data, sizeof(haykuyo_data))) {
    Serial.println("system_rtc_mem_write success");
  } else {
    Serial.println("system_rtc_mem_write failed");
  }




  
  Wire.begin();



  // WiFi設定
  WiFi.setOutputPower(0); // 低出力に（節電！）20.5dBm(最大)から0.0dBm(最小)までの値
  WiFi.mode(WIFI_STA);
  WiFi.config(IPAddress(LOCAL), IPAddress(GATAWAY), IPAddress(SUBNET), IPAddress(DNS));


  // Wi-Fi接続
  WiFi.begin(SSID, PWD);

  while (WiFi.status() != WL_CONNECTED) { // Wi-Fi AP接続待ち
    delay(500);
    Serial.print(".");
  }

  // WiFiClient client;
  if (!client.connect(HOST, PORT)) {
    Serial.println("connection failed");
    return;
  }

  // NTP設定
  configTime(JST, 0, NTP1, NTP2);
  delay(500);


  time_t t;
  struct tm *tm;
  static const char *wd[7] = {"Sun", "Mon", "Tue", "Wed", "Thr", "Fri", "Sat"};

  t = time(NULL);
  tm = localtime(&t);
  Serial.println(String(t));
  Serial.printf("%04d/%02d/%02d(%s) %02d:%02d:%02d\n",
                tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                wd[tm->tm_wday],
                tm->tm_hour, tm->tm_min, tm->tm_sec);

  // ISO 8601 日本標準時(JST)

  //String D = String(tm->tm_year+1900) + "-" + String(tm->tm_mon+1) + "-" + String(tm->tm_mday);
  //D += "T" + String(tm->tm_hour) + ":" + String(tm->tm_min) + ":" + String(tm->tm_sec) + "%2B09:00";
  long E = t;
  char D[28]; // 27文字（2018-06-21T02:30:26%2B09:00）＋ 末尾のNULL(\0)
  sprintf(D, "%04d-%02d-%02dT%02d:%02d:%02d%%2B09:00"
          , tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);

  // RTCメモリに最小限に記録することを考える
  // 1529593225,24.6,42.3,1024
  // epoch: 10バイト（UNIXTIME）
  // temp: 5バイト（-40.0 to 80.0）
  // humid: 4バイト（0 to 99.9）
  // lum: 4バイト（0 to 1024）
  
  // 合計 23バイト
  
  // 一応バッファを25(24+1) x 20回分とする
  // 10回（10分）毎に送信し、エラー時再試行+10回（10分まで）可能とする

  // センサーの初期化
  byte rdptr[20];
  readAM2321(rdptr, 8);

  //Serial.print("\n機能コード。3になってる？＞" + String(rdptr[0], HEX) + "\n");
  //Serial.print("\nバイト数。4になってる？＞"+ String(rdptr[1], HEX) + "\n");
  //Serial.print("\nCRC ＞"+ String(rdptr[7], HEX) + String(rdptr[6], HEX) + "\n");
  char b[100];
  sprintf(b, "\n機能コード:%02x, バイト数:%02x, CRC:%02x%02x\n", rdptr[0], rdptr[1], rdptr[7], rdptr[6]);
  Serial.print(b);
  // 排他的論理和＝どちらか一方が１のときのみ１ 両方１や両方０は０
  sprintf(b, "\n%02x,%02x,%02x,%02x,%02x,%02x,%02x,%02x\n", rdptr[0], rdptr[1], rdptr[2], rdptr[3], rdptr[40], rdptr[5], rdptr[6], rdptr[7]);
  Serial.print(b);


  sprintf(b, "\n計算:%04x\n", crc16(rdptr, 8));
  Serial.print(b);

  // 65535はエラー？
  float T;
  
  if(rdptr[4] < B10000000) {
    T = (float)(rdptr[4] * 256 + rdptr[5]) / 10.0;  // -40.0 to 80.0
  } else {
    // マイナス温度対策 ADD A_GOTO
    // 最上位ビット分引いてマイナスをつける
    rdptr[4] -= B10000000;
    T = (float) - (rdptr[4] * 256 + rdptr[5]) / 10.0; // -40.0 to 80.0
  }

  float H = (float)(rdptr[2] * 256 + rdptr[3]) / 10.0;  // -40.0 to 80.0
  int L = analogRead(0);                     // 0 to 1024 (ESP8266)
  
  Serial.print(T, 1);
  Serial.print("°C");
  Serial.print("\t");
  Serial.print(H, 1);
  Serial.print("%RH");
  Serial.print("\t");
  Serial.println(L, DEC);
  
  t = time(NULL);
  
  /*
  tm = localtime(&t);
  Serial.println(String(t));
  Serial.printf("%04d/%02d/%02d(%s) %02d:%02d:%02d\n",
                tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                wd[tm->tm_wday],
                tm->tm_hour, tm->tm_min, tm->tm_sec);

  char S[28]; // 27文字（2018-06-21T02:30:26%2B09:00）＋ 末尾のNULL(\0)
  sprintf(S, "%04d-%02d-%02dT%02d:%02d:%02d%%2B09:00"
          , tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
  */
  /*
    String data = "{";
    data += "\"send\":\"" + String(S) + "\",";
    data += "\"data\":[{";
  
    data += "\"id\":\"" + String(1) + "\",";
    data += "\"iso8601\":\"" + String(D) + "\",";
    data += "\"epoch\":\"" + String(E) + "\",";
    data += "\"temp\":\"" + M + String(T) + "\",";
    data += "\"humid\":\"" + String(H) + "\",";
    data += "\"lum\":\"" + String(L) + "\",";
  
    data += "}]";
    data += "}";
  */

  // いったんRTCメモリに保存することを想定したデータを作成してみる
  char data[25];
  sprintf(data, "%02d%10d%5.1f%4.1f%4d", haykuyo_data.count, E, T, H, L);
  
  String url = "/hyakuyobako/receive.php";
  //url += "?data=" + String(data);

  
  url += "?data=" + URLEncode(data);

  Serial.println(url);
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + HOST + "\r\n" +
               "User-Agent: ESP8266\r\n" +
               "Pragma: no-cache\r\n" +
               "Connection: close\r\n\r\n");
               
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println(">>> Client Timeout !");
      client.stop();
      return;
    }
  }

  // Read all the lines of the reply from server and print them to Serial
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      Serial.println("headers received");
      break;
    }
  }

  String line = client.readStringUntil('\n');
  if (line.startsWith("{\"state\":\"success\"")) {
    Serial.println("esp8266/Arduino CI successfull!");
    Serial.println("#####################################");
    Serial.println(line);
    Serial.println("#####################################");

  } else {
    Serial.println("esp8266/Arduino CI has failed");
  }

  // Ambientの初期化
  // センサー値の取得
  // Ambientへの送信
  
  WiFi.mode(WIFI_OFF);
  delay(20);
  WiFi.forceSleepBegin();

  Serial.println("SLEEP");
  delay(20);
  ESP.deepSleep(1e6 * 15, WAKE_RF_DEFAULT); // sleep 15 seconds
  delay(1000);
}

void loop() {

}
void readAM2321(byte *rdptr, byte length ) {
  int i;
  byte  deviceaddress = 0x5C;
  //step1
  Wire.beginTransmission(deviceaddress);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(1);
  //step2
  Wire.beginTransmission(deviceaddress);
  Wire.write(0x03);
  Wire.write(0x00);
  Wire.write(0x04);
  Wire.endTransmission();
  delay(2);
  //step3
  Wire.requestFrom(deviceaddress, length);
  delayMicroseconds(60);
  if (length <= Wire.available())
  {
    for (i = 0; i < length; i++)
    {
      rdptr[i] = Wire.read();
    }
  }
}

String URLEncode(const char* msg) {
  const char *hex = "0123456789abcdef";
  String encodedMsg = "";


  while (*msg != '\0') {
    if ( ('a' <= *msg && *msg <= 'z')
         || ('A' <= *msg && *msg <= 'Z')
         || ('0' <= *msg && *msg <= '9') ) {
      encodedMsg += *msg;
    } else {
      encodedMsg += '%';
      encodedMsg += hex[*msg >> 4];
      encodedMsg += hex[*msg & 15];
    }
    msg++;
  }
  return encodedMsg;
}

// AM2321 Product Manualより

unsigned short crc16(unsigned char *ptr, unsigned char len)
{
  unsigned short crc = 0xFFFF;
  unsigned char i;
  while (len--)
  {

    
    crc ^= *ptr++;
    for (i = 0; i < 8; i++)
    {


      
      if (crc & 0x01)
      {
        crc >>= 1;
        crc ^= 0xA001;
      } else
      {
        crc >>= 1;
      }
      Serial.println(String(crc, HEX));
    }
  }

  return crc;
}
