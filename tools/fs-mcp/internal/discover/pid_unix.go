//go:build unix

package discover

import "syscall"

// pidAlive reports whether pid looks like a live process. Existence is
// best-effort: ESRCH means dead; EPERM means the process exists but cannot
// be signaled, which we treat as alive.
func pidAlive(pid int) (alive bool, uncertain error) {
	if pid <= 0 {
		return false, nil
	}
	err := syscall.Kill(pid, 0)
	if err == nil {
		return true, nil
	}
	if err == syscall.ESRCH {
		return false, nil
	}
	if err == syscall.EPERM {
		return true, nil
	}
	return true, err
}
