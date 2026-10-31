// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jibril Sharafi

#include "gateway_net.h"

#include <cerrno>

#include "esp_random.h"
#include "lwip/sockets.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/oid.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/sha256.h"
#include "mbedtls/x509_crt.h"

namespace GatewayNet
{
    static constexpr uint32_t POLL_CAP_MS = 50;

    static int32_t _remaining(uint32_t deadline) { return (int32_t)(deadline - millis()); }

    static int _random(void* ctx, unsigned char* out, size_t length) {
        (void)ctx;
        esp_fill_random(out, length);
        return 0;
    }

    bool Connection::_wait(bool forWrite, uint32_t deadline, uint32_t capMs) const {
        int32_t remaining = _remaining(deadline);
        if (remaining <= 0) return false;
        uint32_t waitMs = (uint32_t)remaining < capMs ? (uint32_t)remaining : capMs;
        fd_set set;
        FD_ZERO(&set);
        FD_SET(_fd, &set);
        struct timeval tv = {(time_t)(waitMs / 1000), (suseconds_t)((waitMs % 1000) * 1000)};
        return select(_fd + 1, forWrite ? nullptr : &set, forWrite ? &set : nullptr, nullptr, &tv) > 0;
    }

    Status Connection::connect(const uint8_t octets[4], uint16_t port, uint32_t deadline) {
        close();
        _fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (_fd < 0) return Status::NO_MEMORY;
        fcntl(_fd, F_SETFL, fcntl(_fd, F_GETFL, 0) | O_NONBLOCK);

        struct sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_port = htons(port);
        address.sin_addr.s_addr = htonl(((uint32_t)octets[0] << 24) | ((uint32_t)octets[1] << 16) | ((uint32_t)octets[2] << 8) | octets[3]);
        if (::connect(_fd, (struct sockaddr*)&address, sizeof(address)) == 0) return Status::OK;
        if (errno != EINPROGRESS) {
            close();
            return Status::CONNECT_FAILED;
        }
        uint32_t loops = 0;
        while (_remaining(deadline) > 0 && loops++ < 1000) {
            if (!_wait(true, deadline, POLL_CAP_MS)) continue;
            int error = 0;
            socklen_t length = sizeof(error);
            getsockopt(_fd, SOL_SOCKET, SO_ERROR, &error, &length);
            if (error == 0) return Status::OK;
            close();
            return Status::CONNECT_FAILED;
        }
        close();
        return Status::TIMEOUT;
    }

    int Connection::_send(void* ctx, const unsigned char* buf, size_t len) {
        int n = lwip_send(*(int*)ctx, buf, len, 0);
        if (n >= 0) return n;
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? MBEDTLS_ERR_SSL_WANT_WRITE : MBEDTLS_ERR_NET_SEND_FAILED;
    }

    int Connection::_recv(void* ctx, unsigned char* buf, size_t len) {
        int n = lwip_recv(*(int*)ctx, buf, len, 0);
        if (n >= 0) return n; // 0 is EOF
        return (errno == EAGAIN || errno == EWOULDBLOCK) ? MBEDTLS_ERR_SSL_WANT_READ : MBEDTLS_ERR_NET_RECV_FAILED;
    }

