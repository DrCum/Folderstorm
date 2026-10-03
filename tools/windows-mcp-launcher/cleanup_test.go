package main

import (
	"bytes"
	"errors"
	"reflect"
	"testing"
)

type fakeCandidate struct {
	calls                     *[]string
	inspectError, deleteError error
}

func (f *fakeCandidate) Inspect() error {
	*f.calls = append(*f.calls, "inspect")
	return f.inspectError
}
func (f *fakeCandidate) Delete() error { *f.calls = append(*f.calls, "delete"); return f.deleteError }
func (f *fakeCandidate) Close()        { *f.calls = append(*f.calls, "close") }

func TestCleanupRefusesUnprovenObjectsAndAlwaysCloses(t *testing.T) {
	for _, status := range []category{absent, notOwned, unverified, busy, accessDenied} {
		t.Run(string(status), func(t *testing.T) {
			var calls []string
			c := &fakeCandidate{calls: &calls, inspectError: refuse(status, "test_refusal")}
			r := cleanupCandidate("fixed candidate", func() (candidate, error) { calls = append(calls, "open"); return c, nil })
			if r.Status != status || r.Reason != "test_refusal" {
				t.Fatalf("unexpected result: %+v", r)
			}
			if !reflect.DeepEqual(calls, []string{"open", "inspect", "close"}) {
				t.Fatalf("unsafe lifecycle: %v", calls)
			}
		})
	}
}

func TestCleanupOwnedObjectDeletesBeforeReleasingEvidence(t *testing.T) {
	var calls []string
	c := &fakeCandidate{calls: &calls}
	r := cleanupCandidate("fixed candidate", func() (candidate, error) { return c, nil })
	if r.Status != removed || !reflect.DeepEqual(calls, []string{"inspect", "delete", "close"}) {
		t.Fatalf("result=%+v lifecycle=%v", r, calls)
	}
}

func TestCleanupOpenAndDispositionFailures(t *testing.T) {
	r := cleanupCandidate("fixed candidate", func() (candidate, error) { return nil, refuse(busy, "held_elsewhere") })
	if r.Status != busy {
		t.Fatalf("unexpected result: %+v", r)
	}
	var calls []string
	c := &fakeCandidate{calls: &calls, deleteError: refuse(accessDenied, "disposition_denied")}
	r = cleanupCandidate("fixed candidate", func() (candidate, error) { return c, nil })
	if r.Status != accessDenied || !reflect.DeepEqual(calls, []string{"inspect", "delete", "close"}) {
		t.Fatalf("result=%+v lifecycle=%v", r, calls)
	}
	r = cleanupCandidate("fixed candidate", func() (candidate, error) { return nil, errors.New("unexpected native error") })
	if r.Status != unverified || r.Reason != "native_operation_failed" {
		t.Fatalf("unexpected result: %+v", r)
	}
}

func TestCLIRejectsUnknownOperationsAndDestinations(t *testing.T) {
	for _, args := range [][]string{nil, {"create"}, {"cleanup-owned-legacy"}, {"cleanup-owned-legacy", "--install-dir", "somewhere", "--delete-path", "elsewhere"}} {
		var out, diagnostics bytes.Buffer
		if status := run(args, &out, &diagnostics); status != 2 || out.Len() != 0 {
			t.Fatalf("args=%v status=%d stdout=%q", args, status, out.String())
		}
	}
}
