// Package discover locates live Firestorm MCP Event API bridges.
package discover

import (
	"encoding/json"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"regexp"
	"runtime"
	"strconv"
	"strings"
)

const (
	// EnvDiscovery overrides the default per-OS user_settings search path.
	// It may be a file, a directory, or a list of paths separated by
	// os.PathListSeparator.
	EnvDiscovery = "FIRESTORM_MCP_DISCOVERY"

	// FilenamePattern is the glob written by the viewer: fs-mcp-<pid>.json.
	FilenamePattern = "fs-mcp-*.json"
)

var pidFromName = regexp.MustCompile(`(?i)^fs-mcp-(\d+)\.json$`)

// Instance is one discovered viewer Event API bridge.
type Instance struct {
	PID        int    `json:"pid"`
	Port       int    `json:"port"`
	Token      string `json:"-"`
	APIVersion string `json:"api_version,omitempty"`
	Host       string `json:"host,omitempty"`
	Path       string `json:"path,omitempty"`
	Scheme     string `json:"scheme,omitempty"`
	URL        string `json:"url,omitempty"`
	Viewer     string `json:"viewer,omitempty"`
	Source     string `json:"source"`
	Alive      bool   `json:"alive"`
}

// BaseURL returns the loopback Event API URL for this instance.
func (in Instance) BaseURL() string {
	if in.URL != "" {
		return strings.TrimRight(in.URL, "/")
	}
	scheme := in.Scheme
	if scheme == "" {
		scheme = "http"
	}
	host := in.Host
	if host == "" {
		host = "127.0.0.1"
	}
	path := in.Path
	if path == "" {
		path = "/"
	}
	if !strings.HasPrefix(path, "/") {
		path = "/" + path
	}
	return fmt.Sprintf("%s://%s:%d%s", scheme, host, in.Port, path)
}

// Redacted returns a copy safe to show to an MCP client.
func (in Instance) Redacted() Instance {
	out := in
	out.Token = ""
	return out
}

// Options control discovery.
type Options struct {
	// Override is FIRESTORM_MCP_DISCOVERY or a test path list.
	Override string
	// SkipPIDCheck disables best-effort process liveness checks.
	SkipPIDCheck bool
	// IncludeDead keeps discovery files whose PID appears dead.
	IncludeDead bool
	// Home overrides the user home directory (tests).
	Home string
	// AppData overrides %APPDATA% (tests).
	AppData string
}

// Find locates live viewer discovery files using default OS paths and
// FIRESTORM_MCP_DISCOVERY when set.
func Find() ([]Instance, error) {
	return FindWith(Options{Override: os.Getenv(EnvDiscovery)})
}

// FindWith locates viewer discovery files using opts.
func FindWith(opts Options) ([]Instance, error) {
	roots := searchRoots(opts)
	seen := make(map[string]struct{})
	var out []Instance
	var firstErr error
	for _, root := range roots {
		if root == "" {
			continue
		}
		info, err := os.Stat(root)
		if err != nil {
			if os.IsNotExist(err) {
				continue
			}
			if firstErr == nil {
				firstErr = err
			}
			continue
		}
		files := []string{root}
		if info.IsDir() {
			matches, err := filepath.Glob(filepath.Join(root, FilenamePattern))
			if err != nil {
				if firstErr == nil {
					firstErr = err
				}
				continue
			}
			files = matches
		} else if !looksLikeDiscoveryFile(filepath.Base(root)) {
			// Explicit file override is always considered, even if the name
			// does not match the default glob.
		}
		for _, file := range files {
			abs, err := filepath.Abs(file)
			if err != nil {
				abs = file
			}
			if _, ok := seen[abs]; ok {
				continue
			}
			seen[abs] = struct{}{}
			inst, err := LoadFile(abs)
			if err != nil {
				if firstErr == nil {
					firstErr = fmt.Errorf("%s: %w", abs, err)
				}
				continue
			}
			if !opts.SkipPIDCheck {
				alive, uncertain := pidAlive(inst.PID)
				if uncertain != nil {
					inst.Alive = true
				} else {
					inst.Alive = alive
				}
			} else {
				inst.Alive = true
			}
			if !inst.Alive && !opts.IncludeDead {
				continue
			}
			out = append(out, inst)
		}
	}
	if len(out) == 0 && firstErr != nil {
		return nil, firstErr
	}
	return out, nil
}

