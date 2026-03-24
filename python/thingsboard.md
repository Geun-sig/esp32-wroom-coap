# ThingsBoard PC Client 구현 문서

## 목적
ThingsBoard Cloud에 연결된 ESP32-CoAP-01 디바이스의 텔레메트리 데이터를 조회하고,
LED를 RPC로 제어하는 Python PC 클라이언트.

---

## 환경 정보

| 항목 | 값 |
|------|-----|
| ThingsBoard Cloud URL | `https://thingsboard.cloud` |
| 디바이스 이름 | `ESP32-CoAP-01` |
| 인증 방식 | JWT (이메일/비밀번호 로그인) |
| 텔레메트리 키 | `value`, `count` |
| RPC method | `setLed` |
| RPC params (ON) | `{"state": "on"}` |
| RPC params (OFF) | `{"state": "off"}` |

---

## 인증 방식

ThingsBoard Cloud REST API는 **API Key 인증을 지원하지 않음**.
JWT 토큰 방식만 허용:

1. `POST /api/auth/login` 에 이메일/비밀번호 전송 → JWT 토큰 수신
2. 이후 모든 요청에 `X-Authorization: Bearer {token}` 헤더 사용

> Google OAuth로 가입한 경우 "Forgot Password"로 비밀번호를 별도 설정해야 함.

---

## 자격증명 저장

- 이메일/비밀번호는 **`config.py`에 저장하지 않음** (보안)
- 최초 실행 시 로그인 화면에서 입력 → 로그인 성공 시 `~/.tb_credentials`에 JSON으로 저장
- 이후 실행 시 자동 로드, 로그인 화면 없이 바로 시작
- **계정 변경** 버튼 클릭 시 저장된 자격증명 삭제 후 로그인 화면으로 전환

---

## 설정 파일 구조

### `config.py`
```python
TB_HOST   = "https://thingsboard.cloud"
DEVICE_ID = "여기에_DEVICE_ID_입력"
```

### DEVICE_ID 확인 방법
ThingsBoard Cloud 브라우저에서:
```
Entities → Devices → ESP32-CoAP-01 클릭
URL: thingsboard.cloud/entities/devices/{DEVICE_ID}
```

---

## 구현 기능

### 1. ThingsBoardClient 클래스 (`tb_client.py`)

#### 초기화 및 로그인
- 생성자에서 이메일/비밀번호를 인자로 받음
- `POST /api/auth/login` → JWT 토큰 수신
- requests 세션 헤더에 `X-Authorization: Bearer {token}` 설정

#### 최신 텔레메트리 조회 (`get_latest()`)
- API: `GET /api/plugins/telemetry/DEVICE/{DEVICE_ID}/values/timeseries`
- params: `keys=value,count`
- 반환: 각 키의 최신값과 타임스탬프 (ms → datetime 변환)

#### 히스토리 조회 (`get_history(hours=1)`)
- API: `GET /api/plugins/telemetry/DEVICE/{DEVICE_ID}/values/timeseries`
- params: `keys=value,count`, `startTs`, `endTs`, `limit=1000`, `orderBy=ASC`
- 반환: `{"value": [(datetime, float), ...], "count": [(datetime, float), ...]}`

#### LED 제어 (`set_led(state: bool)`)
- API: `POST /api/plugins/rpc/oneway/{DEVICE_ID}`
- payload: `{"method": "setLed", "params": {"state": "on"|"off"}}`

#### 디바이스 상태 확인 (`get_device_status()`)
- API: `GET /api/device/{DEVICE_ID}`
- 반환: `{"name": str, "active": bool}`

---

### 2. GUI 메인 실행 파일 (`main.py`)

단일 CTk 창에서 로그인 화면과 메인 화면을 전환하는 방식 (CTkToplevel 미사용).

#### 로그인 화면 (360×320)
- 이메일 / 비밀번호 입력
- 비밀번호 표시/숨김 토글 버튼
- 로그인 성공 시 `~/.tb_credentials` 저장 후 메인 화면으로 전환
- Enter 키로 로그인 가능

#### 메인 화면 (1150×580) — 좌우 분할

**왼쪽 패널 (360px)**

| UI 요소 | 기능 |
|---------|------|
| 상단 상태 표시 | 디바이스 온라인/오프라인 (초록/빨강) |
| LED ON 버튼 | RPC setLed on 전송 |
| LED OFF 버튼 | RPC setLed off 전송 |
| 최신값 조회 버튼 | value, count 최신값 + 타임스탬프 표시 |
| 최근 1시간 버튼 | 오른쪽 패널에 히스토리 그래프 표시 |
| 최근 24시간 버튼 | 오른쪽 패널에 히스토리 그래프 표시 |
| 계정 변경 버튼 | 자격증명 삭제 후 로그인 화면으로 전환 |
| 하단 상태바 | API 호출 결과 메시지 |

**오른쪽 패널**
- matplotlib 그래프 영역 (인라인, 별도 창 없음)
- 초기에는 안내 문구 표시
- 히스토리 버튼 클릭 시 value / count 서브플롯 표시

#### 구현 특이사항
- API 호출은 별도 스레드에서 실행 (UI 블로킹 방지)
- 종료 시 `sys.exit(0)` 으로 즉시 프로세스 종료 (tkinter after 콜백 오류 방지)
- CTkToplevel 미사용 → 화면 전환 시 after 콜백 오류 없음

---

## 파일 구조

```
python/
├── thingsboard.md              # 이 문서
└── thingsboard_client/
    ├── config.py               # TB_HOST, DEVICE_ID 설정 (비밀번호 없음)
    ├── tb_client.py            # ThingsBoardClient 클래스
    ├── main.py                 # customtkinter GUI (로그인 + 메인 화면)
    └── requirements.txt        # requests, matplotlib, customtkinter
```

> 자격증명: `~/.tb_credentials` (프로젝트 외부, JSON 형식)

---

## requirements.txt

```
requests
matplotlib
customtkinter
```

---

## 에러 처리

- HTTP 401: 인증 오류 → 하단 상태바에 메시지 표시
- HTTP 404: Device ID 오류 → 하단 상태바에 메시지 표시
- ConnectionError: 네트워크 오류 → 로그인 화면에 메시지 표시
- 모든 API 호출은 별도 스레드 + try/except로 처리

---

## 실행 방법

```bash
cd python/thingsboard_client
pip install -r requirements.txt
python main.py
```

---

## 참고: ThingsBoard REST API

| 기능 | Method | URL |
|------|--------|-----|
| 로그인 (JWT 발급) | POST | `/api/auth/login` |
| 최신 텔레메트리 | GET | `/api/plugins/telemetry/DEVICE/{id}/values/timeseries` |
| 히스토리 | GET | `/api/plugins/telemetry/DEVICE/{id}/values/timeseries?startTs=&endTs=` |
| RPC 전송 | POST | `/api/plugins/rpc/oneway/{id}` |
| 디바이스 정보 | GET | `/api/device/{id}` |
| 인증 헤더 | - | `X-Authorization: Bearer {jwtToken}` |
