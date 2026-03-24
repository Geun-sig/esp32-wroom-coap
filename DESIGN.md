# ESP32 WROOM CoAP + ThingsBoard Cloud 설계

## 개요

ESP32 WROOM에서 ThingsBoard Cloud로 CoAP 프로토콜을 이용해 Telemetry를 주기적으로 전송하고,
Server-side RPC를 통해 LED를 제어하는 프로그램.
WiFi 인증정보와 ThingsBoard Access Token은 SoftAP 웹 설정 페이지를 통해 NVS에 저장하며,
최대 3개 네트워크를 지원한다.

---

## 환경

| 항목 | 내용 |
|------|------|
| 보드 | ESP32 DevKitC WROOM-32D |
| 개발환경 | ESP-IDF v6.1-dev |
| 서버 | ThingsBoard Cloud (`coap.thingsboard.cloud:5683`) |
| 프로토콜 | CoAP over UDP (평문, 포트 5683) |
| CoAP 라이브러리 | `espressif/coap` (libcoap 4.x 기반) |
| ThingsBoard 장치명 | `ESP32-CoAP-01` |

---

## CoAP 엔드포인트

| 목적 | 메서드 | URI |
|------|--------|-----|
| Telemetry 전송 | CON POST | `coap://coap.thingsboard.cloud/api/v1/{token}/telemetry` |
| RPC 구독 | GET + Observe(0) | `coap://coap.thingsboard.cloud/api/v1/{token}/rpc` |
| RPC 응답 | CON POST | `coap://coap.thingsboard.cloud/api/v1/{token}/rpc/{requestId}` |

---

## Payload

### Telemetry (ESP32 → ThingsBoard)

```json
{"value": 42, "count": 1}
```

| 필드 | 타입 | 설명 |
|------|------|------|
| value | int | 0~99 랜덤값 (`esp_random()` 사용) |
| count | int | 전송 누적 횟수 (1부터 증가) |

### RPC 요청 (ThingsBoard → ESP32, Observe 알림)

```json
{"method": "setLed", "params": {"state": "on"}, "id": 1}
```

| 필드 | 타입 | 설명 |
|------|------|------|
| method | string | RPC 메서드명 (`setLed`) |
| params.state | string | `on` / `off` |
| id | int | requestId (응답 URI에 사용) |

### RPC 응답 (ESP32 → ThingsBoard)

```json
{"result": "ok"}
```

---

## LED 제어

| 항목 | 내용 |
|------|------|
| GPIO | GPIO4 |
| 연결 | GPIO4 → 저항(220Ω) → LED(+) → GND |
| state: on | GPIO4 HIGH → LED 켜짐 |
| state: off | GPIO4 LOW → LED 꺼짐 |

---

## WiFi 설정 (SoftAP + STA)

### 동작 모드

ESP32는 `WIFI_MODE_APSTA`로 SoftAP와 STA를 동시에 운영한다.

| 인터페이스 | 역할 |
|-----------|------|
| SoftAP | 항상 켜짐. 사용자 접속 및 WiFi / Access Token 설정 수신 |
| STA | NVS에 저장된 인증정보로 실제 WiFi 접속. CoAP 동작 |

### SoftAP 설정

| 항목 | 값 |
|------|-----|
| SSID | `ESP32-Config` |
| 보안 | 오픈 (인증 없음) |
| 채널 | 1 |
| 최대 접속 | 4대 |
| IP | 192.168.4.1 |

### 웹 설정 페이지

- 접속: `ESP32-Config` 연결 → 브라우저에서 `http://192.168.4.1`
- 최대 3개 SSID / Password 입력
- **ThingsBoard Access Token** 입력 필드 (비밀번호 표시/숨김 토글)
- 저장 즉시 NVS 기록 및 STA 재연결 시도

### NVS 저장 구조

| 네임스페이스 | 키 | 내용 |
|-------------|-----|------|
| `wifi_cfg` | `ssid1` ~ `ssid3` | WiFi SSID (최대 32바이트) |
| `wifi_cfg` | `pass1` ~ `pass3` | WiFi Password (최대 64바이트) |
| `wifi_cfg` | `tb_token` | ThingsBoard Access Token (최대 32바이트) |

### 연결 우선순위 및 폴오버

