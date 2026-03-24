# CLAUDE.md

## 프로젝트 개요

ESP32 DevKitC WROOM-32D에서 ThingsBoard Cloud로 **CoAP** Telemetry 전송 및 RPC 수신하는 ESP-IDF 프로젝트.
- 3분 주기로 랜덤 데이터 Telemetry 전송 (CoAP CON POST)
- Server-side RPC 수신 시 GPIO4 LED 제어 (CoAP Observe)
- SoftAP + STA 동시 동작 / 웹 페이지로 WiFi 및 Access Token 설정

---

## 개발 환경

- **보드**: ESP32 DevKitC WROOM-32D
- **프레임워크**: ESP-IDF v6.1-dev
- **서버**: ThingsBoard Cloud (`coap.thingsboard.cloud:5683`, 평문 UDP)
- **CoAP 컴포넌트**: `espressif/coap` (idf_component.yml로 관리)
- **ThingsBoard 장치명**: `ESP32-CoAP-01`

---

## 주요 설정값

| 항목 | 값 |
|------|-----|
| ThingsBoard CoAP 호스트 | `coap.thingsboard.cloud` |
| CoAP 포트 | 5683 (UDP 평문) |
| Telemetry 엔드포인트 | `/api/v1/{ACCESS_TOKEN}/telemetry` |
| RPC 엔드포인트 | `/api/v1/{ACCESS_TOKEN}/rpc` |
| Telemetry 전송 주기 | 3분 (180초) |
| LED GPIO | GPIO4 |
| SoftAP SSID | `ESP32-Config` |
| SoftAP 보안 | 오픈 (인증 없음) |
| 설정 페이지 | http://192.168.4.1 |
| 저장 가능 WiFi | 최대 3개 (NVS 저장) |
| Access Token 저장 | NVS (`wifi_cfg` 네임스페이스, 키: `tb_token`) |

---

## ThingsBoard 인증

- **방식**: Access Token (ThingsBoard 장치별 고유 토큰)
- **위치**: CoAP URI 경로에 포함 (`/api/v1/{token}/...`)
- **설정**: 웹 설정 페이지 (`http://192.168.4.1`) 에서 입력 → NVS 저장
- **NVS 키**: `wifi_cfg` 네임스페이스 / `tb_token` 키 (최대 32바이트)

---

## CoAP 통신

| 방향 | 방법 | URI | Payload |
|------|------|-----|---------|
| Telemetry 전송 (ESP32 → TB) | CON POST | `/api/v1/{token}/telemetry` | `{"value":N,"count":N}` |
| RPC 구독 (TB → ESP32) | GET + Observe(0) | `/api/v1/{token}/rpc` | (ThingsBoard에서 전송) |
| RPC 응답 (ESP32 → TB) | CON POST | `/api/v1/{token}/rpc/{requestId}` | `{"result":"ok"}` |

---

## RPC Payload (ThingsBoard → ESP32)

```json
{"method": "setLed", "params": {"state": "on"}, "id": 1}
{"method": "setLed", "params": {"state": "off"}, "id": 2}
```

| 필드 | 값 | 설명 |
|------|-----|------|
| method | `setLed` | RPC 메서드명 |
| params.state | `on` / `off` | LED 상태 |
| id | int | requestId (응답 URI에 사용) |

---

## LED 제어

- **핀**: GPIO4
- **연결**: GPIO4 → 저항(220Ω) → LED(+) → GND
- `state: on` → LED 켜짐 (GPIO4 HIGH)
- `state: off` → LED 꺼짐 (GPIO4 LOW)

---

## WiFi + Access Token 설정 방법

1. 스마트폰/PC에서 **`ESP32-Config`** WiFi 접속 (비밀번호 없음)
2. 브라우저에서 **`http://192.168.4.1`** 접속
3. 최대 3개 SSID / Password 입력
4. **ThingsBoard Access Token** 입력 (장치 Credentials에서 복사)
5. 저장 즉시 STA 연결 시도 (1번 → 2번 → 3번 순서)
6. 연결 성공 시 CoAP 통신 자동 시작

---

## NVS 저장 구조

