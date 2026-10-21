// Package controller is the separately authenticated Windows host authority.
// It never uses viewer discovery, worker grants, arbitrary URLs or selected PID.
package controller

import (
	"bytes"
	"context"
	"crypto/rand"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"time"
)

const RequestBytes = 4096
const ResponseBytes = 65536

var hex32 = regexp.MustCompile(`^[0-9a-f]{32}$`)
var hex64 = regexp.MustCompile(`^[0-9a-f]{64}$`)
var requestID = regexp.MustCompile(`^[0-9]{10,16}-[0-9a-f]{32}$`)
var ErrUnavailable = errors.New("controller unavailable; inspect current controller instances before retrying")

type Op uint32

const (
	List Op = 1 + iota
	Status
	Receipt
	Activate
	Background
	OpenLogin
	Restart
	Logout
	Detach
	MonitorList
	MonitorOpen
	MonitorClose
	MonitorOpenAll
	MonitorArrange
	CycleConfigure
	CyclePause
	CycleNext
)

func (op Op) valid() bool    { return op >= List && op <= CycleNext }
func (op Op) mutating() bool { return op != List && op != Status && op != Receipt && op != MonitorList }

type Instance struct {
	ControllerInstance string `json:"controller_instance"`
	token              string
}
type Request struct {
	ControllerInstance string
	RequestID          string
	Op                 Op
	ProfileID          string
	ProfileRevision    uint64
	WorkerID           string
	WorkerGeneration   uint64
	ViewerSession      string
	Economy            bool
	MonitorID          uint32
	Seconds            uint32
	Row                bool
	Playing            bool
}

// NewRequestID embeds its admission time so an expired deduplication record
// cannot turn an old retry into another mutation.
func NewRequestID() (string, error) {
	var nonce [16]byte
	if _, err := rand.Read(nonce[:]); err != nil {
		return "", err
	}
	return strconv.FormatInt(time.Now().UnixMilli(), 10) + "-" + hex.EncodeToString(nonce[:]), nil
}
func ValidRequestID(id string) bool { return requestID.MatchString(id) }
func Validate(q Request) error {
	if !hex32.MatchString(q.ControllerInstance) || !q.Op.valid() || q.MonitorID >= 5 || q.Seconds < 2 || q.Seconds > 3600 || len(q.ViewerSession) > 128 {
		return errors.New("invalid controller request")
	}
	if q.Op.mutating() || q.Op == Receipt {
		if !ValidRequestID(q.RequestID) {
			return errors.New("request_id must be the timestamped ID returned by controller_list")
		}
	}
	global := q.Op == List || q.Op == Receipt || q.Op == MonitorList || q.Op == MonitorOpenAll || q.Op == MonitorArrange || q.Op == CycleConfigure || q.Op == CyclePause || q.Op == CycleNext
	if global {
		if q.ProfileID != "" || q.ProfileRevision != 0 || q.WorkerID != "" || q.WorkerGeneration != 0 || q.ViewerSession != "" {
			return errors.New("global controller operation cannot carry an implicit profile")
		}
	} else if !hex32.MatchString(q.ProfileID) || q.ProfileRevision == 0 {
		return errors.New("exact profile_id and profile_revision required")
	}
	if q.WorkerID != "" && !hex32.MatchString(q.WorkerID) {
		return errors.New("invalid worker_id")
	}
	if q.WorkerID != "" || q.WorkerGeneration != 0 || q.ViewerSession != "" {
		if q.WorkerID == "" || q.WorkerGeneration == 0 || q.ViewerSession == "" {
			return errors.New("complete exact worker and viewer_session binding required")
		}
	}
	if q.Op.mutating() && !global && q.Op != OpenLogin && q.WorkerID == "" {
		return errors.New("running control requires exact worker_id, generation and viewer_session")
	}
	return nil
}
func encode(q Request, token string) ([]byte, error) {
	if err := Validate(q); err != nil {
		return nil, err
	}
	if !hex64.MatchString(token) {
		return nil, errors.New("invalid private controller credential")
	}
	var b bytes.Buffer
	b.WriteString("FSC9")
	num := func(n uint32) { _ = binary.Write(&b, binary.LittleEndian, n) }
	wide := func(n uint64) { _ = binary.Write(&b, binary.LittleEndian, n) }
	str := func(s string) { num(uint32(len(s))); b.WriteString(s) }
	num(1)
	str(q.ControllerInstance)
	str(token)
	str(q.RequestID)
	num(uint32(q.Op))
	str(q.ProfileID)
	wide(q.ProfileRevision)
	str(q.WorkerID)
	wide(q.WorkerGeneration)
	str(q.ViewerSession)
	flag := func(on bool) {
		if on {
			num(1)
		} else {
			num(0)
		}
	}
	flag(q.Economy)
	num(q.MonitorID)
	num(q.Seconds)
	flag(q.Row)
	flag(q.Playing)
	if b.Len() > RequestBytes-4 {
		return nil, errors.New("controller request too large")
	}
	frame := make([]byte, RequestBytes)
	binary.LittleEndian.PutUint32(frame, uint32(b.Len()))
	copy(frame[4:], b.Bytes())
	return frame, nil
}