// LoadFile parses one discovery JSON file.
func LoadFile(path string) (Instance, error) {
	raw, err := os.ReadFile(path)
	if err != nil {
		return Instance{}, err
	}
	inst, err := Parse(raw)
	if err != nil {
		return Instance{}, err
	}
	inst.Source = path
	if inst.PID == 0 {
		if m := pidFromName.FindStringSubmatch(filepath.Base(path)); len(m) == 2 {
			if pid, convErr := strconv.Atoi(m[1]); convErr == nil {
				inst.PID = pid
			}
		}
	}
	if inst.Port <= 0 || inst.Port > 65535 {
		return Instance{}, fmt.Errorf("invalid port %d", inst.Port)
	}
	if inst.Token == "" {
		return Instance{}, fmt.Errorf("missing bearer token")
	}
	if inst.Host == "" {
		inst.Host = "127.0.0.1"
	}
	if err := requireLoopback(inst.Host); err != nil {
		return Instance{}, err
	}
	if inst.Scheme == "" {
		inst.Scheme = "http"
	}
	if inst.Path == "" {
		inst.Path = "/"
	}
	return inst, nil
}

type wireDiscovery struct {
	PID             json.RawMessage `json:"pid"`
	Port            json.RawMessage `json:"port"`
	HTTPPort        json.RawMessage `json:"http_port"`
	Token           string          `json:"token"`
	BearerToken     string          `json:"bearer_token"`
	AuthToken       string          `json:"auth_token"`
	APIVersion      json.RawMessage `json:"api_version"`
	APIVersionCamel json.RawMessage `json:"apiVersion"`
	Host            string          `json:"host"`
	Path            string          `json:"path"`
	Scheme          string          `json:"scheme"`
	URL             string          `json:"url"`
	Viewer          string          `json:"viewer"`
}

// Parse unmarshals a discovery document.
func Parse(data []byte) (Instance, error) {
	var w wireDiscovery
	if err := json.Unmarshal(data, &w); err != nil {
		return Instance{}, err
	}
	inst := Instance{
		Token:      firstNonEmpty(w.Token, w.BearerToken, w.AuthToken),
		Host:       w.Host,
		Path:       w.Path,
		Scheme:     w.Scheme,
		URL:        w.URL,
		Viewer:     w.Viewer,
		APIVersion: stringifyRaw(firstRaw(w.APIVersion, w.APIVersionCamel)),
	}
	if pid, err := intFromRaw(w.PID); err == nil {
		inst.PID = pid
	}
	portRaw := firstRaw(w.Port, w.HTTPPort)
	if port, err := intFromRaw(portRaw); err == nil {
		inst.Port = port
	}
	return inst, nil
}

func searchRoots(opts Options) []string {
	if strings.TrimSpace(opts.Override) != "" {
		return splitPathList(opts.Override)
	}
	return DefaultSearchDirs(opts)
}

