package main

import (
	"context"
	"log"
	"os"

	"fs-mcp/internal/mcptools"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

func main() {
	log.SetFlags(0)
	log.SetOutput(os.Stderr)

	state := mcptools.New()
	server := mcptools.NewMCPServer(state)
	if err := server.Run(context.Background(), &mcp.StdioTransport{}); err != nil {
		log.Fatal(err)
	}
}
