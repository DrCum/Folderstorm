//go:build linux || darwin

package mcptools

import (
	"os"
	"syscall"
)

// A file swapped for a FIFO after Lstat must never block the MCP server.
func openUploadFile(path string) (*os.File, error) {
	fd, err := syscall.Open(path, syscall.O_RDONLY|syscall.O_NONBLOCK|syscall.O_NOFOLLOW|syscall.O_CLOEXEC, 0)
	if err != nil {
		return nil, err
	}
	return os.NewFile(uintptr(fd), path), nil
}
