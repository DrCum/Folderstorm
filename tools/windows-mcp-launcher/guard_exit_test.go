package main

import (
	"bytes"
	"errors"
	"os"
	"os/exec"
	"strings"
	"testing"
)

func TestUpdateGuardRefusalExit(t *testing.T) {
	if os.Getenv("FOLDERSTORM_TEST_GUARD_REFUSAL") == "1" {
		acquireUpdateGuard = func() (func(), error) { return nil, errors.New("update reserved") }
		os.Args = []string{"fs-mcp-launcher-maintenance", "cleanup-owned-legacy", "--install-dir", "must-not-change"}
		main()
		return
	}
	command := exec.Command(os.Args[0], "-test.run=^TestUpdateGuardRefusalExit$")
	command.Env = append(os.Environ(), "FOLDERSTORM_TEST_GUARD_REFUSAL=1")
	var out, diagnostics bytes.Buffer
	command.Stdout, command.Stderr = &out, &diagnostics
	err := command.Run()
	var exit *exec.ExitError
	if !errors.As(err, &exit) || exit.ExitCode() != 1 {
		t.Fatalf("refusal returned %v, output=%q diagnostics=%q", err, out.String(), diagnostics.String())
	}
	if out.Len() != 0 || !strings.Contains(diagnostics.String(), "Folderstorm is being updated") {
		t.Fatalf("maintenance ran or refusal missing: out=%q stderr=%q", out.String(), diagnostics.String())
	}
}
