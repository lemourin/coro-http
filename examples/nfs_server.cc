#include <array>
#include <cstring>
#include <iostream>
#include <sstream>

#include "coro/http/curl_http.h"
#include "coro/promise.h"
#include "coro/rpc/rpc_exception.h"
#include "coro/rpc/rpc_server.h"
#include "coro/util/event_loop.h"

namespace {

using ::coro::Generator;
using ::coro::Task;
using ::coro::rpc::GetVariableLengthOpaque;
using ::coro::rpc::RpcException;
using ::coro::rpc::RpcRequest;
using ::coro::rpc::RpcResponse;
using ::coro::rpc::RpcResponseAcceptedBody;
using ::coro::rpc::XdrSerializer;
using ::coro::util::DrainTcpDataProvider;
using ::coro::util::TcpResponseChunk;

constexpr uint32_t kPortMapperServicePort = 111;
constexpr uint32_t kNfsServicePort = 2049;
constexpr uint32_t kNfsHandleSize = 64;
constexpr uint32_t kMntPathLength = 1024;
constexpr uint32_t kCookieVerfSize = 8;
constexpr uint64_t kRootFileId = 1;

constexpr uint64_t kFileSize = 158008374;
const std::string_view kFileUrl =
    "https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/"
    "BigBuckBunny.mp4";
const std::string_view kFileName = "video.mp4";
const uint64_t kFileId = 2137;

// constexpr uint64_t kFileSize = 1256;
// const std::string_view kFileUrl = "https://example.com";
// const std::string_view kFileName = "index.html";
// const uint64_t kFileId = 2137;

enum class NfsFileType : uint32_t {
  kReg = 1,
  kDir = 2,
  kBlk = 3,
  kChr = 4,
  kLnk = 5,
  kSock = 6,
  kFifo = 7,
};

enum class NfsStat3 : uint32_t {
  kOk = 0,
  kPerm = 1,
  kNoEnt = 2,
  kIO = 5,
  kInval = 22,
  kNotSupp = 10004
};

struct NfsHandle3 {
  uint64_t fileid;
};

struct NfsTime3 {
  uint32_t seconds;
  uint32_t nseconds;
};

struct NfsSpecData3 {
  uint32_t specdata1;
  uint32_t specdata2;
};

struct NfsFileAttr3 {
  NfsFileType type;
  uint32_t mode;
  uint32_t nlink;
  uint32_t uid;
  uint32_t gid;
  uint64_t size;
  uint64_t used;
  NfsSpecData3 rdev;
  uint64_t fsid;
  uint64_t fileid;
  NfsTime3 atime;
  NfsTime3 mtime;
  NfsTime3 ctime;
};

struct FsInfo3 {
  std::optional<NfsFileAttr3> attributes;
  uint32_t rtmax;
  uint32_t rtpref;
  uint32_t rtmult;
  uint32_t wtmax;
  uint32_t wtpref;
  uint32_t wtmult;
  uint32_t dtpref;
  uint64_t maxfilesize;
  NfsTime3 time_delta;
  uint32_t properties;
};

struct FsStat3 {
  std::optional<NfsFileAttr3> attributes;
  uint64_t tbytes;
  uint64_t fbytes;
  uint64_t abytes;
  uint64_t tfiles;
  uint64_t ffiles;
  uint64_t afiles;
  uint32_t invarsec;
};

struct PathConf3 {
  std::optional<NfsFileAttr3> attributes;
  uint32_t linkmax;
  uint32_t name_max;
  bool no_trunc;
  bool chown_restricted;
  bool case_insensitive;
  bool case_preserving;
};

struct ReadDirPlus3 {
  struct Entry {
    uint64_t fileid;
    std::string name;
    uint64_t cookie;
    std::optional<NfsFileAttr3> attributes;
    std::optional<NfsHandle3> handle;
  };

