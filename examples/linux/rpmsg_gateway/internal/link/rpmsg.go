// Package link carries Pouch Serial frames over an rpmsg character device.
//
// One rpmsg message is exactly one frame, so there is no framing to do here:
// a read returns a whole frame and a write sends one.
package link

import (
	"fmt"
	"syscall"
)

// MaxFrame is the largest frame an rpmsg buffer can carry. The kernel's default
// buffer is 512 bytes, of which 16 are the rpmsg header.
const MaxFrame = 496

// RPMsg is a frame-oriented link over /dev/rpmsgN.
//
// It deliberately uses raw file descriptors rather than *os.File. Go's runtime
// registers pollable descriptors with the netpoller and switches them to
// non-blocking, which is fatal here: virtio_rpmsg_poll() reports the endpoint
// writable only when a TX buffer is already free, and never enables the
// tx-complete callback - only the blocking write(2) path calls
// rpmsg_upref_sleepers(), which is what arms it. A non-blocking writer that
// drains the ring, which is routine when streaming, then waits for a wakeup
// that structurally cannot arrive. Keeping the descriptor blocking and out of
// the poller is the fix.
type RPMsg struct {
	fd   int
	path string
}

// Open attaches to an rpmsg endpoint, e.g. /dev/rpmsg0.
func Open(path string) (*RPMsg, error) {
	fd, err := syscall.Open(path, syscall.O_RDWR, 0)
	if err != nil {
		return nil, fmt.Errorf("open %s: %w", path, err)
	}
	return &RPMsg{fd: fd, path: path}, nil
}

// ReadFrame blocks until a frame arrives and returns it.
func (r *RPMsg) ReadFrame() ([]byte, error) {
	buf := make([]byte, MaxFrame)
	for {
		n, err := syscall.Read(r.fd, buf)
		if err == syscall.EINTR {
			continue
		}
		if err != nil {
			return nil, fmt.Errorf("read %s: %w", r.path, err)
		}
		if n == 0 {
			continue
		}
		return buf[:n], nil
	}
}

// WriteFrame sends one frame. The write blocks until the peer frees a buffer.
func (r *RPMsg) WriteFrame(frame []byte) error {
	if len(frame) == 0 {
		return fmt.Errorf("write %s: empty frame", r.path)
	}
	if len(frame) > MaxFrame {
		return fmt.Errorf("write %s: frame of %d bytes exceeds %d", r.path, len(frame), MaxFrame)
	}
	for {
		n, err := syscall.Write(r.fd, frame)
		if err == syscall.EINTR {
			continue
		}
		if err != nil {
			return fmt.Errorf("write %s: %w", r.path, err)
		}
		if n != len(frame) {
			// rpmsg is message-oriented; a short write would mean a truncated
			// frame on the wire, which the peer cannot recover from.
			return fmt.Errorf("write %s: short write, %d of %d bytes", r.path, n, len(frame))
		}
		return nil
	}
}

// Close releases the endpoint.
func (r *RPMsg) Close() error { return syscall.Close(r.fd) }
