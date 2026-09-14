//go:build windows

package discover

import "syscall"

const (
	processQueryLimitedInformation = 0x1000
	stillActive                    = 259
)

func pidAlive(pid int) (alive bool, uncertain error) {
	if pid <= 0 {
		return false, nil
	}
	h, err := syscall.OpenProcess(processQueryLimitedInformation, false, uint32(pid))
	if err != nil {
		return false, nil
	}
	defer syscall.CloseHandle(h)
	var code uint32
	if err := syscall.GetExitCodeProcess(h, &code); err != nil {
		return true, err
	}
	return code == stillActive, nil
}
