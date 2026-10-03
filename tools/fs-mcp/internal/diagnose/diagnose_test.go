package diagnose

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"testing"
	"time"
)

func discoveryFor(t *testing.T, server *httptest.Server) string {
	t.Helper()
	parsed, _ := url.Parse(server.URL)
	port, _ := strconv.Atoi(parsed.Port())
	dir := t.TempDir()
	data, _ := json.Marshal(map[string]any{"pid": os.Getpid(), "port": port, "token": "do-not-echo-this-token"})
	if err := os.WriteFile(filepath.Join(dir, "fs-mcp-"+strconv.Itoa(os.Getpid())+".json"), data, 0600); err != nil {
		t.Fatal(err)
	}
	return dir
}

func TestHealthIsReadOnlyAndOutputRedacted(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Header.Get("Authorization") != "Bearer do-not-echo-this-token" {
			t.Error("missing auth")
		}
		var req map[string]any
		_ = json.NewDecoder(r.Body).Decode(&req)
		if req["api"] != "LocalAssistant" || req["op"] != "health" || req["diagnostic"] != true {
			t.Errorf("request: %v", req)
		}
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{"bridge_ready":true,"api_version":"1","policy_generation":5,"apis_ready":{"LLInventory":false,"LLCamera":true,"secret":"do-not-echo-this-token"},"token":"do-not-echo-this-token"}`))
	}))
	defer server.Close()
	// Non-boolean unknown fields should not cause a raw error or response leak.
	result := Run(context.Background(), os.Getpid(), discoveryFor(t, server))
	if result.OK {
		t.Fatal("malformed readiness map should fail cleanly")
	}
	raw, _ := json.Marshal(result)
	if strings.Contains(string(raw), "do-not-echo") {
		t.Fatal("secret leaked")
	}
}

func TestHealthWithoutInventoryPermission(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte(`{"bridge_ready":true,"api_version":"1","policy_generation":5,"apis_ready":{"LLInventory":false,"LLCamera":true},"token":"do-not-echo-this-token"}`))
	}))
	defer server.Close()
	result := Run(context.Background(), os.Getpid(), discoveryFor(t, server))
	if !result.OK || !result.BridgeReady || result.APIsReady["LLInventory"] || result.APIVersion != "1" {
		t.Fatalf("result: %+v", result)
	}
	raw, _ := json.Marshal(result)
	if strings.Contains(string(raw), "do-not-echo") {
		t.Fatal("secret leaked")
	}
}

func TestFailuresAreSanitized(t *testing.T) {
	for _, status := range []int{401, 403, 500} {
		t.Run(strconv.Itoa(status), func(t *testing.T) {
			server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
				w.WriteHeader(status)
				_, _ = w.Write([]byte(`{"error":"do-not-echo-this-token"}`))
			}))
			defer server.Close()
			dir := discoveryFor(t, server)
			result := Run(context.Background(), os.Getpid(), dir)
			want := "authorization"
			if status == 500 {
				want = "bridge"
			}
			if result.OK || result.Stage != want {
				t.Fatalf("%+v", result)
			}
			raw, _ := json.Marshal(result)
			if strings.Contains(string(raw), "do-not-echo") {
				t.Fatal("secret leaked")
			}
			if wrong := Run(context.Background(), os.Getpid()+99999, dir); wrong.Stage != "discovery" || wrong.OK {
				t.Fatal(wrong)
			}
		})
	}
}

func TestCanceledCheckAndRedirect(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		select {
		case <-r.Context().Done():
		case <-time.After(time.Second):
		}
	}))
	dir := discoveryFor(t, server)
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	result := Run(ctx, os.Getpid(), dir)
	server.Close()
	if result.OK || result.Stage != "timeout" {
		t.Fatal(result)
	}
	redirect := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Redirect(w, r, "http://example.invalid/", http.StatusTemporaryRedirect)
	}))
	defer redirect.Close()
	if result := Run(context.Background(), os.Getpid(), discoveryFor(t, redirect)); result.OK || result.Stage != "bridge" {
		t.Fatal(result)
	}
}
