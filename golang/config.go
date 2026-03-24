package main

import (
	"encoding/json"
	"os"
	"path/filepath"
)

const (
	TBHost   = "https://thingsboard.cloud"
	DeviceID = "675987f0-2725-11f1-9cdc-43ca8fc8dcc9"
)

type Credentials struct {
	Email    string `json:"email"`
	Password string `json:"password"`
}

func credPath() string {
	home, _ := os.UserHomeDir()
	return filepath.Join(home, ".tb_credentials")
}

func LoadCredentials() (*Credentials, error) {
	data, err := os.ReadFile(credPath())
	if err != nil {
		return nil, err
	}
	var c Credentials
	if err := json.Unmarshal(data, &c); err != nil {
		return nil, err
	}
	return &c, nil
}

func SaveCredentials(email, password string) error {
	data, _ := json.Marshal(Credentials{Email: email, Password: password})
	return os.WriteFile(credPath(), data, 0600)
}

func DeleteCredentials() {
	os.Remove(credPath())
}