  std::optional<NfsFileAttr3> attributes;
  std::array<uint8_t, kCookieVerfSize> cookieverf;
  std::vector<Entry> entries;
  bool eof;
};

class NfsXdrSerializer {
 public:
  explicit NfsXdrSerializer(std::vector<uint8_t>* dest) : dest_(dest) {}

  template <typename T>

  NfsXdrSerializer& Put(const std::optional<T>& i) {
    XdrSerializer s{dest_};
    if (i) {
      s.Put(1u);
      Put(*i);
    } else {
      s.Put(0u);
    }
    return *this;
  }

  NfsXdrSerializer& Put(const NfsTime3& time) {
    XdrSerializer{dest_}.Put(time.seconds).Put(time.nseconds);
    return *this;
  }

  NfsXdrSerializer& Put(const NfsSpecData3& spec) {
    XdrSerializer{dest_}.Put(spec.specdata1).Put(spec.specdata2);
    return *this;
  }

  NfsXdrSerializer& Put(const NfsFileAttr3& attr) {
    XdrSerializer s{dest_};
    s.Put(static_cast<uint32_t>(attr.type))
        .Put(attr.mode)
        .Put(attr.nlink)
        .Put(attr.uid)
        .Put(attr.gid)
        .Put(attr.size)
        .Put(attr.used);
    Put(attr.rdev);
    s.Put(attr.fsid).Put(attr.fileid);
    Put(attr.atime);
    Put(attr.mtime);
    Put(attr.ctime);
    return *this;
  }

  NfsXdrSerializer& Put(const FsInfo3& i) {
    XdrSerializer s{dest_};
    Put(i.attributes);
    s.Put(i.rtmax)
        .Put(i.rtpref)
        .Put(i.rtmult)
        .Put(i.wtmax)
        .Put(i.wtpref)
        .Put(i.wtmult)
        .Put(i.dtpref)
        .Put(i.maxfilesize);
    Put(i.time_delta);
    s.Put(i.properties);
    return *this;
  }

  NfsXdrSerializer& Put(const PathConf3& i) {
    XdrSerializer s{dest_};
    Put(i.attributes);
    s.Put(i.linkmax)
        .Put(i.name_max)
        .Put(i.no_trunc)
        .Put(i.chown_restricted)
        .Put(i.case_insensitive)
        .Put(i.case_preserving);
    return *this;
  }

  NfsXdrSerializer& Put(const FsStat3& i) {
    XdrSerializer s{dest_};
    Put(i.attributes);
    s.Put(i.tbytes)
        .Put(i.fbytes)
        .Put(i.abytes)
        .Put(i.tfiles)
        .Put(i.ffiles)
        .Put(i.afiles)
        .Put(i.invarsec);
    return *this;
  }

  NfsXdrSerializer& Put(const ReadDirPlus3& i) {
    XdrSerializer s{dest_};
    Put(i.attributes);
    s.PutFixedSize(i.cookieverf);
    for (const auto& e : i.entries) {
      s.Put(1u).Put(e.fileid).Put(e.name).Put(e.cookie);
      Put(e.attributes).Put(e.handle);
    }
    s.Put(0u).Put(i.eof);
    return *this;
  }

  NfsXdrSerializer& Put(const NfsHandle3& i) {
    XdrSerializer s{dest_};
    s.Put(8u).Put(i.fileid);
    return *this;
  }

