/**
 * Unit tests for VirtualFileSystem + HostPathDevice/NullDevice interaction.
 *
 * These pin down two behaviors that motivated mounting a real writable
 * Cache0 device instead of routing it through the NullDevice:
 *
 *  1. HostPathDevice actually supports FILE_OPEN_IF (create-if-missing) and
 *     FILE_DIRECTORY_FILE (create-if-missing directories), including the
 *     real write/flush/close/reopen cycle a title's storage init depends on.
 *  2. NullDevice cannot satisfy a create-if request for a path it doesn't
 *     already know about: NullEntry never overrides CreateEntryInternal, so
 *     VirtualFileSystem::OpenFile's fallback CreatePath() call returns
 *     nullptr and OpenFile reports X_STATUS_ACCESS_DENIED. This is the exact
 *     failure observed for 'cache0:\preferences.dat' before Cache0 got a
 *     writable HostPathDevice mount.
 *
 * All I/O happens under a scoped temporary directory; nothing here touches
 * a user's real profile/save data.
 */

#include <cstdint>
#include <filesystem>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include <rex/filesystem/devices/host_path_device.h>
#include <rex/filesystem/devices/null_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>
#include <rex/filesystem/vfs.h>

using namespace rex;  // NOLINT: X_STATUS_* macros expand to the unqualified rex::X_STATUS typedef.
using rex::filesystem::FileAccess;
using rex::filesystem::FileAction;
using rex::filesystem::FileDisposition;
using rex::filesystem::HostPathDevice;
using rex::filesystem::NullDevice;
using rex::filesystem::VirtualFileSystem;

