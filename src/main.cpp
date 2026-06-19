#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>      // Thư viện gốc xử lý nạp Flash của ESP32
#include "esp_ota_ops.h" // Thư viện hệ thống quản lý phân vùng

// THÊM: Cấu hình chân nút nhấn vật lý để đảo vùng thủ công lúc khởi động
const int BOOT_BUTTON_PIN = 14;

// Cấu hình mạng Wi-Fi
const char *ssid = "vuweed";
const char *password = "11111111";

bool ledState = 0;
const int ledPin = 2;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

// Biến toàn cục để hứng trạng thái checkbox từ Web gửi lên
bool shouldSwitchPartition = true;

// ==========================================
// GIAO DIỆN CHÍNH (Đã sửa lỗi font tiếng Việt)
// ==========================================
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta charset="UTF-8">
  <title>Bộ Điều Khiển ESP32</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html { font-family: Arial, Helvetica, sans-serif; text-align: center; }
    h1 { font-size: 1.8rem; color: white; }
    h2 { font-size: 1.5rem; font-weight: bold; color: #143642; }
    .topnav { overflow: hidden; background-color: #143642; padding: 10px; }
    body { margin: 0; background-color: #f4f4f6; }
    .content { padding: 30px; max-width: 600px; margin: 0 auto; }
    .card { background-color: #F8F7F9; box-shadow: 2px 2px 12px rgba(0,0,0,0.1); padding: 20px; border-radius: 8px; }
    .button { padding: 15px 50px; font-size: 24px; color: #fff; background-color: #0f8b8d; border: none; border-radius: 5px; cursor: pointer; }
    .button:active { transform: translateY(2px); }
    .state { font-size: 1.5rem; color:#8c8c8c; font-weight: bold; }
    .link-ota { display: inline-block; margin-top: 20px; color: #0f8b8d; text-decoration: none; font-weight: bold; }
  </style>
</head>
<body>
  <div class="topnav">
    <h1>Hệ Thống WebSocket Server</h1>
  </div>
  <div class="content">
    <div class="card">
      <h2>Điều Khiển Đầu Ra - GPIO 2</h2>
      <p class="state">Trạng thái: <span id="state">%STATE%</span></p>
      <p><button id="button" class="button">Đảo Trạng Thái</button></p>
      <a class="link-ota" href="/update_page">➔ Đi tới trang nạp phần mềm (OTA)</a>
    </div>
  </div>
<script>
  var gateway = `ws://${window.location.hostname}/ws`;
  var websocket;
  window.addEventListener('load', () => {
    websocket = new WebSocket(gateway);
    websocket.onmessage = (e) => {
      document.getElementById('state').innerHTML = (e.data == "1") ? "BẬT" : "TẮT";
    };
    document.getElementById('button').addEventListener('click', () => { websocket.send('toggle'); });
  });
</script>
</body>
</html>)rawliteral";

// ==========================================
// GIAO DIỆN TRANG OTA (Có tích chọn chuyển vùng, chuẩn tiếng Việt)
// ==========================================
const char update_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta charset="UTF-8">
  <title>Cập Nhật Phần Mềm Thủ Công</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html { font-family: Arial, sans-serif; text-align: center; background-color: #f4f4f6; }
    .content { padding: 40px 20px; max-width: 500px; margin: 0 auto; }
    .card { background-color: #fff; padding: 30px; border-radius: 8px; box-shadow: 0 4px 10px rgba(0,0,0,0.1); }
    h2 { color: #143642; margin-bottom: 20px; }
    .form-group { margin-bottom: 20px; text-align: left; }
    .checkbox-group { display: flex; align-items: center; margin-top: 15px; background: #eef7f7; padding: 10px; border-radius: 5px; }
    .checkbox-group input { width: 20px; height: 20px; margin-right: 10px; cursor: pointer; }
    .btn-submit { width: 100%; padding: 12px; font-size: 16px; color: #fff; background-color: #0f8b8d; border: none; border-radius: 5px; cursor: pointer; font-weight: bold; }
    .btn-submit:hover { background-color: #0c7274; }
  </style>
</head>
<body>
  <div class="content">
    <div class="card">
      <h2>Nạp Firmware Qua Web</h2>
      <form method='POST' action='/update' enctype='multipart/form-data' onsubmit="this.action='/update?switch=' + document.getElementById('switch_prt').checked;">
        <div class="form-group">
          <label style="font-weight:bold;">Chọn file phần mềm (.bin):</label><br><br>
          <input type='file' name='update' accept='.bin' required>
        </div>
        
        <div class="checkbox-group">
          <input type='checkbox' id='switch_prt' checked>
          <label for='switch_prt' style="cursor:pointer; font-size:14px; color:#143642;"><b>Tự động chuyển phân vùng và khởi động lại</b> sau khi nạp xong</label>
        </div>
        <br>
        <input type='submit' class='btn-submit' value='Bắt Đầu Nạp'>
      </form>
    </div>
  </div>
</body>
</html>)rawliteral";

// ==========================================
// CÁC HÀM XỬ LÝ LOGIC TRÊN ESP32
// ==========================================
void notifyClients()
{
  ws.textAll(String(ledState));
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len)
{
  AwsFrameInfo *info = (AwsFrameInfo *)arg;
  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {
    data[len] = 0;
    if (strcmp((char *)data, "toggle") == 0)
    {
      ledState = !ledState;
      notifyClients();
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  if (type == WS_EVT_DATA)
  {
    handleWebSocketMessage(arg, data, len);
  }
}

String processor(const String &var)
{
  if (var == "STATE")
    return ledState ? "BẬT" : "TẮT";
  return String();
}

// THÊM: Hàm kiểm tra nút nhấn cơ học để đổi vùng thủ công ngay khi khởi động
void checkBootButton()
{
  int state_button = 0;
  int button_confirm = 0;
  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  delay(100); // Chờ ổn định điện áp trên chân GPIO 32 sau khi cấp nguồn
  for (int i = 0; i < 10; i++)
  {
    state_button = digitalRead(BOOT_BUTTON_PIN);
    if (state_button == LOW)
    {
      button_confirm++;
    }
    else
    {
      button_confirm--;
    }
    delay(50);
  }
  Serial.printf("[INIT] button_confirm = %d\r\n", button_confirm);

  const esp_partition_t *target_partition = NULL;
  const esp_partition_t *running_partition = esp_ota_get_running_partition();

  if (button_confirm > 5)
  {
    // Nếu nút bấm = 1 -> Mục tiêu ép buộc chạy phân vùng 1 (thường có label định danh là ota_0 hoặc app0)
    Serial.println("[INIT] Nút bấm = 1 -> Yêu cầu vào Vùng 1 (app0)");
    target_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL);
    if (!target_partition)
    {
      target_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
    }
  }
  else
  {
    // Nếu nút bấm = 0 -> Mục tiêu ép buộc chạy phân vùng 2 (label định danh là ota_1 hoặc app1)
    Serial.println("[INIT] Nút bấm = 0 -> Yêu cầu vào Vùng 2 (app1)");
    target_partition = esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, NULL);
  }

  // Thực hiện chuyển vùng boot nếu phân vùng chỉ định khác với phân vùng hiện tại đang chạy
  if (target_partition != NULL)
  {
    if (target_partition != running_partition)
    {
      Serial.printf("[INIT] Đang chạy ở phân vùng khác (%s). Tiến hành CHUYỂN VÙNG sang: %s\n", running_partition->label, target_partition->label);
      esp_err_t err = esp_ota_set_boot_partition(target_partition);
      if (err == ESP_OK)
      {
        Serial.println("[INIT] Đổi vùng cấu hình thành công! Đang tự động Reset chip để khởi động lại vào vùng mới...");
        delay(1000);
        esp_restart();
      }
      else
      {
        Serial.printf("[INIT] Đổi vùng thất bại, mã lỗi: 0x%x\n", err);
      }
    }
    else
    {
      Serial.printf("[INIT] Hoàn tất! Chip đang chạy đúng phân vùng yêu cầu: %s\n", running_partition->label);
    }
  }
  else
  {
    Serial.println("[INIT] LỖI: Không thể tìm thấy phân vùng đích trong Partition Table!");
  }
}

void get_flash_info()
{
  Serial.println("\n--- THÔNG TIN BỘ NHỚ FLASH ESP32 ---");

  // 1. Lấy dung lượng Flash được cấu hình khi biên dịch (Flash Size)
  uint32_t flashSize = ESP.getFlashChipSize();
  float flashSizeMB = (float)flashSize / (1024.0 * 1024.0);

  Serial.printf("Dung lượng bộ nhớ Flash tổng: %u Bytes (~%.1f MB)\n", flashSize, flashSizeMB);

  // 2. Lấy tốc độ giao tiếp với chip Flash (Flash Speed)
  uint32_t flashSpeed = ESP.getFlashChipSpeed();
  Serial.printf("Tốc độ xung nhịp Flash: %u Hz (%.1f MHz)\n", flashSpeed, (float)flashSpeed / 1000000.0);

  // 3. Lấy chế độ kết nối Flash (SPI Mode như DIO, QIO, DOUT, QOUT)
  FlashMode_t flashMode = ESP.getFlashChipMode();
  Serial.printf("Chế độ kết nối Flash (SPI Mode ID): %d\n", flashMode);

  Serial.println("------------------------------------");
}

void setup()
{
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  digitalWrite(ledPin, LOW);
  get_flash_info();
  // 1. Thực hiện check nút nhấn trên GPIO 32 ngay khi init phần cứng
  checkBootButton();

  // 2. Kết nối Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nĐã kết nối Wi-Fi! Địa chỉ IP của ESP32: ");
  Serial.println(WiFi.localIP());

  // In thông tin phân vùng hiện tại lên Serial
  const esp_partition_t *running = esp_ota_get_running_partition();
  Serial.printf("Phân vùng đang chạy hiện tại: %s\n", running->label);

  ws.onEvent(onEvent);
  server.addHandler(&ws);

  // Route trang chủ điều khiển LED
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/html", index_html, processor); });

  // Route trang nạp phần mềm OTA
  server.on("/update_page", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "text/html", update_html); });

  // Xử lý HTTP POST nhận file dữ liệu .bin từ trình duyệt
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request)
            {
    if (request->hasParam("switch")) {
      String switchVal = request->getParam("switch")->value();
      shouldSwitchPartition = (switchVal == "true");
    }

    bool hasError = Update.hasError();
    String msg = "";
    if (hasError) {
      msg = "NẠP FIRMWARE THẤT BẠI!";
    } else {
      if (shouldSwitchPartition) {
        msg = "NẠP THÀNH CÔNG! Đang tự động chuyển phân vùng và Reset chip...";
      } else {
        msg = "NẠP THÀNH CÔNG! Đã ghi vào phân vùng trống. (Giữ nguyên App cũ cho đến khi bạn giữ nút GPIO 32 lúc boot để đảo vùng).";
      }
    }

    AsyncWebServerResponse *response = request->beginResponse(hasError ? 500 : 200, "text/plain; charset=utf-8", msg);
    response->addHeader("Connection", "close");
    request->send(response);
    
    if (!hasError && shouldSwitchPartition) {
      delay(1000);
      esp_restart();
    } }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
            {
    
    if(!index){
      Serial.printf("Bắt đầu nhận file: %s\n", filename.c_str());
      
      const esp_partition_t* next_partition = esp_ota_get_next_update_partition(NULL);
      if (next_partition != NULL) {
        Serial.printf("Hệ thống tự động nhận diện vùng nạp đích: %s\n", next_partition->label);
      }

      if(!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)){ 
        Update.printError(Serial);
      }
    }
    
    if(!Update.hasError()){
      if(Update.write(data, len) != len){
        Update.printError(Serial);
      }
    }
    
    if(final){
      if(Update.end(shouldSwitchPartition)){ 
        Serial.printf("Đã ghi xong toàn bộ file: %u bytes.\n", index + len);
      } else {
        Update.printError(Serial);
      }
    } });

  server.begin();
  Serial.printf("VER 1 đang hoạt động");
}

void loop()
{
  digitalWrite(ledPin, ledState);
  // Serial.printf("ledPin = %d", ledState);
  ws.cleanupClients();
}