 private:
  std::vector<uint8_t>* dest_;
};

std::string ToString(std::span<const uint8_t> bytes) {
  std::string data;
  data.resize(bytes.size());
  memcpy(data.data(), bytes.data(), bytes.size());
  return data;
}

std::string ToArray(std::span<const uint8_t> bytes) {
  std::stringstream stream;
  for (const auto b : bytes) {
    stream << uint32_t(b) << ' ';
  }
  return std::move(stream).str();
}

Generator<TcpResponseChunk> ToResponseChunk(std::vector<uint8_t> data) {
  co_yield std::move(data);
}

RpcResponse ToResponse(std::vector<uint8_t> data) {
  return RpcResponse{.body = {.body = RpcResponseAcceptedBody{
                                  .data = ToResponseChunk(std::move(data))}}};
}

Task<RpcResponse> ToErrorResponse(RpcRequest request,
                                  RpcResponseAcceptedBody::Stat stat) {
  co_await DrainTcpDataProvider(std::move(request.body.data));
  co_return RpcResponse{
      .body = {.body = RpcResponseAcceptedBody{.stat = stat}}};
}

Task<NfsHandle3> GetNfsHandle(coro::util::TcpRequestDataProvider& provider) {
  auto nfs_handle = co_await GetVariableLengthOpaque(provider, kNfsHandleSize);
  if (nfs_handle.size() != 8) {
    throw RpcException(RpcException::kMalformedRequest,
                       "invalid nfs handle size");
  }
  co_return NfsHandle3{.fileid = coro::rpc::ParseUInt64(nfs_handle)};
}

struct NfsLockManagerService {
  static constexpr int kProgId = 100021;
  static constexpr int kProgVersion = 4;
};

struct NfsService {
  static constexpr int kProgId = 100003;
  static constexpr int kProgVersion = 3;

  static constexpr int kGetAttrProcId = 1;
  static constexpr int kLookupProcId = 3;
  static constexpr int kAccessProcId = 4;
  static constexpr int kReadProcId = 6;
  static constexpr int kReadDirPlusProcId = 17;
  static constexpr int kFsStatProcId = 18;
  static constexpr int kFsInfoProcId = 19;
  static constexpr int kPathConfProcId = 20;

  static Task<RpcResponse> GetAttr(RpcRequest request) {
    auto nfs_handle = co_await GetNfsHandle(request.body.data);
    std::cerr << "GETATTR " << nfs_handle.fileid << '\n';
    std::vector<uint8_t> data;
    XdrSerializer{&data}.Put(NfsStat3::kOk);
    NfsXdrSerializer{&data}.Put(
        nfs_handle.fileid == kRootFileId
            ? NfsFileAttr3{.type = NfsFileType::kDir, .mode = 0xfffff}
            : NfsFileAttr3{.type = NfsFileType::kReg,
                           .mode = 0xfffff,
                           .size = kFileSize});
    co_return ToResponse(std::move(data));
  }

  static Task<RpcResponse> Access(RpcRequest request) {
    auto nfs_handle = co_await GetNfsHandle(request.body.data);
    uint32_t access = coro::rpc::ParseInt32(co_await request.body.data(4));
    std::vector<uint8_t> data;
    XdrSerializer{&data}.Put(NfsStat3::kOk).Put(0u).Put(access);
    co_return ToResponse(std::move(data));
  }

  static Task<RpcResponse> Read(RpcRequest request, coro::http::Http* http,
                                coro::stdx::stop_token stop_token) {
    auto nfs_handle = co_await GetNfsHandle(request.body.data);
    uint64_t offset = coro::rpc::ParseUInt64(co_await request.body.data(8));
    uint32_t count = coro::rpc::ParseUInt32(co_await request.body.data(4));
    std::cerr << "READ " << nfs_handle.fileid << " OFFSET = " << offset
              << " COUNT = " << count << '\n';
    std::vector<uint8_t> data;
    XdrSerializer s{&data};
    if (nfs_handle.fileid != kFileId) {
      s.Put(NfsStat3::kInval).Put(0u);
      co_return ToResponse(std::move(data));
    }
    coro::http::Request<> http_request{
        .url = std::string(kFileUrl),
        .headers = {coro::http::ToRangeHeader(
            coro::http::Range{.start = static_cast<int64_t>(offset),
                              .end = offset + count - 1})}};
    auto http_response =
        co_await http->FetchOk(std::move(http_request), stop_token);
    auto body = co_await coro::http::GetBody(std::move(http_response.body));

    s.Put(NfsStat3::kOk)
        .Put(0u)
        .Put(static_cast<uint32_t>(body.size()))
        .Put(offset + count >= kFileSize)
        .Put(body);
    co_return ToResponse(std::move(data));
  }

