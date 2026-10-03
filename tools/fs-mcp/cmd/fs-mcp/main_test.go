package main

import (
	"bytes"
	"encoding/json"
	"testing"
)

func TestDiagnosticArgumentErrorsAreJSONOnly(t *testing.T) {
	for _, args := range [][]string{{"--bad"}, {"--diagnose"}, {"--diagnose", "--viewer-pid", "xyz"}, {"--diagnose", "--viewer-pid", "1", "--discovery", "somewhere", "extra"}} {
		var out bytes.Buffer
		if status := diagnosticMain(args, &out); status != 2 {
			t.Fatalf("status %d", status)
		}
		var result map[string]any
		if err := json.Unmarshal(out.Bytes(), &result); err != nil || result["stage"] != "arguments" {
			t.Fatalf("output %q: %v", out.String(), err)
		}
	}
}

func TestMissingDiscoveryExitsWithFailure(t *testing.T) {
	var out bytes.Buffer
	status := diagnosticMain([]string{"--diagnose", "--viewer-pid", "1", "--discovery", t.TempDir()}, &out)
	if status != 1 {
		t.Fatal(status)
	}
	var result map[string]any
	if err := json.Unmarshal(out.Bytes(), &result); err != nil || result["stage"] != "discovery" {
		t.Fatalf("output %q: %v", out.String(), err)
	}
}
