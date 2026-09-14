package viewerapi

import (
	"context"
	"encoding/json"
)

// Status calls Event API op "status".
func (c *Client) Status(ctx context.Context) (json.RawMessage, error) {
	return c.Call(ctx, "status", nil)
}

// Get calls Event API op "get".
func (c *Client) Get(ctx context.Context, id string) (json.RawMessage, error) {
	return c.Call(ctx, "get", map[string]any{"id": id})
}

// List calls Event API op "list".
func (c *Client) List(ctx context.Context, params map[string]any) (json.RawMessage, error) {
	return c.Call(ctx, "list", params)
}

// Search calls Event API op "search".
func (c *Client) Search(ctx context.Context, params map[string]any) (json.RawMessage, error) {
	return c.Call(ctx, "search", params)
}

// SystemFolder calls Event API op "systemFolder".
func (c *Client) SystemFolder(ctx context.Context, ftName string) (json.RawMessage, error) {
	return c.Call(ctx, "systemFolder", map[string]any{
		"ft_name": ftName,
		"type":    ftName,
	})
}

// CreateFolder calls Event API op "createFolder".
func (c *Client) CreateFolder(ctx context.Context, params map[string]any) (json.RawMessage, error) {
	return c.Call(ctx, "createFolder", params)
}

// Move calls Event API op "move".
func (c *Client) Move(ctx context.Context, params map[string]any) (json.RawMessage, error) {
	return c.Call(ctx, "move", params)
}

// Rename calls Event API op "rename".
func (c *Client) Rename(ctx context.Context, params map[string]any) (json.RawMessage, error) {
	return c.Call(ctx, "rename", params)
}

// Copy calls Event API op "copy".
func (c *Client) Copy(ctx context.Context, params map[string]any) (json.RawMessage, error) {
	return c.Call(ctx, "copy", params)
}

// ConfirmCopy calls Event API op "confirmCopy".
func (c *Client) ConfirmCopy(ctx context.Context, params map[string]any) (json.RawMessage, error) {
	return c.Call(ctx, "confirmCopy", params)
}
