#include "serial_console.h"

#include <driver/usb_serial_jtag.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define TAG "Console"

namespace {

// A CA certificate is the long one this has to carry. Anything past it is a
// mistake, and silently truncating would store half a certificate.
constexpr size_t kMaxLine = 3000;

bool started = false;

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

}  // namespace

// Function-local static, not a file-scope one: a feature can Register() from a
// board constructor, and a plain global would not be guaranteed to exist yet.
std::vector<SerialConsole::Command>& SerialConsole::Commands() {
    static std::vector<Command> commands;
    return commands;
}

void SerialConsole::Register(const char* prefix, const char* help, Handler handler) {
    Commands().push_back({prefix, help, std::move(handler)});
}

void SerialConsole::Dispatch(const std::string& raw) {
    const std::string line = Trim(raw);
    if (line.empty()) return;

    if (line == "HELP" || line == "?") {
        ESP_LOGW(TAG, "commands:");
        for (const auto& c : Commands()) {
            ESP_LOGW(TAG, "  %-14s %s", c.prefix.c_str(), c.help.c_str());
        }
        return;
    }
    for (const auto& c : Commands()) {
        if (line.rfind(c.prefix, 0) == 0) {
            c.handler(line);
            return;
        }
    }
    // Only for something that LOOKS like a command. The console also receives
    // whatever a terminal sends on connect, and answering that with an error is
    // how a working port looks broken.
    if (line.find(' ') == std::string::npos || line.find('_') != std::string::npos) {
        ESP_LOGW(TAG, "unknown command: %s (try HELP)", line.c_str());
    }
}

void SerialConsole::Task(void*) {
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = 1024;
    cfg.tx_buffer_size = 256;
    const esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "no serial commands: driver install failed (%s)", esp_err_to_name(err));
        vTaskDelete(nullptr);
        return;
    }
    ESP_LOGI(TAG, "serial console up, %d commands - send HELP", (int)Commands().size());

    std::string s;
    uint8_t buf[64];
    for (;;) {
        const int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(500));
        for (int i = 0; i < n; i++) {
            const char c = static_cast<char>(buf[i]);
            if (c != '\n' && c != '\r') {
                if (s.size() < kMaxLine) s.push_back(c);
                continue;
            }
            if (s.empty()) continue;
            Dispatch(s);
            s.clear();
        }
    }
}

void SerialConsole::Start() {
    if (started) return;
    started = true;
    xTaskCreate(Task, "serial_console", 4096, nullptr, 1, nullptr);
}
