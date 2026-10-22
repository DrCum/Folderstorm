package mcptools

import (
	"errors"
	"os"
	"syscall"
)

func openUploadFile(path string) (*os.File, error) {
	name, err := syscall.UTF16PtrFromString(path)
	if err != nil {
		return nil, err
	}
	handle, err := syscall.CreateFile(name, syscall.GENERIC_READ, syscall.FILE_SHARE_READ, nil, syscall.OPEN_EXISTING, syscall.FILE_FLAG_OPEN_REPARSE_POINT|0x08000000 /* FILE_FLAG_SEQUENTIAL_SCAN */, 0)
	if err != nil {
		return nil, err
	}
	fail := func() (*os.File, error) {
		_ = syscall.CloseHandle(handle)
		return nil, errors.New("upload input must be a regular disk file without reparse points")
	}
	kind, err := syscall.GetFileType(handle)
	if err != nil || kind != syscall.FILE_TYPE_DISK {
		return fail()
	}
	var info syscall.ByHandleFileInformation
	if syscall.GetFileInformationByHandle(handle, &info) != nil || info.FileAttributes&(syscall.FILE_ATTRIBUTE_DIRECTORY|syscall.FILE_ATTRIBUTE_REPARSE_POINT) != 0 {
		return fail()
	}
	return os.NewFile(uintptr(handle), path), nil
}
