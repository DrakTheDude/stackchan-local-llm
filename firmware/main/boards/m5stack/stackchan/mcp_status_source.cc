#include "mcp_status_source.h"

#include "serial_console.h"
#include "settings.h"

#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <cstdlib>
#include <cstring>

#define TAG "McpStatus"

namespace {

// NVS namespace. Nothing here ships with a value - an unprovisioned robot has
// no URL, and no URL means the feature does not exist.
constexpr const char kNs[] = "status";

constexpr int kDefaultSecs = 180;
constexpr int kMinSecs = 30;
constexpr int kMaxSecs = 3600;

// Sooner while it is not working, because the interesting case is the one where
// somebody has just typed the URL in and is watching for it to take.
constexpr int kPollFailMs = 30 * 1000;

// 🔴 Not 20s. The first poll used to land at 20s on the reference robot, and
//    the audio encoder started dropping frames at 21s:
//
//      W (21064) AudioService: Encode queue is full, dropping oldest frame
//
//    A TLS handshake is the most CPU-hungry thing this firmware does, and 20s
//    is exactly when he is greeting you. Ambient status is never worth a
//    stutter in his voice, so the first poll waits until the opening exchange
//    is over. See also the task priority in Start().
constexpr int kFirstPollMs = 45 * 1000;

constexpr int kMaxResponse = 12 * 1024;
constexpr size_t kMaxCards = 6;
// It gets spoken. A paragraph read aloud on a level change is not an alert.
constexpr size_t kMaxSummary = 200;

std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

std::string Field(cJSON* obj, const char* key) {
    cJSON* v = cJSON_GetObjectItem(obj, key);
    return cJSON_IsString(v) ? std::string(v->valuestring) : std::string();
}

}  // namespace

bool McpStatusSource::Configured() {
    Settings settings(kNs);
    return !settings.GetString("url").empty();
}

McpStatusSource::Level McpStatusSource::level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

std::string McpStatusSource::summary() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return summary_;
}

std::vector<StatusSource::Card> McpStatusSource::cards() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cards_;
}

int64_t McpStatusSource::age_seconds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (last_ok_us_ == 0) return -1;
    return (esp_timer_get_time() - last_ok_us_) / 1000000;
}

std::string McpStatusSource::CallTool() {
    std::string url, tool, token, ca;
    {
        Settings settings(kNs);
        url = settings.GetString("url");
        tool = settings.GetString("tool", "status.get");
        token = settings.GetString("token");
        ca = settings.GetString("ca");
    }
    if (url.empty()) return "";

    const std::string body = std::string(
        "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"") +
        tool + "\",\"arguments\":{}}}";

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.method = HTTP_METHOD_POST;
    cfg.timeout_ms = 8000;
    cfg.buffer_size = 2048;
    // A stored CA wins over the bundle: a private authority is the case this
    // exists for, and silently falling back to the public bundle would turn a
    // pinning mistake into a connection that merely fails later and elsewhere.
    if (!ca.empty()) {
        cfg.cert_pem = ca.c_str();
    } else if (url.rfind("https://", 0) == 0) {
        cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == nullptr) return "";

    std::string out;
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "application/json");
    if (!token.empty()) {
        const std::string auth =
            token.rfind("Bearer ", 0) == 0 ? token : "Bearer " + token;
        esp_http_client_set_header(client, "Authorization", auth.c_str());
    }

    do {
        if (esp_http_client_open(client, body.size()) != ESP_OK) {
            ESP_LOGW(TAG, "%s: connect failed (%s)", tool.c_str(), url.c_str());
            break;
        }
        if (esp_http_client_write(client, body.data(), body.size()) !=
            static_cast<int>(body.size())) {
            break;
        }
        if (esp_http_client_fetch_headers(client) < 0) break;

        const int status = esp_http_client_get_status_code(client);
        if (status != 200) {
            // Say WHICH failure. "no data" and "not allowed" need different
            // fixes, and a 404 here usually means the path is the web UI rather
            // than the MCP endpoint - which answers 200 on some servers, hence
            // the parse check below rather than trusting the status alone.
            const char* hint = "";
            if (status == 401 || status == 403) hint = " (token missing, wrong or revoked)";
            if (status == 404) hint = " (wrong path? the MCP endpoint is usually /mcp)";
            ESP_LOGW(TAG, "%s: HTTP %d%s", tool.c_str(), status, hint);
            break;
        }

        char chunk[512];
        int n;
        while ((n = esp_http_client_read(client, chunk, sizeof(chunk))) > 0) {
            out.append(chunk, n);
            if (static_cast<int>(out.size()) > kMaxResponse) break;
        }
    } while (false);

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    if (out.empty()) return "";

    // Unwrap {"result":{"content":[{"type":"text","text":"..."}]}}
    std::string text;
    cJSON* root = cJSON_Parse(out.c_str());
    if (root != nullptr) {
        cJSON* error = cJSON_GetObjectItem(root, "error");
        if (error != nullptr) {
            // A JSON-RPC error is a REPLY, not a transport failure, and the
            // commonest one by far is a tool name that does not exist there.
            ESP_LOGW(TAG, "%s: server returned an error - %s", tool.c_str(),
                     Field(error, "message").c_str());
        }
        cJSON* result = cJSON_GetObjectItem(root, "result");
        cJSON* content = result ? cJSON_GetObjectItem(result, "content") : nullptr;
        cJSON* first = content ? cJSON_GetArrayItem(content, 0) : nullptr;
        cJSON* t = first ? cJSON_GetObjectItem(first, "text") : nullptr;
        if (cJSON_IsString(t)) text = t->valuestring;
        cJSON_Delete(root);
    }
    if (text.empty()) {
        ESP_LOGW(TAG, "%s: no text content in the reply", tool.c_str());
    }
    return text;
}

