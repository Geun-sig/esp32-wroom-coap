package main

import (
	"fmt"
	"image"
	"image/color"
	"os"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/app"
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"
)

var (
	myApp    fyne.App
	myWindow fyne.Window
	tbClient *TBClient
)

func main() {
	os.Setenv("FYNE_FONT", `C:\Windows\Fonts\malgun.ttf`)
	myApp = app.New()
	myApp.Settings().SetTheme(theme.DarkTheme())
	myWindow = myApp.NewWindow("ThingsBoard ESP32-CoAP-01 클라이언트")
	myWindow.SetMaster()

	creds, err := LoadCredentials()
	if err == nil && creds != nil {
		c, err := NewTBClient(creds.Email, creds.Password)
		if err == nil {
			tbClient = c
			showMainScreen()
			myWindow.ShowAndRun()
			return
		}
	}
	showLoginScreen()
	myWindow.ShowAndRun()
}

// ── 로그인 화면 ───────────────────────────────────────────────
func showLoginScreen() {
	myWindow.Resize(fyne.NewSize(400, 300))
	myWindow.SetTitle("ThingsBoard 로그인")

	titleLabel := widget.NewLabelWithStyle("ThingsBoard 로그인",
		fyne.TextAlignCenter, fyne.TextStyle{Bold: true})

	emailEntry := widget.NewEntry()
	emailEntry.SetPlaceHolder("email@example.com")

	passEntry := widget.NewEntry()
	passEntry.Password = true
	passEntry.SetPlaceHolder("password")

	showPass := false
	var eyeBtn *widget.Button
	eyeBtn = widget.NewButton("표시", func() {
		showPass = !showPass
		passEntry.Password = !showPass
		passEntry.Refresh()
		if showPass {
			eyeBtn.SetText("숨김")
		} else {
			eyeBtn.SetText("표시")
		}
	})
	eyeBtn.Importance = widget.LowImportance

	errLabel := canvas.NewText("", color.RGBA{R: 220, G: 53, B: 69, A: 255})
	errLabel.TextSize = 12

	loginBtn := widget.NewButton("로그인", func() {
		email := emailEntry.Text
		pass := passEntry.Text
		if email == "" || pass == "" {
			errLabel.Text = "이메일과 비밀번호를 입력하세요."
			errLabel.Refresh()
			return
		}
		errLabel.Text = "로그인 중..."
		errLabel.Color = color.RGBA{R: 150, G: 150, B: 150, A: 255}
		errLabel.Refresh()
		go func() {
			c, err := NewTBClient(email, pass)
			if err != nil {
				errLabel.Text = err.Error()
				errLabel.Color = color.RGBA{R: 220, G: 53, B: 69, A: 255}
				errLabel.Refresh()
				return
			}
			DeleteCredentials()
			SaveCredentials(email, pass)
			tbClient = c
			showMainScreen()
		}()
	})
	loginBtn.Importance = widget.HighImportance

	passRow := container.NewBorder(nil, nil, nil, eyeBtn, passEntry)

	items := []fyne.CanvasObject{
		titleLabel,
		widget.NewSeparator(),
		widget.NewLabel("이메일"),
		emailEntry,
		widget.NewLabel("비밀번호"),
		passRow,
		errLabel,
		loginBtn,
	}

	// 기존 클라이언트가 있으면 취소 버튼 추가
	if tbClient != nil {
		cancelBtn := widget.NewButton("취소", func() {
			showMainScreen()
		})
		cancelBtn.Importance = widget.LowImportance
		items = append(items, cancelBtn)
	}

	myWindow.SetContent(container.NewPadded(container.NewVBox(items...)))
}

