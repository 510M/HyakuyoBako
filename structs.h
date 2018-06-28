struct Data {
  timeval epoch;      // 時間
  bool crc;           // チェックサム結果
  float temp;         // 気温
  float humid;        // 湿度
  unsigned short lum; // 明るさ
};
struct Hyakuyo{
  unsigned long int hash;
  unsigned short cnt;
  struct Data data[20];
};
struct Hyakuyo hyakuyo;
