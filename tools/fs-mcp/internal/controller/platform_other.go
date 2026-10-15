//go:build !windows

package controller

import "context"

func discoveryRoot() (string, error)                            { return "", ErrUnavailable }
func privateFile(string) error                                  { return ErrUnavailable }
func roundTrip(context.Context, string, []byte) ([]byte, error) { return nil, ErrUnavailable }
