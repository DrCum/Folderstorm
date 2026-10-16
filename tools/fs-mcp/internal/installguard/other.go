//go:build !windows

package installguard

func Acquire() (func(), error) { return func() {}, nil }
