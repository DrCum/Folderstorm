package mcptools

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"unicode"
	"unicode/utf8"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

const uploadFileLimit int64 = 64 << 20
const uploadBatchLimit int64 = 128 << 20
const uploadCountLimit = 32

func registerUploads(server *mcp.Server, state *Server, readOnly, mutating *mcp.ToolAnnotations) {
	mcp.AddTool(server, &mcp.Tool{Name: "inventory_upload_preview", Description: "Prepare local PNG/JPEG/TGA/BMP, WAV, ANIM or BVH uploads for an exact character. Supply absolute files OR one absolute directory and an inventory folder_id. Directory hierarchy is preserved. Rejects unsupported files, links, special files and oversized batches. Native viewer verifies SHA-256, encodes immutable bytes and quotes this account's fees, including Premium free uploads. Preview does not upload or charge. Returns a short-lived plan_id. Requires inventory Read; local paths refer to the machine running the MCP sidecar and viewer, not a remote agent filesystem.", Annotations: withTitle(readOnly, "Preview file uploads")}, state.uploadPreview)
	mcp.AddTool(server, &mcp.Tool{Name: "inventory_upload_execute", Description: "Execute an exact preview plan once. Requires Upload and Create permissions. A positive total always asks in the viewer; zero cost needs no price confirmation (Ask permissions still apply). Stops on a failure or increased quote. Returns per-file status; never retry uncertain uploads with a new plan. Does not wear, play, rez or execute assets.", Annotations: withTitle(mutating, "Upload planned files")}, state.uploadExecute)
	mcp.AddTool(server, &mcp.Tool{Name: "inventory_upload_status", Description: "Read source-bound per-file progress and confirmed IDs for a prior upload plan. Includes partial failures and uncertain submissions; never resends. Requires inventory Read.", Annotations: withTitle(readOnly, "File upload status")}, state.uploadStatus)
	mcp.AddTool(server, &mcp.Tool{Name: "inventory_upload_cancel", Description: "Stop unsent files in an exact upload plan. Already accepted uploads or charges cannot be undone. Requires inventory Read; no new spending authorization.", Annotations: withTitle(mutating, "Cancel pending uploads")}, state.uploadCancel)
	mcp.AddTool(server, &mcp.Tool{Name: "inventory_upload_mesh_review", Description: "Open one absolute local Collada DAE in this character's native mesh importer for explicit LOD, physics, rigging and price review. This only opens native review; it does NOT upload or return an asset ID. The resident completes the native importer. Refuses an occupied importer. Requires active character, Upload and Create. Referenced local model resources may be loaded by the native importer.", Annotations: withTitle(mutating, "Review mesh upload")}, state.uploadMesh)
}

type uploadPreviewArgs struct {
	Files               []string `json:"files,omitempty" jsonschema:"absolute local paths; choose files or directory"`
	Directory           string   `json:"directory,omitempty" jsonschema:"absolute local directory, at most 32 supported files and depth 8"`
	FolderID            string   `json:"folder_id" jsonschema:"exact destination inventory folder UUID"`
	MaxTextureDimension int      `json:"max_texture_dimension,omitempty" jsonschema:"1024 (default) or 2048"`
	ViewerSession       string   `json:"viewer_session"`
	ViewerPID           int      `json:"viewer_pid,omitempty"`
}
type uploadPlanArgs struct {
	PlanID        string `json:"plan_id"`
	ViewerSession string `json:"viewer_session"`
	ViewerPID     int    `json:"viewer_pid,omitempty"`
}
type uploadMeshArgs struct {
	Path          string `json:"path"`
	FolderID      string `json:"folder_id"`
	ViewerSession string `json:"viewer_session"`
	ViewerPID     int    `json:"viewer_pid,omitempty"`
}
type uploadFile struct {
	Path   string `json:"path"`
	Name   string `json:"name"`
	Folder string `json:"relative_folder"`
	Bytes  int64  `json:"bytes"`
	SHA256 string `json:"sha256"`
}

