/*
 * Dual Controller: Serial + NimBLE Gamepad PG-9193 (ESP32-C3)
 * 串口始终可用，NimBLE 比 Bluedroid 节省 ~100KB RAM
 *
 * 依赖库: NimBLE-Arduino (h2zero)
 *   Arduino IDE -> 库管理器 -> 搜索 "NimBLE-Arduino" 安装
 */
#include <Arduino.h>
#include <NimBLEDevice.h>

#include "hw_config.h"

extern "C" void nofrendo_pause_timer();
extern "C" void nofrendo_resume_timer();

// ========== NimBLE 全局 ==========
#if defined(HW_CONTROLLER_BLE)
static volatile uint32_t gBleBtnState = 0xFFFFFFFF;
static volatile bool gBleReady = false;
static bool gBleScanning = false;

static uint32_t pgBtnToMask(uint8_t code) {
  switch (code) {
    case 0x06: return 1 << 0;  // UP
    case 0x07: return 1 << 1;  // DOWN
    case 0x08: return 1 << 2;  // LEFT
    case 0x09: return 1 << 3;  // RIGHT
    case 0x11: return 1 << 4;  // SELECT
    case 0x12: return 1 << 5;  // START
    case 0x0A: return 1 << 6;  // A
    case 0x0D: return 1 << 7;  // B
    case 0x0B: return 1 << 8;  // X
    case 0x0C: return 1 << 9;  // Y
    default:   return 0;
  }
}

static void hidNotify(NimBLERemoteCharacteristic *pChar, uint8_t *data, size_t length, bool isNotify) {
  Serial.printf("HID[%d]:", length);
  for (size_t i = 0; i < length && i < 16; i++) Serial.printf(" %02X", data[i]);
  Serial.println();

  if (length == 7) {
    uint32_t state = 0xFFFFFFFF;
    for (int i = 2; i < 7; i++) {
      if (data[i] == 0) continue;
      uint32_t mask = pgBtnToMask(data[i]);
      if (mask) state &= ~mask;
    }
    gBleBtnState = state;
  }
}

class MyClientCb : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *client)    { Serial.println("BLE: Connected"); }
  void onDisconnect(NimBLEClient *client, int reason) {
    Serial.printf("BLE: Disconnected reason=%d\n", reason);
    gBleReady = false;
    gBleScanning = false;
    gBleBtnState = 0xFFFFFFFF;
    // 清理客户端以便下次重连
    NimBLEDevice::deleteClient(client);
  }
  uint32_t onPassKeyRequest() { return 123456; }
  bool onConfirmPIN(uint32_t pin) { return true; }
  void onAuthenticationComplete(NimBLEConnInfo &info) {
    Serial.printf("BLE Pairing: %s\n", info.isEncrypted() ? "OK" : "FAIL");
  }
};

// 扫描回调
static NimBLEAddress gTargetAddr;
static String gTargetName;
static bool gFound = false;

class MyAdCb : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *device) {
    if (gFound) return;
    String n = device->getName().c_str();
    if (n.length() == 0) return;
    bool match = false;
    if (device->isAdvertisingService(NimBLEUUID((uint16_t)0x1812))) match = true;
    if (!match && (n.indexOf("PG-9193") >= 0 || n.indexOf("Gamepad") >= 0)) match = true;
    if (!match) return;

    gTargetAddr = device->getAddress();
    gTargetName = n;
    gFound = true;
    Serial.printf("BLE: Found %s [%s] RSSI=%d\n",
      n.c_str(), gTargetAddr.toString().c_str(), device->getRSSI());
    NimBLEDevice::getScan()->stop();
  }
};

static bool doInit(NimBLEClient *pClient) {
  Serial.println("BLE: Discovering services...");
  std::vector<NimBLERemoteService *> svcs = pClient->getServices(true);
  Serial.printf("BLE: %d service(s)\n", (int)svcs.size());
  for (auto &svc : svcs) {
    Serial.printf("  Svc: %s\n", svc->getUUID().toString().c_str());
  }

  // 激活
  Serial.println("BLE: Activation...");
  for (auto &svc : svcs) {
    String uuid = svc->getUUID().toString().c_str();
    if (uuid.indexOf("1800") >= 0 || uuid.indexOf("1801") >= 0 ||
        uuid.indexOf("180a") >= 0 || uuid.indexOf("1812") >= 0) continue;
    auto chs = svc->getCharacteristics(true);
    for (auto &c : chs) {
      if (c->canWrite()) {
        Serial.printf("  Activate -> %s\n", c->getUUID().toString().c_str());
        uint8_t v1[] = {0x01};       c->writeValue(v1, 1, true); delay(30);
        uint8_t v2[] = {0x01, 0x00}; c->writeValue(v2, 2, true); delay(30);
        uint8_t v3[] = {0xAA, 0x55}; c->writeValue(v3, 2, true); delay(30);
      }
    }
  }

  // 订阅
  int notifyCount = 0;
  for (auto &svc : svcs) {
    auto chs = svc->getCharacteristics(true);
    for (auto &c : chs) {
      if (c->canNotify()) {
        if (c->subscribe(true, hidNotify, true)) {
          notifyCount++;
          Serial.printf("  Subscribed: %s\n", c->getUUID().toString().c_str());
        }
      }
    }
  }
  Serial.printf("BLE: Subscribed: %d\n", notifyCount);
  return notifyCount > 0;
}

static MyClientCb *gClientCb = nullptr;

