//go:build windows

package controller

import (
	"context"
	"encoding/binary"
	"errors"
	"golang.org/x/sys/windows"
	"path/filepath"
	"unsafe"
)

func discoveryRoot() (string, error) {
	root, err := windows.KnownFolderPath(windows.FOLDERID_LocalAppData, 0)
	if err != nil {
		return "", ErrUnavailable
	}
	return filepath.Join(root, "Folderstorm", "controllers"), nil
}
func privateFile(path string) error {
	p, err := windows.UTF16PtrFromString(path)
	if err != nil {
		return ErrUnavailable
	}
	attributes, err := windows.GetFileAttributes(p)
	if err != nil || attributes&windows.FILE_ATTRIBUTE_REPARSE_POINT != 0 {
		return ErrUnavailable
	}
	sd, err := windows.GetNamedSecurityInfo(path, windows.SE_FILE_OBJECT, windows.OWNER_SECURITY_INFORMATION|windows.DACL_SECURITY_INFORMATION)
	if err != nil {
		return ErrUnavailable
	}
	user, err := windows.GetCurrentProcessToken().GetTokenUser()
	if err != nil {
		return ErrUnavailable
	}
	owner, _, err := sd.Owner()
	if err != nil || !owner.Equals(user.User.Sid) {
		return ErrUnavailable
	}
	acl, _, err := sd.DACL()
	if err != nil || acl == nil || acl.AceCount == 0 {
		return ErrUnavailable
	}
	for i := uint32(0); i < uint32(acl.AceCount); i++ {
		var ace *windows.ACCESS_ALLOWED_ACE
		if windows.GetAce(acl, i, &ace) != nil || ace.Header.AceType != windows.ACCESS_ALLOWED_ACE_TYPE || !(*windows.SID)(unsafe.Pointer(&ace.SidStart)).Equals(user.User.Sid) {
			return ErrUnavailable
		}
	}
	return nil
}
func roundTrip(ctx context.Context, name string, frame []byte) ([]byte, error) {
	path, err := windows.UTF16PtrFromString(name)
	if err != nil {
		return nil, ErrUnavailable
	}
	h, err := windows.CreateFile(path, windows.GENERIC_READ|windows.GENERIC_WRITE, 0, nil, windows.OPEN_EXISTING, windows.FILE_FLAG_OVERLAPPED, 0)
	if err != nil {
		return nil, ErrUnavailable
	}
	defer windows.CloseHandle(h)
	// Do not send a bearer credential to a server owned by another user.
	var pid uint32
	if windows.GetNamedPipeServerProcessId(h, &pid) != nil {
		return nil, ErrUnavailable
	}
	process, err := windows.OpenProcess(windows.PROCESS_QUERY_LIMITED_INFORMATION, false, pid)
	if err != nil {
		return nil, ErrUnavailable
	}
	defer windows.CloseHandle(process)
	var token windows.Token
	if windows.OpenProcessToken(process, windows.TOKEN_QUERY, &token) != nil {
		return nil, ErrUnavailable
	}
	defer token.Close()
	serverUser, err := token.GetTokenUser()
	if err != nil {
		return nil, ErrUnavailable
	}
	localUser, err := windows.GetCurrentProcessToken().GetTokenUser()
	if err != nil || !serverUser.User.Sid.Equals(localUser.User.Sid) {
		return nil, ErrUnavailable
	}
	transfer := func(buffer []byte, write bool) error {
		for len(buffer) > 0 {
			if err := ctx.Err(); err != nil {
				return err
			}
			event, err := windows.CreateEvent(nil, 1, 0, nil)
			if err != nil {
				return err
			}
			over := windows.Overlapped{HEvent: event}
			var n uint32
			if write {
				err = windows.WriteFile(h, buffer, &n, &over)
			} else {
				err = windows.ReadFile(h, buffer, &n, &over)
			}
			if err == windows.ERROR_IO_PENDING {
				for {
					wait, waitErr := windows.WaitForSingleObject(event, 50)
					if waitErr != nil {
						_ = windows.CancelIoEx(h, &over)
						_ = windows.GetOverlappedResult(h, &over, &n, true)
						err = waitErr
						break
					}
					if wait == windows.WAIT_OBJECT_0 {
						err = windows.GetOverlappedResult(h, &over, &n, false)
						break
					}
					if ctx.Err() != nil {
						_ = windows.CancelIoEx(h, &over)
						_ = windows.GetOverlappedResult(h, &over, &n, true)
						err = ctx.Err()
						break
					}
				}
			}
			windows.CloseHandle(event)
			if err != nil {
				return err
			}
			if n == 0 || int(n) > len(buffer) {
				return errors.New("short controller transfer")
			}
			buffer = buffer[n:]
		}
		return nil
	}
	if err := transfer(frame, true); err != nil {
		return nil, err
	}
	header := make([]byte, 4)
	if err := transfer(header, false); err != nil {
		return nil, err
	}
	n := binary.LittleEndian.Uint32(header)
	if n == 0 || n > ResponseBytes-4 {
		return nil, ErrUnavailable
	}
	body := make([]byte, n)
	if err := transfer(body, false); err != nil {
		return nil, err
	} // transport never retries submitted bytes
	return body, nil
}
