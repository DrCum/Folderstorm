// Package diagnose performs one bounded, authenticated, read-only viewer check.
// Its result contains fixed categories and readiness facts, never bearer secrets.
package diagnose

import (
	"context"
	"encoding/json"
	"errors"
	"net/http"
	"regexp"
	"time"

	"fs-mcp/internal/discover"
	"fs-mcp/internal/viewerapi"
)

type Result struct {
	OK               bool            `json:"ok"`
	PID              int             `json:"pid"`
	Stage            string          `json:"stage"`
	BridgeReady      bool            `json:"bridge_ready"`
	APIVersion       string          `json:"api_version,omitempty"`
	PolicyGeneration int             `json:"policy_generation,omitempty"`
	APIsReady        map[string]bool `json:"apis_ready,omitempty"`
}

var versionPattern = regexp.MustCompile(`^[0-9]+([.][0-9]+)*$`)

func Run(ctx context.Context, pid int, directory string) Result {
	result := Result{PID: pid, Stage: "discovery"}
	if pid <= 0 || directory == "" {
		return result
	}
	ctx, cancel := context.WithTimeout(ctx, 4*time.Second)
	defer cancel()
	instances, err := discover.FindWith(discover.Options{Override: directory})
	if err != nil {
		return result
	}
	var selected *discover.Instance
	for i := range instances {
		if instances[i].PID == pid {
			selected = &instances[i]
			break
		}
	}
	if selected == nil {
		return result
	}
	client := viewerapi.New(*selected)
	client.HTTPClient = &http.Client{
		Timeout:       4 * time.Second,
		CheckRedirect: func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse },
	}
	body, err := client.CallAPI(ctx, "LocalAssistant", "health", map[string]any{"diagnostic": true})
	if err != nil {
		result.Stage = "bridge"
		var apiErr *viewerapi.Error
		if errors.As(err, &apiErr) && (apiErr.StatusCode == http.StatusUnauthorized || apiErr.StatusCode == http.StatusForbidden) {
			result.Stage = "authorization"
		} else if ctx.Err() != nil || (errors.As(err, &apiErr) && apiErr.Code == "timeout") {
			result.Stage = "timeout"
		}
		return result
	}
	var health struct {
		BridgeReady      bool            `json:"bridge_ready"`
		APIVersion       json.RawMessage `json:"api_version"`
		PolicyGeneration int             `json:"policy_generation"`
		APIsReady        map[string]bool `json:"apis_ready"`
	}
	result.Stage = "bridge"
	if len(body) > 8192 || json.Unmarshal(body, &health) != nil || !health.BridgeReady {
		return result
	}
	// Keep even a faulty/mismatched bridge from echoing arbitrary strings into output.
	var version string
	if json.Unmarshal(health.APIVersion, &version) != nil {
		version = string(health.APIVersion)
	}
	if len(version) <= 16 && versionPattern.MatchString(version) {
		result.APIVersion = version
	}
	result.APIsReady = make(map[string]bool)
	for _, api := range []string{"LLInventory", "LLAppearance", "LLCamera"} {
		if ready, exists := health.APIsReady[api]; exists {
			result.APIsReady[api] = ready
		}
	}
	result.PolicyGeneration = health.PolicyGeneration
	result.BridgeReady = true
	result.OK = true
	result.Stage = "complete"
	return result
}
