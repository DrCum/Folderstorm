//go:build !windows

package updateguard

func Acquire() (func(), error) { return func() {}, nil }
