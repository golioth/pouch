package serial

import "bytes"

// BufReceiver collects a whole transfer in memory and hands it to Done when the
// channel closes. Done runs before the broker advances to the next verb, so it
// is the place to update provisioning state.
type BufReceiver struct {
	buf  bytes.Buffer
	Done func(data []byte, success bool)
}

func (r *BufReceiver) Start() error {
	r.buf.Reset()
	return nil
}

func (r *BufReceiver) Recv(p []byte) error {
	r.buf.Write(p)
	return nil
}

func (r *BufReceiver) End(success bool) {
	if r.Done != nil {
		r.Done(r.buf.Bytes(), success)
	}
}

// Bytes returns what has been collected so far.
func (r *BufReceiver) Bytes() []byte { return r.buf.Bytes() }

// BufSender serves a byte slice, fragmenting it to whatever the link allows.
// Data is called once per transfer, at Start.
type BufSender struct {
	Data func() []byte
	Done func(success bool)

	data []byte
	off  int
}

func (s *BufSender) Start() error {
	if s.Data != nil {
		s.data = s.Data()
	}
	s.off = 0
	return nil
}

func (s *BufSender) Send(max int) ([]byte, Result) {
	n := len(s.data) - s.off
	if n > max {
		n = max
	}
	chunk := s.data[s.off : s.off+n]
	s.off += n

	if s.off >= len(s.data) {
		return chunk, NoMoreData
	}
	return chunk, MoreData
}

func (s *BufSender) End(success bool) {
	if s.Done != nil {
		s.Done(success)
	}
}
