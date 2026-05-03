#include <Arduino.h>
#include <BH1750.h>
#include <LittleFS.h>
#include <NOxGasIndexAlgorithm.h>
#include <SensirionI2CSgp41.h>
#include <SensirionI2cScd4x.h>
#include <SensirionI2cSht4x.h>
#include <SPI.h>
#include <VOCGasIndexAlgorithm.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <math.h>
#include <time.h>

#ifndef WIFI_STA_SSID
#define WIFI_STA_SSID ""
#endif

#ifndef WIFI_STA_PASS
#define WIFI_STA_PASS ""
#endif

namespace {

constexpr char kApSsid[] = "ESP32S3M-ENV";
constexpr char kStaSsid[] = WIFI_STA_SSID;
constexpr char kStaPass[] = WIFI_STA_PASS;

constexpr int kI2cSda = 4;
constexpr int kI2cScl = 5;
constexpr uint32_t kI2cClockHz = 400000;

constexpr gpio_num_t kLcdMosi = GPIO_NUM_11;
constexpr gpio_num_t kLcdSclk = GPIO_NUM_12;
constexpr gpio_num_t kLcdMiso = GPIO_NUM_13;
constexpr gpio_num_t kLcdDc = GPIO_NUM_40;
constexpr gpio_num_t kLcdCs = GPIO_NUM_39;
constexpr gpio_num_t kLcdRst = GPIO_NUM_38;
constexpr gpio_num_t kLcdBl = GPIO_NUM_41;
constexpr gpio_num_t kBootKey = GPIO_NUM_0;
constexpr uint8_t kLcdBlPwmChannel = 0;
constexpr uint32_t kLcdBlPwmHz = 5000;
constexpr uint8_t kLcdBlPwmBits = 8;

constexpr uint16_t kLcdWidth = 160;
constexpr uint16_t kLcdHeight = 80;
constexpr unsigned long kDisplayIntervalMs = 1000;
constexpr unsigned long kSerialIntervalMs = 5000;
constexpr uint16_t kSgpConditioningSeconds = 10;
constexpr uint8_t kDisplayPageCount = 4;
constexpr size_t kHistoryCapacity = 180;
constexpr unsigned long kWifiReconnectIntervalMs = 30000;
constexpr uint32_t kMinuteRingMagic = 0x31474E45UL;  // ENV1
constexpr uint32_t kMinuteRingVersion = 2;
constexpr uint32_t kMinuteRingSegmentRecords = 105;
constexpr uint32_t kMinuteRingSegments = 96;
constexpr uint32_t kMinuteRingCapacity = kMinuteRingSegmentRecords * kMinuteRingSegments;  // 7 days at 1 point/minute
constexpr char kMinuteRingPath[] = "/env_min.hdr";
constexpr char kLegacyMinuteRingPath[] = "/env_min.ring";
constexpr char kConfigPath[] = "/env_config.txt";
constexpr char kConfigTmpPath[] = "/env_config.tmp";
constexpr char kTimeZone[] = "CST-8";
constexpr char kNtpServer1[] = "pool.ntp.org";
constexpr char kNtpServer2[] = "time.nist.gov";
constexpr char kNtpServer3[] = "ntp.aliyun.com";
constexpr uint32_t kTimeValidEpoch = 1700000000UL;
constexpr uint32_t kEpochMinuteFloor = kTimeValidEpoch / 60UL;
constexpr unsigned long kNtpRetryMs = 60000UL;
constexpr unsigned long kNtpRefreshMs = 12UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kDefaultWebBoostMs = 180000UL;
constexpr uint32_t kMaxWebBoostMs = 600000UL;
constexpr uint32_t kButtonLongPressMs = 850UL;

enum PowerMode : uint8_t {
  POWER_LOW = 0,
  POWER_BALANCED = 1,
  POWER_PERFORMANCE = 2,
};

enum ButtonEvent : uint8_t {
  BUTTON_NONE = 0,
  BUTTON_SHORT = 1,
  BUTTON_LONG = 2,
};

enum MenuItem : uint8_t {
  MENU_POWER = 0,
  MENU_LCD_BRIGHTNESS,
  MENU_BACKLIGHT_TIMEOUT,
  MENU_WIFI_SLEEP,
  MENU_SHT_PRECISION,
  MENU_BH_MODE,
  MENU_WEB_BOOST,
  MENU_SAVE_EXIT,
  MENU_COUNT,
};

constexpr uint16_t RGB565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

constexpr uint16_t kBlack = RGB565(0, 0, 0);
constexpr uint16_t kWhite = RGB565(255, 255, 255);
constexpr uint16_t kRed = RGB565(235, 78, 78);
constexpr uint16_t kGreen = RGB565(52, 196, 126);
constexpr uint16_t kBlue = RGB565(32, 88, 180);
constexpr uint16_t kCyan = RGB565(64, 210, 235);
constexpr uint16_t kYellow = RGB565(245, 196, 70);
constexpr uint16_t kOrange = RGB565(245, 135, 55);
constexpr uint16_t kPanel = RGB565(12, 22, 34);
constexpr uint16_t kGrid = RGB565(36, 58, 78);

WebServer server(80);
spi_device_handle_t gLcd = nullptr;
DRAM_ATTR uint16_t gLcdFrame[kLcdWidth * kLcdHeight];
bool gFsReady = false;
uint32_t gPersistedSamples = 0;
uint32_t gStorageFailures = 0;
uint32_t gLastSampleMs = 0;
uint32_t gWifiReconnects = 0;
uint32_t gLastWifiReconnectMs = 0;
uint32_t gLastLoopMs = 0;
uint32_t gLoopStallCount = 0;
uint32_t gMaxLoopGapMs = 0;
uint32_t gLastPersistDurationMs = 0;
uint32_t gMaxPersistDurationMs = 0;
uint32_t gDroppedInvalidSamples = 0;
uint32_t gDroppedUnsyncedSamples = 0;
uint32_t gDroppedTimeDomainSamples = 0;
uint32_t gLastFilteredMinuteRows = 0;
uint8_t gDisplayPage = 0;
bool gForceDisplayDraw = true;
bool gBacklightOn = true;
uint32_t gLastUserActivityMs = 0;
bool gTimeSyncStarted = false;
bool gTimeSynced = false;
uint32_t gLastTimeSyncAttemptMs = 0;
uint32_t gLastTimeSyncOkMs = 0;
time_t gLastSyncedEpoch = 0;
uint32_t gWebBoostUntilMs = 0;
bool gLastAppliedWebBoost = false;
bool gMenuMode = false;
bool gMenuEditing = false;
uint8_t gMenuIndex = 0;

void applyPowerRuntimeConfig();

SensirionI2cSht4x gSht4x;
SensirionI2cScd4x gScd4x;
SensirionI2CSgp41 gSgp41;
BH1750 gBh1750;
VOCGasIndexAlgorithm gVocAlgorithm;
NOxGasIndexAlgorithm gNoxAlgorithm;

struct EnvConfig {
  uint8_t powerMode = POWER_BALANCED;
  uint32_t fastIntervalMs = 1000;
  uint32_t scdIntervalMs = 5000;
  uint32_t logIntervalMs = 60000;
  uint32_t liveRefreshMs = 1000;
  uint8_t lcdBrightnessPct = 70;
  uint32_t backlightTimeoutMs = 60000;
  bool wifiStaSleep = true;
  uint32_t lowPowerSensorIntervalMs = 30000;
  uint16_t co2WarnPpm = 1000;
  uint16_t co2BadPpm = 1500;
  uint16_t vocWarn = 150;
  uint16_t vocBad = 250;
  uint16_t noxWarn = 10;
  uint16_t noxBad = 50;
  float tempLowC = 18.0f;
  float tempHighC = 28.0f;
  float humidityLow = 40.0f;
  float humidityHigh = 70.0f;
  float luxLow = 50.0f;
  float luxHigh = 1500.0f;
  uint8_t shtPrecision = 0;
  uint8_t bhMode = 0;
} cfg;

struct EnvState {
  bool shtOnline = false;
  bool scdOnline = false;
  bool sgpOnline = false;
  bool bhOnline = false;
  bool scdReady = false;
  uint8_t bhAddr = 0;

  float shtTempC = 0.0f;
  float shtHumidity = 0.0f;
  uint16_t scdCo2 = 0;
  float scdTempC = 0.0f;
  float scdHumidity = 0.0f;
  uint16_t srawVoc = 0;
  uint16_t srawNox = 0;
  int32_t vocIndex = 0;
  int32_t noxIndex = 0;
  uint16_t sgpConditioning = kSgpConditioningSeconds;
  float lux = 0.0f;

  uint32_t shtOk = 0;
  uint32_t shtFail = 0;
  uint32_t scdOk = 0;
  uint32_t scdFail = 0;
  uint32_t sgpOk = 0;
  uint32_t sgpFail = 0;
  uint32_t bhOk = 0;
  uint32_t bhFail = 0;

