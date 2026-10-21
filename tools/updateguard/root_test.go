package updateguard

import (
	"os"
	"path/filepath"
	"testing"
)

func TestStableBarrierSurvivesMissingBundle(t *testing.T) {
	root := t.TempDir()
	current := filepath.Join(root, "current")
	if err := os.Mkdir(current, 0700); err != nil {
		t.Fatal(err)
	}
	exe := filepath.Join(current, "fs-mcp.exe")
	if actual, err := installation(exe); err != nil || actual != "" {
		t.Fatalf("unknown layout recognized: %q %v", actual, err)
	}
	for _, name := range []string{".folderstorm-update.lock", ".folderstorm-update-pending"} {
		path := filepath.Join(root, name)
		if err := os.WriteFile(path, []byte("transaction"), 0600); err != nil {
			t.Fatal(err)
		}
		if actual, err := installation(exe); err != nil || actual != root {
			t.Fatalf("barrier bypassed while bundle missing: %q %v", actual, err)
		}
		if err := os.Remove(path); err != nil {
			t.Fatal(err)
		}
	}
}
