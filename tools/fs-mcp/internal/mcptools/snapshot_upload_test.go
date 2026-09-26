package mcptools

import (
	"strings"
	"testing"
)

func numberIs(t *testing.T, params map[string]any, key string, want int) {
	t.Helper()
	switch value := params[key].(type) {
	case int:
		if value != want {
			t.Fatalf("%s = %v", key, value)
		}
	case float64:
		if value != float64(want) {
			t.Fatalf("%s = %v", key, value)
		}
	default:
		t.Fatalf("%s = %#v", key, params[key])
	}
}

func TestCameraSnapshotSquareViewport(t *testing.T) {
	api := &fakeAPI{}
	cs := connect(t, singleViewer(api), nil)
	res := call(t, cs, "camera_snapshot", map[string]any{
		"width":         640,
		"height":        640,
		"viewport_only": true,
	})
	if !res.IsError {
		t.Fatal("expected a missing snapshot file, params should still be sent")
	}
	if !contains(api.opsCopy(), "LLCamera.snapshot") {
		t.Fatalf("ops = %v", api.opsCopy())
	}
	api.mu.Lock()
	params := api.lastNamed
	api.mu.Unlock()
	numberIs(t, params, "width", 640)
	numberIs(t, params, "height", 640)
	viewportOnly, _ := params["viewport_only"].(bool)
	if !viewportOnly {
		t.Fatalf("viewport_only = %#v", params["viewport_only"])
	}
	if _, ok := params["max_edge"]; ok {
		t.Fatalf("max_edge sent with an explicit size: %#v", params["max_edge"])
	}
}

func TestCameraSnapshotOmitsNewFields(t *testing.T) {
	api := &fakeAPI{}
	cs := connect(t, singleViewer(api), nil)
	_ = call(t, cs, "camera_snapshot", map[string]any{"max_edge": 256})
	api.mu.Lock()
	params := api.lastNamed
	api.mu.Unlock()
	for _, key := range []string{"width", "height", "viewport_only"} {
		if _, ok := params[key]; ok {
			t.Fatalf("%s was sent when omitted: %#v", key, params[key])
		}
	}
	numberIs(t, params, "max_edge", 256)
}

func TestCameraSnapshotRejectsHalfSize(t *testing.T) {
	api := &fakeAPI{}
	cs := connect(t, singleViewer(api), nil)
	res := call(t, cs, "camera_snapshot", map[string]any{"width": 512})
	if !res.IsError || !strings.Contains(textOf(res), "width and height") {
		t.Fatalf("half size: %s", textOf(res))
	}
	if contains(api.opsCopy(), "LLCamera.snapshot") {
		t.Fatal("snapshot ran with only width")
	}
}

func TestSnapshotUploadSquareViewportDestination(t *testing.T) {
	api := &fakeAPI{status: policyStatus(1, map[string]string{"edit": "allow", "create": "allow"})}
	cs := connect(t, singleViewer(api), nil)
	res := call(t, cs, "inventory_snapshot_upload", map[string]any{
		"id":            "outfit-1",
		"width":         1024,
		"height":        1024,
		"viewport_only": false,
		"destination":   "texture",
		"name":          "Portrait",
	})
	if res.IsError {
		t.Fatalf("upload: %s", textOf(res))
	}
	if !contains(api.opsCopy(), "LLInventory.snapshotUpload") {
		t.Fatalf("ops = %v", api.opsCopy())
	}
	api.mu.Lock()
	params := api.lastNamed
	api.mu.Unlock()
	numberIs(t, params, "width", 1024)
	numberIs(t, params, "height", 1024)
	if params["destination"] != "texture" {
		t.Fatalf("destination = %#v", params["destination"])
	}
	if params["viewport_only"] != false {
		t.Fatalf("viewport_only = %#v", params["viewport_only"])
	}
	if params["name"] != "Portrait" || params["id"] != "outfit-1" {
		t.Fatalf("params = %#v", params)
	}
}

func TestSnapshotUploadDefaults(t *testing.T) {
	api := &fakeAPI{status: policyStatus(1, map[string]string{"edit": "allow", "create": "deny"})}
	cs := connect(t, singleViewer(api), nil)
	res := call(t, cs, "inventory_snapshot_upload", map[string]any{"id": "folder-1"})
	if res.IsError {
		t.Fatalf("default upload: %s", textOf(res))
	}
	api.mu.Lock()
	params := api.lastNamed
	api.mu.Unlock()
	numberIs(t, params, "width", 1024)
	numberIs(t, params, "height", 1024)
	if params["destination"] != "thumbnail" {
		t.Fatalf("destination = %#v", params["destination"])
	}
	if params["viewport_only"] != true {
		t.Fatalf("viewport_only = %#v", params["viewport_only"])
	}
}

func TestSnapshotUploadPermissionClass(t *testing.T) {
	thumbnailDenied := &fakeAPI{status: policyStatus(1, map[string]string{"edit": "deny", "create": "allow"})}
	cs := connect(t, singleViewer(thumbnailDenied), nil)
	res := call(t, cs, "inventory_snapshot_upload", map[string]any{"id": "folder-1", "destination": "thumbnail"})
	if !res.IsError || !strings.Contains(textOf(res), "not_permitted") {
		t.Fatalf("thumbnail deny: %s", textOf(res))
	}
	if contains(thumbnailDenied.opsCopy(), "LLInventory.snapshotUpload") {
		t.Fatal("thumbnail upload ran when Edit is Never")
	}

	textureDenied := &fakeAPI{status: policyStatus(1, map[string]string{"edit": "allow", "create": "deny"})}
	cs = connect(t, singleViewer(textureDenied), nil)
	res = call(t, cs, "inventory_snapshot_upload", map[string]any{"id": "folder-1", "destination": "Texture"})
	if !res.IsError || !strings.Contains(textOf(res), "not_permitted") {
		t.Fatalf("texture deny: %s", textOf(res))
	}
	if contains(textureDenied.opsCopy(), "LLInventory.snapshotUpload") {
		t.Fatal("texture upload ran when Create is Never")
	}

	setThumb := &fakeAPI{status: policyStatus(1, map[string]string{"edit": "deny"})}
	cs = connect(t, singleViewer(setThumb), nil)
	res = call(t, cs, "inventory_set_thumbnail", map[string]any{"id": "folder-1", "thumbnail_id": "asset-1"})
	if !res.IsError || !strings.Contains(textOf(res), "not_permitted") {
		t.Fatalf("set thumbnail: %s", textOf(res))
	}
	if contains(setThumb.opsCopy(), "LLInventory.setThumbnail") {
		t.Fatal("inventory_set_thumbnail changed class")
	}
}

func TestSnapshotUploadRejectsBadArgs(t *testing.T) {
	api := &fakeAPI{}
	cs := connect(t, singleViewer(api), nil)
	res := call(t, cs, "inventory_snapshot_upload", map[string]any{"id": "folder-1", "width": 256})
	if !res.IsError || !strings.Contains(textOf(res), "width and height") {
		t.Fatalf("half size: %s", textOf(res))
	}
	res = call(t, cs, "inventory_snapshot_upload", map[string]any{"id": "folder-1", "destination": "photo"})
	if !res.IsError || !strings.Contains(textOf(res), "destination") {
		t.Fatalf("destination: %s", textOf(res))
	}
	if contains(api.opsCopy(), "LLInventory.snapshotUpload") {
		t.Fatalf("upload ran: %v", api.opsCopy())
	}
}
