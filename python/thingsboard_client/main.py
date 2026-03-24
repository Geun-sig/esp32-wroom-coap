import sys
import json
import threading
from pathlib import Path
import customtkinter as ctk
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import requests

from tb_client import ThingsBoardClient

ctk.set_appearance_mode("dark")
ctk.set_default_color_theme("blue")

CRED_PATH = Path.home() / ".tb_credentials"


def load_credentials():
    if CRED_PATH.exists():
        with open(CRED_PATH, "r") as f:
            return json.load(f)
    return None


def save_credentials(email, password):
    with open(CRED_PATH, "w") as f:
        json.dump({"email": email, "password": password}, f)


def delete_credentials():
    if CRED_PATH.exists():
        CRED_PATH.unlink()


class App(ctk.CTk):
    def __init__(self):
        super().__init__()
        self.title("ThingsBoard ESP32-CoAP-01 클라이언트")
        self.resizable(False, False)
        self.protocol("WM_DELETE_WINDOW", self._on_close)

        self._alive = True
        self._canvas = None
        self._fig = None
        self._client = None
        self._current_frame = None

        creds = load_credentials()
        if creds:
            self._show_main(creds)
        else:
            self._show_login()

    # ── 화면 전환 ──────────────────────────────────────────────
    def _clear_frame(self):
        if self._current_frame:
            self._current_frame.destroy()
            self._current_frame = None

    def _show_login(self):
        self.geometry("360x320")
        self._clear_frame()
        self._show_pass = False

        frame = ctk.CTkFrame(self, fg_color="transparent")
        frame.pack(fill="both", expand=True)
        self._current_frame = frame

        ctk.CTkLabel(frame, text="ThingsBoard 로그인",
                     font=ctk.CTkFont(size=15, weight="bold")).pack(pady=(24, 16))

        ctk.CTkLabel(frame, text="이메일").pack(anchor="w", padx=40)
        self._email_entry = ctk.CTkEntry(frame, width=280, placeholder_text="email@example.com")
        self._email_entry.pack(padx=40, pady=(2, 12))

        ctk.CTkLabel(frame, text="비밀번호").pack(anchor="w", padx=40)
        pass_row = ctk.CTkFrame(frame, fg_color="transparent")
        pass_row.pack(padx=40, pady=(2, 4), anchor="w")
        self._pass_entry = ctk.CTkEntry(pass_row, width=240, show="*", placeholder_text="password")
        self._pass_entry.pack(side="left")
        self._eye_btn = ctk.CTkButton(pass_row, text="표시", width=36, fg_color="gray30",
                                      hover_color="gray20", command=self._toggle_pass)
        self._eye_btn.pack(side="left", padx=(4, 0))

        self._login_msg = ctk.CTkLabel(frame, text="", text_color="#dc3545",
                                       font=ctk.CTkFont(size=11))
        self._login_msg.pack(pady=(8, 0))

        ctk.CTkButton(frame, text="로그인", width=280,
                      command=self._do_login).pack(pady=(10, 16))
        self.bind("<Return>", lambda e: self._do_login())

    def _show_main(self, creds):
        self.geometry("1150x580")
        self._clear_frame()
        self.unbind("<Return>")

        self._client = ThingsBoardClient(creds["email"], creds["password"])

        frame = ctk.CTkFrame(self, fg_color="transparent")
        frame.pack(fill="both", expand=True)
        self._current_frame = frame

        left = ctk.CTkFrame(frame, width=360, corner_radius=0, fg_color="transparent")
        left.pack(side="left", fill="y", padx=(16, 8), pady=16)
        left.pack_propagate(False)

        right = ctk.CTkFrame(frame, corner_radius=8)
        right.pack(side="left", fill="both", expand=True, padx=(0, 16), pady=16)

        self._build_left(left)
        self._build_right(right)
        self._check_status()

    # ── 로그인 ─────────────────────────────────────────────────
    def _toggle_pass(self):
        self._show_pass = not self._show_pass
        self._pass_entry.configure(show="" if self._show_pass else "*")
        self._eye_btn.configure(text="숨김" if self._show_pass else "표시")

    def _do_login(self):
        email = self._email_entry.get().strip()
        password = self._pass_entry.get()
        if not email or not password:
            self._login_msg.configure(text="이메일과 비밀번호를 입력하세요.")
            return
        self._login_msg.configure(text="로그인 중...")
        self.update()
        try:
            resp = requests.post(
                "https://thingsboard.cloud/api/auth/login",
                json={"username": email, "password": password},
                timeout=10,
            )
            if resp.status_code == 401:
                self._login_msg.configure(text="이메일 또는 비밀번호가 올바르지 않습니다.")
                return
            resp.raise_for_status()
            save_credentials(email, password)
            self._show_main({"email": email, "password": password})
        except requests.ConnectionError:
            self._login_msg.configure(text="네트워크 연결을 확인하세요.")
        except Exception as e:
            self._login_msg.configure(text=f"오류: {e}")

    # ── 왼쪽 패널 ──────────────────────────────────────────────
    def _build_left(self, parent):
        ctk.CTkLabel(parent, text="ESP32-CoAP-01",
                     font=ctk.CTkFont(size=20, weight="bold")).pack(pady=(16, 4))
        self.status_label = ctk.CTkLabel(parent, text="상태: -", font=ctk.CTkFont(size=12))
        self.status_label.pack()

        led_frame = ctk.CTkFrame(parent)
        led_frame.pack(pady=12, fill="x")
        ctk.CTkLabel(led_frame, text="LED 제어",
                     font=ctk.CTkFont(size=13, weight="bold")).pack(pady=(10, 6))
        btn_row = ctk.CTkFrame(led_frame, fg_color="transparent")
        btn_row.pack(pady=(0, 10))
        ctk.CTkButton(btn_row, text="LED ON", width=130, fg_color="#28a745",
                      hover_color="#1e7e34", command=lambda: self._led(True)).pack(side="left", padx=8)
        ctk.CTkButton(btn_row, text="LED OFF", width=130, fg_color="#dc3545",
                      hover_color="#a71d2a", command=lambda: self._led(False)).pack(side="left", padx=8)

        tele_frame = ctk.CTkFrame(parent)
        tele_frame.pack(pady=6, fill="x")
        ctk.CTkLabel(tele_frame, text="텔레메트리",
                     font=ctk.CTkFont(size=13, weight="bold")).pack(pady=(10, 4))
        self.tele_box = ctk.CTkTextbox(tele_frame, height=80, font=ctk.CTkFont(size=12))
        self.tele_box.pack(fill="x", padx=12, pady=(0, 6))
        self.tele_box.configure(state="disabled")
        ctk.CTkButton(tele_frame, text="최신값 조회", command=self._get_latest).pack(pady=(0, 10))

        hist_frame = ctk.CTkFrame(parent)
        hist_frame.pack(pady=6, fill="x")
        ctk.CTkLabel(hist_frame, text="히스토리 그래프",
                     font=ctk.CTkFont(size=13, weight="bold")).pack(pady=(10, 6))
        btn_row2 = ctk.CTkFrame(hist_frame, fg_color="transparent")
        btn_row2.pack(pady=(0, 10))
        ctk.CTkButton(btn_row2, text="최근 1시간", width=130,
                      command=lambda: self._plot_history(1)).pack(side="left", padx=8)
        ctk.CTkButton(btn_row2, text="최근 24시간", width=130,
                      command=lambda: self._plot_history(24)).pack(side="left", padx=8)

        ctk.CTkButton(parent, text="계정 변경", width=280, fg_color="gray30",
                      hover_color="gray20", command=self._change_account).pack(pady=(16, 0))

        self.bottom_label = ctk.CTkLabel(parent, text="", font=ctk.CTkFont(size=11),
                                         text_color="gray")
        self.bottom_label.pack(pady=(10, 0))

    # ── 오른쪽 패널 ────────────────────────────────────────────
    def _build_right(self, parent):
        self.graph_title = ctk.CTkLabel(parent, text="히스토리 그래프",
                                        font=ctk.CTkFont(size=13, weight="bold"))
        self.graph_title.pack(pady=(10, 4))

        self.graph_frame = ctk.CTkFrame(parent, fg_color="#1e1e1e")
        self.graph_frame.pack(fill="both", expand=True, padx=10, pady=(0, 10))

        self.graph_placeholder = ctk.CTkLabel(
            self.graph_frame, text="← 히스토리 버튼을 누르면 그래프가 표시됩니다",
            text_color="gray", font=ctk.CTkFont(size=12))
        self.graph_placeholder.place(relx=0.5, rely=0.5, anchor="center")

    # ── 디바이스 상태 ──────────────────────────────────────────
    def _check_status(self):
        def task():
            try:
                info = self._client.get_device_status()
                color = "#28a745" if info["active"] else "#dc3545"
                text = f"상태: {'온라인' if info['active'] else '오프라인'}  ({info['name']})"
                if self._alive:
                    self.status_label.configure(text=text, text_color=color)
            except Exception as e:
                if self._alive:
                    self.status_label.configure(text=f"상태 조회 실패: {e}", text_color="orange")
        threading.Thread(target=task, daemon=True).start()

    # ── 최신 텔레메트리 ────────────────────────────────────────
    def _get_latest(self):
        self._set_bottom("조회 중...")
        def task():
            try:
                data = self._client.get_latest()
                lines = [f"  {k}: {v['value']}   ({v['time']})" for k, v in data.items()]
                text = "\n".join(lines) if lines else "데이터 없음"
                self.tele_box.configure(state="normal")
                self.tele_box.delete("1.0", "end")
                self.tele_box.insert("end", text)
                self.tele_box.configure(state="disabled")
                self._set_bottom("조회 완료")
            except Exception as e:
                self._set_bottom(f"오류: {e}")
        threading.Thread(target=task, daemon=True).start()

    # ── LED 제어 ───────────────────────────────────────────────
    def _led(self, state: bool):
        label = "ON" if state else "OFF"
        self._set_bottom(f"LED {label} 전송 중...")
        def task():
            try:
                self._client.set_led(state)
                self._set_bottom(f"LED {label} 완료")
            except Exception as e:
                self._set_bottom(f"오류: {e}")
        threading.Thread(target=task, daemon=True).start()

    # ── 히스토리 그래프 ────────────────────────────────────────
    def _plot_history(self, hours: int):
        self._set_bottom(f"최근 {hours}시간 데이터 로딩 중...")
        def task():
            try:
                history = self._client.get_history(hours)
                self.after(0, lambda: self._show_plot(history, hours))
                self._set_bottom("그래프 표시 완료")
            except Exception as e:
                self._set_bottom(f"오류: {e}")
        threading.Thread(target=task, daemon=True).start()

    def _show_plot(self, history, hours):
        if self._canvas:
            self._canvas.get_tk_widget().destroy()
            plt.close(self._fig)

        self.graph_placeholder.place_forget()
        self.graph_title.configure(text=f"히스토리 — 최근 {hours}시간")

        self._fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(7, 4), tight_layout=True)
        self._fig.patch.set_facecolor("#1e1e1e")

        for ax, key, color, label in [
            (ax1, "value", "#4dabf7", "value"),
            (ax2, "count", "#69db7c", "count"),
        ]:
            pts = history.get(key, [])
            if pts:
                times, vals = zip(*pts)
                ax.plot(times, vals, color=color, linewidth=1.5, marker="o", markersize=3)
            ax.set_ylabel(label, color="white")
            ax.set_facecolor("#1e1e1e")
            ax.tick_params(colors="white", labelsize=8)
            for spine in ax.spines.values():
                spine.set_edgecolor("#555")
            ax.xaxis_date()
            self._fig.autofmt_xdate(rotation=30)

        self._canvas = FigureCanvasTkAgg(self._fig, master=self.graph_frame)
        self._canvas.draw()
        self._canvas.get_tk_widget().pack(fill="both", expand=True)

    # ── 계정 변경 ──────────────────────────────────────────────
    def _change_account(self):
        delete_credentials()
        if self._canvas:
            self._canvas.get_tk_widget().destroy()
            plt.close(self._fig)
            self._canvas = None
            self._fig = None
        self._show_login()

    # ── 헬퍼 ──────────────────────────────────────────────────
    def _on_close(self):
        self._alive = False
        sys.exit(0)

    def _set_bottom(self, msg: str):
        if self._alive:
            try:
                self.after(0, lambda: self.bottom_label.configure(text=msg))
            except Exception:
                pass


if __name__ == "__main__":
    app = App()
    app.mainloop()
