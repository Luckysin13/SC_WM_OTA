#ifndef SSL_CERT_H
#define SSL_CERT_H

// Self-signed SSL certificate for HTTPS server
// Generated with:
// openssl req -x509 -newkey rsa:2048 -keyout server_key.pem -out server_cert.pem -days 3650 -nodes

extern const char server_cert_pem_start[] asm("_binary_server_cert_pem_start");
extern const char server_cert_pem_end[] asm("_binary_server_cert_pem_end");

extern const char server_key_pem_start[] asm("_binary_server_key_pem_start");
extern const char server_key_pem_end[] asm("_binary_server_key_pem_end");

#endif // SSL_CERT_H
