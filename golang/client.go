package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"time"
)

type TBClient struct {
	token string
	http  *http.Client
}

type DataPoint struct {
	Time  time.Time
	Value float64
}

type LatestData struct {
	Value string
	Time  string
}

func NewTBClient(email, password string) (*TBClient, error) {
	c := &TBClient{http: &http.Client{Timeout: 10 * time.Second}}
	if err := c.login(email, password); err != nil {
		return nil, err
	}
	return c, nil
}

func (c *TBClient) login(email, password string) error {
	body, _ := json.Marshal(map[string]string{"username": email, "password": password})
	resp, err := c.http.Post(TBHost+"/api/auth/login", "application/json", bytes.NewReader(body))
	if err != nil {
		return fmt.Errorf("네트워크 오류: %v", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode == 401 {
		return fmt.Errorf("이메일 또는 비밀번호가 올바르지 않습니다")
	}
	if resp.StatusCode != 200 {
		return fmt.Errorf("HTTP %d", resp.StatusCode)
	}
	var result map[string]interface{}
	json.NewDecoder(resp.Body).Decode(&result)
	token, ok := result["token"].(string)
	if !ok {
		return fmt.Errorf("토큰을 가져올 수 없습니다")
	}
	c.token = token
	return nil
}

func (c *TBClient) doGet(path string, params url.Values) (map[string]interface{}, error) {
	u := TBHost + path
	if len(params) > 0 {
		u += "?" + params.Encode()
	}
	req, _ := http.NewRequest("GET", u, nil)
	req.Header.Set("X-Authorization", "Bearer "+c.token)
	resp, err := c.http.Do(req)
	if err != nil {
		return nil, fmt.Errorf("네트워크 오류: %v", err)
	}
	defer resp.Body.Close()
	if resp.StatusCode != 200 {
		return nil, fmt.Errorf("HTTP %d", resp.StatusCode)
	}
	var result map[string]interface{}
	json.NewDecoder(resp.Body).Decode(&result)
	return result, nil
}

func (c *TBClient) doPost(path string, payload interface{}) error {
	body, _ := json.Marshal(payload)
	req, _ := http.NewRequest("POST", TBHost+path, bytes.NewReader(body))
	req.Header.Set("X-Authorization", "Bearer "+c.token)
	req.Header.Set("Content-Type", "application/json")
	resp, err := c.http.Do(req)
	if err != nil {
		return fmt.Errorf("네트워크 오류: %v", err)
	}
	defer resp.Body.Close()
	io.Copy(io.Discard, resp.Body)
	if resp.StatusCode != 200 {
		return fmt.Errorf("HTTP %d", resp.StatusCode)
	}
	return nil
}

func (c *TBClient) GetLatest() (map[string]LatestData, error) {
	params := url.Values{"keys": {"value,count"}}
	raw, err := c.doGet("/api/plugins/telemetry/DEVICE/"+DeviceID+"/values/timeseries", params)
	if err != nil {
		return nil, err
	}
	result := make(map[string]LatestData)
	for key, v := range raw {
		arr, ok := v.([]interface{})
		if !ok || len(arr) == 0 {
			continue
		}
		entry := arr[0].(map[string]interface{})
		tsMs := int64(entry["ts"].(float64))
		t := time.UnixMilli(tsMs).Local()
		result[key] = LatestData{
			Value: fmt.Sprintf("%v", entry["value"]),
			Time:  t.Format("2006-01-02 15:04:05"),
		}
	}
	return result, nil
}

func (c *TBClient) GetHistory(hours int) (map[string][]DataPoint, error) {
	now := time.Now()
	start := now.Add(-time.Duration(hours) * time.Hour)
	params := url.Values{
		"keys":    {"value,count"},
		"startTs": {strconv.FormatInt(start.UnixMilli(), 10)},
		"endTs":   {strconv.FormatInt(now.UnixMilli(), 10)},
		"limit":   {"1000"},
		"orderBy": {"ASC"},
	}
	raw, err := c.doGet("/api/plugins/telemetry/DEVICE/"+DeviceID+"/values/timeseries", params)
	if err != nil {
		return nil, err
	}
	result := make(map[string][]DataPoint)
	for key, v := range raw {
		arr, ok := v.([]interface{})
		if !ok {
			continue
		}
		pts := make([]DataPoint, 0, len(arr))
		for _, item := range arr {
			entry := item.(map[string]interface{})
			tsMs := int64(entry["ts"].(float64))
			val, _ := strconv.ParseFloat(fmt.Sprintf("%v", entry["value"]), 64)
			pts = append(pts, DataPoint{
				Time:  time.UnixMilli(tsMs).Local(),
				Value: val,
			})
		}
		result[key] = pts
	}
	return result, nil
}

func (c *TBClient) SetLed(state bool) error {
	stateStr := "off"
	if state {
		stateStr = "on"
	}
	return c.doPost("/api/plugins/rpc/oneway/"+DeviceID, map[string]interface{}{
		"method": "setLed",
		"params": map[string]string{"state": stateStr},
	})
}

func (c *TBClient) GetDeviceStatus() (name string, active bool, err error) {
	req, _ := http.NewRequest("GET", TBHost+"/api/device/"+DeviceID, nil)
	req.Header.Set("X-Authorization", "Bearer "+c.token)
	resp, err := c.http.Do(req)
	if err != nil {
		return "", false, fmt.Errorf("네트워크 오류: %v", err)
	}
	defer resp.Body.Close()
	var result map[string]interface{}
	json.NewDecoder(resp.Body).Decode(&result)
	name, _ = result["name"].(string)
	active, _ = result["active"].(bool)
	return
}
