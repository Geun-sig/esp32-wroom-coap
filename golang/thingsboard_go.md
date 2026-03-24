# ThingsBoard Go Client 구현 문서

## 목적
ThingsBoard Cloud에 연결된 ESP32-CoAP-01 디바이스의 텔레메트리 데이터를 조회하고,
LED를 RPC로 제어하는 Go PC 클라이언트. Python 버전과 동일한 기능을 제공한다.

---

## 환경 정보

| 항목 | 값 |
|------|-----|
| 언어 | Go 1.21+ |
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

- 이메일/비밀번호는 소스코드에 저장하지 않음 (보안)
- 최초 실행 시 로그인 화면에서 입력 → 로그인 성공 시 `~/.tb_credentials`에 JSON으로 저장
- 이후 실행 시 자동 로드, 로그인 화면 없이 바로 시작
- **계정 변경** 버튼 클릭 시 로그인 화면으로 전환 (취소 가능)
- 새 로그인 성공 시 기존 자격증명 삭제 후 새 자격증명 저장

---

## 설정 (`config.go`)

```go
const (
    TBHost   = "https://thingsboard.cloud"
    DeviceID = "675987f0-2725-11f1-9cdc-43ca8fc8dcc9"
)
```

DEVICE_ID 확인 방법:
```
Entities → Devices → ESP32-CoAP-01 클릭
URL: thingsboard.cloud/entities/devices/{DEVICE_ID}
```

---

## 파일 구조

```
golang/
├── thingsboard_go.md     # 이 문서
├── go.mod                # 모듈 및 의존성
├── go.sum                # 의존성 체크섬 (자동 생성)
├── config.go             # TB_HOST, DEVICE_ID 상수 / 자격증명 파일 관리
├── client.go             # ThingsBoard REST API 클라이언트
├── graph.go              # gonum/plot 그래프 렌더링
└── main.go               # fyne GUI (로그인 + 메인 화면)
```

> 자격증명: `~/.tb_credentials` (프로젝트 외부, JSON 형식)

---

## 주요 의존성

| 패키지 | 역할 |
|--------|------|
| `fyne.io/fyne/v2` | GUI 프레임워크 (다크 테마) |
| `gonum.org/v1/plot` | 히스토리 그래프 렌더링 |
| 표준 라이브러리 `net/http` | ThingsBoard REST API 통신 |

---

## 구현 기능

### `client.go` — ThingsBoard API 클라이언트

| 함수 | 설명 |
|------|------|
| `NewTBClient(email, password)` | JWT 로그인 후 클라이언트 생성 |
| `GetLatest()` | 최신 텔레메트리 조회 (value, count) |
| `GetHistory(hours)` | 히스토리 조회 (시작~현재, ASC 정렬) |
| `SetLed(state bool)` | RPC setLed on/off 전송 |
| `GetDeviceStatus()` | 디바이스 이름 및 active 상태 반환 |

### `graph.go` — 그래프 렌더링

- `makePlot()`: gonum/plot으로 단일 플롯 생성 (다크 테마 색상 적용)
- `renderOnePlot()`: 플롯을 image.Image로 렌더링
- `RenderHistory()`: value/count 두 서브플롯을 수직으로 합쳐 반환
- X축: Unix 타임스탬프 → `HH:MM` 포맷 커스텀 틱
- 선 색상: value=#4DABF7 (파랑), count=#69DB7C (초록)

### `main.go` — fyne GUI

단일 윈도우에서 로그인/메인 화면을 전환하는 방식.

#### 로그인 화면 (400×300)
- 이메일 / 비밀번호 입력
- 비밀번호 표시/숨김 토글 버튼
- 로그인 성공 시 자격증명 저장 후 메인 화면 전환
- 기존 계정이 있는 경우 **취소** 버튼 표시 (메인 화면으로 복귀)

#### 메인 화면 (1150×580) — 좌우 분할 (HSplit)

**왼쪽 패널 (31%)**

| UI 요소 | 기능 |
|---------|------|
| 디바이스 상태 | 온라인/오프라인 표시 |
| LED ON 버튼 (초록) | RPC setLed on 전송 |
| LED OFF 버튼 (빨강) | RPC setLed off 전송 |
| 텔레메트리 표시 박스 | value, count 최신값 + 타임스탬프 |
| 최신값 조회 버튼 | GET timeseries 호출 |
| 최근 1시간 버튼 | 오른쪽 패널에 히스토리 그래프 표시 |
| 최근 24시간 버튼 | 오른쪽 패널에 히스토리 그래프 표시 |
| 계정 변경 버튼 | 로그인 화면으로 전환 |
| 하단 상태바 | API 호출 결과 메시지 |

**오른쪽 패널 (69%)**
- gonum/plot으로 렌더링한 image.Image를 canvas.Image로 표시
- 초기: 어두운 빈 배경
- 히스토리 버튼 클릭 시 value / count 서브플롯 표시

#### 구현 특이사항
- API 호출은 고루틴(goroutine)으로 실행 (UI 블로킹 방지)
- 그래프 갱신: `myWindow.Canvas().Refresh(graphImg)` 사용
- 한글 폰트: `FYNE_FONT=C:\Windows\Fonts\malgun.ttf` (맑은 고딕) 환경변수로 설정
- 그래프 다크 테마: `vgimg.UseBackgroundColor` + 축/텍스트 색상 white 설정

---

## 빌드 및 실행

```bash
cd golang

# 최초 1회: 의존성 다운로드 (시간 소요)
go mod tidy

# 기본 빌드
go build -o tb_client.exe .

# 콘솔 창 없이 GUI 전용 빌드 + 심볼 제거 (권장)
go build -ldflags "-H windowsgui -s -w" -o tb_client.exe .

# 실행
.\tb_client.exe
```

---

## 실행 파일 크기 최소화

Go는 모든 의존성을 정적 링크하므로 기본 빌드 시 약 38MB.
아래 순서로 최소화 가능:

### 1단계: 빌드 시 심볼 제거

```bash
go build -ldflags "-H windowsgui -s -w" -o tb_client.exe .
```

- `-s`: 심볼 테이블 제거
- `-w`: DWARF 디버그 정보 제거
- 결과: 약 20~25MB

### 2단계: UPX 압축

```bash
# 균형 (권장)
upx --best tb_client.exe

# 최대 압축 (시간 많이 소요)
upx --brute tb_client.exe
```

- `--best`: 약 10~15MB
- `--brute`: 약 8~10MB

> UPX 설치: https://upx.github.io

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
