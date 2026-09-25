//go:build !windows

package main

import (
	"os/exec"
	"strings"
)

func askGUI(title, message string) (bool, bool) {
	if path, err := exec.LookPath("zenity"); err == nil {
		cmd := exec.Command(path, "--question", "--title", title, "--text", message, "--default-cancel")
		if err := cmd.Run(); err == nil {
			return true, true
		}
		if _, ok := err.(*exec.ExitError); ok {
			return false, true
		}
	}
	if path, err := exec.LookPath("kdialog"); err == nil {
		cmd := exec.Command(path, "--title", title, "--yesno", message)
		if err := cmd.Run(); err == nil {
			return true, true
		}
		if _, ok := err.(*exec.ExitError); ok {
			return false, true
		}
	}
	if path, err := exec.LookPath("osascript"); err == nil {
		script := "display dialog " + appleQuote(message) +
			" buttons {\"No\", \"Yes\"} default button \"No\" with title " + appleQuote(title)
		out, err := exec.Command(path, "-e", script).CombinedOutput()
		if err != nil {
			if _, ok := err.(*exec.ExitError); ok {
				return false, true
			}
			return false, false
		}
		return strings.Contains(string(out), "Yes"), true
	}
	return false, false
}

func appleQuote(value string) string {
	return `"` + strings.ReplaceAll(strings.ReplaceAll(value, `\`, `\\`), `"`, `\"`) + `"`
}