- 부팅 시 `ssid1` → `ssid2` → `ssid3` 순서로 첫 번째 유효 항목에 연결
- 연결 실패(disconnect) 시 다음 네트워크로 자동 스위치 (라운드로빈)
- STA 연결 성공 시 콜백을 통해 CoAP 태스크 시작 (최초 1회)
- STA 재연결 시 `s_coap_started` 리셋 → CoAP 태스크 재시작

### WiFi 재연결 루프 방지

ESP-IDF v5/v6에서 `esp_wifi_disconnect()`를 이미 끊어진 STA에 호출하면
`WIFI_EVENT_STA_DISCONNECTED`가 즉시 발생해 무한루프가 된다.

- `try_connect()`에서 `esp_wifi_disconnect()` 제거
- 웹 저장(`handle_save()`)에서만 명시적으로 `esp_wifi_disconnect()` 호출

---

## 동작 흐름

```
[부팅]
  │
  ▼
led_init()  ← GPIO4 출력 설정
  │
  ▼
wifi_init(coap_start 콜백 등록)
  ├─ SoftAP 시작 → "ESP32-Config" 오픈 핫스팟
  ├─ HTTP 서버 시작 → http://192.168.4.1
  ├─ NVS에서 WiFi 인증정보 + Access Token 로드
  └─ 인증정보 있음 → STA 연결 시도
        │
        ▼ (IP 획득 시)
  coap_start() 콜백 호출 (s_coap_started=false 일 때만)
        │
        ▼
  coap_task 생성 (스택 8192)
        ├─ wifi_get_tb_token() → 토큰 로드
        ├─ getaddrinfo("coap.thingsboard.cloud", "5683") DNS 조회
        │     └─ 실패 시 2초 간격으로 최대 3회 재시도
        ├─ coap_new_context() + coap_new_client_session()
        ├─ RPC Observe 등록 (GET + Observe=0)
        └─ 메인 루프
              ├─ coap_io_process(ctx, 1000ms) → RPC 알림 수신
              └─ 3분 경과 시 CON POST → /api/v1/{token}/telemetry

[STA disconnect 이벤트]
  ├─ s_coap_started = false
  └─ 다음 저장된 네트워크로 자동 재연결
        └─ IP 획득 시 coap_start() 재호출 → coap_task 재생성

[Observe 알림 수신]
  └─ method: "setLed"
        ├─ state: "on"  → GPIO4 HIGH
        └─ state: "off" → GPIO4 LOW
  └─ CON POST → /api/v1/{token}/rpc/{id}  {"result":"ok"}

[HTTP POST /save 이벤트]
  └─ SSID/PW + Access Token 파싱 → NVS 저장
  └─ esp_wifi_disconnect() → try_connect() → STA 재연결
```

---

## CoAP 설정

| 항목 | 값 |
|------|-----|
| 서버 호스트 | `coap.thingsboard.cloud` |
| 포트 | 5683 (UDP) |
| 메시지 타입 | CON (Confirmable) |
| Content-Format | 50 (`application/json`) |
| Observe 등록 | GET Observe=0 (구독) |
| Telemetry 주기 | 3분 (180초) |

---

## DNS 조회

`coap_resolve_address_info()`는 ESP-IDF lwIP 환경에서 동작하지 않는다.
ESP-IDF 네이티브 `getaddrinfo()`를 사용한다.

```c
#include "lwip/netdb.h"

struct addrinfo hints = { .ai_family = AF_INET, .ai_socktype = SOCK_DGRAM };
struct addrinfo *res = NULL;
getaddrinfo("coap.thingsboard.cloud", "5683", &hints, &res);

coap_address_t dst_addr;
coap_address_init(&dst_addr);
dst_addr.addr.sin = *(struct sockaddr_in *)res->ai_addr;
dst_addr.size = sizeof(struct sockaddr_in);
freeaddrinfo(res);
```

---

## 컴포넌트 의존성

### idf_component.yml

```yaml
dependencies:
  idf:
    version: ">=4.1.0"
  espressif/coap:
    version: ">=4.3.3~1"
```

### main/CMakeLists.txt REQUIRES

```cmake
REQUIRES esp_wifi nvs_flash esp_event esp_netif
         espressif__coap esp_driver_gpio esp_http_server
```

### sdkconfig.defaults

