package main

import "errors"

type category string

const (
	removed      category = "removed_owned_link"
	absent       category = "absent"
	notOwned     category = "not_owned"
	unverified   category = "unverified"
	busy         category = "busy"
	accessDenied category = "access_denied"
)

type result struct {
	Path   string   `json:"path"`
	Status category `json:"status"`
	Reason string   `json:"reason"`
}

type report struct {
	Candidates []result `json:"candidates"`
}

type refusal struct {
	status category
	reason string
}

func (e *refusal) Error() string                  { return e.reason }
func refuse(status category, reason string) error { return &refusal{status, reason} }

// The inspected object and its ownership evidence remain open until Close.
// Delete must use that object's handle, never a second pathname lookup.
type candidate interface {
	Inspect() error
	Delete() error
	Close()
}

func cleanupCandidate(path string, open func() (candidate, error)) result {
	r := result{Path: path}
	c, err := open()
	if err == nil {
		defer c.Close()
		err = c.Inspect()
		if err == nil {
			err = c.Delete()
		}
	}
	if err == nil {
		r.Status, r.Reason = removed, "owned_file_symlink_removed"
		return r
	}
	var skipped *refusal
	if errors.As(err, &skipped) {
		r.Status, r.Reason = skipped.status, skipped.reason
	} else {
		r.Status, r.Reason = unverified, "native_operation_failed"
	}
	return r
}
