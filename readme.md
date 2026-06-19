
---

# # Hệ Thống Quản Lý Phân Vùng Độc Lập & Cập Nhật Firmware Qua Web (OTA) Cho ESP32

Dự án này triển khai một ứng dụng Web Server bất đồng bộ (`ESPAsyncWebServer`) kết hợp `WebSocket` trên ESP32. Hệ thống cho phép điều khiển thiết bị ngoại vi theo thời gian thực và cung cấp giải pháp cập nhật phần mềm từ xa (OTA) nâng cao: **Cho phép người dùng tùy chọn áp dụng ngay firmware mới hoặc lưu giữ cấu hình phân vùng cũ**, kết hợp với cơ chế **đảo vùng khởi động thủ công thông qua nút nhấn vật lý**.

---

## ## 1. Tính Năng Nổi Bật

* **Điều khiển thời gian thực (WebSocket):** Bật/tắt LED (GPIO 2) tức thời mà không cần tải lại trang.
* **Nạp OTA thông minh:** Giao diện Web nạp file `.bin` đi kèm checkbox "Tự động chuyển phân vùng". Nếu bỏ tích, firmware mới sẽ được ghi vào phân vùng trống nhưng chip **không** tự chuyển boot, giúp bảo vệ hệ thống tránh lỗi code mới phá hỏng phân vùng cũ.
* **Dual-Boot thủ công (Phần cứng):** Kiểm tra trạng thái nút nhấn (`GPIO 14`) ngay khi khởi động để cưỡng bức ép chip boot vào phân vùng mong muốn (`app0` hoặc `app1`).
* **Chẩn đoán Flash:** Tự động in chi tiết thông số bộ nhớ Flash (Dung lượng, Tốc độ, SPI Mode) lên Serial Monitor khi vừa cấp nguồn.

---

## ## 2. Sơ Đồ Đấu Nối Phần Cứng

| Linh Kiện | Chân Trên ESP32 | Trạng Thái Hoạt Động |
| --- | --- | --- |
| **Nút nhấn vật lý (Boot/Switch)** | `GPIO 14` | Nối với chân `GND` qua nút nhấn (Sử dụng pull-up nội bộ) |
| **Đèn LED Onboard** | `GPIO 2` | Output điều khiển trạng thái |

> ⚠️ **Lưu ý:** Mã nguồn sử dụng `BOOT_BUTTON_PIN = 14`. Hãy đảm bảo bạn nối đúng nút nhấn vào chân GPIO 14 (hoặc sửa lại biến này trong code nếu dùng chân khác như GPIO 0 hoặc GPIO 32).

---

## ## 3. Hướng Dẫn Cấu Hình Cột Phân Vùng (Partition Table)

Để tính năng dual-boot và chuyển vùng hoạt động chính xác, bạn **bắt buộc** phải cấu hình phân vùng cho ESP32 có ít nhất 2 phân vùng OTA (`app0` và `app1`).

Nếu dùng **Arduino IDE**, hãy chọn:

* `Tools` ➔ `Partition Scheme` ➔ **Default 4MB with spiffs (1.2MB APP / 1.5MB SPIFFS)** hoặc **Minimal SPIFFS (Large APPS with OTA)**.

---

## ## 4. Hướng Dẫn Kiểm Thử Hệ Thống (Test Guideline)

Quy trình kiểm thử dưới đây giúp bạn xác thực toàn bộ tính năng chuyển đổi phân vùng tự động và thủ công.

### ### Bước 1: Chuẩn bị hai phiên bản phần mềm

1. Tại dòng cuối cùng của hàm `setup()`, tìm lệnh: `Serial.printf("VER 1 đang hoạt động");`.
2. Biên dịch và xuất file binary thứ nhất thành `firmware_v1.bin`.
3. Sửa dòng code thành: `Serial.printf("VER 2 đang hoạt động");`, sau đó biên dịch và xuất file thứ hai thành `firmware_v2.bin`.
4. Nạp phiên bản `firmware_v1.bin` vào ESP32 trước thông qua dây cáp USB.

### ### Bước 2: Kiểm tra giao diện điều khiển và Nạp OTA Tự Động

1. Mở `Serial Monitor` (Tốc độ **115200**), kết nối Wi-Fi thành công và lấy IP (Ví dụ: `192.168.1.5`).
2. Truy cập IP đó bằng trình duyệt, nhấn nút "Đảo Trạng Thái" để kiểm tra LED 2 phản hồi qua WebSocket.
3. Bấm vào link chuyển hướng sang trang `/update_page`.
4. Chọn file `firmware_v2.bin`, **giữ nguyên tích chọn** *"Tự động chuyển phân vùng..."*, bấm **Bắt Đầu Nạp**.
5. **Kết quả mong đợi:** Web thông báo nạp thành công, ESP32 tự động reset. Trên Serial Monitor sẽ in: `Phân vùng đang chạy hiện tại: ota_1` và `VER 2 đang hoạt động`.

### ### Bước 3: Kiểm tra tính năng Nạp An Toàn (Không tự chuyển vùng)

1. Tại trang OTA hiện tại (đang chạy VER 2), tiến hành chọn lại file `firmware_v1.bin`.
2. **Bỏ tích chọn** ở ô *"Tự động chuyển phân vùng..."*, bấm **Bắt Đầu Nạp**.
3. **Kết quả mong đợi:** Màn hình báo nạp thành công nhưng ESP32 **không reset**. Bạn tải lại trang chủ, hệ thống vẫn báo `VER 2 đang hoạt động`. Bản firmware 1 mới nạp chỉ đang "nằm chờ" ở phân vùng trống (`ota_0`).

### ### Bước 4: Kiểm tra nút nhấn vật lý để Đảo Vùng Thủ Công (Dual-Boot)

1. **Trường hợp 1 (Không nhấn nút):** Ngắt nguồn ESP32 rồi cắm lại (hoặc bấm nút EN/RST trên mạch).
* *Kết quả:* Mạch khởi động bình thường vào phân vùng mặc định hiện tại (`VER 2`).


2. **Trường hợp 2 (Cưỡng bức chuyển vùng bằng nút nhấn):**
* **Nhấn và giữ** nút bấm nối với chân `GPIO 14`.
* Bấm nút EN/RST trên mạch (hoặc cắm nguồn vào) trong khi **vẫn giữ** nút GPIO 14 khoảng 2 giây rồi thả ra.
* *Kết quả trên Serial:* Hàm `checkBootButton()` sẽ quét tín hiệu, nhận diện `button_confirm > 5`, ép bo mạch chuyển cấu hình boot sang phân vùng `app0` (`ota_0`) và tự động reset.
* Sau khi reset xong, màn hình Serial sẽ hiển thị: `VER 1 đang hoạt động`. Bạn đã kích hoạt thành công firmware cũ bằng phần cứng!



---

## ## 5. Thư Viện Cần Thiết

Để biên dịch được mã nguồn này, hãy đảm bảo bạn đã cài đặt các thư viện sau trong Arduino IDE:

* [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer)
* [AsyncTCP](https://github.com/me-no-dev/AsyncTCP)