type Transport func(context.Context, string, []byte) ([]byte, error)
type Client struct {
	instance  Instance
	transport Transport
}

func New(instance Instance) *Client { return &Client{instance: instance, transport: roundTrip} }
func NewWithTransport(instance Instance, transport Transport) *Client {
	return &Client{instance: instance, transport: transport}
}
func (c *Client) Call(ctx context.Context, q Request) (json.RawMessage, error) {
	if q.ControllerInstance != c.instance.ControllerInstance {
		return nil, errors.New("controller_changed")
	}
	frame, err := encode(q, c.instance.token)
	if err != nil {
		return nil, err
	}
	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()
	raw, err := c.transport(ctx, `\\.\pipe\FolderstormController-`+q.ControllerInstance, frame)
	if err != nil {
		return nil, errors.New("controller outcome uncertain or unavailable; query the original request_id, never replay with a fresh ID")
	}
	if len(raw) > ResponseBytes-4 || !json.Valid(raw) {
		return nil, errors.New("invalid controller response; inspect original request_id")
	}
	var response struct {
		Instance string `json:"controller_instance"`
		Error    string `json:"error_code"`
	}
	if json.Unmarshal(raw, &response) != nil {
		return nil, errors.New("invalid controller response")
	}
	if response.Instance != q.ControllerInstance && (response.Instance != "" || response.Error == "") {
		return nil, errors.New("controller_changed")
	}
	return json.RawMessage(raw), nil
}

// Find returns only opaque controller IDs. Credentials and private paths are
// retained solely inside this package and never serialized to an MCP client.
func Find() ([]Instance, error) {
	root, err := discoveryRoot()
	if err != nil {
		return nil, err
	}
	return findDirectory(root)
}
func findDirectory(root string) ([]Instance, error) {
	files, err := filepath.Glob(filepath.Join(root, "controller-*.json"))
	if err != nil || len(files) > 64 {
		return nil, ErrUnavailable
	}
	var out []Instance
	for _, path := range files {
		info, err := os.Lstat(path)
		if err != nil || !info.Mode().IsRegular() || info.Mode()&os.ModeSymlink != 0 || info.Size() > 2048 || privateFile(path) != nil {
			continue
		}
		f, err := os.Open(path)
		if err != nil {
			continue
		}
		raw, err := io.ReadAll(io.LimitReader(f, 2049))
		_ = f.Close()
		if err != nil || len(raw) > 2048 {
			continue
		}
		inst, err := parseDiscovery(raw)
		if err != nil || filepath.Base(path) != "controller-"+inst.ControllerInstance+".json" {
			continue
		}
		out = append(out, inst)
	}
	return out, nil
}
func parseDiscovery(raw []byte) (Instance, error) {
	var wire struct {
		Version  int    `json:"version"`
		Instance string `json:"controller_instance"`
		Token    string `json:"token"`
	}
	if len(raw) > 2048 || json.Unmarshal(raw, &wire) != nil || wire.Version != 1 || !hex32.MatchString(wire.Instance) || !hex64.MatchString(wire.Token) {
		return Instance{}, ErrUnavailable
	}
	return Instance{wire.Instance, wire.Token}, nil
}
func Resolve(instances []Instance, exact string) (Instance, error) {
	if !hex32.MatchString(exact) {
		return Instance{}, errors.New("exact controller_instance required")
	}
	var found *Instance
	for i := range instances {
		if instances[i].ControllerInstance == exact {
			if found != nil {
				return Instance{}, errors.New("duplicate controller_instance")
			}
			found = &instances[i]
		}
	}
	if found == nil {
		return Instance{}, ErrUnavailable
	}
	return *found, nil
}

// Decimal fields are strings on the public wire to preserve 64-bit revisions.
func ParseRevision(text string) (uint64, error) {
	if text == "" || strings.TrimSpace(text) != text {
		return 0, fmt.Errorf("exact decimal revision required")
	}
	n, err := strconv.ParseUint(text, 10, 64)
	if err != nil || n == 0 {
		return 0, errors.New("invalid revision")
	}
	return n, nil
}