bool McpStatusSource::Parse(const std::string& text) {
    if (Trim(text).empty()) return false;

    Level level = Level::kUnknown;
    std::string summary;
    std::vector<Card> cards;

    cJSON* root = cJSON_Parse(text.c_str());
    if (root != nullptr && cJSON_IsObject(root)) {
        const std::string lv = Field(root, "level");
        // 🔴 Unrecognised stays UNKNOWN. The one thing never to do here is infer
        //    a colour from the words in the summary: a ring that guesses green
        //    is the failure this whole feature is supposed to prevent.
        if (lv == "ok") level = Level::kOk;
        else if (lv == "warn") level = Level::kWarn;
        else if (lv == "alert") level = Level::kAlert;
        else if (!lv.empty()) ESP_LOGW(TAG, "level \"%s\" is not ok/warn/alert", lv.c_str());

        summary = Trim(Field(root, "summary"));

        cJSON* arr = cJSON_GetObjectItem(root, "cards");
        const int n = cJSON_IsArray(arr) ? cJSON_GetArraySize(arr) : 0;
        for (int i = 0; i < n && cards.size() < kMaxCards; i++) {
            cJSON* c = cJSON_GetArrayItem(arr, i);
            if (!cJSON_IsObject(c)) continue;
            Card card{Field(c, "label"), Field(c, "value"), Field(c, "sub")};
            if (card.label.empty() && card.value.empty() && card.sub.empty()) continue;
            cards.push_back(std::move(card));
        }
    }
    if (root != nullptr) cJSON_Delete(root);

    // Not JSON, or JSON without a summary: show the words and keep the colour
    // honest. Reduced function, not a failure - plenty of useful tools return a
    // sentence, and a sentence on the idle screen is worth having.
    if (summary.empty()) summary = Trim(text);
    if (summary.size() > kMaxSummary) summary = summary.substr(0, kMaxSummary);

    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
    summary_ = std::move(summary);
    cards_ = std::move(cards);
    return true;
}

bool McpStatusSource::PollOnce() {
    const std::string text = CallTool();
    if (text.empty()) return false;
    if (!Parse(text)) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    last_ok_us_ = esp_timer_get_time();
    return true;
}

void McpStatusSource::PollTask(void* arg) {
    auto* self = static_cast<McpStatusSource*>(arg);
    vTaskDelay(pdMS_TO_TICKS(kFirstPollMs));
    for (;;) {
        int secs = kDefaultSecs;
        {
            Settings settings(kNs);
            secs = settings.GetInt("secs", kDefaultSecs);
        }
        if (secs < kMinSecs) secs = kMinSecs;
        if (secs > kMaxSecs) secs = kMaxSecs;

        // Re-read every time rather than caching: provisioning over serial
        // should take effect on the next poll, not on the next reboot. Somebody
        // typing a URL in wants to see it work while they are still looking.
        const bool ok = Configured() && self->PollOnce();
        if (ok) {
            static const char* kNames[] = {"unknown", "ok", "warn", "alert"};
            ESP_LOGI(TAG, "%s: %s (%d cards)",
                     kNames[static_cast<int>(self->level())], self->summary().c_str(),
                     static_cast<int>(self->cards().size()));
            if (self->on_update_) self->on_update_();
        }
        vTaskDelay(pdMS_TO_TICKS(ok ? secs * 1000 : kPollFailMs));
    }
}

