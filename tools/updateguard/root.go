package updateguard

import (
	"os"
	"path/filepath"
	"strings"
)

func installation(executable string) (string, error) {
	current, err := filepath.EvalSymlinks(filepath.Dir(executable))
	if err != nil {
		return "", err
	}
	if !strings.EqualFold(filepath.Base(current), "current") {
		return "", nil
	}
	root := filepath.Dir(current)
	// Stable coordination files take precedence over a temporarily absent
	// current/sq.version or bundle marker during replacement.
	for _, name := range []string{".folderstorm-update.lock", ".folderstorm-update-pending"} {
		if _, err := os.Lstat(filepath.Join(root, name)); err == nil {
			return root, nil
		} else if !os.IsNotExist(err) {
			return "", err
		}
	}
	for _, path := range []string{filepath.Join(current, "sq.version"), filepath.Join(current, "folderstorm-bundle.json"), filepath.Join(root, "Update.exe")} {
		info, err := os.Stat(path)
		if os.IsNotExist(err) {
			return "", nil
		}
		if err != nil {
			return "", err
		}
		if !info.Mode().IsRegular() {
			return "", nil
		}
	}
	return root, nil
}
