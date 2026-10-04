//go:build !windows

package main

import "fmt"

func cleanupOwnedLegacy(string) (report, error) {
	return report{}, fmt.Errorf("legacy launcher cleanup is available only on Windows")
}
