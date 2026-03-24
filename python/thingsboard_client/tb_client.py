import time
from datetime import datetime, timezone
import requests
import config


class ThingsBoardClient:
    def __init__(self, email: str, password: str):
        self.host = config.TB_HOST
        self.device_id = config.DEVICE_ID
        self.session = requests.Session()
        self._login(email, password)

    def _login(self, email: str, password: str):
        resp = self.session.post(
            f"{self.host}/api/auth/login",
            json={"username": email, "password": password},
            timeout=10,
        )
        resp.raise_for_status()
        token = resp.json()["token"]
        self.session.headers.update({"X-Authorization": f"Bearer {token}"})

    def get_latest(self):
        url = f"{self.host}/api/plugins/telemetry/DEVICE/{self.device_id}/values/timeseries"
        resp = self.session.get(url, params={"keys": "value,count"}, timeout=10)
        resp.raise_for_status()
        result = {}
        for key, values in resp.json().items():
            ts_ms = values[0]["ts"]
            dt = datetime.fromtimestamp(ts_ms / 1000, tz=timezone.utc).astimezone()
            result[key] = {
                "value": values[0]["value"],
                "time": dt.strftime("%Y-%m-%d %H:%M:%S"),
            }
        return result

    def get_history(self, hours=1):
        now_ms = int(time.time() * 1000)
        start_ms = now_ms - hours * 3600 * 1000
        url = f"{self.host}/api/plugins/telemetry/DEVICE/{self.device_id}/values/timeseries"
        params = {
            "keys": "value,count",
            "startTs": start_ms,
            "endTs": now_ms,
            "limit": 1000,
            "orderBy": "ASC",
        }
        resp = self.session.get(url, params=params, timeout=10)
        resp.raise_for_status()
        raw = resp.json()

        data_list = []
        count_list = []

        for entry in raw.get("value", []):
            dt = datetime.fromtimestamp(entry["ts"] / 1000, tz=timezone.utc).astimezone()
            data_list.append((dt, float(entry["value"])))

        for entry in raw.get("count", []):
            dt = datetime.fromtimestamp(entry["ts"] / 1000, tz=timezone.utc).astimezone()
            count_list.append((dt, float(entry["value"])))

        return {"value": data_list, "count": count_list}

    def set_led(self, state: bool):
        url = f"{self.host}/api/plugins/rpc/oneway/{self.device_id}"
        payload = {
            "method": "setLed",
            "params": {"state": "on" if state else "off"},
        }
        resp = self.session.post(url, json=payload, timeout=10)
        resp.raise_for_status()
        return resp.status_code == 200

    def get_device_status(self):
        url = f"{self.host}/api/device/{self.device_id}"
        resp = self.session.get(url, timeout=10)
        resp.raise_for_status()
        info = resp.json()
        return {
            "name": info.get("name", ""),
            "active": info.get("active", False),
        }
