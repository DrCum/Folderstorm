//go:build windows

package main

import (
	"syscall"
	"unsafe"
)

func askGUI(title, message string) (bool, bool) {
	user32 := syscall.NewLazyDLL("user32.dll")
	proc := user32.NewProc("MessageBoxW")
	titlePtr, err := syscall.UTF16PtrFromString(title)
	if err != nil {
		return false, false
	}
	messagePtr, err := syscall.UTF16PtrFromString(message)
	if err != nil {
		return false, false
	}
	// MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2. IDYES is 6.
	const flags = uintptr(0x00000004 | 0x00000020 | 0x00000100)
	ret, _, callErr := proc.Call(0, uintptr(unsafe.Pointer(messagePtr)), uintptr(unsafe.Pointer(titlePtr)), flags)
	if callErr != syscall.Errno(0) && ret == 0 {
		return false, false
	}
	return ret == 6, true
}
