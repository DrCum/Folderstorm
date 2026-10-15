package viewerapi

import (
	"context"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"fs-mcp/internal/discover"
)

func TestCallPostsBearerJSON(t *testing.T) {
	var gotAuth, gotBody string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotAuth = r.Header.Get("Authorization")
		if r.Method != http.MethodPost {
			t.Errorf("method %s", r.Method)
		}
		b, _ := io.ReadAll(r.Body)
		gotBody = string(b)
		w.Header().Set("Content-Type", "application/json")
		_, _ = w.Write([]byte(`{"id":"abc","name":"Textures"}`))
	}))
	t.Cleanup(srv.Close)

	c := New(discover.Instance{
		PID:   1,
		Token: "tok",
		URL:   srv.URL + "/",
	})
	raw, err := c.Get(context.Background(), "abc")
	if err != nil {
		t.Fatal(err)
	}
	if gotAuth != "Bearer tok" {
		t.Fatalf("auth = %q", gotAuth)
	}
	if !strings.Contains(gotBody, `"api":"LLInventory"`) || !strings.Contains(gotBody, `"op":"get"`) {
		t.Fatalf("body = %s", gotBody)
	}
	var payload map[string]any
	if err := json.Unmarshal(raw, &payload); err != nil {
		t.Fatal(err)
	}
	if payload["id"] != "abc" {
		t.Fatalf("payload = %v", payload)
	}
}

func TestCallNamedPostsAPI(t *testing.T) {
	var gotBody string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		b, _ := io.ReadAll(r.Body)
		gotBody = string(b)
		_, _ = w.Write([]byte(`{"ok":true}`))
	}))
	t.Cleanup(srv.Close)
	c := New(discover.Instance{Token: "tok", URL: srv.URL + "/"})
	if _, err := c.CallNamed(context.Background(), AppearanceAPI, "worn", nil); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(gotBody, `"api":"LLAppearance"`) || !strings.Contains(gotBody, `"op":"worn"`) {
		t.Fatalf("body = %s", gotBody)
	}
	if _, err := c.CallNamed(context.Background(), CameraAPI, "snapshot", map[string]any{"max_edge": 128}); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(gotBody, `"api":"LLCamera"`) || !strings.Contains(gotBody, `"op":"snapshot"`) {
		t.Fatalf("body = %s", gotBody)
	}
}

func TestCallMapsUnauthorized(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusUnauthorized)
		_, _ = w.Write([]byte(`{"error":"bad token"}`))
	}))
	t.Cleanup(srv.Close)
	c := New(discover.Instance{Token: "nope", URL: srv.URL + "/"})
	_, err := c.Status(context.Background())
	ve, ok := err.(*Error)
	if !ok {
		t.Fatalf("err type %T: %v", err, err)
	}
	if ve.Code != "unauthorized" {
		t.Fatalf("code = %s", ve.Code)
	}
	if ve.Message != "bad token" {
		t.Fatalf("message = %s", ve.Message)
	}
}

func TestCallMapsEventAPIError(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		_, _ = w.Write([]byte(`{"error":"Folder was not found"}`))
	}))
	t.Cleanup(srv.Close)
	c := New(discover.Instance{Token: "tok", URL: srv.URL + "/"})
	_, err := c.List(context.Background(), map[string]any{"folder_id": "missing"})
	ve, ok := err.(*Error)
	if !ok {
		t.Fatalf("err type %T: %v", err, err)
	}
	if ve.Code != "viewer_error" {
		t.Fatalf("code = %s", ve.Code)
	}
	if ve.Message != "Folder was not found" {
		t.Fatalf("message = %s", ve.Message)
	}
}

func TestRejectsNonLoopback(t *testing.T) {
	c := New(discover.Instance{Token: "tok", URL: "http://example.com/"})
	_, err := c.Status(context.Background())
	ve, ok := err.(*Error)
	if !ok || ve.Code != "not_loopback" {
		t.Fatalf("err = %v", err)
	}
}

func TestCopyAndConfirmOps(t *testing.T) {
	var ops []string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		var body map[string]any
		_ = json.NewDecoder(r.Body).Decode(&body)
		ops = append(ops, body["op"].(string))
		switch body["op"] {
		case "copy":
			_, _ = w.Write([]byte(`{"status":"confirmation_required","plan_id":"p1","proposed_moves":[{"source_id":"s"}]}`))
		case "confirmCopy":
			_, _ = w.Write([]byte(`{"status":"ok","plan_id":"p1"}`))
		default:
			w.WriteHeader(500)
		}
	}))
	t.Cleanup(srv.Close)
	c := New(discover.Instance{Token: "tok", URL: srv.URL + "/"})
	raw, err := c.Copy(context.Background(), map[string]any{"id": "src", "parent_id": "dst"})
	if err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(string(raw), "p1") {
		t.Fatalf("copy = %s", raw)
	}
	if _, err := c.ConfirmCopy(context.Background(), map[string]any{"plan_id": "p1", "confirm": true}); err != nil {
		t.Fatal(err)
	}
	if strings.Join(ops, ",") != "copy,confirmCopy" {
		t.Fatalf("ops = %v", ops)
	}
}

type sessionTransport func(*http.Request) (*http.Response, error)

func (f sessionTransport) RoundTrip(r *http.Request) (*http.Response, error) { return f(r) }
func TestSessionReferenceCannotBeOverridden(t *testing.T) {
	client := New(discover.Instance{PID: 1, Port: 9, Token: "secret", Managed: true, ViewerSession: "bound-login"})
	calls := 0
	client.HTTPClient.Transport = sessionTransport(func(r *http.Request) (*http.Response, error) {
		calls++
		var body map[string]any
		if err := json.NewDecoder(r.Body).Decode(&body); err != nil {
			t.Fatal(err)
		}
		if body["viewer_session"] != "bound-login" {
			t.Fatal("request rebound", body)
		}
		return &http.Response{StatusCode: 200, Body: io.NopCloser(strings.NewReader(`{}`)), Header: make(http.Header)}, nil
	})
	if _, err := client.Call(context.Background(), "get", map[string]any{"viewer_session": "other"}); err != nil {
		t.Fatal(err)
	}
	client.Instance.ViewerSession = ""
	if _, err := client.Call(context.Background(), "rename", nil); err == nil || calls != 1 {
		t.Fatal("unbound managed call reached HTTP")
	}
}
