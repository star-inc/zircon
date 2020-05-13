// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <zircon/compiler.h>

#include <fbl/algorithm.h>
#include <fbl/unique_fd.h>

#include "filesystems.h"
#include "misc.h"

bool TestDirectoryFilenameMax(void) {
  BEGIN_TEST;

  // TODO(smklein): This value may be filesystem-specific. Plumb it through
  // from the test driver.
  constexpr int max_file_len = 255;
  char path[PATH_MAX + 1];

  // Unless the max_file_len is approaching half of PATH_MAX,
  // this shouldn't be an issue.
  static_assert((2 /* '::' */ + (max_file_len + 1) + 1 /* slash */ + max_file_len) < PATH_MAX);
  // Large components should not crash vfs
  snprintf(path, sizeof(path), "::%0*d/%0*d", max_file_len + 1, 0xBEEF, max_file_len, 0xBEEF);
  ASSERT_EQ(open(path, O_RDWR | O_CREAT | O_EXCL, 0644), -1);
  ASSERT_EQ(errno, ENAMETOOLONG);

  // Largest possible file length
  snprintf(path, sizeof(path), "::%0*d", max_file_len, 0x1337);
  fbl::unique_fd fd(open(path, O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(path), 0);

  // Slightly too large file length
  snprintf(path, sizeof(path), "::%0*d", max_file_len + 1, 0xBEEF);
  ASSERT_EQ(open(path, O_RDWR | O_CREAT | O_EXCL, 0644), -1);
  ASSERT_EQ(errno, ENAMETOOLONG);

  END_TEST;
}

// Hopefully not pushing against any 'max file length' boundaries, but large
// enough to fill a directory quickly.
#define LARGE_PATH_LENGTH 128

bool TestDirectoryLarge(void) {
  BEGIN_TEST;

  // ZX-2107: This test is humongous: ~8000 seconds in non-kvm qemu on
  // a fast desktop.
  unittest_cancel_timeout();

  // Write a bunch of files to a directory
  const int num_files = 1024;
  for (int i = 0; i < num_files; i++) {
    char path[LARGE_PATH_LENGTH + 1];
    snprintf(path, sizeof(path), "::%0*d", LARGE_PATH_LENGTH - 2, i);
    fbl::unique_fd fd(open(path, O_RDWR | O_CREAT | O_EXCL, 0644));
    ASSERT_TRUE(fd);
  }

  // Unlink all those files
  for (int i = 0; i < num_files; i++) {
    char path[LARGE_PATH_LENGTH + 1];
    snprintf(path, sizeof(path), "::%0*d", LARGE_PATH_LENGTH - 2, i);
    ASSERT_EQ(unlink(path), 0);
  }

  // TODO(smklein): Verify contents

  END_TEST;
}

bool TestDirectoryMax(void) {
  BEGIN_TEST;

  // Write the maximum number of files to a directory
  int i = 0;
  for (;; i++) {
    char path[LARGE_PATH_LENGTH + 1];
    snprintf(path, sizeof(path), "::%0*d", LARGE_PATH_LENGTH - 2, i);
    if (i % 100 == 0) {
      printf(" Allocating: %s\n", path);
    }

    fbl::unique_fd fd(open(path, O_RDWR | O_CREAT | O_EXCL, 0644));
    if (!fd) {
      printf("    wrote %d direntries\n", i);
      break;
    }
  }

  // Unlink all those files
  for (i -= 1; i >= 0; i--) {
    char path[LARGE_PATH_LENGTH + 1];
    snprintf(path, sizeof(path), "::%0*d", LARGE_PATH_LENGTH - 2, i);
    ASSERT_EQ(unlink(path), 0);
  }

  END_TEST;
}

bool TestDirectoryCoalesceHelper(const int* unlink_order) {
  BEGIN_HELPER;
  const char* files[] = {
      "::coalesce/aaaaaaaa", "::coalesce/bbbbbbbb", "::coalesce/cccccccc",
      "::coalesce/dddddddd", "::coalesce/eeeeeeee",
  };
  int num_files = fbl::count_of(files);

  // Allocate a bunch of files in a directory
  ASSERT_EQ(mkdir("::coalesce", 0755), 0);
  for (int i = 0; i < num_files; i++) {
    fbl::unique_fd fd(open(files[i], O_RDWR | O_CREAT | O_EXCL, 0644));
    ASSERT_TRUE(fd);
  }

  // Unlink all those files in the order specified
  for (int i = 0; i < num_files; i++) {
    assert(0 <= unlink_order[i] && unlink_order[i] < num_files);
    ASSERT_EQ(unlink(files[unlink_order[i]]), 0);
  }

  ASSERT_EQ(rmdir("::coalesce"), 0);

  END_HELPER;
}

bool TestDirectoryCoalesce(void) {
  BEGIN_TEST;

  // Test some cases of coalescing, assuming the directory was filled
  // according to allocation order. If it wasn't, this test should still pass,
  // but there is no mechanism to check the "location of a direntry in a
  // directory", so this is our best shot at "poking" the filesystem to try to
  // coalesce.

  // Case 1: Test merge-with-left
  const int merge_with_left[] = {0, 1, 2, 3, 4};
  ASSERT_TRUE(TestDirectoryCoalesceHelper(merge_with_left));

  // Case 2: Test merge-with-right
  const int merge_with_right[] = {4, 3, 2, 1, 0};
  ASSERT_TRUE(TestDirectoryCoalesceHelper(merge_with_right));

  // Case 3: Test merge-with-both
  const int merge_with_both[] = {1, 3, 2, 0, 4};
  ASSERT_TRUE(TestDirectoryCoalesceHelper(merge_with_both));

  END_TEST;
}

// This test prevents the regression of an fsck bug, which could also
// occur in a filesystem which does similar checks at runtime.
//
// This test ensures that if multiple large direntries are created
// and coalesced, the 'last remaining entry' still has a valid size,
// even though it may be quite large.
bool TestDirectoryCoalesceLargeRecord(void) {
  BEGIN_TEST;

  char buf[NAME_MAX + 1];
  ASSERT_EQ(mkdir("::coalesce_lr", 0666), 0);
  fbl::unique_fd dirfd(open("::coalesce_lr", O_RDONLY | O_DIRECTORY));
  ASSERT_GT(dirfd.get(), 0);

  const int kNumEntries = 20;

  // Make the entries
  for (int i = 0; i < kNumEntries; i++) {
    memset(buf, 'a' + i, 50);
    buf[50] = '\0';
    ASSERT_EQ(mkdirat(dirfd.get(), buf, 0666), 0);
  }

  // Unlink all the entries except the last one
  for (int i = 0; i < kNumEntries - 1; i++) {
    memset(buf, 'a' + i, 50);
    buf[50] = '\0';
    ASSERT_EQ(unlinkat(dirfd.get(), buf, AT_REMOVEDIR), 0);
  }

  // Check that the 'large remaining entry', which may
  // have a fairly large size, isn't marked as 'invalid' by
  // fsck.
  if (test_info->can_be_mounted) {
    ASSERT_EQ(close(dirfd.release()), 0);
    ASSERT_TRUE(check_remount());
    dirfd.reset(open("::coalesce_lr", O_RDONLY | O_DIRECTORY));
    ASSERT_GT(dirfd.get(), 0);
  }

  // Unlink the final entry
  memset(buf, 'a' + kNumEntries - 1, 50);
  buf[50] = '\0';
  ASSERT_EQ(unlinkat(dirfd.get(), buf, AT_REMOVEDIR), 0);

  ASSERT_EQ(close(dirfd.release()), 0);
  ASSERT_EQ(rmdir("::coalesce_lr"), 0);

  END_TEST;
}

bool TestDirectoryTrailingSlash(void) {
  BEGIN_TEST;

  // We should be able to refer to directories with any number of trailing
  // slashes, and still refer to the same entity.
  ASSERT_EQ(mkdir("::a", 0755), 0);
  ASSERT_EQ(mkdir("::b/", 0755), 0);
  ASSERT_EQ(mkdir("::c//", 0755), 0);
  ASSERT_EQ(mkdir("::d///", 0755), 0);

  ASSERT_EQ(rmdir("::a///"), 0);
  ASSERT_EQ(rmdir("::b//"), 0);
  ASSERT_EQ(rmdir("::c/"), 0);

  // Before we unlink 'd', try renaming it using some trailing '/' characters.
  ASSERT_EQ(rename("::d", "::e"), 0);
  ASSERT_EQ(rename("::e", "::d/"), 0);
  ASSERT_EQ(rename("::d/", "::e"), 0);
  ASSERT_EQ(rename("::e/", "::d/"), 0);
  ASSERT_EQ(rmdir("::d"), 0);

  // We can make / unlink a file...
  fbl::unique_fd fd(open("::a", O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink("::a"), 0);

  // ... But we cannot refer to that file using a trailing '/'.
  fd.reset(open("::a", O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(open("::a/", O_RDWR, 0644), -1);

  // We can rename the file...
  ASSERT_EQ(rename("::a", "::b"), 0);
  // ... But neither the source (nor the destination) can have trailing slashes.
  ASSERT_EQ(rename("::b", "::a/"), -1);
  ASSERT_EQ(rename("::b/", "::a"), -1);
  ASSERT_EQ(rename("::b/", "::a/"), -1);
  ASSERT_EQ(unlink("::b/"), -1);

  ASSERT_EQ(unlink("::b"), 0);

  END_TEST;
}

bool TestDirectoryReaddir(void) {
  BEGIN_TEST;

  ASSERT_EQ(mkdir("::a", 0755), 0);
  ASSERT_EQ(mkdir("::a", 0755), -1);

  expected_dirent_t empty_dir[] = {
      {false, ".", DT_DIR},
  };
  ASSERT_TRUE(check_dir_contents("::a", empty_dir, fbl::count_of(empty_dir)));

  ASSERT_EQ(mkdir("::a/dir1", 0755), 0);
  fbl::unique_fd fd(open("::a/file1", O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);

  fd.reset(open("::a/file2", O_RDWR | O_CREAT | O_EXCL, 0644));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);

  ASSERT_EQ(mkdir("::a/dir2", 0755), 0);
  expected_dirent_t filled_dir[] = {
      {false, ".", DT_DIR},     {false, "dir1", DT_DIR},  {false, "dir2", DT_DIR},
      {false, "file1", DT_REG}, {false, "file2", DT_REG},
  };
  ASSERT_TRUE(check_dir_contents("::a", filled_dir, fbl::count_of(filled_dir)));

  ASSERT_EQ(rmdir("::a/dir2"), 0);
  ASSERT_EQ(unlink("::a/file2"), 0);
  expected_dirent_t partial_dir[] = {
      {false, ".", DT_DIR},
      {false, "dir1", DT_DIR},
      {false, "file1", DT_REG},
  };
  ASSERT_TRUE(check_dir_contents("::a", partial_dir, fbl::count_of(partial_dir)));

  ASSERT_EQ(rmdir("::a/dir1"), 0);
  ASSERT_EQ(unlink("::a/file1"), 0);
  ASSERT_TRUE(check_dir_contents("::a", empty_dir, fbl::count_of(empty_dir)));
  ASSERT_EQ(unlink("::a"), 0);

  END_TEST;
}

// Create a directory named "::dir" with entries "00000", "00001" ... up to
// num_entries.
bool large_dir_setup(size_t num_entries) {
  ASSERT_EQ(mkdir("::dir", 0755), 0);

  // Create a large directory (ideally, large enough that our libc
  // implementation can't cache the entire contents of the directory
  // with one 'getdirents' call).
  for (size_t i = 0; i < num_entries; i++) {
    char dirname[100];
    snprintf(dirname, 100, "::dir/%05lu", i);
    ASSERT_EQ(mkdir(dirname, 0755), 0);
  }

  DIR* dir = opendir("::dir");
  ASSERT_NONNULL(dir);

  // As a sanity check, it should contain all then entries we made
  struct dirent* de;
  size_t num_seen = 0;
  size_t i = 0;
  while ((de = readdir(dir)) != NULL) {
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
      // Ignore these entries
      continue;
    }
    char dirname[100];
    snprintf(dirname, 100, "%05lu", i++);
    ASSERT_EQ(strcmp(de->d_name, dirname), 0, "Unexpected dirent");
    num_seen++;
  }
  ASSERT_EQ(closedir(dir), 0);

  return true;
}

bool TestDirectoryReaddirRmAll(void) {
  BEGIN_TEST;

  size_t num_entries = 1000;
  ASSERT_TRUE(large_dir_setup(num_entries));

  DIR* dir = opendir("::dir");
  ASSERT_NONNULL(dir);

  // Unlink all the entries as we read them.
  struct dirent* de;
  size_t num_seen = 0;
  size_t i = 0;
  while ((de = readdir(dir)) != NULL) {
    if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) {
      // Ignore these entries
      continue;
    }
    char dirname[100];
    snprintf(dirname, 100, "%05lu", i++);
    ASSERT_EQ(strcmp(de->d_name, dirname), 0, "Unexpected dirent");
    ASSERT_EQ(unlinkat(dirfd(dir), dirname, AT_REMOVEDIR), 0);
    num_seen++;
  }

  ASSERT_EQ(num_seen, num_entries, "Did not see all expected entries");
  ASSERT_EQ(closedir(dir), 0);
  ASSERT_EQ(rmdir("::dir"), 0, "Could not unlink containing directory");
  END_TEST;
}

bool TestDirectoryRewind(void) {
  BEGIN_TEST;

  ASSERT_EQ(mkdir("::a", 0755), 0);
  expected_dirent_t empty_dir[] = {
      {false, ".", DT_DIR},
  };

  DIR* dir = opendir("::a");
  ASSERT_NONNULL(dir);

  // We should be able to repeatedly access the directory without
  // re-opening it.
  ASSERT_TRUE(fcheck_dir_contents(dir, empty_dir, fbl::count_of(empty_dir)));
  ASSERT_TRUE(fcheck_dir_contents(dir, empty_dir, fbl::count_of(empty_dir)));

  ASSERT_EQ(mkdirat(dirfd(dir), "b", 0755), 0);
  ASSERT_EQ(mkdirat(dirfd(dir), "c", 0755), 0);

  // We should be able to modify the directory and re-process it without
  // re-opening it.
  expected_dirent_t dir_contents[] = {
      {false, ".", DT_DIR},
      {false, "b", DT_DIR},
      {false, "c", DT_DIR},
  };
  ASSERT_TRUE(fcheck_dir_contents(dir, dir_contents, fbl::count_of(dir_contents)));
  ASSERT_TRUE(fcheck_dir_contents(dir, dir_contents, fbl::count_of(dir_contents)));

  ASSERT_EQ(rmdir("::a/b"), 0);
  ASSERT_EQ(rmdir("::a/c"), 0);

  ASSERT_TRUE(fcheck_dir_contents(dir, empty_dir, fbl::count_of(empty_dir)));
  ASSERT_TRUE(fcheck_dir_contents(dir, empty_dir, fbl::count_of(empty_dir)));

  ASSERT_EQ(closedir(dir), 0);
  ASSERT_EQ(rmdir("::a"), 0);

  END_TEST;
}

bool TestDirectoryAfterRmdir(void) {
  BEGIN_TEST;

  expected_dirent_t empty_dir[] = {
      {false, ".", DT_DIR},
  };

  // Make a directory...
  ASSERT_EQ(mkdir("::dir", 0755), 0);
  DIR* dir = opendir("::dir");
  ASSERT_NONNULL(dir);
  // We can make and delete subdirectories, since "::dir" exists...
  ASSERT_EQ(mkdir("::dir/subdir", 0755), 0);
  ASSERT_EQ(rmdir("::dir/subdir"), 0);
  ASSERT_TRUE(fcheck_dir_contents(dir, empty_dir, fbl::count_of(empty_dir)));

  // Remove the directory. It's still open, so it should appear empty.
  ASSERT_EQ(rmdir("::dir"), 0);
  ASSERT_TRUE(fcheck_dir_contents(dir, nullptr, 0));

  // But we can't make new files / directories, by path...
  ASSERT_EQ(mkdir("::dir/subdir", 0755), -1);
  // ... Or with the open fd.
  fbl::unique_fd fd(dirfd(dir));
  ASSERT_TRUE(fd);
  ASSERT_EQ(openat(fd.get(), "file", O_CREAT | O_RDWR), -1, "Can't make new files in deleted dirs");
  ASSERT_EQ(mkdirat(fd.get(), "dir", 0755), -1, "Can't make new files in deleted dirs");

  // In fact, the "dir" path should still be usable, even as a file!
  fbl::unique_fd fd2(open("::dir", O_CREAT | O_EXCL | O_RDWR));
  ASSERT_TRUE(fd2);
  ASSERT_TRUE(fcheck_dir_contents(dir, nullptr, 0));
  ASSERT_EQ(close(fd2.release()), 0);
  ASSERT_EQ(unlink("::dir"), 0);

  // After all that, dir still looks like an empty directory...
  ASSERT_TRUE(fcheck_dir_contents(dir, nullptr, 0));
  ASSERT_EQ(closedir(dir), 0);

  END_TEST;
}

bool TestRenameIntoUnlinkedDirectoryFails() {
  BEGIN_TEST;

  ASSERT_EQ(mkdir("::foo", 0755), 0, "");
  fbl::unique_fd foo_fd(open("::foo", O_RDONLY | O_DIRECTORY, 0644));
  ASSERT_TRUE(foo_fd);
  fbl::unique_fd baz_fd(open("::baz", O_CREAT | O_RDWR));
  ASSERT_TRUE(baz_fd);
  fbl::unique_fd root_fd(open("::", O_RDONLY | O_DIRECTORY, 0644));
  ASSERT_TRUE(root_fd);
  ASSERT_EQ(renameat(root_fd.get(), "baz", foo_fd.get(), "baz"), 0);
  ASSERT_EQ(renameat(foo_fd.get(), "baz", root_fd.get(), "baz"), 0);
  ASSERT_EQ(unlink("::foo"), 0);
  ASSERT_EQ(renameat(root_fd.get(), "baz", foo_fd.get(), "baz"), -1);
  ASSERT_EQ(errno, EPIPE);  // ZX_ERR_BAD_STATE maps to EPIPE, for now.

  END_TEST;
}

RUN_FOR_ALL_FILESYSTEMS(
    directory_tests,
    RUN_TEST_MEDIUM(TestDirectoryCoalesce)
    RUN_TEST_MEDIUM(TestDirectoryCoalesceLargeRecord)
    RUN_TEST_MEDIUM(TestDirectoryFilenameMax)
    RUN_TEST_LARGE(TestDirectoryLarge)
    RUN_TEST_MEDIUM(TestDirectoryTrailingSlash)
    RUN_TEST_MEDIUM(TestDirectoryReaddir)
    RUN_TEST_LARGE(TestDirectoryReaddirRmAll)
    RUN_TEST_MEDIUM(TestDirectoryRewind)
    RUN_TEST_MEDIUM(TestDirectoryAfterRmdir)
    RUN_TEST_MEDIUM(TestRenameIntoUnlinkedDirectoryFails))

// TODO(smklein): Run this when MemFS can execute it without causing an OOM
#if 0
    RUN_TEST_LARGE(TestDirectoryMax)
#endif