  static Task<RpcResponse> Lookup(RpcRequest request) {
    auto nfs_handle = co_await GetNfsHandle(request.body.data);
    auto filename = ToString(
        co_await GetVariableLengthOpaque(request.body.data, kNfsHandleSize));
    std::cerr << "LOOKUP " << filename << '\n';
    std::vector<uint8_t> data;
    XdrSerializer{&data}.Put(NfsStat3::kNoEnt).Put(0u);
    co_return ToResponse(std::move(data));
  }

  static Task<RpcResponse> ReadDirPlus(RpcRequest request) {
    auto nfs_handle = co_await GetNfsHandle(request.body.data);
    auto cookie = coro::rpc::ParseUInt64(co_await request.body.data(8));
    auto cookie_verf = co_await request.body.data(kCookieVerfSize);
    auto dir_count = coro::rpc::ParseUInt32(co_await request.body.data(4));
    auto max_count = coro::rpc::ParseUInt32(co_await request.body.data(4));
    std::cerr << "COOKIE = " << cookie << ' ' << "DR COUNT = " << dir_count
              << ' ' << "MAX COUNT = " << max_count << '\n';
    std::cerr << "COOKIE VERF = " << ToArray(cookie_verf) << '\n';
    std::vector<uint8_t> data;
    XdrSerializer{&data}.Put(NfsStat3::kOk);
    NfsXdrSerializer(&data).Put(
        ReadDirPlus3{.entries = {ReadDirPlus3::Entry{
                         .fileid = kFileId,
                         .name = std::string(kFileName),
                         .cookie = 2311,
                         .attributes = NfsFileAttr3{.type = NfsFileType::kReg,
                                                    .mode = 0xfffff,
                                                    .size = kFileSize},
                         .handle = NfsHandle3{kFileId}}},
                     .eof = true});
    co_return ToResponse(std::move(data));
  }

  static Task<RpcResponse> FsStat(RpcRequest request) {
    auto nfs_handle = co_await GetNfsHandle(request.body.data);
    std::vector<uint8_t> data;
    XdrSerializer(&data).Put(NfsStat3::kOk);
    uint64_t total_space = 2137ull << 50;
    uint64_t free_space = 420ull << 50;
    NfsXdrSerializer(&data).Put(FsStat3{.tbytes = total_space,
                                        .fbytes = free_space,
                                        .abytes = free_space,
                                        .tfiles = UINT64_MAX,
                                        .ffiles = UINT64_MAX,
                                        .afiles = UINT64_MAX,
                                        .invarsec = 0});
    co_return ToResponse(std::move(data));
  }

  static Task<RpcResponse> FsInfo(RpcRequest request) {
    auto nfs_handle = co_await GetNfsHandle(request.body.data);
    std::vector<uint8_t> data;
    XdrSerializer(&data).Put(NfsStat3::kOk);
    NfsXdrSerializer(&data).Put(FsInfo3{.rtmax = 1024 * 1024,
                                        .rtpref = 1024 * 1024,
                                        .rtmult = 1,
                                        .wtmax = 0,
                                        .wtpref = 0,
                                        .wtmult = 1,
                                        .dtpref = 1024 * 1024,
                                        .maxfilesize = UINT64_MAX,
                                        .time_delta = {.seconds = 1},
                                        .properties = 0x0008});
    co_return ToResponse(std::move(data));
  }

