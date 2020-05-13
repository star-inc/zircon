// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/device/llcpp/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fdio/cpp/caller.h>
#include <lib/fdio/unsafe.h>
#include <lib/memfs/memfs.h>
#include <limits.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <fs-management/fvm.h>
#include <unittest/unittest.h>

#include "filesystems.h"

const char* filesystem_name_filter = "";

static void print_test_help(FILE* f) {
  fprintf(f,
          "  -d <blkdev>\n"
          "      Use block device <blkdev> instead of a ramdisk\n"
          "\n"
          "  -f <fs>\n"
          "      Test only fileystem <fs>, where <fs> is one of:\n");
  for (int j = 0; j < NUM_FILESYSTEMS; j++) {
    fprintf(f, "%8s%s\n", "", FILESYSTEMS[j].name);
  }
}

int main(int argc, char** argv) {
  use_real_disk = false;

  unittest_register_test_help_printer(print_test_help);

  int i = 1;
  while (i < argc) {
    if (strcmp(argv[i], "-d") == 0 && (i + 1 < argc)) {
      fbl::unique_fd fd(open(argv[i + 1], O_RDWR));
      if (!fd) {
        fprintf(stderr, "[fs] Could not open block device\n");
        return -1;
      }
      fdio_cpp::FdioCaller caller(std::move(fd));

      size_t path_len;
      auto resp = ::llcpp::fuchsia::device::Controller::Call::GetTopologicalPath(
          zx::unowned_channel(caller.borrow_channel()));
      zx_status_t status = resp.status();
      if (resp->result.is_err()) {
        status = resp->result.err();
      } else {
        auto& r = resp->result.response();
        path_len = r.path.size();
        if (path_len > PATH_MAX) {
          return ZX_ERR_INTERNAL;
        }
        memcpy(test_disk_path, r.path.data(), r.path.size());
      }

      if (status != ZX_OK) {
        fprintf(stderr, "[fs] Could not acquire topological path of block device\n");
        return -1;
      }
      test_disk_path[path_len] = 0;
      zx_status_t io_status =
          fuchsia_hardware_block_BlockGetInfo(caller.borrow_channel(), &status, &test_disk_info);
      if (io_status != ZX_OK) {
        status = io_status;
      }

      if (status != ZX_OK) {
        fprintf(stderr, "[fs] Could not read disk info\n");
        return -1;
      }
      // If we previously tried running tests on this disk, it may
      // have created an FVM and failed. (Try to) clean up from previous state
      // before re-running.
      fvm_destroy(test_disk_path);
      use_real_disk = true;
      i += 2;
    } else if (!strcmp(argv[i], "-f") && (i + 1 < argc)) {
      bool found = false;
      for (int j = 0; j < NUM_FILESYSTEMS; j++) {
        if (!strcmp(argv[i + 1], FILESYSTEMS[j].name)) {
          found = true;
          filesystem_name_filter = argv[i + 1];
          break;
        }
      }
      if (!found) {
        fprintf(stderr, "Error: Filesystem not found\n");
        return -1;
      }
      i += 2;
    } else {
      // Ignore options we don't recognize. See ulib/unittest/README.md.
      break;
    }
  }

  int result;
  memfs_filesystem_t* fs;
  {
    // Initialize tmpfs.
    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    if (loop.StartThread() != ZX_OK) {
      fprintf(stderr, "Error: Cannot initialize local tmpfs loop\n");
      return -1;
    }
    if (memfs_install_at(loop.dispatcher(), kTmpfsPath, &fs) != ZX_OK) {
      fprintf(stderr, "Error: Cannot install local tmpfs\n");
      return -1;
    }

    result = unittest_run_all_tests(argc, argv) ? 0 : -1;
  }
  memfs_uninstall_unsafe(fs, kTmpfsPath);
  return result;
}
