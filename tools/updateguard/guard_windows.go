//go:build windows

package updateguard

import (
	"errors"
	"os"
	"path/filepath"
	"syscall"
)

// The stable root lease lasts for the complete executable lifetime. It is
// acquired before MCP diagnostics, migration or launcher maintenance can run.
func Acquire() (func(), error) {
	noop := func() {}
	executable, err := os.Executable()
	if err != nil {
		return noop, err
	}
	root, err := installation(executable)
	if err != nil || root == "" {
		return noop, err
	}
	name, err := syscall.UTF16PtrFromString(filepath.Join(root, ".folderstorm-update.lock"))
	if err != nil {
		return noop, err
	}
	handle, err := syscall.CreateFile(name, syscall.GENERIC_READ, syscall.FILE_SHARE_READ, nil, syscall.OPEN_ALWAYS, syscall.FILE_ATTRIBUTE_NORMAL, 0)
	if err != nil {
		return noop, err
	}
	release := func() { _ = syscall.CloseHandle(handle) }
	for _, name := range []string{"sq.version", "folderstorm-bundle.json"} {
		if info, err := os.Stat(filepath.Join(root, "current", name)); err != nil || !info.Mode().IsRegular() {
			release()
			return noop, errors.New("incomplete installation")
		}
	}
	if _, err := os.Lstat(filepath.Join(root, ".folderstorm-update-pending")); !os.IsNotExist(err) {
		release()
		return noop, errors.New("update pending")
	}
	return release, nil
}