  static Task<RpcResponse> PathConf(RpcRequest request) {
    auto nfs_handle = co_await GetNfsHandle(request.body.data);
    std::vector<uint8_t> data;
    XdrSerializer(&data).Put(NfsStat3::kOk);
    NfsXdrSerializer(&data).Put(PathConf3{.name_max = 255});
    co_return ToResponse(std::move(data));
  }
};

struct MountService {
  static constexpr int kProgId = 100005;
  static constexpr int kProgVersion = 3;

  static constexpr int kMountProcId = 1;
  static constexpr int kUnmountProcId = 3;
  static constexpr int kExportProcId = 5;

  static Task<RpcResponse> Mount(RpcRequest request) {
    auto dir_path =
        co_await GetVariableLengthOpaque(request.body.data, kMntPathLength);
    std::cerr << "MOUNT " << ToString(dir_path) << '\n';
    std::vector<uint8_t> data;
    XdrSerializer s{&data};
    s.Put(0u);
    NfsXdrSerializer{&data}.Put(NfsHandle3{.fileid = kRootFileId});
    s.Put(1u).Put(0u);
    co_return ToResponse(std::move(data));
  }

  static Task<RpcResponse> Unmount(RpcRequest request) {
    auto dir_path =
        co_await GetVariableLengthOpaque(request.body.data, kMntPathLength);
    std::cerr << "UNMOUNT " << ToString(dir_path) << '\n';
    co_return RpcResponse{};
  }

  static Task<RpcResponse> Export(RpcRequest request) {
    std::vector<uint8_t> data;
    XdrSerializer{&data}.Put(1u).Put(std::string_view("/")).Put(0u).Put(0u);
    co_return ToResponse(std::move(data));
  }
};

struct StatusMonitorService {
  static constexpr int kProgId = 100024;
  static constexpr int kProgVersion = 1;
};

struct PortMapperService {
  static constexpr int kProgId = 100000;
  static constexpr int kProgVersion = 2;

  static constexpr int kGetPortProcId = 3;

  static constexpr int kTcpProtocol = 6;

  static Task<RpcResponse> GetPort(RpcRequest request) {
    uint32_t prog = coro::rpc::ParseUInt32(co_await request.body.data(4));
    uint32_t vers = coro::rpc::ParseUInt32(co_await request.body.data(4));
    uint32_t prot = coro::rpc::ParseUInt32(co_await request.body.data(4));
    uint32_t port = coro::rpc::ParseUInt32(co_await request.body.data(4));
    std::cerr << "PORTMAPPER: PROG = " << prog << " VERS = " << vers
              << " PROT = " << prot << " PORT = " << port << '\n';
    std::vector<uint8_t> data;
    XdrSerializer{&data}.Put([&] {
      if (prot == kTcpProtocol &&
          ((prog == NfsService::kProgId && vers == NfsService::kProgVersion) ||
           (prog == NfsLockManagerService::kProgId &&
            vers == NfsLockManagerService::kProgVersion) ||
           (prog == MountService::kProgId &&
            vers == MountService::kProgVersion) ||
           (prog == StatusMonitorService::kProgId &&
            vers == StatusMonitorService::kProgVersion))) {
        return kNfsServicePort;
      } else {
        return 0u;
      }
    }());
    co_return ToResponse(std::move(data));
  }
};

class NfsRpcHandler {
 public:
  explicit NfsRpcHandler(coro::http::Http* http) : http_(http) {}