```
CONFIG_COAP_MBEDTLS_PSK=n
CONFIG_COAP_MBEDTLS_PKI=n
CONFIG_COAP_CLIENT_SUPPORT=y
CONFIG_COAP_TCP_SUPPORT=n
CONFIG_MBEDTLS_SSL_PROTO_DTLS=y
```

> **주의**: `coap_mbedtls.c`는 PSK/PKI 설정과 무관하게 항상 컴파일되며
> `mbedtls_ssl_cookie_free()`를 참조한다. `CONFIG_MBEDTLS_SSL_PROTO_DTLS=y`
> 없이는 링크 오류 발생. `idf.py fullclean` 후 빌드해야 적용된다.

---

## wifi.h 인터페이스

```c
typedef void (*wifi_connected_cb_t)(void);

void        wifi_init(wifi_connected_cb_t on_connected);
const char *wifi_get_tb_token(void);   // NVS에서 로드된 Access Token 반환
```

---

## 프로젝트 구조

```
coap/
├── CMakeLists.txt
├── CLAUDE.md
├── DESIGN.md
├── sdkconfig.defaults
└── main/
    ├── CMakeLists.txt        # 컴포넌트 의존성
    ├── idf_component.yml     # espressif/coap 외부 컴포넌트
    ├── main.c                # CoAP 태스크, telemetry, RPC observe, LED 제어
    ├── wifi.c                # APSTA, HTTP 설정 서버, NVS 인증정보+토큰 관리
    └── wifi.h                # wifi_init(), wifi_get_tb_token() 인터페이스
```

---

## 시리얼 모니터링

```bash
idf.py flash monitor
```

정상 동작 로그:

```
I (xxx) WIFI: SoftAP started -> SSID: 'ESP32-Config'
I (xxx) WIFI: HTTP server ready -> http://192.168.4.1
I (xxx) WIFI: Trying 'MyHomeWiFi'...
I (xxx) WIFI: STA got IP: 192.168.0.10
I (xxx) COAP: Token: xxxxxxxxxxxxxxxxxxxx
I (xxx) COAP: DNS resolved: coap.thingsboard.cloud
I (xxx) COAP: CoAP session ready -> coap.thingsboard.cloud:5683
I (xxx) COAP: RPC observe registered
I (xxx) COAP: Telemetry sent -> {"value":23,"count":1}
I (xxx) COAP: RPC received: {"method":"setLed","params":{"state":"on"},"id":1}
I (xxx) COAP: LED ON
I (xxx) COAP: RPC response sent -> id=1
```

---

## ThingsBoard Dashboard 설정

### Telemetry 확인
- Devices → `ESP32-CoAP-01` → **Latest Telemetry** 탭
- `value`, `count` 키 수신 확인

### LED RPC 제어
- Dashboard → Add widget → **Control widgets** → **Button**
- RPC method: `setLed`
- Params: `{"state":"on"}` (ON) / `{"state":"off"}` (OFF)

---

## 확정 사항

- [x] MQTTS → CoAP 변경 (coap.thingsboard.cloud:5683, UDP 평문)
- [x] ThingsBoard Cloud 연동 (장치: `ESP32-CoAP-01`)
- [x] Access Token: 웹 설정 페이지 입력 → NVS 저장 (`tb_token`, 32바이트)
- [x] DNS: `coap_resolve_address_info()` 대신 `getaddrinfo()` 사용 (ESP-IDF lwIP 호환)
- [x] WiFi 재연결 루프 수정: `try_connect()`에서 `esp_wifi_disconnect()` 제거
- [x] sdkconfig.defaults: `CONFIG_MBEDTLS_SSL_PROTO_DTLS=y` 추가 (링크 오류 해결)
- [x] WiFi APSTA 모드: SoftAP 항상 켜짐 + STA 연결
- [x] WiFi 설정 웹 페이지: http://192.168.4.1 (Access Token 입력 필드 추가)
- [x] WiFi 인증정보 NVS 저장 (최대 3개 네트워크)
- [x] 연결 실패 시 다음 네트워크로 자동 폴오버
- [x] STA 재연결 시 CoAP 태스크 재시작 (`s_coap_started` 리셋)
- [x] Telemetry payload: `{"value": N, "count": N}` (3분 주기) → ThingsBoard 수신 확인
- [x] RPC payload: `{"method": "setLed", "params": {"state": "on/off"}, "id": N}`
- [x] RPC 응답: `{"result": "ok"}`
- [x] LED 제어: GPIO4, on/off
