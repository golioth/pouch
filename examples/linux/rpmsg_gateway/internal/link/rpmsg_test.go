package link

import (
	"errors"
	"path/filepath"
	"syscall"
	"testing"
	"time"
)

// A FIFO stands in for the rpmsg character device: both are message-ish
// descriptors that block a reader when empty, which is the property under test.
func fifo(t *testing.T) string {
	t.Helper()
	path := filepath.Join(t.TempDir(), "rpmsg")
	if err := syscall.Mkfifo(path, 0o600); err != nil {
		t.Fatalf("mkfifo: %v", err)
	}
	return path
}

// Close must break a reader out of its wait. Before this was fixed the reader
// sat in read(2) with nothing to interrupt it, so SIGTERM did nothing, the
// process only died to SIGKILL, and the stale instance kept /dev/rpmsg0 open -
// the next start then failed with "device or resource busy".
func TestCloseInterruptsBlockedRead(t *testing.T) {
	l, err := Open(fifo(t))
	if err != nil {
		t.Fatalf("open: %v", err)
	}

	result := make(chan error, 1)
	go func() {
		_, err := l.ReadFrame()
		result <- err
	}()

	// Let the reader reach its wait before pulling the rug.
	time.Sleep(100 * time.Millisecond)
	if err := l.Close(); err != nil {
		t.Fatalf("close: %v", err)
	}

	select {
	case err := <-result:
		if !errors.Is(err, ErrClosed) {
			t.Fatalf("ReadFrame returned %v, want ErrClosed", err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("ReadFrame still blocked two seconds after Close")
	}
}

func TestReadAfterCloseReturnsErrClosed(t *testing.T) {
	l, err := Open(fifo(t))
	if err != nil {
		t.Fatalf("open: %v", err)
	}
	if err := l.Close(); err != nil {
		t.Fatalf("close: %v", err)
	}

	if _, err := l.ReadFrame(); !errors.Is(err, ErrClosed) {
		t.Fatalf("ReadFrame returned %v, want ErrClosed", err)
	}
}

// Close runs on the shutdown path and again on the firmware-restart path, so it
// has to tolerate being called twice.
func TestCloseIsIdempotent(t *testing.T) {
	l, err := Open(fifo(t))
	if err != nil {
		t.Fatalf("open: %v", err)
	}
	if err := l.Close(); err != nil {
		t.Fatalf("first close: %v", err)
	}
	if err := l.Close(); err != nil {
		t.Fatalf("second close: %v", err)
	}
}

// A frame written while the reader is waiting still arrives intact.
func TestReadFrameDeliversData(t *testing.T) {
	path := fifo(t)
	l, err := Open(path)
	if err != nil {
		t.Fatalf("open: %v", err)
	}
	defer func() { _ = l.Close() }()

	want := []byte{0xb0, 0x01, 0x02, 0x03}
	go func() {
		time.Sleep(50 * time.Millisecond)
		_ = l.WriteFrame(want)
	}()

	got, err := l.ReadFrame()
	if err != nil {
		t.Fatalf("ReadFrame: %v", err)
	}
	if string(got) != string(want) {
		t.Fatalf("ReadFrame = % x, want % x", got, want)
	}
}
