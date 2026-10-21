//go:build !linux && !darwin && !windows

package mcptools

import (
	"errors"
	"os"
)

func openUploadFile(string) (*os.File, error) {
	return nil, errors.New("local-file uploads are supported on Windows, Linux and macOS")
}
