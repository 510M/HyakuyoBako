extern "C" {
#include <user_interface.h>
};
#include <ESP8266WiFi.h>
#include <time.h>
#include <Wire.h> //AM2321

#include "structs.h"
#include "define.h" // Git管理対象外とする！

#define JST 3600* 9
#define  MAX_COUNT 10
#define  USER_DATA_ADDR  65  // uint32_t => 4バイトの符号なし整数
  
WiFiClientSecure client;

void setup() {
  
  Serial.begin(115200);
  for (int i = 0; i < 10; i++) {
    // シリアルポートが開くまで（5秒まで）待機
    delay(500);
  }
  Serial.println("");
  Serial.println("");
  Serial.println("### START ###");
  Serial.println("");
  
  bool first;

  //External System
  //Deep-Sleep Wake
  Serial.println("    reset reason: " + ESP.getResetReason());
  if (ESP.getResetReason() == "Deep-Sleep Wake") {
    first = false;
  } else {
    first = true;
  }

  if (first) {
    // RTCメモリを初期化
    Serial.print("    rtc_initialization-1: ");
    if(rtcInit(&hyakuyo)) {
      Serial.println("success");
    } else {
      Serial.println("faild");   
    }  
  } else {
    if (system_rtc_mem_read(USER_DATA_ADDR, &hyakuyo, sizeof(hyakuyo))) {
      Serial.println("    rtc_mem_read: success");
      Serial.println("    rtc_mem_hash: " + String(hyakuyo.hash, HEX));
      
      if(hyakuyo.hash != calc_fnv(hyakuyo)){
        // hashが合っていなければ不整合(初期化済み)
        Serial.println("    rtc_mem_hash_unmatched");
        hyakuyo.cnt = 0;
      } else {
        Serial.println("    rtc_mem_count: " + String(hyakuyo.cnt));
        hyakuyo.cnt++;
      }
    } else {
      Serial.println("    rtc_mem_read: faild");
      hyakuyo.cnt = 0;
    }
  }

  Serial.println("    count: " + String(hyakuyo.cnt));
  
  // WiFi設定
  WiFi.setOutputPower(0); // 低出力に（節電！）20.5dBm(最大)から0.0dBm(最小)までの値
  WiFi.mode(WIFI_STA);
  WiFi.config(IPAddress(LOCAL), IPAddress(GATAWAY), IPAddress(SUBNET), IPAddress(DNS));

  // Wi-Fi接続
  WiFi.begin(SSID, PWD);
  Serial.print("    wifi_connecting: ");
  while (WiFi.status() != WL_CONNECTED) { // Wi-Fi AP接続待ち
    delay(500);
    Serial.print("*");
  }
  Serial.println("");
  
  // NTP設定
  configTime(JST, 0, NTP1, NTP2);

  time_t t;
  timeval now;
  struct tm *tm;
  
  Serial.print("    ntp_synchronizing: ");
  for (int i = 0; i < 10; i++) {
    // NTP同期完了まで待機（5秒まで）
    delay(500);
    Serial.print("*");
    
    t = time(NULL);
    tm = localtime(&t);
    if (tm->tm_year + 1900 >= 2016) {
      gettimeofday(&now, NULL);
      hyakuyo.data[hyakuyo.cnt].epoch.tv_sec = t;
      hyakuyo.data[hyakuyo.cnt].epoch.tv_usec = now.tv_usec;
      break;
    }
  }
  Serial.println("");

  byte rdptr[20];
  readAM2321(rdptr, 8);

  char b[100];
  sprintf(b, "    ic2: code %02x, size %02x, crc %02x%02x", rdptr[0], rdptr[1], rdptr[7], rdptr[6]);
  Serial.println(b);

  if(crc16(rdptr, 8) == 0) {
    // CRC OK
    Serial.println("    ic2_crc_check: success");
    hyakuyo.data[hyakuyo.cnt].crc = true;
    
    if (rdptr[4] < B10000000) {
      hyakuyo.data[hyakuyo.cnt].temp = (float)(rdptr[4] * 256 + rdptr[5]) / 10.0;  // -40.0 to 80.0
    } else {
      // マイナス温度対策 ADD A_GOTO
      // 最上位ビット分引いてマイナスをつける
      rdptr[4] -= B10000000;
      hyakuyo.data[hyakuyo.cnt].temp = (float) - (rdptr[4] * 256 + rdptr[5]) / 10.0; // -40.0 to 80.0
    }
    hyakuyo.data[hyakuyo.cnt].humid = (float)(rdptr[2] * 256 + rdptr[3]) / 10.0;  // -40.0 to 80.0
 
  } else {
    // CRC NG
    Serial.println("    ic2_crc_check: faild");
    hyakuyo.data[hyakuyo.cnt].crc = false;
    // とりま0をセット
    hyakuyo.data[hyakuyo.cnt].temp = 0;
    hyakuyo.data[hyakuyo.cnt].humid = 0;
  }

  hyakuyo.data[hyakuyo.cnt].lum = 1024 - analogRead(0);                     // 0 to 1024 (ESP8266)

  sprintf(b, "    am2321: temp %.1f°C, humid %.1f%RH, lum %d"
              ,hyakuyo.data[hyakuyo.cnt].temp
              ,hyakuyo.data[hyakuyo.cnt].humid
              ,hyakuyo.data[hyakuyo.cnt].lum);
  Serial.println(b);
  
  // ここでhash計算予定
  hyakuyo.hash = calc_fnv(hyakuyo);
  Serial.println("    new_hash: " + String(hyakuyo.hash, HEX));
  
  if (system_rtc_mem_write(USER_DATA_ADDR, &hyakuyo, sizeof(hyakuyo))) {
    Serial.println("    rtc_mem_write: success");
  } else {
    Serial.println("    rtc_mem_write: failed");
  }

  if(hyakuyo.cnt >= MAX_COUNT-1) {
    // 10回目終了時、今までのデータをRTCメモリから全て表示してみる

    char cstr[1000];
    hyakuyoJSON(hyakuyo, cstr);
  
    String url = "/hyakuyobako/receive.php";
    url += "?data=" + URLEncode(cstr);

      Serial.println(url);

      // WiFiClient client;
      if (client.connect(HOST, PORT)) {
        Serial.println("    connect_to_host: success");
        client.print(String("GET ") + url + " HTTP/1.1\r\n" +
                     "Host: " + HOST + "\r\n" +
                     "User-Agent: ESP8266\r\n" +
                     "Pragma: no-cache\r\n" +
                     "Connection: close\r\n\r\n");
                     
        unsigned long timeout = millis();
        while (client.available() == 0) {
          if (millis() - timeout > 5000) {
            Serial.println("    host_response: timeout");
            client.stop();
            return;
          }
        }
      
        while (client.connected()) {
          String line = client.readStringUntil('\n');
          if (line == "\r") {
            Serial.println("    host_reply: received");
            break;
          }
        }
      
        String line = client.readStringUntil('\n');
        if (line.startsWith("{\"state\":\"success\"")) {
          Serial.println("    host_status: success");

          // 送信がうまくいったらRTCメモリを初期化
          Serial.print("    rtc_initialization-2: ");
          if(rtcInit(&hyakuyo)) {
            Serial.println("success");
          } else {
            Serial.println("faild");   
          }  
        } else {
          Serial.println("    host_status: failed");
        }
      } else {
        Serial.println("    connect_to_host: failed");
      }
  }
    
  // これ無しで通信を行わずに終了すると
  // 2回目以降接続に失敗してしまった
  client.stop();
  delay(20);
  
  WiFi.mode(WIFI_OFF);
  delay(20);
  WiFi.forceSleepBegin();
  
  Serial.println("");
  Serial.println("### SLEEP ###");
  Serial.println("");
  delay(3000);
  Serial.end();
  delay(3000);
  ESP.deepSleep(1e6 * 15, WAKE_RF_DEFAULT); // sleep 15 seconds
  delay(1000);
}

void loop() {
}


