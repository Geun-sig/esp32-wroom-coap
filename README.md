# ESP32 CoAP ThingsBoard Client

ESP32 DevKitC WROOM-32D에서 **CoAP** 프로토콜로 ThingsBoard Cloud에 텔레메트리를 전송하고 RPC로 LED를 제어하는 프로젝트.

PC 클라이언트(Python / Go)로 텔레메트리 조회 및 LED 제어가 가능합니다.

---

## 주요 기능

| 기능 | 설명 |
|------|------|
| Telemetry 전송 | 3분 주기로 `value`, `count` 랜덤 데이터 전송 (CoAP CON POST) |
| RPC 수신 | ThingsBoard Server-side RPC로 GPIO4 LED 제어 (CoAP Observe) |
| WiFi 설정 | SoftAP(`ESP32-Config`) + 웹 페이지(`http://192.168.4.1`)로 WiFi 및 Access Token 설정 |
| PC 클라이언트 | Python / Go GUI로 텔레메트리 조회, 히스토리 그래프, LED ON/OFF 제어 |

---

## 시스템 구성

```
ThingsBoard Cloud
       ↕ CoAP (UDP 5683)
  ESP32 WROOM-32D
       │
       └─ GPIO4 → LED

PC Client (Python / Go)
       ↕ REST API (HTTPS)
ThingsBoard Cloud
```

---

## 개발 환경

| 항목 | 값 |
|------|-----|
| 보드 | ESP32 DevKitC WROOM-32D |
| 프레임워크 | ESP-IDF v6.1-dev |
| CoAP 서버 | `coap.thingsboard.cloud:5683` (UDP 평문) |
| CoAP 컴포넌트 | `espressif/coap` |
| ThingsBoard 장치명 | `ESP32-CoAP-01` |

---

## 하드웨어 연결

```
GPIO4 → 저항(220Ω) → LED(+) → GND
```

---

## 파일 구조

```
coap/
├── README.md
├── CMakeLists.txt
├── sdkconfig.defaults          # CoAP/mbedTLS 필수 설정
├── main/
│   ├── CMakeLists.txt
│   ├── idf_component.yml       # espressif/coap 의존성
│   ├── main.c                  # CoAP 태스크 (telemetry + RPC observe), LED 제어
│   ├── wifi.c                  # APSTA 초기화, NVS, HTTP 설정 서버
│   └── wifi.h
├── python/
│   ├── thingsboard.md          # Python 클라이언트 상세 문서
│   └── thingsboard_client/
│       ├── config.py           # TB_HOST, DEVICE_ID 설정
│       ├── tb_client.py        # ThingsBoard REST API 클라이언트
│       ├── main.py             # customtkinter GUI
│       └── requirements.txt
└── golang/
    ├── thingsboard_go.md       # Go 클라이언트 상세 문서
    ├── go.mod
    ├── config.go
    ├── client.go
    ├── graph.go
    └── main.go
```

---

## ESP32 펌웨어 빌드 및 플래시

```bash
idf.py build
idf.py flash monitor
```

---

## WiFi 및 Access Token 설정

1. `ESP32-Config` WiFi 접속 (비밀번호 없음)
2. 브라우저에서 `http://192.168.4.1` 접속
3. SSID / Password 입력 (최대 3개)
4. ThingsBoard **Access Token** 입력 (장치 Credentials에서 복사)
5. 저장 후 자동으로 STA 연결 및 CoAP 통신 시작

---

## CoAP 통신

| 방향 | 방법 | URI | Payload |
|------|------|-----|---------|
| Telemetry 전송 | CON POST | `/api/v1/{token}/telemetry` | `{"value":N,"count":N}` |
| RPC 구독 | GET + Observe | `/api/v1/{token}/rpc` | — |
| RPC 응답 | CON POST | `/api/v1/{token}/rpc/{id}` | `{"result":"ok"}` |

---

## PC 클라이언트

ESP32와 독립적으로 ThingsBoard REST API를 통해 동작합니다.
이메일/비밀번호는 `~/.tb_credentials`에 저장되며 소스코드에 포함되지 않습니다.

### Python 클라이언트

```bash
cd python/thingsboard_client
pip install -r requirements.txt
python main.py
```

- Python 3.x
- customtkinter GUI (다크 테마)
- matplotlib 히스토리 그래프

### Go 클라이언트

```bash
cd golang
go mod tidy
go build -ldflags "-H windowsgui -s -w" -o tb_client.exe .
upx --best tb_client.exe   # 선택: 실행 파일 압축
.\tb_client.exe
```

- Go 1.21+
- fyne GUI (다크 테마)
- gonum/plot 히스토리 그래프

### PC 클라이언트 주요 기능

| 기능 | 설명 |
|------|------|
| 디바이스 상태 | 온라인/오프라인 표시 |
| 최신 텔레메트리 조회 | value, count 최신값 + 타임스탬프 |
| 히스토리 그래프 | 최근 1시간 / 24시간 인라인 그래프 |
| LED 제어 | ON / OFF RPC 전송 |
| 계정 관리 | 최초 로그인 후 자동 저장, 계정 변경 가능 |

---

## ThingsBoard Cloud 설정

1. [thingsboard.cloud](https://thingsboard.cloud) 계정 생성
2. Devices → 장치 `ESP32-CoAP-01` 등록
3. 장치 상세 → Credentials → Access Token 복사
4. ESP32 웹 설정 페이지에서 Access Token 입력
5. (선택) Dashboard → Button 위젯 추가
   - RPC method: `setLed`
   - Params ON: `{"state":"on"}` / OFF: `{"state":"off"}`

---

## 라이선스

MIT
