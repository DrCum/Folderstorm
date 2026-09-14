package discover

import (
	"os"
	"path/filepath"
	"runtime"
	"strconv"
	"testing"
)

func writeDiscovery(t *testing.T, dir, name, body string) string {
	t.Helper()
	path := filepath.Join(dir, name)
	if err := os.WriteFile(path, []byte(body), 0o600); err != nil {
		t.Fatal(err)
	}
	return path
}

func TestFindOverrideDirectory(t *testing.T) {
	dir := t.TempDir()
	live := os.Getpid()
	writeDiscovery(t, dir, "fs-mcp-"+strconv.Itoa(live)+".json", `{
		"pid": `+strconv.Itoa(live)+`,
		"port": 17384,
		"token": "secret-token",
		"api_version": 1
	}`)
	writeDiscovery(t, dir, "fs-mcp-99999999.json", `{
		"pid": 99999999,
		"port": 17385,
		"token": "dead-token",
		"api_version": 1
	}`)
	writeDiscovery(t, dir, "not-a-discovery.json", `{"pid":1,"port":1,"token":"x"}`)

	got, err := FindWith(Options{Override: dir})
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 1 {
		t.Fatalf("got %d instances, want 1 live: %+v", len(got), got)
	}
	if got[0].PID != live {
		t.Fatalf("pid = %d, want %d", got[0].PID, live)
	}
	if got[0].Token != "secret-token" {
		t.Fatalf("token = %q", got[0].Token)
	}
	if got[0].Port != 17384 {
		t.Fatalf("port = %d", got[0].Port)
	}
	if got[0].APIVersion != "1" {
		t.Fatalf("api_version = %q", got[0].APIVersion)
	}
}

func TestFindOverrideFileAndAliases(t *testing.T) {
	dir := t.TempDir()
	path := writeDiscovery(t, dir, "custom.json", `{
		"pid": "`+strconv.Itoa(os.Getpid())+`",
		"http_port": "19001",
		"bearer_token": "alias-token",
		"apiVersion": "1",
		"host": "127.0.0.1",
		"path": "/event"
	}`)
	got, err := FindWith(Options{Override: path, SkipPIDCheck: true})
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 1 {
		t.Fatalf("got %d instances", len(got))
	}
	if got[0].Port != 19001 || got[0].Token != "alias-token" {
		t.Fatalf("parsed %+v", got[0])
	}
	if got[0].BaseURL() != "http://127.0.0.1:19001/event" {
		t.Fatalf("url = %s", got[0].BaseURL())
	}
}

func TestFindPathList(t *testing.T) {
	a := t.TempDir()
	b := t.TempDir()
	writeDiscovery(t, a, "fs-mcp-11.json", `{"pid":11,"port":1,"token":"a"}`)
	writeDiscovery(t, b, "fs-mcp-12.json", `{"pid":12,"port":2,"token":"b"}`)
	list := a + string(os.PathListSeparator) + b
	got, err := FindWith(Options{Override: list, SkipPIDCheck: true})
	if err != nil {
		t.Fatal(err)
	}
	if len(got) != 2 {
		t.Fatalf("got %d, want 2", len(got))
	}
}

func TestLoadFileRejectsNonLoopback(t *testing.T) {
	dir := t.TempDir()
	path := writeDiscovery(t, dir, "fs-mcp-1.json", `{"pid":1,"port":80,"token":"x","host":"8.8.8.8"}`)
	if _, err := LoadFile(path); err == nil {
		t.Fatal("expected non-loopback host to fail")
	}
}

func TestLoadFileMissingToken(t *testing.T) {
	dir := t.TempDir()
	path := writeDiscovery(t, dir, "fs-mcp-1.json", `{"pid":1,"port":80}`)
	if _, err := LoadFile(path); err == nil {
		t.Fatal("expected missing token to fail")
	}
}

func TestPIDFromFilename(t *testing.T) {
	dir := t.TempDir()
	path := writeDiscovery(t, dir, "fs-mcp-4242.json", `{"port":9,"token":"t","host":"127.0.0.1"}`)
	inst, err := LoadFile(path)
	if err != nil {
		t.Fatal(err)
	}
	if inst.PID != 4242 {
		t.Fatalf("pid = %d", inst.PID)
	}
}

func TestRedactedHidesToken(t *testing.T) {
	in := Instance{PID: 1, Port: 2, Token: "secret", Host: "127.0.0.1"}
	if in.Redacted().Token != "" {
		t.Fatal("token should be redacted")
	}
	if in.Token != "secret" {
		t.Fatal("original token mutated")
	}
}

func TestDefaultSearchDirs(t *testing.T) {
	dirs := DefaultSearchDirs(Options{Home: "/tmp/home", AppData: `C:\Users\me\AppData\Roaming`})
	if len(dirs) == 0 {
		t.Fatal("expected default dirs")
	}
	joined := filepath.Join(dirs...)
	switch runtime.GOOS {
	case "windows":
		if !containsPath(dirs, filepath.Join(`C:\Users\me\AppData\Roaming`, "Firestorm_x64", "user_settings")) {
			t.Fatalf("missing windows dir in %s", joined)
		}
	case "darwin":
		if !containsPath(dirs, filepath.Join("/tmp/home", "Library", "Application Support", "Firestorm", "user_settings")) {
			t.Fatalf("missing mac dir in %s", joined)
		}
	default:
		if !containsPath(dirs, filepath.Join("/tmp/home", ".firestorm_x64", "user_settings")) {
			t.Fatalf("missing linux dir in %s", joined)
		}
	}
}

func containsPath(dirs []string, want string) bool {
	for _, d := range dirs {
		if d == want {
			return true
		}
	}
	return false
}
