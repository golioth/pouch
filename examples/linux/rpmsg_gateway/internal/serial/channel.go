package serial

import (
	"errors"
	"fmt"
)

// Result is what a Sender reports about the data it just produced.
type Result int

const (
	// MoreData means the endpoint expects to be called again. Returning it with
	// zero bytes means "nothing ready yet", not "finished".
	MoreData Result = iota
	// NoMoreData means the transfer is complete; the frame carrying it is LAST.
	NoMoreData
	// SendError abandons the transfer.
	SendError
)

// ErrNoEndpoint is reported for a frame on a channel this build knows by id but
// has no endpoint for.
var ErrNoEndpoint = errors.New("serial: channel has no endpoint")

// Sender produces bytes for a channel this side transmits on.
type Sender interface {
	// Start opens a transfer. A non-nil error aborts the channel.
	Start() error
	// Send returns up to max bytes. Returning zero bytes with MoreData means
	// the data is not ready yet and the channel should be polled again.
	Send(max int) ([]byte, Result)
	// End reports how the transfer finished. Optional: implementations that
	// have nothing to tear down may leave it a no-op.
	End(success bool)
}

// Receiver consumes bytes for a channel this side receives on.
type Receiver interface {
	Start() error
	Recv(p []byte) error
	End(success bool)
}

// channel is one endpoint's state machine. Only one side of sender/receiver is
// ever set; which one determines whether the channel prompts or is prompted.
type channel struct {
	id       Channel
	sender   Sender
	receiver Receiver

	open      bool
	firstFrag bool
	pending   bool
	errored   bool

	// closed is invoked after a transfer finishes, successfully or not.
	closed func(ch Channel, success bool)
}

func (c *channel) configured() bool { return c.sender != nil || c.receiver != nil }

// ready marks the channel as having something to say on the next frameGet.
func (c *channel) ready() { c.pending = true }

func (c *channel) start() error {
	if c.sender != nil {
		return c.sender.Start()
	}
	return c.receiver.Start()
}

func (c *channel) end(success bool) {
	if c.sender != nil {
		c.sender.End(success)
		return
	}
	c.receiver.End(success)
}

// close ends an open transfer. Closing an already-closed channel is a no-op, so
// the error paths can call it freely.
func (c *channel) close(success bool) {
	if !c.open {
		return
	}
	c.open = false

	if !success {
		c.errored = true
	}

	c.end(success)

	if c.closed != nil {
		c.closed(c.id, success)
	}

	// A failed transfer still owes the peer an error frame.
	if !success {
		c.ready()
	}
}

func (c *channel) recv(h Header, payload []byte) error {
	if !c.configured() {
		// The peer is using an optional feature this build does not implement.
		// Ignore it rather than dereferencing a missing endpoint.
		return ErrNoEndpoint
	}
	if h.IsData {
		return c.handleData(h, payload)
	}
	return c.handleAck(h.Err)
}

func (c *channel) handleAck(err bool) error {
	if err {
		c.close(false)
		return nil
	}

	// Receiver channels do not open on an ACK - they open when the first DATA
	// frame arrives. Answer with an ACK so the remote sender may proceed.
	if c.sender == nil {
		c.ready()
		return nil
	}

	if !c.open {
		c.open = true
		if err := c.sender.Start(); err != nil {
			c.errored = true
			c.close(false)
			return nil
		}
		c.firstFrag = true
		c.ready()
	}
	return nil
}

func (c *channel) handleData(h Header, payload []byte) error {
	if h.Err {
		c.close(false)
		return nil
	}

	if !c.open {
		if !h.First {
			c.close(false)
			return fmt.Errorf("serial: ch %d: data without FIRST", c.id)
		}
		c.open = true
		if err := c.start(); err != nil {
			c.open = false
			c.errored = true
			c.ready() // nack
			return err
		}
		c.firstFrag = true
	} else if h.First {
		c.close(false)
		return fmt.Errorf("serial: ch %d: unexpected FIRST on an open transfer", c.id)
	}

	if len(payload) > 0 {
		if c.receiver == nil {
			c.close(false)
			return fmt.Errorf("serial: ch %d: data on a send-only channel", c.id)
		}
		if err := c.receiver.Recv(payload); err != nil {
			c.close(false)
			return err
		}
	}

	if h.Last {
		c.close(true)
	}
	return nil
}

// frameGet returns the next frame this channel wants to send, or nil if it has
// nothing pending. maxLen bounds the whole frame, header included.
func (c *channel) frameGet(maxLen int) []byte {
	if maxLen <= HeaderLen || !c.configured() {
		return nil
	}
	if !c.pending {
		return nil
	}
	c.pending = false

	// Receiver channel: all it ever emits is an ACK.
	if c.sender == nil {
		errored := c.errored
		c.errored = false
		return []byte{Header{Channel: c.id, Err: errored}.Encode()}
	}

	if c.errored {
		c.errored = false
		first := c.firstFrag
		c.firstFrag = false
		c.open = false
		return []byte{Header{
			IsData: true, Err: true, First: first, Last: true, Channel: c.id,
		}.Encode()}
	}

	if !c.open {
		// Prompt the peer to open the transfer by ACKing back.
		return []byte{Header{Channel: c.id}.Encode()}
	}

	data, result := c.sender.Send(maxLen - HeaderLen)
	if len(data) == 0 && result == MoreData {
		// Nothing ready yet. Do not send an empty frame; re-arm so the channel
		// is polled again, because nothing else will.
		c.ready()
		return nil
	}

	failed := result == SendError
	last := result == NoMoreData || failed

	first := c.firstFrag
	c.firstFrag = false

	frame := make([]byte, 0, HeaderLen+len(data))
	frame = append(frame, Header{
		IsData: true, Err: failed, First: first, Last: last, Channel: c.id,
	}.Encode())
	frame = append(frame, data...)

	if last {
		c.close(!failed)
	} else {
		c.ready()
	}
	return frame
}
