// fs-mcp-launcher-maintenance only removes positively owned legacy launch links.
package main

import (
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
)

func main() { os.Exit(run(os.Args[1:], os.Stdout, os.Stderr)) }

func run(args []string, out, diagnostics io.Writer) int {
	if len(args) == 0 || args[0] != "cleanup-owned-legacy" {
		fmt.Fprintln(diagnostics, "usage: fs-mcp-launcher-maintenance cleanup-owned-legacy --install-dir <actual package directory>")
		return 2
	}
	flags := flag.NewFlagSet("cleanup-owned-legacy", flag.ContinueOnError)
	flags.SetOutput(diagnostics)
	installDir := flags.String("install-dir", "", "trusted uninstalling package directory")
	if err := flags.Parse(args[1:]); err != nil {
		return 2
	}
	if *installDir == "" || flags.NArg() != 0 {
		fmt.Fprintln(diagnostics, "an actual package directory is required")
		return 2
	}
	report, err := cleanupOwnedLegacy(*installDir)
	if err != nil {
		fmt.Fprintln(diagnostics, err)
		return 2
	}
	if err := json.NewEncoder(out).Encode(report); err != nil {
		return 1
	}
	return 0
}
