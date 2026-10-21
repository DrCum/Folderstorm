package controller

import (
	"context"
	"encoding/binary"
	"encoding/json"
	"strings"
	"testing"
)

func TestClosedBindingAndCredentialRedaction(t *testing.T) {
	inst, err := parseDiscovery([]byte(`{"version":1,"controller_instance":"` + strings.Repeat("a", 32) + `","token":"` + strings.Repeat("b", 64) + `"}`))
	if err != nil {
		t.Fatal(err)
	}
	raw, _ := json.Marshal(inst)
	if strings.Contains(string(raw), "token") || strings.Contains(string(raw), strings.Repeat("b", 64)) {
		t.Fatalf("credential serialized: %s", raw)
	}
	if _, err := Resolve([]Instance{inst}, ""); err == nil {
		t.Fatal("implicit instance")
	}
	if _, err := Resolve([]Instance{inst, inst}, inst.ControllerInstance); err == nil {
		t.Fatal("ambiguous controller")
	}
	q := Request{ControllerInstance: inst.ControllerInstance, Op: Activate, ProfileID: strings.Repeat("c", 32), ProfileRevision: 1, Seconds: 10}
	q.RequestID, _ = NewRequestID()
	if Validate(q) == nil {
		t.Fatal("running control without binding")
	}
	q.WorkerID = strings.Repeat("d", 32)
	q.WorkerGeneration = 3
	q.ViewerSession = "opaque-login"
	frame, err := encode(q, inst.token)
	if err != nil {
		t.Fatal(err)
	}
	if len(frame) != 4096 || string(frame[4:8]) != "FSC9" || binary.LittleEndian.Uint32(frame[8:12]) != 1 {
		t.Fatal("wire header")
	}
	size := int(binary.LittleEndian.Uint32(frame))
	for _, b := range frame[4+size:] {
		if b != 0 {
			t.Fatal("uninitialized padding")
		}
	}
	for _, op := range []Op{0, 18, 1000} {
		q.Op = op
		if Validate(q) == nil {
			t.Fatalf("unclassified op %d", op)
		}
	}
	q.Op = Activate
	count := 0
	client := NewWithTransport(inst, func(_ context.Context, path string, wire []byte) ([]byte, error) {
		count++
		if path != `\\.\pipe\FolderstormController-`+inst.ControllerInstance || len(wire) != 4096 {
			t.Fatal("unsafe path/frame")
		}
		return []byte(`{"controller_instance":"` + inst.ControllerInstance + `","state":"pending_approval"}`), nil
	})
	if _, err := client.Call(context.Background(), q); err != nil || count != 1 {
		t.Fatalf("call %v count %d", err, count)
	}
	q.ControllerInstance = strings.Repeat("e", 32)
	if _, err := client.Call(context.Background(), q); err == nil || count != 1 {
		t.Fatal("changed binding sent")
	}
}
func TestNoGlobalProfileFallback(t *testing.T) {
	q := Request{ControllerInstance: strings.Repeat("a", 32), Op: MonitorArrange, ProfileID: strings.Repeat("b", 32), ProfileRevision: 1, Seconds: 10}
	q.RequestID, _ = NewRequestID()
	if Validate(q) == nil {
		t.Fatal("global command borrowed profile")
	}
	q.ProfileID = ""
	q.ProfileRevision = 0
	if err := Validate(q); err != nil {
		t.Fatal(err)
	}
	q.Seconds = 1
	if Validate(q) == nil {
		t.Fatal("invalid cycle bound")
	}
}
