#include "ota.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <new>

#include "config.h"
#include "secrets.h"

namespace {
constexpr size_t kMaxManifestBytes = 2048;
constexpr size_t kMaxSignatureBytes = 160;
constexpr size_t kChunkBytes = 1024;
constexpr uint32_t kHttpTimeoutMs = 15000;
constexpr char kBoard[] = "esp32-wroom-32";
constexpr char kChannel[] = "stable";

Preferences otaPrefs;
volatile bool otaBusy = false;
portMUX_TYPE otaMux = portMUX_INITIALIZER_UNLOCKED;
char otaStateBuffer[48] = "disabled";

struct Manifest {
  String version;
  String imageUrl;
  String sha256;
  uint32_t securityVersion = 0;
  uint32_t imageSize = 0;
  String signature;
};

void setState(const char* state) {
  portENTER_CRITICAL(&otaMux);
  strlcpy(otaStateBuffer, state, sizeof(otaStateBuffer));
  portEXIT_CRITICAL(&otaMux);
}

bool cleanAscii(const String& value, size_t maxLength) {
  if (!value.length() || value.length() > maxLength) return false;
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t c = static_cast<uint8_t>(value[i]);
    if (c < 0x21 || c > 0x7e) return false;
  }
  return true;
}

bool secureUrl(const String& value) {
  return value.startsWith("https://")
    && cleanAscii(value, 240)
    && value.indexOf('@') < 0;
}

bool parseVersion(const String& value, uint32_t parts[3]) {
  if (!cleanAscii(value, 32)) return false;
  size_t start = 0;
  for (size_t part = 0; part < 3; ++part) {
    const int dot = value.indexOf('.', start);
    const size_t end = (part == 2 || dot < 0) ? value.length() : static_cast<size_t>(dot);
    if (end <= start || (part < 2 && dot < 0) || (part == 2 && dot >= 0)) return false;
    uint32_t number = 0;
    for (size_t i = start; i < end; ++i) {
      const char c = value[i];
      if (c < '0' || c > '9') return false;
      number = number * 10U + static_cast<uint32_t>(c - '0');
    }
    parts[part] = number;
    start = end + 1;
  }
  return true;
}

int compareVersion(const String& left, const String& right) {
  uint32_t a[3] = {}, b[3] = {};
  if (!parseVersion(left, a) || !parseVersion(right, b)) return -2;
  for (size_t i = 0; i < 3; ++i) {
    if (a[i] < b[i]) return -1;
    if (a[i] > b[i]) return 1;
  }
  return 0;
}

String canonicalManifest(const Manifest& manifest) {
  String payload;
  payload.reserve(500);
  payload += "schema=1\n";
  payload += "board=";
  payload += kBoard;
  payload += "\nversion=" + manifest.version;
  payload += "\nsecurity_version=" + String(manifest.securityVersion);
  payload += "\nchannel=";
  payload += kChannel;
  payload += "\nimage_url=" + manifest.imageUrl;
  payload += "\nimage_size=" + String(manifest.imageSize);
  payload += "\nsha256=" + manifest.sha256;
  payload += "\n";
  return payload;
}

bool verifyManifestSignature(const Manifest& manifest) {
  if (!HID_OTA_PUBLIC_KEY[0]) return false;

  uint8_t signature[kMaxSignatureBytes] = {};
  size_t signatureLength = 0;
  const int decodeResult = mbedtls_base64_decode(
    signature, sizeof(signature), &signatureLength,
    reinterpret_cast<const unsigned char*>(manifest.signature.c_str()),
    manifest.signature.length()
  );
  if (decodeResult != 0 || signatureLength == 0) return false;

  const String payload = canonicalManifest(manifest);
  const mbedtls_md_info_t* sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!sha256) return false;

  uint8_t digest[32] = {};
  mbedtls_md_context_t md;
  mbedtls_md_init(&md);
  int result = mbedtls_md_setup(&md, sha256, 0);
  if (result == 0) result = mbedtls_md_starts(&md);
  if (result == 0) result = mbedtls_md_update(
    &md, reinterpret_cast<const unsigned char*>(payload.c_str()), payload.length()
  );
  if (result == 0) result = mbedtls_md_finish(&md, digest);
  mbedtls_md_free(&md);
  if (result != 0) return false;

  mbedtls_pk_context publicKey;
  mbedtls_pk_init(&publicKey);
  result = mbedtls_pk_parse_public_key(
    &publicKey,
    reinterpret_cast<const unsigned char*>(HID_OTA_PUBLIC_KEY),
    strlen(HID_OTA_PUBLIC_KEY) + 1
  );
  if (result == 0 && !mbedtls_pk_can_do(&publicKey, MBEDTLS_PK_ECDSA)) result = -1;
  if (result == 0) {
    result = mbedtls_pk_verify(
      &publicKey, MBEDTLS_MD_SHA256, digest, sizeof(digest), signature, signatureLength
    );
  }
  mbedtls_pk_free(&publicKey);
  return result == 0;
}

