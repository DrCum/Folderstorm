//go:build !unix && !windows

package discover

func pidAlive(pid int) (alive bool, uncertain error) {
	if pid <= 0 {
		return false, nil
	}
	// Unknown platform: keep the discovery entry and let the HTTP call fail.
	return true, nil
}
