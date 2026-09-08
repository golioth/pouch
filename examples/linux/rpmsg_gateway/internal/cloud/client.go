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
	resp, err := client.Do(req)
	if err != nil {
		return nil, fmt.Errorf("%s %s: %w", method, path, err)
	}
	defer func() { _ = resp.Body.Close() }()

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
