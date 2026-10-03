package main

import (
	"context"
	"encoding/json"
	"flag"
	"fs-mcp/internal/diagnose"
	"io"
	"log"
	"os"

	"fs-mcp/internal/mcptools"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

func main() {
	if len(os.Args) > 1 {
		os.Exit(diagnosticMain(os.Args[1:], os.Stdout))
	}

	log.SetFlags(0)
	log.SetOutput(os.Stderr)

	state := mcptools.New()
	server := mcptools.NewMCPServer(state)
	if err := server.Run(context.Background(), &mcp.StdioTransport{}); err != nil {
		log.Fatal(err)
	}
}

// No arguments still starts the stdio MCP server. Diagnostic output is exclusively
// bounded JSON; flag-parser and HTTP errors are deliberately never printed.
func diagnosticMain(args []string, output io.Writer) int {
	flags := flag.NewFlagSet("fs-mcp", flag.ContinueOnError)
	flags.SetOutput(io.Discard)
	check := flags.Bool("diagnose", false, "check one viewer and exit")
	pid := flags.Int("viewer-pid", 0, "viewer process ID")
	directory := flags.String("discovery", "", "viewer discovery directory")
	if flags.Parse(args) != nil || !*check || *pid <= 0 || *directory == "" || flags.NArg() != 0 {
		_ = json.NewEncoder(output).Encode(diagnose.Result{Stage: "arguments"})
		return 2
	}
	result := diagnose.Run(context.Background(), *pid, *directory)
	if json.NewEncoder(output).Encode(result) != nil {
		return 2
	}
	if !result.OK {
		return 1
	}
	return 0
}