func uploadExtension(path string) bool {
	switch strings.ToLower(filepath.Ext(path)) {
	case ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".wav", ".anim", ".bvh":
		return true
	}
	return false
}
func uploadLocalPath(path string) (string, error) {
	if !filepath.IsAbs(path) || strings.ContainsRune(path, 0) || !utf8.ValidString(path) || len(path) > 4096 {
		return "", errors.New("an absolute UTF-8 local path is required")
	}
	clean := filepath.Clean(path)
	if strings.HasPrefix(clean, `\\`) || strings.HasPrefix(clean, "//") {
		return "", errors.New("network and device paths are not upload inputs")
	}
	// Walk from the filesystem root so a linked parent is rejected before any
	// operation traverses it. Lstat of the leaf first would already follow parents.
	var parents []string
	for p := clean; ; p = filepath.Dir(p) {
		parents = append(parents, p)
		if filepath.Dir(p) == p {
			break
		}
	}
	for i := len(parents) - 1; i >= 0; i-- {
		info, err := os.Lstat(parents[i])
		if err != nil {
			return "", err
		}
		if info.Mode()&os.ModeSymlink != 0 {
			return "", errors.New("symbolic links are not upload inputs")
		}
	}
	return clean, nil
}
func uploadLocalFile(path string) (uploadFile, error) {
	var result uploadFile
	clean, err := uploadLocalPath(path)
	if err != nil {
		return result, err
	}

	before, err := os.Lstat(clean)
	if err != nil {
		return result, err
	}
	if !before.Mode().IsRegular() || before.Size() <= 0 || before.Size() > uploadFileLimit {
		return result, errors.New("upload input must be a nonempty regular file at most 64 MiB")
	}
	f, err := openUploadFile(clean)
	if err != nil {
		return result, err
	}
	defer f.Close()
	opened, err := f.Stat()
	if err != nil || !opened.Mode().IsRegular() || !os.SameFile(before, opened) {
		return result, errors.New("upload input changed while opening")
	}
	h := sha256.New()
	n, err := io.Copy(h, io.LimitReader(f, uploadFileLimit+1))
	if err != nil {
		return result, err
	}
	after, err := f.Stat()
	if err != nil || n != before.Size() || after.Size() != before.Size() || !after.ModTime().Equal(before.ModTime()) {
		return result, errors.New("upload input changed while hashing")
	}
	name := strings.TrimSuffix(filepath.Base(clean), filepath.Ext(clean))
	if name == "" || len(name) > 63 || strings.IndexFunc(name, unicode.IsControl) >= 0 {
		return result, errors.New("asset names must be 1..63 UTF-8 bytes without control characters")
	}
	return uploadFile{Path: clean, Name: name, Bytes: n, SHA256: hex.EncodeToString(h.Sum(nil))}, nil
}
func uploadInputs(args uploadPreviewArgs) ([]uploadFile, error) {
	if (len(args.Files) > 0) == (args.Directory != "") || len(args.Files) > uploadCountLimit {
		return nil, errors.New("supply 1..32 files OR one directory")
	}
	paths := append([]string(nil), args.Files...)
	root := ""
	if args.Directory != "" {
		rootPath, pathErr := uploadLocalPath(args.Directory)
		if pathErr != nil {
			return nil, pathErr
		}
		root = rootPath
		info, err := os.Lstat(root)
		if err != nil || !info.IsDir() || info.Mode()&os.ModeSymlink != 0 {
			return nil, errors.New("directory must be a real local directory")
		}
		if !filepath.IsAbs(root) {
			return nil, errors.New("directory must be absolute")
		}
		err = filepath.WalkDir(root, func(path string, d os.DirEntry, err error) error {
			if err != nil {
				return err
			}
			if d.Type()&os.ModeSymlink != 0 {
				return fmt.Errorf("linked entry refused: %s", path)
			}
			rel, err := filepath.Rel(root, path)
			if err != nil {
				return err
			}
			depth := filepath.ToSlash(rel)
			if !d.IsDir() {
				depth = filepath.ToSlash(filepath.Dir(rel))
			}
			if depth != "." && len(strings.Split(depth, "/")) > 8 {
				return errors.New("directory exceeds depth 8")
			}
			if d.IsDir() {
				return nil
			}
			if !d.Type().IsRegular() {
				return fmt.Errorf("special file refused: %s", path)
			}
			paths = append(paths, path)
			if len(paths) > uploadCountLimit {
				return errors.New("batch exceeds 32 files")
			}
			return nil
		})
		if err != nil {
			return nil, err
		}
	}
	if len(paths) == 0 {
		return nil, errors.New("no upload files found")
	}
	sort.Strings(paths)
	files := make([]uploadFile, 0, len(paths))
	seen := map[string]bool{}
	var total int64
	for _, path := range paths {
		if !uploadExtension(path) {
			return nil, fmt.Errorf("unsupported automatic upload: %s (DAE uses mesh review)", filepath.Base(path))
		}
		file, err := uploadLocalFile(path)
		if err != nil {
			return nil, fmt.Errorf("%s: %w", filepath.Base(path), err)
		}
		key := file.Path
		if seen[key] {
			return nil, errors.New("duplicate upload file")
		}
		seen[key] = true
		if root != "" {
			rel, err := filepath.Rel(root, filepath.Dir(file.Path))
			if err != nil {
				return nil, err
			}
			if rel != "." {
				file.Folder = filepath.ToSlash(rel)
			}
		}
		total += file.Bytes
		if total > uploadBatchLimit {
			return nil, errors.New("batch exceeds 128 MiB")
		}
		files = append(files, file)
	}
	return files, nil
}
func (s *Server) uploadCall(ctx context.Context, session string, pid int, op string, params map[string]any, classes []string) (*mcp.CallToolResult, any, error) {
	if strings.TrimSpace(session) == "" || pid < 0 {
		return errorResult("invalid_args", "Exact viewer_session is required", nil)
	}
	return s.callWorldFeature(ctx, session, pid, "FSUpload", "file_uploads", op, params, classes)
}
func (s *Server) uploadPreview(ctx context.Context, _ *mcp.CallToolRequest, args uploadPreviewArgs) (*mcp.CallToolResult, any, error) {
	if !socialUUID(args.FolderID) || strings.TrimSpace(args.ViewerSession) == "" || args.ViewerPID < 0 {
		return errorResult("invalid_args", "Exact viewer_session and destination folder_id are required", nil)
	}
	if args.MaxTextureDimension == 0 {
		args.MaxTextureDimension = 1024
	}
	if args.MaxTextureDimension != 1024 && args.MaxTextureDimension != 2048 {
		return errorResult("invalid_args", "max_texture_dimension must be 1024 or 2048", nil)
	}
	files, err := uploadInputs(args)
	if err != nil {
		return errorResult("invalid_args", err.Error(), nil)
	}
	return s.uploadCall(ctx, args.ViewerSession, args.ViewerPID, "preview", map[string]any{"files": files, "folder_id": args.FolderID, "max_texture_dimension": args.MaxTextureDimension}, []string{"read"})
}
func (s *Server) uploadPlan(ctx context.Context, args uploadPlanArgs, op string) (*mcp.CallToolResult, any, error) {
	if !socialUUID(args.PlanID) {
		return errorResult("invalid_args", "Exact plan_id required", nil)
	}
	classes := []string{"read"}
	if op == "execute" {
		classes = append(classes, "upload", "create")
	}
	return s.uploadCall(ctx, args.ViewerSession, args.ViewerPID, op, map[string]any{"plan_id": args.PlanID}, classes)
}
func (s *Server) uploadExecute(ctx context.Context, _ *mcp.CallToolRequest, args uploadPlanArgs) (*mcp.CallToolResult, any, error) {
	return s.uploadPlan(ctx, args, "execute")
}
func (s *Server) uploadStatus(ctx context.Context, _ *mcp.CallToolRequest, args uploadPlanArgs) (*mcp.CallToolResult, any, error) {
	return s.uploadPlan(ctx, args, "status")
}
func (s *Server) uploadCancel(ctx context.Context, _ *mcp.CallToolRequest, args uploadPlanArgs) (*mcp.CallToolResult, any, error) {
	return s.uploadPlan(ctx, args, "cancel")
}
func (s *Server) uploadMesh(ctx context.Context, _ *mcp.CallToolRequest, args uploadMeshArgs) (*mcp.CallToolResult, any, error) {
	if !socialUUID(args.FolderID) || strings.ToLower(filepath.Ext(args.Path)) != ".dae" || strings.TrimSpace(args.ViewerSession) == "" || args.ViewerPID < 0 {
		return errorResult("invalid_args", "Exact viewer_session, destination folder_id and absolute .dae path required", nil)
	}
	file, err := uploadLocalFile(args.Path)
	if err != nil {
		return errorResult("invalid_args", err.Error(), nil)
	}
	return s.uploadCall(ctx, args.ViewerSession, args.ViewerPID, "meshReview", map[string]any{"file": file, "folder_id": args.FolderID}, []string{"read", "upload", "create"})
}
