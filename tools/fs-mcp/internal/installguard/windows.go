//go:build windows

package installguard

import (
	"errors"
	"os"
	"path/filepath"

	"golang.org/x/sys/windows"
)

// The stable root lease is shared for the complete sidecar lifetime. An updater
// cannot reserve the installation while an MCP client still uses these binaries.
func Acquire() (func(), error) {
	noop := func() {}
	executable, err := os.Executable()
	if err != nil {
		return noop, err
	}
	current, err := filepath.EvalSymlinks(filepath.Dir(executable))
	if err != nil {
		return noop, err
	}
	root := filepath.Dir(current)
	if filepath.Base(current) != "current" {
		return noop, nil
	}
	for _, path := range []string{filepath.Join(current, "sq.version"), filepath.Join(current, "folderstorm-bundle.json"), filepath.Join(root, "Update.exe")} {
		if info, err := os.Stat(path); err != nil || !info.Mode().IsRegular() {
			return noop, nil
		}
	}
	name, err := windows.UTF16PtrFromString(filepath.Join(root, ".folderstorm-update.lock"))
	if err != nil {
		return noop, err
	}
	handle, err := windows.CreateFile(name, windows.GENERIC_READ, windows.FILE_SHARE_READ, nil, windows.OPEN_ALWAYS, windows.FILE_ATTRIBUTE_NORMAL, 0)
	if err != nil {
		return noop, err
	}
	release := func() { _ = windows.CloseHandle(handle) }
	if _, err := os.Stat(filepath.Join(root, ".folderstorm-update-pending")); !os.IsNotExist(err) {
		release()
		return noop, errors.New("update pending")
	}
	return release, nil
}