// DefaultSearchDirs returns Folderstorm user_settings directories for this OS,
// then the older Firestorm directories so a side-by-side install is still found.
func DefaultSearchDirs(opts Options) []string {
	home := opts.Home
	if home == "" {
		home, _ = os.UserHomeDir()
	}
	appData := opts.AppData
	if appData == "" {
		appData = os.Getenv("APPDATA")
	}
	var dirs []string
	for _, envName := range []string{
		"FOLDERSTORM_X64_USER_DIR",
		"FOLDERSTORMOS_X64_USER_DIR",
		"FOLDERSTORM_USER_DIR",
		"FOLDERSTORMOS_USER_DIR",
		"FIRESTORM_X64_USER_DIR",
		"FIRESTORMOS_X64_USER_DIR",
		"FIRESTORM_USER_DIR",
		"FIRESTORMOS_USER_DIR",
	} {
		if v := os.Getenv(envName); v != "" {
			dirs = append(dirs, ensureUserSettings(v))
		}
	}
	switch runtime.GOOS {
	case "windows":
		if appData != "" {
			for _, name := range []string{
				"Folderstorm_x64", "FolderstormOS_x64", "Folderstorm", "FolderstormOS",
				"Firestorm_x64", "FirestormOS_x64", "Firestorm", "FirestormOS",
			} {
				dirs = append(dirs, filepath.Join(appData, name, "user_settings"))
			}
		}
	case "darwin":
		support := filepath.Join(home, "Library", "Application Support")
		for _, name := range []string{
			"Folderstorm", "FolderstormOS", "Folderstorm_x64", "FolderstormOS_x64",
			"Firestorm", "FirestormOS", "Firestorm_x64", "FirestormOS_x64",
		} {
			dirs = append(dirs, filepath.Join(support, name, "user_settings"))
		}
	default:
		for _, name := range []string{
			".folderstorm_x64", ".folderstormos_x64", ".folderstorm", ".folderstormos",
			".firestorm_x64", ".firestormos_x64", ".firestorm", ".firestormos",
		} {
			dirs = append(dirs, filepath.Join(home, name, "user_settings"))
		}
	}
	return unique(dirs)
}

func ensureUserSettings(dir string) string {
	trimmed := strings.TrimRight(dir, `\/`)
	if strings.EqualFold(filepath.Base(trimmed), "user_settings") {
		return dir
	}
	return filepath.Join(dir, "user_settings")
}

func splitPathList(list string) []string {
	var out []string
	for _, p := range strings.Split(list, string(os.PathListSeparator)) {
		p = strings.TrimSpace(p)
		if p != "" {
			out = append(out, p)
		}
	}
	return out
}

func looksLikeDiscoveryFile(name string) bool {
	ok, _ := filepath.Match(FilenamePattern, name)
	return ok
}

func requireLoopback(host string) error {
	if host == "localhost" || host == "127.0.0.1" || host == "::1" || host == "[::1]" {
		return nil
	}
	ip := net.ParseIP(strings.Trim(host, "[]"))
	if ip != nil && ip.IsLoopback() {
		return nil
	}
	return fmt.Errorf("discovery host %q is not loopback", host)
}

func firstNonEmpty(vals ...string) string {
	for _, v := range vals {
		if strings.TrimSpace(v) != "" {
			return v
		}
	}
	return ""
}

func firstRaw(vals ...json.RawMessage) json.RawMessage {
	for _, v := range vals {
		if len(v) > 0 && string(v) != "null" {
			return v
		}
	}
	return nil
}

func intFromRaw(raw json.RawMessage) (int, error) {
	if len(raw) == 0 {
		return 0, fmt.Errorf("missing")
	}
	var n int
	if err := json.Unmarshal(raw, &n); err == nil {
		return n, nil
	}
	var s string
	if err := json.Unmarshal(raw, &s); err == nil {
		return strconv.Atoi(s)
	}
	var f float64
	if err := json.Unmarshal(raw, &f); err == nil {
		return int(f), nil
	}
	return 0, fmt.Errorf("not an integer: %s", raw)
}

func stringifyRaw(raw json.RawMessage) string {
	if len(raw) == 0 {
		return ""
	}
	var s string
	if err := json.Unmarshal(raw, &s); err == nil {
		return s
	}
	var n json.Number
	if err := json.Unmarshal(raw, &n); err == nil {
		return n.String()
	}
	return strings.TrimSpace(string(raw))
}

func unique(in []string) []string {
	seen := make(map[string]struct{}, len(in))
	var out []string
	for _, v := range in {
		if v == "" {
			continue
		}
		if _, ok := seen[v]; ok {
			continue
		}
		seen[v] = struct{}{}
		out = append(out, v)
	}
	return out
}
