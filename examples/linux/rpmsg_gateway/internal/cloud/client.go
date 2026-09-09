// Package cloud speaks the Pouch gateway HTTP contract.
//
//	GET  <Address>/.g/server-cert  -> the server certificate chain to push down
//	POST <Address>/.g/device-cert  <- the device's leaf certificate
//	POST <Address>/.g/pouch        <- an uplink pouch; the body back is the downlink
//
// Pouches are opaque here. The session is encrypted end to end between the
// device and the cloud, so the gateway forwards ciphertext and never holds a key.
package cloud

import (
	"bytes"
	"context"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"sync"
	"time"
)

const (
	PathServerCert = "/.g/server-cert"
	PathDeviceCert = "/.g/device-cert"
	PathPouch      = "/.g/pouch"

	maxResponseBytes = 8 << 20
)

// Client talks to one Pouch-compatible server.
type Client struct {
	Address string
	HTTP    *http.Client

	// The server's clock, as observed from its Date header. It is tracked
	// because the server is the authority on time for anything it validates:
	// a device signing an artifact URL stamps it with a not-before, and this
	// server rejects one in the future outright. Handing the device our own
	// clock therefore fails whenever we are fast, so we hand it this instead.
	clockMu     sync.Mutex
	clockOffset time.Duration
	clockValid  bool
}

// ClockOffset returns how far the server's clock runs ahead of ours, and
// whether one has been observed yet. Add it to a local timestamp to express it
// on the server's terms.
func (c *Client) ClockOffset() (time.Duration, bool) {
	c.clockMu.Lock()
	defer c.clockMu.Unlock()
	return c.clockOffset, c.clockValid
}

// observeClock records the server's clock from one response.
//
// sent and received bracket the request, so the instant the server stamped its
// Date lies somewhere between them. Taking the midpoint removes the round trip
// from the estimate; what remains is the header's one-second resolution and
// half the asymmetry of the path.
func (c *Client) observeClock(resp *http.Response, sent, received time.Time) {
	date := resp.Header.Get("Date")
	if date == "" {
		return
	}
	serverTime, err := http.ParseTime(date)
	if err != nil {
		return
	}

	midpoint := sent.Add(received.Sub(sent) / 2)

	c.clockMu.Lock()
	defer c.clockMu.Unlock()
	c.clockOffset = serverTime.Sub(midpoint)
	c.clockValid = true
}

// New returns a client for address, e.g. "https://gw.golioth.io".
func New(address string, timeout time.Duration) *Client {
	return &Client{
		Address: address,
		HTTP:    &http.Client{Timeout: timeout},
	}
}

func (c *Client) do(ctx context.Context, method, path string, body []byte) ([]byte, error) {
	u, err := url.JoinPath(c.Address, path)
	if err != nil {
		return nil, fmt.Errorf("%s %s: build url: %w", method, path, err)
	}

	var r io.Reader
	if body != nil {
		r = bytes.NewReader(body)
	}
	req, err := http.NewRequestWithContext(ctx, method, u, r)
	if err != nil {
		return nil, fmt.Errorf("%s %s: %w", method, path, err)
	}
	if body != nil {
		req.Header.Set("Content-Type", "application/octet-stream")
	}

	client := c.HTTP
	if client == nil {
		client = http.DefaultClient
	}
	sent := time.Now()
	resp, err := client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("%s %s: %w", method, path, err)
	}
	defer func() { _ = resp.Body.Close() }()

	// Before the status check: an error response carries the header too, and a
	// gateway that cannot reach the cloud yet still wants to learn its clock.
	c.observeClock(resp, sent, time.Now())

	if resp.StatusCode < 200 || resp.StatusCode >= 300 {
		b, _ := io.ReadAll(io.LimitReader(resp.Body, 4096))
		return nil, fmt.Errorf("%s %s: %s: %s", method, path, resp.Status, bytes.TrimSpace(b))
	}
	return io.ReadAll(io.LimitReader(resp.Body, maxResponseBytes))
}

// ServerCert fetches the certificate chain to provision onto the device.
func (c *Client) ServerCert(ctx context.Context) ([]byte, error) {
	return c.do(ctx, http.MethodGet, PathServerCert, nil)
}

// RegisterDevice hands the device's leaf certificate to the cloud.
func (c *Client) RegisterDevice(ctx context.Context, cert []byte) error {
	_, err := c.do(ctx, http.MethodPost, PathDeviceCert, cert)
	return err
}

// Forward posts an uplink pouch and returns the downlink to hand back.
func (c *Client) Forward(ctx context.Context, uplink []byte) ([]byte, error) {
	return c.do(ctx, http.MethodPost, PathPouch, uplink)
}
