// Package link carries Pouch Serial frames over an rpmsg character device.
//
// One rpmsg message is exactly one frame, so there is no framing to do here:
// a read returns a whole frame and a write sends one.
package link

import (
	"errors"
	"fmt"
	"sync"
	"sync/atomic"
	"syscall"

	"golang.org/x/sys/unix"
)

// MaxFrame is the largest frame an rpmsg buffer can carry. The kernel's default
// buffer is 512 bytes, of which 16 are the rpmsg header.
const MaxFrame = 496

// ErrClosed is returned by ReadFrame once Close has been called.
var ErrClosed = errors.New("link: closed")

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
//
// Reads still wait in poll(2) rather than in read(2), so that Close can
// interrupt an idle reader. That does not reintroduce the problem above: the
// descriptor stays in blocking mode, writes are untouched, and we only ever ask
// poll about readability. POLLIN comes from the endpoint's receive queue and is
// reliable; POLLOUT is the half of virtio_rpmsg_poll() that cannot be trusted,
// and is never consulted.
type RPMsg struct {
	fd   int
	path string

	// wake is a self-pipe used to break a blocked reader out of poll(2).
	// Closing the device descriptor would not do it: on Linux, close(2) does
	// not reliably wake a thread already waiting on that descriptor.
	wakeR, wakeW int

	closed    atomic.Bool
	closeOnce sync.Once
}

// Open attaches to an rpmsg endpoint, e.g. /dev/rpmsg0.
func Open(path string) (*RPMsg, error) {
	fd, err := syscall.Open(path, syscall.O_RDWR, 0)
	if err != nil {
		return nil, fmt.Errorf("open %s: %w", path, err)
	}

	var p [2]int
	if err := syscall.Pipe2(p[:], syscall.O_CLOEXEC); err != nil {
		_ = syscall.Close(fd)
		return nil, fmt.Errorf("open %s: wake pipe: %w", path, err)
	}

	return &RPMsg{fd: fd, path: path, wakeR: p[0], wakeW: p[1]}, nil
}

// ReadFrame waits for a frame and returns it. It returns ErrClosed if Close is
// called while it is waiting.
func (r *RPMsg) ReadFrame() ([]byte, error) {
	buf := make([]byte, MaxFrame)
	for {
		if r.closed.Load() {
			return nil, ErrClosed
		}

		fds := []unix.PollFd{
			{Fd: int32(r.fd), Events: unix.POLLIN},
			{Fd: int32(r.wakeR), Events: unix.POLLIN},
		}
		if _, err := unix.Poll(fds, -1); err != nil {
			if err == unix.EINTR {
				continue
			}
			return nil, fmt.Errorf("poll %s: %w", r.path, err)
		}

		// Check for shutdown before touching the descriptor: Close may have
		// closed it between poll returning and here.
		if r.closed.Load() || fds[1].Revents != 0 {
			return nil, ErrClosed
		}

		if fds[0].Revents&(unix.POLLERR|unix.POLLHUP|unix.POLLNVAL) != 0 {
			return nil, fmt.Errorf("read %s: endpoint went away", r.path)
		}
		if fds[0].Revents&unix.POLLIN == 0 {
			continue
		}

		n, err := syscall.Read(r.fd, buf)
		if err == syscall.EINTR {
			continue
		}
		if err != nil {
			if r.closed.Load() {
				return nil, ErrClosed
			}
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

// Close releases the endpoint and wakes a reader blocked in ReadFrame. It is
// safe to call concurrently with ReadFrame, and safe to call more than once.
func (r *RPMsg) Close() error {
	var err error
	r.closeOnce.Do(func() {
		r.closed.Store(true)

		// Wake the reader before releasing anything it might be waiting on.
		_, _ = syscall.Write(r.wakeW, []byte{0})

		err = syscall.Close(r.fd)
		_ = syscall.Close(r.wakeW)
		_ = syscall.Close(r.wakeR)
	})
	return err
}