namespace {

// NT STATUS_NOT_A_DIRECTORY (0xC0000103). Mirrors the private constant used
// by VirtualFileSystem::OpenFile in src/filesystem/virtual_file_system.cpp;
// not yet promoted to a shared X_STATUS_* macro in xtypes.h.
constexpr rex::X_STATUS kStatusNotADirectory = static_cast<rex::X_STATUS>(0xC0000103L);

class ScopedTempDir {
 public:
  ScopedTempDir() {
    path_ =
        std::filesystem::temp_directory_path() / ("rexglue_vfs_test_" + std::to_string(counter_++));
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~ScopedTempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
  static inline int counter_ = 0;
};

}  // namespace

TEST_CASE("HostPathDevice honors FILE_OPEN_IF and writes real bytes", "[filesystem][vfs]") {
  ScopedTempDir root;

  VirtualFileSystem vfs;
  auto device = std::make_unique<HostPathDevice>("\\Device\\TestCache", root.path(), false);
  REQUIRE(device->Initialize());
  REQUIRE(vfs.RegisterDevice(std::move(device)));
  vfs.RegisterSymbolicLink("cache0:", "\\Device\\TestCache");

  // First open: the file does not exist yet, so FILE_OPEN_IF must create it,
  // exactly like the title's NtCreateFile('cache0:\preferences.dat', 0x3).
  rex::filesystem::File* file = nullptr;
  FileAction action{};
  auto status = vfs.OpenFile(nullptr, "cache0:\\preferences.dat", FileDisposition::kOpenIf,
                             FileAccess::kGenericRead | FileAccess::kGenericWrite,
                             /*is_directory=*/false, /*is_non_directory=*/true, &file, &action);
  REQUIRE(status == X_STATUS_SUCCESS);
  REQUIRE(file != nullptr);
  CHECK(action == FileAction::kCreated);

  const uint8_t payload[] = {'r', 'e', 'x'};
  size_t bytes_written = 0;
  REQUIRE(file->WriteSync(payload, 0, &bytes_written) == X_STATUS_SUCCESS);
  CHECK(bytes_written == sizeof(payload));
  file->Destroy();

  // The file must exist on the host with the bytes we actually wrote: no
  // fabricated checkpoint, just the real host file the title created.
  auto host_file = root.path() / "preferences.dat";
  REQUIRE(std::filesystem::exists(host_file));
  CHECK(std::filesystem::file_size(host_file) == sizeof(payload));

  // Second open: the file now exists, so FILE_OPEN_IF must open (not
  // recreate) it, and the previously-written bytes must survive the
  // close/reopen cycle.
  rex::filesystem::File* reopened = nullptr;
  FileAction reopen_action{};
  status = vfs.OpenFile(nullptr, "cache0:\\preferences.dat", FileDisposition::kOpenIf,
                        FileAccess::kGenericRead, /*is_directory=*/false,
                        /*is_non_directory=*/true, &reopened, &reopen_action);
  REQUIRE(status == X_STATUS_SUCCESS);
  CHECK(reopen_action == FileAction::kOpened);

  uint8_t read_back[sizeof(payload)] = {};
  size_t bytes_read = 0;
  REQUIRE(reopened->ReadSync(read_back, 0, &bytes_read) == X_STATUS_SUCCESS);
  CHECK(bytes_read == sizeof(payload));
  CHECK(std::string_view(reinterpret_cast<char*>(read_back), bytes_read) == "rex");
  reopened->Destroy();
}

TEST_CASE("HostPathDevice creates intermediate directories for FILE_DIRECTORY_FILE",
          "[filesystem][vfs]") {
  ScopedTempDir root;

  VirtualFileSystem vfs;
  auto device = std::make_unique<HostPathDevice>("\\Device\\TestCache", root.path(), false);
  REQUIRE(device->Initialize());
  REQUIRE(vfs.RegisterDevice(std::move(device)));
  vfs.RegisterSymbolicLink("cache1:", "\\Device\\TestCache");

  // Mirrors probing/creating 'cache1:\upload_queue\' (directory_file=true).
  rex::filesystem::File* dir_file = nullptr;
  FileAction action{};
  auto status = vfs.OpenFile(nullptr, "cache1:\\upload_queue", FileDisposition::kOpenIf,
                             FileAccess::kGenericRead, /*is_directory=*/true,
                             /*is_non_directory=*/false, &dir_file, &action);
  REQUIRE(status == X_STATUS_SUCCESS);
  CHECK(action == FileAction::kCreated);
  dir_file->Destroy();

  auto host_dir = root.path() / "upload_queue";
  REQUIRE(std::filesystem::exists(host_dir));
  CHECK(std::filesystem::is_directory(host_dir));
}

TEST_CASE(
    "OpenFile rejects FILE_DIRECTORY_FILE against an existing regular file "
    "(STATUS_NOT_A_DIRECTORY)",
    "[filesystem][vfs]") {
  // Reproduces the exact race from campaign_cache0_026_runtime.log: one
  // thread creates 'cache1:\autosave\asq_010_jun_BBBBBBBB.temp' with
  // FILE_OVERWRITE_IF (a regular file) and keeps writing to it, while another
  // thread opens that same path with FILE_DIRECTORY_FILE to probe whether it
  // is a directory. Real NT fails that probe with STATUS_NOT_A_DIRECTORY; the
  // VFS previously reported success instead, and the caller then marked the
  // still-being-written checkpoint delete-on-close.
  ScopedTempDir root;

  VirtualFileSystem vfs;
  auto device = std::make_unique<HostPathDevice>("\\Device\\TestCache", root.path(), false);
  REQUIRE(device->Initialize());
  REQUIRE(vfs.RegisterDevice(std::move(device)));
  vfs.RegisterSymbolicLink("cache1:", "\\Device\\TestCache");

  // The autosave directory itself must already exist (mirrors the title
  // creating 'cache1:\autosave\' once at storage init time, well before any
  // individual checkpoint file).
  rex::filesystem::File* autosave_dir = nullptr;
  FileAction autosave_dir_action{};
  REQUIRE(vfs.OpenFile(nullptr, "cache1:\\autosave", FileDisposition::kOpenIf,
                       FileAccess::kGenericRead, /*is_directory=*/true, /*is_non_directory=*/false,
                       &autosave_dir, &autosave_dir_action) == X_STATUS_SUCCESS);
  autosave_dir->Destroy();

  // The writer thread's open: FILE_OVERWRITE_IF against a brand-new path
  // creates a real regular file and keeps it open (mirrors handle 0xf8000130
  // in the trace).
  rex::filesystem::File* writer_file = nullptr;
  FileAction writer_action{};
  auto writer_status = vfs.OpenFile(
      nullptr, "cache1:\\autosave\\asq_010_jun_BBBBBBBB.temp", FileDisposition::kOverwriteIf,
      FileAccess::kGenericRead | FileAccess::kGenericWrite,
      /*is_directory=*/false, /*is_non_directory=*/false, &writer_file, &writer_action);
  REQUIRE(writer_status == X_STATUS_SUCCESS);
  REQUIRE(writer_file != nullptr);

  const uint8_t payload[] = {'c', 'k', 'p', 't'};
  size_t bytes_written = 0;
  REQUIRE(writer_file->WriteSync(payload, 0, &bytes_written) == X_STATUS_SUCCESS);
  CHECK(bytes_written == sizeof(payload));

  // The second thread's probe: FILE_OPEN + FILE_DIRECTORY_FILE against the
  // exact same path, while the writer's handle above is still open. This
  // must fail instead of succeeding.
  rex::filesystem::File* probe_file = nullptr;
  FileAction probe_action{};
  auto probe_status =
      vfs.OpenFile(nullptr, "cache1:\\autosave\\asq_010_jun_BBBBBBBB.temp", FileDisposition::kOpen,
                   FileAccess::kGenericRead, /*is_directory=*/true, /*is_non_directory=*/false,
                   &probe_file, &probe_action);
  CHECK(probe_status == kStatusNotADirectory);
  CHECK(probe_file == nullptr);

  writer_file->Destroy();

  // The real host file must survive untouched: no fabricated success, no
  // deletion triggered by the (now-rejected) directory probe.
  auto host_file = root.path() / "autosave" / "asq_010_jun_BBBBBBBB.temp";
  REQUIRE(std::filesystem::exists(host_file));
  CHECK(std::filesystem::file_size(host_file) == sizeof(payload));
}

TEST_CASE(
    "OpenFile rejects FILE_NON_DIRECTORY_FILE against an existing directory "
    "(STATUS_FILE_IS_A_DIRECTORY)",
    "[filesystem][vfs]") {
  // Mirrors 'cache1:\autosave\' itself: a real directory that must keep
  // opening successfully with FILE_DIRECTORY_FILE, but must fail with
  // STATUS_FILE_IS_A_DIRECTORY if a caller demands FILE_NON_DIRECTORY_FILE.
  ScopedTempDir root;

  VirtualFileSystem vfs;
  auto device = std::make_unique<HostPathDevice>("\\Device\\TestCache", root.path(), false);
  REQUIRE(device->Initialize());
  REQUIRE(vfs.RegisterDevice(std::move(device)));
  vfs.RegisterSymbolicLink("cache1:", "\\Device\\TestCache");

  rex::filesystem::File* dir_file = nullptr;
  FileAction dir_action{};
  REQUIRE(vfs.OpenFile(nullptr, "cache1:\\autosave", FileDisposition::kOpenIf,
                       FileAccess::kGenericRead, /*is_directory=*/true, /*is_non_directory=*/false,
                       &dir_file, &dir_action) == X_STATUS_SUCCESS);
  dir_file->Destroy();

  // Legitimate re-open with FILE_DIRECTORY_FILE must still succeed.
  rex::filesystem::File* reopened_dir = nullptr;
  FileAction reopened_action{};
  CHECK(vfs.OpenFile(nullptr, "cache1:\\autosave", FileDisposition::kOpen, FileAccess::kGenericRead,
                     /*is_directory=*/true, /*is_non_directory=*/false, &reopened_dir,
                     &reopened_action) == X_STATUS_SUCCESS);
  CHECK(reopened_dir != nullptr);
  reopened_dir->Destroy();

  // Opening the same directory with FILE_NON_DIRECTORY_FILE must fail.
  rex::filesystem::File* non_dir_probe = nullptr;
  FileAction non_dir_action{};
  auto status = vfs.OpenFile(
      nullptr, "cache1:\\autosave", FileDisposition::kOpen, FileAccess::kGenericRead,
      /*is_directory=*/false, /*is_non_directory=*/true, &non_dir_probe, &non_dir_action);
  CHECK(status == X_STATUS_FILE_IS_A_DIRECTORY);
  CHECK(non_dir_probe == nullptr);
}

TEST_CASE("NullDevice rejects create-if for paths it doesn't already know", "[filesystem][vfs]") {
  VirtualFileSystem vfs;
  // Reproduces the pre-fix Cache0 mount: a NullDevice claiming \Cache0 with
  // no writable backing store.
  auto null_device = std::make_unique<NullDevice>("\\Device\\Harddisk0",
                                                  std::initializer_list<std::string>{"\\Cache0"});
  REQUIRE(null_device->Initialize());
  REQUIRE(vfs.RegisterDevice(std::move(null_device)));
  vfs.RegisterSymbolicLink("cache0:", "\\Device\\Harddisk0\\Cache0");

  rex::filesystem::File* file = nullptr;
  FileAction action{};
  auto status = vfs.OpenFile(nullptr, "cache0:\\preferences.dat", FileDisposition::kOpenIf,
                             FileAccess::kGenericRead | FileAccess::kGenericWrite,
                             /*is_directory=*/false, /*is_non_directory=*/true, &file, &action);

  // This is the exact failure mode observed in the campaign_filetrace_025
  // session: disposition=FILE_OPEN_IF against a NullDevice-backed path comes
  // back STATUS_ACCESS_DENIED instead of creating anything.
  CHECK(status == X_STATUS_ACCESS_DENIED);
}