static void bleTask(void *arg) {
  Serial.println("BLE: Pausing game...");
  nofrendo_pause_timer();
  delay(100);

  Serial.printf("BLE: Free heap before init: %d\n", esp_get_free_heap_size());

  static bool bleInited = false;
  if (!bleInited) {
    NimBLEDevice::init("ESP32-C3-NES");
    NimBLEDevice::setSecurityAuth(true, true, true); // bonding, MITM, secure connection
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_KEYBOARD_DISPLAY);
    NimBLEDevice::setSecurityPasskey(123456);
    NimBLEDevice::setPower(3); // 3dBm 节省电量
    gClientCb = new MyClientCb();
    bleInited = true;
  }

  Serial.printf("BLE: Free heap after init: %d\n", esp_get_free_heap_size());

  // 扫描
  Serial.println("BLE: Scanning for PG-9193...");
  gFound = false;
  NimBLEScan *pScan = NimBLEDevice::getScan();
  pScan->setScanCallbacks(new MyAdCb(), false);
  pScan->setActiveScan(true);
  pScan->setInterval(100);
  pScan->setWindow(99);
  pScan->getResults(5 * 1000, false);

  if (!gFound) {
    Serial.println("BLE: No PG-9193 found");
    gBleScanning = false;
    nofrendo_resume_timer();
    vTaskDelete(NULL);
    return;
  }

  // 连接
  Serial.println("BLE: Creating client...");
  NimBLEClient *pClient = NimBLEDevice::createClient();
  if (!pClient) {
    Serial.println("BLE: createClient returned NULL, skipping");
    gBleScanning = false;
    nofrendo_resume_timer();
    vTaskDelete(NULL);
    return;
  }
  pClient->setClientCallbacks(gClientCb, false);
  pClient->setConnectionParams(12, 12, 0, 51);

  Serial.println("BLE: Connecting...");
  if (!pClient->connect(gTargetAddr)) {
    Serial.println("BLE: Connect FAILED");
    gBleScanning = false;
    nofrendo_resume_timer();
    vTaskDelete(NULL);
    return;
  }

  Serial.println("BLE: Connected!");
  delay(500);

  // 主动触发配对（关键！否则 PG-9193 不会发 HID 报告）
  Serial.println("BLE: Triggering pairing...");
  if (pClient->secureConnection()) {
    Serial.println("BLE: secureConnection OK");
  } else {
    Serial.println("BLE: secureConnection FAILED");
  }
  delay(1000);

  Serial.printf("BLE: After connect, isConnected=%d, free_heap=%d\n",
                pClient->isConnected(), esp_get_free_heap_size());

  bool ok = doInit(pClient);

  Serial.printf("BLE: After doInit, isConnected=%d, free_heap=%d\n",
                pClient->isConnected(), esp_get_free_heap_size());

  if (ok) {
    gBleReady = true;
    Serial.println("BLE: === Ready! Press buttons on gamepad ===");
  } else {
    Serial.println("BLE: doInit failed");
    gBleScanning = false;
  }

  Serial.println("BLE: Resuming game...");
  nofrendo_resume_timer();
  vTaskDelete(NULL);
}
#endif /* HW_CONTROLLER_BLE */

// ========== 串口控制 ==========
static uint32_t serialReadInput() {
  static uint32_t state = 0xFFFFFFFF;
  static int holdFrames = 0;

  while (Serial.available()) {
    char c = Serial.read();
    uint32_t mask = 0;
    switch (c) {
      case 'w': case 'W': mask = 1 << 0; break;
      case 's': case 'S': mask = 1 << 1; break;
      case 'a': case 'A': mask = 1 << 2; break;
      case 'd': case 'D': mask = 1 << 3; break;
      case 'v': case 'V': mask = 1 << 4; break;
      case 'b': case 'B': mask = 1 << 5; break;
      case 'k': case 'K': mask = 1 << 6; break;
      case 'j': case 'J': mask = 1 << 7; break;
      case 'i': case 'I': mask = 1 << 8; break;
      case 'u': case 'U': mask = 1 << 9; break;
      case 'z': case 'Z':
#if defined(HW_CONTROLLER_BLE)
        if (!gBleScanning && !gBleReady) {
          gBleScanning = true;
          Serial.println("BLE: Starting scan...");
          xTaskCreate(bleTask, "bleTask", 8192, NULL, 1, NULL);
        } else {
          Serial.println(gBleReady ? "BLE: Already connected" : "BLE: Already scanning");
        }
#endif
        break;
    }
    if (mask) {
      state &= ~mask;
      holdFrames = 10;
    }
  }

  if (holdFrames > 0) {
    holdFrames--;
    if (holdFrames == 0) state = 0xFFFFFFFF;
  }
  return state;
}

// ========== 对外接口 ==========
extern "C" void controller_init() {
  Serial.println("Serial: W/A/S/D=方向 V=SELECT B=START K=A J=B I=X U=Y  Z=开启BLE");

#if defined(HW_CONTROLLER_BLE)
  // 游戏启动后自动扫描一次
  gBleScanning = true;
  xTaskCreate(bleTask, "bleTask", 8192, NULL, 1, NULL);
#endif
}

extern "C" uint32_t controller_read_input() {
  uint32_t state = serialReadInput();

#if defined(HW_CONTROLLER_BLE)
  if (gBleReady) {
    state &= gBleBtnState;
  }
#endif

  return state;
}

// 兼容旧接口（如有调用）
extern "C" void controller_ble_preinit() {}