bool parseManifest(const String& raw, Manifest& manifest) {
  if (!raw.length() || raw.length() > kMaxManifestBytes) return false;
  JsonDocument doc;
  if (deserializeJson(doc, raw)) return false;

  if ((doc["schema"] | 0) != 1) return false;
  if (String(doc["board"] | "") != kBoard) return false;
  if (String(doc["channel"] | "") != kChannel) return false;

  manifest.version = String(doc["version"] | "");
  manifest.securityVersion = doc["security_version"] | 0U;
  manifest.imageUrl = String(doc["image_url"] | "");
  manifest.imageSize = doc["image_size"] | 0U;
  manifest.sha256 = String(doc["sha256"] | "");
  manifest.signature = String(doc["signature"] | "");

  uint32_t parsedVersion[3] = {};
  if (!parseVersion(manifest.version, parsedVersion)) return false;
  if (!secureUrl(manifest.imageUrl)) return false;
  if (manifest.imageSize < 1024 || manifest.sha256.length() != 64) return false;
  if (!cleanAscii(manifest.signature, 256)) return false;
  for (size_t i = 0; i < manifest.sha256.length(); ++i) {
    const char c = manifest.sha256[i];
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return verifyManifestSignature(manifest);
}

const esp_partition_t* findAppPartitionAt(uint32_t address) {
  esp_partition_iterator_t iterator = esp_partition_find(
    ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, nullptr
  );
  const esp_partition_t* found = nullptr;
  while (iterator) {
    const esp_partition_t* candidate = esp_partition_get(iterator);
    if (candidate && candidate->address == address) {
      found = candidate;
      break;
    }
    iterator = esp_partition_next(iterator);
  }
  if (iterator) esp_partition_iterator_release(iterator);
  return found;
}

bool downloadManifest(const String& url, Manifest& manifest) {
  WiFiClientSecure tls;
  tls.setCACert(HID_OTA_ROOT_CA);
  tls.setTimeout(kHttpTimeoutMs);
  HTTPClient client;
  client.setTimeout(kHttpTimeoutMs);
  client.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!client.begin(tls, url)) return false;

  const int status = client.GET();
  const int length = client.getSize();
  if (status != HTTP_CODE_OK || length <= 0 || length > static_cast<int>(kMaxManifestBytes)) {
    client.end();
    return false;
  }
  const String body = client.getString();
  client.end();
  return body.length() == static_cast<size_t>(length) && parseManifest(body, manifest);
}

bool versionAllowed(const Manifest& manifest) {
  const String activeVersion = otaPrefs.getString("version", config::FIRMWARE_VERSION);
  const uint32_t activeSecurity = otaPrefs.getUInt("security", config::FIRMWARE_SECURITY_VERSION);
  return compareVersion(manifest.version, activeVersion) == 1
    && manifest.securityVersion >= activeSecurity;
}

bool downloadAndInstall(const Manifest& manifest) {
  const esp_partition_t* nextPartition = esp_ota_get_next_update_partition(nullptr);
  if (!nextPartition || manifest.imageSize > nextPartition->size) return false;

  WiFiClientSecure tls;
  tls.setCACert(HID_OTA_ROOT_CA);
  tls.setTimeout(kHttpTimeoutMs);
  HTTPClient client;
  client.setTimeout(kHttpTimeoutMs);
  client.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!client.begin(tls, manifest.imageUrl)) return false;

  const int status = client.GET();
  const int contentLength = client.getSize();
  if (status != HTTP_CODE_OK || contentLength < 0
      || static_cast<uint32_t>(contentLength) != manifest.imageSize) {
    client.end();
    return false;
  }

  if (!Update.begin(manifest.imageSize, U_FLASH)) {
    client.end();
    return false;
  }

  const mbedtls_md_info_t* sha256 = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  mbedtls_md_context_t md;
  mbedtls_md_init(&md);
  int hashResult = sha256 ? mbedtls_md_setup(&md, sha256, 0) : -1;
  if (hashResult == 0) hashResult = mbedtls_md_starts(&md);

  uint8_t buffer[kChunkBytes];
  uint32_t remaining = manifest.imageSize;
  Stream* stream = client.getStreamPtr();
  if (stream) stream->setTimeout(kHttpTimeoutMs);

  while (remaining > 0 && hashResult == 0) {
    const size_t want = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
    const size_t received = stream ? stream->readBytes(buffer, want) : 0;
    if (received == 0 || Update.write(buffer, received) != received) {
      hashResult = -1;
      break;
    }
    hashResult = mbedtls_md_update(&md, buffer, received);
    remaining -= static_cast<uint32_t>(received);
    if (hashResult == 0) setState("downloading_image");
  }

  uint8_t digest[32] = {};
  if (hashResult == 0) hashResult = mbedtls_md_finish(&md, digest);
  mbedtls_md_free(&md);
  client.end();

  if (hashResult != 0 || remaining != 0) {
    Update.abort();
    return false;
  }

  char actualHash[65] = {};
  for (size_t i = 0; i < sizeof(digest); ++i) {
    snprintf(actualHash + (i * 2), 3, "%02x", digest[i]);
  }
  if (manifest.sha256 != actualHash) {
    Update.abort();
    return false;
  }

  // Persist pending metadata before Update.end() changes the boot partition.
  if (otaPrefs.putString("pending_version", manifest.version) == 0
      || otaPrefs.putUInt("pending_security", manifest.securityVersion) == 0
      || otaPrefs.putUInt("previous_addr", esp_ota_get_running_partition()->address) == 0) {
    Update.abort();
    otaPrefs.remove("pending_version");
    otaPrefs.remove("pending_security");
    otaPrefs.remove("previous_addr");
    return false;
  }

  if (!Update.end()) {
    Update.abort();
    otaPrefs.remove("pending_version");
    otaPrefs.remove("pending_security");
    otaPrefs.remove("previous_addr");
    return false;
  }

  setState("rebooting");
  delay(750);
  ESP.restart();
  return true;
}