  int16_t shtInitErr = 0;
  int16_t scdInitErr = 0;
  uint16_t sgpInitErr = 0;
  uint16_t sgpSelfTest = 0;
} env;

struct SampleRow {
  uint32_t seq = 0;
  uint32_t uptimeMs = 0;
  uint16_t co2 = 0;
  float tempC = 0.0f;
  float humidity = 0.0f;
  int32_t voc = 0;
  int32_t nox = 0;
  float lux = 0.0f;
  bool ok = false;
};

struct MinuteAggregate {
  uint32_t minute = 0;
  uint32_t count = 0;
  double co2Sum = 0.0;
  double tempSum = 0.0;
  double humiditySum = 0.0;
  double vocSum = 0.0;
  double noxSum = 0.0;
  double luxSum = 0.0;
  uint32_t okCount = 0;
};

struct MinuteRingHeader {
  uint32_t magic = kMinuteRingMagic;
  uint32_t version = kMinuteRingVersion;
  uint32_t capacity = kMinuteRingCapacity;
  uint32_t writeIndex = 0;
  uint32_t count = 0;
  uint32_t totalWrites = 0;
};

struct MinuteRingRecord {
  uint32_t minute = 0;
  uint16_t count = 0;
  uint16_t okPermille = 0;
  float co2 = 0.0f;
  float tempC = 0.0f;
  float humidity = 0.0f;
  float voc = 0.0f;
  float nox = 0.0f;
  float lux = 0.0f;
};

SampleRow gHistory[kHistoryCapacity];
size_t gHistoryHead = 0;
size_t gHistoryCount = 0;
uint32_t gSampleSeq = 0;
MinuteAggregate gMinuteAgg;
MinuteRingHeader gMinuteRing;
bool gMinuteRingReady = false;

const char kIndexHtml[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>ESP32-S3M 环境监测站</title>
  <style>
    :root { color-scheme: dark; --bg:#081018; --panel:#111d28; --line:#263a4d; --text:#edf4fb; --muted:#91a6ba; --ok:#43c889; --warn:#f0c35a; --bad:#ff6f6f; --accent:#61b5ff; }
    * { box-sizing: border-box; }
    body { margin:0; font-family:Segoe UI,Arial,sans-serif; background:#081018; color:var(--text); }
    .wrap { max-width:980px; margin:0 auto; padding:22px 14px 36px; }
    .head { display:flex; justify-content:space-between; gap:12px; align-items:flex-end; flex-wrap:wrap; margin-bottom:14px; }
    h1 { margin:0; font-size:26px; letter-spacing:0; }
    .sub,.meta { color:var(--muted); font-size:13px; }
    .grid { display:grid; grid-template-columns:repeat(12,minmax(0,1fr)); gap:12px; }
    .card { grid-column:span 3; background:var(--panel); border:1px solid var(--line); border-radius:8px; padding:14px; min-height:116px; }
    .wide { grid-column:span 6; }
    .full { grid-column:span 12; }
    .label { color:var(--muted); font-size:13px; margin-bottom:10px; }
    .value { font-size:34px; line-height:1; font-weight:700; margin-bottom:8px; }
    .small { font-size:24px; }
    .ok { color:var(--ok); }
    .warn { color:var(--warn); }
    .bad { color:var(--bad); }
    .toolbar { display:flex; gap:8px; flex-wrap:wrap; margin:10px 0 14px; }
    .btn { color:var(--text); background:#1b3044; border:1px solid var(--line); border-radius:8px; padding:8px 10px; text-decoration:none; cursor:pointer; }
    .kv { display:grid; grid-template-columns:repeat(4,minmax(0,1fr)); gap:8px; }
    .kv div { display:grid; gap:4px; }
    .charts { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:12px; }
    .chartBox { border:1px solid var(--line); border-radius:8px; background:#0a1621; padding:10px; }
    .chartTitle { display:flex; justify-content:space-between; gap:8px; color:var(--muted); font-size:13px; margin-bottom:8px; }
    .infoGrid { display:grid; grid-template-columns:repeat(2,minmax(0,1fr)); gap:12px; }
    .infoGrid h3 { margin:0 0 8px; font-size:15px; }
    .infoGrid ul { margin:0; padding-left:18px; color:var(--muted); line-height:1.6; }
    .tag { display:inline-block; border:1px solid var(--line); border-radius:6px; padding:2px 6px; color:var(--muted); font-size:12px; }
    input,select { width:100%; color:var(--text); background:#0d1824; border:1px solid var(--line); border-radius:6px; padding:7px; }
    canvas { width:100%; height:180px; display:block; background:#0a1621; }
    table { width:100%; border-collapse:collapse; font-size:13px; }
    td,th { text-align:left; padding:8px; border-bottom:1px solid var(--line); }
    @media (max-width:760px) { .card,.wide { grid-column:span 12; } .kv,.charts,.infoGrid { grid-template-columns:1fr; } h1 { font-size:22px; } }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="head">
      <div>
        <h1>ESP32-S3M 环境监测站</h1>
        <div class="sub">SGP41 / SCD41 / SHT41 / BH1750，I2C：SDA IO4 / SCL IO5 / 400 kHz</div>
      </div>
      <div class="meta" id="net">加载中...</div>
    </div>
    <div class="toolbar">
      <a class="btn" href="/api/status" target="_blank">JSON</a>
      <a class="btn" href="/api/history" target="_blank">历史数据</a>
      <a class="btn" href="/api/log.csv" target="_blank">分钟 CSV</a>
      <button class="btn" id="boostPerf" type="button">加速查看</button>
      <button class="btn" id="saveCfg" type="button">保存配置</button>
      <button class="btn" id="resetCfg" type="button">重置配置</button>
      <button class="btn" id="clearLog" type="button">数据清零</button>
    </div>
    <div class="grid">
      <section class="card"><div class="label">SCD41 二氧化碳</div><div class="value" id="co2">--</div><div class="meta" id="scd">离线</div><div class="meta" id="co2Meaning">--</div></section>
      <section class="card"><div class="label">SHT41 温度</div><div class="value" id="temp">--</div><div class="meta" id="sht">离线</div><div class="meta" id="tempMeaning">--</div></section>
      <section class="card"><div class="label">SHT41 湿度</div><div class="value" id="rh">--</div><div class="meta">相对湿度</div></section>
      <section class="card"><div class="label">BH1750 光照</div><div class="value" id="lux">--</div><div class="meta" id="bh">离线</div><div class="meta" id="luxMeaning">--</div></section>
      <section class="card wide"><div class="label">SGP41 气体指数</div><div class="value small" id="gas">--</div><div class="meta" id="sgp">离线</div><div class="meta" id="gasMeaning">--</div></section>
      <section class="card wide"><div class="label">系统 / 存储</div><div class="value small" id="state">--</div><div class="meta" id="uptime">--</div><div class="meta" id="storage">--</div><div class="meta" id="timeState">时间同步：--</div></section>
      <section class="card full">
        <div class="label">趋势曲线</div>
        <div class="toolbar">
          <button class="btn rangeBtn" data-range="ram" type="button">实时</button>
          <button class="btn rangeBtn" data-range="60" type="button">1小时</button>
          <button class="btn rangeBtn" data-range="360" type="button">6小时</button>
          <button class="btn rangeBtn" data-range="1440" type="button">1天</button>
          <button class="btn rangeBtn" data-range="4320" type="button">3天</button>
          <button class="btn rangeBtn" data-range="all" type="button">全部分钟</button>
        </div>
        <div class="meta" id="historyMeta">历史数据加载中...</div>
        <div class="charts">
          <div class="chartBox"><div class="chartTitle"><span>CO2 二氧化碳</span><span class="tag">ppm</span></div><canvas id="chartCo2" width="440" height="180"></canvas></div>
          <div class="chartBox"><div class="chartTitle"><span>VOC 指数</span><span class="tag">index</span></div><canvas id="chartVoc" width="440" height="180"></canvas></div>
          <div class="chartBox"><div class="chartTitle"><span>NOx 指数</span><span class="tag">index</span></div><canvas id="chartNox" width="440" height="180"></canvas></div>
          <div class="chartBox"><div class="chartTitle"><span>BH1750 光照</span><span class="tag">lx</span></div><canvas id="chartLux" width="440" height="180"></canvas></div>
        </div>
      </section>
      <section class="card full">
        <div class="label">参数配置</div>
        <div class="kv">
          <div><span class="meta">power_mode</span><select id="cfgPowerMode"><option value="balanced">balanced 均衡</option><option value="low_power">low_power 低功耗</option><option value="performance">performance 性能</option></select></div>
          <div><span class="meta">LCD lcd_brightness_pct</span><input id="cfgLcdBrightness" type="number" min="0" max="100" step="5"></div>
          <div><span class="meta">LCD backlight_timeout_ms</span><input id="cfgBacklightTimeout" type="number" min="5000" max="600000" step="5000"></div>
          <div><span class="meta">WiFi wifi_sta_sleep</span><select id="cfgWifiSleep"><option value="1">开启省电</option><option value="0">关闭省电</option></select></div>
          <div><span class="meta">low_power_sensor_interval_ms</span><input id="cfgLowPowerSensor" type="number" min="30000" max="600000" step="5000"></div>
          <div><span class="meta">快速采样间隔 ms</span><input id="cfgFast" type="number" min="500" max="10000" step="100"></div>
          <div><span class="meta">SCD41 读取间隔 ms</span><input id="cfgScd" type="number" min="5000" max="60000" step="1000"></div>
          <div><span class="meta">实时采样间隔 ms</span><input id="cfgLog" type="number" min="500" max="600000" step="500"></div>
          <div><span class="meta">网页刷新间隔 ms</span><input id="cfgLive" type="number" min="250" max="10000" step="250"></div>
          <div><span class="meta">CO2 提醒 ppm</span><input id="cfgCo2Warn" type="number" min="400" max="5000" step="50"></div>
          <div><span class="meta">CO2 严重 ppm</span><input id="cfgCo2Bad" type="number" min="400" max="5000" step="50"></div>
          <div><span class="meta">VOC 提醒</span><input id="cfgVocWarn" type="number" min="1" max="500" step="5"></div>
          <div><span class="meta">VOC 严重</span><input id="cfgVocBad" type="number" min="1" max="500" step="5"></div>
          <div><span class="meta">NOx 提醒</span><input id="cfgNoxWarn" type="number" min="1" max="500" step="1"></div>
          <div><span class="meta">NOx 严重</span><input id="cfgNoxBad" type="number" min="1" max="500" step="1"></div>
          <div><span class="meta">温度下限 C</span><input id="cfgTempLow" type="number" min="-20" max="60" step="0.5"></div>
          <div><span class="meta">温度上限 C</span><input id="cfgTempHigh" type="number" min="-20" max="80" step="0.5"></div>
          <div><span class="meta">湿度下限 %</span><input id="cfgRhLow" type="number" min="0" max="100" step="1"></div>
          <div><span class="meta">湿度上限 %</span><input id="cfgRhHigh" type="number" min="1" max="100" step="1"></div>
          <div><span class="meta">光照下限 lx</span><input id="cfgLuxLow" type="number" min="0" max="100000" step="10"></div>
          <div><span class="meta">光照上限 lx</span><input id="cfgLuxHigh" type="number" min="1" max="100000" step="10"></div>
          <div><span class="meta">SHT41 精度</span><select id="cfgShtPrecision"><option value="0">低精度/低热量</option><option value="1">中精度</option><option value="2">高精度</option></select></div>
          <div><span class="meta">BH1750 模式</span><select id="cfgBhMode"><option value="0">连续高分辨率 1 lx</option><option value="1">连续高分辨率 0.5 lx</option><option value="2">连续低分辨率 4 lx</option></select></div>
        </div>
        <div class="meta" id="cfgState">配置加载中...</div>
      </section>
      <section class="card full">
        <div class="label">数据解读</div>
        <div class="meta">VOC 指数是 Sensirion 的相对空气质量指标，约 100 表示当前基线；越低通常越干净，越高通常表示酒精、烹饪、溶剂、烟雾、人体气味等 VOC 事件。NOx 指数在干净空气中通常接近 1，持续升高一般代表燃烧源或室外污染进入。SRAW 是算法原始输入，不是直接给人看的空气质量分数。</div>
      </section>
      <section class="card full">
        <div class="label">阈值依据 / 传感器设置 / 使用说明</div>
        <div class="infoGrid">
          <div>
            <h3>当前阈值怎么来的</h3>
            <ul>
              <li>CO2：400 ppm 左右接近室外背景；1000 ppm 作为通风提醒，1500 ppm 作为明显通风不足。</li>
              <li>VOC/NOx：来自 Sensirion Gas Index 的相对指数思路，100 附近是 VOC 基线，NOx 清洁空气通常接近低位。</li>
              <li>温湿度：默认按人体舒适和霉菌/冷凝风险做工程默认值，后续可按实际场景调。</li>
              <li>光照：默认用室内偏暗到明亮办公环境的经验范围，实际应按安装位置校准。</li>
            </ul>
          </div>
          <div>
            <h3>可暴露的传感器能力</h3>
            <ul>
              <li>SHT41：已开放高/中/低精度测量；加热器模式适合除湿诊断，暂未开放长期控制。</li>
              <li>SCD41：周期测量约 5 秒出新值，可配置自动自校准、强制校准、温度偏移和海拔补偿。</li>
              <li>SGP41：需要温湿度补偿和预热；VOC/NOx 指数算法可调学习时间和门限，当前先用库默认值。</li>
              <li>BH1750：已开放连续高分辨率、连续高分辨率 2、连续低分辨率；单次模式和 MTreg 后续再接。</li>
            </ul>
          </div>
          <div>
            <h3>安装和预热</h3>
            <ul>
              <li>传感器不要贴近 ESP32、LDO、屏幕背光或发热器件，否则温度会偏高。</li>
              <li>外壳要留通风孔，SGP41 和 SCD41 需要能接触流动空气，不能被胶带或外壳挡住。</li>
              <li>上电后先等 SGP41 预热完成；VOC/NOx 指数还会继续学习环境基线。</li>
              <li>避免把手、酒精、清洁剂直接靠近探头，否则 VOC 会瞬间升高，这是正常响应。</li>
            </ul>
          </div>
          <div>
            <h3>怎么看曲线</h3>
            <ul>
              <li>每张图都有自己的纵轴单位，不再把不同量纲硬塞进一张图。</li>
              <li>实时视图显示 RAM 里的最近样本；1小时/1天/3天视图读取 Flash 环形分钟聚合，每分钟一个点。</li>
              <li>先看趋势，再看瞬时值。持续上升比单点跳动更有意义。</li>
              <li>CSV 下载的是分钟聚合数据；1 秒实时数据只保留在 RAM，避免长期频繁写 Flash。</li>
            </ul>
          </div>
        </div>
      </section>
      <section class="card full">
        <div class="label">原始值 / 诊断状态</div>
        <table><tbody id="rows"></tbody></table>
      </section>
    </div>
  </div>
  <script>
    const $ = id => document.getElementById(id);
    const fmt = (v,d=1) => Number.isFinite(Number(v)) ? Number(v).toFixed(d) : '--';
    const yes = v => v ? '在线' : '离线';
    const charts = {
      co2: { el: $('chartCo2'), key: 'co2_ppm', color: '#f0c35a', unit: 'ppm', min: 400, max: 1600 },
      voc: { el: $('chartVoc'), key: 'voc_index', color: '#ff8f55', unit: '', min: 0, max: 300 },
      nox: { el: $('chartNox'), key: 'nox_index', color: '#43c889', unit: '', min: 0, max: 80 },
      lux: { el: $('chartLux'), key: 'lux', color: '#61b5ff', unit: 'lx', min: 0, max: 1500 },
    };
    let liveTimer = null;
    let configLoaded = false;
    let latestStatus = null;
    let historyRange = 'ram';
    const MAX_CHART_POINTS = 360;
    const rangeMinutes = { '60':60, '360':360, '1440':1440, '4320':4320 };
    const minuteCache = { loaded:false, loading:null, rows:[], totalRows:0, loadedAt:0 };
    const ramCache = { rows:[], loadedAt:0 };
    function cls(level) { return level === 'bad' ? 'bad' : level === 'warn' ? 'warn' : 'ok'; }
    function setConfigInputs(c) {
      if (configLoaded || !c) return;
      $('cfgPowerMode').value = c.power_mode;
      $('cfgLcdBrightness').value = c.lcd_brightness_pct;
      $('cfgBacklightTimeout').value = c.backlight_timeout_ms;
      $('cfgWifiSleep').value = c.wifi_sta_sleep ? '1' : '0';
      $('cfgLowPowerSensor').value = c.low_power_sensor_interval_ms;
      $('cfgFast').value = c.fast_interval_ms;
      $('cfgScd').value = c.scd_interval_ms;
      $('cfgLog').value = c.log_interval_ms;
      $('cfgLive').value = c.live_refresh_ms;
      $('cfgCo2Warn').value = c.co2_warn_ppm;
      $('cfgCo2Bad').value = c.co2_bad_ppm;
      $('cfgVocWarn').value = c.voc_warn;
      $('cfgVocBad').value = c.voc_bad;
      $('cfgNoxWarn').value = c.nox_warn;
      $('cfgNoxBad').value = c.nox_bad;
      $('cfgTempLow').value = c.temp_low_c;
      $('cfgTempHigh').value = c.temp_high_c;
      $('cfgRhLow').value = c.humidity_low;
      $('cfgRhHigh').value = c.humidity_high;
      $('cfgLuxLow').value = c.lux_low;
      $('cfgLuxHigh').value = c.lux_high;
      $('cfgShtPrecision').value = c.sht_precision;
      $('cfgBhMode').value = c.bh1750_mode;
      $('cfgState').textContent = `配置已加载 / power ${c.power_mode} / 每 ${Math.round(c.log_interval_ms / 1000)} 秒更新实时样本，Flash 固定每分钟聚合保存`;
      configLoaded = true;
    }
    function decimateRows(rows) {
      if (!rows || rows.length <= MAX_CHART_POINTS) return rows || [];
      const out = [];
      const step = (rows.length - 1) / (MAX_CHART_POINTS - 1);
      for (let i = 0; i < MAX_CHART_POINTS; i++) out.push(rows[Math.round(i * step)]);
      return out;
    }
    function formatMinuteLabel(row) {
      if (!row) return '--';
      if (row.minute && row.minute > 28000000) {
        const d = new Date(Number(row.minute) * 60000);
        return d.toLocaleString();
      }
      if (row.uptime_ms) return `运行 ${Math.round(Number(row.uptime_ms) / 60000)} 分钟`;
      return `第 ${row.minute ?? '--'} 分钟`;
    }
    function updateHistoryButtons() {
      document.querySelectorAll('.rangeBtn').forEach(btn => {
        btn.classList.toggle('ok', btn.dataset.range === historyRange);
      });
    }
    function drawOneChart(def, rows, totalRows) {
      const ctx = def.el.getContext('2d');
      const w = def.el.width, h = def.el.height;
      const left = 48, right = 10, top = 12, bottom = 28;
      const plotW = w - left - right, plotH = h - top - bottom;
      ctx.clearRect(0,0,w,h);
      let dataMax = def.max;
      let dataMin = def.min;
      let valueCount = 0;
      (rows || []).forEach(r => {
        const value = Number(r[def.key]);
        if (!Number.isFinite(value)) return;
        if (valueCount === 0) {
          dataMax = value;
          dataMin = value;
        } else {
          if (value > dataMax) dataMax = value;
          if (value < dataMin) dataMin = value;
        }
        valueCount++;
      });
      const yMin = Math.min(def.min, dataMin);
      const yMax = Math.max(def.max, dataMax * 1.1, yMin + 1);
      ctx.strokeStyle = '#263a4d';
      ctx.lineWidth = 1;
      for (let i=0;i<5;i++) {
        const y = top + i * (plotH / 4);
        const label = yMax - i * ((yMax - yMin) / 4);
        ctx.beginPath(); ctx.moveTo(left,y); ctx.lineTo(w - right,y); ctx.stroke();
        ctx.fillStyle = '#91a6ba'; ctx.font = '11px Segoe UI'; ctx.textAlign = 'right';
        ctx.fillText(Math.round(label).toString(), left - 6, y + 4);
      }
      if (!rows || rows.length < 2) {
        ctx.fillStyle = '#91a6ba';
        ctx.font = '14px Segoe UI';
        ctx.textAlign = 'left';
        ctx.fillText('等待日志样本...', left, 42);
        return;
      }
      ctx.strokeStyle = def.color;
      ctx.lineWidth = 2;
      ctx.beginPath();
      rows.forEach((r,i) => {
        const value = Number(r[def.key]);
        const x = left + i * (plotW / Math.max(1, rows.length - 1));
        const y = top + (1 - ((value - yMin) / (yMax - yMin))) * plotH;
        if (i === 0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
      });
      ctx.stroke();
      ctx.fillStyle = '#91a6ba';
      ctx.font = '11px Segoe UI';
      ctx.textAlign = 'left';
      const shownText = totalRows && totalRows > rows.length ? `${rows.length}/${totalRows}` : `${rows.length}`;
      ctx.fillText(historyRange === 'ram' ? `实时样本 ${shownText}` : `分钟点 ${shownText}`, left, h - 8);
      ctx.textAlign = 'right';
      ctx.fillText(def.unit, w - right, h - 8);
    }
    function drawChart(rows) {
      const sourceRows = rows || [];
      const drawRows = decimateRows(sourceRows);
      requestAnimationFrame(() => Object.values(charts).forEach(def => drawOneChart(def, drawRows, sourceRows.length)));
    }
    async function refresh() {
      const r = await fetch('/api/status', {cache:'no-store'});
      const s = await r.json();
      latestStatus = s;
      setConfigInputs(s.config);
      $('net').textContent = `${s.network.mode} ${s.network.ip}`;
      $('co2').textContent = s.scd41.online && s.scd41.ok > 0 ? `${s.scd41.co2_ppm} ppm` : '--';
      $('scd').textContent = `${yes(s.scd41.online)} / 成功 ${s.scd41.ok} 失败 ${s.scd41.fail} 就绪 ${s.scd41.ready}`;
      $('co2Meaning').textContent = s.interpretation.co2.text;
      $('co2Meaning').className = `meta ${cls(s.interpretation.co2.level)}`;
      $('temp').textContent = s.sht41.online ? `${fmt(s.sht41.temp_c)} C` : '--';
      $('rh').textContent = s.sht41.online ? `${fmt(s.sht41.humidity)} %` : '--';
      $('sht').textContent = `${yes(s.sht41.online)} / 成功 ${s.sht41.ok} 失败 ${s.sht41.fail}`;
      $('tempMeaning').textContent = `${s.interpretation.temperature.text}; ${s.interpretation.humidity.text}`;
      $('lux').textContent = s.bh1750.online ? `${fmt(s.bh1750.lux)} lx` : '--';
      $('bh').textContent = `${yes(s.bh1750.online)} / 地址 0x${Number(s.bh1750.addr).toString(16)} / 成功 ${s.bh1750.ok}`;
      $('luxMeaning').textContent = s.interpretation.lux.text;
      $('gas').textContent = s.sgp41.online ? `VOC ${s.sgp41.voc_index} / NOx ${s.sgp41.nox_index}` : '--';
      $('sgp').textContent = `${yes(s.sgp41.online)} / 原始值 ${s.sgp41.sraw_voc}/${s.sgp41.sraw_nox} / 预热 ${s.sgp41.conditioning_s}s`;
      $('gasMeaning').textContent = `${s.interpretation.voc.text}; ${s.interpretation.nox.text}`;
      $('gasMeaning').className = `meta ${cls(s.interpretation.voc.level === 'bad' || s.interpretation.nox.level === 'bad' ? 'bad' : s.interpretation.voc.level === 'warn' || s.interpretation.nox.level === 'warn' ? 'warn' : 'ok')}`;
      $('state').textContent = s.ok ? '正常' : '检查总线';
      $('state').className = `value small ${s.ok ? 'ok' : 'bad'}`;
      $('uptime').textContent = `运行 ${s.system.uptime_s}s / 剩余堆内存 ${s.system.heap}`;
      $('storage').textContent = `LittleFS ${s.storage.mounted ? '已挂载' : '离线'} / 分钟 ${s.storage.ring_count}/${s.storage.ring_capacity} / 失败 ${s.storage.failures}`;
      $('timeState').textContent = `时间同步：${s.time && s.time.sync_time ? s.time.local_time : '未同步，使用运行分钟'} / ${s.time ? s.time.time_source : 'unknown'}`;
      $('boostPerf').classList.toggle('ok', !!(s.power && s.power.web_boost_active));
      const rows = [
        ['I2C', `SDA ${s.i2c.sda}, SCL ${s.i2c.scl}, ${s.i2c.clock_hz} Hz`],
        ['SHT41 初始化', s.sht41.init_error],
        ['SCD41 初始化', s.scd41.init_error],
        ['SGP41 初始化/自检', `${s.sgp41.init_error} / ${s.sgp41.self_test}`],
        ['Flash 环形日志', `${s.storage.ring_path}, ${s.storage.ring_count}/${s.storage.ring_capacity} 分钟点`],
        ['实时缓存', `${s.storage.raw_ram_rows} 条，间隔 ${s.config.log_interval_ms} ms`],
        ['Power optimization', `${s.power.power_mode}, LCD ${s.power.lcd_brightness_pct}%, 背光 ${s.power.backlight_on ? '开' : '关'}, sensor ${s.power.sensor_interval_ms} ms`],
        ['网页加速', s.power.web_boost_active ? '开启：临时关闭 WiFi sleep，保持 80MHz 低功耗 CPU' : '关闭：按配置省电'],
        ['长期运行', `WiFi重连 ${s.network.reconnects} 次，loop卡顿 ${s.system.loop_stalls} 次，最大间隔 ${s.system.max_loop_gap_ms} ms`],
        ['Flash写入耗时', `最近 ${s.storage.last_persist_duration_ms} ms，最大 ${s.storage.max_persist_duration_ms} ms`],
        ['时间', s.time && s.time.sync_time ? `${s.time.local_time} / epoch ${s.time.epoch_s}` : '未同步：日志分钟先按运行时间计'],
        ['IP', s.network.ip],
      ];
      $('rows').innerHTML = rows.map(x => `<tr><th>${x[0]}</th><td>${x[1]}</td></tr>`).join('');
      if (liveTimer && s.config && Number(s.config.live_refresh_ms) > 0) {
        clearInterval(liveTimer);
        liveTimer = setInterval(() => refresh().catch(()=>{}), Number(s.config.live_refresh_ms));
      }
    }
    async function ensureMinuteCache(force=false) {
      if (minuteCache.loading) return minuteCache.loading;
      if (!force && minuteCache.loaded) return minuteCache;
      $('historyMeta').textContent = '正在从 ESP32 读取全部分钟历史，之后切换 1小时/6小时/1天 会直接用浏览器缓存...';
      minuteCache.loading = fetch('/api/history?range=all', {cache:'no-store'})
        .then(r => r.json())
        .then(h => {
          minuteCache.rows = h.rows || [];
          minuteCache.totalRows = h.total_rows || minuteCache.rows.length;
          minuteCache.loaded = true;
          minuteCache.loadedAt = Date.now();
          minuteCache.loading = null;
          return minuteCache;
        })
        .catch(err => {
          minuteCache.loading = null;
          throw err;
        });
      return minuteCache.loading;
    }
    function sliceRowsForRange(range) {
      if (range === 'all') return minuteCache.rows;
      const minutes = rangeMinutes[range] || 60;
      return minuteCache.rows.slice(Math.max(0, minuteCache.rows.length - minutes));
    }
    function renderHistoryRange() {
      updateHistoryButtons();
      let rows = [];
      if (historyRange === 'ram') {
        rows = ramCache.rows;
        $('historyMeta').textContent = `实时缓存 ${rows.length} 点，来自 RAM；切换分钟范围时会使用浏览器缓存。`;
      } else {
        rows = sliceRowsForRange(historyRange);
        const first = rows[0], last = rows[rows.length - 1];
        $('historyMeta').textContent = `浏览器已缓存 ${minuteCache.rows.length}/${minuteCache.totalRows} 个分钟点；当前显示 ${rows.length} 点，${formatMinuteLabel(first)} 到 ${formatMinuteLabel(last)}。`;
      }
      drawChart(rows);
    }
    async function refreshHistory(force=false) {
      if (historyRange === 'ram') {
        const r = await fetch('/api/history', {cache:'no-store'});
        const h = await r.json();
        ramCache.rows = h.rows || [];
        ramCache.loadedAt = Date.now();
        renderHistoryRange();
        return;
      }
      await ensureMinuteCache(force);
      renderHistoryRange();
    }
    $('saveCfg').addEventListener('click', async () => {
      const params = new URLSearchParams({
        power_mode: $('cfgPowerMode').value,
        lcd_brightness_pct: $('cfgLcdBrightness').value,
        backlight_timeout_ms: $('cfgBacklightTimeout').value,
        wifi_sta_sleep: $('cfgWifiSleep').value,
        low_power_sensor_interval_ms: $('cfgLowPowerSensor').value,
        fast_interval_ms: $('cfgFast').value,
        scd_interval_ms: $('cfgScd').value,
        log_interval_ms: $('cfgLog').value,
        live_refresh_ms: $('cfgLive').value,
        co2_warn_ppm: $('cfgCo2Warn').value,
        co2_bad_ppm: $('cfgCo2Bad').value,
        voc_warn: $('cfgVocWarn').value,
        voc_bad: $('cfgVocBad').value,
        nox_warn: $('cfgNoxWarn').value,
        nox_bad: $('cfgNoxBad').value,
        temp_low_c: $('cfgTempLow').value,
        temp_high_c: $('cfgTempHigh').value,
        humidity_low: $('cfgRhLow').value,
        humidity_high: $('cfgRhHigh').value,
        lux_low: $('cfgLuxLow').value,
        lux_high: $('cfgLuxHigh').value,
        sht_precision: $('cfgShtPrecision').value,
        bh1750_mode: $('cfgBhMode').value,
      });
      $('cfgState').textContent = '正在保存...';
      const r = await fetch('/api/config?' + params.toString(), {method:'POST', cache:'no-store'});
      $('cfgState').textContent = r.ok ? '已保存到 LittleFS' : '保存失败';
      configLoaded = false;
      await refresh();
    });
    $('resetCfg').addEventListener('click', async () => {
      $('cfgState').textContent = '正在恢复默认值...';
      const r = await fetch('/api/config/reset', {method:'POST', cache:'no-store'});
      $('cfgState').textContent = r.ok ? '已恢复默认值并保存' : '重置失败';
      configLoaded = false;
      await refresh();
    });
    $('boostPerf').addEventListener('click', async () => {
      $('historyMeta').textContent = '正在开启 3 分钟网页加速...';
      const r = await fetch('/api/performance/boost?duration_ms=180000', {method:'POST', cache:'no-store'});
      $('historyMeta').textContent = r.ok ? '网页加速已开启：临时关闭 WiFi 省电，保持低功耗 CPU 频率。' : '网页加速开启失败';
      await refresh();
    });
    $('clearLog').addEventListener('click', async () => {
      if (!confirm('确认清零历史数据？这会删除 LittleFS 分钟日志和当前 RAM 实时缓存，配置不会被删除。')) return;
      $('historyMeta').textContent = '正在清零历史数据...';
      const r = await fetch('/api/log/clear?confirm=1', {method:'POST', cache:'no-store'});
      if (!r.ok) {
        $('historyMeta').textContent = '数据清零失败';
        return;
      }
      minuteCache.loaded = false;
      minuteCache.rows = [];
      minuteCache.totalRows = 0;
      ramCache.rows = [];
      await refresh();
      await refreshHistory(true);
      $('historyMeta').textContent = '历史数据已清零，新的分钟点会继续写入。';
    });
    document.querySelectorAll('.rangeBtn').forEach(btn => {
      btn.addEventListener('click', async () => {
        historyRange = btn.dataset.range;
        if (historyRange !== 'ram') {
          await ensureMinuteCache();
        }
        renderHistoryRange();
      });
    });
    fetch('/api/performance/boost?duration_ms=180000', {method:'POST', cache:'no-store'}).catch(()=>{});
    refresh().catch(()=>{});
    refreshHistory().catch(()=>{});
    ensureMinuteCache().then(() => {
      if (historyRange === 'ram') $('historyMeta').textContent = `分钟历史已预加载 ${minuteCache.rows.length} 点，切换 1小时/6小时/1天会直接本地显示。`;
    }).catch(()=>{});
    liveTimer = setInterval(() => refresh().catch(()=>{}), 1000);
    setInterval(() => {
      if (historyRange === 'ram') refreshHistory().catch(()=>{});
      else if (Date.now() - minuteCache.loadedAt > 60000) refreshHistory(true).catch(()=>{});
    }, 10000);
  </script>
</body>
</html>
)HTML";

void spiWrite(const uint8_t *data, size_t len) {
  if (!gLcd || len == 0) return;
  spi_transaction_t t = {};
  t.length = len * 8;
  t.tx_buffer = data;
  spi_device_polling_transmit(gLcd, &t);
}

void lcdCmd(uint8_t cmd) {
  gpio_set_level(kLcdDc, 0);
  spiWrite(&cmd, 1);
}

void lcdData(const uint8_t *data, size_t len) {
  gpio_set_level(kLcdDc, 1);
  spiWrite(data, len);
}

void lcdData16(uint16_t color) {
  uint8_t data[] = {static_cast<uint8_t>(color >> 8), static_cast<uint8_t>(color)};
  lcdData(data, sizeof(data));
}

uint16_t lcdWireColor(uint16_t color) {
  return static_cast<uint16_t>((color << 8) | (color >> 8));
}

void lcdSetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  uint8_t data[4];
  data[0] = (x0 + 1) >> 8;
  data[1] = (x0 + 1) & 0xFF;
  data[2] = (x1 + 1) >> 8;
  data[3] = (x1 + 1) & 0xFF;
  lcdCmd(0x2A);
  lcdData(data, 4);
  data[0] = (y0 + 26) >> 8;
  data[1] = (y0 + 26) & 0xFF;
  data[2] = (y1 + 26) >> 8;
  data[3] = (y1 + 26) & 0xFF;
  lcdCmd(0x2B);
  lcdData(data, 4);
  lcdCmd(0x2C);
}

void lcdFill(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color) {
  if (x0 >= kLcdWidth || y0 >= kLcdHeight) return;
  x1 = min<uint16_t>(x1, kLcdWidth - 1);
  y1 = min<uint16_t>(y1, kLcdHeight - 1);
  const uint16_t wireColor = lcdWireColor(color);
  for (uint16_t y = y0; y <= y1; y++) {
    uint16_t *row = &gLcdFrame[y * kLcdWidth + x0];
    for (uint16_t x = x0; x <= x1; x++) {
      *row++ = wireColor;
    }
  }
}

void lcdPixel(uint16_t x, uint16_t y, uint16_t color) {
  if (x >= kLcdWidth || y >= kLcdHeight) return;
  gLcdFrame[y * kLcdWidth + x] = lcdWireColor(color);
}

void lcdPresent() {
  if (!gLcd) return;
  lcdSetWindow(0, 0, kLcdWidth - 1, kLcdHeight - 1);
  gpio_set_level(kLcdDc, 1);
  spiWrite(reinterpret_cast<const uint8_t *>(gLcdFrame), sizeof(gLcdFrame));
}

void setBacklight(bool on) {
  gBacklightOn = on;
  ledcWrite(kLcdBlPwmChannel, on ? static_cast<uint8_t>((static_cast<uint16_t>(cfg.lcdBrightnessPct) * 255U) / 100U) : 0);
}

void noteUserActivity() {
  gLastUserActivityMs = millis();
  if (!gBacklightOn) {
    setBacklight(true);
    gForceDisplayDraw = true;
  }
}

bool webBoostActive() {
  const uint32_t now = millis();
  return gWebBoostUntilMs != 0 && static_cast<int32_t>(gWebBoostUntilMs - now) > 0;
}

void maintainWebBoostPower() {
  const bool active = webBoostActive();
  if (active == gLastAppliedWebBoost) return;
  gLastAppliedWebBoost = active;
  WiFi.setSleep(cfg.wifiStaSleep && !active);
}

void noteWebActivity(uint32_t durationMs = kDefaultWebBoostMs) {
  uint32_t clamped = durationMs;
  if (clamped < 5000UL) clamped = 5000UL;
  if (clamped > kMaxWebBoostMs) clamped = kMaxWebBoostMs;
  gWebBoostUntilMs = millis() + clamped;
  WiFi.setSleep(false);
  gLastAppliedWebBoost = true;
}

void handleBacklightTimeout() {
  if (cfg.backlightTimeoutMs == 0) return;
  if (gBacklightOn && millis() - gLastUserActivityMs > cfg.backlightTimeoutMs) {
    setBacklight(false);
  }
}

const uint8_t *glyph5x7(char c) {
  static const uint8_t blank[5] = {0, 0, 0, 0, 0};
  static const uint8_t digits[10][5] = {
      {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, {0x18,0x14,0x12,0x7F,0x10},
      {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03}, {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}};
  static const uint8_t upper[26][5] = {
      {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22}, {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41},
      {0x7F,0x09,0x09,0x09,0x01}, {0x3E,0x41,0x49,0x49,0x7A}, {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01},
      {0x7F,0x08,0x14,0x22,0x41}, {0x7F,0x40,0x40,0x40,0x40}, {0x7F,0x02,0x0C,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E},
      {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31}, {0x01,0x01,0x7F,0x01,0x01},
      {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F}, {0x3F,0x40,0x38,0x40,0x3F}, {0x63,0x14,0x08,0x14,0x63}, {0x07,0x08,0x70,0x08,0x07},
      {0x61,0x51,0x49,0x45,0x43}};
  static const uint8_t dot[5] = {0x00,0x60,0x60,0x00,0x00};
  static const uint8_t colon[5] = {0x00,0x36,0x36,0x00,0x00};
  static const uint8_t slash[5] = {0x20,0x10,0x08,0x04,0x02};
  static const uint8_t minus[5] = {0x08,0x08,0x08,0x08,0x08};
  static const uint8_t percent[5] = {0x63,0x13,0x08,0x64,0x63};
  if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
  if (c >= '0' && c <= '9') return digits[c - '0'];
  if (c >= 'A' && c <= 'Z') return upper[c - 'A'];
  if (c == '.') return dot;
  if (c == ':') return colon;
  if (c == '/') return slash;
  if (c == '-') return minus;
  if (c == '%') return percent;
  return blank;
}

struct Glyph12 {
  uint16_t codepoint;
  uint8_t data[24];
};

const Glyph12 kCnGlyphs[] = {
  {0x4E3B,{0x00,0x00,0x08,0x00,0x08,0x00,0xFF,0x80,0x08,0x00,0x08,0x00,0x7F,0x00,0x08,0x00,0x08,0x00,0xFF,0x80,0x00,0x00,0x00,0x00}},
  {0x770B,{0x00,0x00,0x03,0x00,0x7C,0x00,0x7F,0x00,0xFF,0x80,0x30,0x00,0x7F,0x00,0xBF,0x00,0x3F,0x00,0x3F,0x00,0x00,0x00,0x00,0x00}},
  {0x677F,{0x00,0x00,0x20,0x00,0x2F,0x00,0x78,0x00,0x2F,0x00,0x3D,0x00,0x6D,0x00,0x6A,0x00,0x3B,0x00,0x3C,0x80,0x00,0x00,0x00,0x00}},
  {0x4E8C,{0x00,0x00,0x00,0x00,0x00,0x00,0x7F,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x80,0x00,0x00,0x00,0x00,0x00,0x00}},
  {0x6C27,{0x00,0x00,0x00,0x00,0x00,0x00,0x7F,0x80,0x80,0x00,0x7E,0x00,0x7E,0x00,0x7E,0x00,0xFF,0x80,0x11,0x80,0x00,0x00,0x00,0x00}},
  {0x5316,{0x00,0x00,0x00,0x00,0x14,0x00,0x24,0x80,0x25,0x00,0x66,0x00,0x6C,0x00,0x34,0x80,0x24,0x80,0x27,0x80,0x00,0x00,0x00,0x00}},
  {0x78B3,{0x00,0x00,0x02,0x00,0xFA,0x80,0x2F,0x80,0x5F,0x80,0x6A,0x00,0xEE,0x80,0x75,0x00,0x75,0x00,0x58,0x80,0x00,0x00,0x00,0x00}},
  {0x6E29,{0x00,0x00,0x00,0x00,0x9F,0x80,0x50,0x80,0x9F,0x80,0x5F,0x80,0x1F,0x80,0x5A,0x80,0x5A,0x80,0xBF,0xC0,0x00,0x00,0x00,0x00}},
  {0x6E7F,{0x00,0x00,0x00,0x00,0x9F,0x80,0x5F,0x80,0x90,0x80,0x5F,0x80,0x2A,0x00,0x5B,0x80,0x4A,0x00,0xBF,0x80,0x00,0x00,0x00,0x00}},
  {0x6325,{0x00,0x00,0x00,0x00,0x5F,0x80,0xF4,0x80,0x5F,0x80,0x72,0x00,0xDF,0x80,0x42,0x00,0x5F,0x80,0xC2,0x00,0x00,0x00,0x00,0x00}},
  {0x53D1,{0x00,0x00,0x00,0x00,0x2A,0x00,0x51,0x00,0x7F,0x80,0x1F,0x00,0x31,0x00,0x2A,0x00,0x46,0x00,0x79,0x80,0x00,0x00,0x00,0x00}},
  {0x6C2E,{0x00,0x00,0x20,0x00,0x7F,0x80,0x7F,0x00,0xBF,0x00,0x7F,0x00,0x3F,0x00,0xD3,0x00,0x75,0x80,0xCF,0x80,0x00,0x00,0x00,0x00}},
  {0x5149,{0x00,0x00,0x00,0x00,0x08,0x00,0x49,0x00,0x2A,0x00,0xFF,0x80,0x14,0x00,0x14,0x80,0x24,0x80,0xC7,0x80,0x00,0x00,0x00,0x00}},
  {0x7167,{0x00,0x00,0x00,0x00,0xFF,0x80,0x94,0x80,0xF7,0x80,0x9F,0x80,0x98,0x80,0xFF,0x80,0x01,0x00,0x55,0x00,0x92,0x80,0x00,0x00}},
  {0x539F,{0x00,0x00,0x00,0x00,0x7F,0x80,0x5F,0x00,0x51,0x00,0x5F,0x00,0x5F,0x00,0x56,0x00,0xD5,0x00,0xBC,0x80,0x00,0x00,0x00,0x00}},
  {0x59CB,{0x00,0x00,0x22,0x00,0x25,0x00,0xF9,0x00,0x5F,0x80,0x50,0x80,0x6F,0x00,0x29,0x00,0x5F,0x00,0x89,0x00,0x00,0x00,0x00,0x00}},
  {0x8BB0,{0x00,0x00,0x00,0x00,0x4F,0x80,0x20,0x80,0x00,0x80,0x6F,0x80,0x28,0x00,0x28,0x40,0x38,0x40,0x2F,0x80,0x00,0x00,0x00,0x00}},
  {0x5F55,{0x00,0x00,0x00,0x00,0x7F,0x00,0x01,0x00,0x7F,0x00,0xFF,0x80,0x08,0x00,0x6F,0x80,0x3A,0x00,0xF9,0x80,0x00,0x00,0x00,0x00}},
  {0x5931,{0x00,0x00,0x00,0x00,0x28,0x00,0x7F,0x80,0x88,0x00,0xFF,0x80,0x18,0x00,0x14,0x00,0x22,0x00,0xC1,0x80,0x00,0x00,0x00,0x00}},
  {0x8D25,{0x00,0x00,0x00,0x00,0x72,0x00,0x57,0x80,0x75,0x00,0x7D,0x00,0x75,0x00,0x32,0x00,0x4F,0x00,0x48,0x80,0x00,0x00,0x00,0x00}},
  {0x9608,{0x00,0x00,0x20,0x00,0x1F,0x80,0x47,0x80,0x7F,0x80,0x7C,0x80,0x6D,0x80,0x7B,0x80,0x7F,0x80,0x43,0x80,0x00,0x00,0x00,0x00}},
  {0x503C,{0x00,0x00,0x24,0x00,0x3F,0x80,0x5F,0x00,0x51,0x00,0xDF,0x00,0xDF,0x00,0x51,0x00,0x5F,0x00,0x7F,0x80,0x00,0x00,0x00,0x00}},
  {0x4F59,{0x00,0x00,0x00,0x00,0x08,0x00,0x36,0x00,0x7F,0x80,0x88,0x00,0xFF,0x80,0x2A,0x00,0x49,0x00,0xB8,0x80,0x00,0x00,0x00,0x00}},
  {0x91CF,{0x00,0x00,0x00,0x00,0x3F,0x00,0x3F,0x00,0x3F,0x00,0x7F,0x80,0x3F,0x00,0x3F,0x00,0x7F,0x80,0x7F,0x80,0x00,0x00,0x00,0x00}},
  {0x4F4E,{0x00,0x00,0x21,0x80,0x5E,0x00,0x54,0x00,0xD4,0x00,0xDF,0x80,0x54,0x00,0x54,0x00,0x5A,0x80,0x55,0x80,0x00,0x00,0x00,0x00}},
  {0x9AD8,{0x00,0x00,0x0C,0x00,0xFF,0x80,0x3F,0x00,0x3F,0x00,0x00,0x00,0x7F,0x80,0x5E,0x80,0x5E,0x80,0x43,0x80,0x00,0x00,0x00,0x00}},
  {0x4F20,{0x00,0x00,0x00,0x00,0x24,0x00,0x5F,0x80,0x48,0x00,0xFF,0x80,0xDF,0x00,0x52,0x00,0x4C,0x00,0x42,0x00,0x00,0x00,0x00,0x00}},
  {0x611F,{0x00,0x00,0x05,0x00,0x7F,0x80,0x7D,0x00,0x46,0x00,0x7A,0x80,0xBD,0x80,0x69,0x00,0x62,0x80,0xBE,0x80,0x00,0x00,0x00,0x00}},
  {0x8BE6,{0x00,0x00,0x91,0x00,0x4A,0x00,0x5F,0x80,0xC4,0x00,0x5F,0x80,0x44,0x00,0x5F,0x80,0x64,0x00,0x44,0x00,0x00,0x00,0x00,0x00}},
  {0x60C5,{0x00,0x00,0x42,0x00,0x4F,0xC0,0xFF,0x80,0xDF,0xC0,0xDF,0x80,0x5F,0x80,0x50,0x80,0x5F,0x80,0x53,0x80,0x00,0x00,0x00,0x00}},
  {0x9884,{0x00,0x00,0x00,0x00,0xFF,0x80,0x12,0x00,0x6F,0x80,0xF8,0x80,0x3A,0x80,0x2A,0x80,0x2E,0x80,0xF9,0x80,0x00,0x00,0x00,0x00}},
  {0x70ED,{0x00,0x00,0x00,0x00,0x48,0x00,0xFF,0x00,0x79,0x00,0xCD,0x00,0x53,0x80,0xF1,0x80,0x65,0x00,0x94,0x80,0x00,0x00,0x00,0x00}},
  {0x79D2,{0x00,0x00,0x02,0x00,0x73,0x00,0x26,0x80,0x76,0x80,0x2A,0x00,0x72,0x80,0x61,0x00,0x26,0x00,0x38,0x00,0x00,0x00,0x00,0x00}},
  {0x91C7,{0x00,0x00,0x00,0x00,0x7F,0x00,0x59,0x00,0x2A,0x00,0xFF,0x80,0x1C,0x00,0x2A,0x00,0xC9,0x80,0x08,0x00,0x00,0x00,0x00,0x00}},
  {0x6837,{0x00,0x00,0x29,0x00,0x25,0x00,0x7F,0x80,0x22,0x00,0x3F,0x80,0x72,0x00,0x7F,0x80,0x22,0x00,0x22,0x00,0x00,0x00,0x00,0x00}},
  {0x5B58,{0x00,0x00,0x08,0x00,0x7F,0x80,0x10,0x00,0x2F,0x00,0x23,0x00,0x7F,0x80,0x22,0x00,0x22,0x00,0x26,0x00,0x00,0x00,0x00,0x00}},
  {0x50A8,{0x00,0x00,0x00,0x00,0x22,0x00,0x5E,0x80,0x43,0x00,0xFF,0xC0,0xD3,0x80,0x5C,0x80,0x5F,0x80,0x57,0x80,0x44,0x80,0x00,0x00}},
  {0x6B63,{0x00,0x00,0x00,0x00,0x7F,0x80,0x04,0x00,0x24,0x00,0x27,0x00,0x24,0x00,0x24,0x00,0x24,0x00,0x7F,0x80,0x00,0x00,0x00,0x00}},
  {0x5E38,{0x00,0x00,0x00,0x00,0x3B,0x00,0x7F,0x80,0x40,0x80,0x7F,0x80,0x3F,0x00,0x3F,0x80,0x24,0x80,0x25,0x80,0x04,0x00,0x00,0x00}},
  {0x7CFB,{0x00,0x00,0x00,0x00,0x7F,0x00,0x13,0x00,0x7C,0x00,0x13,0x00,0x7F,0x80,0x2A,0x00,0x49,0x00,0x98,0x80,0x00,0x00,0x00,0x00}},
  {0x7EDF,{0x00,0x00,0x00,0x00,0x46,0x00,0x9F,0x80,0xA9,0x00,0xDF,0x80,0xEA,0x00,0x0A,0x80,0xF2,0x80,0x23,0x80,0x00,0x00,0x00,0x00}},
  {0x72B6,{0x00,0x00,0x00,0x00,0x25,0x00,0xA4,0x80,0x7F,0x80,0x24,0x00,0x66,0x00,0xAA,0x00,0x29,0x00,0x30,0x80,0x00,0x00,0x00,0x00}},
  {0x6001,{0x00,0x00,0x00,0x00,0x08,0x00,0xFF,0x80,0x18,0x00,0x36,0x00,0xC9,0x80,0x7F,0x00,0x62,0x80,0xBE,0x00,0x00,0x00,0x00,0x00}},
  {0x7F51,{0x00,0x00,0x00,0x00,0x7F,0x80,0x40,0x80,0x69,0x80,0x56,0x80,0x56,0x80,0x69,0x80,0x60,0x80,0x43,0x80,0x00,0x00,0x00,0x00}},
  {0x7EDC,{0x00,0x00,0x48,0x00,0x8F,0x00,0xB9,0x00,0xDA,0x00,0x8E,0x00,0xF1,0x80,0x9F,0x00,0xD1,0x00,0x1F,0x00,0x00,0x00,0x00,0x00}},
  {0x5185,{0x00,0x00,0x08,0x00,0x08,0x00,0x7F,0x00,0x49,0x00,0x49,0x00,0x55,0x00,0x63,0x00,0x41,0x00,0x47,0x00,0x00,0x00,0x00,0x00}},
  {0x5207,{0x00,0x00,0x40,0x00,0x5F,0x00,0x49,0x00,0xE9,0x00,0x49,0x00,0x49,0x00,0x69,0x00,0x51,0x00,0x27,0x00,0x00,0x00,0x00,0x00}},
  {0x6362,{0x00,0x00,0x44,0x00,0x4F,0x00,0xD1,0x00,0x7F,0x80,0x52,0x80,0xD2,0x80,0x7F,0xC0,0x46,0x00,0xF9,0xC0,0x00,0x00,0x00,0x00}},
  {0x9875,{0x00,0x00,0x00,0x00,0xFF,0x80,0x08,0x00,0x7F,0x00,0x49,0x00,0x49,0x00,0x49,0x00,0x36,0x00,0xC1,0x80,0x00,0x00,0x00,0x00}},
};

const Glyph12 *findCnGlyph(uint16_t codepoint) {
  for (const auto &glyph : kCnGlyphs) {
    if (glyph.codepoint == codepoint) return &glyph;
  }
  return nullptr;
}

uint16_t nextUtf8Codepoint(const char **text) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(*text);
  if (*p < 0x80) {
    (*text)++;
    return *p;
  }
  if ((*p & 0xF0) == 0xE0 && p[1] && p[2]) {
    const uint16_t cp = static_cast<uint16_t>((*p & 0x0F) << 12 | (p[1] & 0x3F) << 6 | (p[2] & 0x3F));
    *text += 3;
    return cp;
  }
  (*text)++;
  return '?';
}

void lcdChar(int x, int y, char c, uint16_t fg, uint16_t bg, uint8_t scale = 1) {
  const uint8_t *g = glyph5x7(c);
  for (uint8_t col = 0; col < 6; col++) {
    const uint8_t bits = col < 5 ? g[col] : 0;
    for (uint8_t row = 0; row < 8; row++) {
      const uint16_t color = (bits & (1U << row)) ? fg : bg;
      if (scale == 1) {
        lcdPixel(x + col, y + row, color);
      } else {
        lcdFill(x + col * scale, y + row * scale, x + col * scale + scale - 1, y + row * scale + scale - 1, color);
      }
    }
  }
}

void lcdCnChar(int x, int y, uint16_t codepoint, uint16_t fg, uint16_t bg) {
  const Glyph12 *glyph = findCnGlyph(codepoint);
  for (uint8_t row = 0; row < 12; row++) {
    const uint16_t bits = glyph ? (static_cast<uint16_t>(glyph->data[row * 2]) << 8 | glyph->data[row * 2 + 1]) : 0;
    for (uint8_t col = 0; col < 12; col++) {
      lcdPixel(x + col, y + row, (bits & (1U << (15 - col))) ? fg : bg);
    }
  }
}

void lcdText(int x, int y, const char *text, uint16_t fg, uint16_t bg, uint8_t scale = 1) {
  while (*text && x < static_cast<int>(kLcdWidth)) {
    lcdChar(x, y, *text++, fg, bg, scale);
    x += 6 * scale;
  }
}

void lcdTextUtf8(int x, int y, const char *text, uint16_t fg, uint16_t bg) {
  const char *p = text;
  while (*p && x < static_cast<int>(kLcdWidth)) {
    const uint16_t cp = nextUtf8Codepoint(&p);
    if (cp < 0x80) {
      lcdChar(x, y + 2, static_cast<char>(cp), fg, bg, 1);
      x += 6;
    } else {
      lcdCnChar(x, y, cp, fg, bg);
      x += 12;
    }
  }
}

void lcdInit() {
  pinMode(static_cast<int>(kLcdDc), OUTPUT);
  pinMode(static_cast<int>(kLcdRst), OUTPUT);
  pinMode(static_cast<int>(kLcdBl), OUTPUT);
  ledcSetup(kLcdBlPwmChannel, kLcdBlPwmHz, kLcdBlPwmBits);
  ledcAttachPin(static_cast<int>(kLcdBl), kLcdBlPwmChannel);
  ledcWrite(kLcdBlPwmChannel, 0);
  digitalWrite(static_cast<int>(kLcdRst), LOW);
  delay(120);
  digitalWrite(static_cast<int>(kLcdRst), HIGH);
  delay(120);

  spi_bus_config_t bus = {};
  bus.mosi_io_num = kLcdMosi;
  bus.miso_io_num = kLcdMiso;
  bus.sclk_io_num = kLcdSclk;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = kLcdWidth * kLcdHeight * 2;
  esp_err_t ret = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    Serial.printf("LCD SPI bus init failed: %d\n", ret);
    return;
  }

  spi_device_interface_config_t dev = {};
  dev.clock_speed_hz = 40000000;
  dev.mode = 0;
  dev.spics_io_num = kLcdCs;
  dev.queue_size = 4;
  ret = spi_bus_add_device(SPI2_HOST, &dev, &gLcd);
  if (ret != ESP_OK) {
    Serial.printf("LCD SPI add device failed: %d\n", ret);
    return;
  }

  struct InitCmd { uint8_t cmd; uint8_t data[16]; uint8_t len; };
  const InitCmd init[] = {
      {0x11, {0}, 0x80}, {0x21, {0}, 0x80}, {0xB1, {0x05,0x3A,0x3A}, 3}, {0xB2, {0x05,0x3A,0x3A}, 3},
      {0xB3, {0x05,0x3A,0x3A,0x05,0x3A,0x3A}, 6}, {0xB4, {0x03}, 1}, {0xC0, {0x62,0x02,0x04}, 3}, {0xC1, {0xC0}, 1},
      {0xC2, {0x0D,0x00}, 2}, {0xC3, {0x8D,0x6A}, 2}, {0xC4, {0x8D,0xEE}, 2}, {0xC5, {0x0E}, 1},
      {0xE0, {0x10,0x0E,0x02,0x03,0x0E,0x07,0x02,0x07,0x0A,0x12,0x27,0x37,0x00,0x0D,0x0E,0x10}, 16},
      {0xE1, {0x10,0x0E,0x03,0x03,0x0F,0x06,0x02,0x08,0x0A,0x13,0x26,0x36,0x00,0x0D,0x0E,0x10}, 16},
      {0x3A, {0x05}, 1}, {0x36, {0xA8}, 1}, {0x29, {0}, 0x80}, {0, {0}, 0xFF}};
  for (uint8_t i = 0; init[i].len != 0xFF; i++) {
    lcdCmd(init[i].cmd);
    lcdData(init[i].data, init[i].len & 0x1F);
    if (init[i].len & 0x80) delay(120);
  }
  lcdFill(0, 0, kLcdWidth - 1, kLcdHeight - 1, kBlack);
  lcdPresent();
  noteUserActivity();
}

uint16_t sgpHumidityTicks(float humidityPct) {
  return static_cast<uint16_t>(lroundf(constrain(humidityPct, 0.0f, 100.0f) * 65535.0f / 100.0f));
}

uint16_t sgpTemperatureTicks(float tempC) {
  return static_cast<uint16_t>(lroundf((constrain(tempC, -45.0f, 130.0f) + 45.0f) * 65535.0f / 175.0f));
}

String ipString() {
  if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
  return WiFi.softAPIP().toString();
}

String networkMode() {
  return WiFi.status() == WL_CONNECTED ? "STA" : "AP";
}

void appendJsonString(String &json, const char *value) {
  json += "\"";
  if (value) {
    for (const char *p = value; *p; p++) {
      if (*p == '"' || *p == '\\') json += "\\";
      if (static_cast<uint8_t>(*p) >= 0x20) json += *p;
    }
  }
  json += "\"";
}

const char *levelText(const char *level) {
  if (strcmp(level, "bad") == 0) return "bad";
  if (strcmp(level, "warn") == 0) return "warn";
  return "good";
}

void appendMeaning(String &json, const char *key, const char *level, const char *text) {
  json += "\"";
  json += key;
  json += "\":{\"level\":\"";
  json += levelText(level);
  json += "\",\"text\":";
  appendJsonString(json, text);
  json += "}";
}

bool epochIsValid(time_t epoch) {
  return epoch >= static_cast<time_t>(kTimeValidEpoch);
}

void updateTimeStateFromRtc() {
  const time_t now = time(nullptr);
  if (!epochIsValid(now)) return;
  gTimeSynced = true;
  gLastSyncedEpoch = now;
  gLastTimeSyncOkMs = millis();
}

void maintainTimeSync() {
  if (WiFi.status() != WL_CONNECTED) return;
  const uint32_t nowMs = millis();
  const bool needsInitialSync = !gTimeSyncStarted;
  const bool needsRetry = !gTimeSynced && nowMs - gLastTimeSyncAttemptMs >= kNtpRetryMs;
  const bool needsRefresh = gTimeSynced && nowMs - gLastTimeSyncAttemptMs >= kNtpRefreshMs;
  if (needsInitialSync || needsRetry || needsRefresh) {
    configTzTime(kTimeZone, kNtpServer1, kNtpServer2, kNtpServer3);
    gTimeSyncStarted = true;
    gLastTimeSyncAttemptMs = nowMs;
  }
  updateTimeStateFromRtc();
}

uint32_t currentMinuteKey() {
  const time_t now = time(nullptr);
  if (epochIsValid(now)) return static_cast<uint32_t>(now / 60);
  return millis() / 60000UL;
}

void formatLocalTime(char *buffer, size_t size, time_t epoch) {
  if (!buffer || size == 0) return;
  if (!epochIsValid(epoch)) {
    snprintf(buffer, size, "unsynced");
    return;
  }
  struct tm local = {};
  localtime_r(&epoch, &local);
  strftime(buffer, size, "%Y-%m-%d %H:%M:%S", &local);
}

void appendTimeStatus(String &json) {
  const time_t now = time(nullptr);
  const bool valid = epochIsValid(now);
  char local[24];
  formatLocalTime(local, sizeof(local), now);
  json += "\"time\":{\"sync_time\":";
  json += valid ? "true" : "false";
  json += ",\"time_source\":\"";
  json += valid ? "ntp_rtc" : "uptime";
  json += "\",\"epoch_s\":";
  json += valid ? String(static_cast<unsigned long>(now)) : "0";
  json += ",\"epoch_minute\":";
  json += String(currentMinuteKey());
  json += ",\"local_time\":";
  appendJsonString(json, local);
  json += ",\"last_sync_ms\":";
  json += String(gLastTimeSyncOkMs);
  json += "}";
}

const char *co2Level() {
  if (env.scdCo2 >= cfg.co2BadPpm) return "bad";
  if (env.scdCo2 >= cfg.co2WarnPpm) return "warn";
  return "good";
}

const char *vocLevel() {
  if (env.vocIndex >= cfg.vocBad) return "bad";
  if (env.vocIndex >= cfg.vocWarn) return "warn";
  return "good";
}

const char *noxLevel() {
  if (env.noxIndex >= cfg.noxBad) return "bad";
  if (env.noxIndex >= cfg.noxWarn) return "warn";
  return "good";
}

const char *humidityLevel() {
  if (env.shtHumidity < cfg.humidityLow || env.shtHumidity > cfg.humidityHigh) return "warn";
  return "good";
}

const char *tempLevel() {
  if (env.shtTempC < cfg.tempLowC || env.shtTempC > cfg.tempHighC) return "warn";
  return "good";
}

const char *luxLevel() {
  if (env.lux < cfg.luxLow || env.lux > cfg.luxHigh) return "warn";
  return "good";
}

const char *shtPrecisionName() {
  if (cfg.shtPrecision == 2) return "high";
  if (cfg.shtPrecision == 1) return "medium";
  return "low";
}

const char *bhModeName() {
  if (cfg.bhMode == 2) return "continuous_low_res_4lx";
  if (cfg.bhMode == 1) return "continuous_high_res_0_5lx";
  return "continuous_high_res_1lx";
}

const char *powerModeName() {
  if (cfg.powerMode == POWER_LOW) return "low_power";
  if (cfg.powerMode == POWER_PERFORMANCE) return "performance";
  return "balanced";
}

uint32_t sensorIntervalMs() {
  if (cfg.powerMode == POWER_LOW) return cfg.lowPowerSensorIntervalMs;
  if (cfg.powerMode == POWER_PERFORMANCE) return max<uint32_t>(500UL, cfg.fastIntervalMs / 2UL);
  return cfg.fastIntervalMs;
}

bool sleepEligible() {
  return cfg.powerMode == POWER_LOW && gBacklightOn == false && cfg.wifiStaSleep;
}

bool lowPowerStaOnly() {
  return cfg.powerMode == POWER_LOW && strlen(kStaSsid) > 0;
}

uint32_t targetCpuMhz() {
  if (cfg.powerMode == POWER_LOW) return 80;
  if (cfg.powerMode == POWER_PERFORMANCE) return 240;
  return 160;
}

BH1750::Mode currentBhMode() {
  if (cfg.bhMode == 2) return BH1750::CONTINUOUS_LOW_RES_MODE;
  if (cfg.bhMode == 1) return BH1750::CONTINUOUS_HIGH_RES_MODE_2;
  return BH1750::CONTINUOUS_HIGH_RES_MODE;
}

void buildMeaning(char *co2, size_t co2Size, char *voc, size_t vocSize, char *nox, size_t noxSize,
                  char *temp, size_t tempSize, char *humidity, size_t humiditySize, char *lux, size_t luxSize) {
  snprintf(co2, co2Size, env.scdCo2 < cfg.co2WarnPpm ? "通风良好，CO2 处于低位" : (env.scdCo2 < cfg.co2BadPpm ? "CO2 偏高，建议加强通风" : "空气闷，建议立即通风"));
  if (env.sgpConditioning > 0) {
    snprintf(voc, vocSize, "SGP41 正在预热，VOC 指数暂时不稳定");
    snprintf(nox, noxSize, "SGP41 正在预热，NOx 指数暂时不稳定");
  } else {
    snprintf(voc, vocSize, env.vocIndex < cfg.vocWarn ? "VOC 接近正常基线" : (env.vocIndex < cfg.vocBad ? "VOC 偏高，检查异味或挥发物来源" : "VOC 很高，建议通风并排查污染源"));
    snprintf(nox, noxSize, env.noxIndex < cfg.noxWarn ? "NOx 接近清洁空气基线" : (env.noxIndex < cfg.noxBad ? "NOx 偏高，可能有燃烧源或室外污染进入" : "NOx 很高，检查通风和污染源"));
  }
  snprintf(temp, tempSize, strcmp(tempLevel(), "good") == 0 ? "温度在设定舒适区间内" : "温度超出设定舒适区间");
  snprintf(humidity, humiditySize, strcmp(humidityLevel(), "good") == 0 ? "湿度在设定舒适区间内" : "湿度超出设定舒适区间");
  snprintf(lux, luxSize, strcmp(luxLevel(), "good") == 0 ? "光照在设定区间内" : "光照超出设定区间");
}

void pushHistory(const SampleRow &row) {
  gHistory[gHistoryHead] = row;
  gHistoryHead = (gHistoryHead + 1) % kHistoryCapacity;
  if (gHistoryCount < kHistoryCapacity) gHistoryCount++;
}

const SampleRow &historyAt(size_t index) {
  const size_t start = (gHistoryHead + kHistoryCapacity - gHistoryCount) % kHistoryCapacity;
  return gHistory[(start + index) % kHistoryCapacity];
}

SampleRow makeSample() {
  SampleRow row;
  row.seq = ++gSampleSeq;
  row.uptimeMs = millis();
  row.co2 = env.scdCo2;
  row.tempC = env.shtTempC;
  row.humidity = env.shtHumidity;
  row.voc = env.vocIndex;
  row.nox = env.noxIndex;
  row.lux = env.lux;
  row.ok = env.shtOnline && env.scdOnline && env.sgpOnline && env.bhOnline &&
           env.shtOk > 0 && env.scdOk > 0 && env.sgpOk > 0 && env.bhOk > 0 &&
           env.sgpConditioning == 0 &&
           row.co2 > 0 &&
           isfinite(row.tempC) && row.tempC > -40.0f && row.tempC < 85.0f &&
           isfinite(row.humidity) && row.humidity >= 0.0f && row.humidity <= 100.0f &&
           env.srawVoc > 0 && env.srawNox > 0 &&
           row.voc >= 0 && row.nox >= 0 &&
           isfinite(row.lux) && row.lux > 0.0f;
  return row;
}

size_t minuteRingOffset(uint32_t index) {
  return static_cast<size_t>(index) * sizeof(MinuteRingRecord);
}

void minuteRingSegmentPath(uint32_t segment, char *path, size_t pathSize) {
  snprintf(path, pathSize, "/env%03lu.bin", static_cast<unsigned long>(segment));
}

bool writeMinuteRingHeader(File &file) {
  if (!file.seek(0, SeekSet)) return false;
  return file.write(reinterpret_cast<const uint8_t *>(&gMinuteRing), sizeof(gMinuteRing)) == sizeof(gMinuteRing);
}

bool saveMinuteRingHeader() {
  File file = LittleFS.open(kMinuteRingPath, "w");
  if (!file) return false;
  bool ok = writeMinuteRingHeader(file);
  file.flush();
  ok = ok && file.getWriteError() == 0;
  file.close();
  return ok;
}

bool readMinuteRingRecord(uint32_t logicalIndex, MinuteRingRecord *record) {
  if (!gMinuteRingReady || !record || logicalIndex >= gMinuteRing.count) return false;
  const uint32_t start = (gMinuteRing.writeIndex + gMinuteRing.capacity - gMinuteRing.count) % gMinuteRing.capacity;
  const uint32_t slot = (start + logicalIndex) % gMinuteRing.capacity;
  const uint32_t segment = slot / kMinuteRingSegmentRecords;
  const uint32_t segmentSlot = slot % kMinuteRingSegmentRecords;
  char path[16];
  minuteRingSegmentPath(segment, path, sizeof(path));
  File file = LittleFS.open(path, "r");
  if (!file) return false;
  const bool ok = file.seek(minuteRingOffset(segmentSlot), SeekSet) &&
                  file.read(reinterpret_cast<uint8_t *>(record), sizeof(*record)) == sizeof(*record);
  file.close();
  return ok;
}

bool readMinuteRingSlotCached(uint32_t slot, MinuteRingRecord *record, File &file, int32_t &openSegment) {
  if (!gMinuteRingReady || !record || slot >= gMinuteRing.capacity) return false;
  const uint32_t segment = slot / kMinuteRingSegmentRecords;
  const uint32_t segmentSlot = slot % kMinuteRingSegmentRecords;
  if (openSegment != static_cast<int32_t>(segment)) {
    if (file) file.close();
    char path[16];
    minuteRingSegmentPath(segment, path, sizeof(path));
    file = LittleFS.open(path, "r");
    openSegment = file ? static_cast<int32_t>(segment) : -1;
  }
  if (!file) return false;
  const size_t offset = minuteRingOffset(segmentSlot);
  if (file.position() != offset) {
    if (!file.seek(offset, SeekSet)) return false;
  }
  return file.read(reinterpret_cast<uint8_t *>(record), sizeof(*record)) == sizeof(*record);
}

bool createMinuteRingFile() {
  LittleFS.remove(kMinuteRingPath);
  LittleFS.remove(kLegacyMinuteRingPath);
  for (uint32_t i = 0; i < kMinuteRingSegments; i++) {
    char path[16];
    minuteRingSegmentPath(i, path, sizeof(path));
    LittleFS.remove(path);
    if ((i & 0x0F) == 0) delay(1);
  }
  File file = LittleFS.open(kMinuteRingPath, "w+");
  if (!file) return false;
  gMinuteRing = MinuteRingHeader();
  bool ok = writeMinuteRingHeader(file);
  file.flush();
  ok = ok && file.getWriteError() == 0;
  file.close();
  return ok;
}

bool initMinuteRing() {
  if (!gFsReady) return false;
  bool valid = false;
  if (LittleFS.exists(kMinuteRingPath)) {
    File file = LittleFS.open(kMinuteRingPath, "r");
    if (file && file.read(reinterpret_cast<uint8_t *>(&gMinuteRing), sizeof(gMinuteRing)) == sizeof(gMinuteRing)) {
      valid = gMinuteRing.magic == kMinuteRingMagic &&
              gMinuteRing.version == kMinuteRingVersion &&
              gMinuteRing.capacity == kMinuteRingCapacity &&
              gMinuteRing.writeIndex < kMinuteRingCapacity &&
              gMinuteRing.count <= kMinuteRingCapacity;
    }
    if (file) file.close();
  }
  if (!valid) {
    valid = createMinuteRingFile();
  }
  gMinuteRingReady = valid;
  if (!valid) gStorageFailures++;
  gPersistedSamples = gMinuteRing.totalWrites;
  return valid;
}

bool clearMinuteLog() {
  if (!gFsReady) return false;
  gMinuteAgg = MinuteAggregate();
  gHistoryHead = 0;
  gHistoryCount = 0;
  gSampleSeq = 0;
  gLastSampleMs = 0;
  gLastPersistDurationMs = 0;
  gMaxPersistDurationMs = 0;
  gDroppedInvalidSamples = 0;
  gDroppedUnsyncedSamples = 0;
  gDroppedTimeDomainSamples = 0;
  const bool ok = createMinuteRingFile();
  gMinuteRingReady = ok;
  if (ok) {
    gPersistedSamples = 0;
  } else {
    gStorageFailures++;
  }
  return ok;
}

bool appendMinuteRecord(const MinuteRingRecord &record) {
  if (!gFsReady || !gMinuteRingReady) return false;
  const uint32_t startMs = millis();
  const uint32_t segment = gMinuteRing.writeIndex / kMinuteRingSegmentRecords;
  const uint32_t segmentSlot = gMinuteRing.writeIndex % kMinuteRingSegmentRecords;
  char path[16];
  minuteRingSegmentPath(segment, path, sizeof(path));
  if (segmentSlot == 0) {
    LittleFS.remove(path);
  }
  File file = LittleFS.open(path, segmentSlot == 0 ? "w" : "a");
  if (!file) {
    gStorageFailures++;
    return false;
  }
  bool ok = file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) == sizeof(record);
  file.flush();
  ok = ok && file.getWriteError() == 0;
  file.close();
  if (ok) {
    gMinuteRing.writeIndex = (gMinuteRing.writeIndex + 1) % gMinuteRing.capacity;
    if (gMinuteRing.count < gMinuteRing.capacity) gMinuteRing.count++;
    gMinuteRing.totalWrites++;
    ok = saveMinuteRingHeader();
  }
  if (ok) {
    gPersistedSamples = gMinuteRing.totalWrites;
    gLastSampleMs = millis();
  } else {
    gStorageFailures++;
  }
  gLastPersistDurationMs = millis() - startMs;
  if (gLastPersistDurationMs > gMaxPersistDurationMs) {
    gMaxPersistDurationMs = gLastPersistDurationMs;
  }
  return ok;
}

void resetMinuteAggregate(uint32_t minute) {
  gMinuteAgg = MinuteAggregate();
  gMinuteAgg.minute = minute;
}

bool appendMinuteAggregate() {
  if (!gFsReady || gMinuteAgg.count == 0) return false;
  const double count = static_cast<double>(gMinuteAgg.count);
  MinuteRingRecord record;
  record.minute = gMinuteAgg.minute;
  record.count = static_cast<uint16_t>(min<uint32_t>(gMinuteAgg.count, 65535UL));
  record.okPermille = static_cast<uint16_t>(lround((static_cast<double>(gMinuteAgg.okCount) * 1000.0) / count));
  record.co2 = gMinuteAgg.co2Sum / count;
  record.tempC = gMinuteAgg.tempSum / count;
  record.humidity = gMinuteAgg.humiditySum / count;
  record.voc = gMinuteAgg.vocSum / count;
  record.nox = gMinuteAgg.noxSum / count;
  record.lux = gMinuteAgg.luxSum / count;
  return appendMinuteRecord(record);
}

bool minuteKeyIsEpoch(uint32_t minute) {
  return minute >= kEpochMinuteFloor;
}

bool minuteRecordHasUsableValues(const MinuteRingRecord &record) {
  return record.count > 0 &&
         record.okPermille > 0 &&
         record.co2 > 0.0f &&
         isfinite(record.tempC) && record.tempC > -40.0f && record.tempC < 85.0f &&
         isfinite(record.humidity) && record.humidity > 0.0f && record.humidity <= 100.0f &&
         isfinite(record.voc) && record.voc > 0.0f &&
         isfinite(record.nox) && record.nox >= 0.0f &&
         isfinite(record.lux) && record.lux > 0.0f;
}

bool shouldHoldLoggingForTimeSync() {
  return !gTimeSynced && strlen(kStaSsid) > 0;
}

void updateMinuteAggregate(const SampleRow &row) {
  const uint32_t minute = currentMinuteKey();
  if (gMinuteAgg.count == 0) {
    resetMinuteAggregate(minute);
  } else if (minuteKeyIsEpoch(gMinuteAgg.minute) != minuteKeyIsEpoch(minute)) {
    gDroppedTimeDomainSamples += gMinuteAgg.count;
    resetMinuteAggregate(minute);
  } else if (minute != gMinuteAgg.minute) {
    appendMinuteAggregate();
    resetMinuteAggregate(minute);
  }
  gMinuteAgg.count++;
  gMinuteAgg.co2Sum += row.co2;
  gMinuteAgg.tempSum += row.tempC;
  gMinuteAgg.humiditySum += row.humidity;
  gMinuteAgg.vocSum += row.voc;
  gMinuteAgg.noxSum += row.nox;
  gMinuteAgg.luxSum += row.lux;
  if (row.ok) gMinuteAgg.okCount++;
}

void publishSampleIfDue() {
  static unsigned long lastSample = 0;
  if (millis() - lastSample < cfg.logIntervalMs) return;
  lastSample = millis();
  if (shouldHoldLoggingForTimeSync()) {
    gDroppedUnsyncedSamples++;
    return;
  }
  const SampleRow row = makeSample();
  if (!row.ok) {
    gDroppedInvalidSamples++;
    return;
  }
  pushHistory(row);
  updateMinuteAggregate(row);
}

bool parseUintArg(const char *name, uint32_t minValue, uint32_t maxValue, uint32_t *out) {
  if (!server.hasArg(name)) return true;
  const String value = server.arg(name);
  if (value.length() == 0) return false;
  char *end = nullptr;
  const unsigned long parsed = strtoul(value.c_str(), &end, 10);
  if (!end || *end != '\0' || parsed < minValue || parsed > maxValue) return false;
  *out = static_cast<uint32_t>(parsed);
  return true;
}

bool parseFloatArg(const char *name, float minValue, float maxValue, float *out) {
  if (!server.hasArg(name)) return true;
  const String value = server.arg(name);
  if (value.length() == 0) return false;
  char *end = nullptr;
  const float parsed = strtof(value.c_str(), &end);
  if (!end || *end != '\0' || parsed < minValue || parsed > maxValue) return false;
  *out = parsed;
  return true;
}

bool parseBoolArg(const char *name, bool *out) {
  if (!server.hasArg(name)) return true;
  String value = server.arg(name);
  value.toLowerCase();
  if (value == "1" || value == "true" || value == "on") {
    *out = true;
    return true;
  }
  if (value == "0" || value == "false" || value == "off") {
    *out = false;
    return true;
  }
  return false;
}

bool parseEnumArg(const char *name, uint8_t *out) {
  if (!server.hasArg(name)) return true;
  String value = server.arg(name);
  value.toLowerCase();
  if (value == "low_power" || value == "low" || value == "0") {
    *out = POWER_LOW;
    return true;
  }
  if (value == "balanced" || value == "1") {
    *out = POWER_BALANCED;
    return true;
  }
  if (value == "performance" || value == "2") {
    *out = POWER_PERFORMANCE;
    return true;
  }
  return false;
}

uint32_t clampU32(uint32_t value, uint32_t minValue, uint32_t maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

float clampFloat(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

void normalizeConfig() {
  cfg.powerMode = static_cast<uint8_t>(clampU32(cfg.powerMode, POWER_LOW, POWER_PERFORMANCE));
  cfg.fastIntervalMs = clampU32(cfg.fastIntervalMs, 500UL, 10000UL);
  cfg.scdIntervalMs = clampU32(cfg.scdIntervalMs, 5000UL, 60000UL);
  cfg.logIntervalMs = clampU32(cfg.logIntervalMs, 500UL, 600000UL);
  cfg.liveRefreshMs = clampU32(cfg.liveRefreshMs, 250UL, 10000UL);
  cfg.lcdBrightnessPct = static_cast<uint8_t>(clampU32(cfg.lcdBrightnessPct, 0UL, 100UL));
  cfg.backlightTimeoutMs = clampU32(cfg.backlightTimeoutMs, 5000UL, 600000UL);
  cfg.lowPowerSensorIntervalMs = clampU32(cfg.lowPowerSensorIntervalMs, 30000UL, 600000UL);
  cfg.co2WarnPpm = static_cast<uint16_t>(clampU32(cfg.co2WarnPpm, 400UL, 4999UL));
  cfg.co2BadPpm = static_cast<uint16_t>(clampU32(cfg.co2BadPpm, cfg.co2WarnPpm + 1UL, 5000UL));
  cfg.vocWarn = static_cast<uint16_t>(clampU32(cfg.vocWarn, 1UL, 499UL));
  cfg.vocBad = static_cast<uint16_t>(clampU32(cfg.vocBad, cfg.vocWarn + 1UL, 500UL));
  cfg.noxWarn = static_cast<uint16_t>(clampU32(cfg.noxWarn, 1UL, 499UL));
  cfg.noxBad = static_cast<uint16_t>(clampU32(cfg.noxBad, cfg.noxWarn + 1UL, 500UL));
  cfg.tempLowC = clampFloat(cfg.tempLowC, -20.0f, 60.0f);
  cfg.tempHighC = clampFloat(cfg.tempHighC, cfg.tempLowC + 0.5f, 80.0f);
  cfg.humidityLow = clampFloat(cfg.humidityLow, 0.0f, 99.0f);
  cfg.humidityHigh = clampFloat(cfg.humidityHigh, cfg.humidityLow + 1.0f, 100.0f);
  cfg.luxLow = clampFloat(cfg.luxLow, 0.0f, 99999.0f);
  cfg.luxHigh = clampFloat(cfg.luxHigh, cfg.luxLow + 1.0f, 100000.0f);
  cfg.shtPrecision = static_cast<uint8_t>(clampU32(cfg.shtPrecision, 0UL, 2UL));
  cfg.bhMode = static_cast<uint8_t>(clampU32(cfg.bhMode, 0UL, 2UL));
}

bool loadConfig() {
  if (!gFsReady || !LittleFS.exists(kConfigPath)) return false;
  File file = LittleFS.open(kConfigPath, "r");
  if (!file) return false;
  while (file.available()) {
    String line = file.readStringUntil('\n');
    line.trim();
    const int eq = line.indexOf('=');
    if (eq <= 0) continue;
    const String key = line.substring(0, eq);
    const String value = line.substring(eq + 1);
    if (key == "power_mode") {
      if (value == "low_power" || value == "0") cfg.powerMode = POWER_LOW;
      else if (value == "performance" || value == "2") cfg.powerMode = POWER_PERFORMANCE;
      else cfg.powerMode = POWER_BALANCED;
    }
    else if (key == "fast_interval_ms") cfg.fastIntervalMs = value.toInt();
    else if (key == "scd_interval_ms") cfg.scdIntervalMs = value.toInt();
    else if (key == "log_interval_ms") cfg.logIntervalMs = value.toInt();
    else if (key == "live_refresh_ms") cfg.liveRefreshMs = value.toInt();
    else if (key == "lcd_brightness_pct") cfg.lcdBrightnessPct = value.toInt();
    else if (key == "backlight_timeout_ms") cfg.backlightTimeoutMs = value.toInt();
    else if (key == "wifi_sta_sleep") cfg.wifiStaSleep = value.toInt() != 0;
    else if (key == "low_power_sensor_interval_ms") cfg.lowPowerSensorIntervalMs = value.toInt();
    else if (key == "co2_warn_ppm") cfg.co2WarnPpm = value.toInt();
    else if (key == "co2_bad_ppm") cfg.co2BadPpm = value.toInt();
    else if (key == "voc_warn") cfg.vocWarn = value.toInt();
    else if (key == "voc_bad") cfg.vocBad = value.toInt();
    else if (key == "nox_warn") cfg.noxWarn = value.toInt();
    else if (key == "nox_bad") cfg.noxBad = value.toInt();
    else if (key == "temp_low_c") cfg.tempLowC = value.toFloat();
    else if (key == "temp_high_c") cfg.tempHighC = value.toFloat();
    else if (key == "humidity_low") cfg.humidityLow = value.toFloat();
    else if (key == "humidity_high") cfg.humidityHigh = value.toFloat();
    else if (key == "lux_low") cfg.luxLow = value.toFloat();
    else if (key == "lux_high") cfg.luxHigh = value.toFloat();
    else if (key == "sht_precision") cfg.shtPrecision = value.toInt();
    else if (key == "bh1750_mode") cfg.bhMode = value.toInt();
  }
  file.close();
  normalizeConfig();
  return true;
}

bool saveConfig() {
  if (!gFsReady) return false;
  LittleFS.remove(kConfigTmpPath);
  File file = LittleFS.open(kConfigTmpPath, "w");
  if (!file) return false;
  file.printf("power_mode=%s\n", powerModeName());
  file.printf("fast_interval_ms=%lu\n", static_cast<unsigned long>(cfg.fastIntervalMs));
  file.printf("scd_interval_ms=%lu\n", static_cast<unsigned long>(cfg.scdIntervalMs));
  file.printf("log_interval_ms=%lu\n", static_cast<unsigned long>(cfg.logIntervalMs));
  file.printf("live_refresh_ms=%lu\n", static_cast<unsigned long>(cfg.liveRefreshMs));
  file.printf("lcd_brightness_pct=%u\n", cfg.lcdBrightnessPct);
  file.printf("backlight_timeout_ms=%lu\n", static_cast<unsigned long>(cfg.backlightTimeoutMs));
  file.printf("wifi_sta_sleep=%u\n", cfg.wifiStaSleep ? 1 : 0);
  file.printf("low_power_sensor_interval_ms=%lu\n", static_cast<unsigned long>(cfg.lowPowerSensorIntervalMs));
  file.printf("co2_warn_ppm=%u\nco2_bad_ppm=%u\n", cfg.co2WarnPpm, cfg.co2BadPpm);
  file.printf("voc_warn=%u\nvoc_bad=%u\n", cfg.vocWarn, cfg.vocBad);
  file.printf("nox_warn=%u\nnox_bad=%u\n", cfg.noxWarn, cfg.noxBad);
  file.printf("temp_low_c=%.2f\ntemp_high_c=%.2f\n", cfg.tempLowC, cfg.tempHighC);
  file.printf("humidity_low=%.2f\nhumidity_high=%.2f\n", cfg.humidityLow, cfg.humidityHigh);
  file.printf("lux_low=%.2f\nlux_high=%.2f\n", cfg.luxLow, cfg.luxHigh);
  file.printf("sht_precision=%u\nbh1750_mode=%u\n", cfg.shtPrecision, cfg.bhMode);
  file.flush();
  const bool ok = file.getWriteError() == 0;
  file.close();
  if (!ok) {
    LittleFS.remove(kConfigTmpPath);
    return false;
  }
  LittleFS.remove(kConfigPath);
  return LittleFS.rename(kConfigTmpPath, kConfigPath);
}

void initStorage() {
  gFsReady = LittleFS.begin(false);
  if (!gFsReady) {
    gFsReady = LittleFS.begin(true);
  }
  if (!gFsReady) {
    gStorageFailures++;
    Serial.println("[FS] LittleFS mount failed");
    return;
  }
  loadConfig();
  normalizeConfig();
  initMinuteRing();
  Serial.printf("[FS] LittleFS mounted used=%u total=%u ring=%lu/%lu writes=%lu history=%u\n",
                static_cast<unsigned int>(LittleFS.usedBytes()),
                static_cast<unsigned int>(LittleFS.totalBytes()),
                static_cast<unsigned long>(gMinuteRing.count),
                static_cast<unsigned long>(gMinuteRing.capacity),
                static_cast<unsigned long>(gPersistedSamples),
                static_cast<unsigned int>(gHistoryCount));
}

void initSensors() {
  gSht4x.begin(Wire, SHT40_I2C_ADDR_44);
  uint32_t shtSerial = 0;
  env.shtInitErr = gSht4x.serialNumber(shtSerial);
  env.shtOnline = env.shtInitErr == 0;

  gSgp41.begin(Wire);
  uint16_t sgpSerial[3] = {};
  env.sgpInitErr = gSgp41.getSerialNumber(sgpSerial);
  if (env.sgpInitErr == 0) {
    env.sgpInitErr = gSgp41.executeSelfTest(env.sgpSelfTest);
  }
  env.sgpOnline = env.sgpInitErr == 0 && env.sgpSelfTest == 0xD400;

  gScd4x.begin(Wire, SCD41_I2C_ADDR_62);
  delay(30);
  gScd4x.wakeUp();
  gScd4x.stopPeriodicMeasurement();
  gScd4x.reinit();
  delay(30);
  uint64_t scdSerial = 0;
  env.scdInitErr = gScd4x.getSerialNumber(scdSerial);
  if (env.scdInitErr == 0) env.scdInitErr = gScd4x.startPeriodicMeasurement();
  env.scdOnline = env.scdInitErr == 0;

  env.bhAddr = 0x23;
  env.bhOnline = gBh1750.begin(currentBhMode(), env.bhAddr, &Wire);
  if (!env.bhOnline) {
    env.bhAddr = 0x5C;
    env.bhOnline = gBh1750.begin(currentBhMode(), env.bhAddr, &Wire);
  }
}

void readSensorsIfDue() {
  static unsigned long lastFast = 0;
  static unsigned long lastScd = 0;
  const unsigned long now = millis();
  const uint32_t effectiveSensorIntervalMs = sensorIntervalMs();

  if (now - lastFast >= effectiveSensorIntervalMs) {
    lastFast = now;

    if (env.shtOnline) {
      float temp = 0.0f;
      float rh = 0.0f;
      int16_t shtError = 0;
      if (cfg.shtPrecision == 2) {
        shtError = gSht4x.measureHighPrecision(temp, rh);
      } else if (cfg.shtPrecision == 1) {
        shtError = gSht4x.measureMediumPrecision(temp, rh);
      } else {
        shtError = gSht4x.measureLowestPrecision(temp, rh);
      }
      if (shtError == 0) {
        env.shtTempC = temp;
        env.shtHumidity = rh;
        env.shtOk++;
      } else {
        env.shtFail++;
      }
    }

    if (env.sgpOnline) {
      const uint16_t rhTicks = env.shtOk > 0 ? sgpHumidityTicks(env.shtHumidity) : 0x8000;
      const uint16_t tempTicks = env.shtOk > 0 ? sgpTemperatureTicks(env.shtTempC) : 0x6666;
      uint16_t error = 0;
      uint16_t srawVoc = 0;
      uint16_t srawNox = 0;
      if (env.sgpConditioning > 0) {
        error = gSgp41.executeConditioning(rhTicks, tempTicks, srawVoc);
        if (error == 0) env.sgpConditioning--;
      } else {
        error = gSgp41.measureRawSignals(rhTicks, tempTicks, srawVoc, srawNox);
      }
      if (error == 0) {
        env.srawVoc = srawVoc;
        env.srawNox = srawNox;
        env.vocIndex = gVocAlgorithm.process(srawVoc);
        if (env.sgpConditioning == 0) env.noxIndex = gNoxAlgorithm.process(srawNox);
        env.sgpOk++;
      } else {
        env.sgpFail++;
      }
    }

    if (env.bhOnline) {
      const float lux = gBh1750.readLightLevel();
      if (isfinite(lux) && lux >= 0.0f) {
        env.lux = lux;
        env.bhOk++;
      } else {
        env.bhFail++;
      }
    }
  }

  if (env.scdOnline && now - lastScd >= cfg.scdIntervalMs) {
    lastScd = now;
    bool ready = false;
    int16_t error = gScd4x.getDataReadyStatus(ready);
    env.scdReady = ready;
    if (error == 0 && ready) {
      uint16_t co2 = 0;
      float temp = 0.0f;
      float rh = 0.0f;
      error = gScd4x.readMeasurement(co2, temp, rh);
      if (error == 0 && co2 != 0) {
        env.scdCo2 = co2;
        env.scdTempC = temp;
        env.scdHumidity = rh;
        env.scdOk++;
      }
    }
    if (error != 0) env.scdFail++;
  }
}

bool allSensorsOk() {
  return env.shtOnline && env.scdOnline && env.sgpOnline && env.bhOnline;
}

String statusJson() {
  char co2Meaning[160];
  char vocMeaning[180];
  char noxMeaning[180];
  char tempMeaning[160];
  char humidityMeaning[160];
  char luxMeaning[160];
  buildMeaning(co2Meaning, sizeof(co2Meaning), vocMeaning, sizeof(vocMeaning), noxMeaning, sizeof(noxMeaning),
               tempMeaning, sizeof(tempMeaning), humidityMeaning, sizeof(humidityMeaning), luxMeaning, sizeof(luxMeaning));

  String json;
  json.reserve(2600);
  json += "{\"ok\":";
  json += allSensorsOk() ? "true" : "false";
  json += ",\"i2c\":{\"sda\":4,\"scl\":5,\"clock_hz\":400000}";
  json += ",\"network\":{\"mode\":\"";
  json += networkMode();
  json += "\",\"ip\":\"";
  json += ipString();
  json += "\",\"reconnects\":";
  json += String(gWifiReconnects);
  json += ",\"last_reconnect_ms\":";
  json += String(gLastWifiReconnectMs);
  json += "}";
  json += ",\"sht41\":{\"online\":";
  json += env.shtOnline ? "true" : "false";
  json += ",\"temp_c\":";
  json += String(env.shtTempC, 2);
  json += ",\"humidity\":";
  json += String(env.shtHumidity, 2);
  json += ",\"ok\":";
  json += String(env.shtOk);
  json += ",\"fail\":";
  json += String(env.shtFail);
  json += ",\"init_error\":";
  json += String(env.shtInitErr);
  json += "}";
  json += ",\"scd41\":{\"online\":";
  json += env.scdOnline ? "true" : "false";
  json += ",\"ready\":";
  json += env.scdReady ? "true" : "false";
  json += ",\"co2_ppm\":";
  json += String(env.scdCo2);
  json += ",\"temp_c\":";
  json += String(env.scdTempC, 2);
  json += ",\"humidity\":";
  json += String(env.scdHumidity, 2);
  json += ",\"ok\":";
  json += String(env.scdOk);
  json += ",\"fail\":";
  json += String(env.scdFail);
  json += ",\"init_error\":";
  json += String(env.scdInitErr);
  json += "}";
  json += ",\"sgp41\":{\"online\":";
  json += env.sgpOnline ? "true" : "false";
  json += ",\"sraw_voc\":";
  json += String(env.srawVoc);
  json += ",\"sraw_nox\":";
  json += String(env.srawNox);
  json += ",\"voc_index\":";
  json += String(env.vocIndex);
  json += ",\"nox_index\":";
  json += String(env.noxIndex);
  json += ",\"conditioning_s\":";
  json += String(env.sgpConditioning);
  json += ",\"ok\":";
  json += String(env.sgpOk);
  json += ",\"fail\":";
  json += String(env.sgpFail);
  json += ",\"init_error\":";
  json += String(env.sgpInitErr);
  json += ",\"self_test\":";
  json += String(env.sgpSelfTest);
  json += "}";
  json += ",\"bh1750\":{\"online\":";
  json += env.bhOnline ? "true" : "false";
  json += ",\"addr\":";
  json += String(env.bhAddr);
  json += ",\"lux\":";
  json += String(env.lux, 2);
  json += ",\"ok\":";
  json += String(env.bhOk);
  json += ",\"fail\":";
  json += String(env.bhFail);
  json += "}";
  json += ",\"interpretation\":{";
  appendMeaning(json, "co2", co2Level(), co2Meaning);
  json += ",";
  appendMeaning(json, "voc", env.sgpConditioning > 0 ? "warn" : vocLevel(), vocMeaning);
  json += ",";
  appendMeaning(json, "nox", env.sgpConditioning > 0 ? "warn" : noxLevel(), noxMeaning);
  json += ",";
  appendMeaning(json, "temperature", tempLevel(), tempMeaning);
  json += ",";
  appendMeaning(json, "humidity", humidityLevel(), humidityMeaning);
  json += ",";
  appendMeaning(json, "lux", luxLevel(), luxMeaning);
  json += "}";
  json += ",";
  appendTimeStatus(json);
  json += ",\"config\":{\"power_mode\":\"";
  json += powerModeName();
  json += "\",\"fast_interval_ms\":";
  json += String(cfg.fastIntervalMs);
  json += ",\"scd_interval_ms\":";
  json += String(cfg.scdIntervalMs);
  json += ",\"log_interval_ms\":";
  json += String(cfg.logIntervalMs);
  json += ",\"live_refresh_ms\":";
  json += String(cfg.liveRefreshMs);
  json += ",\"lcd_brightness_pct\":";
  json += String(cfg.lcdBrightnessPct);
  json += ",\"backlight_timeout_ms\":";
  json += String(cfg.backlightTimeoutMs);
  json += ",\"wifi_sta_sleep\":";
  json += cfg.wifiStaSleep ? "true" : "false";
  json += ",\"low_power_sensor_interval_ms\":";
  json += String(cfg.lowPowerSensorIntervalMs);
  json += ",\"co2_warn_ppm\":";
  json += String(cfg.co2WarnPpm);
  json += ",\"co2_bad_ppm\":";
  json += String(cfg.co2BadPpm);
  json += ",\"voc_warn\":";
  json += String(cfg.vocWarn);
  json += ",\"voc_bad\":";
  json += String(cfg.vocBad);
  json += ",\"nox_warn\":";
  json += String(cfg.noxWarn);
  json += ",\"nox_bad\":";
  json += String(cfg.noxBad);
  json += ",\"temp_low_c\":";
  json += String(cfg.tempLowC, 2);
  json += ",\"temp_high_c\":";
  json += String(cfg.tempHighC, 2);
  json += ",\"humidity_low\":";
  json += String(cfg.humidityLow, 2);
  json += ",\"humidity_high\":";
  json += String(cfg.humidityHigh, 2);
  json += ",\"lux_low\":";
  json += String(cfg.luxLow, 2);
  json += ",\"lux_high\":";
  json += String(cfg.luxHigh, 2);
  json += ",\"sht_precision\":";
  json += String(cfg.shtPrecision);
  json += ",\"bh1750_mode\":";
  json += String(cfg.bhMode);
  json += "}";
  // JSON contract: "power": {
  json += ",\"power\":{\"power_mode\":\"";
  json += powerModeName();
  json += "\",\"lcd_brightness_pct\":";
  json += String(cfg.lcdBrightnessPct);
  json += ",\"backlight_on\":";
  json += gBacklightOn ? "true" : "false";
  json += ",\"backlight_timeout_ms\":";
  json += String(cfg.backlightTimeoutMs);
  json += ",\"wifi_sta_sleep\":";
  json += cfg.wifiStaSleep ? "true" : "false";
  json += ",\"sensor_interval_ms\":";
  json += String(sensorIntervalMs());
  json += ",\"low_power_sensor_interval_ms\":";
  json += String(cfg.lowPowerSensorIntervalMs);
  json += ",\"ap_enabled\":";
  json += lowPowerStaOnly() ? "false" : "true";
  json += ",\"cpu_mhz\":";
  json += String(getCpuFrequencyMhz());
  json += ",\"sleep_eligible\":";
  json += sleepEligible() ? "true" : "false";
  json += ",\"web_boost_active\":";
  json += webBoostActive() ? "true" : "false";
  json += ",\"web_boost_until_ms\":";
  json += String(gWebBoostUntilMs);
  json += "}";
  json += ",\"sensor_options\":{\"sht41\":{\"current\":\"";
  json += shtPrecisionName();
  json += "\",\"available\":[\"low\",\"medium\",\"high\"],\"note\":\"低精度发热最小；高精度更慢但噪声更低；加热器模式暂不开放为长期控制\"},\"scd41\":{\"current\":\"periodic_5s\",\"available\":[\"periodic_5s\",\"low_power_30s\",\"single_shot\",\"asc\",\"temperature_offset\",\"altitude_compensation\",\"forced_recalibration\"],\"note\":\"当前开放周期读数；校准类设置需要更严格流程，先展示能力\"},\"sgp41\":{\"current\":\"voc_nox_default_algorithm\",\"available\":[\"humidity_compensation\",\"conditioning\",\"voc_index_algorithm\",\"nox_index_algorithm\"],\"note\":\"当前使用 SHT41 温湿度补偿和库默认 VOC/NOx 算法参数\"},\"bh1750\":{\"current\":\"";
  json += bhModeName();
  json += "\",\"available\":[\"continuous_high_res_1lx\",\"continuous_high_res_0_5lx\",\"continuous_low_res_4lx\",\"one_time_modes\",\"mtreg\"],\"note\":\"当前开放连续模式分辨率切换；MTreg 和单次模式暂不开放\"}}";
  json += ",\"storage\":{\"mounted\":";
  json += gFsReady ? "true" : "false";
  json += ",\"persisted_samples\":";
  json += String(gPersistedSamples);
  json += ",\"failures\":";
  json += String(gStorageFailures);
  json += ",\"last_sample_ms\":";
  json += String(gLastSampleMs);
  json += ",\"ring_ready\":";
  json += gMinuteRingReady ? "true" : "false";
  json += ",\"ring_path\":\"";
  json += kMinuteRingPath;
  json += "\",\"ring_capacity\":";
  json += String(gMinuteRing.capacity);
  json += ",\"ring_segments\":";
  json += String(kMinuteRingSegments);
  json += ",\"ring_segment_records\":";
  json += String(kMinuteRingSegmentRecords);
  json += ",\"ring_count\":";
  json += String(gMinuteRing.count);
  json += ",\"ring_write_index\":";
  json += String(gMinuteRing.writeIndex);
  json += ",\"ring_total_writes\":";
  json += String(gMinuteRing.totalWrites);
  json += ",\"ring_record_bytes\":";
  json += String(sizeof(MinuteRingRecord));
  json += ",\"ring_file_bytes\":";
  json += String(minuteRingOffset(kMinuteRingCapacity));
  json += ",\"raw_ram_rows\":";
  json += String(gHistoryCount);
  json += ",\"last_persist_duration_ms\":";
  json += String(gLastPersistDurationMs);
  json += ",\"max_persist_duration_ms\":";
  json += String(gMaxPersistDurationMs);
  json += ",\"dropped_invalid_samples\":";
  json += String(gDroppedInvalidSamples);
  json += ",\"dropped_unsynced_samples\":";
  json += String(gDroppedUnsyncedSamples);
  json += ",\"dropped_time_domain_samples\":";
  json += String(gDroppedTimeDomainSamples);
  json += ",\"last_filtered_minute_rows\":";
  json += String(gLastFilteredMinuteRows);
  json += ",\"used_bytes\":";
  json += gFsReady ? String(LittleFS.usedBytes()) : "0";
  json += ",\"total_bytes\":";
  json += gFsReady ? String(LittleFS.totalBytes()) : "0";
  json += "}";
  json += ",\"system\":{\"uptime_s\":";
  json += String(millis() / 1000UL);
  json += ",\"heap\":";
  json += String(ESP.getFreeHeap());
  json += ",\"last_loop_ms\":";
  json += String(gLastLoopMs);
  json += ",\"loop_stalls\":";
  json += String(gLoopStallCount);
  json += ",\"max_loop_gap_ms\":";
  json += String(gMaxLoopGapMs);
  json += ",\"watchdog\":\"soft_monitor\"";
  json += "}}";
  return json;
}

String historyJson() {
  String json;
  json.reserve(9000);
  json += "{\"source\":\"ram\",\"rows\":[";
  for (size_t i = 0; i < gHistoryCount; i++) {
    const SampleRow &row = historyAt(i);
    if (i != 0) json += ",";
    json += "{\"seq\":";
    json += String(row.seq);
    json += ",\"uptime_ms\":";
    json += String(row.uptimeMs);
    json += ",\"co2_ppm\":";
    json += String(row.co2);
    json += ",\"temp_c\":";
    json += String(row.tempC, 2);
    json += ",\"humidity\":";
    json += String(row.humidity, 2);
    json += ",\"voc_index\":";
    json += String(row.voc);
    json += ",\"nox_index\":";
    json += String(row.nox);
    json += ",\"lux\":";
    json += String(row.lux, 2);
    json += ",\"ok\":";
    json += row.ok ? "true" : "false";
    json += "}";
  }
  json += "]}";
  return json;
}

bool currentMinuteRecord(MinuteRingRecord *record) {
  if (!record || gMinuteAgg.count == 0) return false;
  const double count = static_cast<double>(gMinuteAgg.count);
  record->minute = gMinuteAgg.minute;
  record->count = static_cast<uint16_t>(min<uint32_t>(gMinuteAgg.count, 65535UL));
  record->okPermille = static_cast<uint16_t>(lround((static_cast<double>(gMinuteAgg.okCount) * 1000.0) / count));
  record->co2 = gMinuteAgg.co2Sum / count;
  record->tempC = gMinuteAgg.tempSum / count;
  record->humidity = gMinuteAgg.humiditySum / count;
  record->voc = gMinuteAgg.vocSum / count;
  record->nox = gMinuteAgg.noxSum / count;
  record->lux = gMinuteAgg.luxSum / count;
  return true;
}

String minuteRecordJson(const MinuteRingRecord &record) {
  String json;
  json.reserve(150);
  json += "{\"minute\":";
  json += String(record.minute);
  json += ",\"count\":";
  json += String(record.count);
  json += ",\"co2_ppm\":";
  json += String(record.co2, 1);
  json += ",\"temp_c\":";
  json += String(record.tempC, 2);
  json += ",\"humidity\":";
  json += String(record.humidity, 2);
  json += ",\"voc_index\":";
  json += String(record.voc, 1);
  json += ",\"nox_index\":";
  json += String(record.nox, 1);
  json += ",\"lux\":";
  json += String(record.lux, 2);
  json += ",\"ok_ratio\":";
  json += String(static_cast<float>(record.okPermille) / 1000.0f, 3);
  json += "}";
  return json;
}

String minuteRecordCsv(const MinuteRingRecord &record) {
  String line;
  line.reserve(110);
  line += String(record.minute);
  line += ",";
  line += String(record.count);
  line += ",";
  line += String(record.co2, 1);
  line += ",";
  line += String(record.tempC, 2);
  line += ",";
  line += String(record.humidity, 2);
  line += ",";
  line += String(record.voc, 1);
  line += ",";
  line += String(record.nox, 1);
  line += ",";
  line += String(record.lux, 2);
  line += ",";
  line += String(static_cast<float>(record.okPermille) / 1000.0f, 3);
  line += "\n";
  return line;
}

void flushBufferedContent(char *buffer, size_t &used) {
  if (used == 0) return;
  buffer[used] = '\0';
  server.sendContent(buffer);
  used = 0;
}

void appendBufferedContent(const char *text, char *buffer, size_t bufferSize, size_t &used) {
  const size_t len = strlen(text);
  if (len + 1 >= bufferSize) {
    flushBufferedContent(buffer, used);
    server.sendContent(text);
    return;
  }
  if (used + len + 1 >= bufferSize) {
    flushBufferedContent(buffer, used);
  }
  memcpy(buffer + used, text, len);
  used += len;
}

void appendBufferedContent(const String &text, char *buffer, size_t bufferSize, size_t &used) {
  appendBufferedContent(text.c_str(), buffer, bufferSize, used);
}

void minuteRecordJsonLine(const MinuteRingRecord &record, char *out, size_t outSize) {
  snprintf(out, outSize,
           "{\"minute\":%lu,\"count\":%u,\"co2_ppm\":%.1f,\"temp_c\":%.2f,\"humidity\":%.2f,\"voc_index\":%.1f,\"nox_index\":%.1f,\"lux\":%.2f,\"ok_ratio\":%.3f}",
           static_cast<unsigned long>(record.minute),
           static_cast<unsigned int>(record.count),
           record.co2,
           record.tempC,
           record.humidity,
           record.voc,
           record.nox,
           record.lux,
           static_cast<float>(record.okPermille) / 1000.0f);
}

void minuteRecordCsvLine(const MinuteRingRecord &record, char *out, size_t outSize) {
  snprintf(out, outSize,
           "%lu,%u,%.1f,%.2f,%.2f,%.1f,%.1f,%.2f,%.3f\n",
           static_cast<unsigned long>(record.minute),
           static_cast<unsigned int>(record.count),
           record.co2,
           record.tempC,
           record.humidity,
           record.voc,
           record.nox,
           record.lux,
           static_cast<float>(record.okPermille) / 1000.0f);
}

struct MinuteExportStats {
  uint32_t rawRows = 0;
  uint32_t usableRows = 0;
  uint32_t epochUsableRows = 0;
  uint32_t exportedRows = 0;
  uint32_t filteredRows = 0;
  bool preferEpoch = false;
};

MinuteExportStats computeMinuteExportStats() {
  MinuteExportStats stats;
  stats.rawRows = (gMinuteRingReady ? gMinuteRing.count : 0) + (gMinuteAgg.count > 0 ? 1UL : 0UL);
  const uint32_t startSlot = (gMinuteRing.writeIndex + gMinuteRing.capacity - gMinuteRing.count) % gMinuteRing.capacity;
  File segmentFile;
  int32_t openSegment = -1;
  for (uint32_t i = 0; i < gMinuteRing.count; i++) {
    MinuteRingRecord record;
    const uint32_t slot = (startSlot + i) % gMinuteRing.capacity;
    if (readMinuteRingSlotCached(slot, &record, segmentFile, openSegment) && minuteRecordHasUsableValues(record)) {
      stats.usableRows++;
      if (minuteKeyIsEpoch(record.minute)) stats.epochUsableRows++;
    }
    if ((i & 0x3F) == 0) yield();
  }
  if (segmentFile) segmentFile.close();
  MinuteRingRecord current;
  if (currentMinuteRecord(&current) && minuteRecordHasUsableValues(current)) {
    stats.usableRows++;
    if (minuteKeyIsEpoch(current.minute)) stats.epochUsableRows++;
  }
  stats.preferEpoch = stats.epochUsableRows > 0;
  if (stats.preferEpoch) stats.usableRows = stats.epochUsableRows;
  stats.filteredRows = stats.rawRows > stats.usableRows ? stats.rawRows - stats.usableRows : 0;
  return stats;
}

bool minuteRecordExportable(const MinuteRingRecord &record, const MinuteExportStats &stats) {
  if (!minuteRecordHasUsableValues(record)) return false;
  return !stats.preferEpoch || minuteKeyIsEpoch(record.minute);
}

void sendMinuteHistory(uint32_t maxRows) {
  MinuteExportStats stats = computeMinuteExportStats();
  const uint32_t ringRows = gMinuteRingReady ? gMinuteRing.count : 0;
  const uint32_t skipRows = (maxRows > 0 && stats.usableRows > maxRows) ? stats.usableRows - maxRows : 0;
  uint32_t usableRow = 0;
  bool first = true;

  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json; charset=utf-8", "");
  char outBuffer[2048];
  char line[224];
  size_t outUsed = 0;
  snprintf(line, sizeof(line), "{\"source\":\"minute\",\"total_rows\":%lu,\"raw_rows\":%lu,\"filtered_rows\":%lu,\"capacity\":%lu,\"rows\":[",
           static_cast<unsigned long>(stats.usableRows),
           static_cast<unsigned long>(stats.rawRows),
           static_cast<unsigned long>(stats.rawRows > stats.usableRows ? stats.rawRows - stats.usableRows : 0),
           static_cast<unsigned long>(kMinuteRingCapacity));
  appendBufferedContent(line, outBuffer, sizeof(outBuffer), outUsed);

  const uint32_t startSlot = (gMinuteRing.writeIndex + gMinuteRing.capacity - gMinuteRing.count) % gMinuteRing.capacity;
  File segmentFile;
  int32_t openSegment = -1;
  for (uint32_t i = 0; i < ringRows; i++) {
    MinuteRingRecord record;
    const uint32_t slot = (startSlot + i) % gMinuteRing.capacity;
    if (!readMinuteRingSlotCached(slot, &record, segmentFile, openSegment) || !minuteRecordExportable(record, stats)) continue;
    if (usableRow++ < skipRows) continue;
    if (!first) appendBufferedContent(",", outBuffer, sizeof(outBuffer), outUsed);
    first = false;
    minuteRecordJsonLine(record, line, sizeof(line));
    appendBufferedContent(line, outBuffer, sizeof(outBuffer), outUsed);
    if ((i & 0x1F) == 0) yield();
  }
  if (segmentFile) segmentFile.close();

  MinuteRingRecord record;
  if (currentMinuteRecord(&record) && minuteRecordExportable(record, stats)) {
    if (usableRow >= skipRows) {
      if (!first) appendBufferedContent(",", outBuffer, sizeof(outBuffer), outUsed);
      minuteRecordJsonLine(record, line, sizeof(line));
      appendBufferedContent(line, outBuffer, sizeof(outBuffer), outUsed);
    }
    usableRow++;
  }

  stats.exportedRows = usableRow > skipRows ? usableRow - skipRows : 0;
  gLastFilteredMinuteRows = stats.rawRows > stats.exportedRows ? stats.rawRows - stats.exportedRows : 0;
  appendBufferedContent("]}", outBuffer, sizeof(outBuffer), outUsed);
  flushBufferedContent(outBuffer, outUsed);
}

void handleHistory() {
  noteWebActivity();
  if (server.hasArg("range")) {
    const String range = server.arg("range");
    uint32_t rows = 0;
    if (range == "all") rows = 0;
    else rows = static_cast<uint32_t>(range.toInt());
    sendMinuteHistory(rows);
    return;
  }
  server.send(200, "application/json; charset=utf-8", historyJson());
}

void handleConfig() {
  noteWebActivity();
  if (server.method() == HTTP_POST) {
    uint32_t value = 0;
    float fvalue = 0.0f;
    bool bvalue = false;
    uint8_t enumValue = POWER_BALANCED;
    if (!parseEnumArg("power_mode", &enumValue)) { server.send(400, "text/plain", "invalid power_mode"); return; }
    if (server.hasArg("power_mode")) cfg.powerMode = enumValue;
    if (!parseUintArg("fast_interval_ms", 500, 10000, &value)) { server.send(400, "text/plain", "invalid fast_interval_ms"); return; }
    if (server.hasArg("fast_interval_ms")) cfg.fastIntervalMs = value;
    if (!parseUintArg("scd_interval_ms", 5000, 60000, &value)) { server.send(400, "text/plain", "invalid scd_interval_ms"); return; }
    if (server.hasArg("scd_interval_ms")) cfg.scdIntervalMs = value;
    if (!parseUintArg("log_interval_ms", 500, 600000, &value)) { server.send(400, "text/plain", "invalid log_interval_ms"); return; }
    if (server.hasArg("log_interval_ms")) cfg.logIntervalMs = value;
    if (!parseUintArg("live_refresh_ms", 250, 10000, &value)) { server.send(400, "text/plain", "invalid live_refresh_ms"); return; }
    if (server.hasArg("live_refresh_ms")) cfg.liveRefreshMs = value;
    if (!parseUintArg("lcd_brightness_pct", 0, 100, &value)) { server.send(400, "text/plain", "invalid lcd_brightness_pct"); return; }
    if (server.hasArg("lcd_brightness_pct")) cfg.lcdBrightnessPct = static_cast<uint8_t>(value);
    if (!parseUintArg("backlight_timeout_ms", 5000, 600000, &value)) { server.send(400, "text/plain", "invalid backlight_timeout_ms"); return; }
    if (server.hasArg("backlight_timeout_ms")) cfg.backlightTimeoutMs = value;
    if (!parseBoolArg("wifi_sta_sleep", &bvalue)) { server.send(400, "text/plain", "invalid wifi_sta_sleep"); return; }
    if (server.hasArg("wifi_sta_sleep")) cfg.wifiStaSleep = bvalue;
    if (!parseUintArg("low_power_sensor_interval_ms", 30000, 600000, &value)) { server.send(400, "text/plain", "invalid low_power_sensor_interval_ms"); return; }
    if (server.hasArg("low_power_sensor_interval_ms")) cfg.lowPowerSensorIntervalMs = value;
    if (!parseUintArg("co2_warn_ppm", 400, 5000, &value)) { server.send(400, "text/plain", "invalid co2_warn_ppm"); return; }
    if (server.hasArg("co2_warn_ppm")) cfg.co2WarnPpm = static_cast<uint16_t>(value);
    if (!parseUintArg("co2_bad_ppm", 400, 5000, &value)) { server.send(400, "text/plain", "invalid co2_bad_ppm"); return; }
    if (server.hasArg("co2_bad_ppm")) cfg.co2BadPpm = static_cast<uint16_t>(value);
    if (!parseUintArg("voc_warn", 1, 500, &value)) { server.send(400, "text/plain", "invalid voc_warn"); return; }
    if (server.hasArg("voc_warn")) cfg.vocWarn = static_cast<uint16_t>(value);
    if (!parseUintArg("voc_bad", 1, 500, &value)) { server.send(400, "text/plain", "invalid voc_bad"); return; }
    if (server.hasArg("voc_bad")) cfg.vocBad = static_cast<uint16_t>(value);
    if (!parseUintArg("nox_warn", 1, 500, &value)) { server.send(400, "text/plain", "invalid nox_warn"); return; }
    if (server.hasArg("nox_warn")) cfg.noxWarn = static_cast<uint16_t>(value);
    if (!parseUintArg("nox_bad", 1, 500, &value)) { server.send(400, "text/plain", "invalid nox_bad"); return; }
    if (server.hasArg("nox_bad")) cfg.noxBad = static_cast<uint16_t>(value);
    if (!parseFloatArg("temp_low_c", -20.0f, 60.0f, &fvalue)) { server.send(400, "text/plain", "invalid temp_low_c"); return; }
    if (server.hasArg("temp_low_c")) cfg.tempLowC = fvalue;
    if (!parseFloatArg("temp_high_c", -20.0f, 80.0f, &fvalue)) { server.send(400, "text/plain", "invalid temp_high_c"); return; }
    if (server.hasArg("temp_high_c")) cfg.tempHighC = fvalue;
    if (!parseFloatArg("humidity_low", 0.0f, 100.0f, &fvalue)) { server.send(400, "text/plain", "invalid humidity_low"); return; }
    if (server.hasArg("humidity_low")) cfg.humidityLow = fvalue;
    if (!parseFloatArg("humidity_high", 1.0f, 100.0f, &fvalue)) { server.send(400, "text/plain", "invalid humidity_high"); return; }
    if (server.hasArg("humidity_high")) cfg.humidityHigh = fvalue;
    if (!parseFloatArg("lux_low", 0.0f, 100000.0f, &fvalue)) { server.send(400, "text/plain", "invalid lux_low"); return; }
    if (server.hasArg("lux_low")) cfg.luxLow = fvalue;
    if (!parseFloatArg("lux_high", 1.0f, 100000.0f, &fvalue)) { server.send(400, "text/plain", "invalid lux_high"); return; }
    if (server.hasArg("lux_high")) cfg.luxHigh = fvalue;
    if (!parseUintArg("sht_precision", 0, 2, &value)) { server.send(400, "text/plain", "invalid sht_precision"); return; }
    if (server.hasArg("sht_precision")) cfg.shtPrecision = static_cast<uint8_t>(value);
    if (!parseUintArg("bh1750_mode", 0, 2, &value)) { server.send(400, "text/plain", "invalid bh1750_mode"); return; }
    if (server.hasArg("bh1750_mode")) {
      cfg.bhMode = static_cast<uint8_t>(value);
      if (env.bhOnline && !gBh1750.configure(currentBhMode())) {
        env.bhFail++;
      }
    }
    normalizeConfig();
    applyPowerRuntimeConfig();
    const bool saved = saveConfig();
    server.send(saved ? 200 : 500, "application/json; charset=utf-8", saved ? "{\"saved\":true}" : "{\"saved\":false}");
    return;
  }
  server.send(200, "application/json; charset=utf-8", statusJson());
}

void handleConfigReset() {
  noteWebActivity();
  cfg = EnvConfig();
  normalizeConfig();
  setBacklight(true);
  applyPowerRuntimeConfig();
  if (env.bhOnline && !gBh1750.configure(currentBhMode())) {
    env.bhFail++;
  }
  const bool saved = saveConfig();
  server.send(saved ? 200 : 500, "application/json; charset=utf-8", saved ? "{\"reset\":true,\"saved\":true}" : "{\"reset\":true,\"saved\":false}");
}

void handleLogClear() {
  noteWebActivity();
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "POST required");
    return;
  }
  if (server.arg("confirm") != "1") {
    server.send(400, "text/plain", "confirm=1 required");
    return;
  }
  const bool cleared = clearMinuteLog();
  String json = "{\"cleared\":";
  json += cleared ? "true" : "false";
  json += ",\"ring_ready\":";
  json += gMinuteRingReady ? "true" : "false";
  json += ",\"minute_rows\":";
  json += String((gMinuteRingReady ? gMinuteRing.count : 0) + (gMinuteAgg.count > 0 ? 1UL : 0UL));
  json += "}";
  server.send(cleared ? 200 : 500, "application/json; charset=utf-8", json);
}

void handlePerformanceBoost() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "POST required");
    return;
  }
  uint32_t durationMs = kDefaultWebBoostMs;
  if (server.hasArg("duration_ms")) {
    durationMs = static_cast<uint32_t>(server.arg("duration_ms").toInt());
  }
  noteWebActivity(durationMs);
  String json = "{\"boosted\":true,\"web_boost_active\":";
  json += webBoostActive() ? "true" : "false";
  json += ",\"web_boost_until_ms\":";
  json += String(gWebBoostUntilMs);
  json += ",\"wifi_sta_sleep_effective\":";
  json += (cfg.wifiStaSleep && !webBoostActive()) ? "true" : "false";
  json += "}";
  server.send(200, "application/json; charset=utf-8", json);
}

void handleHealth() {
  noteWebActivity();
  String json;
  json.reserve(360);
  const uint32_t minuteRows = (gMinuteRingReady ? gMinuteRing.count : 0) + (gMinuteAgg.count > 0 ? 1UL : 0UL);
  json += "{\"ok\":";
  json += (allSensorsOk() && gFsReady && gMinuteRingReady && gStorageFailures == 0) ? "true" : "false";
  json += ",\"sensors_ok\":";
  json += allSensorsOk() ? "true" : "false";
  json += ",\"storage_ok\":";
  json += (gFsReady && gMinuteRingReady && gStorageFailures == 0) ? "true" : "false";
  json += ",\"ring_ready\":";
  json += gMinuteRingReady ? "true" : "false";
  json += ",\"persisted_samples\":";
  json += String(gPersistedSamples);
  json += ",\"minute_rows\":";
  json += String(minuteRows);
  json += ",\"wifi_reconnects\":";
  json += String(gWifiReconnects);
  json += ",\"free_heap\":";
  json += String(ESP.getFreeHeap());
  json += ",\"loop_stalls\":";
  json += String(gLoopStallCount);
  json += ",\"max_loop_gap_ms\":";
  json += String(gMaxLoopGapMs);
  json += ",\"last_persist_duration_ms\":";
  json += String(gLastPersistDurationMs);
  // JSON contract: "power_ok":
  json += ",\"power_ok\":true";
  json += ",\"power_mode\":\"";
  json += powerModeName();
  json += "\",\"backlight_on\":";
  json += gBacklightOn ? "true" : "false";
  json += ",\"wifi_sta_sleep\":";
  json += cfg.wifiStaSleep ? "true" : "false";
  json += ",\"ap_enabled\":";
  json += lowPowerStaOnly() ? "false" : "true";
  json += ",\"cpu_mhz\":";
  json += String(getCpuFrequencyMhz());
  json += ",\"sleep_eligible\":";
  json += sleepEligible() ? "true" : "false";
  json += ",\"web_boost_active\":";
  json += webBoostActive() ? "true" : "false";
  json += "}";
  server.send(200, "application/json; charset=utf-8", json);
}

void handleLogDownload() {
  noteWebActivity();
  if (!gFsReady || !gMinuteRingReady) {
    server.send(503, "text/plain", "minute ring not ready");
    return;
  }
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv; charset=utf-8", "");
  char outBuffer[2048];
  char line[128];
  size_t outUsed = 0;
  MinuteExportStats stats = computeMinuteExportStats();
  appendBufferedContent("\xEF\xBB\xBFminute,count,co2_ppm,temp_c,humidity_pct,voc_index,nox_index,lux,ok_ratio\n", outBuffer, sizeof(outBuffer), outUsed);
  const uint32_t startSlot = (gMinuteRing.writeIndex + gMinuteRing.capacity - gMinuteRing.count) % gMinuteRing.capacity;
  File segmentFile;
  int32_t openSegment = -1;
  for (uint32_t i = 0; i < gMinuteRing.count; i++) {
    MinuteRingRecord record;
    const uint32_t slot = (startSlot + i) % gMinuteRing.capacity;
    if (readMinuteRingSlotCached(slot, &record, segmentFile, openSegment) && minuteRecordExportable(record, stats)) {
      minuteRecordCsvLine(record, line, sizeof(line));
      appendBufferedContent(line, outBuffer, sizeof(outBuffer), outUsed);
      stats.exportedRows++;
    }
    if ((i & 0x1F) == 0) yield();
  }
  if (segmentFile) segmentFile.close();
  MinuteRingRecord current;
  if (currentMinuteRecord(&current) && minuteRecordExportable(current, stats)) {
    minuteRecordCsvLine(current, line, sizeof(line));
    appendBufferedContent(line, outBuffer, sizeof(outBuffer), outUsed);
    stats.exportedRows++;
  }
  gLastFilteredMinuteRows = stats.rawRows > stats.exportedRows ? stats.rawRows - stats.exportedRows : 0;
  flushBufferedContent(outBuffer, outUsed);
}

const char *qualityText(const char *level) {
  if (strcmp(level, "bad") == 0) return "严重";
  if (strcmp(level, "warn") == 0) return "提醒";
  return "正常";
}

const char *menuTitle() {
  switch (gMenuIndex) {
    case MENU_POWER: return "模式";
    case MENU_LCD_BRIGHTNESS: return "背光";
    case MENU_BACKLIGHT_TIMEOUT: return "熄屏";
    case MENU_WIFI_SLEEP: return "WiFi省电";
    case MENU_SHT_PRECISION: return "SHT精度";
    case MENU_BH_MODE: return "BH模式";
    case MENU_WEB_BOOST: return "网页加速";
    default: return "保存退出";
  }
}

const char *menuValueText() {
  switch (gMenuIndex) {
    case MENU_POWER:
      if (cfg.powerMode == POWER_LOW) return "低功耗";
      if (cfg.powerMode == POWER_PERFORMANCE) return "性能";
      return "均衡";
    case MENU_LCD_BRIGHTNESS:
      if (cfg.lcdBrightnessPct <= 20) return "15%";
      if (cfg.lcdBrightnessPct <= 55) return "50%";
      if (cfg.lcdBrightnessPct <= 85) return "80%";
      return "100%";
    case MENU_BACKLIGHT_TIMEOUT:
      if (cfg.backlightTimeoutMs <= 5000UL) return "5秒";
      if (cfg.backlightTimeoutMs <= 30000UL) return "30秒";
      if (cfg.backlightTimeoutMs <= 60000UL) return "60秒";
      return "5分钟";
    case MENU_WIFI_SLEEP:
      return cfg.wifiStaSleep ? "开启" : "关闭";
    case MENU_SHT_PRECISION:
      if (cfg.shtPrecision == 2) return "高";
      if (cfg.shtPrecision == 1) return "中";
      return "低";
    case MENU_BH_MODE:
      if (cfg.bhMode == 2) return "低分辨";
      if (cfg.bhMode == 1) return "0.5lx";
      return "1lx";
    case MENU_WEB_BOOST:
      return webBoostActive() ? "已开启" : "关闭";
    default:
      return "长按保存";
  }
}

void applyMenuSelection() {
  switch (gMenuIndex) {
    case MENU_POWER:
      cfg.powerMode = (cfg.powerMode + 1) % 3;
      applyPowerRuntimeConfig();
      break;
    case MENU_LCD_BRIGHTNESS:
      if (cfg.lcdBrightnessPct <= 20) cfg.lcdBrightnessPct = 50;
      else if (cfg.lcdBrightnessPct <= 55) cfg.lcdBrightnessPct = 80;
      else if (cfg.lcdBrightnessPct <= 85) cfg.lcdBrightnessPct = 100;
      else cfg.lcdBrightnessPct = 15;
      setBacklight(true);
      break;
    case MENU_BACKLIGHT_TIMEOUT:
      if (cfg.backlightTimeoutMs <= 5000UL) cfg.backlightTimeoutMs = 30000UL;
      else if (cfg.backlightTimeoutMs <= 30000UL) cfg.backlightTimeoutMs = 60000UL;
      else if (cfg.backlightTimeoutMs <= 60000UL) cfg.backlightTimeoutMs = 300000UL;
      else cfg.backlightTimeoutMs = 5000UL;
      break;
    case MENU_WIFI_SLEEP:
      cfg.wifiStaSleep = !cfg.wifiStaSleep;
      applyPowerRuntimeConfig();
      break;
    case MENU_SHT_PRECISION:
      cfg.shtPrecision = (cfg.shtPrecision + 1) % 3;
      break;
    case MENU_BH_MODE:
      cfg.bhMode = (cfg.bhMode + 1) % 3;
      if (env.bhOnline && !gBh1750.configure(currentBhMode())) env.bhFail++;
      break;
    case MENU_WEB_BOOST:
      noteWebActivity(kDefaultWebBoostMs);
      break;
    default:
      break;
  }
  normalizeConfig();
  gForceDisplayDraw = true;
}

void saveMenuConfig() {
  normalizeConfig();
  applyPowerRuntimeConfig();
  if (env.bhOnline && !gBh1750.configure(currentBhMode())) env.bhFail++;
  saveConfig();
}

void handleShortPress() {
  noteUserActivity();
  if (!gMenuMode) {
    gDisplayPage = (gDisplayPage + 1) % kDisplayPageCount;
  } else if (gMenuEditing) {
    applyMenuSelection();
  } else {
    gMenuIndex = (gMenuIndex + 1) % MENU_COUNT;
  }
  gForceDisplayDraw = true;
}

void handleLongPress() {
  noteUserActivity();
  if (!gMenuMode) {
    gMenuMode = true;
    gMenuEditing = false;
    gMenuIndex = 0;
  } else if (gMenuIndex == MENU_SAVE_EXIT) {
    saveMenuConfig();
    gMenuMode = false;
    gMenuEditing = false;
  } else if (gMenuEditing) {
    saveMenuConfig();
    gMenuEditing = false;
  } else {
    gMenuEditing = true;
  }
  gForceDisplayDraw = true;
}

void handleButtonIfDue() {
  static int lastStable = HIGH;
  static int lastRead = HIGH;
  static unsigned long lastChange = 0;
  static unsigned long pressStart = 0;
  const int nowRead = digitalRead(static_cast<int>(kBootKey));
  const unsigned long now = millis();
  if (nowRead != lastRead) {
    lastRead = nowRead;
    lastChange = now;
  }
  if (now - lastChange < 30) return;
  if (nowRead != lastStable) {
    lastStable = nowRead;
    if (lastStable == LOW) {
      pressStart = now;
    } else {
      const ButtonEvent event = (now - pressStart >= kButtonLongPressMs) ? BUTTON_LONG : BUTTON_SHORT;
      if (event == BUTTON_LONG) handleLongPress();
      else if (event == BUTTON_SHORT) handleShortPress();
    }
  }
}

void drawLcdHeader(const char *title);

void drawSettingsMenu() {
  drawLcdHeader("设置");
  char line[32];
  snprintf(line, sizeof(line), "%u/%u", static_cast<unsigned int>(gMenuIndex + 1), static_cast<unsigned int>(MENU_COUNT));
  lcdText(4, 16, line, kYellow, kPanel, 1);
  lcdTextUtf8(38, 16, menuTitle(), kWhite, kPanel);
  lcdTextUtf8(4, 32, gMenuEditing ? "编辑" : "选择", gMenuEditing ? kOrange : kCyan, kPanel);
  lcdTextUtf8(54, 32, menuValueText(), gMenuEditing ? kOrange : kGreen, kPanel);
  lcdTextUtf8(4, 50, gMenuEditing ? "短按切换" : "短按下一项", kWhite, kPanel);
  lcdTextUtf8(4, 66, gMenuEditing ? "长按保存" : "长按进入/退出", kWhite, kPanel);
}

void drawLcdHeader(const char *title) {
  lcdFill(0, 0, kLcdWidth - 1, 13, kBlue);
  lcdTextUtf8(3, 1, title, kWhite, kBlue);
  char page[8];
  snprintf(page, sizeof(page), "%u/%u", static_cast<unsigned int>(gDisplayPage + 1), static_cast<unsigned int>(kDisplayPageCount));
  lcdText(136, 3, page, kWhite, kBlue, 1);
}

void drawDisplayIfDue() {
  static unsigned long lastDraw = 0;
  if (!gLcd || (!gForceDisplayDraw && millis() - lastDraw < kDisplayIntervalMs)) return;
  if (!gBacklightOn && !gForceDisplayDraw) return;
  lastDraw = millis();
  gForceDisplayDraw = false;

  lcdFill(0, 0, kLcdWidth - 1, kLcdHeight - 1, kPanel);
  char line[32];

  if (gMenuMode) {
    drawSettingsMenu();
  } else if (gDisplayPage == 0) {
    drawLcdHeader("主看板");
    lcdTextUtf8(4, 16, "二氧化碳", kYellow, kPanel);
    snprintf(line, sizeof(line), "%uppm", env.scdCo2);
    lcdText(76, 18, line, kYellow, kPanel, 1);
    lcdTextUtf8(4, 29, "温", kCyan, kPanel);
    snprintf(line, sizeof(line), "%.1fC", env.shtTempC);
    lcdText(18, 31, line, kCyan, kPanel, 1);
    lcdTextUtf8(78, 29, "湿", kCyan, kPanel);
    snprintf(line, sizeof(line), "%.0f%%", env.shtHumidity);
    lcdText(92, 31, line, kCyan, kPanel, 1);
    lcdTextUtf8(4, 42, "挥发", kOrange, kPanel);
    snprintf(line, sizeof(line), "%ld", static_cast<long>(env.vocIndex));
    lcdText(30, 44, line, kOrange, kPanel, 1);
    lcdTextUtf8(78, 42, "氮氧", kGreen, kPanel);
    snprintf(line, sizeof(line), "%ld", static_cast<long>(env.noxIndex));
    lcdText(104, 44, line, kGreen, kPanel, 1);
    lcdTextUtf8(4, 55, "光照", kYellow, kPanel);
    snprintf(line, sizeof(line), "%.0flx", env.lux);
    lcdText(30, 57, line, kYellow, kPanel, 1);
    lcdTextUtf8(78, 55, "记录", kWhite, kPanel);
    snprintf(line, sizeof(line), "%lu", static_cast<unsigned long>(gPersistedSamples));
    lcdText(104, 57, line, kWhite, kPanel, 1);
    lcdTextUtf8(4, 68, allSensorsOk() ? "正常" : "离线", allSensorsOk() ? kGreen : kRed, kPanel);
    lcdTextUtf8(78, 68, "BOOT切换", kWhite, kPanel);
  } else if (gDisplayPage == 1) {
    drawLcdHeader("阈值余量");
    snprintf(line, sizeof(line), "%ldppm", static_cast<long>(cfg.co2WarnPpm) - static_cast<long>(env.scdCo2));
    lcdTextUtf8(4, 16, "二氧化碳余", kYellow, kPanel);
    lcdText(88, 18, line, kYellow, kPanel, 1);
    snprintf(line, sizeof(line), "%ld", static_cast<long>(cfg.vocWarn) - static_cast<long>(env.vocIndex));
    lcdTextUtf8(4, 29, "挥发余量", kOrange, kPanel);
    lcdText(76, 31, line, kOrange, kPanel, 1);
    snprintf(line, sizeof(line), "%ld", static_cast<long>(cfg.noxWarn) - static_cast<long>(env.noxIndex));
    lcdTextUtf8(4, 42, "氮氧余量", kGreen, kPanel);
    lcdText(76, 44, line, kGreen, kPanel, 1);
    snprintf(line, sizeof(line), "%.0flx", env.lux - cfg.luxLow);
    lcdTextUtf8(4, 55, "光照低余", kYellow, kPanel);
    lcdText(76, 57, line, kYellow, kPanel, 1);
    snprintf(line, sizeof(line), "%.1fC", cfg.tempHighC - env.shtTempC);
    lcdTextUtf8(4, 68, "温高余", kCyan, kPanel);
    lcdText(76, 70, line, kCyan, kPanel, 1);
  } else if (gDisplayPage == 2) {
    drawLcdHeader("传感详情");
    lcdTextUtf8(4, 16, "原始挥发", kOrange, kPanel);
    snprintf(line, sizeof(line), "%u", env.srawVoc);
    lcdText(76, 18, line, kOrange, kPanel, 1);
    lcdTextUtf8(4, 29, "原始氮氧", kGreen, kPanel);
    snprintf(line, sizeof(line), "%u", env.srawNox);
    lcdText(76, 31, line, kGreen, kPanel, 1);
    lcdTextUtf8(4, 42, "预热", kYellow, kPanel);
    snprintf(line, sizeof(line), "%us", env.sgpConditioning);
    lcdText(42, 44, line, kYellow, kPanel, 1);
    lcdTextUtf8(78, 42, "采样", kWhite, kPanel);
    snprintf(line, sizeof(line), "%lus", static_cast<unsigned long>(cfg.fastIntervalMs / 1000UL));
    lcdText(128, 44, line, kWhite, kPanel, 1);
    lcdTextUtf8(4, 55, "质量", kWhite, kPanel);
    lcdTextUtf8(42, 55, qualityText(co2Level()), strcmp(co2Level(), "good") == 0 ? kGreen : kOrange, kPanel);
    lcdTextUtf8(4, 68, "存储", gFsReady ? kGreen : kRed, kPanel);
    lcdTextUtf8(42, 68, gFsReady ? "正常" : "失败", gFsReady ? kGreen : kRed, kPanel);
  } else {
    drawLcdHeader("系统状态");
    lcdTextUtf8(4, 16, "网络", kCyan, kPanel);
    lcdText(42, 18, ipString().c_str(), kCyan, kPanel, 1);
    lcdTextUtf8(4, 29, "记录", kWhite, kPanel);
    snprintf(line, sizeof(line), "%lu", static_cast<unsigned long>(gPersistedSamples));
    lcdText(42, 31, line, kWhite, kPanel, 1);
    lcdTextUtf8(92, 29, "失败", gStorageFailures == 0 ? kGreen : kRed, kPanel);
    snprintf(line, sizeof(line), "%lu", static_cast<unsigned long>(gStorageFailures));
    lcdText(134, 31, line, gStorageFailures == 0 ? kGreen : kRed, kPanel, 1);
    lcdTextUtf8(4, 42, "内存", kGreen, kPanel);
    snprintf(line, sizeof(line), "%luK", static_cast<unsigned long>(ESP.getFreeHeap() / 1024UL));
    lcdText(42, 44, line, kGreen, kPanel, 1);
    lcdTextUtf8(4, 55, "在线", allSensorsOk() ? kGreen : kRed, kPanel);
    snprintf(line, sizeof(line), "SHT SCD SGP BH");
    lcdText(42, 57, line, allSensorsOk() ? kGreen : kRed, kPanel, 1);
    lcdTextUtf8(4, 68, "BOOT切换页", kWhite, kPanel);
  }
  lcdPresent();
}

void printStatusIfDue() {
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint < kSerialIntervalMs) return;
  lastPrint = millis();
  Serial.printf("[ENV] sht=%s %.2fC %.2f%% scd=%s co2=%u sgp=%s voc=%ld nox=%ld raw=%u/%u bh=%s %.2flx fs=%s log=%lu fail=%lu ip=%s\n",
                env.shtOnline ? "on" : "off", env.shtTempC, env.shtHumidity,
                env.scdOnline ? "on" : "off", env.scdCo2,
                env.sgpOnline ? "on" : "off", static_cast<long>(env.vocIndex), static_cast<long>(env.noxIndex), env.srawVoc, env.srawNox,
                env.bhOnline ? "on" : "off", env.lux,
                gFsReady ? "on" : "off", static_cast<unsigned long>(gPersistedSamples),
                static_cast<unsigned long>(gStorageFailures), ipString().c_str());
}

void setupServer() {
  server.on("/", HTTP_GET, []() { noteWebActivity(); server.send_P(200, "text/html; charset=utf-8", kIndexHtml); });
  server.on("/api/status", HTTP_GET, []() { noteWebActivity(); server.send(200, "application/json; charset=utf-8", statusJson()); });
  server.on("/api/live", HTTP_GET, []() { noteWebActivity(); server.send(200, "application/json; charset=utf-8", statusJson()); });
  server.on("/api/history", HTTP_GET, handleHistory);
  server.on("/api/health", HTTP_GET, handleHealth);
  server.on("/api/config", HTTP_ANY, handleConfig);
  server.on("/api/config/reset", HTTP_POST, handleConfigReset);
  server.on("/api/log.csv", HTTP_GET, handleLogDownload);
  server.on("/api/log/clear", HTTP_POST, handleLogClear);
  server.on("/api/performance/boost", HTTP_POST, handlePerformanceBoost);
  server.begin();
}

void applyCpuPowerMode() {
  const uint32_t mhz = targetCpuMhz();
  if (getCpuFrequencyMhz() != mhz) {
    setCpuFrequencyMhz(mhz);
  }
}

void setupWifi() {
  applyCpuPowerMode();
  WiFi.mode(lowPowerStaOnly() ? WIFI_STA : WIFI_AP_STA);
  WiFi.setSleep(cfg.wifiStaSleep);
  if (lowPowerStaOnly()) {
    WiFi.softAPdisconnect(true);
  } else {
    WiFi.softAP(kApSsid);
  }
  if (strlen(kStaSsid) > 0) WiFi.begin(kStaSsid, kStaPass);
}

void applyPowerRuntimeConfig() {
  applyCpuPowerMode();
  WiFi.setSleep(cfg.wifiStaSleep && !webBoostActive());
  if (lowPowerStaOnly()) {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    if (WiFi.status() != WL_CONNECTED) WiFi.begin(kStaSsid, kStaPass);
  } else {
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(kApSsid);
    if (strlen(kStaSsid) > 0 && WiFi.status() != WL_CONNECTED) WiFi.begin(kStaSsid, kStaPass);
  }
  setBacklight(gBacklightOn);
  gLastAppliedWebBoost = webBoostActive();
}

void handleWifiReliability() {
  if (strlen(kStaSsid) == 0 || WiFi.status() == WL_CONNECTED) return;
  const unsigned long now = millis();
  if (now - gLastWifiReconnectMs < kWifiReconnectIntervalMs) return;
  gLastWifiReconnectMs = now;
  gWifiReconnects++;
  WiFi.disconnect(false);
  WiFi.begin(kStaSsid, kStaPass);
  Serial.printf("[NET] STA reconnect #%lu\n", static_cast<unsigned long>(gWifiReconnects));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println();
  Serial.println("ESP32-S3M environment dashboard boot");

  Wire.begin(kI2cSda, kI2cScl, kI2cClockHz);
  Wire.setClock(kI2cClockHz);
  pinMode(static_cast<int>(kBootKey), INPUT_PULLUP);
  initStorage();
  setupWifi();
  lcdInit();
  lcdFill(0, 0, kLcdWidth - 1, kLcdHeight - 1, kPanel);
  lcdText(4, 8, "BOOT", kYellow, kPanel, 2);
  lcdText(4, 36, "I2C 400K SDA4 SCL5", kWhite, kPanel, 1);
  lcdPresent();

  initSensors();
  setupServer();
  noteWebActivity(kDefaultWebBoostMs);
  maintainTimeSync();
  readSensorsIfDue();

  Serial.printf("[NET] AP=%s IP=%s\n", kApSsid, ipString().c_str());
  Serial.println("[API] /api/status /api/history /api/health /api/config /api/log.csv");
}

void loop() {
  const uint32_t now = millis();
  if (gLastLoopMs != 0) {
    const uint32_t gap = now - gLastLoopMs;
    if (gap > gMaxLoopGapMs) gMaxLoopGapMs = gap;
    if (gap > 5000UL) gLoopStallCount++;
  }
  gLastLoopMs = now;
  server.handleClient();
  handleWifiReliability();
  maintainTimeSync();
  maintainWebBoostPower();
  handleButtonIfDue();
  handleBacklightTimeout();
  readSensorsIfDue();
  publishSampleIfDue();
  drawDisplayIfDue();
  printStatusIfDue();
}