// ── 메인 화면 ─────────────────────────────────────────────────
func showMainScreen() {
	myWindow.Resize(fyne.NewSize(1150, 580))
	myWindow.SetTitle("ThingsBoard ESP32-CoAP-01 클라이언트")

	// 상태 레이블
	statusLabel := widget.NewLabelWithStyle("상태: 확인 중...",
		fyne.TextAlignCenter, fyne.TextStyle{})

	// 텔레메트리 표시
	teleLabel := widget.NewLabelWithStyle("최신값 조회 버튼을 누르세요",
		fyne.TextAlignLeading, fyne.TextStyle{})
	teleLabel.Wrapping = fyne.TextWrapWord
	teleBox := container.NewStack(
		canvas.NewRectangle(color.RGBA{R: 40, G: 40, B: 40, A: 255}),
		container.NewPadded(teleLabel),
	)

	// 상태바
	statusBar := widget.NewLabel("")
	setStatus := func(msg string) {
		statusBar.SetText(msg)
	}

	// 그래프 이미지 (초기: 어두운 배경)
	placeholder := image.NewRGBA(image.Rect(0, 0, 650, 400))
	for y := 0; y < 400; y++ {
		for x := 0; x < 650; x++ {
			placeholder.Set(x, y, color.RGBA{R: 30, G: 30, B: 30, A: 255})
		}
	}
	graphImg := canvas.NewImageFromImage(placeholder)
	graphImg.FillMode = canvas.ImageFillContain
	graphImg.SetMinSize(fyne.NewSize(700, 440))

	graphTitle := widget.NewLabelWithStyle("히스토리 그래프",
		fyne.TextAlignCenter, fyne.TextStyle{Bold: true})

	// LED 버튼
	ledOnBtn := widget.NewButton("LED ON", func() {
		setStatus("LED ON 전송 중...")
		go func() {
			if err := tbClient.SetLed(true); err != nil {
				setStatus("오류: " + err.Error())
				return
			}
			setStatus("LED ON 완료")
		}()
	})
	ledOnBtn.Importance = widget.SuccessImportance

	ledOffBtn := widget.NewButton("LED OFF", func() {
		setStatus("LED OFF 전송 중...")
		go func() {
			if err := tbClient.SetLed(false); err != nil {
				setStatus("오류: " + err.Error())
				return
			}
			setStatus("LED OFF 완료")
		}()
	})
	ledOffBtn.Importance = widget.DangerImportance

	// 최신값 조회
	latestBtn := widget.NewButton("최신값 조회", func() {
		setStatus("조회 중...")
		go func() {
			data, err := tbClient.GetLatest()
			if err != nil {
				setStatus("오류: " + err.Error())
				return
			}
			text := ""
			for k, v := range data {
				text += fmt.Sprintf("%s: %s\n(%s)\n\n", k, v.Value, v.Time)
			}
			if text == "" {
				text = "데이터 없음"
			}
			teleLabel.SetText(text)
			setStatus("조회 완료")
		}()
	})

	// 히스토리 그래프
	plotHistory := func(hours int) {
		setStatus(fmt.Sprintf("최근 %d시간 데이터 로딩 중...", hours))
		go func() {
			history, err := tbClient.GetHistory(hours)
			if err != nil {
				setStatus("오류: " + err.Error())
				return
			}
			img := RenderHistory(history, hours)
			graphImg.Image = img
			graphTitle.SetText(fmt.Sprintf("히스토리 — 최근 %d시간", hours))
			myWindow.Canvas().Refresh(graphImg)
			setStatus("그래프 표시 완료")
		}()
	}

	hist1Btn := widget.NewButton("최근 1시간", func() { plotHistory(1) })
	hist24Btn := widget.NewButton("최근 24시간", func() { plotHistory(24) })

	// 계정 변경
	changeBtn := widget.NewButton("계정 변경", func() {
		showLoginScreen()
	})
	changeBtn.Importance = widget.LowImportance

	// 왼쪽 패널
	leftPanel := container.NewVBox(
		widget.NewLabelWithStyle("ESP32-CoAP-01", fyne.TextAlignCenter, fyne.TextStyle{Bold: true}),
		statusLabel,
		widget.NewSeparator(),
		widget.NewLabelWithStyle("LED 제어", fyne.TextAlignCenter, fyne.TextStyle{Bold: true}),
		container.NewGridWithColumns(2, ledOnBtn, ledOffBtn),
		widget.NewSeparator(),
		widget.NewLabelWithStyle("텔레메트리", fyne.TextAlignCenter, fyne.TextStyle{Bold: true}),
		teleBox,
		latestBtn,
		widget.NewSeparator(),
		widget.NewLabelWithStyle("히스토리 그래프", fyne.TextAlignCenter, fyne.TextStyle{Bold: true}),
		container.NewGridWithColumns(2, hist1Btn, hist24Btn),
		widget.NewSeparator(),
		changeBtn,
		statusBar,
	)

	// 오른쪽 패널
	rightPanel := container.NewBorder(graphTitle, nil, nil, nil, graphImg)

	// 전체 레이아웃 (좌우 분할)
	split := container.NewHSplit(
		container.NewScroll(leftPanel),
		rightPanel,
	)
	split.SetOffset(0.31)

	myWindow.SetContent(split)

	// 디바이스 상태 초기 조회
	go func() {
		name, active, err := tbClient.GetDeviceStatus()
		if err != nil {
			statusLabel.SetText("상태 조회 실패: " + err.Error())
			return
		}
		status := "오프라인"
		if active {
			status = "온라인"
		}
		statusLabel.SetText(fmt.Sprintf("상태: %s  (%s)", status, name))
	}()
}
