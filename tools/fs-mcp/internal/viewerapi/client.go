// Package viewerapi is a bearer-authenticated JSON client for the Firestorm
// Event API loopback bridge.
package viewerapi

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/url"
	"os"
	"strings"
	"time"

	"fs-mcp/internal/discover"
)

const (
	// DefaultAPI is the LLEventAPI instance name for inventory.
	DefaultAPI = "LLInventory"
	// EnvTimeout overrides the HTTP timeout (Go duration, default 90s).
	EnvTimeout = "FIRESTORM_MCP_TIMEOUT"
	maxBody    = 8 << 20
)

// Client talks to one viewer bridge.
type Client struct {
	Instance   discover.Instance
	HTTPClient *http.Client
	API        string
}

// New constructs a client for inst.
func New(inst discover.Instance) *Client {
	timeout := 90 * time.Second
	if v := os.Getenv(EnvTimeout); v != "" {
		if d, err := time.ParseDuration(v); err == nil && d > 0 {
			timeout = d
		}
	}
	return &Client{
		Instance: inst,
		HTTPClient: &http.Client{
			Timeout: timeout,
		},
		API: DefaultAPI,
	}
}

// Error is a structured viewer-bridge or Event API failure.
type Error struct {
	Code       string          `json:"code"`
	Message    string          `json:"message"`
	StatusCode int             `json:"status_code,omitempty"`
	Op         string          `json:"op,omitempty"`
	Body       json.RawMessage `json:"body,omitempty"`
}

func (e *Error) Error() string {
	if e == nil {
		return ""
	}
	if e.Op != "" {
		return fmt.Sprintf("%s: %s (%s)", e.Op, e.Message, e.Code)
	}
	return fmt.Sprintf("%s (%s)", e.Message, e.Code)
}

// Call POSTs an Event API request {api, op, ...params} and returns the JSON body.
func (c *Client) Call(ctx context.Context, op string, params map[string]any) (json.RawMessage, error) {
	if c == nil {
		return nil, &Error{Code: "internal", Message: "nil viewer client"}
	}
	if err := requireLoopbackURL(c.Instance.BaseURL()); err != nil {
		return nil, err
	}
	body := map[string]any{}
	for k, v := range params {
		body[k] = v
	}
	api := c.API
	if api == "" {
		api = DefaultAPI
	}
	if _, ok := body["api"]; !ok {
		body["api"] = api
	}
	body["op"] = op

	raw, err := json.Marshal(body)
	if err != nil {
		return nil, &Error{Code: "encode", Message: err.Error(), Op: op}
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, c.Instance.BaseURL(), bytes.NewReader(raw))
	if err != nil {
		return nil, &Error{Code: "request", Message: err.Error(), Op: op}
	}
	req.Header.Set("Authorization", "Bearer "+c.Instance.Token)
	req.Header.Set("Content-Type", "application/json")
	req.Header.Set("Accept", "application/json")
	req.Header.Set("User-Agent", "fs-mcp")

	httpClient := c.HTTPClient
	if httpClient == nil {
		httpClient = http.DefaultClient
	}
	resp, err := httpClient.Do(req)
	if err != nil {
		return nil, &Error{Code: "viewer_unavailable", Message: err.Error(), Op: op}
	}
	defer resp.Body.Close()
	limited := io.LimitReader(resp.Body, maxBody+1)
	respBody, err := io.ReadAll(limited)
	if err != nil {
		return nil, &Error{Code: "read", Message: err.Error(), Op: op, StatusCode: resp.StatusCode}
	}
	if len(respBody) > maxBody {
		return nil, &Error{Code: "read", Message: "response too large", Op: op, StatusCode: resp.StatusCode}
	}

	code := codeForStatus(resp.StatusCode)
	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		msg := strings.TrimSpace(string(respBody))
		if parsed := messageFromBody(respBody); parsed != "" {
			msg = parsed
		}
		if msg == "" {
			msg = resp.Status
		}
		return nil, &Error{
			Code:       code,
			Message:    msg,
			StatusCode: resp.StatusCode,
			Op:         op,
			Body:       json.RawMessage(respBody),
		}
	}
	if msg, eventCode := eventAPIError(respBody); msg != "" {
		if eventCode == "" {
			eventCode = "viewer_error"
		}
		return nil, &Error{
			Code:       eventCode,
			Message:    msg,
			StatusCode: resp.StatusCode,
			Op:         op,
			Body:       json.RawMessage(respBody),
		}
	}
	if len(respBody) == 0 {
		return json.RawMessage(`{}`), nil
	}
	return json.RawMessage(respBody), nil
}

func codeForStatus(status int) string {
	switch status {
	case http.StatusUnauthorized:
		return "unauthorized"
	case http.StatusForbidden:
		return "forbidden"
	case http.StatusNotFound:
		return "not_found"
	case http.StatusRequestEntityTooLarge:
		return "too_large"
	case http.StatusRequestTimeout, http.StatusGatewayTimeout:
		return "timeout"
	default:
		if status >= 500 {
			return "viewer_unavailable"
		}
		return "viewer_error"
	}
}

func eventAPIError(body []byte) (message, code string) {
	var payload map[string]any
	if err := json.Unmarshal(body, &payload); err != nil {
		return "", ""
	}
	if raw, ok := payload["error"]; ok {
		switch v := raw.(type) {
		case string:
			if v != "" {
				return v, stringFrom(payload["error_code"])
			}
		case map[string]any:
			msg := stringFrom(v["message"])
			if msg == "" {
				msg = stringFrom(v["error"])
			}
			c := stringFrom(v["code"])
			if c == "" {
				c = stringFrom(payload["error_code"])
			}
			return msg, c
		}
	}
	return "", ""
}

func messageFromBody(body []byte) string {
	msg, _ := eventAPIError(body)
	if msg != "" {
		return msg
	}
	var payload map[string]any
	if err := json.Unmarshal(body, &payload); err == nil {
		if m := stringFrom(payload["message"]); m != "" {
			return m
		}
	}
	return ""
}

func stringFrom(v any) string {
	switch t := v.(type) {
	case string:
		return t
	case json.Number:
		return t.String()
	case float64:
		if t == float64(int(t)) {
			return fmt.Sprintf("%d", int(t))
		}
		return fmt.Sprintf("%v", t)
	default:
		return ""
	}
}

func requireLoopbackURL(raw string) error {
	u, err := url.Parse(raw)
	if err != nil {
		return &Error{Code: "invalid_url", Message: err.Error()}
	}
	host := u.Hostname()
	if host == "localhost" {
		return nil
	}
	ip := net.ParseIP(host)
	if ip == nil || !ip.IsLoopback() {
		return &Error{Code: "not_loopback", Message: fmt.Sprintf("refusing non-loopback host %q", host)}
	}
	return nil
}