    Status Connection::startTls(uint32_t deadline) {
        if (_fd < 0) return Status::CONNECT_FAILED;
        _ssl = (mbedtls_ssl_context*)ps_malloc(sizeof(mbedtls_ssl_context));
        _conf = (mbedtls_ssl_config*)ps_malloc(sizeof(mbedtls_ssl_config));
        if (_ssl == nullptr || _conf == nullptr) {
            close();
            return Status::NO_MEMORY;
        }
        mbedtls_ssl_init(_ssl);
        mbedtls_ssl_config_init(_conf);
        _tlsReady = true;

        int rc = mbedtls_ssl_config_defaults(_conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        if (rc == 0) {
            mbedtls_ssl_conf_authmode(_conf, MBEDTLS_SSL_VERIFY_NONE); // pinned by the caller
            mbedtls_ssl_conf_rng(_conf, _random, nullptr);
            mbedtls_ssl_conf_min_tls_version(_conf, MBEDTLS_SSL_VERSION_TLS1_2);
            rc = mbedtls_ssl_setup(_ssl, _conf);
        }
        if (rc != 0) {
            close();
            return rc == MBEDTLS_ERR_SSL_ALLOC_FAILED ? Status::NO_MEMORY : Status::TLS_FAILED;
        }
        mbedtls_ssl_set_bio(_ssl, &_fd, _send, _recv, nullptr);

        uint32_t loops = 0;
        while ((rc = mbedtls_ssl_handshake(_ssl)) != 0) {
            if ((rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) || loops++ > 100000) {
                    close();
                return rc == MBEDTLS_ERR_SSL_ALLOC_FAILED ? Status::NO_MEMORY : Status::TLS_FAILED;
            }
            if (_remaining(deadline) <= 0) {
                close();
                return Status::TIMEOUT;
            }
            _wait(rc == MBEDTLS_ERR_SSL_WANT_WRITE, deadline, POLL_CAP_MS);
        }
        return Status::OK;
    }

    // Days since 1970-01-01 of a proleptic Gregorian date (Howard Hinnant).
    static int64_t _daysFromCivil(int64_t y, unsigned m, unsigned d) {
        y -= m <= 2;
        const int64_t era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = (unsigned)(y - era * 400);
        const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + (int64_t)doe - 719468;
    }

    bool Connection::peerCert(CertInfo* info) const {
        if (_ssl == nullptr || info == nullptr) return false;
        const mbedtls_x509_crt* crt = mbedtls_ssl_get_peer_cert(_ssl);
        if (crt == nullptr || crt->raw.p == nullptr) return false;
        if (mbedtls_sha256(crt->raw.p, crt->raw.len, info->sha256, 0) != 0) return false;

        info->subjectCn[0] = '\0';
        for (const mbedtls_x509_name* name = &crt->subject; name != nullptr; name = name->next) {
            if (MBEDTLS_OID_CMP(MBEDTLS_OID_AT_CN, &name->oid) == 0) {
                size_t length = name->val.len < sizeof(info->subjectCn) - 1 ? name->val.len : sizeof(info->subjectCn) - 1;
                memcpy(info->subjectCn, name->val.p, length);
                info->subjectCn[length] = '\0';
                break;
            }
        }
        const mbedtls_x509_time& t = crt->valid_to;
        if (t.year >= 1970 && t.mon >= 1 && t.mon <= 12 && t.day >= 1 && t.day <= 31) {
            info->notAfter = (uint64_t)(_daysFromCivil(t.year, (unsigned)t.mon, (unsigned)t.day) * 86400 + t.hour * 3600 + t.min * 60 + t.sec);
        } else {
            info->notAfter = 0;
        }
        return true;
    }

    Status Connection::writeAll(const uint8_t* data, size_t length, uint32_t deadline) {
        if (_fd < 0) return Status::CLOSED;
        size_t sent = 0;
        uint32_t loops = 0;
        while (sent < length) {
            if (_remaining(deadline) <= 0 || loops++ > 100000) return Status::TIMEOUT;
            int rc = _ssl != nullptr ? mbedtls_ssl_write(_ssl, data + sent, length - sent) : _send(&_fd, data + sent, length - sent);
            if (rc > 0) {
                sent += (size_t)rc;
            } else if (rc == MBEDTLS_ERR_SSL_WANT_WRITE || rc == MBEDTLS_ERR_SSL_WANT_READ) {
                _wait(rc == MBEDTLS_ERR_SSL_WANT_WRITE, deadline, POLL_CAP_MS);
            } else {
                return _ssl != nullptr ? Status::TLS_FAILED : Status::CLOSED;
            }
        }
        return Status::OK;
    }

    Status Connection::read(uint8_t* buffer, size_t capacity, size_t* got, uint32_t deadline, uint32_t waitMs) {
        *got = 0;
        if (_fd < 0) return Status::CLOSED;
        if (_remaining(deadline) <= 0) return Status::TIMEOUT; // a trickling peer cannot hold the gateway
        int rc = _ssl != nullptr ? mbedtls_ssl_read(_ssl, buffer, capacity) : _recv(&_fd, buffer, capacity);
        if (rc > 0) {
            *got = (size_t)rc;
            return Status::OK;
        }
        if (rc == 0 || rc == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || rc == MBEDTLS_ERR_SSL_CONN_EOF || rc == MBEDTLS_ERR_NET_CONN_RESET ||
            rc == MBEDTLS_ERR_NET_RECV_FAILED) {
            return Status::CLOSED;
        }
        if (rc != MBEDTLS_ERR_SSL_WANT_READ && rc != MBEDTLS_ERR_SSL_WANT_WRITE) return Status::TLS_FAILED;
        if (_remaining(deadline) <= 0) return Status::TIMEOUT;
        _wait(rc == MBEDTLS_ERR_SSL_WANT_WRITE, deadline, waitMs);
        return Status::OK;
    }

    void Connection::close() {
        if (_tlsReady) {
            mbedtls_ssl_free(_ssl);
            mbedtls_ssl_config_free(_conf);
            _tlsReady = false;
        }
        free(_ssl);
        free(_conf);
        _ssl = nullptr;
        _conf = nullptr;
        if (_fd >= 0) {
            lwip_close(_fd);
            _fd = -1;
        }
    }
}