void McpStatusSource::Start() {
    if (task_ != nullptr) return;
    // TLS needs real stack. 6KB is comfortable for one small POST at a time.
    //
    // 🔴 PRIORITY 1, BELOW THE AUDIO PIPELINE. At priority 3 the TLS handshake
    //    starved the encoder and AudioService began dropping frames. Nothing
    //    here is time-critical - the reading is three minutes old by design -
    //    so this task yields to anything that makes a sound.
    xTaskCreate(PollTask, "mcp_status", 6144, this, 1, &task_);
    ESP_LOGI(TAG, "ambient status poller started");
}

// 🔴 Provisioning over USB serial, on purpose.
//
//    A token must never be in this repository, in a build file, or in a chat
//    message. Putting it in a header would leave it one `cp` away from being
//    committed. So it arrives over the wire that already requires physically
//    holding the robot, and lands in NVS. Nothing is ever echoed back but a
//    length.
//
// The reading of the port is not ours - see serial_console.h, which explains
// why it cannot be, and why it must not be fgets(stdin).
void McpStatusSource::HandleProvisionLine(const std::string& raw) {
    const std::string s = Trim(raw);
    if (s.rfind("STATUS_", 0) != 0) return;

    const size_t sp = s.find(' ');
    const std::string cmd = s.substr(0, sp);
    const std::string arg = sp == std::string::npos ? "" : Trim(s.substr(sp + 1));

    if (cmd == "STATUS_SHOW") {
        Settings settings(kNs);
        const std::string token = settings.GetString("token");
        // Length only. Never the value - this goes to a log that gets pasted
        // into bug reports.
        ESP_LOGW(TAG, "url=%s tool=%s secs=%d ca=%s token=%s",
                 settings.GetString("url", "(none - status is off)").c_str(),
                 settings.GetString("tool", "status.get").c_str(),
                 (int)settings.GetInt("secs", kDefaultSecs),
                 settings.GetString("ca").empty() ? "no" : "yes",
                 token.empty() ? "no" : "yes");
        return;
    }
    if (cmd == "STATUS_OFF") {
        Settings settings(kNs, true);
        settings.EraseAll();
        ESP_LOGW(TAG, "status forgotten - URL, tool, token and CA. Reboot to stop polling.");
        return;
    }
    if (cmd == "STATUS_URL") {
        if (arg.rfind("http://", 0) != 0 && arg.rfind("https://", 0) != 0) {
            ESP_LOGW(TAG, "ignored: a URL starts with http:// or https://");
            return;
        }
        Settings settings(kNs, true);
        settings.SetString("url", arg);
        ESP_LOGW(TAG, "url stored: %s. Reboot if this is the first one.", arg.c_str());
        return;
    }
    if (cmd == "STATUS_TOOL") {
        Settings settings(kNs, true);
        settings.SetString("tool", arg);
        ESP_LOGW(TAG, "tool stored: %s", arg.c_str());
        return;
    }
    if (cmd == "STATUS_TOKEN") {
        Settings settings(kNs, true);
        settings.SetString("token", arg);
        ESP_LOGW(TAG, "token stored (%d chars)", static_cast<int>(arg.size()));
        return;
    }
    if (cmd == "STATUS_SECS") {
        int secs = atoi(arg.c_str());
        if (secs < kMinSecs || secs > kMaxSecs) {
            ESP_LOGW(TAG, "ignored: seconds must be %d..%d", kMinSecs, kMaxSecs);
            return;
        }
        Settings settings(kNs, true);
        settings.SetInt("secs", secs);
        ESP_LOGW(TAG, "interval stored: %ds", secs);
        return;
    }
    if (cmd == "STATUS_CA") {
        // A PEM is multi-line and this is a line protocol, so the newlines
        // arrive escaped. Nothing else is unescaped - \n is the only one a
        // certificate needs.
        std::string pem;
        for (size_t i = 0; i < arg.size(); i++) {
            if (arg[i] == '\\' && i + 1 < arg.size() && arg[i + 1] == 'n') {
                pem.push_back('\n');
                i++;
            } else {
                pem.push_back(arg[i]);
            }
        }
        if (pem.rfind("-----BEGIN CERTIFICATE-----", 0) != 0) {
            ESP_LOGW(TAG, "ignored: that does not start with -----BEGIN CERTIFICATE-----");
            return;
        }
        if (!pem.empty() && pem.back() != '\n') pem.push_back('\n');
        Settings settings(kNs, true);
        settings.SetString("ca", pem);
        ESP_LOGW(TAG, "CA stored (%d bytes)", static_cast<int>(pem.size()));
        return;
    }
    ESP_LOGW(TAG, "unknown command: %s", cmd.c_str());
}

void McpStatusSource::ProvisionFromSerial() {
    SerialConsole::Register("STATUS_", "URL TOOL TOKEN SECS CA SHOW OFF - ambient status",
                            [](const std::string& line) { HandleProvisionLine(line); });
    SerialConsole::Start();
}