void otaWorker(void* argument) {
  String* manifestUrl = static_cast<String*>(argument);
  delay(1200); // Give the HTTP 202 response time to leave the device.
  setState("verifying_manifest");
  Manifest manifest;
  bool success = downloadManifest(*manifestUrl, manifest);
  if (success) success = versionAllowed(manifest);
  if (success) {
    setState("downloading_image");
    success = downloadAndInstall(manifest);
  }
  delete manifestUrl;

  if (!success) {
    setState("failed");
    otaBusy = false;
  }
  vTaskDelete(nullptr);
}
} // namespace

void otaBegin() {
  otaPrefs.begin("hidota", false);
  if (otaReady()) setState("idle");
  else setState("disabled");
}

bool otaReady() {
  return HID_OTA_ROOT_CA[0] != '\0' && HID_OTA_PUBLIC_KEY[0] != '\0';
}

bool otaIsBusy() {
  return otaBusy;
}

String otaState() {
  char current[sizeof(otaStateBuffer)] = {};
  portENTER_CRITICAL(&otaMux);
  strlcpy(current, otaStateBuffer, sizeof(current));
  portEXIT_CRITICAL(&otaMux);
  return String(current);
}

String otaCurrentVersion() {
  return otaPrefs.getString("version", config::FIRMWARE_VERSION);
}

bool otaStartUpdate(const String& manifestUrl) {
  if (!otaReady() || otaBusy || !secureUrl(manifestUrl) || WiFi.status() != WL_CONNECTED) {
    return false;
  }
  String* url = new (std::nothrow) String(manifestUrl);
  if (!url) return false;

  otaBusy = true;
  setState("starting");
  const BaseType_t created = xTaskCreatePinnedToCore(
    otaWorker, "hid-ota", 12288, url, 1, nullptr, 0
  );
  if (created != pdPASS) {
    delete url;
    otaBusy = false;
    setState("failed");
    return false;
  }
  return true;
}

void otaConfirmPendingBoot(bool startupHealthy) {
  const String pendingVersion = otaPrefs.getString("pending_version", "");
  if (!pendingVersion.length()) return;

  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  const esp_err_t stateResult = esp_ota_get_state_partition(running, &state);

  // A bootloader without rollback support must not silently accept the image.
  if (stateResult != ESP_OK || state == ESP_OTA_IMG_UNDEFINED) {
    const uint32_t previousAddress = otaPrefs.getUInt("previous_addr", 0);
    const esp_partition_t* previous = findAppPartitionAt(previousAddress);
    if (previous) {
      esp_ota_set_boot_partition(previous);
      delay(150);
      ESP.restart();
    }
    otaPrefs.remove("pending_version");
    otaPrefs.remove("pending_security");
    otaPrefs.remove("previous_addr");
    setState("rollback_unavailable");
    return;
  }

  if (state == ESP_OTA_IMG_PENDING_VERIFY && !startupHealthy) {
    esp_ota_mark_app_invalid_rollback_and_reboot();
    return;
  }
  if (!startupHealthy) {
    const uint32_t previousAddress = otaPrefs.getUInt("previous_addr", 0);
    const esp_partition_t* previous = findAppPartitionAt(previousAddress);
    if (previous) {
      esp_ota_set_boot_partition(previous);
      delay(150);
      ESP.restart();
    }
    setState("health_check_failed");
    return;
  }

  if (state == ESP_OTA_IMG_PENDING_VERIFY
      && esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
    esp_ota_mark_app_invalid_rollback_and_reboot();
    return;
  }

  otaPrefs.putString("version", pendingVersion);
  otaPrefs.putUInt("security", otaPrefs.getUInt("pending_security", config::FIRMWARE_SECURITY_VERSION));
  otaPrefs.remove("pending_version");
  otaPrefs.remove("pending_security");
  otaPrefs.remove("previous_addr");
  setState("confirmed");
}
