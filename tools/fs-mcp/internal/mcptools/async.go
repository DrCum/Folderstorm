package mcptools

import (
	"context"
	"github.com/modelcontextprotocol/go-sdk/mcp"
)

type asyncStatusArgs struct {
	ViewerPID     int    `json:"viewer_pid,omitempty"`
	ViewerSession string `json:"viewer_session,omitempty"`
	OperationID   string `json:"operation_id" jsonschema:"operation receipt from createItem or appearance mutation"`
}

func (s *Server) assistantOperationStatus(ctx context.Context, _ *mcp.CallToolRequest, args asyncStatusArgs) (*mcp.CallToolResult, any, error) {
	ctx = withViewerSession(ctx, args.ViewerSession)
	if args.OperationID == "" {
		return errorResult("invalid_args", "operation_id is required", nil)
	}
	_, api, err := s.resolve(ctx, args.ViewerPID)
	if err != nil {
		return err, nil, nil
	}
	return apiResult(api.CallNamed(ctx, "LocalAssistant", "operationStatus", map[string]any{"operation_id": args.OperationID}))
}