| 네임스페이스 | 키 | 내용 |
|-------------|-----|------|
| `wifi_cfg` | `ssid1` ~ `ssid3` | WiFi SSID (최대 32바이트) |
| `wifi_cfg` | `pass1` ~ `pass3` | WiFi Password (최대 64바이트) |
| `wifi_cfg` | `tb_token` | ThingsBoard Access Token (최대 32바이트) |

---

## 파일 구조

```
coap/
├── CLAUDE.md
├── DESIGN.md
├── CMakeLists.txt
├── sdkconfig.defaults        # CoAP/mbedTLS 필수 설정
├── main/
│   ├── CMakeLists.txt        # REQUIRES에 espressif__coap, esp_driver_gpio, esp_http_server 포함
│   ├── idf_component.yml     # espressif/coap 의존성
│   ├── main.c                # CoAP 초기화, coap_task (telemetry + RPC observe), LED 제어
│   ├── wifi.c                # APSTA 초기화, NVS, HTTP 설정 서버 (토큰 입력 포함)
│   └── wifi.h                # wifi_init(callback), wifi_get_tb_token() 인터페이스
├── python/
│   ├── thingsboard.md        # Python PC 클라이언트 구현 문서
│   └── thingsboard_client/
│       ├── config.py         # TB_HOST, DEVICE_ID 설정
│       ├── tb_client.py      # ThingsBoardClient 클래스
│       ├── main.py           # customtkinter GUI
│       └── requirements.txt  # requests, matplotlib, customtkinter
└── golang/
    ├── thingsboard_go.md     # Go PC 클라이언트 구현 문서
    ├── go.mod                # 모듈 및 의존성
    ├── config.go             # TB_HOST, DEVICE_ID 상수 / 자격증명 관리
    ├── client.go             # ThingsBoard REST API 클라이언트
    ├── graph.go              # gonum/plot 그래프 렌더링
    └── main.go               # fyne GUI (로그인 + 메인 화면)
```

---

## 빌드 명령

```bash
idf.py build
idf.py flash monitor
```

---

## 주의 사항

- **CoAP 컴포넌트**: `espressif/coap` (idf_component.yml 관리), CMakeLists.txt REQUIRES에 `espressif__coap` 사용
- **CoAP 헤더**: `#include "coap3/coap.h"`
- **DNS 조회**: `coap_resolve_address_info()`는 ESP-IDF lwIP에서 동작하지 않음 → `getaddrinfo()` + `lwip/netdb.h` 사용
- **CoAP Observe**: UDP 기반, STA 재연결 시 `s_coap_started` 리셋 후 재시작
- **WiFi 재연결 루프 방지**: `try_connect()`에서 `esp_wifi_disconnect()` 제거, `handle_save()`에서만 호출
- **sdkconfig.defaults 필수**: `CONFIG_MBEDTLS_SSL_PROTO_DTLS=y` — `coap_mbedtls.c`가 항상 컴파일되므로 mbedTLS DTLS 활성화 필요
- **Content-Format**: `application/json` (코드 50) 필요
- **RPC 응답**: requestId 추출 후 `/api/v1/{token}/rpc/{id}`로 POST 필수
- **GPIO**: `esp_driver_gpio`를 REQUIRES에 추가 필요
- **WiFi 인증정보 및 Access Token**: NVS `wifi_cfg` 네임스페이스에 저장

---

## sdkconfig.defaults

```
CONFIG_COAP_MBEDTLS_PSK=n
CONFIG_COAP_MBEDTLS_PKI=n
CONFIG_COAP_CLIENT_SUPPORT=y
CONFIG_COAP_TCP_SUPPORT=n
CONFIG_MBEDTLS_SSL_PROTO_DTLS=y
```

> `idf.py fullclean` 후 빌드 시 적용됨

---

## ThingsBoard Cloud 설정 절차

1. [thingsboard.cloud](https://thingsboard.cloud) 계정 생성 (완료)
2. Devices → 장치 `ESP32-CoAP-01` 등록 (완료)
3. 장치 상세 → Credentials → Access Token 복사
4. ESP32 설정 페이지(`http://192.168.4.1`)에서 Access Token 입력 후 저장
5. ThingsBoard Dashboard → Add widget → Control widgets → Button
   - RPC method: `setLed`
   - Params ON: `{"state":"on"}` / Params OFF: `{"state":"off"}`

---

## 설계 문서

상세 설계는 `DESIGN.md` 참조.