  Task<RpcResponse> operator()(RpcRequest request,
                               coro::stdx::stop_token stop_token) const {
    std::cerr << "XID = " << request.xid << " prog = " << request.body.prog
              << " vers = " << request.body.vers
              << " proc =" << request.body.proc << '\n';

    switch (request.body.prog) {
      case PortMapperService::kProgId: {
        if (request.body.vers != PortMapperService::kProgVersion) {
          co_return co_await ToErrorResponse(
              std::move(request), RpcResponseAcceptedBody::Stat::kProcUnavail);
        }
        switch (request.body.proc) {
          case 0:
            co_return RpcResponse{};
          case PortMapperService::kGetPortProcId:
            co_return co_await PortMapperService::GetPort(std::move(request));
          default:
            co_return co_await ToErrorResponse(
                std::move(request),
                RpcResponseAcceptedBody::Stat::kProcUnavail);
        }
      }
      case MountService::kProgId: {
        if (request.body.vers != MountService::kProgVersion) {
          co_return co_await ToErrorResponse(
              std::move(request), RpcResponseAcceptedBody::Stat::kProcUnavail);
        }
        switch (request.body.proc) {
          case 0:
            co_return RpcResponse{};
          case MountService::kMountProcId:
            co_return co_await MountService::Mount(std::move(request));
          case MountService::kUnmountProcId:
            co_return co_await MountService::Unmount(std::move(request));
          case MountService::kExportProcId:
            co_return co_await MountService::Export(std::move(request));
          default:
            co_return co_await ToErrorResponse(
                std::move(request),
                RpcResponseAcceptedBody::Stat::kProcUnavail);
        }
      }
      case NfsService::kProgId: {
        if (request.body.vers != NfsService::kProgVersion) {
          co_return co_await ToErrorResponse(
              std::move(request), RpcResponseAcceptedBody::Stat::kProcUnavail);
        }
        switch (request.body.proc) {
          case 0:
            co_return RpcResponse{};
          case NfsService::kGetAttrProcId:
            co_return co_await NfsService::GetAttr(std::move(request));
          case NfsService::kAccessProcId:
            co_return co_await NfsService::Access(std::move(request));
          case NfsService::kLookupProcId:
            co_return co_await NfsService::Lookup(std::move(request));
          case NfsService::kReadProcId:
            co_return co_await NfsService::Read(std::move(request), http_,
                                                std::move(stop_token));
          case NfsService::kReadDirPlusProcId:
            co_return co_await NfsService::ReadDirPlus(std::move(request));
          case NfsService::kFsStatProcId:
            co_return co_await NfsService::FsStat(std::move(request));
          case NfsService::kFsInfoProcId:
            co_return co_await NfsService::FsInfo(std::move(request));
          case NfsService::kPathConfProcId:
            co_return co_await NfsService::PathConf(std::move(request));
          default:
            co_return co_await ToErrorResponse(
                std::move(request),
                RpcResponseAcceptedBody::Stat::kProcUnavail);
        }
      }
      case NfsLockManagerService::kProgId: {
        if (request.body.vers != NfsLockManagerService::kProgVersion) {
          co_return co_await ToErrorResponse(
              std::move(request), RpcResponseAcceptedBody::Stat::kProcUnavail);
        }
        switch (request.body.proc) {
          case 0:
            co_return RpcResponse{};
          default:
            co_return co_await ToErrorResponse(
                std::move(request),
                RpcResponseAcceptedBody::Stat::kProcUnavail);
        }
      }
      default: {
        co_return co_await ToErrorResponse(
            std::move(request), RpcResponseAcceptedBody::Stat::kProgUnavail);
      }
    }
  }

 private:
  coro::http::Http* http_;
};

coro::Task<void> RunMain(const coro::util::EventLoop* event_loop) {
  coro::Promise<void> semaphore;
  coro::http::Http http{coro::http::CurlHttp(event_loop)};
  auto portmapper = coro::rpc::CreateRpcServer(
      NfsRpcHandler(&http), event_loop,
      {.address = "0.0.0.0", .port = kPortMapperServicePort});
  auto nfsd = coro::rpc::CreateRpcServer(
      NfsRpcHandler(&http), event_loop,
      {.address = "0.0.0.0", .port = kNfsServicePort});
  co_await semaphore;
}

}  // namespace

int main() {
#ifdef SIGPIPE
  signal(SIGPIPE, SIG_IGN);
#endif
  coro::util::EventLoop event_loop;
  coro::RunTask(RunMain, &event_loop);
  event_loop.EnterLoop();
  return 0;
}