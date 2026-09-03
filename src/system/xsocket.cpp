/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 *
 * @modified    Tom Clay, 2026 - Adapted for ReXGlue runtime
 */

#include <cstring>

#include <rex/kernel/xam/module.h>
#include <rex/platform.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xsocket.h>
// #include <rex/system/xnet.h>

#include <rex/net/socket.h>

// Standard socket types used by Xbox API emulation
#if REX_PLATFORM_WIN32
#include <WinSock2.h>

#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

namespace rex::system {

namespace {

// Xbox/Winsock socket-option constants, as seen on the wire from guest code
// (xam_net.cpp's NetDll_setsockopt/NetDll_getsockopt). These do not match
// the host libc's SOL_SOCKET/SO_* numeric values, so every level/optname
// pair has to be translated before reaching the host setsockopt/getsockopt.
constexpr uint32_t kXSolSocket = 0xFFFFu;
constexpr uint32_t kXIpprotoTcpLevel = 6u;
constexpr uint32_t kXSoReuseAddr = 0x0004u;
constexpr uint32_t kXSoBroadcast = 0x0020u;
constexpr uint32_t kXSoLinger = 0x0080u;
constexpr uint32_t kXSoSndBuf = 0x1001u;
constexpr uint32_t kXSoRcvBuf = 0x1002u;
constexpr uint32_t kXTcpNoDelay = 0x0001u;
// Xbox-only SO_VDP.../SO_XNET... pseudo-options (0x5800+); this module has
// no encryption/security-association layer to configure, so they are
// accepted as a no-op success rather than forwarded to the host.
constexpr uint32_t kXOptXboxMin = 0x5800u;
constexpr uint32_t kXOptXboxSecureOffFirst = 0x5801u;
constexpr uint32_t kXOptXboxSecureOffSecond = 0x5802u;

bool MapXboxSocketLevel(uint32_t guest_level, int* out_host_level) {
  if (guest_level == kXSolSocket) {
    *out_host_level = SOL_SOCKET;
    return true;
  }
  if (guest_level == kXIpprotoTcpLevel) {
    *out_host_level = IPPROTO_TCP;
    return true;
  }
  return false;
}

bool MapXboxSocketOptName(uint32_t guest_level, uint32_t optname, int* out_host_optname) {
  if (guest_level == kXSolSocket) {
    switch (optname) {
      case kXSoReuseAddr:
        *out_host_optname = SO_REUSEADDR;
        return true;
      case kXSoBroadcast:
        *out_host_optname = SO_BROADCAST;
        return true;
      case kXSoLinger:
        *out_host_optname = SO_LINGER;
        return true;
      case kXSoSndBuf:
        *out_host_optname = SO_SNDBUF;
        return true;
      case kXSoRcvBuf:
        *out_host_optname = SO_RCVBUF;
        return true;
      default:
        return false;
    }
  }
  if (guest_level == kXIpprotoTcpLevel && optname == kXTcpNoDelay) {
    *out_host_optname = TCP_NODELAY;
    return true;
  }
  return false;
}

bool IsXboxPseudoOption(uint32_t guest_level, uint32_t optname) {
  return guest_level == kXSolSocket && optname >= kXOptXboxMin;
}

uint32_t LoadGuestBE32(const void* p) {
  const uint8_t* b = reinterpret_cast<const uint8_t*>(p);
  return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | uint32_t(b[3]);
}

void StoreGuestBE32(void* p, uint32_t v) {
  uint8_t* b = reinterpret_cast<uint8_t*>(p);
  b[0] = uint8_t(v >> 24);
  b[1] = uint8_t(v >> 16);
  b[2] = uint8_t(v >> 8);
  b[3] = uint8_t(v);
}

uint16_t LoadGuestBE16(const void* p) {
  const uint8_t* b = reinterpret_cast<const uint8_t*>(p);
  return uint16_t((uint32_t(b[0]) << 8) | uint32_t(b[1]));
}

void StoreGuestBE16(void* p, uint16_t v) {
  uint8_t* b = reinterpret_cast<uint8_t*>(p);
  b[0] = uint8_t(v >> 8);
  b[1] = uint8_t(v);
}

// Guest `struct linger` (Winsock shape): {be<u16> l_onoff; be<u16> l_linger;}.
struct linger LoadGuestLinger(const void* guest_optval) {
  struct linger lg {};
  lg.l_onoff = LoadGuestBE16(guest_optval);
  lg.l_linger = LoadGuestBE16(reinterpret_cast<const uint8_t*>(guest_optval) + 2);
  return lg;
}

void StoreGuestLinger(const struct linger& lg, void* guest_optval) {
  StoreGuestBE16(guest_optval, static_cast<uint16_t>(lg.l_onoff));
  StoreGuestBE16(reinterpret_cast<uint8_t*>(guest_optval) + 2, static_cast<uint16_t>(lg.l_linger));
}

}  // namespace

XSocket::XSocket(KernelState* kernel_state) : XObject(kernel_state, kObjectType) {}

XSocket::XSocket(KernelState* kernel_state, uint64_t native_handle)
    : XObject(kernel_state, kObjectType), native_handle_(native_handle) {}

XSocket::~XSocket() {
  Close();
}

X_STATUS XSocket::Initialize(AddressFamily af, Type type, Protocol proto) {
  af_ = af;
  type_ = type;
  proto_ = proto;

  if (proto == Protocol::X_IPPROTO_VDP) {
    // VDP is a layer on top of UDP: the host socket itself is a plain UDP
    // socket, and Send/Recv/SendTo/RecvFrom already forward the caller's
    // buffer byte-for-byte, so the guest's 2-byte VDP header rides through
    // untouched as ordinary payload -- there is nothing else to do here.
    proto = Protocol::X_IPPROTO_UDP;
  }

  native_handle_ = socket(af, type, proto);
  if (native_handle_ == -1) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::Close() {
  int ret = rex::net::socket_close(native_handle_);
  if (ret != 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::SetOption(uint32_t level, uint32_t optname, void* optval_ptr, uint32_t optlen) {
  if (level == kXSolSocket &&
      (optname == kXOptXboxSecureOffFirst || optname == kXOptXboxSecureOffSecond)) {
    // Disable socket encryption
    secure_ = false;
    return X_STATUS_SUCCESS;
  }

  if (IsXboxPseudoOption(level, optname)) {
    // Other Xbox-only SO_VDP.../SO_XNET... pseudo-options: no encryption or
    // NAT layer to configure host-side, accept as a no-op success.
    return X_STATUS_SUCCESS;
  }

  int host_level = 0;
  int host_optname = 0;
  if (!MapXboxSocketLevel(level, &host_level) ||
      !MapXboxSocketOptName(level, optname, &host_optname)) {
    return X_STATUS_UNSUCCESSFUL;
  }

  int ret;
  if (host_optname == SO_LINGER) {
    struct linger lg = LoadGuestLinger(optval_ptr);
    ret = setsockopt(native_handle_, host_level, host_optname, &lg, sizeof(lg));
  } else if (host_optname == SO_BROADCAST || host_optname == SO_REUSEADDR) {
    int enabled = optlen >= 4 ? static_cast<int>(LoadGuestBE32(optval_ptr))
                              : (optval_ptr && *reinterpret_cast<const uint8_t*>(optval_ptr));
    ret = setsockopt(native_handle_, host_level, host_optname, &enabled, sizeof(enabled));
  } else {
    int value = optlen >= 4 ? static_cast<int>(LoadGuestBE32(optval_ptr)) : 0;
    ret = setsockopt(native_handle_, host_level, host_optname, &value, sizeof(value));
  }
  if (ret < 0) {
    // TODO: WSAGetLastError()
    return X_STATUS_UNSUCCESSFUL;
  }

  if (host_optname == SO_BROADCAST) {
    broadcast_socket_ = true;
  }
#if defined(SO_REUSEPORT)
  if (host_optname == SO_REUSEADDR) {
    // SO_REUSEADDR alone does not let two locally bound UDP sockets share a
    // port on Linux; opportunistically also set SO_REUSEPORT so two
    // instances on one host can bind the same System Link port (see
    // net_port_offset for the alternative of shifting ports instead).
    // Best-effort: failure here does not fail the guest's setsockopt call.
    int enabled = 1;
    setsockopt(native_handle_, SOL_SOCKET, SO_REUSEPORT, &enabled, sizeof(enabled));
  }
#endif

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::GetOption(uint32_t level, uint32_t optname, void* optval_ptr,
                            uint32_t* inout_optlen) {
  if (!inout_optlen) {
    return X_STATUS_INVALID_PARAMETER;
  }

  if (IsXboxPseudoOption(level, optname)) {
    if (*inout_optlen >= 4 && optval_ptr) {
      StoreGuestBE32(optval_ptr, 0);
      *inout_optlen = 4;
    }
    return X_STATUS_SUCCESS;
  }

  int host_level = 0;
  int host_optname = 0;
  if (!MapXboxSocketLevel(level, &host_level) ||
      !MapXboxSocketOptName(level, optname, &host_optname)) {
    return X_STATUS_UNSUCCESSFUL;
  }

  if (host_optname == SO_LINGER) {
    struct linger lg {};
    socklen_t len = sizeof(lg);
    if (getsockopt(native_handle_, host_level, host_optname, &lg, &len) != 0) {
      return X_STATUS_UNSUCCESSFUL;
    }
    if (*inout_optlen >= 4 && optval_ptr) {
      StoreGuestLinger(lg, optval_ptr);
      *inout_optlen = 4;
    }
    return X_STATUS_SUCCESS;
  }

  int value = 0;
  socklen_t len = sizeof(value);
  if (getsockopt(native_handle_, host_level, host_optname, &value, &len) != 0) {
    return X_STATUS_UNSUCCESSFUL;
  }
  if (*inout_optlen >= 4 && optval_ptr) {
    StoreGuestBE32(optval_ptr, static_cast<uint32_t>(value));
    *inout_optlen = 4;
  }
  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::IOControl(uint32_t cmd, uint8_t* arg_ptr) {
  int ret = rex::net::socket_ioctl(native_handle_, cmd, arg_ptr);
  if (ret < 0) {
    // TODO: Get last error
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::Connect(N_XSOCKADDR* name, int name_len) {
  int ret = connect(native_handle_, (sockaddr*)name, name_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::Bind(N_XSOCKADDR_IN* name, int name_len) {
  int ret = bind(native_handle_, (sockaddr*)name, name_len);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  bound_ = true;
  bound_port_ = name->sin_port;

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::GetSockName(N_XSOCKADDR_IN* out_name) {
  if (!out_name) {
    return X_STATUS_INVALID_PARAMETER;
  }

  sockaddr_in addr{};
  socklen_t len = sizeof(addr);
  if (getsockname(native_handle_, (sockaddr*)&addr, &len) != 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  out_name->sin_family = addr.sin_family;
  out_name->sin_port = addr.sin_port;
  out_name->sin_addr = ntohl(addr.sin_addr.s_addr);  // BE <- BE, see RecvFrom.
  std::memset(out_name->x_sin_zero, 0, sizeof(out_name->x_sin_zero));

  return X_STATUS_SUCCESS;
}

X_STATUS XSocket::Listen(int backlog) {
  int ret = listen(native_handle_, backlog);
  if (ret < 0) {
    return X_STATUS_UNSUCCESSFUL;
  }

  return X_STATUS_SUCCESS;
}

object_ref<XSocket> XSocket::Accept(N_XSOCKADDR* name, int* name_len) {
  sockaddr n_sockaddr;
  socklen_t n_name_len = sizeof(sockaddr);
  uintptr_t ret = accept(native_handle_, &n_sockaddr, &n_name_len);
  if (ret == -1) {
    std::memset(name, 0, *name_len);
    *name_len = 0;
    return nullptr;
  }

  std::memcpy(name, &n_sockaddr, n_name_len);
  *name_len = n_name_len;

  // Create a kernel object to represent the new socket, and copy parameters
  // over.
  auto socket = object_ref<XSocket>(new XSocket(kernel_state_, ret));
  socket->af_ = af_;
  socket->type_ = type_;
  socket->proto_ = proto_;

  return socket;
}

int XSocket::Shutdown(int how) {
  return shutdown(native_handle_, how);
}

int XSocket::Recv(uint8_t* buf, uint32_t buf_len, uint32_t flags) {
  return recv(native_handle_, reinterpret_cast<char*>(buf), buf_len, flags);
}

int XSocket::RecvFrom(uint8_t* buf, uint32_t buf_len, uint32_t flags, N_XSOCKADDR_IN* from,
                      uint32_t* from_len) {
  // Pop from secure packets first
  // TODO(DrChat): Enable when I commit XNet
  /*
  {
    std::lock_guard<std::mutex> lock(incoming_packet_mutex_);
    if (incoming_packets_.size()) {
      packet* pkt = (packet*)incoming_packets_.front();
      int data_len = pkt->data_len;
      std::memcpy(buf, pkt->data, std::min((uint32_t)pkt->data_len, buf_len));

      from->sin_family = 2;
      from->sin_addr = pkt->src_ip;
      from->sin_port = pkt->src_port;

      incoming_packets_.pop();
      uint8_t* pkt_ui8 = (uint8_t*)pkt;
      delete[] pkt_ui8;

      return data_len;
    }
  }
  */

  sockaddr_in nfrom;
  socklen_t nfromlen = sizeof(sockaddr_in);
  int ret = recvfrom(native_handle_, reinterpret_cast<char*>(buf), buf_len, flags,
                     (sockaddr*)&nfrom, &nfromlen);
  if (from) {
    from->sin_family = nfrom.sin_family;
    from->sin_addr = ntohl(nfrom.sin_addr.s_addr);  // BE <- BE
    from->sin_port = nfrom.sin_port;
    std::memset(from->x_sin_zero, 0, sizeof(from->x_sin_zero));
  }

  if (from_len) {
    *from_len = nfromlen;
  }

  return ret;
}

int XSocket::Send(const uint8_t* buf, uint32_t buf_len, uint32_t flags) {
  return send(native_handle_, reinterpret_cast<const char*>(buf), buf_len, flags);
}

int XSocket::SendTo(uint8_t* buf, uint32_t buf_len, uint32_t flags, N_XSOCKADDR_IN* to,
                    uint32_t to_len) {
  // Send 2 copies of the packet: One to XNet (for network security) and an
  // unencrypted copy for other Xenia hosts.
  // TODO(DrChat): Enable when I commit XNet.
  /*
  auto xam = kernel_state()->GetKernelModule<xam::XamModule>("xam.xex");
  auto xnet = xam->xnet();
  if (xnet) {
    xnet->SendPacket(this, to, buf, buf_len);
  }
  */

  sockaddr_in nto;
  if (to) {
    nto.sin_addr.s_addr = to->sin_addr;
    nto.sin_family = to->sin_family;
    nto.sin_port = to->sin_port;
  }

  return sendto(native_handle_, reinterpret_cast<char*>(buf), buf_len, flags,
                to ? (sockaddr*)&nto : nullptr, to_len);
}

bool XSocket::QueuePacket(uint32_t src_ip, uint16_t src_port, const uint8_t* buf, size_t len) {
  packet* pkt = reinterpret_cast<packet*>(new uint8_t[sizeof(packet) + len]);
  pkt->src_ip = src_ip;
  pkt->src_port = src_port;

  pkt->data_len = (uint16_t)len;
  std::memcpy(pkt->data, buf, len);

  std::lock_guard<std::mutex> lock(incoming_packet_mutex_);
  incoming_packets_.push((uint8_t*)pkt);

  // TODO: Limit on number of incoming packets?
  return true;
}

}  // namespace rex::system
