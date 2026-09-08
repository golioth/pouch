package cloud

import (
	"crypto/tls"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"net/http"
	"os"
	"time"
)

// LoadKeyPair reads a certificate and private key for upstream mTLS. Both DER
// and PEM encodings are accepted, since Golioth's PKI tooling hands out either.
func LoadKeyPair(certPath, keyPath string) (tls.Certificate, error) {
	certBytes, err := os.ReadFile(certPath)
	if err != nil {
		return tls.Certificate{}, fmt.Errorf("read certificate: %w", err)
	}
	keyBytes, err := os.ReadFile(keyPath)
	if err != nil {
		return tls.Certificate{}, fmt.Errorf("read key: %w", err)
	}

	certDER, err := toDER(certBytes)
	if err != nil {
		return tls.Certificate{}, fmt.Errorf("certificate: %w", err)
	}
	keyDER, err := toDER(keyBytes)
	if err != nil {
		return tls.Certificate{}, fmt.Errorf("key: %w", err)
	}

	key, err := parsePrivateKey(keyDER)
	if err != nil {
		return tls.Certificate{}, err
	}

	if _, err := x509.ParseCertificate(certDER); err != nil {
		return tls.Certificate{}, fmt.Errorf("parse certificate: %w", err)
	}

	return tls.Certificate{Certificate: [][]byte{certDER}, PrivateKey: key}, nil
}

// toDER returns the DER body, unwrapping a PEM block if there is one.
func toDER(b []byte) ([]byte, error) {
	if block, _ := pem.Decode(b); block != nil {
		return block.Bytes, nil
	}
	if len(b) == 0 {
		return nil, fmt.Errorf("empty file")
	}
	return b, nil
}

func parsePrivateKey(der []byte) (any, error) {
	if k, err := x509.ParsePKCS8PrivateKey(der); err == nil {
		return k, nil
	}
	if k, err := x509.ParseECPrivateKey(der); err == nil {
		return k, nil
	}
	if k, err := x509.ParsePKCS1PrivateKey(der); err == nil {
		return k, nil
	}
	return nil, fmt.Errorf("parse key: not PKCS#8, SEC1 or PKCS#1")
}

// NewMTLS returns a client that presents cert to the server.
func NewMTLS(address string, timeout time.Duration, cert tls.Certificate, insecure bool) *Client {
	return &Client{
		Address: address,
		HTTP: &http.Client{
			Timeout: timeout,
			Transport: &http.Transport{
				TLSClientConfig: &tls.Config{
					MinVersion:         tls.VersionTLS12,
					Certificates:       []tls.Certificate{cert},
					InsecureSkipVerify: insecure,
				},
			},
		},
	}
}
